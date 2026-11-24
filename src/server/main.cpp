#include "routes.h"
#include "updateRoutes.h"
#include "logBuffer.h"
#include "drainController.h"
#include "processEnd.h"
#include "installer.h"
#include "updateApplier.h"

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/version.h"
#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/storageManager.h"
#include "arbiterAI/inferenceScheduler.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>

#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>
#include <map>
#include <csignal>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <atomic>
#include <pthread.h>

namespace
{

struct StartupDefaultSelection {
    std::string model;
    std::string variant;
    int contextSize=0;
    arbiterAI::RuntimeOptions runtimeOptions;
};

struct StartupModelEntry {
    std::string model;
    std::string variant;
    int contextSize=0;
    arbiterAI::RuntimeOptions runtimeOptions;
    std::vector<int> devices;
};

arbiterAI::RuntimeOptions parseStartupRuntimeOptions(const nlohmann::json &j)
{
    arbiterAI::RuntimeOptions opts;
    if(!j.is_object()) return opts;
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
    if(j.contains("vulkan_no_host_visible_vram")&&j["vulkan_no_host_visible_vram"].is_boolean())
        opts.vulkanNoHostVisibleVram=j["vulkan_no_host_visible_vram"].get<bool>();
    return opts;
}

int sanitizeContextSize(int contextSize)
{
    return contextSize>0?contextSize:0;
}

std::string toLowerCopy(const std::string &value)
{
    std::string lower=value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });
    return lower;
}

std::string normalizeAcceleratorKey(const std::string &value)
{
    std::string lower=toLowerCopy(value);

    if(lower=="cpu"||lower=="cuda"||lower=="vulkan")
    {
        return lower;
    }

    return "";
}

std::map<std::string, StartupDefaultSelection> parseStartupDefaults(const nlohmann::json &cfg)
{
    std::map<std::string, StartupDefaultSelection> startupDefaults={
        {"cpu", StartupDefaultSelection{}},
        {"cuda", StartupDefaultSelection{}},
        {"vulkan", StartupDefaultSelection{}}
    };

    nlohmann::json defaultsJson=cfg.value("startup_defaults", nlohmann::json::object());
    if(!defaultsJson.is_object())
    {
        return startupDefaults;
    }

    for(auto it=defaultsJson.begin(); it!=defaultsJson.end(); ++it)
    {
        std::string key=normalizeAcceleratorKey(it.key());
        if(key.empty())
        {
            continue;
        }

        if(it.value().is_string())
        {
            startupDefaults[key].model=it.value().get<std::string>();
            startupDefaults[key].variant.clear();
            startupDefaults[key].contextSize=0;
            continue;
        }

        if(!it.value().is_object())
        {
            continue;
        }

        startupDefaults[key].model=it.value().value("model", "");
        startupDefaults[key].variant=it.value().value("variant", "");
        startupDefaults[key].contextSize=sanitizeContextSize(it.value().value("context_size", 0));
        if(it.value().contains("runtime_options"))
        {
            startupDefaults[key].runtimeOptions=parseStartupRuntimeOptions(it.value()["runtime_options"]);
        }
    }

    return startupDefaults;
}

