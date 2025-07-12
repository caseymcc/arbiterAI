#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"
#include <cstdlib>
#include <algorithm>
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

    // 2. Check the model info from ModelManager
    auto modelInfo=ModelManager::instance().getModelInfo(modelName);
    if(modelInfo&&modelInfo->apiKey.has_value()&&!modelInfo->apiKey.value().empty())
    {
        apiKey=modelInfo->apiKey.value();
        return ErrorCode::Success;
    }

    // 3. Fallback to environment variables
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


DownloadStatus BaseProvider::getDownloadStatus(const std::string &modelName, std::string &error)
{
    return DownloadStatus::Completed;
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

} // namespace arbiterAI
