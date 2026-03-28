#include "routes.h"
#include "dashboard.h"

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/modelFitCalculator.h"
#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/telemetryCollector.h"
#include "arbiterAI/storageManager.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>
#include <random>
#include <sstream>
#include <iomanip>

namespace arbiterAI
{
namespace server
{

namespace
{

std::string g_overridePath;

/// Generate a unique ID with the given prefix (e.g., "chatcmpl-").
std::string generateId(const std::string &prefix="chatcmpl-")
{
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;

    std::ostringstream ss;
    ss<<prefix<<std::hex<<std::setfill('0')<<std::setw(16)<<dist(rng);
    return ss.str();
}

nlohmann::json errorJson(const std::string &message, const std::string &type="server_error",
    const std::string &param="", const std::string &code="")
{
    nlohmann::json err={
        {"message", message},
        {"type", type}
    };
    if(!param.empty()) err["param"]=param;
    else err["param"]=nullptr;
    if(!code.empty()) err["code"]=code;
    else err["code"]=nullptr;
    return {{"error", err}};
}

std::string gpuBackendToString(GpuBackend backend)
{
    switch(backend)
    {
        case GpuBackend::CUDA:   return "CUDA";
        case GpuBackend::Vulkan: return "Vulkan";
        default:                 return "None";
    }
}

nlohmann::json gpuInfoToJson(const GpuInfo &gpu)
{
    nlohmann::json j={
        {"index", gpu.index},
        {"name", gpu.name},
        {"backend", gpuBackendToString(gpu.backend)},
        {"vram_total_mb", gpu.vramTotalMb},
        {"vram_free_mb", gpu.vramFreeMb},
        {"compute_capability", gpu.computeCapability},
        {"utilization_percent", gpu.utilizationPercent},
        {"unified_memory", gpu.unifiedMemory}
    };

    if(gpu.unifiedMemory&&gpu.gpuAccessibleRamMb>0)
    {
        j["gpu_accessible_ram_mb"]=gpu.gpuAccessibleRamMb;
        j["gpu_accessible_ram_free_mb"]=gpu.gpuAccessibleRamFreeMb;
    }

    return j;
}

nlohmann::json systemInfoToJson(const SystemInfo &hw)
{
    nlohmann::json gpus=nlohmann::json::array();
    for(const GpuInfo &gpu:hw.gpus)
    {
        gpus.push_back(gpuInfoToJson(gpu));
    }

    return {
        {"total_ram_mb", hw.totalRamMb},
        {"free_ram_mb", hw.freeRamMb},
        {"cpu_cores", hw.cpuCores},
        {"cpu_utilization_percent", hw.cpuUtilizationPercent},
        {"gpus", gpus}
    };
}

std::string modelStateToString(ModelState state)
{
    switch(state)
    {
        case ModelState::Unloaded:    return "Unloaded";
        case ModelState::Downloading: return "Downloading";
        case ModelState::Ready:       return "Ready";
        case ModelState::Loaded:      return "Loaded";
        case ModelState::Unloading:   return "Unloading";
        default:                      return "Unknown";
    }
}

nlohmann::json loadedModelToJson(const LoadedModel &m)
{
    nlohmann::json gpuIndices=nlohmann::json::array();
    for(int idx:m.gpuIndices)
    {
        gpuIndices.push_back(idx);
    }

    return {
        {"model", m.modelName},
        {"variant", m.variant},
        {"state", modelStateToString(m.state)},
        {"vram_usage_mb", m.vramUsageMb},
        {"ram_usage_mb", m.ramUsageMb},
        {"estimated_vram_mb", m.estimatedVramUsageMb},
        {"context_size", m.contextSize},
        {"gpu_indices", gpuIndices},
        {"pinned", m.pinned}
    };
}

nlohmann::json inferenceStatsToJson(const InferenceStats &s)
{
    return {
        {"model", s.model},
        {"variant", s.variant},
        {"tokens_per_second", s.tokensPerSecond},
        {"prompt_tokens", s.promptTokens},
        {"completion_tokens", s.completionTokens},
        {"latency_ms", s.latencyMs},
        {"total_time_ms", s.totalTimeMs}
    };
}

nlohmann::json swapEventToJson(const SwapEvent &e)
{
    return {
        {"from", e.from},
        {"to", e.to},
        {"time_ms", e.timeMs}
    };
}

nlohmann::json modelFitToJson(const ModelFit &f)
{
    nlohmann::json gpuIndices=nlohmann::json::array();
    for(int idx:f.gpuIndices)
    {
        gpuIndices.push_back(idx);
    }

    return {
        {"model", f.model},
        {"variant", f.variant},
        {"can_run", f.canRun},
        {"max_context_size", f.maxContextSize},
        {"limiting_factor", f.limitingFactor},
        {"estimated_vram_mb", f.estimatedVramUsageMb},
        {"gpu_indices", gpuIndices}
    };
}

std::string errorCodeToString(ErrorCode code)
{
    switch(code)
    {
        case ErrorCode::Success:             return "success";
        case ErrorCode::ModelNotFound:       return "model_not_found";
        case ErrorCode::ModelNotLoaded:      return "model_not_loaded";
        case ErrorCode::ModelLoadError:      return "model_load_error";
        case ErrorCode::ModelDownloading:    return "model_downloading";
        case ErrorCode::ModelDownloadFailed: return "model_download_failed";
        case ErrorCode::InsufficientStorage: return "insufficient_storage";
        case ErrorCode::UnknownModel:        return "unknown_model";
        case ErrorCode::UnsupportedProvider: return "unsupported_provider";
        case ErrorCode::InvalidRequest:      return "invalid_request";
        case ErrorCode::NetworkError:        return "network_error";
        case ErrorCode::InvalidResponse:     return "invalid_response";
        case ErrorCode::NotImplemented:      return "not_implemented";
        case ErrorCode::GenerationError:     return "generation_error";
        case ErrorCode::ApiKeyNotFound:      return "api_key_not_found";
        default:                             return "unknown_error";
    }
}

} // anonymous namespace

// ========== Override Path ==========

void setOverridePath(const std::string &path)
{
    g_overridePath=path;
}

// ========== Route Registration ==========

void registerRoutes(httplib::Server &server)
{
    // Ensure POST/PUT/DELETE requests without a body include Content-Length: 0.
    // httplib's read_content() rejects requests that lack Content-Length,
    // but many REST clients omit it for bodyless POST (e.g. /api/models/:name/load).
    server.set_pre_routing_handler([](const httplib::Request &req, httplib::Response &) -> httplib::Server::HandlerResponse {
        if((req.method=="POST"||req.method=="PUT"||req.method=="DELETE")
            &&!req.has_header("Content-Length")
            &&!req.has_header("Transfer-Encoding"))
        {
            const_cast<httplib::Request &>(req).set_header("Content-Length", "0");
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // CORS preflight handler for all routes
    server.Options(R"(.*)", [](const httplib::Request &, httplib::Response &res)
    {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS, DELETE");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "86400");
        res.status=204;
    });

    // Set CORS headers on all responses
    server.set_post_routing_handler([](const httplib::Request &, httplib::Response &res)
    {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS, DELETE");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    });

    // Health check
    server.Get("/health", handleHealth);
    server.Get("/v1/health", handleHealth);

    // Version
    server.Get("/api/version", handleGetVersion);

    // Chat completions (OpenAI-compatible)
    server.Post("/v1/chat/completions", handleChatCompletions);
    server.Get("/v1/models", handleListModelsV1);
    server.Get(R"(/v1/models/([^/]+))", handleGetModelV1);

    // Embeddings (OpenAI-compatible)
    server.Post("/v1/embeddings", handleEmbeddings);

    // Model management
    server.Get("/api/models", handleGetModels);
    server.Get("/api/models/loaded", handleGetLoadedModels);
    server.Post(R"(/api/models/([^/]+)/load)", handleLoadModel);
    server.Post(R"(/api/models/([^/]+)/unload)", handleUnloadModel);
    server.Post(R"(/api/models/([^/]+)/pin)", handlePinModel);
    server.Post(R"(/api/models/([^/]+)/unpin)", handleUnpinModel);
    server.Post(R"(/api/models/([^/]+)/download)", handleDownloadModel);
    server.Get(R"(/api/models/([^/]+)/download)", handleGetDownloadStatus);

    // Model config injection
    server.Post("/api/models/config", handleAddModelConfig);
    server.Put("/api/models/config", handleUpdateModelConfig);
    server.Get(R"(/api/models/config/([^/]+))", handleGetModelConfig);
    server.Delete(R"(/api/models/config/([^/]+))", handleDeleteModelConfig);

    // Telemetry
    server.Get("/api/stats", handleGetStats);
    server.Get("/api/stats/history", handleGetStatsHistory);
    server.Get("/api/stats/swaps", handleGetStatsSwaps);
    server.Get("/api/hardware", handleGetHardware);

    // Storage management
    server.Get("/api/storage", handleGetStorage);
    server.Get("/api/storage/models", handleGetStorageModels);
    server.Get(R"(/api/storage/models/([^/]+)/([^/]+))", handleGetStorageModelVariant);
    server.Get(R"(/api/storage/models/([^/]+))", handleGetStorageModel);
    server.Post("/api/storage/limit", handleSetStorageLimit);
    server.Delete(R"(/api/models/([^/]+)/files)", handleDeleteModelFiles);
    server.Post(R"(/api/models/([^/]+)/variants/([^/]+)/hot-ready)", handleSetHotReady);
    server.Delete(R"(/api/models/([^/]+)/variants/([^/]+)/hot-ready)", handleClearHotReady);
    server.Post(R"(/api/models/([^/]+)/variants/([^/]+)/protected)", handleSetProtected);
    server.Delete(R"(/api/models/([^/]+)/variants/([^/]+)/protected)", handleClearProtected);
    server.Get("/api/storage/cleanup/preview", handleGetCleanupPreview);
    server.Post("/api/storage/cleanup/run", handleRunCleanup);
    server.Get("/api/storage/cleanup/config", handleGetCleanupConfig);
    server.Put("/api/storage/cleanup/config", handleSetCleanupConfig);
    server.Get("/api/downloads", handleGetActiveDownloads);

    // Dashboard
    server.Get("/dashboard", handleDashboard);

    spdlog::info("Registered all HTTP routes");
}

// ========== Chat Completions ==========

void handleChatCompletions(const httplib::Request &req, httplib::Response &res)
{
    nlohmann::json requestJson;

    try
    {
        requestJson=nlohmann::json::parse(req.body);
    }
    catch(const nlohmann::json::parse_error &e)
    {
        res.status=400;
        res.set_content(errorJson("Failed to parse JSON body", "invalid_request_error", "", "parse_error").dump(), "application/json");
        return;
    }

    CompletionRequest arbiterRequest;

    try
    {
        arbiterRequest.model=requestJson.at("model");

        // Parse messages with full OpenAI message format support
        for(const nlohmann::json &msg:requestJson.at("messages"))
        {
            Message m;
            m.role=msg.at("role").get<std::string>();

            // content can be null for assistant messages with tool_calls
            if(msg.contains("content") && !msg.at("content").is_null())
                m.content=msg.at("content").get<std::string>();

            // tool_call_id for role="tool" messages
            if(msg.contains("tool_call_id"))
                m.toolCallId=msg.at("tool_call_id").get<std::string>();

            // tool_calls for role="assistant" messages
            if(msg.contains("tool_calls"))
            {
                std::vector<ToolCall> calls;
                for(const nlohmann::json &tc:msg.at("tool_calls"))
                {
                    ToolCall call;
                    call.id=tc.at("id").get<std::string>();
                    call.name=tc.at("function").at("name").get<std::string>();
                    std::string argsStr=tc.at("function").at("arguments").get<std::string>();
                    try
                    {
                        call.arguments=nlohmann::json::parse(argsStr);
                    }
                    catch(...)
                    {
                        call.arguments=argsStr;
                    }
                    calls.push_back(std::move(call));
                }
                m.toolCalls=std::move(calls);
            }

            // name field
            if(msg.contains("name"))
                m.name=msg.at("name").get<std::string>();

            arbiterRequest.messages.push_back(std::move(m));
        }

        if(requestJson.contains("temperature"))
            arbiterRequest.temperature=requestJson.at("temperature").get<double>();

        // Accept both max_tokens and max_completion_tokens
        if(requestJson.contains("max_tokens"))
            arbiterRequest.max_tokens=requestJson.at("max_tokens").get<int>();
        else if(requestJson.contains("max_completion_tokens"))
            arbiterRequest.max_tokens=requestJson.at("max_completion_tokens").get<int>();

        if(requestJson.contains("top_p"))
            arbiterRequest.top_p=requestJson.at("top_p").get<double>();
        if(requestJson.contains("presence_penalty"))
            arbiterRequest.presence_penalty=requestJson.at("presence_penalty").get<double>();
        if(requestJson.contains("frequency_penalty"))
            arbiterRequest.frequency_penalty=requestJson.at("frequency_penalty").get<double>();
        if(requestJson.contains("stop"))
        {
            if(requestJson.at("stop").is_string())
                arbiterRequest.stop=std::vector<std::string>{requestJson.at("stop").get<std::string>()};
            else
                arbiterRequest.stop=requestJson.at("stop").get<std::vector<std::string>>();
        }

        // Parse tools in OpenAI format: [{type: "function", function: {...}}]
        if(requestJson.contains("tools"))
        {
            std::vector<ToolDefinition> tools;
            for(const nlohmann::json &toolJson:requestJson.at("tools"))
            {
                if(!toolJson.contains("function")) continue;
                const nlohmann::json &fn=toolJson.at("function");

                ToolDefinition tool;
                tool.name=fn.at("name").get<std::string>();
                if(fn.contains("description"))
                    tool.description=fn.at("description").get<std::string>();
                if(fn.contains("parameters"))
                    tool.parametersSchema=fn.at("parameters");
                tools.push_back(std::move(tool));
            }
            arbiterRequest.tools=std::move(tools);
        }

        if(requestJson.contains("tool_choice"))
        {
            if(requestJson.at("tool_choice").is_string())
                arbiterRequest.tool_choice=requestJson.at("tool_choice").get<std::string>();
            else if(requestJson.at("tool_choice").is_object())
                arbiterRequest.tool_choice=requestJson.at("tool_choice").dump();
        }

        // n, response_format, logprobs, user, seed: accepted but not used for inference
        // (prevents client-side errors from unrecognized parameters)
    }
    catch(const nlohmann::json::exception &e)
    {
        res.status=400;
        res.set_content(errorJson(std::string("JSON validation error: ")+e.what(), "invalid_request_error", "", "invalid_request").dump(), "application/json");
        return;
    }

    bool stream=requestJson.value("stream", false);
    std::string requestId=generateId("chatcmpl-");
    auto created=std::time(nullptr);

    // Check for stream_options.include_usage
    bool includeUsage=false;
    if(requestJson.contains("stream_options") && requestJson.at("stream_options").is_object())
    {
        includeUsage=requestJson.at("stream_options").value("include_usage", false);
    }

    if(stream)
    {
        res.set_chunked_content_provider(
            "text/event-stream",
            [arbiterRequest, requestId, created, includeUsage](size_t, httplib::DataSink &sink)
            {
                // Send initial chunk with role
                nlohmann::json roleChunk={
                    {"id", requestId},
                    {"object", "chat.completion.chunk"},
                    {"created", created},
                    {"model", arbiterRequest.model},
                    {"system_fingerprint", nullptr},
                    {"choices", {{
                        {"index", 0},
                        {"delta", {{"role", "assistant"}}},
                        {"finish_reason", nullptr}
                    }}}
                };
                std::string roleLine="data: "+roleChunk.dump()+"\n\n";
                sink.write(roleLine.c_str(), roleLine.length());

                auto callback=[&](const std::string &chunk)
                {
                    if(chunk.empty()) return;
                    nlohmann::json sseChunk={
                        {"id", requestId},
                        {"object", "chat.completion.chunk"},
                        {"created", created},
                        {"model", arbiterRequest.model},
                        {"system_fingerprint", nullptr},
                        {"choices", {{
                            {"index", 0},
                            {"delta", {{"content", chunk}}},
                            {"finish_reason", nullptr}
                        }}}
                    };
                    std::string line="data: "+sseChunk.dump()+"\n\n";
                    sink.write(line.c_str(), line.length());
                };

                ErrorCode err=ArbiterAI::instance().streamingCompletion(arbiterRequest, callback);

                std::string finishReason=(err==ErrorCode::Success)?"stop":"error";

                if(err!=ErrorCode::Success)
                {
                    spdlog::error("Streaming completion failed: {}", errorCodeToString(err));
                }

                // Send final chunk with finish_reason
                nlohmann::json finishChunk={
                    {"id", requestId},
                    {"object", "chat.completion.chunk"},
                    {"created", created},
                    {"model", arbiterRequest.model},
                    {"system_fingerprint", nullptr},
                    {"choices", {{
                        {"index", 0},
                        {"delta", nlohmann::json::object()},
                        {"finish_reason", finishReason}
                    }}}
                };
                std::string finishLine="data: "+finishChunk.dump()+"\n\n";
                sink.write(finishLine.c_str(), finishLine.length());

                // Send usage chunk if requested
                if(includeUsage)
                {
                    nlohmann::json usageChunk={
                        {"id", requestId},
                        {"object", "chat.completion.chunk"},
                        {"created", created},
                        {"model", arbiterRequest.model},
                        {"system_fingerprint", nullptr},
                        {"choices", nlohmann::json::array()},
                        {"usage", {
                            {"prompt_tokens", 0},
                            {"completion_tokens", 0},
                            {"total_tokens", 0}
                        }}
                    };
                    std::string usageLine="data: "+usageChunk.dump()+"\n\n";
                    sink.write(usageLine.c_str(), usageLine.length());
                }

                std::string done="data: [DONE]\n\n";
                sink.write(done.c_str(), done.length());
                sink.done();
                return true;
            }
        );
    }
    else
    {
        CompletionResponse arbiterResponse;
        ErrorCode err=ArbiterAI::instance().completion(arbiterRequest, arbiterResponse);

        if(err!=ErrorCode::Success)
        {
            int status=500;
            std::string errType="server_error";
            std::string errCode=errorCodeToString(err);

            if(err==ErrorCode::UnknownModel || err==ErrorCode::ModelNotFound)
            {
                status=404;
                errType="invalid_request_error";
            }
            else if(err==ErrorCode::InvalidRequest)
            {
                status=400;
                errType="invalid_request_error";
            }

            res.status=status;
            res.set_content(errorJson("Completion failed: "+errCode, errType, "", errCode).dump(), "application/json");
            return;
        }

        std::string finishReason=arbiterResponse.finishReason.empty()?"stop":arbiterResponse.finishReason;

        // Build the message object
        nlohmann::json messageJson={
            {"role", "assistant"}
        };

        // If model made tool calls, set content to null and include tool_calls
        if(!arbiterResponse.toolCalls.empty())
        {
            messageJson["content"]=nullptr;
            nlohmann::json toolCallsJson=nlohmann::json::array();
            for(const ToolCall &tc:arbiterResponse.toolCalls)
            {
                std::string argsStr;
                if(tc.arguments.is_string())
                    argsStr=tc.arguments.get<std::string>();
                else
                    argsStr=tc.arguments.dump();

                toolCallsJson.push_back({
                    {"id", tc.id},
                    {"type", "function"},
                    {"function", {
                        {"name", tc.name},
                        {"arguments", argsStr}
                    }}
                });
            }
            messageJson["tool_calls"]=toolCallsJson;
            if(finishReason=="stop") finishReason="tool_calls";
        }
        else
        {
            messageJson["content"]=arbiterResponse.text;
            messageJson["tool_calls"]=nullptr;
        }

        nlohmann::json responseJson={
            {"id", requestId},
            {"object", "chat.completion"},
            {"created", created},
            {"model", arbiterResponse.model},
            {"system_fingerprint", nullptr},
            {"choices", {{
                {"index", 0},
                {"message", messageJson},
                {"finish_reason", finishReason}
            }}},
            {"usage", {
                {"prompt_tokens", arbiterResponse.usage.prompt_tokens},
                {"completion_tokens", arbiterResponse.usage.completion_tokens},
                {"total_tokens", arbiterResponse.usage.total_tokens}
            }}
        };

        res.set_content(responseJson.dump(), "application/json");
    }
}

void handleListModelsV1(const httplib::Request &, httplib::Response &res)
{
    std::vector<std::string> modelNames;
    ArbiterAI::instance().getAvailableModels(modelNames);

    auto created=static_cast<int64_t>(std::time(nullptr));

    nlohmann::json data=nlohmann::json::array();
    for(const std::string &name:modelNames)
    {
        data.push_back({
            {"id", name},
            {"object", "model"},
            {"created", created},
            {"owned_by", "arbiterai"},
            {"permission", nlohmann::json::array()}
        });
    }

    nlohmann::json response={
        {"object", "list"},
        {"data", data}
    };

    res.set_content(response.dump(), "application/json");
}

void handleGetModelV1(const httplib::Request &req, httplib::Response &res)
{
    std::string modelId=req.matches[1];

    std::vector<std::string> modelNames;
    ArbiterAI::instance().getAvailableModels(modelNames);

    bool found=false;
    for(const std::string &name:modelNames)
    {
        if(name==modelId)
        {
            found=true;
            break;
        }
    }

    if(!found)
    {
        res.status=404;
        res.set_content(errorJson("Model '"+modelId+"' not found", "invalid_request_error", "model", "model_not_found").dump(), "application/json");
        return;
    }

    nlohmann::json response={
        {"id", modelId},
        {"object", "model"},
        {"created", static_cast<int64_t>(std::time(nullptr))},
        {"owned_by", "arbiterai"},
        {"permission", nlohmann::json::array()}
    };

    res.set_content(response.dump(), "application/json");
}

// ========== Embeddings ==========

void handleEmbeddings(const httplib::Request &req, httplib::Response &res)
{
    nlohmann::json requestJson;

    try
    {
        requestJson=nlohmann::json::parse(req.body);
    }
    catch(const nlohmann::json::parse_error &e)
    {
        res.status=400;
        res.set_content(errorJson("Failed to parse JSON body", "invalid_request_error", "", "parse_error").dump(), "application/json");
        return;
    }

    EmbeddingRequest embeddingRequest;

    try
    {
        embeddingRequest.model=requestJson.at("model").get<std::string>();

        if(requestJson.at("input").is_string())
            embeddingRequest.input=requestJson.at("input").get<std::string>();
        else if(requestJson.at("input").is_array())
            embeddingRequest.input=requestJson.at("input").get<std::vector<std::string>>();
        else
        {
            res.status=400;
            res.set_content(errorJson("'input' must be a string or array of strings", "invalid_request_error", "input", "invalid_type").dump(), "application/json");
            return;
        }
    }
    catch(const nlohmann::json::exception &e)
    {
        res.status=400;
        res.set_content(errorJson(std::string("JSON validation error: ")+e.what(), "invalid_request_error", "", "invalid_request").dump(), "application/json");
        return;
    }

    EmbeddingResponse embeddingResponse;
    ErrorCode err=ArbiterAI::instance().getEmbeddings(embeddingRequest, embeddingResponse);

    if(err!=ErrorCode::Success)
    {
        res.status=500;
        res.set_content(errorJson("Embedding failed: "+errorCodeToString(err), "server_error", "", errorCodeToString(err)).dump(), "application/json");
        return;
    }

    nlohmann::json data=nlohmann::json::array();
    for(const Embedding &emb:embeddingResponse.data)
    {
        data.push_back({
            {"object", "embedding"},
            {"index", emb.index},
            {"embedding", emb.embedding}
        });
    }

    nlohmann::json responseJson={
        {"object", "list"},
        {"data", data},
        {"model", embeddingResponse.model},
        {"usage", {
            {"prompt_tokens", embeddingResponse.usage.prompt_tokens},
            {"total_tokens", embeddingResponse.usage.total_tokens}
        }}
    };

    res.set_content(responseJson.dump(), "application/json");
}

// ========== Health ==========

void handleHealth(const httplib::Request &, httplib::Response &res)
{
    auto ver=arbiterAI::getVersion();
    nlohmann::json response={
        {"status", "ok"},
        {"version", ver.toString()}
    };
    res.set_content(response.dump(), "application/json");
}

// ========== Version ==========

void handleGetVersion(const httplib::Request &, httplib::Response &res)
{
    auto ver=arbiterAI::getVersion();
    nlohmann::json j={
        {"version", ver.toString()},
        {"major", ver.major},
        {"minor", ver.minor},
        {"patch", ver.patch}
    };
    res.set_content(j.dump(), "application/json");
}

// ========== Model Management ==========

void handleGetModels(const httplib::Request &, httplib::Response &res)
{
    std::vector<ModelFit> fits=ArbiterAI::instance().getLocalModelCapabilities();
    std::vector<std::string> allNames;
    ArbiterAI::instance().getAvailableModels(allNames);

    nlohmann::json models=nlohmann::json::array();

    // Add models with hardware fit info
    for(const ModelFit &f:fits)
    {
        models.push_back(modelFitToJson(f));
    }

    // Add cloud models (no fit data)
    std::set<std::string> fitModels;
    for(const ModelFit &f:fits)
    {
        fitModels.insert(f.model);
    }

    for(const std::string &name:allNames)
    {
        if(fitModels.find(name)==fitModels.end())
        {
            models.push_back({
                {"model", name},
                {"variant", ""},
                {"can_run", true},
                {"max_context_size", 0},
                {"limiting_factor", ""},
                {"estimated_vram_mb", 0},
                {"gpu_indices", nlohmann::json::array()}
            });
        }
    }

    res.set_content(nlohmann::json{{"models", models}}.dump(), "application/json");
}

void handleGetLoadedModels(const httplib::Request &, httplib::Response &res)
{
    std::vector<LoadedModel> loaded=ArbiterAI::instance().getLoadedModels();

    nlohmann::json models=nlohmann::json::array();
    for(const LoadedModel &m:loaded)
    {
        models.push_back(loadedModelToJson(m));
    }

    res.set_content(nlohmann::json{{"models", models}}.dump(), "application/json");
}

void handleLoadModel(const httplib::Request &req, httplib::Response &res)
{
    try
    {
        std::string modelName=req.matches[1];
        std::string variant;
        int contextSize=0;

        if(req.has_param("variant"))
            variant=req.get_param_value("variant");
        if(req.has_param("context"))
            contextSize=std::stoi(req.get_param_value("context"));

        ErrorCode err=ArbiterAI::instance().loadModel(modelName, variant, contextSize);

        if(err==ErrorCode::Success)
        {
            res.set_content(nlohmann::json{{"status", "loaded"}, {"model", modelName}}.dump(), "application/json");
        }
        else if(err==ErrorCode::ModelDownloading)
        {
            res.status=202;
            res.set_content(nlohmann::json{{"status", "downloading"}, {"model", modelName}}.dump(), "application/json");
        }
        else if(err==ErrorCode::InsufficientStorage)
        {
            StorageInfo storageInfo=StorageManager::instance().getStorageInfo();
            nlohmann::json details={
                {"available_bytes", storageInfo.availableForModelsBytes},
                {"storage_limit_bytes", storageInfo.storageLimitBytes},
                {"used_by_models_bytes", storageInfo.usedByModelsBytes},
                {"suggestion", "Delete unused models or increase the storage limit"}
            };
            res.status=507;
            res.set_content(nlohmann::json{
                {"error", {
                    {"message", "Insufficient storage to load model"},
                    {"type", "insufficient_storage"},
                    {"details", details}
                }}
            }.dump(), "application/json");
        }
        else
        {
            res.status=400;
            res.set_content(errorJson("Failed to load model: "+errorCodeToString(err), "invalid_request_error", "model", errorCodeToString(err)).dump(), "application/json");
        }
    }
    catch(const std::exception &e)
    {
        spdlog::error("Exception in handleLoadModel: {}", e.what());
        res.status=500;
        res.set_content(errorJson(std::string("Internal error: ")+e.what(), "server_error").dump(), "application/json");
    }
}

void handleUnloadModel(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];

    ErrorCode err=ArbiterAI::instance().unloadModel(modelName);

    if(err==ErrorCode::Success)
    {
        res.set_content(nlohmann::json{{"status", "unloaded"}, {"model", modelName}}.dump(), "application/json");
    }
    else
    {
        res.status=400;
        res.set_content(errorJson("Failed to unload model: "+errorCodeToString(err), "invalid_request_error", "model", errorCodeToString(err)).dump(), "application/json");
    }
}

void handlePinModel(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];

    ErrorCode err=ArbiterAI::instance().pinModel(modelName);

    if(err==ErrorCode::Success)
    {
        res.set_content(nlohmann::json{{"status", "pinned"}, {"model", modelName}}.dump(), "application/json");
    }
    else
    {
        res.status=400;
        res.set_content(errorJson("Failed to pin model: "+errorCodeToString(err), "invalid_request_error", "model", errorCodeToString(err)).dump(), "application/json");
    }
}

void handleUnpinModel(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];

    ErrorCode err=ArbiterAI::instance().unpinModel(modelName);

    if(err==ErrorCode::Success)
    {
        res.set_content(nlohmann::json{{"status", "unpinned"}, {"model", modelName}}.dump(), "application/json");
    }
    else
    {
        res.status=400;
        res.set_content(errorJson("Failed to unpin model: "+errorCodeToString(err), "invalid_request_error", "model", errorCodeToString(err)).dump(), "application/json");
    }
}

void handleDownloadModel(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];

    // Initiate download via loadModel (which triggers download if file not present)
    std::string variant;
    if(req.has_param("variant"))
        variant=req.get_param_value("variant");

    ErrorCode err=ArbiterAI::instance().loadModel(modelName, variant);

    if(err==ErrorCode::ModelDownloading)
    {
        res.status=202;
        res.set_content(nlohmann::json{{"status", "downloading"}, {"model", modelName}}.dump(), "application/json");
    }
    else if(err==ErrorCode::Success)
    {
        res.set_content(nlohmann::json{{"status", "already_available"}, {"model", modelName}}.dump(), "application/json");
    }
    else
    {
        if(err==ErrorCode::InsufficientStorage)
        {
            StorageInfo storageInfo=StorageManager::instance().getStorageInfo();
            std::vector<CleanupCandidate> cleanupCandidates=StorageManager::instance().previewCleanup();

            nlohmann::json candidatesJson=nlohmann::json::array();
            for(const CleanupCandidate &c:cleanupCandidates)
            {
                candidatesJson.push_back({
                    {"model", c.modelName},
                    {"variant", c.variant},
                    {"file_size_bytes", c.fileSizeBytes},
                    {"usage_count", c.usageCount}
                });
            }

            nlohmann::json details={
                {"available_bytes", storageInfo.availableForModelsBytes},
                {"storage_limit_bytes", storageInfo.storageLimitBytes},
                {"used_by_models_bytes", storageInfo.usedByModelsBytes},
                {"cleanup_candidates", candidatesJson},
                {"suggestion", "Delete unused models or increase the storage limit"}
            };
            res.status=507;
            res.set_content(nlohmann::json{
                {"error", {
                    {"message", "Insufficient storage to download model"},
                    {"type", "insufficient_storage"},
                    {"details", details}
                }}
            }.dump(), "application/json");
        }
        else
        {
            res.status=400;
            res.set_content(errorJson("Download failed: "+errorCodeToString(err), "invalid_request_error", "model", errorCodeToString(err)).dump(), "application/json");
        }
    }
}

void handleGetDownloadStatus(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];
    std::string error;

    ArbiterAI::instance().getDownloadStatus(modelName, error);

    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(modelName);

    nlohmann::json response={
        {"model", modelName},
        {"state", state.has_value()?modelStateToString(state->state):"unknown"}
    };

    if(!error.empty())
    {
        response["error"]=error;
    }

    // Include speed/ETA snapshot if download is active
    std::optional<DownloadProgressSnapshot> snap=ModelRuntime::instance().getDownloadSnapshot(modelName);
    if(snap.has_value())
    {
        response["bytes_downloaded"]=snap->bytesDownloaded;
        response["total_bytes"]=snap->totalBytes;
        response["percent_complete"]=snap->percentComplete;
        response["speed_mbps"]=snap->speedMbps;
        response["eta_seconds"]=snap->etaSeconds;
    }

    res.set_content(response.dump(), "application/json");
}

// ========== Model Config Injection ==========

void handleAddModelConfig(const httplib::Request &req, httplib::Response &res)
{
    nlohmann::json requestJson;

    try
    {
        requestJson=nlohmann::json::parse(req.body);
    }
    catch(const nlohmann::json::parse_error &e)
    {
        res.status=400;
        res.set_content(errorJson("Invalid JSON: "+std::string(e.what()), "invalid_request_error").dump(), "application/json");
        return;
    }

    // Normalize: single model or array
    std::vector<nlohmann::json> modelJsons;
    if(requestJson.contains("models")&&requestJson["models"].is_array())
    {
        for(const nlohmann::json &m:requestJson["models"])
        {
            modelJsons.push_back(m);
        }
    }
    else if(requestJson.contains("model"))
    {
        modelJsons.push_back(requestJson);
    }
    else
    {
        res.status=400;
        res.set_content(errorJson("Request must contain 'model' field or 'models' array", "invalid_request_error").dump(), "application/json");
        return;
    }

    ModelManager &mm=ModelManager::instance();
    std::vector<std::string> added;

    for(const nlohmann::json &modelJson:modelJsons)
    {
        std::string error;
        if(!mm.addModelFromJson(modelJson, error))
        {
            // Rollback models added in this request
            for(const std::string &name:added)
            {
                mm.removeModel(name);
            }

            int status=400;
            if(error.find("already exists")!=std::string::npos)
                status=409;

            res.status=status;
            res.set_content(errorJson(error, "invalid_request_error").dump(), "application/json");
            return;
        }
        added.push_back(modelJson["model"].get<std::string>());
    }

    // Persist if override path is set
    if(!g_overridePath.empty())
    {
        mm.saveOverrides(g_overridePath);
    }

    res.status=201;
    res.set_content(nlohmann::json{{"added", added}}.dump(), "application/json");
}

void handleUpdateModelConfig(const httplib::Request &req, httplib::Response &res)
{
    nlohmann::json requestJson;

    try
    {
        requestJson=nlohmann::json::parse(req.body);
    }
    catch(const nlohmann::json::parse_error &e)
    {
        res.status=400;
        res.set_content(errorJson("Invalid JSON: "+std::string(e.what()), "invalid_request_error").dump(), "application/json");
        return;
    }

    // Normalize: single model or array
    std::vector<nlohmann::json> modelJsons;
    if(requestJson.contains("models")&&requestJson["models"].is_array())
    {
        for(const nlohmann::json &m:requestJson["models"])
        {
            modelJsons.push_back(m);
        }
    }
    else if(requestJson.contains("model"))
    {
        modelJsons.push_back(requestJson);
    }
    else
    {
        res.status=400;
        res.set_content(errorJson("Request must contain 'model' field or 'models' array", "invalid_request_error").dump(), "application/json");
        return;
    }

    ModelManager &mm=ModelManager::instance();
    std::vector<std::string> updated;
    std::vector<std::string> created;

    for(const nlohmann::json &modelJson:modelJsons)
    {
        std::string modelName=modelJson["model"].get<std::string>();
        bool existed=mm.getModelInfo(modelName).has_value();

        std::string error;
        if(!mm.updateModelFromJson(modelJson, error))
        {
            res.status=400;
            res.set_content(errorJson(error, "invalid_request_error").dump(), "application/json");
            return;
        }

        if(existed)
            updated.push_back(modelName);
        else
            created.push_back(modelName);
    }

    // Persist if override path is set
    if(!g_overridePath.empty())
    {
        mm.saveOverrides(g_overridePath);
    }

    res.set_content(nlohmann::json{{"updated", updated}, {"added", created}}.dump(), "application/json");
}

void handleGetModelConfig(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];

