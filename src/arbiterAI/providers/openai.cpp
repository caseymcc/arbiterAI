#include "arbiterAI/providers/openai.h"

namespace arbiterAI
{

// Sanitize a JSON Schema for llama.cpp server compatibility.
// The llama.cpp Jinja chat template uses `items` as a built-in filter,
// so parameter schemas with "type": "array" cause template errors even
// when "items" is stripped (template branches on type=="array" and expects items).
// Convert array-typed properties to string type with a description note.
static void sanitizeSchemaForLlamaCpp(nlohmann::json &schema)
{
    if(!schema.is_object())
        return;

    // If this schema has "properties", recurse into each property
    if(schema.contains("properties") && schema["properties"].is_object())
    {
        for(auto &[key, prop] : schema["properties"].items())
        {
            sanitizeSchemaForLlamaCpp(prop);
        }
    }

    // Convert array-typed properties to string — the llama.cpp Jinja template
    // can't handle "array" type with "items" sub-schema
    if(schema.contains("type") && schema["type"] == "array")
    {
        std::string itemType = "string";
        if(schema.contains("items") && schema["items"].is_object())
        {
            itemType = schema["items"].value("type", "any");
            schema.erase("items");
        }
        schema["type"] = "string";
        std::string desc = schema.value("description", "");
        if(!desc.empty())
            desc += " (JSON array of " + itemType + ", e.g. [\"a\",\"b\"])";
        else
            desc = "JSON array of " + itemType + ", e.g. [\"a\",\"b\"]";
        schema["description"] = desc;
    }
}

OpenAI::OpenAI()
    : BaseProvider("openai")
{
    // Default to OpenAI's API URL
    // Can be overridden via setApiUrl() for local endpoints
}

ErrorCode OpenAI::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    std::string apiKey;
    auto result=getApiKey(request.model, request.api_key, apiKey);
    if(result!=ErrorCode::Success)
    {
        return result;
    }

