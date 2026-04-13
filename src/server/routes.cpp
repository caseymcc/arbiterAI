#include "routes.h"
#include "dashboard.h"
#include "logBuffer.h"

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
        {"unified_memory", gpu.unifiedMemory},
        {"vram_overridden", gpu.vramOverridden}
    };

    if(gpu.unifiedMemory&&gpu.gpuAccessibleRamMb>0)
    {
        j["gpu_accessible_ram_mb"]=gpu.gpuAccessibleRamMb;
        j["gpu_accessible_ram_free_mb"]=gpu.gpuAccessibleRamFreeMb;
    }

    if(!gpu.memoryHeaps.empty())
    {
        nlohmann::json heaps=nlohmann::json::array();
        for(const MemoryHeapInfo &heap:gpu.memoryHeaps)
        {
            nlohmann::json h={
                {"index", heap.index},
                {"device_local", heap.deviceLocal},
                {"size_mb", heap.sizeMb}
            };
            if(gpu.hasMemoryBudget)
            {
                h["budget_mb"]=heap.budgetMb;
                h["usage_mb"]=heap.usageMb;
            }
            heaps.push_back(h);
        }
        j["memory_heaps"]=heaps;
        j["has_memory_budget"]=gpu.hasMemoryBudget;
    }

    return j;
}