    std::optional<ModelInfo> info=ModelManager::instance().getModelInfo(modelName);
    if(!info.has_value())
    {
        res.status=404;
        res.set_content(errorJson("Model not found: "+modelName, "not_found_error").dump(), "application/json");
        return;
    }

    res.set_content(ModelManager::modelInfoToJson(info.value()).dump(), "application/json");
}

void handleDeleteModelConfig(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];

    // Unload from ModelRuntime if loaded
    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(modelName);
    if(state.has_value()&&state->state!=ModelState::Unloaded)
    {
        ArbiterAI::instance().unloadModel(modelName);
    }

    ModelManager &mm=ModelManager::instance();
    if(!mm.removeModel(modelName))
    {
        res.status=404;
        res.set_content(errorJson("Model not found: "+modelName, "not_found_error").dump(), "application/json");
        return;
    }

    // Persist if override path is set
    if(!g_overridePath.empty())
    {
        mm.saveOverrides(g_overridePath);
    }

    res.set_content(nlohmann::json{{"status", "removed"}, {"model", modelName}}.dump(), "application/json");
}

// ========== Telemetry ==========

void handleGetStats(const httplib::Request &, httplib::Response &res)
{
    SystemSnapshot snapshot=ArbiterAI::instance().getTelemetrySnapshot();

    nlohmann::json models=nlohmann::json::array();
    for(const LoadedModel &m:snapshot.models)
    {
        models.push_back(loadedModelToJson(m));
    }

    nlohmann::json response={
        {"hardware", systemInfoToJson(snapshot.hardware)},
        {"models", models},
        {"avg_tokens_per_second", snapshot.avgTokensPerSecond},
        {"active_requests", snapshot.activeRequests}
    };

    res.set_content(response.dump(), "application/json");
}

