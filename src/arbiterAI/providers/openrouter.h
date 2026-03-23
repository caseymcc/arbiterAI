#ifndef _ARBITER_PROMPT_PROVIDER_OPENROUTER_H_
#define _ARBITER_PROMPT_PROVIDER_OPENROUTER_H_

#include "baseProvider.h"
#include "arbiterAI/arbiterAI.h"
#include <functional>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

class OpenRouter_LLM : public BaseProvider
{
public:
    OpenRouter_LLM();
    ~OpenRouter_LLM()=default;

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    ErrorCode getAvailableModels(std::vector<std::string>& models) override;

private:
    friend class ProviderTest;
    nlohmann::json createRequestBody(const CompletionRequest &request, bool streaming);
    ErrorCode parseResponse(const cpr::Response &rawResponse, CompletionResponse &response);
};

} // namespace arbiterAI

#endif // _ARBITER_PROMPT_PROVIDER_OPENROUTER_H_