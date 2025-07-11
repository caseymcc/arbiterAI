#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/providers/openai.h"
#include "arbiterAI/providers/anthropic.h"
#include "arbiterAI/providers/deepseek.h"
#include "arbiterAI/providers/llama.h"

#include <memory>

namespace arbiterAI
{

namespace
{
class arbiterAI_
{
public:
    static arbiterAI_ &instance()
    {
        static arbiterAI_ instance;
        return instance;
    }

    arbiterAI_()=default;

    bool initialized=false;
    std::map<std::string, std::unique_ptr<BaseProvider>> providers;
};
}

arbiterAI::arbiterAI()
{
}

arbiterAI::~arbiterAI()
{
}

ErrorCode arbiterAI::initialize(const std::vector<std::filesystem::path> &configPaths)
{
    if(!ModelManager::instance().initialize(configPaths))
    {
        return ErrorCode::InvalidRequest;
    }

    arbiterAI_::instance().initialized=true;
    return ErrorCode::Success;
}

bool arbiterAI::doesModelNeedApiKey(const std::string &model)
{
    auto provider=ModelManager::instance().getProvider(model);

    if(!provider)
    {
        return false;
    }

    return *provider=="openai";
}

bool arbiterAI::supportModelDownload(const std::string &provider)
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

namespace
{
BaseProvider *getProvider(const std::string &providerName)
{
    auto &hermes=arbiterAI_::instance();

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
        auto models=ModelManager::instance().getModels(providerName);
        provider->initialize(models);
        it=hermes.providers.emplace(providerName, std::move(provider)).first;
    }

    return it->second.get();
}
}

ErrorCode arbiterAI::completion(const CompletionRequest &request, CompletionResponse &response)
{
    if(!arbiterAI_::instance().initialized)
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

ErrorCode arbiterAI::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    if(!arbiterAI_::instance().initialized)
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

ErrorCode arbiterAI::getEmbeddings(const EmbeddingRequest &request, EmbeddingResponse &response)
{
    if(!arbiterAI_::instance().initialized)
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

ErrorCode arbiterAI::getDownloadStatus(const std::string &modelName, std::string &error)
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
            return ErrorCode::ModelDownloadFailed;
        }
    }
    return ErrorCode::UnsupportedProvider;
}


} // namespace arbiterAI
