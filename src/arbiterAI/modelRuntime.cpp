#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/telemetryCollector.h"
#include "arbiterAI/storageManager.h"

#include <llama.h>
#include <ggml.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <regex>

namespace arbiterAI
{

ModelRuntime &ModelRuntime::instance()
{
    static ModelRuntime runtime;
    return runtime;
}

void ModelRuntime::reset()
{
    ModelRuntime &rt=instance();

    // Signal all background download threads to stop waiting
    {
        std::lock_guard<std::mutex> lock(rt.m_mutex);
        rt.m_shuttingDown=true;
        rt.m_downloadCv.notify_all();
    }

    for(std::thread &t:rt.m_downloadThreads)
    {
        if(t.joinable())
        {
            t.join();
        }
    }

    std::lock_guard<std::mutex> lock(rt.m_mutex);

    rt.m_downloadThreads.clear();
    rt.m_activeDownloadCount=0;
    rt.m_maxConcurrentDownloads=2;
    rt.m_shuttingDown=false;

    // Free all llama.cpp resources
    for(auto &pair:rt.m_models)
    {
        rt.freeLlamaModel(pair.second);
    }

    rt.m_models.clear();
    rt.m_inferenceActive=false;
    rt.m_inferenceModel.clear();
    while(!rt.m_pendingSwaps.empty())
    {
        rt.m_pendingSwaps.pop();
    }

    // Reset RAM budget to default (50% of system RAM)
    SystemInfo hw=HardwareDetector::instance().getSystemInfo();
    rt.m_readyRamBudgetMb=hw.totalRamMb/2;
}

ModelRuntime::ModelRuntime()
{
    // Default ready RAM budget: 50% of total system RAM
    SystemInfo hw=HardwareDetector::instance().getSystemInfo();
    m_readyRamBudgetMb=hw.totalRamMb/2;
}

// ---- llama.cpp log capture ------------------------------------------------

// Thread-local pointer to the active ModelRuntime capturing logs.
// Only one load happens at a time (under m_mutex), so this is safe.
static ModelRuntime *s_capturingInstance=nullptr;

static void llamaLogCallback(enum ggml_log_level level, const char *text, void *userData)
{
    (void)userData;
    ModelRuntime *rt=s_capturingInstance;
    if(rt)
    {
        rt->appendLlamaLog(text);
    }

    // Also forward to spdlog so logs still appear in the server log
    if(text)
    {
        std::string msg(text);
        // Strip trailing newline for spdlog
        while(!msg.empty()&&(msg.back()=='\n'||msg.back()=='\r'))
        {
            msg.pop_back();
        }
        if(msg.empty()) return;

        switch(level)
        {
            case GGML_LOG_LEVEL_ERROR:
                spdlog::error("[llama] {}", msg);
                break;
            case GGML_LOG_LEVEL_WARN:
                spdlog::warn("[llama] {}", msg);
                break;
            case GGML_LOG_LEVEL_DEBUG:
                spdlog::debug("[llama] {}", msg);
                break;
            default:
                spdlog::info("[llama] {}", msg);
                break;
        }
    }
}

void ModelRuntime::appendLlamaLog(const char *text)
{
    if(m_capturingLlamaLog&&text)
    {
        m_llamaLogCapture<<text;
    }
}

void ModelRuntime::beginLlamaLogCapture()
{
    m_llamaLogCapture.str("");
    m_llamaLogCapture.clear();
    m_capturingLlamaLog=true;
    s_capturingInstance=this;
    llama_log_set(llamaLogCallback, nullptr);
}

void ModelRuntime::endLlamaLogCapture()
{
    m_capturingLlamaLog=false;
    s_capturingInstance=nullptr;
    llama_log_set(llamaLogCallback, nullptr); // keep forwarding to spdlog
}

const char *loadFailureReasonToString(LoadFailureReason reason)
{
    switch(reason)
    {
        case LoadFailureReason::FileNotFound:     return "file_not_found";
        case LoadFailureReason::FileCorrupt:       return "file_corrupt";
        case LoadFailureReason::InsufficientVram:  return "insufficient_vram";
        case LoadFailureReason::InsufficientRam:   return "insufficient_ram";
        case LoadFailureReason::ContextTooLarge:   return "context_too_large";
        case LoadFailureReason::UnsupportedArch:   return "unsupported_arch";
        case LoadFailureReason::BackendError:      return "backend_error";
        default:                                   return "unknown";
    }
}

LoadErrorDetail ModelRuntime::classifyLoadFailure(
    const std::string &llamaLog,
    const std::string &model,
    const std::string &filePath,
    int contextSize) const
{
    LoadErrorDetail detail;
    detail.llamaLog=llamaLog;

    std::string logLower=llamaLog;
    std::transform(logLower.begin(), logLower.end(), logLower.begin(), ::tolower);

    // Check for file-not-found / failed-to-open
    if(logLower.find("failed to open")!=std::string::npos||
        logLower.find("no such file")!=std::string::npos)
    {
        detail.reason=LoadFailureReason::FileNotFound;
        detail.summary="Model file not found: "+filePath;
        detail.suggestion="The GGUF file does not exist at the expected path. "
            "Re-download the model or verify the file path in the model configuration.";
        detail.action="redownload";
        detail.recoverable=true;
        return detail;
    }

    // Check for corrupt / truncated GGUF
    if(logLower.find("invalid magic")!=std::string::npos||
        logLower.find("gguf_init")!=std::string::npos&&logLower.find("fail")!=std::string::npos||
        logLower.find("unexpected end")!=std::string::npos||
        logLower.find("truncated")!=std::string::npos||
        logLower.find("invalid header")!=std::string::npos||
        logLower.find("bad magic")!=std::string::npos)
    {
        detail.reason=LoadFailureReason::FileCorrupt;
        detail.summary="Model file appears corrupt or truncated: "+filePath;
        detail.suggestion="Delete the file and re-download it. "
            "The download may have been interrupted or the file was partially written.";
        detail.action="delete_and_redownload";
        detail.recoverable=true;
        return detail;
    }

    // Check for VRAM / GPU memory allocation failures
    if(logLower.find("out of memory")!=std::string::npos||
        logLower.find("cuda error")!=std::string::npos||
        logLower.find("cudamalloc")!=std::string::npos||
        logLower.find("not enough vram")!=std::string::npos||
        logLower.find("vk_error_out_of_device_memory")!=std::string::npos||
        logLower.find("failed to allocate")!=std::string::npos&&logLower.find("buffer")!=std::string::npos||
        logLower.find("ggml_backend_cuda")!=std::string::npos&&logLower.find("error")!=std::string::npos||
        logLower.find("ggml_cuda_error")!=std::string::npos)
    {
        detail.reason=LoadFailureReason::InsufficientVram;
        detail.summary="Insufficient GPU memory to load model with context size "+std::to_string(contextSize);
        detail.suggestion="Try reducing the context size, use a smaller quantization variant, "
            "or unload other models to free VRAM.";
        detail.action="reduce_context";
        detail.recoverable=true;
        return detail;
    }

    // Check for RAM allocation failures
    if(logLower.find("bad_alloc")!=std::string::npos||
        logLower.find("cannot allocate memory")!=std::string::npos||
        logLower.find("enomem")!=std::string::npos)
    {
        detail.reason=LoadFailureReason::InsufficientRam;
        detail.summary="Insufficient system RAM to load model";
        detail.suggestion="Close other applications, unload other models, or use a smaller quantization variant.";
        detail.action="reduce_context";
        detail.recoverable=true;
        return detail;
    }

    // Check for context size issues
    if(logLower.find("n_ctx")!=std::string::npos&&
        (logLower.find("too large")!=std::string::npos||logLower.find("exceeds")!=std::string::npos))
    {
        detail.reason=LoadFailureReason::ContextTooLarge;
        detail.summary="Requested context size "+std::to_string(contextSize)+" exceeds model or hardware limits";
        detail.suggestion="Use a smaller context size. The model may support a maximum context window smaller "
            "than what was requested.";
        detail.action="reduce_context";
        detail.recoverable=true;
        return detail;
    }

    // Check for unsupported architecture
    if(logLower.find("unknown model architecture")!=std::string::npos||
        logLower.find("unsupported model")!=std::string::npos||
        logLower.find("unknown arch")!=std::string::npos)
    {
        detail.reason=LoadFailureReason::UnsupportedArch;
        detail.summary="Model architecture is not supported by this llama.cpp build";
        detail.suggestion="Update the server to a newer version that supports this model architecture, "
            "or use a different model.";
        detail.action="update_server";
        detail.recoverable=false;
        return detail;
    }

    // Generic model load failure — still include the log for debugging
    if(logLower.find("failed to load model")!=std::string::npos)
    {
        detail.reason=LoadFailureReason::BackendError;
        detail.summary="llama.cpp failed to load the model from: "+filePath;
        detail.suggestion="Check the llama.cpp log output above for specific error details. "
            "Common causes include corrupt files, insufficient memory, or unsupported model formats.";
        detail.action="check_logs";
        detail.recoverable=false;
        return detail;
    }

    // Fallback: generic context creation failure
    detail.reason=LoadFailureReason::Unknown;
    detail.summary="Model load failed for '"+model+"'";
    detail.suggestion="Check the server log for llama.cpp error output.";
    detail.action="check_logs";
    detail.recoverable=false;
    return detail;
}

LoadErrorDetail ModelRuntime::getLastLoadError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastLoadError;
}

