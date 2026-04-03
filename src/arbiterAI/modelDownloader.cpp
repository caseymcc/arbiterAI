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
    // Delegate to downloadModelWithProgress with no callback or tracking name
    return downloadModelWithProgress(downloadUrl, filePathStr, fileHash, nullptr, "", "");
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
    const std::string &modelName,
    const std::string &variant)
{
    // Create tracking state
    auto downloadState=std::make_shared<ActiveDownload>();
    downloadState->modelName=modelName.empty()?filePathStr:modelName;
    downloadState->variant=variant;
    downloadState->status=DownloadStatus::Pending;
    downloadState->startTime=std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(m_downloadsMutex);
        m_activeDownloads[downloadState->modelName]=downloadState;
    }

    return std::async(std::launch::async, [this, downloadUrl, filePathStr, fileHash, progressCallback, downloadState]()
    {
        downloadState->status=DownloadStatus::InProgress;
        std::filesystem::path filePath(filePathStr);

        // Check if file already exists and is valid
        if(std::filesystem::exists(filePath))
        {
            if(fileHash&&m_fileVerifier->verifyFile(filePath.string(), *fileHash))
            {
                spdlog::info("Model already exists and is verified: {}", filePath.string());
                downloadState->status=DownloadStatus::Completed;
                downloadState->percentComplete=100.0f;
                if(progressCallback)
                {
                    progressCallback(0, 0, 100.0f);
                }
                return true;
            }
        }

        spdlog::info("Downloading model from {} to {}", downloadUrl, filePath.string());

        try
        {
            std::filesystem::create_directories(filePath.parent_path());
        }
        catch(const std::filesystem::filesystem_error &e)
        {
            spdlog::error("Failed to create directory: {}", e.what());
            downloadState->status=DownloadStatus::Failed;
            downloadState->error=e.what();
            return false;
        }

        // Stream directly to a .partial file on disk to avoid buffering the
        // entire response body in RAM.  A 20 GB model download would otherwise
        // require 20+ GB of heap, which caused OOM / heap corruption (SEGV).
        std::string partialPath=filePathStr+".partial";

        // Remove stale partial file so we start fresh
        {
            std::error_code ec;
            std::filesystem::remove(partialPath, ec);
        }

        std::ofstream outFile(partialPath, std::ios::binary|std::ios::trunc);
        if(!outFile.is_open())
        {
            spdlog::error("Failed to open partial file for writing: {}", partialPath);
            downloadState->status=DownloadStatus::Failed;
            downloadState->error="Failed to open "+partialPath;
            return false;
        }

        bool writeError=false;

        cpr::Response r=cpr::Get(
            cpr::Url{downloadUrl},
            cpr::WriteCallback([&outFile, &writeError](const std::string_view &data, intptr_t) -> bool
            {
                outFile.write(data.data(), static_cast<std::streamsize>(data.size()));
                if(!outFile.good())
                {
                    writeError=true;
                    return false; // abort transfer
                }
                return true;
            }),
            cpr::ProgressCallback([&downloadState, &progressCallback](cpr::cpr_off_t downloadTotal,
                cpr::cpr_off_t downloadNow,
                cpr::cpr_off_t uploadTotal,
                cpr::cpr_off_t uploadNow,
                intptr_t userdata) -> bool
            {
                (void)uploadTotal;
                (void)uploadNow;
                (void)userdata;

                downloadState->bytesDownloaded=downloadNow;
                downloadState->totalBytes=downloadTotal;

                float percent=0.0f;
                if(downloadTotal>0)
                {
                    percent=(static_cast<float>(downloadNow)/downloadTotal)*100.0f;
                }
                downloadState->percentComplete=percent;

                // Record speed sample
                {
                    std::lock_guard<std::mutex> lock(downloadState->speedMutex);
                    std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now();

                    downloadState->speedSamples.push_back({now, downloadNow});

                    // Keep only last 10 seconds of samples
                    std::chrono::steady_clock::time_point cutoff=now-std::chrono::seconds(10);
                    while(!downloadState->speedSamples.empty()&&downloadState->speedSamples.front().first<cutoff)
                    {
                        downloadState->speedSamples.pop_front();
                    }
                }

                if(progressCallback)
                {
                    progressCallback(downloadNow, downloadTotal, percent);
                }

                return true;
            })
        );

        outFile.close();

        if(writeError)
        {
            spdlog::error("Write error during download to {}", partialPath);
            std::error_code ec;
            std::filesystem::remove(partialPath, ec);
            downloadState->status=DownloadStatus::Failed;
            downloadState->error="Disk write error";
            return false;
        }

        if(r.status_code!=200)
        {
            spdlog::error("Failed to download model. Status code: {}", r.status_code);
            std::error_code ec;
            std::filesystem::remove(partialPath, ec);
            downloadState->status=DownloadStatus::Failed;
            downloadState->error="HTTP error: "+std::to_string(r.status_code);
            return false;
        }

        // Rename .partial -> final path atomically
        {
            std::error_code ec;
            std::filesystem::rename(partialPath, filePath, ec);
            if(ec)
            {
                spdlog::error("Failed to rename {} -> {}: {}", partialPath, filePath.string(), ec.message());
                std::filesystem::remove(partialPath, ec);
                downloadState->status=DownloadStatus::Failed;
                downloadState->error="Failed to rename partial file";
                return false;
            }
        }

        if(fileHash)
        {
            if(m_fileVerifier->verifyFile(filePath.string(), *fileHash))
            {
                spdlog::info("Model downloaded and verified successfully: {}", filePath.string());
                downloadState->status=DownloadStatus::Completed;
                downloadState->percentComplete=100.0f;
                return true;
            }
            else
            {
                spdlog::error("SHA256 verification failed for: {}", filePath.string());
                downloadState->status=DownloadStatus::Failed;
                downloadState->error="SHA256 verification failed";
                return false;
            }
        }

        spdlog::info("Model downloaded successfully: {}", filePath.string());
        downloadState->status=DownloadStatus::Completed;
        downloadState->percentComplete=100.0f;
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

DownloadProgressSnapshot ModelDownloader::buildSnapshot(const std::shared_ptr<ActiveDownload> &download)
{
    DownloadProgressSnapshot snap;

    snap.bytesDownloaded=download->bytesDownloaded.load();
    snap.totalBytes=download->totalBytes.load();
    snap.percentComplete=download->percentComplete.load();
    snap.modelName=download->modelName;
    snap.variant=download->variant;

    // Calculate speed from rolling window
    {
        std::lock_guard<std::mutex> lock(download->speedMutex);

        if(download->speedSamples.size()>=2)
        {
            const std::pair<std::chrono::steady_clock::time_point, int64_t> &oldest=download->speedSamples.front();
            const std::pair<std::chrono::steady_clock::time_point, int64_t> &newest=download->speedSamples.back();

            double elapsedSec=std::chrono::duration<double>(newest.first-oldest.first).count();
            int64_t byteDelta=newest.second-oldest.second;

            if(elapsedSec>0.0 && byteDelta>0)
            {
                double bytesPerSec=static_cast<double>(byteDelta)/elapsedSec;
                snap.speedMbps=bytesPerSec/(1024.0*1024.0);

                // ETA from remaining bytes and current speed
                int64_t remaining=snap.totalBytes-snap.bytesDownloaded;
                if(remaining>0 && bytesPerSec>0.0)
                {
                    snap.etaSeconds=static_cast<int>(static_cast<double>(remaining)/bytesPerSec);
                }
            }
        }
    }

    return snap;
}

std::optional<DownloadProgressSnapshot> ModelDownloader::getProgressSnapshot(const std::string &modelName)
{
    std::lock_guard<std::mutex> lock(m_downloadsMutex);

    auto it=m_activeDownloads.find(modelName);
    if(it==m_activeDownloads.end())
    {
        return std::nullopt;
    }

    DownloadStatus status=it->second->status.load();
    if(status!=DownloadStatus::InProgress && status!=DownloadStatus::Pending)
    {
        return std::nullopt;
    }

    return buildSnapshot(it->second);
}

std::vector<DownloadProgressSnapshot> ModelDownloader::getActiveSnapshots()
{
    std::lock_guard<std::mutex> lock(m_downloadsMutex);

    std::vector<DownloadProgressSnapshot> result;

    for(const std::pair<const std::string, std::shared_ptr<ActiveDownload>> &entry:m_activeDownloads)
    {
        DownloadStatus status=entry.second->status.load();
        if(status==DownloadStatus::InProgress || status==DownloadStatus::Pending)
        {
            result.push_back(buildSnapshot(entry.second));
        }
    }

    return result;
}

} // namespace arbiterAI