std::vector<StartupModelEntry> parseStartupModels(const nlohmann::json &cfg)
{
    std::vector<StartupModelEntry> entries;

    if(!cfg.contains("startup_models")||!cfg["startup_models"].is_array())
    {
        return entries;
    }

    for(const nlohmann::json &item:cfg["startup_models"])
    {
        if(!item.is_object()||!item.contains("model"))
            continue;

        StartupModelEntry entry;
        entry.model=item.value("model", "");
        entry.variant=item.value("variant", "");
        entry.contextSize=sanitizeContextSize(item.value("context_size", 0));

        if(item.contains("runtime_options"))
        {
            entry.runtimeOptions=parseStartupRuntimeOptions(item["runtime_options"]);
        }

        if(item.contains("devices")&&item["devices"].is_array())
        {
            for(const nlohmann::json &d:item["devices"])
            {
                if(d.is_number_integer())
                {
                    entry.devices.push_back(d.get<int>());
                }
            }
        }

        if(!entry.model.empty())
        {
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

bool hasAccelerator(const arbiterAI::SystemInfo &hw, const std::string &accelerator)
{
    if(accelerator=="cpu")
    {
        return true;
    }

    for(const arbiterAI::GpuInfo &gpu:hw.gpus)
    {
        if(accelerator=="cuda"&&gpu.backend==arbiterAI::GpuBackend::CUDA)
        {
            return true;
        }
        if(accelerator=="vulkan"&&gpu.backend==arbiterAI::GpuBackend::Vulkan)
        {
            return true;
        }
    }

    return false;
}

std::vector<std::string> buildStartupAcceleratorOrder(
    const arbiterAI::SystemInfo &hw,
    const std::vector<std::string> &defaultBackendPriority)
{
    std::vector<std::string> order;

    auto appendIfAvailable=[&order, &hw](const std::string &accelerator)
    {
        if(accelerator.empty()||!hasAccelerator(hw, accelerator))
        {
            return;
        }
        if(std::find(order.begin(), order.end(), accelerator)==order.end())
        {
            order.push_back(accelerator);
        }
    };

    for(const std::string &backend:defaultBackendPriority)
    {
        appendIfAvailable(normalizeAcceleratorKey(backend));
    }

    appendIfAvailable("cuda");
    appendIfAvailable("vulkan");
    appendIfAvailable("cpu");

    return order;
}

StartupDefaultSelection selectStartupDefault(
    const arbiterAI::SystemInfo &hw,
    const std::map<std::string, StartupDefaultSelection> &startupDefaults,
    const std::vector<std::string> &defaultBackendPriority,
    const std::string &legacyDefaultModel,
    const std::string &legacyDefaultVariant,
    std::string &selectedAccelerator)
{
    for(const std::string &accelerator:buildStartupAcceleratorOrder(hw, defaultBackendPriority))
    {
        auto it=startupDefaults.find(accelerator);
        if(it!=startupDefaults.end()&&!it->second.model.empty())
        {
            selectedAccelerator=accelerator;
            return it->second;
        }
    }

    selectedAccelerator.clear();
    return {legacyDefaultModel, legacyDefaultVariant, 0};
}

void scheduleStartupLoadAfterDownload(
    const StartupDefaultSelection &selection,
    const std::string &accelerator)
{
    std::thread([selection, accelerator]()
    {
        std::string variant=selection.variant;

        for(int attempt=0; attempt<300; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            std::optional<arbiterAI::LoadedModel> state=
                arbiterAI::ModelRuntime::instance().getModelState(selection.model);
            if(!state.has_value())
            {
                spdlog::warn("Startup default model '{}' disappeared while waiting for download to finish", selection.model);
                return;
            }

            if(!state->variant.empty())
            {
                variant=state->variant;
            }

            if(state->state==arbiterAI::ModelState::Downloading)
            {
                continue;
            }

            if(state->state==arbiterAI::ModelState::Loaded||state->state==arbiterAI::ModelState::Ready)
            {
                spdlog::info("Startup default model '{}' is ready after background download for {}",
                    selection.model,
                    accelerator.empty()?"legacy startup":accelerator);
                return;
            }

            if(state->state!=arbiterAI::ModelState::Unloaded)
            {
                spdlog::warn("Startup default model '{}' ended in unexpected state {} after download",
                    selection.model,
                    static_cast<int>(state->state));
                return;
            }

            spdlog::info("Startup default model '{}' finished downloading; loading now for {}",
                selection.model,
                accelerator.empty()?"legacy startup":accelerator);

            arbiterAI::RuntimeOptions opts=selection.runtimeOptions;
            arbiterAI::ErrorCode loadErr=arbiterAI::ArbiterAI::instance().loadModel(
                selection.model,
                variant,
                selection.contextSize,
                &opts);

            if(loadErr==arbiterAI::ErrorCode::Success)
            {
                spdlog::info("Startup default model '{}' loaded successfully after download", selection.model);
                return;
            }

            if(loadErr==arbiterAI::ErrorCode::ModelDownloading)
            {
                continue;
            }

            spdlog::warn("Failed to load startup default model '{}' after download (error={})",
                selection.model,
                static_cast<int>(loadErr));
            return;
        }

        spdlog::warn("Timed out waiting for startup default model '{}' to finish downloading", selection.model);
    }).detach();
}

void scheduleStartupModelLoadAfterDownload(const StartupModelEntry &entry)
{
    std::thread([entry]()
    {
        std::string variant=entry.variant;

        for(int attempt=0; attempt<300; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            std::optional<arbiterAI::LoadedModel> state=
                arbiterAI::ModelRuntime::instance().getModelState(entry.model);
            if(!state.has_value())
            {
                spdlog::warn("Startup model '{}' disappeared while waiting for download", entry.model);
                return;
            }

            if(!state->variant.empty())
                variant=state->variant;

            if(state->state==arbiterAI::ModelState::Downloading)
                continue;

            if(state->state==arbiterAI::ModelState::Loaded||state->state==arbiterAI::ModelState::Ready)
            {
                spdlog::info("Startup model '{}' is ready after background download", entry.model);
                return;
            }

            if(state->state!=arbiterAI::ModelState::Unloaded)
            {
                spdlog::warn("Startup model '{}' ended in unexpected state {}", entry.model, static_cast<int>(state->state));
                return;
            }

            spdlog::info("Startup model '{}' finished downloading; loading now", entry.model);

            arbiterAI::RuntimeOptions opts=entry.runtimeOptions;
            arbiterAI::ErrorCode loadErr=arbiterAI::ArbiterAI::instance().loadModel(
                entry.model, variant, entry.contextSize, &opts, entry.devices);

            if(loadErr==arbiterAI::ErrorCode::Success)
            {
                spdlog::info("Startup model '{}' loaded successfully after download", entry.model);
                return;
            }
            if(loadErr==arbiterAI::ErrorCode::ModelDownloading)
                continue;

            spdlog::warn("Failed to load startup model '{}' after download (error={})", entry.model, static_cast<int>(loadErr));
            return;
        }

        spdlog::warn("Timed out waiting for startup model '{}' to finish downloading", entry.model);
    }).detach();
}

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

/// SIGTERM/SIGINT are blocked in every thread and taken by sigwait() on a
/// thread of their own, so the drain runs as ordinary code, not in a handler.
class StopSignals {
public:
    StopSignals()
    {
        sigemptyset(&m_signals);
        sigaddset(&m_signals, SIGTERM);
        sigaddset(&m_signals, SIGINT);
    }

    /// Must run before any other thread starts, so they all inherit the mask.
    /// Until start() is called, a signal exits at once: nothing needs draining
    /// yet, and a hung clone or model load can still be interrupted.
    void block()
    {
        pthread_sigmask(SIG_BLOCK, &m_signals, nullptr);

        std::thread([this]()
        {
            while(true)
            {
                int sig=0;
                if(sigwait(&m_signals, &sig)!=0)
                    continue;

                std::unique_lock<std::mutex> lock(m_mutex);
                if(!m_started)
                {
                    std::function<void()> beforeEarlyExit=m_beforeEarlyExit;
                    lock.unlock();
                    spdlog::warn("Received {} before the server was listening, exiting now", strsignal(sig));
                    if(beforeEarlyExit)
                        beforeEarlyExit();
                    spdlog::default_logger()->flush();
                    std::_Exit(128+sig);
                }
                if(m_stopRequested||m_shuttingDown)
                {
                    lock.unlock();
                    spdlog::warn("Received {} again, exiting now", strsignal(sig));
                    spdlog::default_logger()->flush();
                    std::_Exit(128+sig);
                }
                m_stopRequested=true;
                lock.unlock();
                spdlog::info("Received {}, shutting down (send it again to exit at once)", strsignal(sig));
                m_cv.notify_all();
            }
        }).detach();
    }

    /// Runs on the signal thread before a signal that came before start() exits.
    void setBeforeEarlyExit(std::function<void()> hook)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_beforeEarlyExit=std::move(hook);
    }

    /// From here on the first signal runs onStop on its own thread; any
    /// signal after it, or after shuttingDown(), exits the process at once.
    void start(std::function<void()> onStop)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_onStop=std::move(onStop);
        m_started=true;
        m_stopThread=std::thread([this]()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_stopRequested||m_shuttingDown; });
            if(!m_stopRequested)
                return;
            lock.unlock();
            m_onStop();
        });
    }

    /// A stop signal has come in since start().
    bool stopRequested()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stopRequested;
    }

    /// For onStop: wait until shuttingDown() has been called.
    bool waitForShutdown(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [this]() { return m_shuttingDown; });
    }

    /// The server has stopped listening: no drain is started from here on.
    void shuttingDown()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_shuttingDown=true;
        }
        m_cv.notify_all();
        if(m_stopThread.joinable())
            m_stopThread.join();
    }

