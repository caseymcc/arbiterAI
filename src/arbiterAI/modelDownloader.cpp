#include "arbiterAI/modelDownloader.h"
#include "arbiterAI/modelManager.h"
#include <picosha2.h>
#include <cpr/cpr.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

ModelDownloader::ModelDownloader(std::shared_ptr<IFileVerifier> fileVerifier) : m_fileVerifier(fileVerifier)
{
    m_cacheDir=std::filesystem::temp_directory_path()/"arbiterAI_cache";
    std::filesystem::create_directories(m_cacheDir);
    spdlog::debug("Initialized cache directory at: {}", m_cacheDir.string());
}

std::future<bool> ModelDownloader::downloadModel(const std::string &downloadUrl, const std::string &filePathStr, const std::optional<std::string> &fileHash, const std::optional<std::string> &minVersion, const std::optional<std::string> &maxVersion)
{
    return std::async(std::launch::async, [this, downloadUrl, filePathStr, fileHash, minVersion, maxVersion]()
        {
            // Check version compatibility first
            if(minVersion||maxVersion)
            {
                std::string clientVersion="1.0.0"; // TODO: Get from build config
                if ((minVersion && ModelManager::compareVersions(clientVersion, *minVersion) < 0) ||
                    (maxVersion && ModelManager::compareVersions(clientVersion, *maxVersion) > 0))
                {
                    spdlog::error("Version mismatch: client {} not compatible with model requirements (min: {}, max: {})",
                        clientVersion, minVersion?*minVersion:"none", maxVersion?*maxVersion:"none");
                    return false;
                }
            }

            std::filesystem::path filePath(filePathStr);
            if(std::filesystem::exists(filePath))
            {
                if(fileHash&&m_fileVerifier->verifyFile(filePath.string(), *fileHash))
                {
                    spdlog::info("Model already exists and is verified: {}", filePath.string());
                    return true;
                }
            }

            spdlog::info("Downloading model from {} to {}", downloadUrl, filePath.string());
            cpr::Response r=cpr::Get(cpr::Url{ downloadUrl });
            if(r.status_code!=200)
            {
                spdlog::error("Failed to download model. Status code: {}", r.status_code);
                return false;
            }

            try
            {
                std::filesystem::create_directories(filePath.parent_path());
                std::ofstream outFile(filePath, std::ios::binary);
                outFile.write(r.text.c_str(), r.text.length());
                outFile.close();
            }
            catch(const std::filesystem::filesystem_error &e)
            {
                spdlog::error("Failed to write model to file: {}. Error: {}", filePath.string(), e.what());
                return false;
            }

            if(fileHash)
            {
                if(m_fileVerifier->verifyFile(filePath.string(), *fileHash))
                {
                    spdlog::info("Model downloaded and verified successfully: {}", filePath.string());
                    return true;
                }
                else
                {
                    spdlog::error("SHA256 verification failed for: {}", filePath.string());
                    return false;
                }
            }

            return true;
        });
}


std::future<std::optional<nlohmann::json>> ModelDownloader::downloadConfigFromRepo(const std::string &repoOwner, const std::string &repoName, const std::string &configPath, const std::optional<std::string> &ref)
{
    return std::async(std::launch::async, [this, repoOwner, repoName, configPath, ref]() -> std::optional<nlohmann::json>
    {
        std::string cacheKey=fmt::format("{}/{}/{}/{}", repoOwner, repoName, ref.value_or("main"), configPath);
        if(auto cached=loadFromCache(cacheKey))
        {
            spdlog::debug("Returning cached config for: {}", cacheKey);
            return cached;
        }

        std::string apiUrl=fmt::format("https://api.github.com/repos/{}/{}/contents/{}", repoOwner, repoName, configPath);
        if(ref)
        {
            apiUrl+=fmt::format("?ref={}", *ref);
        }

        cpr::Response r=cpr::Get(cpr::Url{apiUrl},
            cpr::Header{{"Accept", "application/vnd.github.v3.raw"}});

        if(r.status_code!=200)
        {
            spdlog::error("GitHub API request failed for {}: {}", apiUrl, r.status_code);
            return std::nullopt;
        }

        auto config=parseConfigFromJSON(r.text);
        if(!config)
        {
            spdlog::error("Failed to parse GitHub config");
            return std::nullopt;
        }

        saveToCache(cacheKey, *config);
        return config;
    });
}

std::optional<nlohmann::json> ModelDownloader::parseConfigFromJSON(const std::string &jsonContent)
{
    try
    {
        return nlohmann::json::parse(jsonContent);
    }
    catch(const nlohmann::json::exception &e)
    {
        spdlog::error("JSON parsing failed: {}", e.what());
        return std::nullopt;
    }
}

std::string ModelDownloader::getCachePath(const std::string &key)
{
    std::string safeKey=key;
    std::replace(safeKey.begin(), safeKey.end(), '/', '_');
    return (m_cacheDir/safeKey).string();
}

std::optional<nlohmann::json> ModelDownloader::loadFromCache(const std::string &key)
{
    std::string cacheFile=getCachePath(key);
    if(!std::filesystem::exists(cacheFile))
    {
        return std::nullopt;
    }

    try
    {
        std::ifstream file(cacheFile);
        nlohmann::json cached;
        file>>cached;
        return cached;
    }
    catch(const std::exception &e)
    {
        spdlog::warn("Failed to load cached config {}: {}", key, e.what());
        return std::nullopt;
    }
}