void handleGetStatsHistory(const httplib::Request &req, httplib::Response &res)
{
    int minutes=5;
    if(req.has_param("minutes"))
    {
        minutes=std::stoi(req.get_param_value("minutes"));
        if(minutes<=0) minutes=5;
        if(minutes>60) minutes=60;
    }

    std::vector<InferenceStats> history=ArbiterAI::instance().getInferenceHistory(std::chrono::minutes(minutes));

    nlohmann::json arr=nlohmann::json::array();
    for(const InferenceStats &s:history)
    {
        arr.push_back(inferenceStatsToJson(s));
    }

    res.set_content(arr.dump(), "application/json");
}

void handleGetStatsSwaps(const httplib::Request &, httplib::Response &res)
{
    std::vector<SwapEvent> swaps=TelemetryCollector::instance().getSwapHistory();

    nlohmann::json arr=nlohmann::json::array();
    for(const SwapEvent &e:swaps)
    {
        arr.push_back(swapEventToJson(e));
    }

    res.set_content(arr.dump(), "application/json");
}

void handleGetHardware(const httplib::Request &, httplib::Response &res)
{
    HardwareDetector::instance().refresh();
    SystemInfo hw=HardwareDetector::instance().getSystemInfo();

    res.set_content(systemInfoToJson(hw).dump(), "application/json");
}

