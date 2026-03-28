#include "routes.h"

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/storageManager.h"

#include <httplib.h>
#include <cxxopts.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

int main(int argc, char *argv[])
{
    cxxopts::Options options("arbiterAI-server", "ArbiterAI standalone server with OpenAI-compatible API");

    options.add_options()
        ("p,port", "HTTP port", cxxopts::value<int>()->default_value("8080"))
        ("H,host", "Bind address", cxxopts::value<std::string>()->default_value("0.0.0.0"))
        ("c,config", "Model config path(s)", cxxopts::value<std::vector<std::string>>()->default_value("config"))
        ("m,model", "Default model to load on startup", cxxopts::value<std::string>()->default_value(""))
        ("v,variant", "Default variant (e.g., Q4_K_M)", cxxopts::value<std::string>()->default_value(""))
        ("override-path", "Path to write runtime model config overrides (enables persistence)", cxxopts::value<std::string>()->default_value(""))
        ("ram-budget", "Ready model RAM budget in MB (0 = auto 50%)", cxxopts::value<int>()->default_value("0"))
        ("models-dir", "Path to directory for downloaded model files", cxxopts::value<std::string>()->default_value("/models"))
        ("storage-limit", "Maximum bytes for model storage (0 = all free, supports suffixes: 10G, 500M)", cxxopts::value<std::string>()->default_value("0"))
        ("cleanup-enabled", "Enable automated storage cleanup", cxxopts::value<bool>()->default_value("true"))
        ("cleanup-max-age", "Maximum days since last use before cleanup candidacy", cxxopts::value<int>()->default_value("30"))
        ("cleanup-interval", "Hours between automated cleanup runs", cxxopts::value<int>()->default_value("24"))
        ("log-level", "Log level (trace, debug, info, warn, error)", cxxopts::value<std::string>()->default_value("info"))
        ("h,help", "Print usage");

    cxxopts::ParseResult result;

    try
    {
        result=options.parse(argc, argv);
    }
    catch(const cxxopts::exceptions::exception &e)
    {
        std::cerr<<"Error parsing options: "<<e.what()<<std::endl;
        std::cout<<options.help()<<std::endl;
        return 1;
    }

    if(result.count("help"))
    {
        std::cout<<options.help()<<std::endl;
        return 0;
    }

    // Configure logging
    std::string logLevel=result["log-level"].as<std::string>();
    if(logLevel=="trace")       spdlog::set_level(spdlog::level::trace);
    else if(logLevel=="debug")  spdlog::set_level(spdlog::level::debug);
    else if(logLevel=="info")   spdlog::set_level(spdlog::level::info);
    else if(logLevel=="warn")   spdlog::set_level(spdlog::level::warn);
    else if(logLevel=="error")  spdlog::set_level(spdlog::level::err);
    else                        spdlog::set_level(spdlog::level::info);

    int port=result["port"].as<int>();
    std::string host=result["host"].as<std::string>();
    std::vector<std::string> configStrs=result["config"].as<std::vector<std::string>>();
    std::string defaultModel=result["model"].as<std::string>();
    std::string defaultVariant=result["variant"].as<std::string>();
    std::string overridePath=result["override-path"].as<std::string>();
    int ramBudget=result["ram-budget"].as<int>();
    std::string modelsDir=result["models-dir"].as<std::string>();
    std::string storageLimitStr=result["storage-limit"].as<std::string>();
    bool cleanupEnabled=result["cleanup-enabled"].as<bool>();
    int cleanupMaxAgeDays=result["cleanup-max-age"].as<int>();
    int cleanupIntervalHours=result["cleanup-interval"].as<int>();

    // Parse storage limit (supports suffixes: G, M, K)
    int64_t storageLimitBytes=0;
    if(!storageLimitStr.empty()&&storageLimitStr!="0")
    {
        char suffix=storageLimitStr.back();
        std::string numStr=storageLimitStr;

        if(suffix=='G'||suffix=='g')
        {
            numStr.pop_back();
            storageLimitBytes=static_cast<int64_t>(std::stod(numStr)*1073741824);
        }
        else if(suffix=='M'||suffix=='m')
        {
            numStr.pop_back();
            storageLimitBytes=static_cast<int64_t>(std::stod(numStr)*1048576);
        }
        else if(suffix=='K'||suffix=='k')
        {
            numStr.pop_back();
            storageLimitBytes=static_cast<int64_t>(std::stod(numStr)*1024);
        }
        else
        {
            storageLimitBytes=std::stoll(storageLimitStr);
        }
    }

    // Convert config paths
    std::vector<std::filesystem::path> configPaths;
    for(const std::string &s:configStrs)
    {
        configPaths.push_back(s);
    }

    spdlog::info("Initializing ArbiterAI...");

    // Initialize ArbiterAI
    arbiterAI::ArbiterAI &ai=arbiterAI::ArbiterAI::instance();
    arbiterAI::ErrorCode err=ai.initialize(configPaths);

    if(err!=arbiterAI::ErrorCode::Success)
    {
        spdlog::error("Failed to initialize ArbiterAI (error={})", static_cast<int>(err));
        return 1;
    }

    spdlog::info("ArbiterAI initialized successfully");

    // Configure StorageManager
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

    // Set RAM budget if specified
    if(ramBudget>0)
    {
        arbiterAI::ModelRuntime::instance().setReadyRamBudget(ramBudget);
        spdlog::info("Ready model RAM budget set to {} MB", ramBudget);
    }

    // Load default model if specified
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

    // Create HTTP server
    httplib::Server server;

    // Register routes
    arbiterAI::server::registerRoutes(server);

    // Set override path for runtime model config persistence
    if(!overridePath.empty())
    {
        arbiterAI::server::setOverridePath(overridePath);
        spdlog::info("Runtime model config overrides will be saved to: {}", overridePath);
    }

    // Log available endpoints
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