void ModelRuntime::setMaxConcurrentDownloads(int max)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxConcurrentDownloads=std::max(1, max);
    m_downloadCv.notify_all();
}

int ModelRuntime::getMaxConcurrentDownloads() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxConcurrentDownloads;
}

ErrorCode ModelRuntime::loadModel(
    const std::string &model,
    const std::string &variant,
    int contextSize)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Clear previous load error
    m_lastLoadError=LoadErrorDetail{};

    // Check if already loaded
    auto it=m_models.find(model);
    if(it!=m_models.end())
    {
        if(it->second.state==ModelState::Loaded)
        {
            it->second.lastUsed=std::chrono::steady_clock::now();
            return ErrorCode::Success;
        }
        if(it->second.state==ModelState::Downloading)
        {
            return ErrorCode::ModelDownloading;
        }
        if(it->second.state==ModelState::Ready)
        {
            // Promote from Ready to Loaded
            // If llama model is in RAM but context was freed, recreate context
            if(it->second.llamaModel&&!it->second.llamaCtx)
            {
                llama_context_params cparams=llama_context_default_params();
                cparams.n_ctx=it->second.contextSize;
                cparams.n_threads=std::thread::hardware_concurrency();
                cparams.n_threads_batch=std::thread::hardware_concurrency();

                it->second.llamaCtx=llama_init_from_model(it->second.llamaModel, cparams);
                if(!it->second.llamaCtx)
                {
                    spdlog::error("Failed to recreate llama context for model: {}", model);
                    return ErrorCode::ModelLoadError;
                }
            }
            it->second.state=ModelState::Loaded;
            it->second.lastUsed=std::chrono::steady_clock::now();
            spdlog::info("Promoted model '{}' from Ready to Loaded", model);
            return ErrorCode::Success;
        }
    }

    // Look up model info from ModelManager
    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(model);
    if(!modelInfo.has_value())
    {
        spdlog::error("Model '{}' not found in ModelManager", model);
        return ErrorCode::ModelNotFound;
    }

    // Determine which variant to use
    std::string selectedVariant=variant;
    if(selectedVariant.empty()&&!modelInfo->variants.empty())
    {
        selectedVariant=selectBestVariant(modelInfo.value());
    }

    // Determine context size
    // For llama provider models, contextSize=0 means "use model's native
    // training context from GGUF metadata" — resolved in loadLlamaModel after
    // loading model weights.  For cloud models, fall back to the config value.
    int resolvedContext=contextSize;
    bool useNativeContext=(resolvedContext<=0&&modelInfo->provider=="llama");

    if(resolvedContext<=0&&!useNativeContext)
    {
        resolvedContext=modelInfo->contextWindow;
    }

    // Check hardware fit
    if(!modelInfo->variants.empty())
    {
        SystemInfo hw=HardwareDetector::instance().getSystemInfo();

        // Find the selected variant
        const ModelVariant *selectedVar=nullptr;
        for(const ModelVariant &v:modelInfo->variants)
        {
            if(v.quantization==selectedVariant)
            {
                selectedVar=&v;
                break;
            }
        }

        if(selectedVar)
        {
            ModelFit fit=ModelFitCalculator::calculateModelFit(modelInfo.value(), *selectedVar, hw);
            if(!fit.canRun)
            {
                m_lastLoadError.reason=(fit.limitingFactor=="ram")
                    ?LoadFailureReason::InsufficientRam
                    :LoadFailureReason::InsufficientVram;
                m_lastLoadError.summary="Model '"+model+"' variant '"+selectedVariant+
                    "' cannot run on this hardware: "+fit.limitingFactor;
                m_lastLoadError.suggestion=(fit.limitingFactor=="ram")
                    ?"Insufficient system RAM. Try a smaller quantization variant or close other applications."
                    :"Insufficient VRAM. Try a smaller quantization variant, reduce context size, or unload other models.";
                m_lastLoadError.action="use_smaller_variant";
                m_lastLoadError.recoverable=true;
                spdlog::error("Model '{}' variant '{}' cannot run: {}", model, selectedVariant, fit.limitingFactor);
                return ErrorCode::ModelLoadError;
            }

            // Evict if needed to make room
            evictIfNeeded(selectedVar->minVramMb);

            // Check if all model files exist, initiate async download for any missing ones
            std::vector<VariantDownload> allFiles=selectedVar->getAllFiles();
            std::string primaryFilename=selectedVar->getPrimaryFilename();

            if(!allFiles.empty())
            {
                bool anyMissing=false;
                for(const VariantDownload &file:allFiles)
                {
                    std::string filePath="/models/"+file.filename;
                    if(!std::filesystem::exists(filePath)&&!file.url.empty())
                    {
                        anyMissing=true;
                        break;
                    }
                }

                if(anyMissing)
                {
                    // Check storage quota for total missing file size
                    int64_t totalDownloadBytes=static_cast<int64_t>(selectedVar->fileSizeMb)*1024*1024;
                    if(!StorageManager::instance().canDownload(totalDownloadBytes))
                    {
                        StorageManager::instance().runCleanup();
                        if(!StorageManager::instance().canDownload(totalDownloadBytes))
                        {
                            spdlog::error("Insufficient storage to download '{}' variant '{}' ({} MB)",
                                model, selectedVariant, selectedVar->fileSizeMb);
                            return ErrorCode::InsufficientStorage;
                        }
                    }

                    // Mark as downloading and launch async background download
                    LoadedModel &dlEntry=m_models[model];
                    dlEntry.modelName=model;
                    dlEntry.variant=selectedVariant;
                    dlEntry.state=ModelState::Downloading;
                    dlEntry.lastUsed=std::chrono::steady_clock::now();

                    spdlog::info("Model '{}' variant '{}' needs download — launching async download",
                        model, selectedVariant);

                    m_downloadThreads.emplace_back(
                        &ModelRuntime::runBackgroundDownload, this,
                        model, selectedVariant, modelInfo.value());

                    return ErrorCode::ModelDownloading;
                }
            }

            // All files present — create/update loaded model entry
            LoadedModel &entry=m_models[model];
            entry.modelName=model;
            entry.variant=selectedVariant;
            entry.contextSize=useNativeContext?0:std::min(resolvedContext, fit.maxContextSize);
            entry.estimatedVramUsageMb=fit.estimatedVramUsageMb;
            entry.gpuIndices=fit.gpuIndices;
            entry.lastUsed=std::chrono::steady_clock::now();

            // Actually load llama.cpp model for local providers
            if(modelInfo->provider=="llama")
            {
                std::string filePath="/models/"+primaryFilename;
                ErrorCode loadResult=loadLlamaModel(model, filePath, entry.contextSize, entry.gpuIndices,
                    fit.maxContextSize);
                if(loadResult!=ErrorCode::Success)
                {
                    m_models.erase(model);
                    return loadResult;
                }
            }

            entry.state=ModelState::Loaded;

            spdlog::info("Loaded model '{}' variant '{}' (context={}, vram={}MB, gpus={})",
                model, selectedVariant, entry.contextSize, entry.estimatedVramUsageMb,
                entry.gpuIndices.size());
        }
        else if(!selectedVariant.empty())
        {
            spdlog::error("Variant '{}' not found for model '{}'", selectedVariant, model);
            return ErrorCode::ModelNotFound;
        }
    }
    else if(modelInfo->provider=="llama")
    {
        // Local llama models require variants for download/VRAM info
        spdlog::error("Model '{}' is a llama provider model but has no variants defined. "
            "Add a 'variants' array with quantization, download, and VRAM requirements.", model);
        return ErrorCode::InvalidRequest;
    }
    else
    {
        // Cloud model without variants — just track it
        LoadedModel &entry=m_models[model];
        entry.modelName=model;
        entry.state=ModelState::Loaded;
        entry.contextSize=resolvedContext;
        entry.lastUsed=std::chrono::steady_clock::now();
    }

    return ErrorCode::Success;
}

