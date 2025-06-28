#include "hermesaxiom/providers/anthropic.h"

namespace hermesaxiom
{

Anthropic::Anthropic()
    : BaseProvider("anthropic")
{
}

ErrorCode Anthropic::completion(const CompletionRequest &request,
    CompletionResponse &response)
{
    std::string apiKey;
    auto result=getApiKey(request.model, request.api_key, apiKey);
    if(result!=ErrorCode::Success)
    {
        return result;
    }

    // Implementation using cpr for Anthropic API
    // TODO: Implement actual API call
    response.provider="anthropic";
    return ErrorCode::Success;
}

ErrorCode Anthropic::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    // TODO: Implement Anthropic streaming API
    // This will be similar to OpenAI implementation but with Anthropic's specific API format
    return ErrorCode::NotImplemented;
}

ErrorCode Anthropic::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    return ErrorCode::NotImplemented;
}

} // namespace hermesaxiom
