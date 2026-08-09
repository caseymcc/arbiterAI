#include "arbiterAI/providers/onnxGenai.h"
#include "arbiterAI/storageManager.h"

#include <ort_genai.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>

namespace arbiterAI
{

// Definition lives here so the header need not see OGA's types.
struct OnnxGenai::LoadedOnnxModelData {
    std::shared_ptr<OgaModel> model;
    std::shared_ptr<OgaTokenizer> tokenizer;
};

namespace
{

/// Loading an ONNX model is expensive, so keep them alive per directory.
/// Keyed by the resolved model path; entries live until clearCache().
std::mutex g_cacheMutex;
std::map<std::string, OnnxGenai::LoadedOnnxModelData> g_cache;

/// Resolve the model directory: absolute paths as given, otherwise relative to
/// the configured models dir.
std::string resolveModelDir(const ModelInfo &modelInfo)
{
    if(!modelInfo.filePath.has_value()||modelInfo.filePath->empty())
    {
        return {};
    }

    std::filesystem::path path(modelInfo.filePath.value());
    if(path.is_absolute())
    {
        return path.string();
    }

    std::filesystem::path modelsDir=StorageManager::instance().getModelsDir();
    if(modelsDir.empty())
    {
        return path.string();
    }
    return (modelsDir/path).string();
}

/// Messages as the JSON array OGA's chat template expects.
std::string messagesToJson(const std::vector<Message> &messages)
{
    nlohmann::json array=nlohmann::json::array();
    for(const Message &message:messages)
    {
        array.push_back({{"role", message.role}, {"content", message.content}});
    }
    return array.dump();
}

} // namespace

OnnxGenai::OnnxGenai():
    BaseProvider("onnx-genai")
{
}

OnnxGenai::~OnnxGenai()
{
}

void OnnxGenai::clearCache()
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_cache.clear();
}

bool OnnxGenai::acquireModel(const ModelInfo &modelInfo, LoadedOnnxModel &out)
{
    std::string modelDir=resolveModelDir(modelInfo);
    if(modelDir.empty())
    {
        m_lastErrorDetail="model '"+modelInfo.model+"' has no file_path; onnx-genai needs the "
            "directory holding genai_config.json";
        return false;
    }
    if(!std::filesystem::exists(std::filesystem::path(modelDir)/"genai_config.json"))
    {
        m_lastErrorDetail="no genai_config.json in '"+modelDir+"' — an ONNX Runtime GenAI model is a "
            "directory, not a single file";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it=g_cache.find(modelDir);
        if(it!=g_cache.end())
        {
            out.model=it->second.model;
            out.tokenizer=it->second.tokenizer;
            return true;
        }
    }

    try
    {
        std::unique_ptr<OgaConfig> config=OgaConfig::Create(modelDir.c_str());

        // Execution providers are the reason this provider exists: ONNX Runtime
        // reaches accelerators llama.cpp cannot, and which one is used is a
        // runtime choice.  Absent config we leave the model's own
        // genai_config.json alone.
        const nlohmann::json &options=modelInfo.onnxOptions;
        if(options.is_object()&&options.contains("execution_providers")&&
            options["execution_providers"].is_array()&&!options["execution_providers"].empty())
        {
            config->ClearProviders();
            for(const auto &provider:options["execution_providers"])
            {
                if(!provider.is_string()) continue;

                std::string name=provider.get<std::string>();
                config->AppendProvider(name.c_str());
                spdlog::info("[onnx-genai] execution provider: {}", name);

                if(options.contains("provider_options")&&
                    options["provider_options"].is_object()&&
                    options["provider_options"].contains(name)&&
                    options["provider_options"][name].is_object())
                {
                    for(auto it=options["provider_options"][name].begin();
                        it!=options["provider_options"][name].end(); ++it)
                    {
                        std::string value=it.value().is_string()
                            ?it.value().get<std::string>()
                            :it.value().dump();
                        config->SetProviderOption(name.c_str(), it.key().c_str(), value.c_str());
                    }
                }
            }
        }

        auto start=std::chrono::steady_clock::now();

        LoadedOnnxModelData entry;
        entry.model=std::shared_ptr<OgaModel>(OgaModel::Create(*config).release());
        entry.tokenizer=std::shared_ptr<OgaTokenizer>(OgaTokenizer::Create(*entry.model).release());

        double loadMs=std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now()-start).count();
        spdlog::info("[onnx-genai] loaded '{}' from {} in {:.0f}ms", modelInfo.model, modelDir, loadMs);

        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            g_cache[modelDir]=entry;
        }
        out.model=entry.model;
        out.tokenizer=entry.tokenizer;
        return true;
    }
    catch(const std::exception &e)
    {
        m_lastErrorDetail=std::string("failed to load ONNX model from '")+modelDir+"': "+e.what();
        spdlog::error("[onnx-genai] {}", m_lastErrorDetail);
        return false;
    }
}

