#include "hermesaxiom/providers/deepseek_llm.h"

namespace hermesaxiom
{

ErrorCode DeepseekLLM::completion(const CompletionRequest &request,
    CompletionResponse &response)
{
    std::string apiKey;
    auto result = getApiKey(request, apiKey);
    if (result != ErrorCode::Success) {
        return result;
    }

    // Create request body and headers
    auto body=createRequestBody(request, false);
    auto headers=createHeaders(apiKey);
 
    // Make the API request
    auto raw_response=cpr::Post(
        cpr::Url{ m_apiUrl },
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

nlohmann::json DeepseekLLM::createRequestBody(const CompletionRequest &request, bool streaming)
{
    nlohmann::json body;
    body["model"]="deepseek-chat";  // Currently only supporting main chat model
    body["stream"]=streaming;

    // Convert messages to Deepseek format
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

    // Add default values for required fields
    body["frequency_penalty"]=0;
    body["presence_penalty"]=0;
    body["response_format"]={ {"type", "text"} };
    body["stream"]=false;
    body["top_p"]=1;

    return body;
}

cpr::Header DeepseekLLM::createHeaders(const std::string &apiKey)
{
    return cpr::Header{
        {"Content-Type", "application/json"},
        {"Accept", "application/json"},
        {"Authorization", "Bearer "+apiKey}
    };
}

ErrorCode DeepseekLLM::parseResponse(const cpr::Response &rawResponse,
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
    response.provider="deepseek";

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

ErrorCode DeepseekLLM::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string&)> callback)
{
    std::string apiKey;
    auto result = getApiKey(request, apiKey);
    if (result != ErrorCode::Success) {
        // Handle error, maybe by calling callback with an error message
        return result;
    }

    auto headers = createHeaders(apiKey);
    auto body = createRequestBody(request, true);
 
    // Setup streaming request
    auto session = cpr::Session();
    session.SetUrl(cpr::Url{m_apiUrl});
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

    auto response = session.Get();
    
    if (response.status_code != 200) {
        return ErrorCode::NetworkError;
    }

    return ErrorCode::Success;
}

} // namespace hermesaxiom