private:
    sigset_t m_signals;
    std::function<void()> m_beforeEarlyExit;
    std::function<void()> m_onStop;
    std::thread m_stopThread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_started=false;
    bool m_stopRequested=false;
    bool m_shuttingDown=false;
};

/// Exits without static destruction: ~ModelRuntime would join downloads that
/// are still running, and detached startup pollers could call into singletons
/// being torn down. Saves storage state and the logs first.
[[noreturn]] void exitServer(int code)
{
    arbiterAI::StorageManager::instance().shutdown();
    spdlog::default_logger()->flush();
    std::_Exit(code);
}

/// Replaces the process with <root>/bin/arbiterAI-server, same argv and cwd,
/// after saving storage state; exits if execv fails.
[[noreturn]] void restartServer(const std::filesystem::path &root, const std::vector<std::string> &args)
{
    arbiterAI::StorageManager::instance().shutdown();
    std::filesystem::path binary=arbiterAI::server::installedBinaryPath(root);
    spdlog::info("Restarting as {}", binary.string());

    arbiterAI::server::ExecRestarter restarter;
    std::string error;
    restarter.restart(binary, args, error);
    spdlog::error("Restart failed: {}", error);
    exitServer(1);
}

/// Loads the models update-state.json says were loaded before the update,
/// unless the startup config already loaded them, and pins the pinned ones.
void reloadModelsAfterUpdate(const nlohmann::json &models)
{
    if(!models.is_array())
        return;

    arbiterAI::ArbiterAI &ai=arbiterAI::ArbiterAI::instance();
    for(const nlohmann::json &entry:models)
    {
        if(!entry.is_object()||!entry.value("model", nlohmann::json()).is_string())
            continue;

        std::string model=entry["model"].get<std::string>();
        bool loaded=false;
        for(const arbiterAI::LoadedModel &state:ai.getLoadedModels())
        {
            if(state.modelName==model&&(state.state==arbiterAI::ModelState::Loaded||state.state==arbiterAI::ModelState::Ready))
                loaded=true;
        }

        std::string variant=entry.value("variant", nlohmann::json()).is_string()?entry["variant"].get<std::string>():std::string();
        int contextSize=entry.value("context", nlohmann::json()).is_number_integer()?sanitizeContextSize(entry["context"].get<int>()):0;
        arbiterAI::RuntimeOptions options=parseStartupRuntimeOptions(entry.value("runtime_options", nlohmann::json::object()));
        std::vector<int> devices;
        if(entry.value("devices", nlohmann::json()).is_array())
        {
            for(const nlohmann::json &device:entry["devices"])
            {
                if(device.is_number_integer())
                    devices.push_back(device.get<int>());
            }
        }

        if(!loaded)
        {
            spdlog::info("Reloading '{}' (loaded before the update)", model);
            arbiterAI::ErrorCode loadErr=ai.loadModel(model, variant, contextSize, &options, devices);
            if(loadErr!=arbiterAI::ErrorCode::Success)
            {
                spdlog::warn("Could not reload '{}' after the update (error={})", model, static_cast<int>(loadErr));
                continue;
            }
        }

        bool pinned=entry.value("pinned", nlohmann::json()).is_boolean()&&entry["pinned"].get<bool>();
        if(pinned&&ai.pinModel(model)!=arbiterAI::ErrorCode::Success)
            spdlog::warn("Could not pin '{}' after the update", model);
    }
}

