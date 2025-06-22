#include "hermesaxiom/hermesaxiom.h"
#include "hermesaxiom/modelManager.h"
#include "hermesaxiom/providers/openai_llm.h"
#include "hermesaxiom/providers/anthropic_llm.h"
#include "hermesaxiom/providers/deepseek_llm.h"
#include "hermesaxiom/providers/llama_llm.h"

#include <memory>

namespace hermesaxiom
{

struct HermesAxiom
{
    static HermesAxiom &instance()
    {
        static HermesAxiom instance;
        return instance;
    }

    bool initialized=false;
    std::map<std::string, std::unique_ptr<BaseLLM>> llms;
};

ErrorCode initialize(const std::vector<std::filesystem::path> &configPaths)
{
    if(!ModelManager::instance().initialize(configPaths))
    {
        return ErrorCode::InvalidRequest;
    }

    HermesAxiom::instance().initialized=true;
    return ErrorCode::Success;
}

bool doesModelNeedApiKey(const std::string &model)
{
    auto provider=ModelManager::instance().getProvider(model);

    if(!provider)
    {
        return false;
    }

    return *provider=="openai";
}

bool supportModelDownload(const std::string &provider)
{
    if(provider=="llama")
    {
        return true;
    }
    return false;
}

std::unique_ptr<BaseLLM> createLLM(const std::string& provider)
{
    if(provider=="openai")
    {
        return std::make_unique<OpenAILLM>();
    }
    else if(provider=="anthropic")
    {
        return std::make_unique<AnthropicLLM>();
    }
    else if(provider=="deepseek")
    {
        return std::make_unique<DeepseekLLM>();
    }
    else if(provider=="llama")
    {
        return std::make_unique<LlamaLLM>();
    }
    return nullptr;
}

BaseLLM *getLLM(const std::string &provider)
{
    auto &hermes=HermesAxiom::instance();

    // Check if we already have an LLM instance for this provider
    auto it=hermes.llms.find(provider);

    if(it==hermes.llms.end())
    {
        // Create new LLM instance
        auto llm=createLLM(provider);
        if(!llm)
        {
            return nullptr;
        }
        it=hermes.llms.emplace(provider, std::move(llm)).first;
    }

    return it->second.get();
}

ErrorCode completion(const CompletionRequest &request, CompletionResponse &response)
{
    if(!HermesAxiom::instance().initialized)
    {
        return ErrorCode::InvalidRequest;
    }

    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(request.model);
    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    BaseLLM *llm=getLLM(modelInfo->provider);

    if(!llm)
    {
        return ErrorCode::UnsupportedProvider;
    }

    return llm->completion(request, response);
}

ErrorCode streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    if(!HermesAxiom::instance().initialized)
    {
        return ErrorCode::InvalidRequest;
    }

    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(request.model);
    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    BaseLLM *llm=getLLM(modelInfo->provider);

    if(!llm)
    {
        return ErrorCode::UnsupportedProvider;
    }

    return llm->streamingCompletion(request, callback);
}

ErrorCode getEmbeddings(const EmbeddingRequest &request, EmbeddingResponse &response)
{
    if(!HermesAxiom::instance().initialized)
    {
        return ErrorCode::InvalidRequest;
    }

    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(request.model);
    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    BaseLLM *llm=getLLM(modelInfo->provider);

    if(!llm)
    {
        return ErrorCode::UnsupportedProvider;
    }

    return llm->getEmbeddings(request, response);
}

ErrorCode getDownloadStatus(const std::string &modelName, std::string &error)
{
    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(modelName);
    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    BaseLLM *llm=getLLM(modelInfo->provider);

    if(llm)
    {
        auto status=llm->getDownloadStatus(modelName, error);
        switch(status)
        {
        case DownloadStatus::NotStarted:
            return ErrorCode::Success;
        case DownloadStatus::InProgress:
            return ErrorCode::ModelDownloading;
        case DownloadStatus::Completed:
            return ErrorCode::Success;
        case DownloadStatus::Failed:
            return ErrorCode::DownloadFailed;
        }
    }
    return ErrorCode::UnsupportedProvider;
}

} // namespace hermesaxiom
