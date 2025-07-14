#include "arbiterAI/providers/openrouter.h"
#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/arbiterAI.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

namespace
{
nlohmann::json createRequestBody(const CompletionRequest &request, bool streaming)
{
    nlohmann::json body;
    body["model"]=request.model;

    nlohmann::json messages=nlohmann::json::array();
    for(const auto &msg:request.messages)
    {
        messages.push_back({
            {"role", msg.role},
            {"content", msg.content}
            });
    }
    body["messages"]=messages;

    if(streaming)
    {
        body["stream"]=true;
    }

    if(request.temperature.has_value())
    {
        body["temperature"]=request.temperature.value();
    }
    if(request.max_tokens.has_value())
    {
        body["max_tokens"]=request.max_tokens.value();
    }
    if(request.top_p.has_value())
    {
        body["top_p"]=request.top_p.value();
    }
    if(request.presence_penalty.has_value())
    {
        body["presence_penalty"]=request.presence_penalty.value();
    }
    if(request.frequency_penalty.has_value())
    {
        body["frequency_penalty"]=request.frequency_penalty.value();
    }
    if(request.stop.has_value()&&!request.stop->empty())
    {
        body["stop"]=request.stop.value();
    }
    return body;
}

ErrorCode parseResponse(const cpr::Response &rawResponse, CompletionResponse &response)
{
    if(rawResponse.status_code!=200)
    {
        return ErrorCode::NetworkError;
    }

    try
    {
        nlohmann::json jsonResponse=nlohmann::json::parse(rawResponse.text);
        const auto &choice=jsonResponse["choices"][0];
        response.text=choice["message"]["content"];
        response.model=jsonResponse["model"];
        if(jsonResponse.contains("usage"))
        {
            response.usage.prompt_tokens=jsonResponse["usage"].value("prompt_tokens", 0);
            response.usage.completion_tokens=jsonResponse["usage"].value("completion_tokens", 0);
            response.usage.total_tokens=jsonResponse["usage"].value("total_tokens", 0);
        }
        return ErrorCode::Success;
    }
    catch(const nlohmann::json::exception &e)
    {
        return ErrorCode::InvalidResponse;
    }
}
}

ErrorCode OpenRouter_LLM::completion(const CompletionRequest &request, const ModelInfo &model, CompletionResponse &response)
{
    std::string apiKey;
    if(getApiKey(request.model, request.api_key, apiKey)!=ErrorCode::Success)
    {
        return ErrorCode::ApiKeyNotFound;
    }

    cpr::Header headers{
        {"Authorization", "Bearer "+apiKey},
        {"Content-Type", "application/json"}
    };

    std::string url=model.apiBase.value_or("https://openrouter.ai/api/v1")+"/chat/completions";

    auto body=createRequestBody(request, false);

    cpr::Response r=cpr::Post(cpr::Url{ url },
        cpr::Body{ body.dump() },
        headers);

    return parseResponse(r, response);
}

OpenRouter_LLM::OpenRouter_LLM()
    : BaseProvider("openrouter")
{
}

ErrorCode OpenRouter_LLM::streamingCompletion(const CompletionRequest &request, std::function<void(const std::string &)> callback)
{
    // Not yet implemented for OpenRouter
    return ErrorCode::NotImplemented;
}

ErrorCode OpenRouter_LLM::getEmbeddings(const EmbeddingRequest &request, EmbeddingResponse &response)
{
    // Not yet implemented for OpenRouter
    return ErrorCode::NotImplemented;
}

ErrorCode OpenRouter_LLM::getAvailableModels(std::vector<std::string>& models)
{
    std::string apiKey;
    if (getApiKey("", std::nullopt, apiKey) != ErrorCode::Success)
    {
        return ErrorCode::ApiKeyNotFound;
    }

    cpr::Header headers{
        {"Authorization", "Bearer " + apiKey}
    };

    std::string url = "https://openrouter.ai/api/v1/models";

    cpr::Response r = cpr::Get(cpr::Url{ url }, headers);

    if (r.status_code != 200)
    {
        return ErrorCode::NetworkError;
    }

    try
    {
        nlohmann::json jsonResponse = nlohmann::json::parse(r.text);
        if (jsonResponse.contains("data") && jsonResponse["data"].is_array())
        {
            for (const auto& model : jsonResponse["data"])
            {
                if (model.contains("id"))
                {
                    models.push_back(model["id"]);
                }
            }
        }
        return ErrorCode::Success;
    }
    catch (const nlohmann::json::exception& e)
    {
        return ErrorCode::InvalidResponse;
    }
}
}