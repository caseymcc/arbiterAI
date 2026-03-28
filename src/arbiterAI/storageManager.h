#ifndef _ARBITERAI_STORAGEMANAGER_H_
#define _ARBITERAI_STORAGEMANAGER_H_

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>

namespace arbiterAI
{

struct StorageInfo {
    std::filesystem::path modelsDirectory;
    int64_t totalDiskBytes=0;           // partition total
    int64_t freeDiskBytes=0;            // partition free
    int64_t usedByModelsBytes=0;        // sum of all tracked model files
    int64_t storageLimitBytes=0;        // configured limit (0 = use all free)
    int64_t availableForModelsBytes=0;  // min(freeDisk, limit - usedByModels)
    int modelCount=0;
    bool cleanupEnabled=true;
};

struct DownloadedModelFile {
    std::string modelName;
    std::string variant;             // quantization (e.g., "Q4_K_M")
    std::string filename;
    std::filesystem::path filePath;
    int64_t fileSizeBytes=0;
    std::chrono::system_clock::time_point downloadedAt;
    std::chrono::system_clock::time_point lastUsedAt;
    int usageCount=0;                // number of inference requests served
    bool hotReady=false;             // keep weights in RAM for quick VRAM reload
    bool isProtected=false;          // protected from deletion (manual and automated)
    std::string runtimeState;        // cross-referenced from ModelRuntime
};

struct CleanupPolicy {
    bool enabled=true;
    std::chrono::hours maxAge{30*24};              // 30 days
    std::chrono::hours checkInterval{24};          // run every 24 hours
    double targetFreePercent=20.0;                 // try to keep 20% free
    bool respectHotReady=true;                     // never delete hot_ready variants
    bool respectProtected=true;                    // never delete protected variants
};

struct CleanupCandidate {
    std::string modelName;
    std::string variant;
    std::string filename;
    int64_t fileSizeBytes=0;
    std::chrono::system_clock::time_point lastUsedAt;
    int usageCount=0;
};

class StorageManager {
public:
    static StorageManager &instance();
    static void reset(); // For testing

    /// Initialize with the models directory path.
    void initialize(const std::filesystem::path &modelsDir);

    /// Shut down the background flush/cleanup timers.
    void shutdown();

    /// Set the storage limit in bytes. 0 = use all free disk space.
    void setStorageLimit(int64_t limitBytes);

    /// Get the current storage limit.
    int64_t getStorageLimit() const;

    /// Get a snapshot of current storage usage.
    StorageInfo getStorageInfo() const;

    /// Check if a download of the given size can proceed.
    /// @return true if enough space, false otherwise.
    bool canDownload(int64_t fileSizeBytes) const;

    /// Get the list of all downloaded model files with usage stats.
    std::vector<DownloadedModelFile> getDownloadedModels() const;

    /// Register a completed download (updates inventory).
    void registerDownload(const std::string &modelName,
        const std::string &variant,
        const std::string &filename,
        int64_t fileSizeBytes);

    /// Record a model usage event (inference served).
    void recordUsage(const std::string &modelName, const std::string &variant);

    /// Delete a downloaded model file from disk.
    /// Fails if the variant is hot ready or protected (must clear flags first).
    /// @param modelName Model name.
    /// @param variant Specific variant to delete. If empty, deletes all variants of the model.
    /// @param freedBytes [out] Total bytes freed.
    /// @return true if deleted, false if file not found, deletion failed, or guarded.
    bool deleteModelFile(const std::string &modelName, const std::string &variant,
        int64_t &freedBytes);

    /// Set/clear hot ready on a variant (keep weights in RAM for quick VRAM reload).
    /// @return true if the variant was found, false otherwise.
    bool setHotReady(const std::string &modelName, const std::string &variant, bool enabled);

    /// Set/clear protected on a variant (prevent deletion, manual or automated).
    /// @return true if the variant was found, false otherwise.
    bool setProtected(const std::string &modelName, const std::string &variant, bool enabled);

    /// Get the storage stats for a specific model (all variants).
    std::vector<DownloadedModelFile> getModelStats(const std::string &modelName) const;

    /// Get the storage stats for a specific model variant.
    std::optional<DownloadedModelFile> getVariantStats(
        const std::string &modelName, const std::string &variant) const;

    /// Check if a variant is guarded (hot ready or protected).
    /// @return true if either flag is set, false otherwise.
    bool isGuarded(const std::string &modelName, const std::string &variant) const;

    /// Flush usage stats to disk (called periodically and on shutdown).
    void flush();

    /// Scan the models directory for GGUF files not yet in the inventory.
    void scanModelsDirectory();

    // ========== Cleanup ==========

    /// Set the cleanup policy.
    void setCleanupPolicy(const CleanupPolicy &policy);

    /// Get the current cleanup policy.
    CleanupPolicy getCleanupPolicy() const;

    /// Preview what automated cleanup would delete (without deleting anything).
    std::vector<CleanupCandidate> previewCleanup() const;

    /// Run cleanup: remove stale, unguarded, unloaded variants.
    /// @return Total bytes freed.
    int64_t runCleanup();

private:
    StorageManager()=default;
    ~StorageManager();

    StorageManager(const StorageManager &)=delete;
    StorageManager &operator=(const StorageManager &)=delete;

    struct ModelFileEntry {
        std::string modelName;
        std::string variant;
        std::string filename;
        int64_t fileSizeBytes=0;
        std::chrono::system_clock::time_point downloadedAt;
        std::chrono::system_clock::time_point lastUsedAt;
        int usageCount=0;
        bool hotReady=false;
        bool isProtected=false;
    };

    /// Find an entry by model name and variant.
    ModelFileEntry *findEntry(const std::string &modelName, const std::string &variant);
    const ModelFileEntry *findEntry(const std::string &modelName, const std::string &variant) const;

    void loadUsageData();
    void saveUsageData() const;

    /// Collect cleanup candidates (caller holds m_mutex).
    std::vector<CleanupCandidate> collectCleanupCandidates() const;

    /// Convert internal entry to public DownloadedModelFile.
    DownloadedModelFile entryToPublic(const ModelFileEntry &entry) const;

    /// Start the background flush/cleanup timer.
    void startBackgroundTimer();

    std::filesystem::path m_modelsDir;
    int64_t m_storageLimitBytes=0;

    std::vector<ModelFileEntry> m_entries;
    mutable std::mutex m_mutex;
    bool m_initialized=false;
    bool m_dirty=false; // has unsaved changes

    CleanupPolicy m_cleanupPolicy;

    // Background timer
    std::thread m_timerThread;
    std::atomic<bool> m_timerRunning{false};
};

} // namespace arbiterAI

#endif//_ARBITERAI_STORAGEMANAGER_H_