ErrorCode ModelRuntime::downloadModel(
    const std::string &model,
    const std::string &variant)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if already downloading
    auto it=m_models.find(model);
    if(it!=m_models.end())
    {
        if(it->second.state==ModelState::Downloading)
        {
            return ErrorCode::ModelDownloading;
        }
        if(it->second.state==ModelState::Loaded||it->second.state==ModelState::Ready)
        {
            return ErrorCode::Success;
        }
    }

    // Look up model info from ModelManager
    std::optional<ModelInfo> modelInfo=ModelManager::instance().getModelInfo(model);
    if(!modelInfo.has_value())
    {
        spdlog::error("downloadModel: model '{}' not found in ModelManager", model);
        return ErrorCode::ModelNotFound;
    }

    if(modelInfo->variants.empty())
    {
        // Cloud model — nothing to download
        return ErrorCode::Success;
    }

    // Determine which variant to use
    std::string selectedVariant=variant;
    if(selectedVariant.empty())
    {
        selectedVariant=selectBestVariant(modelInfo.value());
    }

    // Find the selected variant
    const ModelVariant *selectedVar=nullptr;
    for(const ModelVariant &v:modelInfo->variants)
    {
        if(v.quantization==selectedVariant)
        {
            selectedVar=&v;
            break;
        }
    }

    if(!selectedVar)
    {
        spdlog::error("downloadModel: variant '{}' not found for model '{}'", selectedVariant, model);
        return ErrorCode::ModelNotFound;
    }

    // Check if all files are already present
    std::vector<VariantDownload> allFiles=selectedVar->getAllFiles();
    bool anyMissing=false;
    for(const VariantDownload &file:allFiles)
    {
        std::string filePath="/models/"+file.filename;
        if(!std::filesystem::exists(filePath)&&!file.url.empty())
        {
            anyMissing=true;
            break;
        }
    }

    if(!anyMissing)
    {
        return ErrorCode::Success;
    }

    // Check storage quota
    int64_t totalDownloadBytes=static_cast<int64_t>(selectedVar->fileSizeMb)*1024*1024;
    if(!StorageManager::instance().canDownload(totalDownloadBytes))
    {
        StorageManager::instance().runCleanup();
        if(!StorageManager::instance().canDownload(totalDownloadBytes))
        {
            spdlog::error("Insufficient storage to download '{}' variant '{}' ({} MB)",
                model, selectedVariant, selectedVar->fileSizeMb);
            return ErrorCode::InsufficientStorage;
        }
    }

    // Mark as downloading and launch async background download
    LoadedModel &dlEntry=m_models[model];
    dlEntry.modelName=model;
    dlEntry.variant=selectedVariant;
    dlEntry.state=ModelState::Downloading;
    dlEntry.lastUsed=std::chrono::steady_clock::now();

    spdlog::info("downloadModel: launching async download for '{}' variant '{}'",
        model, selectedVariant);

    m_downloadThreads.emplace_back(
        &ModelRuntime::runBackgroundDownload, this,
        model, selectedVariant, modelInfo.value());

    return ErrorCode::ModelDownloading;
}