nlohmann::json systemInfoToJson(const SystemInfo &hw)
{
    nlohmann::json gpus=nlohmann::json::array();
    for(const GpuInfo &gpu:hw.gpus)
    {
        nlohmann::json gpuJson=gpuInfoToJson(gpu);

        // Attach matched architecture rule (if any)
        std::optional<GpuBackendRule> rule=ModelManager::instance().findGpuBackendRule(gpu.name);
        if(rule)
        {
            nlohmann::json ruleJson={
                {"name", rule->name}
            };
            if(!rule->disabledBackends.empty())
                ruleJson["disabled_backends"]=rule->disabledBackends;
            if(!rule->backendPriority.empty())
                ruleJson["backend_priority"]=rule->backendPriority;
            gpuJson["architecture_rule"]=ruleJson;
        }

        gpus.push_back(gpuJson);
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

nlohmann::json runtimeOptionsToJson(const RuntimeOptions &opts)
{
    nlohmann::json j=nlohmann::json::object();

    if(opts.flashAttn.has_value())
        j["flash_attn"]=opts.flashAttn.value();
    if(opts.kvCacheTypeK.has_value())
        j["kv_cache_type_k"]=opts.kvCacheTypeK.value();
    if(opts.kvCacheTypeV.has_value())
        j["kv_cache_type_v"]=opts.kvCacheTypeV.value();
    if(opts.noMmap.has_value())
        j["no_mmap"]=opts.noMmap.value();
    if(opts.reasoningBudget.has_value())
        j["reasoning_budget"]=opts.reasoningBudget.value();
    if(opts.swaFull.has_value())
        j["swa_full"]=opts.swaFull.value();
    if(opts.nGpuLayers.has_value())
        j["n_gpu_layers"]=opts.nGpuLayers.value();
    if(opts.overrideTensor.has_value())
        j["override_tensor"]=opts.overrideTensor.value();

    return j;
}

RuntimeOptions parseRuntimeOptions(const nlohmann::json &j)
{
    RuntimeOptions opts;

    if(j.contains("flash_attn")&&j["flash_attn"].is_boolean())
        opts.flashAttn=j["flash_attn"].get<bool>();
    if(j.contains("kv_cache_type_k")&&j["kv_cache_type_k"].is_string())
        opts.kvCacheTypeK=j["kv_cache_type_k"].get<std::string>();
    if(j.contains("kv_cache_type_v")&&j["kv_cache_type_v"].is_string())
        opts.kvCacheTypeV=j["kv_cache_type_v"].get<std::string>();
    if(j.contains("no_mmap")&&j["no_mmap"].is_boolean())
        opts.noMmap=j["no_mmap"].get<bool>();
    if(j.contains("reasoning_budget")&&j["reasoning_budget"].is_number_integer())
        opts.reasoningBudget=j["reasoning_budget"].get<int>();
    if(j.contains("swa_full")&&j["swa_full"].is_boolean())
        opts.swaFull=j["swa_full"].get<bool>();
    if(j.contains("n_gpu_layers")&&j["n_gpu_layers"].is_number_integer())
        opts.nGpuLayers=j["n_gpu_layers"].get<int>();
    if(j.contains("override_tensor")&&j["override_tensor"].is_string())
        opts.overrideTensor=j["override_tensor"].get<std::string>();

    return opts;
}

nlohmann::json loadedModelToJson(const LoadedModel &m)
{
    nlohmann::json gpuIndices=nlohmann::json::array();
    for(int idx:m.gpuIndices)
    {
        gpuIndices.push_back(idx);
    }

    nlohmann::json j={
        {"model", m.modelName},
        {"variant", m.variant},
        {"state", modelStateToString(m.state)},
        {"vram_usage_mb", m.vramUsageMb},
        {"ram_usage_mb", m.ramUsageMb},
        {"estimated_vram_mb", m.estimatedVramUsageMb},
        {"context_size", m.contextSize},
        {"max_context_size", m.maxContextSize},
        {"gpu_indices", gpuIndices},
        {"pinned", m.pinned}
    };

    nlohmann::json activeOpts=runtimeOptionsToJson(m.activeOptions);
    if(!activeOpts.empty())
    {
        j["runtime_options"]=activeOpts;
    }

    return j;
}

nlohmann::json inferenceStatsToJson(const InferenceStats &s)
{
    return {
        {"model", s.model},
        {"variant", s.variant},
        {"tokens_per_second", s.tokensPerSecond},
        {"prompt_tokens_per_second", s.promptTokensPerSecond},
        {"generation_tokens_per_second", s.generationTokensPerSecond},
        {"prompt_tokens", s.promptTokens},
        {"completion_tokens", s.completionTokens},
        {"latency_ms", s.latencyMs},
        {"total_time_ms", s.totalTimeMs},
        {"prompt_time_ms", s.promptTimeMs},
        {"generation_time_ms", s.generationTimeMs}
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

/// Parse a model identifier that may contain a ":variant" suffix.
/// Examples:
///   "Qwen3.5-27B:Q4_K_M" → ("Qwen3.5-27B", "Q4_K_M")
///   "gpt-4"               → ("gpt-4", "")
///   "gpt-oss-120b:Q8_0"   → ("gpt-oss-120b", "Q8_0")
/// Uses rfind to handle model names that may themselves contain colons.
std::pair<std::string, std::string> parseModelVariant(const std::string &modelId)
{
    // Only split on ':' if the suffix looks like a quantization variant
    // (starts with Q, F, IQ, or BF — e.g. Q4_K_M, F16, IQ4_XS, BF16).
    // This avoids breaking model names that contain colons for other reasons.
    size_t pos=modelId.rfind(':');
    if(pos!=std::string::npos&&pos+1<modelId.size())
    {
        std::string suffix=modelId.substr(pos+1);
        char first=suffix[0];

        if(first=='Q'||first=='q'||first=='F'||first=='f'
            ||(suffix.size()>=2&&(suffix.substr(0, 2)=="IQ"||suffix.substr(0, 2)=="iq"
                ||suffix.substr(0, 2)=="BF"||suffix.substr(0, 2)=="bf")))
        {
            return {modelId.substr(0, pos), suffix};
        }
    }
    return {modelId, ""};
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
    server.Post("/api/hardware/vram-override", handleSetVramOverride);
    server.Delete(R"(/api/hardware/vram-override/(\d+))", handleClearVramOverride);

    // Logs
    server.Get("/api/logs", handleGetLogs);

    // Runtime options
    server.Get("/api/runtime-options", handleGetRuntimeOptions);

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
    server.Get("/dashboard/storage", handleDashboardStorage);
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

        // Parse "model:variant" syntax (e.g. "Qwen3.5-27B:Q4_K_M")
        // Strip the variant from the model name so the core API gets the bare
        // model name.  If a variant was specified, pre-load it so the llama
        // provider uses the right quantization.
        auto [baseName, requestedVariant]=parseModelVariant(arbiterRequest.model);
        arbiterRequest.model=baseName;

        if(!requestedVariant.empty())
        {
            ErrorCode loadErr=ArbiterAI::instance().loadModel(baseName, requestedVariant);
            if(loadErr==ErrorCode::ModelDownloading)
            {
                res.status=503;
                res.set_content(errorJson("Model '"+baseName+"' variant '"+requestedVariant
                    +"' is still downloading", "server_error", "model", "model_downloading").dump(),
                    "application/json");
                return;
            }
            if(loadErr!=ErrorCode::Success)
            {
                spdlog::warn("Failed to pre-load model '{}' variant '{}' (error={})",
                    baseName, requestedVariant, errorCodeToString(loadErr));
            }
        }

        // Parse messages with full OpenAI message format support
        for(const nlohmann::json &msg:requestJson.at("messages"))
        {
            Message m;
            m.role=msg.at("role").get<std::string>();

            // content can be null for assistant messages with tool_calls
            // content can be a string or an array of content parts (OpenAI spec)
            if(msg.contains("content") && !msg.at("content").is_null())
                m.content=contentToString(msg.at("content"));

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
    std::string responseModelId=requestJson.at("model").get<std::string>();

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
            [arbiterRequest, requestId, created, includeUsage, responseModelId](size_t, httplib::DataSink &sink)
            {
                // Send initial chunk with role
                nlohmann::json roleChunk={
                    {"id", requestId},
                    {"object", "chat.completion.chunk"},
                    {"created", created},
                    {"model", responseModelId},
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
                        {"model", responseModelId},
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
                    {"model", responseModelId},
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
                        {"model", responseModelId},
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
            {"model", responseModelId},
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
        // Always emit the bare model name
        data.push_back({
            {"id", name},
            {"object", "model"},
            {"created", created},
            {"owned_by", "arbiterai"},
            {"permission", nlohmann::json::array()}
        });

        // For models with variants, also emit "model:variant" entries
        ModelInfo info;
        if(ArbiterAI::instance().getModelInfo(name, info)==ErrorCode::Success
            &&!info.variants.empty())
        {
            for(const ModelVariant &v:info.variants)
            {
                data.push_back({
                    {"id", name+":"+v.quantization},
                    {"object", "model"},
                    {"created", created},
                    {"owned_by", "arbiterai"},
                    {"permission", nlohmann::json::array()}
                });
            }
        }
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
    auto [baseName, variantName]=parseModelVariant(modelId);

    ModelInfo info;
    bool found=(ArbiterAI::instance().getModelInfo(baseName, info)==ErrorCode::Success);

    // If a variant was specified, verify it exists on this model
    if(found&&!variantName.empty()&&!info.variants.empty())
    {
        bool variantFound=false;
        for(const ModelVariant &v:info.variants)
        {
            if(v.quantization==variantName)
            {
                variantFound=true;
                break;
            }
        }
        if(!variantFound) found=false;
    }
    else if(!variantName.empty()&&info.variants.empty())
    {
        // Variant requested but model has no variants
        found=false;
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
        {"patch", ver.patch},
        {"llamaCppBuild", ver.llamaCppBuild}
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
        nlohmann::json modelJson=modelFitToJson(f);

        // Include runtime_options and backend_priority from model config
        ModelInfo info;
        if(ArbiterAI::instance().getModelInfo(f.model, info)==ErrorCode::Success)
        {
            nlohmann::json opts=runtimeOptionsToJson(info.runtimeOptions);
            if(!opts.empty())
            {
                modelJson["runtime_options"]=opts;
            }
            if(!info.backendPriority.empty())
            {
                modelJson["backend_priority"]=info.backendPriority;
            }
        }

        models.push_back(modelJson);
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
            nlohmann::json modelJson={
                {"model", name},
                {"variant", ""},
                {"can_run", true},
                {"max_context_size", 0},
                {"limiting_factor", ""},
                {"estimated_vram_mb", 0},
                {"gpu_indices", nlohmann::json::array()}
            };

            ModelInfo info;
            if(ArbiterAI::instance().getModelInfo(name, info)==ErrorCode::Success)
            {
                nlohmann::json opts=runtimeOptionsToJson(info.runtimeOptions);
                if(!opts.empty())
                {
                    modelJson["runtime_options"]=opts;
                }
                if(!info.backendPriority.empty())
                {
                    modelJson["backend_priority"]=info.backendPriority;
                }
            }

            models.push_back(modelJson);
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
        RuntimeOptions optionsOverride;

        // Accept parameters from query string
        if(req.has_param("variant"))
            variant=req.get_param_value("variant");
        if(req.has_param("context"))
            contextSize=std::stoi(req.get_param_value("context"));

        // Also accept from JSON body (body takes precedence)
        if(!req.body.empty())
        {
            try
            {
                nlohmann::json body=nlohmann::json::parse(req.body);
                if(body.contains("variant")&&body["variant"].is_string())
                    variant=body["variant"].get<std::string>();
                if(body.contains("context")&&body["context"].is_number_integer())
                    contextSize=body["context"].get<int>();
                if(body.contains("context_size")&&body["context_size"].is_number_integer())
                    contextSize=body["context_size"].get<int>();
                if(body.contains("runtime_options")&&body["runtime_options"].is_object())
                    optionsOverride=parseRuntimeOptions(body["runtime_options"]);
            }
            catch(const nlohmann::json::parse_error &)
            {
                // Not JSON body — ignore, use query params
            }
        }

        spdlog::info("Load request: model='{}' variant='{}' context={}", modelName, variant, contextSize);

        ErrorCode err=ArbiterAI::instance().loadModel(modelName, variant, contextSize, &optionsOverride);

        if(err==ErrorCode::Success)
        {
            // Include the actual context info in the response
            nlohmann::json response={{"status", "loaded"}, {"model", modelName}};

            std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(modelName);
            if(state.has_value())
            {
                response["context_size"]=state->contextSize;
                response["max_context_size"]=state->maxContextSize;

                nlohmann::json activeOpts=runtimeOptionsToJson(state->activeOptions);
                if(!activeOpts.empty())
                {
                    response["runtime_options"]=activeOpts;
                }
            }

            res.set_content(response.dump(), "application/json");
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
            std::string errCode=errorCodeToString(err);
            std::string reason;

            switch(err)
            {
                case ErrorCode::ModelNotFound:
                    reason="Model '"+modelName+"' is not defined in any loaded configuration file. "
                        "Check that the model name matches a config entry and that the config path is correct.";
                    break;
                case ErrorCode::ModelLoadError:
                {
                    LoadErrorDetail detail=ModelRuntime::instance().getLastLoadError();
                    reason=detail.summary;
                    if(reason.empty())
                    {
                        reason="Failed to load model '"+modelName+"'";
                        if(!variant.empty()) reason+=" variant '"+variant+"'";
                        reason+=". Check the server log for details.";
                    }
                    break;
                }
                case ErrorCode::ModelDownloadFailed:
                    reason="Download failed for model '"+modelName+"'";
                    if(!variant.empty()) reason+=" variant '"+variant+"'";
                    reason+=". The download URL may be unreachable or the SHA256 hash did not match.";
                    break;
                case ErrorCode::InvalidRequest:
                    reason="Invalid request for model '"+modelName+"'. "
                        "The model may be a local provider without variants defined.";
                    break;
                case ErrorCode::UnsupportedProvider:
                    reason="The provider for model '"+modelName+"' is not supported or not enabled in this build.";
                    break;
                default:
                    reason="Unexpected error loading model '"+modelName+"': "+errCode;
                    break;
            }

            spdlog::warn("Load failed: model='{}' variant='{}' error={} — {}", modelName, variant, errCode, reason);

            // Build detail payload; include structured error fields for programmatic handling
            LoadErrorDetail loadDetail=ModelRuntime::instance().getLastLoadError();

            nlohmann::json details={
                {"model", modelName},
                {"variant", variant.empty()?nlohmann::json(nullptr):nlohmann::json(variant)},
                {"context_requested", contextSize},
                {"error_code", errCode},
                {"reason", loadFailureReasonToString(loadDetail.reason)},
                {"recoverable", loadDetail.recoverable}
            };

            if(!loadDetail.action.empty())
            {
                details["action"]=loadDetail.action;
            }
            if(!loadDetail.suggestion.empty())
            {
                details["suggestion"]=loadDetail.suggestion;
            }
            if(!loadDetail.llamaLog.empty())
            {
                // Trim to a reasonable size for the response
                std::string logSnippet=loadDetail.llamaLog;
                if(logSnippet.size()>2000)
                {
                    logSnippet=logSnippet.substr(logSnippet.size()-2000);
                }
                details["llama_log"]=logSnippet;
            }

            nlohmann::json body={
                {"error", {
                    {"message", reason},
                    {"type", "invalid_request_error"},
                    {"code", errCode},
                    {"param", "model"},
                    {"details", details}
                }}
            };
            res.status=400;
            res.set_content(body.dump(), "application/json");
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

    std::string variant;
    if(req.has_param("variant"))
        variant=req.get_param_value("variant");

    ErrorCode err=ArbiterAI::instance().downloadModel(modelName, variant);

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

    // Legacy: persist to single override file if configured
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

    // Legacy: persist to single override file if configured
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

    // Legacy: persist to single override file if configured
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
        {"avg_prompt_tokens_per_second", snapshot.avgPromptTokensPerSecond},
        {"avg_generation_tokens_per_second", snapshot.avgGenerationTokensPerSecond},
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

    nlohmann::json j=systemInfoToJson(hw);

    std::vector<std::string> defaultBP=ModelRuntime::instance().getDefaultBackendPriority();
    if(!defaultBP.empty())
    {
        j["default_backend_priority"]=defaultBP;
    }

    res.set_content(j.dump(), "application/json");
}

void handleSetVramOverride(const httplib::Request &req, httplib::Response &res)
{
    nlohmann::json body;

    try
    {
        body=nlohmann::json::parse(req.body);
    }
    catch(const std::exception &)
    {
        res.status=400;
        res.set_content(R"({"error":"Invalid JSON body"})", "application/json");
        return;
    }

    if(!body.contains("gpu_index")||!body.contains("vram_mb"))
    {
        res.status=400;
        res.set_content(R"({"error":"Missing required fields: gpu_index, vram_mb"})", "application/json");
        return;
    }

    int gpuIndex=body["gpu_index"].get<int>();
    int vramMb=body["vram_mb"].get<int>();

    if(vramMb<=0)
    {
        res.status=400;
        res.set_content(R"({"error":"vram_mb must be positive"})", "application/json");
        return;
    }

    HardwareDetector::instance().setVramOverride(gpuIndex, vramMb);

    nlohmann::json result={
        {"status", "ok"},
        {"gpu_index", gpuIndex},
        {"vram_mb", vramMb}
    };

    res.set_content(result.dump(), "application/json");
}

void handleClearVramOverride(const httplib::Request &req, httplib::Response &res)
{
    int gpuIndex=std::stoi(req.matches[1]);

    HardwareDetector::instance().clearVramOverride(gpuIndex);

    // Refresh to restore detected values
    HardwareDetector::instance().refresh();

    nlohmann::json result={
        {"status", "ok"},
        {"gpu_index", gpuIndex}
    };

    res.set_content(result.dump(), "application/json");
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
    nlohmann::json j={
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
        {"runtime_state", f.runtimeState},
        {"file_count", 1+static_cast<int>(f.additionalFiles.size())}
    };
    if(!f.additionalFiles.empty())
    {
        j["additional_files"]=f.additionalFiles;
    }
    return j;
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

    bool ascending=(sortOrder=="asc");

    auto compare=[&](const DownloadedModelFile &a, const DownloadedModelFile &b) -> bool
    {
        // For descending, swap a and b so strict weak ordering is maintained.
        const DownloadedModelFile &lhs=ascending?a:b;
        const DownloadedModelFile &rhs=ascending?b:a;

        if(sortField=="name") return lhs.modelName<rhs.modelName;
        if(sortField=="size") return lhs.fileSizeBytes<rhs.fileSizeBytes;
        if(sortField=="usage_count") return lhs.usageCount<rhs.usageCount;
        return lhs.lastUsedAt<rhs.lastUsedAt; // default: last_used
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

    // Reject deletion of models that are currently downloading
    std::optional<LoadedModel> state=ModelRuntime::instance().getModelState(modelName);
    if(state.has_value()&&state->state==ModelState::Downloading)
    {
        if(variant.empty()||state->variant==variant)
        {
            res.status=409;
            res.set_content(nlohmann::json{
                {"error", {
                    {"message", "Cannot delete model '"+modelName+"': download is in progress"},
                    {"type", "invalid_request_error"}
                }}
            }.dump(), "application/json");
            return;
        }
    }

    // Unload from ModelRuntime if loaded
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

// ========== Runtime Options ==========

void handleGetRuntimeOptions(const httplib::Request &, httplib::Response &res)
{
    // Return a schema describing all available runtime options, their types,
    // defaults, and valid values — so callers know what can be set.
    nlohmann::json options=nlohmann::json::array();

    options.push_back({
        {"name", "flash_attn"},
        {"type", "boolean"},
        {"description", "Enable or disable flash attention (-fa). Some models crash with it enabled."},
        {"default", nullptr}
    });
    options.push_back({
        {"name", "kv_cache_type_k"},
        {"type", "string"},
        {"description", "KV cache data type for keys (-ctk). Lower precision uses less VRAM."},
        {"valid_values", {"f32", "f16", "bf16", "q8_0", "q4_0", "q4_1", "q5_0", "q5_1"}},
        {"default", "f16"}
    });
    options.push_back({
        {"name", "kv_cache_type_v"},
        {"type", "string"},
        {"description", "KV cache data type for values (-ctv). Lower precision uses less VRAM."},
        {"valid_values", {"f32", "f16", "bf16", "q8_0", "q4_0", "q4_1", "q5_0", "q5_1"}},
        {"default", "f16"}
    });
    options.push_back({
        {"name", "no_mmap"},
        {"type", "boolean"},
        {"description", "Disable memory-mapped file I/O (--no-mmap). Required for some models/systems."},
        {"default", false}
    });
    options.push_back({
        {"name", "reasoning_budget"},
        {"type", "integer"},
        {"description", "Reasoning token budget (--reasoning-budget). 0 disables reasoning/thinking tokens."},
        {"default", nullptr}
    });
    options.push_back({
        {"name", "swa_full"},
        {"type", "boolean"},
        {"description", "Use full-size sliding window attention cache (--swa-full)."},
        {"default", nullptr}
    });
    options.push_back({
        {"name", "n_gpu_layers"},
        {"type", "integer"},
        {"description", "Number of layers to offload to GPU (-ngl). 99 offloads all layers."},
        {"default", 99}
    });
    options.push_back({
        {"name", "override_tensor"},
        {"type", "string"},
        {"description", "Tensor override pattern (-ot). Advanced: route specific tensors to CPU/GPU."},
        {"default", nullptr}
    });

    nlohmann::json backendPriorityInfo={
        {"name", "backend_priority"},
        {"type", "array of strings"},
        {"description", "Ordered preference for GPU backends. First available backend is used."},
        {"valid_values", {"vulkan", "rocm", "cuda"}}
    };

    res.set_content(nlohmann::json{
        {"runtime_options", options},
        {"backend_priority", backendPriorityInfo}
    }.dump(), "application/json");
}

// ========== Logs ==========

void handleGetLogs(const httplib::Request &req, httplib::Response &res)
{
    auto &sink=logBufferSinkInstance();
    if(!sink)
    {
        res.set_content(nlohmann::json{{"logs", nlohmann::json::array()}}.dump(), "application/json");
        return;
    }

    size_t count=200;
    std::string levelFilter;

    if(req.has_param("count"))
    {
        count=static_cast<size_t>(std::stoi(req.get_param_value("count")));
        if(count>1000) count=1000;
    }
    if(req.has_param("level"))
    {
        levelFilter=req.get_param_value("level");
    }

    std::deque<LogEntry> entries=sink->getEntries(count);

    nlohmann::json logs=nlohmann::json::array();
    for(const LogEntry &entry:entries)
    {
        if(!levelFilter.empty()&&entry.level!=levelFilter)
            continue;

        auto epochMs=std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.timestamp.time_since_epoch()).count();

        // Format ISO timestamp
        std::time_t t=std::chrono::system_clock::to_time_t(entry.timestamp);
        std::tm tm{};
        gmtime_r(&t, &tm);

        auto ms=epochMs%1000;
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

        std::ostringstream ts;
        ts<<buf<<"."<<std::setfill('0')<<std::setw(3)<<ms<<"Z";

        logs.push_back({
            {"timestamp", ts.str()},
            {"epoch_ms", epochMs},
            {"level", entry.level},
            {"message", entry.message}
        });
    }

    res.set_content(nlohmann::json{{"logs", logs}}.dump(), "application/json");
}

// ========== Dashboard ==========

void handleDashboard(const httplib::Request &, httplib::Response &res)
{
    res.set_content(DASHBOARD_HTML, "text/html");
}

void handleDashboardStorage(const httplib::Request &, httplib::Response &res)
{
    res.set_content(DASHBOARD_STORAGE_HTML, "text/html");
}

} // namespace server
} // namespace arbiterAI
