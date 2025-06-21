#include "hermesaxiom/providers/base_llm.h"
#include "hermesaxiom/modelManager.h"
#include <cstdlib>
#include <algorithm>
#include <string>

namespace hermesaxiom
{

std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::toupper(c); }
    );
    return s;
}

ErrorCode BaseLLM::getApiKey(const std::string &modelName,
    const std::optional<std::string> &providerName,
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
    std::string provider;
    if(providerName.has_value())
    {
        provider=providerName.value();
    }
    else if(modelInfo)
    {
        provider=modelInfo->provider;
    }

    if(!provider.empty())
    {
        std::string envVarName=to_upper(provider)+"_API_KEY";
        if(const char *key=std::getenv(envVarName.c_str()))
        {
            apiKey=key;
            return ErrorCode::Success;
        }
    }

    return ErrorCode::ApiKeyNotFound;
}

ErrorCode BaseLLM::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    return ErrorCode::NotImplemented;
}

} // namespace hermesaxiom
