#include "routes.h"
#include "logBuffer.h"

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/storageManager.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

namespace
{

int64_t parseStorageLimit(const std::string &str)
{
    if(str.empty()||str=="0") return 0;

    char suffix=str.back();
    std::string numStr=str;

    if(suffix=='G'||suffix=='g')
    {
        numStr.pop_back();
        return static_cast<int64_t>(std::stod(numStr)*1073741824);
    }
    if(suffix=='M'||suffix=='m')
    {
        numStr.pop_back();
        return static_cast<int64_t>(std::stod(numStr)*1048576);
    }
    if(suffix=='K'||suffix=='k')
    {
        numStr.pop_back();
        return static_cast<int64_t>(std::stod(numStr)*1024);
    }
    return std::stoll(str);
}

void printUsage()
{
    std::cout<<"Usage: arbiterAI-server [options]\n"
        "\n"
        "Options:\n"
        "  -c, --config <path>   Path to server configuration JSON file (required)\n"
        "  -h, --help            Print this help message\n"
        "\n"
        "See examples/server_config.json for the configuration file format.\n";
}

} // anonymous namespace

int main(int argc, char *argv[])
{
    // ── Parse CLI — only --config and --help ──────────────────────
    std::string configPath;

    for(int i=1; i<argc; ++i)
    {
        std::string arg=argv[i];

        if(arg=="-h"||arg=="--help")
        {
            printUsage();
            return 0;
        }
        if((arg=="-c"||arg=="--config")&&i+1<argc)
        {
            configPath=argv[++i];
        }
        else if(arg.rfind("--config=", 0)==0)
        {
            configPath=arg.substr(9);
        }
        else
        {
            std::cerr<<"Unknown argument: "<<arg<<"\n";
            printUsage();
            return 1;
        }
    }

    if(configPath.empty())
    {
        std::cerr<<"Error: --config <path> is required.\n\n";
        printUsage();
        return 1;
    }

    // ── Load config file ─────────────────────────────────────────
    nlohmann::json cfg;

    try
    {
        std::ifstream file(configPath);

        if(!file.is_open())
        {
            std::cerr<<"Error: cannot open config file: "<<configPath<<"\n";
            return 1;
        }

        cfg=nlohmann::json::parse(file, nullptr, true, true);
    }
    catch(const std::exception &e)
    {
        std::cerr<<"Error parsing config file: "<<e.what()<<"\n";
        return 1;
    }

    // ── Extract settings with defaults ───────────────────────────
    std::string host=cfg.value("host", "0.0.0.0");
    int port=cfg.value("port", 8080);

    std::vector<std::string> modelConfigPaths;
    if(cfg.contains("model_config_paths")&&cfg["model_config_paths"].is_array())
    {
        for(const nlohmann::json &p:cfg["model_config_paths"])
        {
            modelConfigPaths.push_back(p.get<std::string>());
        }
    }
    if(modelConfigPaths.empty())
    {
        modelConfigPaths.push_back("config");
    }

    std::string modelsDir=cfg.value("models_dir", "/models");
    std::string defaultModel=cfg.value("default_model", "");
    std::string defaultVariant=cfg.value("default_variant", "");
    std::string overridePath=cfg.value("override_path", "");
    std::string injectedConfigDir=cfg.value("injected_config_dir", "");
    int ramBudget=cfg.value("ram_budget_mb", 0);
    int maxDownloads=cfg.value("max_concurrent_downloads", 2);

    // Storage
    nlohmann::json storageCfg=cfg.value("storage", nlohmann::json::object());
    std::string storageLimitStr=storageCfg.value("limit", "0");
    bool cleanupEnabled=storageCfg.value("cleanup_enabled", true);
    int cleanupMaxAgeDays=storageCfg.value("cleanup_max_age_days", 30);
    int cleanupIntervalHours=storageCfg.value("cleanup_interval_hours", 24);

    // Hardware
    nlohmann::json hwCfg=cfg.value("hardware", nlohmann::json::object());
    nlohmann::json vramOverrides=hwCfg.value("vram_overrides", nlohmann::json::object());

    std::vector<std::string> defaultBackendPriority;
    if(hwCfg.contains("default_backend_priority")&&hwCfg["default_backend_priority"].is_array())
    {
        for(const nlohmann::json &bp:hwCfg["default_backend_priority"])
        {
            defaultBackendPriority.push_back(bp.get<std::string>());
        }
    }

    // Logging
    nlohmann::json logCfg=cfg.value("logging", nlohmann::json::object());
    std::string logLevel=logCfg.value("level", "info");
    std::string logDir=logCfg.value("directory", "");
    int logRotateHour=logCfg.value("rotate_hour", 0);
    int logRetainDays=logCfg.value("retain_days", 7);

    if(logRotateHour<0||logRotateHour>23) logRotateHour=0;
    if(logRetainDays<1) logRetainDays=1;

    // ── Configure logging ────────────────────────────────────────
    if(logLevel=="trace")       spdlog::set_level(spdlog::level::trace);
    else if(logLevel=="debug")  spdlog::set_level(spdlog::level::debug);
    else if(logLevel=="info")   spdlog::set_level(spdlog::level::info);
    else if(logLevel=="warn")   spdlog::set_level(spdlog::level::warn);
    else if(logLevel=="error")  spdlog::set_level(spdlog::level::err);
    else                        spdlog::set_level(spdlog::level::info);

    auto consoleSink=std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto bufferSink=std::make_shared<arbiterAI::server::LogBufferSink>(1000);
    arbiterAI::server::logBufferSinkInstance()=bufferSink;

    std::vector<spdlog::sink_ptr> sinks{consoleSink, bufferSink};

    if(!logDir.empty())
    {
        std::filesystem::create_directories(logDir);
        std::string logPath=logDir+"/arbiterAI-server.log";

        auto fileSink=std::make_shared<spdlog::sinks::daily_file_sink_mt>(
            logPath, logRotateHour, 0, false,
            static_cast<uint16_t>(logRetainDays));
        sinks.push_back(fileSink);
    }

    auto logger=std::make_shared<spdlog::logger>("arbiterAI",
        sinks.begin(), sinks.end());
    logger->set_level(spdlog::get_level());
    spdlog::set_default_logger(logger);

    spdlog::info("Loaded config from: {}", configPath);

    if(!logDir.empty())
    {
        spdlog::info("File logging enabled: dir={}, rotate at {:02d}:00, retain {} days",
            logDir, logRotateHour, logRetainDays);
    }

    // ── Initialize ArbiterAI ─────────────────────────────────────
    std::vector<std::filesystem::path> fsPaths;
    for(const std::string &s:modelConfigPaths)
    {
        fsPaths.push_back(s);
    }

    spdlog::info("Initializing ArbiterAI...");

    arbiterAI::ArbiterAI &ai=arbiterAI::ArbiterAI::instance();
    arbiterAI::ErrorCode err=ai.initialize(fsPaths);

    if(err!=arbiterAI::ErrorCode::Success)
    {
        spdlog::error("Failed to initialize ArbiterAI (error={})", static_cast<int>(err));
        return 1;
    }

    spdlog::info("ArbiterAI initialized successfully");

    // ── Restore injected model configs ───────────────────────────
    if(!injectedConfigDir.empty())
    {
        arbiterAI::ModelManager::instance().setInjectedConfigDir(injectedConfigDir);
        int restored=arbiterAI::ModelManager::instance().loadInjectedConfigs();
        if(restored>0)
        {
            spdlog::info("Restored {} injected model config(s)", restored);
        }
    }

    // ── Apply VRAM overrides ─────────────────────────────────────
    for(auto it=vramOverrides.begin(); it!=vramOverrides.end(); ++it)
    {
        int gpuIndex=std::stoi(it.key());
        int vramMb=it.value().get<int>();

        if(vramMb>0)
        {
            arbiterAI::HardwareDetector::instance().setVramOverride(gpuIndex, vramMb);
            spdlog::info("VRAM override set for GPU {}: {} MB", gpuIndex, vramMb);
        }
    }

    // ── Configure StorageManager ─────────────────────────────────
    int64_t storageLimitBytes=parseStorageLimit(storageLimitStr);

    arbiterAI::StorageManager &storage=arbiterAI::StorageManager::instance();
    storage.initialize(modelsDir);

    if(storageLimitBytes>0)
    {
        storage.setStorageLimit(storageLimitBytes);
        spdlog::info("Storage limit set to {} bytes", storageLimitBytes);
    }

    arbiterAI::CleanupPolicy cleanupPolicy;
    cleanupPolicy.enabled=cleanupEnabled;
    cleanupPolicy.maxAge=std::chrono::hours(cleanupMaxAgeDays*24);
    cleanupPolicy.checkInterval=std::chrono::hours(cleanupIntervalHours);
    storage.setCleanupPolicy(cleanupPolicy);
    spdlog::info("Cleanup policy: enabled={}, maxAge={}d, interval={}h",
        cleanupEnabled, cleanupMaxAgeDays, cleanupIntervalHours);

    // ── RAM budget ───────────────────────────────────────────────
    if(ramBudget>0)
    {
        arbiterAI::ModelRuntime::instance().setReadyRamBudget(ramBudget);
        spdlog::info("Ready model RAM budget set to {} MB", ramBudget);
    }

    // ── Default backend priority ─────────────────────────────────
    if(!defaultBackendPriority.empty())
    {
        arbiterAI::ModelRuntime::instance().setDefaultBackendPriority(defaultBackendPriority);
    }

    // ── Concurrent download limit ────────────────────────────────
    if(maxDownloads>0)
    {
        arbiterAI::ModelRuntime::instance().setMaxConcurrentDownloads(maxDownloads);
        spdlog::info("Max concurrent downloads set to {}", maxDownloads);
    }

    // ── Load default model ───────────────────────────────────────
    if(!defaultModel.empty())
    {
        spdlog::info("Loading default model: {} (variant: {})", defaultModel, defaultVariant.empty()?"auto":defaultVariant);
        arbiterAI::ErrorCode loadErr=ai.loadModel(defaultModel, defaultVariant);

        if(loadErr==arbiterAI::ErrorCode::Success)
        {
            spdlog::info("Default model '{}' loaded successfully", defaultModel);
        }
        else if(loadErr==arbiterAI::ErrorCode::ModelDownloading)
        {
            spdlog::info("Default model '{}' is downloading...", defaultModel);
        }
        else
        {
            spdlog::warn("Failed to load default model '{}' (error={})", defaultModel, static_cast<int>(loadErr));
        }
    }

    // ── HTTP server ──────────────────────────────────────────────
    httplib::Server server;

    arbiterAI::server::registerRoutes(server);

    if(!overridePath.empty())
    {
        arbiterAI::server::setOverridePath(overridePath);
        spdlog::info("Runtime model config overrides will be saved to: {}", overridePath);
    }

    spdlog::info("Server endpoints:");
    spdlog::info("  GET  /health                - Health check");
    spdlog::info("  POST /v1/chat/completions   - Chat completions (OpenAI-compatible)");
    spdlog::info("  GET  /v1/models             - List models (OpenAI-compatible)");
    spdlog::info("  GET  /v1/models/:id         - Get model info (OpenAI-compatible)");
    spdlog::info("  POST /v1/embeddings         - Embeddings (OpenAI-compatible)");
    spdlog::info("  GET  /api/models            - Available models + hardware fit");
    spdlog::info("  GET  /api/models/loaded      - Currently loaded models");
    spdlog::info("  POST /api/models/:name/load  - Load a model");
    spdlog::info("  POST /api/models/:name/unload - Unload a model");
    spdlog::info("  POST /api/models/:name/pin     - Pin a model");
    spdlog::info("  POST /api/models/:name/unpin   - Unpin a model");
    spdlog::info("  POST /api/models/config        - Add model config(s)");
    spdlog::info("  PUT  /api/models/config        - Add/update model config(s)");
    spdlog::info("  GET  /api/models/config/:name  - Get model config");
    spdlog::info("  DEL  /api/models/config/:name  - Remove model config");
    spdlog::info("  GET  /api/stats             - System snapshot");
    spdlog::info("  GET  /api/stats/history      - Inference history");
    spdlog::info("  GET  /api/stats/swaps        - Swap history");
    spdlog::info("  GET  /api/hardware           - Hardware info");
    spdlog::info("  POST /api/hardware/vram-override  - Set VRAM override");
    spdlog::info("  DEL  /api/hardware/vram-override/:idx - Clear VRAM override");
    spdlog::info("  GET  /api/storage            - Storage overview");
    spdlog::info("  GET  /api/storage/models     - Downloaded models");
    spdlog::info("  GET  /api/storage/models/:n  - Model storage stats");
    spdlog::info("  POST /api/storage/limit       - Set storage limit");
    spdlog::info("  DEL  /api/models/:n/files     - Delete model files");
    spdlog::info("  POST /api/models/:n/variants/:v/hot-ready    - Set hot ready");
    spdlog::info("  DEL  /api/models/:n/variants/:v/hot-ready    - Clear hot ready");
    spdlog::info("  POST /api/models/:n/variants/:v/protected    - Set protected");
    spdlog::info("  DEL  /api/models/:n/variants/:v/protected    - Clear protected");
    spdlog::info("  GET  /api/storage/cleanup/preview - Preview cleanup");
    spdlog::info("  POST /api/storage/cleanup/run     - Run cleanup");
    spdlog::info("  GET  /api/downloads          - Active downloads");
    spdlog::info("  GET  /dashboard              - Live dashboard");

    spdlog::info("Starting server on {}:{}", host, port);
    spdlog::info("Dashboard: http://{}:{}/dashboard", host=="0.0.0.0"?"localhost":host, port);

    if(!server.listen(host, port))
    {
        spdlog::error("Failed to start server on {}:{}", host, port);
        return 1;
    }

    return 0;
}