// ========== Storage Management ==========

namespace
{

std::string formatBytesDisplay(int64_t bytes)
{
    if(bytes>=1073741824)
    {
        double gb=static_cast<double>(bytes)/1073741824.0;
        std::ostringstream ss;
        ss<<std::fixed<<std::setprecision(1)<<gb<<" GB";
        return ss.str();
    }
    if(bytes>=1048576)
    {
        double mb=static_cast<double>(bytes)/1048576.0;
        std::ostringstream ss;
        ss<<std::fixed<<std::setprecision(1)<<mb<<" MB";
        return ss.str();
    }
    return std::to_string(bytes)+" B";
}

std::string timePointToIsoStr(const std::chrono::system_clock::time_point &tp)
{
    std::time_t t=std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

nlohmann::json downloadedModelToJson(const DownloadedModelFile &f)
{
    return {
        {"model", f.modelName},
        {"variant", f.variant},
        {"filename", f.filename},
        {"file_size_bytes", f.fileSizeBytes},
        {"file_size_display", formatBytesDisplay(f.fileSizeBytes)},
        {"downloaded_at", timePointToIsoStr(f.downloadedAt)},
        {"last_used_at", timePointToIsoStr(f.lastUsedAt)},
        {"usage_count", f.usageCount},
        {"hot_ready", f.hotReady},
        {"protected", f.isProtected},
        {"runtime_state", f.runtimeState}
    };
}

} // anonymous namespace

void handleGetStorage(const httplib::Request &, httplib::Response &res)
{
    StorageInfo info=StorageManager::instance().getStorageInfo();

    nlohmann::json response={
        {"models_directory", info.modelsDirectory.string()},
        {"total_disk_bytes", info.totalDiskBytes},
        {"free_disk_bytes", info.freeDiskBytes},
        {"used_by_models_bytes", info.usedByModelsBytes},
        {"storage_limit_bytes", info.storageLimitBytes},
        {"available_for_models_bytes", info.availableForModelsBytes},
        {"model_count", info.modelCount},
        {"cleanup_enabled", info.cleanupEnabled}
    };

    res.set_content(response.dump(), "application/json");
}

void handleGetStorageModels(const httplib::Request &req, httplib::Response &res)
{
    std::vector<DownloadedModelFile> models=StorageManager::instance().getDownloadedModels();

    // Sorting
    std::string sortField=req.has_param("sort")?req.get_param_value("sort"):"last_used";
    std::string sortOrder=req.has_param("order")?req.get_param_value("order"):"desc";

    auto compare=[&](const DownloadedModelFile &a, const DownloadedModelFile &b) -> bool
    {
        bool result=false;
        if(sortField=="name") result=a.modelName<b.modelName;
        else if(sortField=="size") result=a.fileSizeBytes<b.fileSizeBytes;
        else if(sortField=="usage_count") result=a.usageCount<b.usageCount;
        else result=a.lastUsedAt<b.lastUsedAt; // default: last_used

        return sortOrder=="asc"?result:!result;
    };

    std::sort(models.begin(), models.end(), compare);

    int64_t totalSize=0;
    nlohmann::json modelsJson=nlohmann::json::array();
    for(const DownloadedModelFile &f:models)
    {
        modelsJson.push_back(downloadedModelToJson(f));
        totalSize+=f.fileSizeBytes;
    }

    nlohmann::json response={
        {"models", modelsJson},
        {"total_size_bytes", totalSize},
        {"total_size_display", formatBytesDisplay(totalSize)}
    };

    res.set_content(response.dump(), "application/json");
}

void handleGetStorageModel(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];

