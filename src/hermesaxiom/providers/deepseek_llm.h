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
    DeepseekLLM();

    ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

private:
    ErrorCode parseResponse(const cpr::Response &rawResponse,
        CompletionResponse &response);
    ErrorCode parseResponse(const cpr::Response &rawResponse,
        EmbeddingResponse &response);

    nlohmann::json createRequestBody(const CompletionRequest &request, bool streaming=false);
    cpr::Header createHeaders(const std::string &apiKey);

    std::string m_apiUrl="https://api.deepseek.com/chat/completions";
    std::string m_apiKey="";
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_deepseek_llm_h_
