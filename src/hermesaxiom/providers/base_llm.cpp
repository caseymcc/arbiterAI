#include "hermesaxiom/providers/base_llm.h"
#include "hermesaxiom/modelManager.h"
#include <cstdlib>
#include <algorithm>
#include <string>

namespace hermesaxiom
{

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); }
                  );
    return s;
}

ErrorCode BaseLLM::getApiKey(const CompletionRequest &request, std::string &apiKey)
{
    // 1. Check the request itself
    if (request.api_key.has_value() && !request.api_key.value().empty()) {
        apiKey = request.api_key.value();
        return ErrorCode::Success;
    }

    // 2. Check the model info from ModelManager
    auto modelInfo = ModelManager::instance().getModelInfo(request.model);
    if (modelInfo && modelInfo->apiKey.has_value() && !modelInfo->apiKey.value().empty()) {
        apiKey = modelInfo->apiKey.value();
        return ErrorCode::Success;
    }

    // 3. Fallback to environment variables
    if (modelInfo) {
        std::string envVarName = to_upper(modelInfo->provider) + "_API_KEY";
        if (const char* key = std::getenv(envVarName.c_str())) {
            apiKey = key;
            return ErrorCode::Success;
        }
    }
    
    return ErrorCode::ApiKeyNotFound;
}

} // namespace hermesaxiom