void ModelRuntime::runBackgroundDownload(
    const std::string &model,
    const std::string &variant,
    const ModelInfo &info)
{
    // Wait for a download slot (respects concurrent download limit)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_downloadCv.wait(lock, [this]()
        {
            return m_shuttingDown||m_activeDownloadCount<m_maxConcurrentDownloads;
        });

        if(m_shuttingDown)
        {
            return;
        }

        ++m_activeDownloadCount;
    }

    // Find the selected variant from the ModelInfo
    const ModelVariant *selectedVar=nullptr;
    for(const ModelVariant &v:info.variants)
    {
        if(v.quantization==variant)
        {
            selectedVar=&v;
            break;
        }
    }

    if(!selectedVar)
    {
        spdlog::error("runBackgroundDownload: variant '{}' not found for model '{}'", variant, model);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_models.erase(model);
        --m_activeDownloadCount;
        m_downloadCv.notify_one();
        return;
    }

    std::vector<VariantDownload> allFiles=selectedVar->getAllFiles();
    std::string primaryFilename=selectedVar->getPrimaryFilename();

    // Collect files that need downloading
    std::vector<const VariantDownload *> missingFiles;
    for(const VariantDownload &file:allFiles)
    {
        std::string filePath="/models/"+file.filename;
        if(!std::filesystem::exists(filePath)&&!file.url.empty())
        {
            missingFiles.push_back(&file);
        }
    }

    if(missingFiles.size()>1||allFiles.size()>1)
    {
        spdlog::info("Downloading model '{}' variant '{}' ({} files, {} missing)",
            model, variant, allFiles.size(), missingFiles.size());
    }
    else
    {
        spdlog::info("Downloading model '{}' variant '{}'", model, variant);
    }

    // Download each missing file (no mutex held)
    bool allDownloadsOk=true;
    for(const VariantDownload *file:missingFiles)
    {
        std::string filePath="/models/"+file->filename;
        bool downloadOk=downloadModelFile(
            file->url,
            filePath,
            file->sha256,
            model,
            variant);

        if(!downloadOk)
        {
            allDownloadsOk=false;
            spdlog::error("Failed to download shard '{}' for model '{}' variant '{}'",
                file->filename, model, variant);
            break;
        }
    }

    // Release the download slot
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        --m_activeDownloadCount;
        m_downloadCv.notify_one();
    }

    // Update model state under lock
    std::lock_guard<std::mutex> lock(m_mutex);

    if(!allDownloadsOk)
    {
        m_models.erase(model);
        spdlog::error("Download failed for model '{}' variant '{}'", model, variant);
        return;
    }

    // Register all files with StorageManager
    int64_t totalActualSize=0;
    std::vector<std::string> extraFiles;
    for(size_t i=0; i<allFiles.size(); ++i)
    {
        std::string filePath="/models/"+allFiles[i].filename;
        int64_t actualSize=0;
        std::error_code ec;
        if(std::filesystem::exists(filePath, ec))
        {
            actualSize=static_cast<int64_t>(std::filesystem::file_size(filePath, ec));
        }
        totalActualSize+=actualSize;
        if(i>0)
        {
            extraFiles.push_back(allFiles[i].filename);
        }
    }
    StorageManager::instance().registerDownload(
        model, variant, primaryFilename, totalActualSize, extraFiles);

    // Transition to Unloaded (downloaded, not yet loaded into VRAM)
    auto it=m_models.find(model);
    if(it!=m_models.end()&&it->second.state==ModelState::Downloading)
    {
        it->second.state=ModelState::Unloaded;
        spdlog::info("Download complete for model '{}' variant '{}' — state is now Unloaded", model, variant);
    }
}