    std::vector<DownloadedModelFile> variants=StorageManager::instance().getModelStats(modelName);

    if(variants.empty())
    {
        res.status=404;
        res.set_content(errorJson("No downloaded files found for model: "+modelName, "not_found_error").dump(), "application/json");
        return;
    }

    nlohmann::json variantsJson=nlohmann::json::array();
    int64_t totalSize=0;
    for(const DownloadedModelFile &f:variants)
    {
        variantsJson.push_back(downloadedModelToJson(f));
        totalSize+=f.fileSizeBytes;
    }

    nlohmann::json response={
        {"model", modelName},
        {"variants", variantsJson},
        {"total_size_bytes", totalSize},
        {"total_size_display", formatBytesDisplay(totalSize)}
    };

    res.set_content(response.dump(), "application/json");
}

void handleGetStorageModelVariant(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];
    std::string variant=req.matches[2];

    std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats(modelName, variant);

    if(!stats.has_value())
    {
        res.status=404;
        res.set_content(errorJson("Variant not found: "+modelName+" "+variant, "not_found_error").dump(), "application/json");
        return;
    }

    res.set_content(downloadedModelToJson(stats.value()).dump(), "application/json");
}

void handleSetStorageLimit(const httplib::Request &req, httplib::Response &res)
{
    try
    {
        nlohmann::json body=nlohmann::json::parse(req.body);
        int64_t limitBytes=body.value("limit_bytes", int64_t(0));

        StorageManager::instance().setStorageLimit(limitBytes);

        res.set_content(nlohmann::json{
            {"status", "updated"},
            {"storage_limit_bytes", limitBytes}
        }.dump(), "application/json");
    }
    catch(const std::exception &e)
    {
        res.status=400;
        res.set_content(errorJson("Invalid request: "+std::string(e.what()), "invalid_request_error").dump(), "application/json");
    }
}

