#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/cacheManager.h"
#include "arbiterAI/costManager.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/providers/openai.h"
#include "arbiterAI/providers/anthropic.h"
#include "arbiterAI/providers/deepseek.h"
#include "arbiterAI/providers/llama.h"
#include "arbiterAI/providers/openrouter.h"

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

arbiterAI::arbiterAI(
    bool enableCache,
    const std::filesystem::path &cacheDir,
    std::chrono::seconds ttl,
    double spendingLimit,
    const std::filesystem::path &costStateFile
)
{
    if(enableCache)
    {
        m_cacheManager=std::make_unique<CacheManager>(cacheDir, ttl);
    }
    if(spendingLimit>=0.0&&!costStateFile.empty())
    {
        m_costManager=std::make_unique<CostManager>(costStateFile, spendingLimit);
    }
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
    else if(provider=="openrouter")
    {
        return std::make_unique<OpenRouter_LLM>();
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

    if(m_cacheManager)
    {
        auto cachedResponse=m_cacheManager->get(request);
        if(cachedResponse)
        {
            response=*cachedResponse;
            return ErrorCode::Success;
        }
    }

    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(request.model);
    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    if(m_costManager)
    {
        double estimatedCost=0.0;
        if(modelInfo->pricing.prompt_token_cost>0)
        {
            // A very simple estimation
            int totalPromptTokens=0;
            for(const auto &msg:request.messages)
            {
                totalPromptTokens+=msg.content.length()/4; // Rough estimate
            }
            estimatedCost=totalPromptTokens*modelInfo->pricing.prompt_token_cost;
        }
        if(!m_costManager->canProceed(estimatedCost))
        {
            return ErrorCode::GenerationError; // Or a more specific error code
        }
    }

    BaseProvider *provider=getProvider(modelInfo->provider);

    if(!provider)
    {
        return ErrorCode::UnsupportedProvider;
    }

    auto result=provider->completion(request, *modelInfo, response);

    if(result==ErrorCode::Success)
    {
        if(m_cacheManager)
        {
            m_cacheManager->put(request, response);
        }
        if(m_costManager)
        {
            m_costManager->recordCost(response.cost);
        }
    }

    return result;
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

std::vector<CompletionResponse> arbiterAI::batchCompletion(const std::vector<CompletionRequest> &requests)
{
    std::vector<CompletionResponse> allResponses(requests.size());
    std::vector<int> originalIndices(requests.size());
    std::vector<CompletionRequest> uncachedRequests;
    std::map<int, int> uncachedRequestIndexMap; // Maps original index to uncached index

    if(!arbiterAI_::instance().initialized)
    {
        return allResponses; // Return responses with default/error state
    }

    // First, try to fulfill requests from cache and check cost
    for(size_t i=0; i<requests.size(); ++i)
    {
        originalIndices[i]=i;
        if(m_cacheManager)
        {
            auto cachedResponse=m_cacheManager->get(requests[i]);
            if(cachedResponse)
            {
                allResponses[i]=*cachedResponse;
                continue;
            }
        }

        std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(requests[i].model);
        if(!modelInfo)
        {
            // Mark as error or skip
            continue;
        }

        if(m_costManager)
        {
            double estimatedCost=0.0;
            if(modelInfo->pricing.prompt_token_cost>0)
            {
                int totalPromptTokens=0;
                for(const auto &msg:requests[i].messages)
                {
                    totalPromptTokens+=msg.content.length()/4; // Rough estimate
                }
                estimatedCost=totalPromptTokens*modelInfo->pricing.prompt_token_cost;
            }
            if(!m_costManager->canProceed(estimatedCost))
            {
                // Mark as error or skip
                continue;
            }
        }

        uncachedRequestIndexMap[i]=uncachedRequests.size();
        uncachedRequests.push_back(requests[i]);
    }

    if(uncachedRequests.empty())
    {
        return allResponses;
    }

    // Group uncached requests by model
    std::map<std::string, std::vector<CompletionRequest>> requestsByModel;
    std::map<std::string, std::vector<int>> originalIndicesByModel;

    for(size_t i=0; i<requests.size(); ++i)
    {
        // If the response for this index is not yet filled (i.e., not from cache and passed cost check)
        if(allResponses[i].text.empty()&&uncachedRequestIndexMap.count(i))
        {
            requestsByModel[requests[i].model].push_back(requests[i]);
            originalIndicesByModel[requests[i].model].push_back(i);
        }
    }

    for(auto const &[modelName, modelRequests]:requestsByModel)
    {
        std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(modelName);
        if(!modelInfo)
        {
            // Handle unknown model for all requests in this group
            continue;
        }

        BaseProvider *provider=getProvider(modelInfo->provider);
        if(!provider)
        {
            // Handle unsupported provider
            continue;
        }

        std::vector<CompletionResponse> providerResponses=provider->batchCompletion(modelRequests);

        // Place responses back in the correct positions and cache them
        const auto &originalIdxs=originalIndicesByModel.at(modelName);
        for(size_t i=0; i<providerResponses.size(); ++i)
        {
            int originalIndex=originalIdxs[i];
            allResponses[originalIndex]=providerResponses[i];
            if(providerResponses[i].text!="") // Assuming empty text indicates an error
            {
                if(m_cacheManager)
                {
                    m_cacheManager->put(requests[originalIndex], providerResponses[i]);
                }
                if(m_costManager)
                {
                    m_costManager->recordCost(providerResponses[i].cost);
                }
            }
        }
    }

    return allResponses;
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
