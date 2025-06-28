#include "hermesaxiom/hermesaxiom.h"
#include "hermesaxiom/modelManager.h"
#include "hermesaxiom/providers/openai.h"
#include "hermesaxiom/providers/anthropic.h"
#include "hermesaxiom/providers/deepseek.h"
#include "hermesaxiom/providers/llama.h"

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
    std::map<std::string, std::unique_ptr<BaseProvider>> providers;
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

std::unique_ptr<BaseProvider> createProvider(const std::string &provider)
{
    if(provider=="openai")
    {
        return std::make_unique<OpenAI>();
    }
    else if(provider=="anthropic")
    {
        return std::make_unique<Anthropic>();
    }
    else if(provider=="deepseek")
    {
        return std::make_unique<Deepseek>();
    }
    else if(provider=="llama")
    {
        return std::make_unique<Llama>();
    }
    return nullptr;
}

BaseProvider *getProvider(const std::string &providerName)
{
    auto &hermes=HermesAxiom::instance();

    // Check if we already have an Provider instance for this provider
    auto it=hermes.providers.find(providerName);

    if(it==hermes.providers.end())
    {
        // Create new Provider instance
        auto provider=createProvider(providerName);

        if(!provider)
        {
            return nullptr;
        }
        it=hermes.providers.emplace(providerName, std::move(provider)).first;
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

    BaseProvider *provider=getProvider(modelInfo->provider);

    if(!provider)
    {
        return ErrorCode::UnsupportedProvider;
    }

    return provider->completion(request, response);
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

    BaseProvider *provider=getProvider(modelInfo->provider);

    if(!provider)
    {
        return ErrorCode::UnsupportedProvider;
    }

    return provider->streamingCompletion(request, callback);
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

    BaseProvider *provider=getProvider(modelInfo->provider);

    if(!provider)
    {
        return ErrorCode::UnsupportedProvider;
    }

    return provider->getEmbeddings(request, response);
}

ErrorCode getDownloadStatus(const std::string &modelName, std::string &error)
{
    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(modelName);
    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    BaseProvider *provider=getProvider(modelInfo->provider);

    if(provider)
    {
        auto status=provider->getDownloadStatus(modelName, error);
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
