#ifndef _arbiterAI_providers_anthropic_h_
#define _arbiterAI_providers_anthropic_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

namespace arbiterAI
{

class Anthropic : public BaseProvider
{
public:
    Anthropic();

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;
private:
    nlohmann::json createRequestBody(const CompletionRequest &request, bool streaming);
    cpr::Header createHeaders(const std::string &apiKey);
    ErrorCode parseResponse(const cpr::Response &rawResponse, CompletionResponse &response);

    std::string m_apiUrl;
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_anthropic_h_
