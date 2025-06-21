#ifndef _hermesaxiom_providers_deepseek_llm_h_
#define _hermesaxiom_providers_deepseek_llm_h_

#include "hermesaxiom/providers/base_llm.h"
#include "hermesaxiom/modelManager.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace hermesaxiom
{

class DeepseekLLM : public BaseLLM
{
public:
    DeepseekLLM(const ModelInfo &modelInfo) : m_modelInfo(modelInfo) {};
    
    ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response) override;
        
    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string&)> callback) override;

private:
    ErrorCode parseResponse(const cpr::Response &rawResponse,
        CompletionResponse &response);

    nlohmann::json createRequestBody(const CompletionRequest &request, bool streaming = false);
    cpr::Header createHeaders(const std::string &apiKey);

    ModelInfo m_modelInfo;

    std::string m_apiUrl="https://api.deepseek.com/chat/completions";
    std::string m_apiKey="";
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_deepseek_llm_h_