void handleDeleteModelFiles(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];
    std::string variant;
    if(req.has_param("variant"))
    {
        variant=req.get_param_value("variant");
    }

    // Check if guarded
    if(!variant.empty()&&StorageManager::instance().isGuarded(modelName, variant))
    {
        std::optional<DownloadedModelFile> stats=StorageManager::instance().getVariantStats(modelName, variant);
        bool hotReady=stats.has_value()?stats->hotReady:false;
        bool isProtected=stats.has_value()?stats->isProtected:false;

        res.status=409;
        res.set_content(nlohmann::json{
            {"error", {
                {"message", "Cannot delete variant '"+variant+"' of model '"+modelName+"': variant is "+(isProtected?"protected":"hot_ready")},
                {"type", "invalid_request_error"},
                {"details", {
                    {"hot_ready", hotReady},
                    {"protected", isProtected}
                }}
            }}
        }.dump(), "application/json");
        return;
    }

    // Unload from ModelRuntime if loaded
    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(modelName);
    if(state.has_value()&&(state->state==ModelState::Loaded||state->state==ModelState::Ready))
    {
        if(variant.empty()||state->variant==variant)
        {
            ArbiterAI::instance().unloadModel(modelName);
        }
    }

    int64_t freedBytes=0;
    bool deleted=StorageManager::instance().deleteModelFile(modelName, variant, freedBytes);

    if(!deleted)
    {
        if(freedBytes==0)
        {
            res.status=404;
            res.set_content(errorJson("Model files not found: "+modelName, "not_found_error").dump(), "application/json");
        }
        else
        {
            res.status=409;
            res.set_content(errorJson("Cannot delete: variant is hot_ready or protected", "invalid_request_error").dump(), "application/json");
        }
        return;
    }

    nlohmann::json response={
        {"status", "deleted"},
        {"model", modelName},
        {"freed_bytes", freedBytes}
    };
    if(!variant.empty())
    {
        response["variant"]=variant;
    }

    res.set_content(response.dump(), "application/json");
}

