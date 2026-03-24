#ifndef _ARBITERAI_MODELRUNTIME_H_
#define _ARBITERAI_MODELRUNTIME_H_

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/modelFitCalculator.h"
#include "arbiterAI/modelDownloader.h"

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <queue>
#include <functional>
#include <chrono>

// Forward declarations for llama.cpp types
struct llama_model;
struct llama_context;

namespace arbiterAI
{

enum class ModelState {
    Unloaded,
    Downloading,
    Ready,      // in system RAM, quick to reload to VRAM
    Loaded,     // fully loaded in VRAM, ready for inference
    Unloading
};

struct LoadedModel {
    std::string modelName;
    std::string variant;
    ModelState state=ModelState::Unloaded;
    int vramUsageMb=0;
    int ramUsageMb=0;
    int estimatedVramUsageMb=0;
    int contextSize=0;
    std::vector<int> gpuIndices;
    std::chrono::steady_clock::time_point lastUsed;
    bool pinned=false;
    llama_model *llamaModel=nullptr;
    llama_context *llamaCtx=nullptr;
};

class ModelRuntime {
public:
    static ModelRuntime &instance();
    static void reset(); // For testing

    /// Load a model into VRAM for inference.
    /// @param model     Model name from ModelManager.
    /// @param variant   Quantization variant (empty = auto-select best fitting).
    /// @param contextSize  Context size (0 = use model default).
    /// @return ErrorCode::Success, ModelDownloading, ModelNotFound, ModelLoadError.
    ErrorCode loadModel(
        const std::string &model,
        const std::string &variant="",
        int contextSize=0);

    /// Unload a model. Pinned models move to Ready; others to Unloaded.
    ErrorCode unloadModel(const std::string &model);

    /// Pin a model to keep it in RAM for quick reload after VRAM eviction.
    ErrorCode pinModel(const std::string &model);

    /// Unpin a model, allowing LRU eviction from RAM.
    ErrorCode unpinModel(const std::string &model);

    /// Swap the active model: unload current, load new.
    /// If inference is active, the swap is queued.
    /// @return ErrorCode::Success (immediate) or ModelDownloading (queued).
    ErrorCode swapModel(
        const std::string &newModel,
        const std::string &variant="",
        int contextSize=0);

    /// Get the state of all tracked models.
    std::vector<LoadedModel> getModelStates() const;

    /// Get the state of a specific model.
    /// @return nullopt if the model is not tracked.
    std::optional<LoadedModel> getModelState(const std::string &model) const;

    /// Get model fit capabilities for all local models given current hardware.
    std::vector<ModelFit> getLocalModelCapabilities() const;

    /// Set the RAM budget for "Ready" tier models (MB).
    void setReadyRamBudget(int mb);

    /// Get the current RAM budget for "Ready" tier models.
    int getReadyRamBudget() const;

    /// Evict least-recently-used non-pinned models to free VRAM.
    void evictIfNeeded(int requiredVramMb);

    /// Mark inference as started (blocks swap execution).
    void beginInference(const std::string &model);

    /// Mark inference as completed and drain pending swaps.
    void endInference();

    /// Check if inference is currently active.
    bool isInferenceActive() const;

    /// Get the llama_model handle for a loaded local model.
    /// Returns nullptr if not loaded or not a local model.
    llama_model *getLlamaModel(const std::string &model) const;

    /// Get the llama_context handle for a loaded local model.
    /// Returns nullptr if not loaded or not a local model.
    llama_context *getLlamaContext(const std::string &model) const;

    /// Get the ModelInfo for a loaded model.
    std::optional<ModelInfo> getLoadedModelInfo(const std::string &model) const;

private:
    ModelRuntime();

    ModelRuntime(const ModelRuntime &)=delete;
    ModelRuntime &operator=(const ModelRuntime &)=delete;

    /// Select the best variant for a model given current hardware.
    std::string selectBestVariant(const ModelInfo &model) const;

    /// Execute a pending swap (called when inference completes).
    void drainPendingSwaps();

    /// Calculate ready-tier RAM usage across all Ready models.
    int calculateReadyRamUsage() const;

    /// Evict LRU non-pinned Ready models to stay within RAM budget.
    void evictReadyModels();

    /// Initialize the llama.cpp backend (called once on first local model load).
    void initLlamaBackend();

    /// Load a GGUF file into llama.cpp.
    ErrorCode loadLlamaModel(
        const std::string &model,
        const std::string &filePath,
        int contextSize,
        const std::vector<int> &gpuIndices);

    /// Free llama.cpp resources for a model.
    void freeLlamaModel(LoadedModel &entry);

    /// Download a model file synchronously.
    /// @return true on success, false on failure.
    bool downloadModelFile(
        const std::string &url,
        const std::string &filePath,
        const std::string &sha256,
        const std::string &modelName);

    std::map<std::string, LoadedModel> m_models;
    mutable std::mutex m_mutex;
    int m_readyRamBudgetMb=0;
    std::atomic<bool> m_inferenceActive{false};
    std::string m_inferenceModel;
    bool m_llamaInitialized=false;

    struct SwapRequest {
        std::string model;
        std::string variant;
        int contextSize=0;
    };
    std::queue<SwapRequest> m_pendingSwaps;

    ModelDownloader m_downloader;
};

} // namespace arbiterAI

#endif//_ARBITERAI_MODELRUNTIME_H_
