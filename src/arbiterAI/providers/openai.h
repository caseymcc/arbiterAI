#ifndef _arbiterAI_providers_openai_h_
#define _arbiterAI_providers_openai_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

class OpenAI : public BaseProvider
{
public:
    OpenAI();

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
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

    std::string m_apiUrl="https://api.openai.com/v1";
    std::string m_apiKey="";
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_openai_h_