void handleSetHotReady(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];
    std::string variant=req.matches[2];

    if(!StorageManager::instance().setHotReady(modelName, variant, true))
    {
        res.status=404;
        res.set_content(errorJson("Variant not found in downloaded inventory", "not_found_error").dump(), "application/json");
        return;
    }

    res.set_content(nlohmann::json{
        {"status", "hot_ready_set"},
        {"model", modelName},
        {"variant", variant}
    }.dump(), "application/json");
}

void handleClearHotReady(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];
    std::string variant=req.matches[2];

    if(!StorageManager::instance().setHotReady(modelName, variant, false))
    {
        res.status=404;
        res.set_content(errorJson("Variant not found", "not_found_error").dump(), "application/json");
        return;
    }

    res.set_content(nlohmann::json{
        {"status", "hot_ready_cleared"},
        {"model", modelName},
        {"variant", variant}
    }.dump(), "application/json");
}

void handleSetProtected(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];
    std::string variant=req.matches[2];

    if(!StorageManager::instance().setProtected(modelName, variant, true))
    {
        res.status=404;
        res.set_content(errorJson("Variant not found in downloaded inventory", "not_found_error").dump(), "application/json");
        return;
    }

    res.set_content(nlohmann::json{
        {"status", "protected_set"},
        {"model", modelName},
        {"variant", variant}
    }.dump(), "application/json");
}

