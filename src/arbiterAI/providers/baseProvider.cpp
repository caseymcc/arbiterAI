#include "arbiterAI/providers/baseProvider.h"
#ifdef ARBITERAI_ENABLE_LLAMA
#include "arbiterAI/modelRuntime.h"
#endif
#include "arbiterAI/modelManager.h"
#include "arbiterAI/modelDownloader.h"
#include "arbiterAI/storageManager.h"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <string>
#include <future>
#include <vector>

namespace arbiterAI
{

BaseProvider::BaseProvider(std::string provider)
    : m_provider(provider)
{
}

std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::toupper(c); }
    );
    return s;
}

ErrorCode BaseProvider::getApiKey(const std::string &modelName,
    const std::optional<std::string> &requestApiKey, std::string &apiKey)
{
    // 1. Check the request itself
    if(requestApiKey.has_value()&&!requestApiKey.value().empty())
    {
        apiKey=requestApiKey.value();
        return ErrorCode::Success;
    }

    // 2. Check if API key was set via setApiKey()
    if(!m_apiKey.empty())
    {
        apiKey=m_apiKey;
        return ErrorCode::Success;
    }

    // 3. Check the model info from ModelManager
    auto modelInfo=ModelManager::instance().getModelInfo(modelName);
    if(modelInfo&&modelInfo->apiKey.has_value()&&!modelInfo->apiKey.value().empty())
    {
        apiKey=modelInfo->apiKey.value();
        return ErrorCode::Success;
    }

    // 4. Fallback to environment variables
    if(!m_provider.empty())
    {
        std::string envVarName=to_upper(m_provider)+"_API_KEY";
        if(const char *key=std::getenv(envVarName.c_str()))
        {
            apiKey=key;
            return ErrorCode::Success;
        }
    }

    return ErrorCode::ApiKeyNotFound;
}


std::string BaseProvider::resolveDownloadableModelFile(const ModelInfo &model)
{
    namespace fs=std::filesystem;

    // 1. Explicit local file that already exists wins.
    if(model.filePath.has_value() && !model.filePath->empty()
        && fs::exists(model.filePath.value()))
    {
        return model.filePath.value();
    }

    // 2. Download from the primary variant's configured files if present.
    if(!model.variants.empty())
    {
        const ModelVariant &variant=model.variants.front();
        std::vector<VariantDownload> files=variant.getAllFiles();
        std::string primaryFilename=variant.getPrimaryFilename();

        if(!files.empty() && !primaryFilename.empty())
        {
            fs::path modelsDir=StorageManager::instance().getModelsDir();
            if(modelsDir.empty())
            {
                spdlog::warn("{} provider: no models directory configured for auto-download of '{}'",
                    m_provider, model.model);
            }
            else
            {
                ModelDownloader downloader;
                for(const VariantDownload &file:files)
                {
                    if(file.url.empty() || file.filename.empty())
                        continue;
                    fs::path localPath=modelsDir/file.filename;
                    if(fs::exists(localPath))
                        continue;
                    spdlog::info("{} provider: downloading '{}' -> '{}'",
                        m_provider, file.url, localPath.string());
                    std::optional<std::string> hash;
                    if(!file.sha256.empty()) hash=file.sha256;
                    auto fut=downloader.downloadModel(file.url, localPath.string(), hash);
                    if(!fut.get())
                    {
                        spdlog::error("{} provider: download failed for '{}'", m_provider, file.url);
                        return {};
                    }
                }
                return (modelsDir/primaryFilename).string();
            }
        }
    }

    // 3. Fall back to file_path (may not exist / be empty — caller handles it).
    return model.filePath.value_or(std::string{});
}

DownloadStatus BaseProvider::getDownloadStatus(const std::string &modelName, std::string &error)
{
    // Default implementation for cloud providers - no download needed
    return DownloadStatus::NotApplicable;
}

ErrorCode BaseProvider::getDownloadProgress(const std::string &modelName, DownloadProgress &progress)
{
    // Default implementation for cloud providers
    progress.status = DownloadStatus::NotApplicable;
    progress.modelName = modelName;
    progress.bytesDownloaded = 0;
    progress.totalBytes = 0;
    progress.percentComplete = 0.0f;
    progress.errorMessage.clear();
    return ErrorCode::Success;
}

ErrorCode BaseProvider::getAvailableModels(std::vector<std::string>& models)
{
    return ErrorCode::NotImplemented;
}

ErrorCode BaseProvider::transcribe(const AudioTranscriptionRequest &request,
    const ModelInfo &model,
    AudioTranscriptionResponse &response)
{
    // Default: provider does not support speech-to-text
    return ErrorCode::NotImplemented;
}

ErrorCode BaseProvider::embedAudio(const AudioEmbeddingRequest &request,
    const ModelInfo &model,
    AudioEmbeddingResponse &response)
{
    // Default: provider does not support speaker embeddings
    return ErrorCode::NotImplemented;
}

ErrorCode BaseProvider::classifyAudio(const AudioClassificationRequest &request,
    const ModelInfo &model,
    AudioClassificationResponse &response)
{
    // Default: provider does not support audio classification
    return ErrorCode::NotImplemented;
}

ErrorCode BaseProvider::synthesizeSpeech(const SpeechRequest &request,
    const ModelInfo &model,
    SpeechResponse &response)
{
    // Default: provider does not support text-to-speech
    return ErrorCode::NotImplemented;
}

ErrorCode BaseProvider::generateImage(const ImageGenerationRequest &request,
    const ModelInfo &model,
    ImageGenerationResponse &response)
{
    // Default: provider does not support image generation
    return ErrorCode::NotImplemented;
}

std::vector<CompletionResponse> BaseProvider::batchCompletion(const std::vector<CompletionRequest> &requests)
{
    std::vector<std::future<CompletionResponse>> futures;
    futures.reserve(requests.size());

    for(const auto &req:requests)
    {
        futures.emplace_back(std::async(std::launch::async, [this, req]()
            {
                CompletionResponse resp;
                auto modelInfo=ModelManager::instance().getModelInfo(req.model);
                if(modelInfo)
                {
                    this->completion(req, *modelInfo, resp);
                    resp.cost=(resp.usage.prompt_tokens*modelInfo->pricing.prompt_token_cost)+(resp.usage.completion_tokens*modelInfo->pricing.completion_token_cost);
                }
                return resp;
            }));
    }

    std::vector<CompletionResponse> responses;
    responses.reserve(requests.size());
    for(auto &fut:futures)
    {
        responses.push_back(fut.get());
    }

    return responses;
}

ErrorCode BaseProvider::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback,
    std::function<void()> waitCallback)
{
    // Default: ignore waitCallback, delegate to standard streaming
    return streamingCompletion(request, callback);
}

void BaseProvider::notifyModelLoaded(const std::string &model, int ramMb) const
{
#ifdef ARBITERAI_ENABLE_LLAMA
    // ModelRuntime holds the loaded-model registry, and it is only built with
    // the llama runtime.  Without it there is no such list to appear in.
    std::string mode;
    std::optional<ModelInfo> info=ModelManager::instance().getModelInfo(model);
    if(info)
    {
        mode=info->mode;
    }
    ModelRuntime::instance().registerProviderModel(model, m_provider, mode, ramMb);
#else
    (void)model;
    (void)ramMb;
#endif
}

void BaseProvider::notifyModelReleased(const std::string &model) const
{
#ifdef ARBITERAI_ENABLE_LLAMA
    ModelRuntime::instance().unregisterProviderModel(model);
#else
    (void)model;
#endif
}

} // namespace arbiterAI
