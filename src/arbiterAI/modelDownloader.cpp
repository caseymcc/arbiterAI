#include "arbiterAI/modelDownloader.h"
#include "arbiterAI/modelManager.h"
#include <picosha2.h>
#include <cpr/cpr.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <thread>
#include <vector>

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

        // Stream to a .partial file on disk rather than buffering the response
        // body in RAM — a 20 GB model would otherwise need 20+ GB of heap.
        //
        // Large files are fetched over several ranged connections in parallel:
        // hosts commonly throttle per-connection, so one stream can be an order
        // of magnitude slower than the link actually supports.
        std::string partialPath=filePathStr+".partial";

        // Remove stale partial file so we start fresh
        {
            std::error_code ec;
            std::filesystem::remove(partialPath, ec);
        }

        int64_t remoteSize=0;
        bool acceptsRanges=false;
        bool probed=probeDownload(downloadUrl, remoteSize, acceptsRanges);

        int connections=1;
        if(probed&&acceptsRanges&&remoteSize>=kParallelThresholdBytes&&m_maxConnectionsPerDownload>1)
        {
            connections=m_maxConnectionsPerDownload;
            // Never split into pieces so small the per-request overhead dominates.
            int64_t maxUseful=remoteSize/(8*1024*1024);
            if(maxUseful<1) maxUseful=1;
            if(connections>maxUseful) connections=static_cast<int>(maxUseful);
        }
        downloadState->connections=connections;

        if(probed&&remoteSize>0)
        {
            downloadState->totalBytes=remoteSize;
        }

        if(connections>1)
        {
            spdlog::info("Downloading {} ({} MB) over {} parallel connections",
                filePathStr, remoteSize/(1024*1024), connections);

            // Size the file up front so each connection can write straight to
            // its own offset without coordinating with the others.
            {
                std::ofstream create(partialPath, std::ios::binary|std::ios::trunc);
                if(!create.is_open())
                {
                    spdlog::error("Failed to create partial file: {}", partialPath);
                    downloadState->status=DownloadStatus::Failed;
                    downloadState->error="Failed to open "+partialPath;
                    return false;
                }
            }
            std::error_code ec;
            std::filesystem::resize_file(partialPath, static_cast<uintmax_t>(remoteSize), ec);
            if(ec)
            {
                spdlog::error("Failed to preallocate {} bytes for {}: {}",
                    remoteSize, partialPath, ec.message());
                std::filesystem::remove(partialPath, ec);
                downloadState->status=DownloadStatus::Failed;
                downloadState->error="Failed to preallocate partial file";
                return false;
            }

            std::atomic<int64_t> totalDownloaded{0};
            std::atomic<bool> anyFailed{false};
            std::mutex errorMutex;
            std::string firstError;

            int64_t chunkSize=(remoteSize+connections-1)/connections;
            std::vector<std::thread> workers;
            workers.reserve(static_cast<size_t>(connections));

            for(int i=0; i<connections; ++i)
            {
                int64_t start=static_cast<int64_t>(i)*chunkSize;
                if(start>=remoteSize) break;
                int64_t end=std::min(start+chunkSize-1, remoteSize-1);

                workers.emplace_back([this, &downloadUrl, &partialPath, start, end, &downloadState,
                    &totalDownloaded, &progressCallback, &anyFailed, &errorMutex, &firstError]()
                {
                    std::string error;
                    if(!downloadRange(downloadUrl, partialPath, start, end,
                        downloadState, totalDownloaded, progressCallback, error))
                    {
                        anyFailed=true;
                        std::lock_guard<std::mutex> lock(errorMutex);
                        if(firstError.empty()) firstError=error;
                    }
                });
            }

            for(std::thread &worker:workers)
            {
                worker.join();
            }

            if(anyFailed||downloadState->cancelled.load())
            {
                std::error_code removeEc;
                std::filesystem::remove(partialPath, removeEc);

                if(downloadState->cancelled.load())
                {
                    spdlog::info("Download cancelled: {}", filePathStr);
                    downloadState->status=DownloadStatus::Cancelled;
                    downloadState->error="Download cancelled";
                }
                else
                {
                    spdlog::error("Parallel download failed for {}: {}", downloadUrl, firstError);
                    downloadState->status=DownloadStatus::Failed;
                    downloadState->error=firstError.empty()?"Parallel download failed":firstError;
                }
                return false;
            }

            int64_t written=static_cast<int64_t>(std::filesystem::file_size(partialPath, ec));
            if(ec||written!=remoteSize)
            {
                spdlog::error("Short parallel download for {}: {} of {} bytes", filePathStr, written, remoteSize);
                std::filesystem::remove(partialPath, ec);
                downloadState->status=DownloadStatus::Failed;
                downloadState->error="Incomplete download";
                return false;
            }
        }
        else
        {
            std::ofstream outFile(partialPath, std::ios::binary|std::ios::trunc);
            if(!outFile.is_open())
            {
                spdlog::error("Failed to open partial file for writing: {}", partialPath);
                downloadState->status=DownloadStatus::Failed;
                downloadState->error="Failed to open "+partialPath;
                return false;
            }

            bool writeError=false;
            int64_t received=0;

            cpr::Response r=cpr::Get(
                cpr::Url{downloadUrl},
                cpr::Redirect{50, true, true, cpr::PostRedirectFlags::POST_ALL},
                cpr::Header{{"User-Agent", "arbiterAI/1.0"}},
                cpr::ConnectTimeout{std::chrono::seconds(30)},
                cpr::LowSpeed{1024, std::chrono::seconds(60)},
                cpr::WriteCallback([this, &outFile, &writeError, &received, &downloadState,
                    &progressCallback](const std::string_view &data, intptr_t) -> bool
                {
                    if(downloadState->cancelled.load()) return false;

                    outFile.write(data.data(), static_cast<std::streamsize>(data.size()));
                    if(!outFile.good())
                    {
                        writeError=true;
                        return false; // abort transfer
                    }

                    received+=static_cast<int64_t>(data.size());
                    recordProgress(downloadState, received, downloadState->totalBytes.load(), progressCallback);
                    return true;
                }),
                cpr::ProgressCallback([&downloadState](cpr::cpr_off_t downloadTotal,
                    cpr::cpr_off_t downloadNow,
                    cpr::cpr_off_t uploadTotal,
                    cpr::cpr_off_t uploadNow,
                    intptr_t userdata) -> bool
                {
                    (void)downloadNow;
                    (void)uploadTotal;
                    (void)uploadNow;
                    (void)userdata;

                    if(downloadTotal>0) downloadState->totalBytes=downloadTotal;
                    return !downloadState->cancelled.load();
                })
            );

            outFile.close();

            if(downloadState->cancelled.load())
            {
                spdlog::info("Download cancelled: {}", filePathStr);
                std::error_code ec;
                std::filesystem::remove(partialPath, ec);
                downloadState->status=DownloadStatus::Cancelled;
                downloadState->error="Download cancelled";
                return false;
            }

            if(r.error)
            {
                spdlog::error("Download transport error for {}: [curl {}] {} (http {}, {} bytes received)",
                    downloadUrl, static_cast<int>(r.error.code), r.error.message,
                    r.status_code, downloadState->bytesDownloaded.load());
                std::error_code ec;
                std::filesystem::remove(partialPath, ec);
                downloadState->status=DownloadStatus::Failed;
                downloadState->error="Transport error: "+r.error.message;
                return false;
            }

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

void ModelDownloader::setMaxConnectionsPerDownload(int connections)
{
    m_maxConnectionsPerDownload=connections<1?1:connections;
}

void ModelDownloader::recordProgress(const std::shared_ptr<ActiveDownload> &download,
    int64_t bytesDownloaded, int64_t totalBytes,
    const DownloadProgressCallback &progressCallback)
{
    download->bytesDownloaded=bytesDownloaded;
    if(totalBytes>0) download->totalBytes=totalBytes;

    int64_t total=download->totalBytes.load();
    float percent=total>0?(static_cast<float>(bytesDownloaded)/total)*100.0f:0.0f;
    download->percentComplete=percent;

    // Sampling is rate-limited: write callbacks fire every few KB, which would
    // otherwise push thousands of samples per second into the speed window.
    bool sample=false;
    {
        std::lock_guard<std::mutex> lock(download->speedMutex);
        std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now();

        if(download->lastSampleTime.time_since_epoch().count()==0||
            now-download->lastSampleTime>=std::chrono::milliseconds(250))
        {
            download->lastSampleTime=now;
            download->speedSamples.push_back({now, bytesDownloaded});
            sample=true;

            std::chrono::steady_clock::time_point cutoff=now-std::chrono::seconds(10);
            while(!download->speedSamples.empty()&&download->speedSamples.front().first<cutoff)
            {
                download->speedSamples.pop_front();
            }
        }
    }

    if(sample&&progressCallback)
    {
        progressCallback(bytesDownloaded, total, percent);
    }
}

bool ModelDownloader::probeDownload(const std::string &url, int64_t &sizeOut, bool &acceptsRangesOut)
{
    sizeOut=0;
    acceptsRangesOut=false;

    cpr::Response r=cpr::Head(
        cpr::Url{url},
        cpr::Redirect{50, true, true, cpr::PostRedirectFlags::POST_ALL},
        cpr::Header{{"User-Agent", "arbiterAI/1.0"}},
        cpr::ConnectTimeout{std::chrono::seconds(30)},
        cpr::Timeout{std::chrono::seconds(60)});

    if(r.error||r.status_code!=200)
    {
        spdlog::debug("HEAD probe failed for {} (http {}), falling back to a single stream",
            url, r.status_code);
        return false;
    }

    auto lengthIt=r.header.find("Content-Length");
    if(lengthIt==r.header.end())
    {
        return false;
    }

    try
    {
        sizeOut=std::stoll(lengthIt->second);
    }
    catch(const std::exception &)
    {
        return false;
    }

    auto rangesIt=r.header.find("Accept-Ranges");
    if(rangesIt!=r.header.end())
    {
        std::string value=rangesIt->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        acceptsRangesOut=(value.find("bytes")!=std::string::npos);
    }

    return sizeOut>0;
}

bool ModelDownloader::downloadRange(const std::string &url, const std::string &filePath,
    int64_t start, int64_t end,
    const std::shared_ptr<ActiveDownload> &download,
    std::atomic<int64_t> &totalDownloaded,
    const DownloadProgressCallback &progressCallback,
    std::string &errorOut)
{
    std::ofstream out(filePath, std::ios::binary|std::ios::in|std::ios::out);
    if(!out.is_open())
    {
        errorOut="Failed to open "+filePath+" for range write";
        return false;
    }
    out.seekp(static_cast<std::streamoff>(start));
    if(!out.good())
    {
        errorOut="Failed to seek to offset "+std::to_string(start);
        return false;
    }

    const int64_t expected=end-start+1;
    int64_t received=0;
    bool writeError=false;

    std::string range=std::to_string(start)+"-"+std::to_string(end);

    cpr::Response r=cpr::Get(
        cpr::Url{url},
        cpr::Redirect{50, true, true, cpr::PostRedirectFlags::POST_ALL},
        cpr::Header{{"User-Agent", "arbiterAI/1.0"}, {"Range", "bytes="+range}},
        cpr::ConnectTimeout{std::chrono::seconds(30)},
        cpr::LowSpeed{1024, std::chrono::seconds(60)},
        cpr::WriteCallback([this, &out, &writeError, &received, &totalDownloaded, &download,
            &progressCallback](const std::string_view &data, intptr_t) -> bool
        {
            if(download->cancelled.load()) return false;

            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            if(!out.good())
            {
                writeError=true;
                return false;
            }

            received+=static_cast<int64_t>(data.size());
            int64_t soFar=totalDownloaded.fetch_add(static_cast<int64_t>(data.size()))
                +static_cast<int64_t>(data.size());
            recordProgress(download, soFar, download->totalBytes.load(), progressCallback);
            return true;
        }));

    out.close();

    if(download->cancelled.load())
    {
        errorOut="cancelled";
        return false;
    }
    if(writeError)
    {
        errorOut="Disk write error";
        return false;
    }
    if(r.error)
    {
        errorOut="Transport error on range "+range+": "+r.error.message;
        return false;
    }
    // 206 is the expected reply; a 200 means the server ignored the range.
    if(r.status_code!=206&&r.status_code!=200)
    {
        errorOut="HTTP "+std::to_string(r.status_code)+" on range "+range;
        return false;
    }
    if(received!=expected)
    {
        errorOut="Short range "+range+": got "+std::to_string(received)
            +" of "+std::to_string(expected)+" bytes";
        return false;
    }

    return true;
}

bool ModelDownloader::cancelDownload(const std::string &modelName)
{
    std::shared_ptr<ActiveDownload> download;
    {
        std::lock_guard<std::mutex> lock(m_downloadsMutex);
        auto it=m_activeDownloads.find(modelName);
        if(it==m_activeDownloads.end())
        {
            return false;
        }
        download=it->second;
    }

    DownloadStatus status=download->status.load();
    if(status!=DownloadStatus::Pending&&status!=DownloadStatus::InProgress)
    {
        return false;
    }

    spdlog::info("Cancelling download for '{}'", modelName);
    download->cancelled=true;
    return true;
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