#include "routes.h"
#include "dashboard.h"

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/modelFitCalculator.h"
#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/telemetryCollector.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>

namespace arbiterAI
{
namespace server
{

namespace
{

nlohmann::json errorJson(const std::string &message, const std::string &type="server_error")
{
    return {{"error", {{"message", message}, {"type", type}}}};
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
    return {
        {"index", gpu.index},
        {"name", gpu.name},
        {"backend", gpuBackendToString(gpu.backend)},
        {"vram_total_mb", gpu.vramTotalMb},
        {"vram_free_mb", gpu.vramFreeMb},
        {"compute_capability", gpu.computeCapability},
        {"utilization_percent", gpu.utilizationPercent}
    };
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

// ========== Route Registration ==========

void registerRoutes(httplib::Server &server)
{
    // Chat completions (OpenAI-compatible)
    server.Post("/v1/chat/completions", handleChatCompletions);
    server.Get("/v1/models", handleListModelsV1);

    // Model management
    server.Get("/api/models", handleGetModels);
    server.Get("/api/models/loaded", handleGetLoadedModels);
    server.Post(R"(/api/models/([^/]+)/load)", handleLoadModel);
    server.Post(R"(/api/models/([^/]+)/unload)", handleUnloadModel);
    server.Post(R"(/api/models/([^/]+)/pin)", handlePinModel);
    server.Post(R"(/api/models/([^/]+)/unpin)", handleUnpinModel);
    server.Post(R"(/api/models/([^/]+)/download)", handleDownloadModel);
    server.Get(R"(/api/models/([^/]+)/download)", handleGetDownloadStatus);

    // Telemetry
    server.Get("/api/stats", handleGetStats);
    server.Get("/api/stats/history", handleGetStatsHistory);
    server.Get("/api/stats/swaps", handleGetStatsSwaps);
    server.Get("/api/hardware", handleGetHardware);

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
        res.set_content(errorJson("Failed to parse JSON body", "invalid_request_error").dump(), "application/json");
        return;
    }

    CompletionRequest arbiterRequest;

    try
    {
        arbiterRequest.model=requestJson.at("model");

        for(const nlohmann::json &msg:requestJson.at("messages"))
        {
            arbiterRequest.messages.push_back({msg.at("role"), msg.at("content")});
        }

        if(requestJson.contains("temperature"))
            arbiterRequest.temperature=requestJson.at("temperature").get<double>();
        if(requestJson.contains("max_tokens"))
            arbiterRequest.max_tokens=requestJson.at("max_tokens").get<int>();
        if(requestJson.contains("top_p"))
            arbiterRequest.top_p=requestJson.at("top_p").get<double>();
        if(requestJson.contains("presence_penalty"))
            arbiterRequest.presence_penalty=requestJson.at("presence_penalty").get<double>();
        if(requestJson.contains("frequency_penalty"))
            arbiterRequest.frequency_penalty=requestJson.at("frequency_penalty").get<double>();
        if(requestJson.contains("stop"))
            arbiterRequest.stop=requestJson.at("stop").get<std::vector<std::string>>();
    }
    catch(const nlohmann::json::exception &e)
    {
        res.status=400;
        res.set_content(errorJson(std::string("JSON validation error: ")+e.what(), "invalid_request_error").dump(), "application/json");
        return;
    }

    bool stream=requestJson.value("stream", false);

    if(stream)
    {
        res.set_chunked_content_provider(
            "text/event-stream",
            [arbiterRequest](size_t, httplib::DataSink &sink)
            {
                auto callback=[&](const std::string &chunk)
                {
                    if(chunk.empty()) return;
                    nlohmann::json sseChunk={
                        {"id", "chatcmpl-arbiterai"},
                        {"object", "chat.completion.chunk"},
                        {"created", std::time(nullptr)},
                        {"model", arbiterRequest.model},
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
                if(err!=ErrorCode::Success)
                {
                    spdlog::error("Streaming completion failed: {}", errorCodeToString(err));
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
            res.status=500;
            res.set_content(errorJson("Completion failed: "+errorCodeToString(err)).dump(), "application/json");
            return;
        }

        nlohmann::json responseJson={
            {"id", "chatcmpl-arbiterai"},
            {"object", "chat.completion"},
            {"created", std::time(nullptr)},
            {"model", arbiterResponse.model},
            {"choices", {{
                {"index", 0},
                {"message", {{"role", "assistant"}, {"content", arbiterResponse.text}}},
                {"finish_reason", arbiterResponse.finishReason.empty()?"stop":arbiterResponse.finishReason}
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

    nlohmann::json data=nlohmann::json::array();
    for(const std::string &name:modelNames)
    {
        data.push_back({
            {"id", name},
            {"object", "model"},
            {"owned_by", "arbiterai"}
        });
    }

    nlohmann::json response={
        {"object", "list"},
        {"data", data}
    };

    res.set_content(response.dump(), "application/json");
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
    else
    {
        res.status=400;
        res.set_content(errorJson("Failed to load model: "+errorCodeToString(err)).dump(), "application/json");
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
        res.set_content(errorJson("Failed to unload model: "+errorCodeToString(err)).dump(), "application/json");
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
        res.set_content(errorJson("Failed to pin model: "+errorCodeToString(err)).dump(), "application/json");
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
        res.set_content(errorJson("Failed to unpin model: "+errorCodeToString(err)).dump(), "application/json");
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
        res.status=400;
        res.set_content(errorJson("Download failed: "+errorCodeToString(err)).dump(), "application/json");
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

    res.set_content(response.dump(), "application/json");
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

// ========== Dashboard ==========

void handleDashboard(const httplib::Request &, httplib::Response &res)
{
    res.set_content(DASHBOARD_HTML, "text/html");
}

} // namespace server
} // namespace arbiterAI
