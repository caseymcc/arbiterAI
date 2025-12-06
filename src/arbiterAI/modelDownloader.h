#ifndef _arbiterAI_modelDownloader_h_
#define _arbiterAI_modelDownloader_h_

#include "arbiterAI/modelManager.h"
#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/fileVerifier.h"

#include <string>
#include <future>
#include <optional>
#include <nlohmann/json.hpp>
#include <memory>
#include <functional>
#include <atomic>

namespace arbiterAI
{

/**
 * @brief Progress callback for model downloads
 * @param bytesDownloaded Current bytes downloaded
 * @param totalBytes Total file size (0 if unknown)
 * @param percentComplete Percentage complete (0-100)
 */
using DownloadProgressCallback = std::function<void(int64_t bytesDownloaded, 
                                                      int64_t totalBytes, 
                                                      float percentComplete)>;

/**
 * @struct DownloadState
 * @brief Tracks the state of an active download
 */
struct ActiveDownload
{
    std::atomic<int64_t> bytesDownloaded{0};
    std::atomic<int64_t> totalBytes{0};
    std::atomic<float> percentComplete{0.0f};
    std::atomic<DownloadStatus> status{DownloadStatus::NotStarted};
    std::string error;
    std::string modelName;
};

/**
 * @class ModelDownloader
 * @brief Downloads and verifies model files
 *
 * Features:
 * - Asynchronous downloading
 * - Progress tracking via callbacks
 * - SHA256 verification
 * - Resume capability for interrupted downloads
 * - Caching of downloaded configs
 */
class ModelDownloader
{
public:
    ModelDownloader(std::shared_ptr<IFileVerifier> fileVerifier = std::make_shared<FileVerifier>());

    /**
     * @brief Download a model file asynchronously
     * @param downloadUrl URL to download from
     * @param filePath Local path to save the file
     * @param fileHash Expected SHA256 hash (optional)
     * @param minClientVersion Minimum client version requirement
     * @param maxClientVersion Maximum client version requirement
     * @return Future that resolves to true on success
     */
    std::future<bool> downloadModel(const std::string &downloadUrl, 
                                     const std::string &filePath, 
                                     const std::optional<std::string> &fileHash, 
                                     const std::optional<std::string> &minClientVersion = std::nullopt, 
                                     const std::optional<std::string> &maxClientVersion = std::nullopt);

    /**
     * @brief Download a model with progress tracking
     * @param downloadUrl URL to download from
     * @param filePath Local path to save the file
     * @param fileHash Expected SHA256 hash (optional)
     * @param progressCallback Callback for progress updates
     * @param modelName Name for tracking in active downloads
     * @return Future that resolves to true on success
     */
    std::future<bool> downloadModelWithProgress(const std::string &downloadUrl,
                                                  const std::string &filePath,
                                                  const std::optional<std::string> &fileHash,
                                                  DownloadProgressCallback progressCallback,
                                                  const std::string &modelName = "");

    /**
     * @brief Get the current download state for a model
     * @param modelName Name of the model
     * @return Active download state, or nullptr if not downloading
     */
    std::shared_ptr<ActiveDownload> getDownloadState(const std::string &modelName);

    /**
     * @brief Check if a download can be resumed
     * @param filePath Path to the partial file
     * @return Number of bytes already downloaded, or 0 if no partial file
     */
    int64_t getPartialDownloadSize(const std::string &filePath);

    // GitHub API functions
    std::future<std::optional<nlohmann::json>> downloadConfigFromRepo(const std::string &repoOwner, 
                                                                        const std::string &repoName, 
                                                                        const std::string &configPath, 
                                                                        const std::optional<std::string> &ref = std::nullopt);
    std::optional<nlohmann::json> parseConfigFromJSON(const std::string &jsonContent);

private:
    std::string getCachePath(const std::string &key);
    std::optional<nlohmann::json> loadFromCache(const std::string &key);
    void saveToCache(const std::string &key, const nlohmann::json &config);

    std::filesystem::path m_cacheDir;
    std::shared_ptr<IFileVerifier> m_fileVerifier;
    
    // Track active downloads
    std::map<std::string, std::shared_ptr<ActiveDownload>> m_activeDownloads;
    std::mutex m_downloadsMutex;
};

} // namespace arbiterAI

#endif // _arbiterAI_modelDownloader_h_