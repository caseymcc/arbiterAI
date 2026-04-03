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
#include <sstream>
#include <thread>
#include <condition_variable>

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

/// Classification of why a model load failed, with actionable guidance.
enum class LoadFailureReason {
    Unknown,            // could not determine
    FileNotFound,       // GGUF file does not exist at expected path
    FileCorrupt,        // GGUF header invalid or file truncated
    InsufficientVram,   // not enough VRAM / failed to allocate GPU buffers
    InsufficientRam,    // not enough system RAM
    ContextTooLarge,    // requested context exceeds model or hardware limits
    UnsupportedArch,    // model architecture not supported by this llama.cpp build
    BackendError        // llama.cpp internal error
};

/// Convert a LoadFailureReason to a stable, snake_case string for API responses.
const char *loadFailureReasonToString(LoadFailureReason reason);

/// Detailed information about the last model load failure.
struct LoadErrorDetail {
    LoadFailureReason reason=LoadFailureReason::Unknown;
    std::string summary;        // one-line human-readable summary
    std::string suggestion;     // actionable fix for a human operator
    std::string action;         // machine-readable recovery action (e.g. "redownload", "reduce_context")
    bool recoverable=false;     // true if the caller can take an automated action to fix the problem
    std::string llamaLog;       // raw llama.cpp log output captured during the load attempt
};

struct LoadedModel {
    std::string modelName;
    std::string variant;
    ModelState state=ModelState::Unloaded;
    int vramUsageMb=0;
    int ramUsageMb=0;
    int estimatedVramUsageMb=0;
    int contextSize=0;
    int maxContextSize=0; // model's native/training context from GGUF metadata
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
    /// If files are not yet downloaded, triggers an async download and returns
    /// ModelDownloading immediately.  If a download is already in progress for
    /// this model the call also returns ModelDownloading.
    /// @param model     Model name from ModelManager.
    /// @param variant   Quantization variant (empty = auto-select best fitting).
    /// @param contextSize  Context size (0 = use model default).
    /// @return ErrorCode::Success, ModelDownloading, ModelNotFound, ModelLoadError.
    ErrorCode loadModel(
        const std::string &model,
        const std::string &variant="",
        int contextSize=0);

    /// Download model files without loading into VRAM.
    /// Launches an async background download that respects the concurrent
    /// download limit.  Returns ModelDownloading on success, Success if
    /// files are already present, or an error code.
    ErrorCode downloadModel(
        const std::string &model,
        const std::string &variant="");

    /// Set the maximum number of concurrent model downloads (default: 2).
    void setMaxConcurrentDownloads(int max);

    /// Get the current concurrent download limit.
    int getMaxConcurrentDownloads() const;

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

    /// Get progress snapshots for all active downloads (with speed and ETA).
    std::vector<DownloadProgressSnapshot> getActiveDownloadSnapshots();

    /// Get a download progress snapshot for a specific model.
    std::optional<DownloadProgressSnapshot> getDownloadSnapshot(const std::string &modelName);

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

    /// Get detailed information about the most recent model load failure.
    /// Cleared at the start of each loadModel() call.
    LoadErrorDetail getLastLoadError() const;

    /// Called from the llama.cpp log callback to append captured text.
    /// Public so the C-style callback can reach it; not intended for external use.
    void appendLlamaLog(const char *text);

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
    /// @param contextSize      Requested context (0 = use model's native training context).
    /// @param maxHardwareContext  Hardware-fit limit (0 = no limit).
    ErrorCode loadLlamaModel(
        const std::string &model,
        const std::string &filePath,
        int contextSize,
        const std::vector<int> &gpuIndices,
        int maxHardwareContext=0);

    /// Free llama.cpp resources for a model.
    void freeLlamaModel(LoadedModel &entry);

    /// Download a model file synchronously.
    /// @return true on success, false on failure.
    bool downloadModelFile(
        const std::string &url,
        const std::string &filePath,
        const std::string &sha256,
        const std::string &modelName,
        const std::string &variant);

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

    /// Background download concurrency management.
    int m_maxConcurrentDownloads=2;
    int m_activeDownloadCount=0;
    bool m_shuttingDown=false;
    std::condition_variable m_downloadCv;
    std::vector<std::thread> m_downloadThreads;

    /// Internal: run a background download for a model.  Respects the
    /// concurrent download semaphore and registers files with StorageManager
    /// on success.  Called on a detached background thread.
    void runBackgroundDownload(
        const std::string &model,
        const std::string &variant,
        const ModelInfo &info);

    /// Last load error detail (set in loadLlamaModel, cleared in loadModel).
    LoadErrorDetail m_lastLoadError;

    /// Buffer for capturing llama.cpp log output during model load.
    std::ostringstream m_llamaLogCapture;
    bool m_capturingLlamaLog=false;

    /// Install/remove the llama.cpp log callback that routes to m_llamaLogCapture.
    void beginLlamaLogCapture();
    void endLlamaLogCapture();

    /// Analyze captured llama.cpp log to classify the failure reason.
    LoadErrorDetail classifyLoadFailure(
        const std::string &llamaLog,
        const std::string &model,
        const std::string &filePath,
        int contextSize) const;
};

} // namespace arbiterAI

#endif//_ARBITERAI_MODELRUNTIME_H_