ErrorCode ModelRuntime::unloadModel(const std::string &model)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_models.find(model);
    if(it==m_models.end())
    {
        return ErrorCode::ModelNotFound;
    }

    LoadedModel &entry=it->second;

    if(entry.state==ModelState::Unloaded)
    {
        return ErrorCode::Success;
    }

    if(entry.pinned)
    {
        // Move pinned model to Ready (keep in RAM)
        // Free context but keep model weights
        if(entry.llamaCtx)
        {
            llama_free(entry.llamaCtx);
            entry.llamaCtx=nullptr;
        }
        entry.state=ModelState::Ready;
        entry.ramUsageMb=entry.estimatedVramUsageMb; // approximate
        entry.vramUsageMb=0;
        spdlog::info("Model '{}' moved to Ready (pinned)", model);

        evictReadyModels();
    }
    else
    {
        freeLlamaModel(entry);
        entry.state=ModelState::Unloaded;
        entry.vramUsageMb=0;
        entry.ramUsageMb=0;
        spdlog::info("Model '{}' unloaded", model);
    }

    return ErrorCode::Success;
}

ErrorCode ModelRuntime::pinModel(const std::string &model)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_models.find(model);
    if(it==m_models.end())
    {
        return ErrorCode::ModelNotFound;
    }

    it->second.pinned=true;
    spdlog::info("Model '{}' pinned", model);
    return ErrorCode::Success;
}