ErrorCode OnnxGenai::generate(const CompletionRequest &request, const ModelInfo &modelInfo,
    const LoadedOnnxModel &loaded,
    std::string &result, int &promptTokens, int &completionTokens,
    const std::function<void(const std::string &)> &onChunk)
{
    const nlohmann::json &options=modelInfo.onnxOptions;

    try
    {
        // Prefer the model's own chat template, same as the llama path.
        std::string prompt;
        try
        {
            OgaString templated=loaded.tokenizer->ApplyChatTemplate(
                nullptr, messagesToJson(request.messages).c_str(), nullptr, true);
            prompt=std::string(templated);
        }
        catch(const std::exception &e)
        {
            spdlog::warn("[onnx-genai] no usable chat template ({}); concatenating messages", e.what());
            for(const Message &message:request.messages)
            {
                prompt+=message.role+": "+message.content+"\n";
            }
            prompt+="assistant: ";
        }

        std::unique_ptr<OgaSequences> sequences=OgaSequences::Create();
        loaded.tokenizer->Encode(prompt.c_str(), *sequences);
        promptTokens=static_cast<int>(sequences->SequenceCount(0));

        int maxOutputTokens=request.max_tokens.value_or(modelInfo.maxOutputTokens);
        if(maxOutputTokens<=0) maxOutputTokens=512;

        std::unique_ptr<OgaGeneratorParams> params=OgaGeneratorParams::Create(*loaded.model);

        // max_length bounds prompt + output, which is not what max_tokens means.
        int maxLength=promptTokens+maxOutputTokens;
        if(options.is_object()&&options.contains("max_length")&&options["max_length"].is_number_integer())
        {
            maxLength=std::min(maxLength, options["max_length"].get<int>());
        }
        params->SetSearchOption("max_length", static_cast<double>(maxLength));

        if(request.temperature.has_value()&&*request.temperature>0.0)
        {
            params->SetSearchOptionBool("do_sample", true);
            params->SetSearchOption("temperature", *request.temperature);
        }
        if(request.top_p.has_value())
        {
            params->SetSearchOption("top_p", *request.top_p);
        }

        std::unique_ptr<OgaGenerator> generator=OgaGenerator::Create(*loaded.model, *params);
        generator->AppendTokenSequences(*sequences);

        std::unique_ptr<OgaTokenizerStream> stream=OgaTokenizerStream::Create(*loaded.tokenizer);

        completionTokens=0;
        while(!generator->IsDone())
        {
            generator->GenerateNextToken();

            // auto: the wrapper returns std::vector under C++17 and std::span
            // under C++20, so naming the type would pin us to one standard.
            auto next=generator->GetNextTokens();
            if(next.empty()) continue;

            const char *piece=stream->Decode(next[0]);
            if(piece&&*piece)
            {
                result+=piece;
                if(onChunk) onChunk(piece);
            }
            completionTokens++;

            if(completionTokens>=maxOutputTokens) break;
        }

        return ErrorCode::Success;
    }
    catch(const std::exception &e)
    {
        m_lastErrorDetail=std::string("generation failed: ")+e.what();
        spdlog::error("[onnx-genai] {}", m_lastErrorDetail);
        return ErrorCode::GenerationError;
    }
}

ErrorCode OnnxGenai::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    LoadedOnnxModel loaded;
    if(!acquireModel(model, loaded))
    {
        return ErrorCode::ModelLoadError;
    }

    std::string text;
    int promptTokens=0;
    int completionTokens=0;

    auto start=std::chrono::steady_clock::now();
    ErrorCode code=generate(request, model, loaded, text, promptTokens, completionTokens, nullptr);
    double totalMs=std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now()-start).count();

    if(code!=ErrorCode::Success)
    {
        return code;
    }

    spdlog::info("[onnx-genai] completion: prompt={} gen={} in {:.0f}ms ({:.1f} tok/s)",
        promptTokens, completionTokens, totalMs,
        totalMs>0.0?(completionTokens/(totalMs/1000.0)):0.0);

    response.text=text;
    response.provider="onnx-genai";
    response.model=request.model;
    response.usage.prompt_tokens=promptTokens;
    response.usage.completion_tokens=completionTokens;
    response.usage.total_tokens=promptTokens+completionTokens;
    response.finishReason="stop";
    return ErrorCode::Success;
}

ErrorCode OnnxGenai::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(request.model);
    if(!modelInfo)
    {
        return ErrorCode::ModelNotFound;
    }

    LoadedOnnxModel loaded;
    if(!acquireModel(*modelInfo, loaded))
    {
        return ErrorCode::ModelLoadError;
    }

    std::string text;
    int promptTokens=0;
    int completionTokens=0;
    return generate(request, *modelInfo, loaded, text, promptTokens, completionTokens, callback);
}

ErrorCode OnnxGenai::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback,
    std::function<void()> waitCallback)
{
    (void)waitCallback;
    return streamingCompletion(request, callback);
}

ErrorCode OnnxGenai::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    (void)request;
    (void)response;
    return ErrorCode::NotImplemented;
}

DownloadStatus OnnxGenai::getDownloadStatus(const std::string &modelName,
    std::string &error)
{
    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(modelName);
    if(!modelInfo)
    {
        error="Model not found: "+modelName;
        return DownloadStatus::NotStarted;
    }

    std::string modelDir=resolveModelDir(*modelInfo);
    if(!modelDir.empty()&&
        std::filesystem::exists(std::filesystem::path(modelDir)/"genai_config.json"))
    {
        return DownloadStatus::Completed;
    }

    error="ONNX model directory not present: "+modelDir;
    return DownloadStatus::NotStarted;
}

ErrorCode OnnxGenai::getAvailableModels(std::vector<std::string> &models)
{
    for(const ModelInfo &info:ModelManager::instance().getModels("onnx-genai"))
    {
        models.push_back(info.model);
    }
    return ErrorCode::Success;
}

} // namespace arbiterAI