    // Create request headers and body
    auto headers=createHeaders(apiKey);
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

nlohmann::json OpenAI::createRequestBody(const CompletionRequest &request, bool streaming)
{
    nlohmann::json body;
    body["model"]=request.model;
    body["stream"]=streaming;

    // Convert messages to OpenAI format
    nlohmann::json messages=nlohmann::json::array();
    for(const auto &msg:request.messages)
    {
        nlohmann::json msgJson = {
            {"role", msg.role},
            {"content", msg.content}
        };

        // Include tool_call_id for tool-result messages
        if(msg.toolCallId.has_value() && !msg.toolCallId->empty())
        {
            msgJson["tool_call_id"] = msg.toolCallId.value();
        }

        // Include tool_calls array for assistant messages that invoked tools
        if(msg.role == "assistant" && msg.toolCalls.has_value() && !msg.toolCalls->empty())
        {
            nlohmann::json toolCallsJson = nlohmann::json::array();
            for(const auto &tc : msg.toolCalls.value())
            {
                toolCallsJson.push_back({
                    {"id", tc.id},
                    {"type", "function"},
                    {"function", {
                        {"name", tc.name},
                        {"arguments", tc.arguments.is_string()
                            ? tc.arguments.get<std::string>()
                            : tc.arguments.dump()}
                    }}
                });
            }
            msgJson["tool_calls"] = toolCallsJson;
        }

        messages.push_back(msgJson);
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

    // Serialize tools in OpenAI function-calling format
    if(request.tools.has_value() && !request.tools->empty())
    {
        nlohmann::json toolsJson = nlohmann::json::array();
        for(const auto &tool : request.tools.value())
        {
            nlohmann::json funcJson = {
                {"name", tool.name},
                {"description", tool.description}
            };

            // Use parametersSchema if available, otherwise build from parameters vector
            if(!tool.parametersSchema.is_null())
            {
                nlohmann::json params = tool.parametersSchema;
                sanitizeSchemaForLlamaCpp(params);
                funcJson["parameters"] = params;
            }
            else if(!tool.parameters.empty())
            {
                nlohmann::json propsJson = nlohmann::json::object();
                std::vector<std::string> requiredParams;
                for(const auto &param : tool.parameters)
                {
                    nlohmann::json paramJson = {{"type", param.type}};
                    if(!param.description.empty())
                        paramJson["description"] = param.description;
                    if(!param.schema.is_null())
                        paramJson.merge_patch(param.schema);
                    propsJson[param.name] = paramJson;
                    if(param.required)
                        requiredParams.push_back(param.name);
                }
                funcJson["parameters"] = {
                    {"type", "object"},
                    {"properties", propsJson}
                };
                if(!requiredParams.empty())
                    funcJson["parameters"]["required"] = requiredParams;
                sanitizeSchemaForLlamaCpp(funcJson["parameters"]);
            }
            else
            {
                funcJson["parameters"] = {{"type", "object"}, {"properties", nlohmann::json::object()}};
            }

            toolsJson.push_back({
                {"type", "function"},
                {"function", funcJson}
            });
        }
        body["tools"] = toolsJson;

        // Add tool_choice if specified
        if(request.tool_choice.has_value())
        {
            body["tool_choice"] = request.tool_choice.value();
        }
    }

    return body;
}

cpr::Header OpenAI::createHeaders(const std::string &apiKey)
{
    if(apiKey.empty())
    {
        return cpr::Header{
            {"Content-Type", "application/json"}
        };
    }

    return cpr::Header{
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer "+apiKey}
    };
}

ErrorCode OpenAI::parseResponse(const cpr::Response &rawResponse,
    CompletionResponse &response)
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

    // Validate basic response structure
    if(!jsonResponse.contains("choices")||
        jsonResponse["choices"].empty()||
        !jsonResponse["choices"][0].contains("message"))
    {
        return ErrorCode::InvalidResponse;
    }

    const auto &choice = jsonResponse["choices"][0];
    const auto &message = choice["message"];

    // Extract finish_reason
    if(choice.contains("finish_reason") && !choice["finish_reason"].is_null())
    {
        response.finishReason = choice["finish_reason"].get<std::string>();
    }

    // Extract content (may be empty/null for tool_calls responses)
    if(message.contains("content") && !message["content"].is_null())
    {
        response.text = message["content"].get<std::string>();
    }

    response.provider="openai";

    if(jsonResponse.contains("model"))
    {
        response.model=jsonResponse["model"];
    }

    // Extract tool_calls if present
    if(message.contains("tool_calls") && message["tool_calls"].is_array())
    {
        for(const auto &tc : message["tool_calls"])
        {
            ToolCall toolCall;

            if(tc.contains("id"))
                toolCall.id = tc["id"].get<std::string>();

            if(tc.contains("function"))
            {
                const auto &func = tc["function"];
                if(func.contains("name"))
                    toolCall.name = func["name"].get<std::string>();
                if(func.contains("arguments"))
                {
                    const auto &args = func["arguments"];
                    if(args.is_string())
                    {
                        // Arguments come as a JSON string — parse it
                        try
                        {
                            toolCall.arguments = nlohmann::json::parse(args.get<std::string>());
                        }
                        catch(const nlohmann::json::parse_error &)
                        {
                            // If parsing fails, store as raw string
                            toolCall.arguments = args;
                        }
                    }
                    else
                    {
                        toolCall.arguments = args;
                    }
                }
            }

            response.toolCalls.push_back(toolCall);
        }
    }

    // Extract usage information if available
    if(jsonResponse.contains("usage")&&
        jsonResponse["usage"].contains("total_tokens"))
    {
        response.usage.total_tokens=jsonResponse["usage"]["total_tokens"];
        response.usage.prompt_tokens=jsonResponse["usage"]["prompt_tokens"];
        response.usage.completion_tokens=jsonResponse["usage"]["completion_tokens"];
    }

    return ErrorCode::Success;
}

ErrorCode OpenAI::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    std::string apiKey;
    auto result=getApiKey(request.model, request.api_key, apiKey);
    if(result!=ErrorCode::Success)
    {
        // Handle error, maybe by calling callback with an error message
        return result;
    }

    auto headers=createHeaders(apiKey);
    auto body=createRequestBody(request, true);
    std::string completionUrl=m_apiUrl+"/chat/completions";