void handleClearProtected(const httplib::Request &req, httplib::Response &res)
{
    std::string modelName=req.matches[1];
    std::string variant=req.matches[2];

    if(!StorageManager::instance().setProtected(modelName, variant, false))
    {
        res.status=404;
        res.set_content(errorJson("Variant not found", "not_found_error").dump(), "application/json");
        return;
    }

    res.set_content(nlohmann::json{
        {"status", "protected_cleared"},
        {"model", modelName},
        {"variant", variant}
    }.dump(), "application/json");
}

void handleGetCleanupPreview(const httplib::Request &, httplib::Response &res)
{
    std::vector<CleanupCandidate> candidates=StorageManager::instance().previewCleanup();

    int64_t totalFreeable=0;
    nlohmann::json candidatesJson=nlohmann::json::array();
    for(const CleanupCandidate &c:candidates)
    {
        candidatesJson.push_back({
            {"model", c.modelName},
            {"variant", c.variant},
            {"filename", c.filename},
            {"file_size_bytes", c.fileSizeBytes},
            {"file_size_display", formatBytesDisplay(c.fileSizeBytes)},
            {"last_used_at", timePointToIsoStr(c.lastUsedAt)},
            {"usage_count", c.usageCount}
        });
        totalFreeable+=c.fileSizeBytes;
    }

    nlohmann::json response={
        {"candidates", candidatesJson},
        {"candidate_count", static_cast<int>(candidates.size())},
        {"total_freeable_bytes", totalFreeable},
        {"total_freeable_display", formatBytesDisplay(totalFreeable)}
    };

    res.set_content(response.dump(), "application/json");
}

void handleRunCleanup(const httplib::Request &, httplib::Response &res)
{
    int64_t freed=StorageManager::instance().runCleanup();

    res.set_content(nlohmann::json{
        {"status", "completed"},
        {"freed_bytes", freed},
        {"freed_display", formatBytesDisplay(freed)}
    }.dump(), "application/json");
}

void handleGetCleanupConfig(const httplib::Request &, httplib::Response &res)
{
    CleanupPolicy policy=StorageManager::instance().getCleanupPolicy();

    res.set_content(nlohmann::json{
        {"enabled", policy.enabled},
        {"max_age_days", policy.maxAge.count()/24},
        {"check_interval_hours", policy.checkInterval.count()},
        {"target_free_percent", policy.targetFreePercent},
        {"respect_hot_ready", policy.respectHotReady},
        {"respect_protected", policy.respectProtected}
    }.dump(), "application/json");
}

void handleSetCleanupConfig(const httplib::Request &req, httplib::Response &res)
{
    try
    {
        nlohmann::json body=nlohmann::json::parse(req.body);

        CleanupPolicy policy=StorageManager::instance().getCleanupPolicy();

        if(body.contains("enabled")) policy.enabled=body["enabled"].get<bool>();
        if(body.contains("max_age_days")) policy.maxAge=std::chrono::hours(body["max_age_days"].get<int>()*24);
        if(body.contains("check_interval_hours")) policy.checkInterval=std::chrono::hours(body["check_interval_hours"].get<int>());
        if(body.contains("target_free_percent")) policy.targetFreePercent=body["target_free_percent"].get<double>();

        StorageManager::instance().setCleanupPolicy(policy);

        res.set_content(nlohmann::json{
            {"status", "updated"},
            {"enabled", policy.enabled},
            {"max_age_days", policy.maxAge.count()/24},
            {"check_interval_hours", policy.checkInterval.count()},
            {"target_free_percent", policy.targetFreePercent}
        }.dump(), "application/json");
    }
    catch(const std::exception &e)
    {
        res.status=400;
        res.set_content(errorJson("Invalid request: "+std::string(e.what()), "invalid_request_error").dump(), "application/json");
    }
}

void handleGetActiveDownloads(const httplib::Request &, httplib::Response &res)
{
    // Get snapshots with speed and ETA from ModelRuntime
    std::vector<DownloadProgressSnapshot> snapshots=ModelRuntime::instance().getActiveDownloadSnapshots();

    nlohmann::json downloads=nlohmann::json::array();
    for(const DownloadProgressSnapshot &snap:snapshots)
    {
        nlohmann::json dl={
            {"model", snap.modelName},
            {"variant", snap.variant},
            {"state", "Downloading"},
            {"bytes_downloaded", snap.bytesDownloaded},
            {"total_bytes", snap.totalBytes},
            {"percent_complete", snap.percentComplete},
            {"speed_mbps", snap.speedMbps},
            {"eta_seconds", snap.etaSeconds}
        };
        downloads.push_back(dl);
    }

    // Also include any models in Downloading state that don't have snapshots
    // (e.g. download hasn't started sending data yet)
    std::vector<LoadedModel> models=ModelRuntime::instance().getModelStates();
    for(const LoadedModel &m:models)
    {
        if(m.state!=ModelState::Downloading)
        {
            continue;
        }

        bool alreadyIncluded=false;
        for(const DownloadProgressSnapshot &snap:snapshots)
        {
            if(snap.modelName==m.modelName)
            {
                alreadyIncluded=true;
                break;
            }
        }

        if(!alreadyIncluded)
        {
            nlohmann::json dl={
                {"model", m.modelName},
                {"variant", m.variant},
                {"state", "Downloading"},
                {"bytes_downloaded", 0},
                {"total_bytes", 0},
                {"percent_complete", 0.0},
                {"speed_mbps", 0.0},
                {"eta_seconds", 0}
            };
            downloads.push_back(dl);
        }
    }

    res.set_content(nlohmann::json{{"downloads", downloads}}.dump(), "application/json");
}

// ========== Dashboard ==========

void handleDashboard(const httplib::Request &, httplib::Response &res)
{
    res.set_content(DASHBOARD_HTML, "text/html");
}

} // namespace server
} // namespace arbiterAI
