#include "hermesaxiom/providers/openai_llm.h"

namespace hermesaxiom
{
OpenAILLM::OpenAILLM(const ModelInfo &modelInfo):
    m_modelInfo(modelInfo)
{
    if(m_modelInfo.apiBase.has_value())
    {
        m_apiUrl=m_modelInfo.apiBase.value();
    }

    if(m_modelInfo.apiKey.has_value())
    {
        m_apiKey=m_modelInfo.apiKey.value();
    }
};

ErrorCode OpenAILLM::completion(const CompletionRequest &request,
    CompletionResponse &response)
{
    // Create request headers and body
    auto headers=createHeaders();
    auto body=createRequestBody(request, false);

    std::string completionUrl=m_apiUrl+"/chat/completions";
    
    // Make the API request
    auto raw_response=cpr::Post(
        cpr::Url{ completionUrl },
        headers,
        cpr::Body{ body.dump() },
        cpr::VerifySsl{ true }
    );

    // Check for HTTP errors
    if(raw_response.status_code!=200)
    {
        return ErrorCode::NetworkError;
    }

    // Parse the response
    return parseResponse(raw_response, response);
}

nlohmann::json OpenAILLM::createRequestBody(const CompletionRequest &request, bool streaming)
{
    nlohmann::json body;
    body["model"]=request.model;
    body["stream"]=streaming;

    // Convert messages to OpenAI format
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
    if(request.max_tokens.has_value())
    {
        body["max_tokens"]=request.max_tokens.value();
    }

    return body;
}

cpr::Header OpenAILLM::createHeaders()
{
    if(m_apiKey.empty())
    {
        return cpr::Header{
            {"Content-Type", "application/json"}
        };
    }

    return cpr::Header{
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer "+m_apiKey}
    };
}

ErrorCode OpenAILLM::parseResponse(const cpr::Response &rawResponse,
    CompletionResponse &response)
{
    nlohmann::json jsonResponse;
    try {
        jsonResponse = nlohmann::json::parse(rawResponse.text);
    } catch(const nlohmann::json::parse_error&)
    {
        return ErrorCode::InvalidResponse;
    }

    // Extract the response text from the first choice
    if(!jsonResponse.contains("choices")||
        jsonResponse["choices"].empty()||
        !jsonResponse["choices"][0].contains("message")||
        !jsonResponse["choices"][0]["message"].contains("content"))
    {
        return ErrorCode::InvalidResponse;
    }

    response.text=jsonResponse["choices"][0]["message"]["content"];
    response.provider="openai";

    if(jsonResponse.contains("model"))
    {
        response.model=jsonResponse["model"];
    }

    // Extract usage information if available
    if(jsonResponse.contains("usage")&&
        jsonResponse["usage"].contains("total_tokens"))
    {
        response.tokens_used=jsonResponse["usage"]["total_tokens"];
    }

    return ErrorCode::Success;
}

ErrorCode OpenAILLM::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string&)> callback)
{
    auto headers = createHeaders();
    auto body = createRequestBody(request, true);
    std::string completionUrl = m_apiUrl + "/chat/completions";

    // Setup streaming request
    auto session = cpr::Session();
    session.SetUrl(cpr::Url{completionUrl});
    session.SetHeader(headers);
    session.SetBody(body.dump());
    session.SetVerifySsl(true);

    // Make streaming request
    session.SetOption(cpr::WriteCallback([callback](const std::string_view& data, intptr_t) -> bool {
        if (data.empty() || data == "\n") return true;
        
        try {
            if (data.substr(0, 6) == "data: ") {
                std::string jsonStr = std::string(data.substr(6)); // Remove "data: " prefix
                if (jsonStr == "[DONE]") return true;
                
                auto json = nlohmann::json::parse(jsonStr);
                if (json.contains("choices") && !json["choices"].empty() &&
                    json["choices"][0].contains("delta") &&
                    json["choices"][0]["delta"].contains("content")) {
                    
                    std::string content = json["choices"][0]["delta"]["content"];
                    callback(content);
                }
            }
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }));

    auto response = session.Post();
    
    if (response.status_code != 200) {
        return ErrorCode::NetworkError;
    }

    return ErrorCode::Success;
}

} // namespace hermesaxiom
