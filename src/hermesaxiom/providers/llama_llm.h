#ifndef _hermesaxiom_providers_llama_llm_h_
#define _hermesaxiom_providers_llama_llm_h_

#include "hermesaxiom/providers/base_llm.h"
#include <vector>

namespace hermesaxiom
{

class LlamaLLM : public BaseLLM
{
public:
    LlamaLLM();
    ~LlamaLLM();

    ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_llama_llm_h_