ErrorCode ModelRuntime::unpinModel(const std::string &model)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_models.find(model);
    if(it==m_models.end())
    {
        return ErrorCode::ModelNotFound;
    }

    it->second.pinned=false;
    spdlog::info("Model '{}' unpinned", model);
    return ErrorCode::Success;
}

ErrorCode ModelRuntime::swapModel(
    const std::string &newModel,
    const std::string &variant,
    int contextSize)
{
    if(m_inferenceActive)
    {
        // Queue the swap for when inference completes
        std::lock_guard<std::mutex> lock(m_mutex);
        SwapRequest req;
        req.model=newModel;
        req.variant=variant;
        req.contextSize=contextSize;
        m_pendingSwaps.push(req);
        spdlog::info("Swap to '{}' queued (inference active)", newModel);
        return ErrorCode::ModelDownloading; // "queued" status
    }

    std::chrono::steady_clock::time_point swapStart=std::chrono::steady_clock::now();

    // Identify current loaded model for telemetry
    std::string fromModel;

    // Unload all currently Loaded models
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for(auto &pair:m_models)
        {
            if(pair.second.state==ModelState::Loaded)
            {
                if(fromModel.empty())
                {
                    fromModel=pair.first;
                }

                if(pair.second.pinned)
                {
                    pair.second.state=ModelState::Ready;
                    pair.second.ramUsageMb=pair.second.estimatedVramUsageMb;
                    pair.second.vramUsageMb=0;
                }
                else
                {
                    pair.second.state=ModelState::Unloaded;
                    pair.second.vramUsageMb=0;
                    pair.second.ramUsageMb=0;
                }
            }
        }
    }

    ErrorCode result=loadModel(newModel, variant, contextSize);

    // Record swap telemetry
    std::chrono::steady_clock::time_point swapEnd=std::chrono::steady_clock::now();
    double swapTimeMs=std::chrono::duration<double, std::milli>(swapEnd-swapStart).count();
    TelemetryCollector::instance().recordModelSwap(fromModel, newModel, swapTimeMs);

    return result;
}

std::vector<LoadedModel> ModelRuntime::getModelStates() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<LoadedModel> result;
    result.reserve(m_models.size());
    for(const auto &pair:m_models)
    {
        result.push_back(pair.second);
    }
    return result;
}

std::optional<LoadedModel> ModelRuntime::getModelState(const std::string &model) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_models.find(model);
    if(it!=m_models.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::vector<ModelFit> ModelRuntime::getLocalModelCapabilities() const
{
    SystemInfo hw=HardwareDetector::instance().getSystemInfo();
    std::vector<ModelInfo> allModels=ModelManager::instance().getModelsByRanking();
    return ModelFitCalculator::calculateFittableModels(allModels, hw);
}

std::vector<DownloadProgressSnapshot> ModelRuntime::getActiveDownloadSnapshots()
{
    return m_downloader.getActiveSnapshots();
}

std::optional<DownloadProgressSnapshot> ModelRuntime::getDownloadSnapshot(const std::string &modelName)
{
    return m_downloader.getProgressSnapshot(modelName);
}

void ModelRuntime::setReadyRamBudget(int mb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_readyRamBudgetMb=mb;
}

int ModelRuntime::getReadyRamBudget() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_readyRamBudgetMb;
}

void ModelRuntime::evictIfNeeded(int requiredVramMb)
{
    // Calculate current VRAM usage across all loaded models
    int currentVramUsage=0;
    for(const auto &pair:m_models)
    {
        if(pair.second.state==ModelState::Loaded)
        {
            currentVramUsage+=pair.second.estimatedVramUsageMb;
        }
    }

    int totalFreeVram=HardwareDetector::instance().getTotalFreeVramMb();
    int available=totalFreeVram-currentVramUsage;

    if(available>=requiredVramMb)
    {
        return; // enough VRAM already
    }

    int needToFree=requiredVramMb-available;

    // Collect eviction candidates: loaded, non-pinned, not currently in inference
    struct EvictCandidate {
        std::string model;
        int vramMb;
        std::chrono::steady_clock::time_point lastUsed;
    };

    std::vector<EvictCandidate> candidates;
    for(const auto &pair:m_models)
    {
        if(pair.second.state==ModelState::Loaded&&
            !pair.second.pinned&&
            pair.first!=m_inferenceModel)
        {
            candidates.push_back({pair.first, pair.second.estimatedVramUsageMb, pair.second.lastUsed});
        }
    }

    // Sort by LRU (oldest first)
    std::sort(candidates.begin(), candidates.end(),
        [](const EvictCandidate &a, const EvictCandidate &b)
        {
            return a.lastUsed<b.lastUsed;
        });

    int freed=0;
    for(const EvictCandidate &candidate:candidates)
    {
        if(freed>=needToFree)
        {
            break;
        }

        auto it=m_models.find(candidate.model);
        if(it!=m_models.end())
        {
            freeLlamaModel(it->second);
            it->second.state=ModelState::Unloaded;
            it->second.vramUsageMb=0;
            it->second.ramUsageMb=0;
            freed+=candidate.vramMb;
            spdlog::info("Evicted model '{}' to free {}MB VRAM", candidate.model, candidate.vramMb);
        }
    }
}