void printUsage()
{
    std::cout<<"Usage: arbiterAI-server [options]\n"
        "\n"
        "Options:\n"
        "  -c, --config <path>   Path to server configuration JSON file (required)\n"
        "      --self-test       Check that the server would start with --config, then exit:\n"
        "                        parse the config, load model configs, detect hardware.\n"
        "                        Binds no port, loads no models and writes nothing; uses the\n"
        "                        arbiterAI_config checkout in the working directory as it is.\n"
        "                        Exits 0 if the start would succeed, non-zero with the reason if not.\n"
        "      --seed <root>     Bring the install at <root> up to this binary's own install\n"
        "                        (the Docker image's entrypoint does this for the install volume):\n"
        "                        copy it into an empty <root>, or swap it in when it is newer\n"
        "                        and has not failed there before; then exit. Needs no --config.\n"
        "      --version         Print the version and exit\n"
        "  -h, --help            Print this help message\n"
        "\n"
        "See examples/server_config.json for the configuration file format.\n";
}

} // anonymous namespace

int main(int argc, char *argv[])
{
    // Static: the signal thread is detached and may outlive main's locals.
    static StopSignals stopSignals;
    stopSignals.block();

    // ── Parse CLI ────────────────────────────────────────────────
    std::string configPath;
    std::string seedRoot;
    bool selfTest=false;

    for(int i=1; i<argc; ++i)
    {
        std::string arg=argv[i];

        if(arg=="-h"||arg=="--help")
        {
            printUsage();
            return 0;
        }
        if(arg=="--version")
        {
            std::cout<<"arbiterAI-server "<<ARBITERAI_VERSION_STRING<<"\n"
                "llama.cpp build "<<ARBITERAI_LLAMACPP_BUILD<<"\n";
            return 0;
        }
        if(arg=="--self-test")
        {
            selfTest=true;
        }
        else if(arg=="--seed"&&i+1<argc)
        {
            seedRoot=argv[++i];
        }
        else if((arg=="-c"||arg=="--config")&&i+1<argc)
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

    if(!seedRoot.empty())
    {
        std::filesystem::path imageRoot=arbiterAI::server::runningInstallRoot();
        arbiterAI::server::SeedResult seed=arbiterAI::server::seedInstall(imageRoot, seedRoot);
        if(seed.outcome==arbiterAI::server::SeedOutcome::Failed)
        {
            std::cerr<<"Seed failed: "<<seed.message<<"\n";
            return 1;
        }
        if(seed.outcome==arbiterAI::server::SeedOutcome::KeptOnError)
            std::cerr<<"Seed: "<<seed.message<<"\n";
        else
            std::cout<<"Seed: "<<seed.message<<"\n";
        return 0;
    }

    if(configPath.empty())
    {
        std::cerr<<"Error: --config <path> is required.\n\n";
        printUsage();
        return 1;
    }

    // ── Update probation ─────────────────────────────────────────
    // First, so a crash anywhere in the start below can still be rolled back.
    // Resolved now: an update moves the running binary to bin.prev.
    std::vector<std::string> args(argv, argv+argc);
    std::filesystem::path installRoot;
    nlohmann::json modelsToReload=nlohmann::json::array();
    if(!selfTest)
        installRoot=arbiterAI::server::runningInstallRoot();
    std::error_code markerError;
    if(!installRoot.empty()&&std::filesystem::exists(arbiterAI::server::installMarkerPath(installRoot), markerError))
    {
        arbiterAI::server::ExecRestarter restarter;
        arbiterAI::server::ProbationResult probation=arbiterAI::server::runStartupProbation(
            installRoot, ARBITERAI_VERSION_STRING, restarter, args);
        if(probation.outcome==arbiterAI::server::ProbationOutcome::Started)
            modelsToReload=probation.state.loadedModels;

        switch(probation.outcome)
        {
        case arbiterAI::server::ProbationOutcome::None:
            break;
        case arbiterAI::server::ProbationOutcome::Failed:
            spdlog::error("Update probation: {}", probation.message);
            break;
        default:
            spdlog::info("Update probation: {}", probation.message);
            break;
        }

        // A stop before the server is up is not a crash: keep the new version.
        stopSignals.setBeforeEarlyExit([installRoot]()
        {
            arbiterAI::server::markHealthy(installRoot, ARBITERAI_VERSION_STRING);
        });
    }
    else
    {
        installRoot.clear();
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
    std::map<std::string, StartupDefaultSelection> startupDefaults=parseStartupDefaults(cfg);
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

    // Updates
    std::string updatesError;
    arbiterAI::server::UpdatePolicy updatePolicy=arbiterAI::server::configUpdatePolicy(cfg, updatesError);
    int drainTimeoutSec=60;
    if(cfg.contains("updates")&&cfg["updates"].is_object())
    {
        const nlohmann::json &drainTimeout=cfg["updates"].value("drain_timeout_s", nlohmann::json());
        if(drainTimeout.is_number_integer())
            drainTimeoutSec=drainTimeout.get<int>();
    }

    // ── Configure logging ────────────────────────────────────────
    if(logLevel=="trace")       spdlog::set_level(spdlog::level::trace);
    else if(logLevel=="debug")  spdlog::set_level(spdlog::level::debug);
    else if(logLevel=="info")   spdlog::set_level(spdlog::level::info);
    else if(logLevel=="warn")   spdlog::set_level(spdlog::level::warn);
    else if(logLevel=="error")  spdlog::set_level(spdlog::level::err);
    else                        spdlog::set_level(spdlog::level::info);

    std::vector<spdlog::sink_ptr> sinks;

    if(selfTest)
    {
        // Report on stderr only; the live server owns the log files.
        sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
    }
    else
    {
        auto bufferSink=std::make_shared<arbiterAI::server::LogBufferSink>(1000);
        arbiterAI::server::logBufferSinkInstance()=bufferSink;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        sinks.push_back(bufferSink);
    }

    if(!logDir.empty()&&!selfTest)
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

    // Install crash signal handlers for better diagnostics
    auto crashHandler=[](int sig)
    {
        spdlog::critical("FATAL SIGNAL {} ({}) received — aborting",
            sig, (sig==SIGABRT?"SIGABRT":sig==SIGSEGV?"SIGSEGV":"OTHER"));
        spdlog::default_logger()->flush();
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    };
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGSEGV, crashHandler);

    spdlog::info("Loaded config from: {}", configPath);

    if(!logDir.empty()&&!selfTest)
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

    // The self-test may run next to a live server in its working directory:
    // read that server's arbiterAI_config checkout, never fetch into it.
    if(selfTest)
        arbiterAI::ModelManager::instance().setConfigSync(false);

    arbiterAI::ArbiterAI &ai=arbiterAI::ArbiterAI::instance();
    arbiterAI::ErrorCode err=ai.initialize(fsPaths);

    if(err!=arbiterAI::ErrorCode::Success)
    {
        spdlog::error("Failed to initialize ArbiterAI (error={}): no model config could be loaded from model_config_paths or {}",
            static_cast<int>(err), (std::filesystem::current_path()/"arbiterAI_config").string());
        return 1;
    }

    spdlog::info("ArbiterAI initialized successfully");

    // ── Restore injected model configs ───────────────────────────
    // setInjectedConfigDir() creates a missing directory.
    if(!injectedConfigDir.empty()&&(!selfTest||std::filesystem::exists(injectedConfigDir)))
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
    // initialize() creates models_dir and starts the cleanup timer.
    if(!selfTest)
        storage.initialize(modelsDir);

    // ── Models directory for ModelRuntime ─────────────────────────
    arbiterAI::ModelRuntime::instance().setModelsDir(modelsDir);

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

    // ── Load startup models ─────────────────────────────────────
    arbiterAI::HardwareDetector::instance().refresh();
    arbiterAI::SystemInfo startupHardware=arbiterAI::HardwareDetector::instance().getSystemInfo();

    // Parsed before the self-test returns: a malformed entry fails both.
    std::vector<StartupModelEntry> startupModels=parseStartupModels(cfg);

    if(selfTest)
    {
        std::cout<<"self-test passed: "<<arbiterAI::ModelManager::instance().getModelsByRanking().size()
            <<" model configs, "<<startupHardware.gpus.size()<<" GPU(s)\n";
        return 0;
    }

    if(!startupModels.empty())
    {
        for(const StartupModelEntry &entry:startupModels)
        {
            std::string devicesStr;
            if(!entry.devices.empty())
            {
                for(size_t i=0; i<entry.devices.size(); ++i)
                {
                    if(i>0) devicesStr+=", ";
                    devicesStr+=std::to_string(entry.devices[i]);
                }
            }
            else
            {
                devicesStr="auto";
            }

            spdlog::info("Loading startup model: {} (variant: {}, devices: [{}])",
                entry.model, entry.variant.empty()?"auto":entry.variant, devicesStr);

            arbiterAI::RuntimeOptions startupOpts=entry.runtimeOptions;
            arbiterAI::ErrorCode loadErr=ai.loadModel(
                entry.model, entry.variant, entry.contextSize, &startupOpts, entry.devices);

            if(loadErr==arbiterAI::ErrorCode::Success)
            {
                spdlog::info("Startup model '{}' loaded successfully", entry.model);
            }
            else if(loadErr==arbiterAI::ErrorCode::ModelDownloading)
            {
                spdlog::info("Startup model '{}' is downloading...", entry.model);
                scheduleStartupModelLoadAfterDownload(entry);
            }
            else
            {
                spdlog::warn("Failed to load startup model '{}' (error={})", entry.model, static_cast<int>(loadErr));
            }
        }
    }
    else
    {
        // Legacy format: startup_defaults keyed by accelerator
        std::string selectedAccelerator;
        StartupDefaultSelection startupSelection=selectStartupDefault(
            startupHardware,
            startupDefaults,
            defaultBackendPriority,
            defaultModel,
            defaultVariant,
            selectedAccelerator);

        if(!startupSelection.model.empty())
        {
            if(selectedAccelerator.empty())
            {
                spdlog::info("Loading legacy default model: {} (variant: {})", startupSelection.model, startupSelection.variant.empty()?"auto":startupSelection.variant);
            }
            else
            {
                spdlog::info("Loading startup default model for {}: {} (variant: {})", selectedAccelerator, startupSelection.model, startupSelection.variant.empty()?"auto":startupSelection.variant);
            }

            arbiterAI::RuntimeOptions startupOpts=startupSelection.runtimeOptions;
            arbiterAI::ErrorCode loadErr=ai.loadModel(
                startupSelection.model,
                startupSelection.variant,
                startupSelection.contextSize,
                &startupOpts);

            if(loadErr==arbiterAI::ErrorCode::Success)
            {
                spdlog::info("Startup default model '{}' loaded successfully", startupSelection.model);
            }
            else if(loadErr==arbiterAI::ErrorCode::ModelDownloading)
            {
                spdlog::info("Startup default model '{}' is downloading...", startupSelection.model);
                scheduleStartupLoadAfterDownload(startupSelection, selectedAccelerator);
            }
            else
            {
                spdlog::warn("Failed to load startup default model '{}' (error={})", startupSelection.model, static_cast<int>(loadErr));
            }
        }
    }

    // ── Models loaded before an update ──────────────────────────
    reloadModelsAfterUpdate(modelsToReload);

    // ── Inference Scheduler ─────────────────────────────────────────
    {
        std::vector<int> gpuIndices;
        auto gpus=arbiterAI::HardwareDetector::instance().getGpus();
        for(const auto &gpu:gpus)
        {
            gpuIndices.push_back(gpu.index);
        }
        arbiterAI::InferenceScheduler::instance().initialize(gpuIndices);
    }

    // ── HTTP server ──────────────────────────────────────────────
    httplib::Server server;
    arbiterAI::server::DrainController drainController;

    drainController.addBusyProbe("scheduler_jobs", []()
    {
        return arbiterAI::InferenceScheduler::instance().getActiveJobs().size();
    });
    drainController.addBusyProbe("downloads", []()
    {
        return arbiterAI::ModelRuntime::instance().getActiveDownloadSnapshots().size();
    });
    arbiterAI::server::setDrainController(&drainController);

    arbiterAI::server::registerRoutes(server);
    arbiterAI::server::setServerConfigPath(configPath);

    if(!overridePath.empty())
    {
        arbiterAI::server::setOverridePath(overridePath);
        spdlog::info("Runtime model config overrides will be saved to: {}", overridePath);
    }

    // ── Release check ───────────────────────────────────────────
    if(!updatesError.empty())
    {
        spdlog::warn("Ignoring the 'updates' config block, apply is off: {}", updatesError);
    }
    arbiterAI::server::UpdateManager updates(
        std::make_unique<arbiterAI::server::GitHubReleaseSource>(ARBITERAI_UPDATE_REPO),
        arbiterAI::getVersion().toString());
    arbiterAI::server::setUpdateManager(&updates);
    updates.start(updatePolicy);

    arbiterAI::server::ApplyContext applyContext;
    applyContext.root=arbiterAI::server::runningInstallRoot();
    std::error_code cwdError;
    applyContext.cwd=std::filesystem::current_path(cwdError);
    applyContext.allowApply=updatePolicy.allowApply;
    applyContext.updatesError=updatesError;
    applyContext.configPaths.push_back({"--config", configPath});
    for(const std::string &path:modelConfigPaths)
        applyContext.configPaths.push_back({"model_config_paths", path});
    applyContext.configPaths.push_back({"models_dir", modelsDir});
    applyContext.configPaths.push_back({"override_path", overridePath});
    applyContext.configPaths.push_back({"injected_config_dir", injectedConfigDir});
    applyContext.configPaths.push_back({"logging.directory", logDir});
    std::shared_ptr<arbiterAI::server::SystemEnvironment> environment=std::make_shared<arbiterAI::server::SystemEnvironment>();
    arbiterAI::server::setApplyContext(applyContext, environment);

    // ── Apply and rollback ──────────────────────────────────────
    // Asked for on the applier's worker thread once the install is swapped:
    // drain, stop listening, and let main restart from the new bin/.
    std::atomic<bool> restartRequested{false};
    // The restart worker, the stop handler and main each end the process;
    // the first one does, the others leave it alone.
    arbiterAI::server::ProcessEnd processEnd;
    arbiterAI::server::ApplierConfig applierConfig;
    applierConfig.root=applyContext.root;
    applierConfig.currentVersion=ARBITERAI_VERSION_STRING;
    applierConfig.configPath=configPath;
    applierConfig.cwd=applyContext.cwd;
    applierConfig.drainTimeoutSeconds=drainTimeoutSec;
    arbiterAI::server::UpdateApplier applier(applierConfig,
        std::make_unique<arbiterAI::server::CprDownloader>(),
        arbiterAI::server::loadedModelsSnapshot,
        [&restartRequested, &processEnd, &drainController, &server, drainTimeoutSec, &args, applyRoot=applyContext.root]()
        {
            restartRequested=true;
            spdlog::info("Restarting for the update: refusing new requests, waiting up to {} s for in-flight work", drainTimeoutSec);
            if(drainController.drain(std::chrono::seconds(drainTimeoutSec)))
                spdlog::info("Drain complete");
            if(processEnd.claimListenerStop())
            {
                server.wait_until_ready();
                server.stop();
            }

            // listen() joins the request threads; one still running after the
            // drain timeout would hold main, so restart from here instead.
            if(stopSignals.waitForShutdown(std::chrono::seconds(5)))
                return;
            switch(processEnd.afterRestartTimeout(stopSignals.stopRequested()))
            {
            case arbiterAI::server::EndAction::Restart:
                spdlog::error("Requests still running after the drain timeout, restarting without unloading models");
                restartServer(applyRoot, args);
            case arbiterAI::server::EndAction::Exit:
                spdlog::error("Requests still running after the drain timeout and a stop came in, exiting without unloading models");
                exitServer(1);
            case arbiterAI::server::EndAction::None:
                break;
            }
        });
    arbiterAI::server::setUpdateApplier(&applier);

    // ── Auto-update ─────────────────────────────────────────────
    arbiterAI::server::AutoUpdater::Sources autoSources;
    autoSources.policy=[&updates]() { return updates.policy(); };
    autoSources.status=[&updates]() { return updates.status(); };
    autoSources.failed=[applyRoot=applyContext.root]() { return arbiterAI::server::readUpdateHistory(applyRoot).failed; };
    autoSources.check=arbiterAI::server::currentApplyCheck;
    autoSources.supervisor=arbiterAI::server::currentSupervisor;
    autoSources.busy=[&drainController]()
    {
        std::string busy;
        size_t requests=drainController.inFlightRequests();
        if(requests>0)
            busy=std::to_string(requests)+" HTTP request(s)";
        for(const arbiterAI::server::DrainController::BusyCount &work:drainController.busyWork())
        {
            if(work.count==0)
                continue;
            busy+=(busy.empty()?"":", ")+std::to_string(work.count)+" "+work.name;
        }
        return busy;
    };
    arbiterAI::server::AutoUpdater autoUpdater(autoSources, applier, ARBITERAI_VERSION_STRING);
    arbiterAI::server::setAutoUpdater(&autoUpdater);
    autoUpdater.start(std::chrono::seconds(15));
    arbiterAI::server::ApplyCheck applyCheck=arbiterAI::server::checkCanApply(applyContext, *environment);
    nlohmann::json supervisor=arbiterAI::server::supervisorJson(arbiterAI::server::detectSupervisor(*environment));
    spdlog::info("Update apply: {}; restart on crash: {}",
        applyCheck.canApply?std::string("available"):"blocked, "+applyCheck.blockedReason,
        supervisor.is_null()?std::string("none (no supervisor declared)"):supervisor.get<std::string>());
    spdlog::info("Release check for {}: {}", ARBITERAI_UPDATE_REPO,
        updatePolicy.check?"every "+std::to_string(updatePolicy.intervalHours)+" h":std::string("off"));

    spdlog::info("Server endpoints:");
    spdlog::info("  GET  /health                - Health check");
    spdlog::info("  GET  /api/update            - Newer release check result");
    spdlog::info("  POST /api/update/check      - Check for a newer release now");
    spdlog::info("  POST /api/update/apply      - Install the latest release and restart");
    spdlog::info("  POST /api/update/rollback   - Go back to the previous install and restart");
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
    spdlog::info("  GET  /api/scheduler/jobs     - Active scheduler jobs");
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
    spdlog::info("  GET  /dashboard              - Live dashboard (Overview tab)");
    spdlog::info("  GET  /dashboard/storage      - Dashboard: downloaded models");
    spdlog::info("  GET  /dashboard/config       - Dashboard: startup configuration");
    spdlog::info("  GET  /dashboard/logs         - Dashboard with the server log open");
    spdlog::info("  GET  /dashboard/app.*        - The dashboard's WebAssembly app (js, wasm, data)");

    spdlog::info("Starting server on {}:{}", host, port);
    spdlog::info("Dashboard: http://{}:{}/dashboard", host=="0.0.0.0"?"localhost":host, port);

    // Serving this long without a crash ends an update's probation.
    arbiterAI::server::HealthTimer healthTimer;
    if(!installRoot.empty())
        healthTimer.start(installRoot, ARBITERAI_VERSION_STRING, std::chrono::seconds(60));

    stopSignals.start([&drainController, &server, &applier, &processEnd, drainTimeoutSec, installRoot]()
    {
        // An auto-update would take the drain for idle and swap while the
        // process exits; one already swapping is let finish first.
        applier.stop();

        // A clean stop is not a crash: a restart right after an update keeps it.
        if(!installRoot.empty()&&arbiterAI::server::markHealthy(installRoot, ARBITERAI_VERSION_STRING))
            spdlog::info("Update to v{} kept: stopped cleanly during probation", ARBITERAI_VERSION_STRING);

        spdlog::info("Draining: refusing new requests, waiting up to {} s for in-flight work", drainTimeoutSec);
        bool drained=drainController.drain(std::chrono::seconds(drainTimeoutSec));
        if(drained)
            spdlog::info("Drain complete");

        // start() runs just before listen(), which may not have bound yet.
        if(processEnd.claimListenerStop())
        {
            server.wait_until_ready();
            server.stop();
        }

        // listen() joins the request threads, so a request still running
        // after the drain timeout would hold it; do not wait for it.
        if(!drained&&!stopSignals.waitForShutdown(std::chrono::seconds(5))
            &&processEnd.afterStopTimeout()==arbiterAI::server::EndAction::Exit)
        {
            spdlog::error("Requests still running after the drain timeout, exiting without unloading models");
            exitServer(1);
        }
    });

    bool listenOk=server.listen(host, port);

    stopSignals.shuttingDown();
    applier.stop();
    // A stop signal during the drain wins: the supervisor (or the operator)
    // starts the new install from disk.
    arbiterAI::server::EndAction endAction=processEnd.afterListen(restartRequested, stopSignals.stopRequested());
    if(endAction==arbiterAI::server::EndAction::None)
    {
        // the restart worker or the stop handler gave up waiting for this and is ending the process
        for(;;)
            std::this_thread::sleep_for(std::chrono::hours(1));
    }
    healthTimer.stop();

    // Stop scheduler worker threads before static destruction tears down
    // the singletons (ModelRuntime, TelemetryCollector) they depend on.
    arbiterAI::InferenceScheduler::instance().shutdown();

    arbiterAI::server::setAutoUpdater(nullptr);
    autoUpdater.stop();
    arbiterAI::server::setUpdateApplier(nullptr);
    arbiterAI::server::setUpdateManager(nullptr);
    updates.stop();

    if(!listenOk)
    {
        spdlog::error("Failed to start server on {}:{}", host, port);
        exitServer(1);
    }

    for(const arbiterAI::LoadedModel &model:arbiterAI::ModelRuntime::instance().getModelStates())
    {
        if(model.state==arbiterAI::ModelState::Loaded||model.state==arbiterAI::ModelState::Ready)
        {
            spdlog::info("Unloading model '{}'", model.modelName);
            // A pinned model only drops to Ready on unload.
            ai.unpinModel(model.modelName);
            ai.unloadModel(model.modelName);
        }
    }

    if(!arbiterAI::ModelRuntime::instance().getActiveDownloadSnapshots().empty())
        spdlog::warn("Downloads still running after the drain timeout, abandoning them");

    if(endAction==arbiterAI::server::EndAction::Restart)
        restartServer(applyContext.root, args);

    spdlog::info("shutdown complete");
    exitServer(0);
}
