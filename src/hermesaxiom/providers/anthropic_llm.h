#ifndef _hermesaxiom_providers_anthropic_llm_h_
#define _hermesaxiom_providers_anthropic_llm_h_

#include "hermesaxiom/providers/base_llm.h"
#include "hermesaxiom/modelManager.h"

namespace hermesaxiom
{

class AnthropicLLM : public BaseLLM
{
public:
    AnthropicLLM();

    ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;
private:
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_anthropic_llm_h_