void ModelDownloader::saveToCache(const std::string &key, const nlohmann::json &config)
{
    std::string cacheFile=getCachePath(key);
    try
    {
        std::ofstream file(cacheFile);
        file<<config.dump(4);
    }
    catch(const std::exception &e)
    {
        spdlog::warn("Failed to save config to cache {}: {}", key, e.what());
    }
}

std::future<bool> ModelDownloader::downloadModelWithProgress(
    const std::string &downloadUrl,
    const std::string &filePathStr,
    const std::optional<std::string> &fileHash,
    DownloadProgressCallback progressCallback,
    const std::string &modelName)
{
    // Create tracking state
    auto downloadState = std::make_shared<ActiveDownload>();
    downloadState->modelName = modelName.empty() ? filePathStr : modelName;
    downloadState->status = DownloadStatus::Pending;

    {
        std::lock_guard<std::mutex> lock(m_downloadsMutex);
        m_activeDownloads[downloadState->modelName] = downloadState;
    }

    return std::async(std::launch::async, [this, downloadUrl, filePathStr, fileHash, progressCallback, downloadState]()
    {
        downloadState->status = DownloadStatus::InProgress;
        std::filesystem::path filePath(filePathStr);

        // Check if file already exists and is valid
        if (std::filesystem::exists(filePath))
        {
            if (fileHash && m_fileVerifier->verifyFile(filePath.string(), *fileHash))
            {
                spdlog::info("Model already exists and is verified: {}", filePath.string());
                downloadState->status = DownloadStatus::Completed;
                downloadState->percentComplete = 100.0f;
                if (progressCallback)
                {
                    progressCallback(0, 0, 100.0f);
                }
                return true;
            }
        }

        // Check for partial download (resume support)
        std::string partialPath = filePathStr + ".partial";
        int64_t existingBytes = 0;
        if (std::filesystem::exists(partialPath))
        {
            existingBytes = std::filesystem::file_size(partialPath);
            spdlog::info("Found partial download with {} bytes", existingBytes);
        }

        spdlog::info("Downloading model from {} to {}", downloadUrl, filePath.string());

        try
        {
            std::filesystem::create_directories(filePath.parent_path());
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            spdlog::error("Failed to create directory: {}", e.what());
            downloadState->status = DownloadStatus::Failed;
            downloadState->error = e.what();
            return false;
        }

        // Use CPR with progress callback
        cpr::Response r = cpr::Get(
            cpr::Url{downloadUrl},
            cpr::ProgressCallback([&downloadState, &progressCallback](cpr::cpr_off_t downloadTotal, 
                                                                        cpr::cpr_off_t downloadNow,
                                                                        cpr::cpr_off_t uploadTotal,
                                                                        cpr::cpr_off_t uploadNow,
                                                                        intptr_t userdata) -> bool
            {
                downloadState->bytesDownloaded = downloadNow;
                downloadState->totalBytes = downloadTotal;
                
                float percent = 0.0f;
                if (downloadTotal > 0)
                {
                    percent = (static_cast<float>(downloadNow) / downloadTotal) * 100.0f;
                }
                downloadState->percentComplete = percent;
                
                if (progressCallback)
                {
                    progressCallback(downloadNow, downloadTotal, percent);
                }
                
                return true;  // Continue downloading
            })
        );

        if (r.status_code != 200)
        {
            spdlog::error("Failed to download model. Status code: {}", r.status_code);
            downloadState->status = DownloadStatus::Failed;
            downloadState->error = "HTTP error: " + std::to_string(r.status_code);
            return false;
        }

        try
        {
            std::ofstream outFile(filePath, std::ios::binary);
            outFile.write(r.text.c_str(), r.text.length());
            outFile.close();
            
            // Remove partial file if it exists
            if (std::filesystem::exists(partialPath))
            {
                std::filesystem::remove(partialPath);
            }
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            spdlog::error("Failed to write model to file: {}. Error: {}", filePath.string(), e.what());
            downloadState->status = DownloadStatus::Failed;
            downloadState->error = e.what();
            return false;
        }

        if (fileHash)
        {
            if (m_fileVerifier->verifyFile(filePath.string(), *fileHash))
            {
                spdlog::info("Model downloaded and verified successfully: {}", filePath.string());
                downloadState->status = DownloadStatus::Completed;
                downloadState->percentComplete = 100.0f;
                return true;
            }
            else
            {
                spdlog::error("SHA256 verification failed for: {}", filePath.string());
                downloadState->status = DownloadStatus::Failed;
                downloadState->error = "SHA256 verification failed";
                return false;
            }
        }

        downloadState->status = DownloadStatus::Completed;
        downloadState->percentComplete = 100.0f;
        return true;
    });
}

std::shared_ptr<ActiveDownload> ModelDownloader::getDownloadState(const std::string &modelName)
{
    std::lock_guard<std::mutex> lock(m_downloadsMutex);
    auto it = m_activeDownloads.find(modelName);
    if (it != m_activeDownloads.end())
    {
        return it->second;
    }
    return nullptr;
}

int64_t ModelDownloader::getPartialDownloadSize(const std::string &filePath)
{
    std::string partialPath = filePath + ".partial";
    if (std::filesystem::exists(partialPath))
    {
        return std::filesystem::file_size(partialPath);
    }
    return 0;
}

} // namespace arbiterAI