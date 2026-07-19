#include "arbiterAI/providers/openai.h"

#include <chrono>
#include <spdlog/spdlog.h>

namespace arbiterAI
{

// Sanitize a JSON Schema for llama.cpp server compatibility.
// The llama.cpp GBNF converter doesn't support boolean, array, or opaque
// object types.  Convert them to string equivalents with descriptive hints.
// Also strips fields the converter doesn't understand ($schema,
// additionalProperties) and normalises null properties.
static void sanitizeSchemaForLlamaCpp(nlohmann::json &schema)
{
    if(!schema.is_object())
        return;

    // Normalise "properties": null → remove the key entirely so downstream
    // checks like schema.contains("properties") behave consistently.
    if(schema.contains("properties") && schema["properties"].is_null())
        schema.erase("properties");

    // Recurse into each property (use explicit iterators — structured bindings
    // through nlohmann::json::items() can silently copy on some compilers).
    if(schema.contains("properties") && schema["properties"].is_object())
    {
        auto &props = schema["properties"];
        for(auto it = props.begin(); it != props.end(); ++it)
        {
            // Fix C++ initializer-list bug: {"type","boolean"} produces a
            // 2-element JSON array ["type","boolean"] instead of the intended
            // object {"type":"boolean"}.  Detect and repair this pattern.
            if(it.value().is_array() && it.value().size() == 2
               && it.value()[0].is_string() && it.value()[1].is_string()
               && it.value()[0].get<std::string>() == "type")
            {
                std::string typeName = it.value()[1].get<std::string>();
                it.value() = nlohmann::json{{"type", typeName}};
                spdlog::debug("sanitizeSchema: repaired array→object for property '{}' (type={})",
                              it.key(), typeName);
            }
            sanitizeSchemaForLlamaCpp(it.value());
        }
    }

    // If this schema has "items" (for array type), recurse into items
    if(schema.contains("items") && schema["items"].is_object())
    {
        sanitizeSchemaForLlamaCpp(schema["items"]);
    }

    // Remove fields that llama.cpp's GBNF converter doesn't understand
    schema.erase("additionalProperties");
    schema.erase("$schema");

    if(!schema.contains("type"))
        return;

    const auto &typeVal = schema["type"];
    if(!typeVal.is_string())
        return;

    const std::string typ = typeVal.get<std::string>();

    // Convert array-typed properties to string — the llama.cpp Jinja template
    // can't handle "array" type with "items" sub-schema
    if(typ == "array")
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
    // Convert boolean to string — llama.cpp GBNF doesn't recognize "boolean"
    else if(typ == "boolean")
    {
        spdlog::debug("sanitizeSchema: converting boolean → string enum for key context");
        schema["type"] = "string";
        std::string desc = schema.value("description", "");
        if(!desc.empty())
            desc += " (\"true\" or \"false\")";
        else
            desc = "\"true\" or \"false\"";
        schema["description"] = desc;
        schema["enum"] = nlohmann::json::array({"true", "false"});
    }
    // Convert integer to number — llama.cpp GBNF may not recognize "integer"
    else if(typ == "integer")
    {
        schema["type"] = "number";
    }
    // Convert nested object to string — llama.cpp can't handle nested object
    // schemas.  Convert if it has NO properties or if properties was null
    // (already erased above).
    else if(typ == "object" && !schema.contains("properties"))
    {
        spdlog::debug("sanitizeSchema: converting bare object → string");
        schema["type"] = "string";
        std::string desc = schema.value("description", "");
        if(!desc.empty())
            desc += " (JSON object as string)";
        else
            desc = "JSON object as string";
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

    // Use model-specific apiBase if available, otherwise fall back to provider default
    std::string baseUrl = (model.apiBase.has_value() && !model.apiBase->empty())
        ? model.apiBase.value() : m_apiUrl;
    std::string completionUrl=baseUrl+"/chat/completions";

    // Make the API request
    int timeoutMs = 300000; // 5 minute default safety cap
    if (request.timeout_ms.has_value() && request.timeout_ms.value() > 0)
        timeoutMs = request.timeout_ms.value();

    // Idle timeout: abort if no bytes received for this many seconds.
    // Uses ProgressCallback to reset on ANY received byte (including \n keepalives)
    // rather than LowSpeed which checks average speed over the window and fails
    // with sparse keepalive bytes.
    int idleTimeoutSec = 60;
    if (request.low_speed_time_s.has_value()) {
        idleTimeoutSec = request.low_speed_time_s.value();
    }

    // Track download progress to implement per-byte idle timeout.
    // Each received byte (including \n keepalives) resets the idle timer.
    cpr::cpr_off_t lastDlNow = 0;
    auto lastByteTime = std::chrono::steady_clock::now();

    auto session = cpr::Session();
    session.SetUrl(cpr::Url{completionUrl});
    session.SetHeader(headers);
    session.SetBody(cpr::Body{body.dump()});
    session.SetVerifySsl(cpr::VerifySsl{true});
    session.SetTimeout(cpr::Timeout{timeoutMs});

    if (idleTimeoutSec > 0) {
        session.SetProgressCallback(cpr::ProgressCallback{
            [&lastDlNow, &lastByteTime, idleTimeoutSec](
                cpr::cpr_off_t /*dlTotal*/, cpr::cpr_off_t dlNow,
                cpr::cpr_off_t /*ulTotal*/, cpr::cpr_off_t /*ulNow*/,
                intptr_t /*userdata*/) -> bool {
                auto now = std::chrono::steady_clock::now();
                if (dlNow > lastDlNow) {
                    lastDlNow = dlNow;
                    lastByteTime = now;
                }
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - lastByteTime).count();
                return elapsed < idleTimeoutSec; // false = abort transfer
            }});
    }

    auto raw_response = session.Post();

    // Check for curl-level errors first (timeout, abort, network failure).
    // cpr may report status_code=200 from headers even when the transfer was
    // aborted mid-body (ProgressCallback return false, hard timeout, etc.).
    if (raw_response.error && raw_response.error.code != cpr::ErrorCode::OK) {
        spdlog::warn("OpenAI provider: transfer error '{}' from {} "
                     "(HTTP {}, body_len: {})",
                     raw_response.error.message.substr(0, 500),
                     completionUrl, raw_response.status_code,
                     raw_response.text.size());
        return ErrorCode::NetworkError;
    }

    // Check for HTTP errors
    if(raw_response.status_code!=200)
    {
        spdlog::warn("OpenAI provider: HTTP {} from {} (error: {}, body: {})",
                     raw_response.status_code, completionUrl,
                     raw_response.error.message.substr(0, 500),
                     raw_response.text.substr(0, 2000));
        return ErrorCode::NetworkError;
    }

    // If body is only whitespace (keepalive \n bytes with no actual JSON content),
    // treat as a retriable network error rather than attempting JSON parse.
    if (raw_response.text.find_first_not_of(" \t\r\n") == std::string::npos) {
        spdlog::warn("OpenAI provider: response body is whitespace-only "
                     "({} bytes from {}), server did not produce a completion",
                     raw_response.text.size(), completionUrl);
        return ErrorCode::NetworkError;
    }

    // Parse the response
    auto parseResult = parseResponse(raw_response, response);
    if (parseResult == ErrorCode::Success && response.text.empty()) {
        spdlog::debug("OpenAI provider: response text empty (finish_reason: '{}', "
                      "body_len: {}, body_preview: {})",
                      response.finishReason,
                      raw_response.text.size(),
                      raw_response.text.substr(0, 500));
    }
    return parseResult;
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
    if(request.max_tokens.has_value() && request.max_tokens.value() > 0)
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
    if(request.cache_prompt.has_value())
    {
        // llama.cpp server extension: reuse the KV cache for the common
        // prompt prefix across requests. Only sent when explicitly set —
        // OpenAI itself rejects unrecognised request arguments.
        body["cache_prompt"]=request.cache_prompt.value();
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

        // Debug: log a compact summary of tool schemas so we can verify sanitisation
        if(spdlog::should_log(spdlog::level::debug))
        {
            for(const auto &t : toolsJson)
            {
                const auto &fn = t.value("function", nlohmann::json::object());
                const auto &ps = fn.value("parameters", nlohmann::json::object());
                spdlog::debug("Tool '{}' schema keys after sanitise: {}",
                    fn.value("name", "?"), ps.dump().substr(0, 500));
            }
        }

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

    // Extract reasoning_content (chain-of-thought from reasoning models)
    if(message.contains("reasoning_content") && !message["reasoning_content"].is_null())
    {
        response.reasoningContent = message["reasoning_content"].get<std::string>();
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

    // If content is empty but we have reasoning, use reasoning as the text
    // so callers that only read .text still get the model output
    if(response.text.empty() && !response.reasoningContent.empty() && response.toolCalls.empty())
    {
        response.text = response.reasoningContent;
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
                        json["choices"][0].contains("delta"))
                    {
                        const auto &delta = json["choices"][0]["delta"];
                        std::string chunk;
                        if(delta.contains("reasoning_content") && !delta["reasoning_content"].is_null())
                            chunk += delta["reasoning_content"].get<std::string>();
                        if(delta.contains("content") && !delta["content"].is_null())
                            chunk += delta["content"].get<std::string>();
                        if(!chunk.empty())
                            callback(chunk);
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
        cpr::VerifySsl{ true },
        cpr::Timeout{ 60000 },
        cpr::LowSpeed{ 1, std::chrono::seconds(60) }
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

ErrorCode OpenAI::transcribe(const AudioTranscriptionRequest &request,
    const ModelInfo &model,
    AudioTranscriptionResponse &response)
{
    std::string apiKey;
    auto result=getApiKey(request.model, request.api_key, apiKey);
    if(result!=ErrorCode::Success)
    {
        return result;
    }

    std::string baseUrl=(model.apiBase.has_value() && !model.apiBase->empty())
        ? model.apiBase.value() : m_apiUrl;
    std::string url=baseUrl+"/audio/transcriptions";

    // Multipart upload: file + model + optional decoding hints.
    // Do not set Content-Type here — cpr fills in the multipart boundary.
    cpr::Multipart multipart{
        {"file", cpr::Buffer{request.audio.begin(), request.audio.end(), request.filename}},
        {"model", request.model}
    };
    if(request.language.has_value())
        multipart.parts.emplace_back("language", request.language.value());
    if(request.prompt.has_value())
        multipart.parts.emplace_back("prompt", request.prompt.value());
    if(request.temperature.has_value())
        multipart.parts.emplace_back("temperature", std::to_string(request.temperature.value()));
    if(request.responseFormat.has_value())
        multipart.parts.emplace_back("response_format", request.responseFormat.value());

    cpr::Header headers;
    if(!apiKey.empty())
        headers["Authorization"]="Bearer "+apiKey;

    auto raw_response=cpr::Post(
        cpr::Url{ url },
        headers,
        multipart,
        cpr::VerifySsl{ true },
        cpr::Timeout{ 300000 }
    );

    if(raw_response.status_code!=200)
    {
        spdlog::warn("OpenAI provider: transcription HTTP {} from {} (body: {})",
            raw_response.status_code, url, raw_response.text.substr(0, 500));
        return ErrorCode::NetworkError;
    }

    response.model=request.model;
    response.provider="openai";

    // response_format=text returns raw text; json/verbose_json return an object.
    const std::string trimmed=raw_response.text;
    size_t firstNonWs=trimmed.find_first_not_of(" \t\r\n");
    if(firstNonWs!=std::string::npos && trimmed[firstNonWs]=='{')
    {
        nlohmann::json jsonResponse;
        try
        {
            jsonResponse=nlohmann::json::parse(trimmed);
        }
        catch(const nlohmann::json::parse_error &)
        {
            return ErrorCode::InvalidResponse;
        }
        if(jsonResponse.contains("text"))
            response.text=jsonResponse["text"].get<std::string>();
        if(jsonResponse.contains("language") && !jsonResponse["language"].is_null())
            response.language=jsonResponse["language"].get<std::string>();
        if(jsonResponse.contains("duration") && jsonResponse["duration"].is_number())
            response.duration=jsonResponse["duration"].get<double>();
    }
    else
    {
        response.text=trimmed;
    }

    return ErrorCode::Success;
}

ErrorCode OpenAI::synthesizeSpeech(const SpeechRequest &request,
    const ModelInfo &model,
    SpeechResponse &response)
{
    std::string apiKey;
    auto result=getApiKey(request.model, request.api_key, apiKey);
    if(result!=ErrorCode::Success)
    {
        return result;
    }

    std::string baseUrl=(model.apiBase.has_value() && !model.apiBase->empty())
        ? model.apiBase.value() : m_apiUrl;
    std::string url=baseUrl+"/audio/speech";

    nlohmann::json body;
    body["model"]=request.model;
    body["input"]=request.input;
    body["voice"]=request.voice;
    std::string format=request.responseFormat.value_or("mp3");
    body["response_format"]=format;
    if(request.speed.has_value())
        body["speed"]=request.speed.value();

    auto raw_response=cpr::Post(
        cpr::Url{ url },
        createHeaders(apiKey),
        cpr::Body{ body.dump() },
        cpr::VerifySsl{ true },
        cpr::Timeout{ 300000 }
    );

    if(raw_response.status_code!=200)
    {
        spdlog::warn("OpenAI provider: speech HTTP {} from {} (body: {})",
            raw_response.status_code, url, raw_response.text.substr(0, 500));
        return ErrorCode::NetworkError;
    }

    // The body is the raw (binary-safe) audio payload.
    response.audio.assign(raw_response.text.begin(), raw_response.text.end());
    response.format=format;
    response.model=request.model;
    response.provider="openai";

    return ErrorCode::Success;
}

ErrorCode OpenAI::generateImage(const ImageGenerationRequest &request,
    const ModelInfo &model,
    ImageGenerationResponse &response)
{
    std::string apiKey;
    auto result=getApiKey(request.model, request.api_key, apiKey);
    if(result!=ErrorCode::Success)
    {
        return result;
    }

    std::string baseUrl=(model.apiBase.has_value() && !model.apiBase->empty())
        ? model.apiBase.value() : m_apiUrl;
    std::string url=baseUrl+"/images/generations";

    nlohmann::json body;
    body["model"]=request.model;
    body["prompt"]=request.prompt;
    if(request.n.has_value())
        body["n"]=request.n.value();
    if(request.size.has_value())
        body["size"]=request.size.value();
    if(request.responseFormat.has_value())
        body["response_format"]=request.responseFormat.value();

    auto raw_response=cpr::Post(
        cpr::Url{ url },
        createHeaders(apiKey),
        cpr::Body{ body.dump() },
        cpr::VerifySsl{ true },
        cpr::Timeout{ 300000 }
    );

    if(raw_response.status_code!=200)
    {
        spdlog::warn("OpenAI provider: image HTTP {} from {} (body: {})",
            raw_response.status_code, url, raw_response.text.substr(0, 500));
        return ErrorCode::NetworkError;
    }

    nlohmann::json jsonResponse;
    try
    {
        jsonResponse=nlohmann::json::parse(raw_response.text);
    }
    catch(const nlohmann::json::parse_error &)
    {
        return ErrorCode::InvalidResponse;
    }

    if(!jsonResponse.contains("data") || !jsonResponse["data"].is_array())
    {
        return ErrorCode::InvalidResponse;
    }

    for(const auto &item:jsonResponse["data"])
    {
        GeneratedImage image;
        if(item.contains("url") && !item["url"].is_null())
            image.url=item["url"].get<std::string>();
        if(item.contains("b64_json") && !item["b64_json"].is_null())
            image.b64Json=item["b64_json"].get<std::string>();
        if(item.contains("revised_prompt") && !item["revised_prompt"].is_null())
            image.revisedPrompt=item["revised_prompt"].get<std::string>();
        response.images.push_back(image);
    }

    response.model=request.model;
    response.provider="openai";

    return ErrorCode::Success;
}

} // namespace arbiterAI
