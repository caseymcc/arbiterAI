#ifndef _hermesaxiom_providers_llama_h_
#define _hermesaxiom_providers_llama_h_

#include "hermesaxiom/providers/baseProvider.h"
#include <vector>

namespace hermesaxiom
{

class Llama : public BaseProvider
{
public:
Llama();
    ~Llama();

    ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_llama_h_