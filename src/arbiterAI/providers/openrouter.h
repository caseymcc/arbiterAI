#ifndef _ARBITER_PROMPT_PROVIDER_OPENROUTER_H_
#define _ARBITER_PROMPT_PROVIDER_OPENROUTER_H_

#include "baseProvider.h"
#include "arbiterAI/arbiterAI.h"
#include <future>
#include <functional>

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
};

} // namespace arbiterAI

#endif // _ARBITER_PROMPT_PROVIDER_OPENROUTER_H_