void ModelRuntime::beginInference(const std::string &model)
{
    m_inferenceActive=true;
    m_inferenceModel=model;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it=m_models.find(model);
    if(it!=m_models.end())
    {
        it->second.lastUsed=std::chrono::steady_clock::now();
    }
}

void ModelRuntime::endInference()
{
    // Record usage for storage tracking
    if(!m_inferenceModel.empty())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it=m_models.find(m_inferenceModel);
        if(it!=m_models.end())
        {
            StorageManager::instance().recordUsage(m_inferenceModel, it->second.variant);
        }
    }

    m_inferenceActive=false;
    m_inferenceModel.clear();
    drainPendingSwaps();
}

bool ModelRuntime::isInferenceActive() const
{
    return m_inferenceActive;
}

std::string ModelRuntime::selectBestVariant(const ModelInfo &model) const
{
    if(model.variants.empty())
    {
        return "";
    }

    SystemInfo hw=HardwareDetector::instance().getSystemInfo();

    // Prefer the highest-quality variant that fits
    // Variants are typically ordered by quality (lower quant = smaller file = lower quality)
    // So iterate in reverse to find the best that fits
    std::string bestVariant;
    int bestVram=0;

    for(const ModelVariant &v:model.variants)
    {
        ModelFit fit=ModelFitCalculator::calculateModelFit(model, v, hw);
        if(fit.canRun&&v.recommendedVramMb>bestVram)
        {
            bestVariant=v.quantization;
            bestVram=v.recommendedVramMb;
        }
    }

    // If no variant fits at recommended, try minimum
    if(bestVariant.empty())
    {
        for(const ModelVariant &v:model.variants)
        {
            ModelFit fit=ModelFitCalculator::calculateModelFit(model, v, hw);
            if(fit.canRun)
            {
                bestVariant=v.quantization;
                break;
            }
        }
    }

    // Fallback to first variant
    if(bestVariant.empty())
    {
        bestVariant=model.variants[0].quantization;
    }

    return bestVariant;
}

void ModelRuntime::drainPendingSwaps()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if(m_pendingSwaps.empty())
    {
        return;
    }

    // Only execute the most recent swap, discard stale ones
    SwapRequest latest=m_pendingSwaps.back();
    while(!m_pendingSwaps.empty())
    {
        m_pendingSwaps.pop();
    }

    // Release lock before calling swapModel (it acquires its own lock)
    m_mutex.unlock();
    swapModel(latest.model, latest.variant, latest.contextSize);
    m_mutex.lock();
}

int ModelRuntime::calculateReadyRamUsage() const
{
    int total=0;
    for(const auto &pair:m_models)
    {
        if(pair.second.state==ModelState::Ready)
        {
            total+=pair.second.ramUsageMb;
        }
    }
    return total;
}

void ModelRuntime::evictReadyModels()
{
    int currentUsage=calculateReadyRamUsage();
    if(currentUsage<=m_readyRamBudgetMb)
    {
        return;
    }

    // Collect non-pinned Ready models, sort by LRU
    struct ReadyCandidate {
        std::string model;
        int ramMb;
        std::chrono::steady_clock::time_point lastUsed;
    };

    std::vector<ReadyCandidate> candidates;
    for(const auto &pair:m_models)
    {
        if(pair.second.state==ModelState::Ready&&!pair.second.pinned)
        {
            candidates.push_back({pair.first, pair.second.ramUsageMb, pair.second.lastUsed});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const ReadyCandidate &a, const ReadyCandidate &b)
        {
            return a.lastUsed<b.lastUsed;
        });

    for(const ReadyCandidate &candidate:candidates)
    {
        if(currentUsage<=m_readyRamBudgetMb)
        {
            break;
        }

        auto it=m_models.find(candidate.model);
        if(it!=m_models.end())
        {
            freeLlamaModel(it->second);
            it->second.state=ModelState::Unloaded;
            it->second.ramUsageMb=0;
            currentUsage-=candidate.ramMb;
            spdlog::info("Evicted Ready model '{}' to free {}MB RAM", candidate.model, candidate.ramMb);
        }
    }
}

void ModelRuntime::initLlamaBackend()
{
    if(!m_llamaInitialized)
    {
        llama_backend_init();
        m_llamaInitialized=true;
        spdlog::info("llama.cpp backend initialized");
    }
}

