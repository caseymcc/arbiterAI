#ifndef _arbiterAI_providers_anthropic_h_
#define _arbiterAI_providers_anthropic_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

namespace arbiterAI
{

class Anthropic : public BaseProvider
{
public:
    Anthropic();

    ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;
private:
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_anthropic_h_
