#include "arbiterAI/providers/anthropic.h"
#include "nlohmann/json.hpp"
#include <cpr/cpr.h>

namespace arbiterAI
{

Anthropic::Anthropic()
    : BaseProvider("anthropic")
{
    m_apiUrl="https://api.anthropic.com/v1";
}

nlohmann::json Anthropic::createRequestBody(const CompletionRequest &request, bool streaming)
{
    nlohmann::json body;
    body["model"]=request.model;
    body["max_tokens"]=request.max_tokens.value_or(1024); // Anthropic requires max_tokens
    body["stream"]=streaming;

    // Convert messages to Anthropic format
    nlohmann::json messages=nlohmann::json::array();
    for(const auto &msg:request.messages)
    {
        messages.push_back({
            {"role", msg.role},
            {"content", msg.content}
            });
    }
    body["messages"]=messages;

    // Add optional parameters if present
    if(request.temperature.has_value())
    {
        body["temperature"]=request.temperature.value();
    }
    if(request.top_p.has_value())
    {
        body["top_p"]=request.top_p.value();
    }
    if(request.stop.has_value()&&!request.stop->empty())
    {
        body["stop_sequences"]=request.stop.value();
    }
    // Anthropic does not support presence_penalty or frequency_penalty

    return body;
}

cpr::Header Anthropic::createHeaders(const std::string &apiKey)
{
    return cpr::Header{
        {"Content-Type", "application/json"},
        {"x-api-key", apiKey},
        {"anthropic-version", "2023-06-01"}
    };
}

ErrorCode Anthropic::parseResponse(const cpr::Response &rawResponse, CompletionResponse &response)
{
    nlohmann::json jsonResponse;
    try
    {
        jsonResponse=nlohmann::json::parse(rawResponse.text);
    }
    catch(const nlohmann::json::parse_error &)
    {
        return ErrorCode::InvalidResponse;
    }

    if(!jsonResponse.contains("content")||
        !jsonResponse["content"].is_array()||
        jsonResponse["content"].empty()||
        !jsonResponse["content"][0].contains("text"))
    {
        return ErrorCode::InvalidResponse;
    }

    response.text=jsonResponse["content"][0]["text"];
    response.provider="anthropic";

    if(jsonResponse.contains("model"))
    {
        response.model=jsonResponse["model"];
    }

    if(jsonResponse.contains("usage")&&
        jsonResponse["usage"].contains("output_tokens")) // Anthropic uses input_tokens and output_tokens
    {
        response.usage.prompt_tokens=jsonResponse["usage"]["input_tokens"].get<int>();
        response.usage.completion_tokens=jsonResponse["usage"]["output_tokens"].get<int>();
        response.usage.total_tokens=jsonResponse["usage"]["input_tokens"].get<int>()+jsonResponse["usage"]["output_tokens"].get<int>();
    }

    return ErrorCode::Success;
}

ErrorCode Anthropic::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    std::string apiKey;
    auto result=getApiKey(request.model, request.api_key, apiKey);
    if(result!=ErrorCode::Success)
    {
        return result;
    }

    auto headers=createHeaders(apiKey);
    auto body=createRequestBody(request, false);

    std::string completionUrl=m_apiUrl+"/messages";

    auto raw_response=cpr::Post(
        cpr::Url{ completionUrl },
        headers,
        cpr::Body(body.dump()),
        cpr::VerifySsl{ true }
    );

    if(raw_response.status_code!=200)
    {
        return ErrorCode::NetworkError;
    }

    return parseResponse(raw_response, response);
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

ErrorCode Anthropic::getAvailableModels(std::vector<std::string>& models)
{
    models = {
        "claude-3-opus-20240229",
        "claude-3-sonnet-20240229",
        "claude-3-haiku-20240307",
        "claude-2.1",
        "claude-2.0",
        "claude-instant-1.2"
    };
    return ErrorCode::Success;
}

} // namespace arbiterAI