    // Setup streaming request
    auto session=cpr::Session();
    session.SetUrl(cpr::Url{ completionUrl });
    session.SetHeader(headers);
    session.SetBody(body.dump());
    session.SetVerifySsl(true);

    // Make streaming request
    session.SetOption(cpr::WriteCallback([callback](const std::string_view &data, intptr_t) -> bool
        {
            if(data.empty()||data=="\n") return true;

            try
            {
                if(data.substr(0, 6)=="data: ")
                {
                    std::string jsonStr=std::string(data.substr(6)); // Remove "data: " prefix
                    if(jsonStr=="[DONE]") return true;

                    auto json=nlohmann::json::parse(jsonStr);
                    if(json.contains("choices")&&!json["choices"].empty()&&
                        json["choices"][0].contains("delta")&&
                        json["choices"][0]["delta"].contains("content"))
                    {

                        std::string content=json["choices"][0]["delta"]["content"];
                        callback(content);
                    }
                }
            }
            catch(const std::exception &)
            {
                return false;
            }
            return true;
        }));

    auto response=session.Post();

    if(response.status_code!=200)
    {
        return ErrorCode::NetworkError;
    }

    return ErrorCode::Success;
}

ErrorCode OpenAI::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    std::string apiKey;
    auto result=getApiKey(request.model, std::nullopt, apiKey);
    if(result!=ErrorCode::Success)
    {
        return result;
    }

    auto headers=createHeaders(apiKey);

    nlohmann::json body;
    body["model"]=request.model;
    std::visit([&body](auto &&arg)
        {
            body["input"]=arg;
        }, request.input);

    std::string embeddingUrl=m_apiUrl+"/embeddings";

    auto raw_response=cpr::Post(
        cpr::Url{ embeddingUrl },
        headers,
        cpr::Body{ body.dump() },
        cpr::VerifySsl{ true }
    );

    if(raw_response.status_code!=200)
    {
        return ErrorCode::NetworkError;
    }

    return parseResponse(raw_response, response);
}

ErrorCode OpenAI::parseResponse(const cpr::Response &rawResponse,
    EmbeddingResponse &response)
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

    if(!jsonResponse.contains("data")||
        !jsonResponse["data"].is_array()||
        jsonResponse["data"].empty()||
        !jsonResponse["data"][0].contains("embedding"))
    {
        return ErrorCode::InvalidResponse;
    }

    for(const auto &data_item:jsonResponse["data"])
    {
        Embedding emb;
        emb.index=data_item["index"];
        emb.embedding=data_item["embedding"].get<std::vector<float>>();
        response.data.push_back(emb);
    }

    if(jsonResponse.contains("model"))
    {
        response.model=jsonResponse["model"];
    }

    if(jsonResponse.contains("usage")&&
        jsonResponse["usage"].contains("total_tokens"))
    {
        response.usage.total_tokens=jsonResponse["usage"]["total_tokens"];
        response.usage.prompt_tokens=jsonResponse["usage"]["prompt_tokens"];
        response.usage.completion_tokens=0;
    }

    return ErrorCode::Success;
}


ErrorCode OpenAI::getAvailableModels(std::vector<std::string>& models)
{
    std::string apiKey;
    auto result=getApiKey("", std::nullopt, apiKey);
    if(result!=ErrorCode::Success)
    {
        return result;
    }

    auto headers = createHeaders(apiKey);
    std::string modelsUrl = m_apiUrl + "/models";

    auto raw_response = cpr::Get(
        cpr::Url{ modelsUrl },
        headers,
        cpr::VerifySsl{ true }
    );

    if (raw_response.status_code != 200)
    {
        return ErrorCode::NetworkError;
    }

    nlohmann::json jsonResponse;
    try
    {
        jsonResponse = nlohmann::json::parse(raw_response.text);
    }
    catch (const nlohmann::json::parse_error&)
    {
        return ErrorCode::InvalidResponse;
    }

    if (!jsonResponse.contains("data") || !jsonResponse["data"].is_array())
    {
        return ErrorCode::InvalidResponse;
    }

    for (const auto& model : jsonResponse["data"])
    {
        if (model.contains("id"))
        {
            models.push_back(model["id"]);
        }
    }

    return ErrorCode::Success;
}

} // namespace arbiterAI