ErrorCode ModelRuntime::loadLlamaModel(
    const std::string &model,
    const std::string &filePath,
    int contextSize,
    const std::vector<int> &gpuIndices,
    int maxHardwareContext)
{
    initLlamaBackend();

    // Start capturing llama.cpp log output for diagnostics
    beginLlamaLogCapture();

    llama_model_params mparams=llama_model_default_params();
    mparams.n_gpu_layers=99; // offload all layers to GPU by default

    llama_model *llamaModel=llama_model_load_from_file(filePath.c_str(), mparams);
    if(!llamaModel)
    {
        std::string captured=m_llamaLogCapture.str();
        endLlamaLogCapture();

        m_lastLoadError=classifyLoadFailure(captured, model, filePath, contextSize);
        spdlog::error("Failed to load llama model from: {} — {}", filePath, m_lastLoadError.summary);
        return ErrorCode::ModelLoadError;
    }

    // Query native training context from GGUF metadata
    int nativeContext=llama_model_n_ctx_train(llamaModel);

    // Resolve actual context to allocate:
    //   contextSize > 0  → user/config requested explicit size
    //   contextSize == 0 → use model's native training context
    // In both cases, cap by the hardware-fit maximum.
    int actualContext=contextSize;
    if(actualContext<=0)
    {
        actualContext=nativeContext;
    }
    if(maxHardwareContext>0&&actualContext>maxHardwareContext)
    {
        spdlog::info("Capping context from {} to {} (hardware limit) for model '{}'",
            actualContext, maxHardwareContext, model);
        actualContext=maxHardwareContext;
    }

    llama_context_params cparams=llama_context_default_params();
    cparams.n_ctx=static_cast<uint32_t>(actualContext);
    cparams.n_threads=std::thread::hardware_concurrency();
    cparams.n_threads_batch=std::thread::hardware_concurrency();

    llama_context *llamaCtx=llama_init_from_model(llamaModel, cparams);
    if(!llamaCtx)
    {
        std::string captured=m_llamaLogCapture.str();
        endLlamaLogCapture();

        m_lastLoadError=classifyLoadFailure(captured, model, filePath, actualContext);

        // If classification didn't catch a specific VRAM/context issue,
        // context creation failure is almost always a memory issue
        if(m_lastLoadError.reason==LoadFailureReason::Unknown||
            m_lastLoadError.reason==LoadFailureReason::BackendError)
        {
            m_lastLoadError.reason=LoadFailureReason::InsufficientVram;
            m_lastLoadError.summary="Failed to create context (size="+std::to_string(actualContext)+
                ") — likely insufficient GPU memory";
            m_lastLoadError.suggestion="Try a smaller context size or use a smaller quantization variant. "
                "You can also unload other models to free VRAM.";
            m_lastLoadError.action="reduce_context";
            m_lastLoadError.recoverable=true;
        }

        spdlog::error("Failed to create llama context for model: {} — {}", model, m_lastLoadError.summary);
        llama_model_free(llamaModel);
        return ErrorCode::ModelLoadError;
    }

    endLlamaLogCapture();

    LoadedModel &entry=m_models[model];
    entry.llamaModel=llamaModel;
    entry.llamaCtx=llamaCtx;
    entry.maxContextSize=nativeContext;
    entry.contextSize=static_cast<int>(llama_n_ctx(llamaCtx));

    spdlog::info("llama.cpp model loaded: {} (context={}, maxContext={})",
        model, entry.contextSize, entry.maxContextSize);
    return ErrorCode::Success;
}

void ModelRuntime::freeLlamaModel(LoadedModel &entry)
{
    if(entry.llamaCtx)
    {
        llama_free(entry.llamaCtx);
        entry.llamaCtx=nullptr;
    }
    if(entry.llamaModel)
    {
        llama_model_free(entry.llamaModel);
        entry.llamaModel=nullptr;
    }
}

llama_model *ModelRuntime::getLlamaModel(const std::string &model) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_models.find(model);
    if(it!=m_models.end()&&it->second.state==ModelState::Loaded)
    {
        return it->second.llamaModel;
    }
    return nullptr;
}

llama_context *ModelRuntime::getLlamaContext(const std::string &model) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_models.find(model);
    if(it!=m_models.end()&&it->second.state==ModelState::Loaded)
    {
        return it->second.llamaCtx;
    }
    return nullptr;
}

std::optional<ModelInfo> ModelRuntime::getLoadedModelInfo(const std::string &model) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_models.find(model);
    if(it==m_models.end())
    {
        return std::nullopt;
    }
    return ModelManager::instance().getModelInfo(model);
}

bool ModelRuntime::downloadModelFile(
    const std::string &url,
    const std::string &filePath,
    const std::string &sha256,
    const std::string &modelName,
    const std::string &variant)
{
    std::optional<std::string> hash=std::nullopt;
    if(!sha256.empty())
    {
        hash=sha256;
    }

    spdlog::info("Starting synchronous download: {} -> {}", url, filePath);

    auto lastLogTime=std::chrono::steady_clock::now();

    std::future<bool> result=m_downloader.downloadModelWithProgress(
        url, filePath, hash,
        [&modelName, &lastLogTime](int64_t bytesDownloaded, int64_t totalBytes, float percent)
        {
            auto now=std::chrono::steady_clock::now();
            double elapsed=std::chrono::duration<double>(now-lastLogTime).count();
            if(elapsed>=5.0||percent>=100.0f)
            {
                lastLogTime=now;
                if(totalBytes>0)
                {
                    spdlog::info("Downloading '{}': {:.1f}% ({}/{} MB)",
                        modelName, percent,
                        bytesDownloaded/(1024*1024),
                        totalBytes/(1024*1024));
                }
            }
        },
        modelName,
        variant);

    bool success=result.get();

    if(success)
    {
        spdlog::info("Download complete: {}", filePath);
    }
    else
    {
        spdlog::error("Download failed: {}", filePath);
    }

    return success;
}

} // namespace arbiterAI
