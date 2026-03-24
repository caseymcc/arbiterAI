#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/telemetryCollector.h"

#include <llama.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <thread>

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
    std::lock_guard<std::mutex> lock(rt.m_mutex);

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

ErrorCode ModelRuntime::loadModel(
    const std::string &model,
    const std::string &variant,
    int contextSize)
{
    std::lock_guard<std::mutex> lock(m_mutex);

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
    int resolvedContext=contextSize;
    if(resolvedContext<=0)
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
                spdlog::error("Model '{}' variant '{}' cannot run: {}", model, selectedVariant, fit.limitingFactor);
                return ErrorCode::ModelLoadError;
            }

            // Evict if needed to make room
            evictIfNeeded(selectedVar->minVramMb);

            // Check if model file exists, initiate download if needed
            if(!selectedVar->download.filename.empty())
            {
                std::string filePath="/models/"+selectedVar->download.filename;
                if(!std::filesystem::exists(filePath)&&!selectedVar->download.url.empty())
                {
                    // Mark as downloading
                    LoadedModel &dlEntry=m_models[model];
                    dlEntry.modelName=model;
                    dlEntry.variant=selectedVariant;
                    dlEntry.state=ModelState::Downloading;
                    dlEntry.lastUsed=std::chrono::steady_clock::now();
                    spdlog::info("Downloading model '{}' variant '{}'", model, selectedVariant);

                    // Release the lock during download (can be very large)
                    m_mutex.unlock();
                    bool downloadOk=downloadModelFile(
                        selectedVar->download.url,
                        filePath,
                        selectedVar->download.sha256,
                        model);
                    m_mutex.lock();

                    if(!downloadOk)
                    {
                        m_models.erase(model);
                        return ErrorCode::ModelDownloadFailed;
                    }
                }
            }

            // Create/update loaded model entry
            LoadedModel &entry=m_models[model];
            entry.modelName=model;
            entry.variant=selectedVariant;
            entry.contextSize=std::min(resolvedContext, fit.maxContextSize);
            entry.estimatedVramUsageMb=fit.estimatedVramUsageMb;
            entry.gpuIndices=fit.gpuIndices;
            entry.lastUsed=std::chrono::steady_clock::now();

            // Actually load llama.cpp model for local providers
            if(modelInfo->provider=="llama")
            {
                std::string filePath="/models/"+selectedVar->download.filename;
                ErrorCode loadResult=loadLlamaModel(model, filePath, entry.contextSize, entry.gpuIndices);
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
    else
    {
        // Cloud model or model without variants — just track it
        LoadedModel &entry=m_models[model];
        entry.modelName=model;
        entry.state=ModelState::Loaded;
        entry.contextSize=resolvedContext;
        entry.lastUsed=std::chrono::steady_clock::now();
    }

    return ErrorCode::Success;
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
    const std::vector<int> &gpuIndices)
{
    initLlamaBackend();

    llama_model_params mparams=llama_model_default_params();
    mparams.n_gpu_layers=99; // offload all layers to GPU by default

    llama_model *llamaModel=llama_model_load_from_file(filePath.c_str(), mparams);
    if(!llamaModel)
    {
        spdlog::error("Failed to load llama model from: {}", filePath);
        return ErrorCode::ModelLoadError;
    }

    llama_context_params cparams=llama_context_default_params();
    cparams.n_ctx=contextSize;
    cparams.n_threads=std::thread::hardware_concurrency();
    cparams.n_threads_batch=std::thread::hardware_concurrency();

    llama_context *llamaCtx=llama_init_from_model(llamaModel, cparams);
    if(!llamaCtx)
    {
        spdlog::error("Failed to create llama context for model: {}", model);
        llama_model_free(llamaModel);
        return ErrorCode::ModelLoadError;
    }

    LoadedModel &entry=m_models[model];
    entry.llamaModel=llamaModel;
    entry.llamaCtx=llamaCtx;

    spdlog::info("llama.cpp model loaded: {} (context={})", model, contextSize);
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
    const std::string &modelName)
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
        modelName);

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
