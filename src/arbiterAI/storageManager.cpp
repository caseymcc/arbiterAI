#include "arbiterAI/storageManager.h"
#include "arbiterAI/modelRuntime.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

namespace arbiterAI
{

namespace
{

std::string timePointToIso(const std::chrono::system_clock::time_point &tp)
{
    std::time_t t=std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

std::chrono::system_clock::time_point isoToTimePoint(const std::string &iso)
{
    if(iso.empty())
    {
        return std::chrono::system_clock::now();
    }

    std::tm tm{};
    strptime(iso.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    std::time_t t=timegm(&tm);
    return std::chrono::system_clock::from_time_t(t);
}

std::string formatBytes(int64_t bytes)
{
    if(bytes>=1073741824)
        return std::to_string(bytes/1073741824)+"."+std::to_string((bytes%1073741824)*10/1073741824)+" GB";
    if(bytes>=1048576)
        return std::to_string(bytes/1048576)+"."+std::to_string((bytes%1048576)*10/1048576)+" MB";
    return std::to_string(bytes)+" B";
}

} // anonymous namespace

StorageManager &StorageManager::instance()
{
    static StorageManager mgr;
    return mgr;
}

void StorageManager::reset()
{
    StorageManager &mgr=instance();

    mgr.shutdown();

    std::lock_guard<std::mutex> lock(mgr.m_mutex);
    mgr.m_entries.clear();
    mgr.m_modelsDir.clear();
    mgr.m_storageLimitBytes=0;
    mgr.m_initialized=false;
    mgr.m_dirty=false;
    mgr.m_cleanupPolicy=CleanupPolicy{};
}

StorageManager::~StorageManager()
{
    shutdown();
}

void StorageManager::initialize(const std::filesystem::path &modelsDir)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_modelsDir=modelsDir;

    if(!std::filesystem::exists(m_modelsDir))
    {
        std::filesystem::create_directories(m_modelsDir);
    }

    loadUsageData();
    scanModelsDirectory();
    m_initialized=true;

    spdlog::info("StorageManager initialized: modelsDir={}", m_modelsDir.string());

    startBackgroundTimer();
}

void StorageManager::shutdown()
{
    m_timerRunning=false;
    if(m_timerThread.joinable())
    {
        m_timerThread.join();
    }

    flush();
}

void StorageManager::setStorageLimit(int64_t limitBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_storageLimitBytes=limitBytes;
    m_dirty=true;
}

int64_t StorageManager::getStorageLimit() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_storageLimitBytes;
}

StorageInfo StorageManager::getStorageInfo() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    StorageInfo info;
    info.modelsDirectory=m_modelsDir;
    info.storageLimitBytes=m_storageLimitBytes;
    info.cleanupEnabled=m_cleanupPolicy.enabled;

    // Query disk space
    if(!m_modelsDir.empty()&&std::filesystem::exists(m_modelsDir))
    {
        std::error_code ec;
        std::filesystem::space_info si=std::filesystem::space(m_modelsDir, ec);

        if(!ec)
        {
            info.totalDiskBytes=static_cast<int64_t>(si.capacity);
            info.freeDiskBytes=static_cast<int64_t>(si.available);
        }
    }

    // Sum model file sizes
    int64_t usedBytes=0;
    for(const ModelFileEntry &entry:m_entries)
    {
        usedBytes+=entry.fileSizeBytes;
    }
    info.usedByModelsBytes=usedBytes;
    info.modelCount=static_cast<int>(m_entries.size());

    // Calculate available space
    if(m_storageLimitBytes>0)
    {
        int64_t limitRemaining=m_storageLimitBytes-usedBytes;
        if(limitRemaining<0) limitRemaining=0;
        info.availableForModelsBytes=std::min(info.freeDiskBytes, limitRemaining);
    }
    else
    {
        info.availableForModelsBytes=info.freeDiskBytes;
    }

    return info;
}

bool StorageManager::canDownload(int64_t fileSizeBytes) const
{
    StorageInfo info=getStorageInfo();
    return info.availableForModelsBytes>=fileSizeBytes;
}

std::vector<DownloadedModelFile> StorageManager::getDownloadedModels() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<DownloadedModelFile> result;
    result.reserve(m_entries.size());

    for(const ModelFileEntry &entry:m_entries)
    {
        result.push_back(entryToPublic(entry));
    }

    return result;
}

void StorageManager::registerDownload(const std::string &modelName,
    const std::string &variant,
    const std::string &filename,
    int64_t fileSizeBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if entry already exists
    ModelFileEntry *existing=findEntry(modelName, variant);
    if(existing)
    {
        existing->filename=filename;
        existing->fileSizeBytes=fileSizeBytes;
        existing->downloadedAt=std::chrono::system_clock::now();
        m_dirty=true;
        return;
    }

    ModelFileEntry entry;
    entry.modelName=modelName;
    entry.variant=variant;
    entry.filename=filename;
    entry.fileSizeBytes=fileSizeBytes;
    entry.downloadedAt=std::chrono::system_clock::now();
    entry.lastUsedAt=std::chrono::system_clock::now();
    entry.usageCount=0;
    entry.hotReady=false;
    entry.isProtected=false;

    m_entries.push_back(entry);
    m_dirty=true;

    spdlog::info("StorageManager: registered download {} variant {} ({})",
        modelName, variant, formatBytes(fileSizeBytes));
}

void StorageManager::recordUsage(const std::string &modelName, const std::string &variant)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ModelFileEntry *entry=findEntry(modelName, variant);
    if(entry)
    {
        entry->lastUsedAt=std::chrono::system_clock::now();
        entry->usageCount++;
        m_dirty=true;
    }
}

bool StorageManager::deleteModelFile(const std::string &modelName, const std::string &variant,
    int64_t &freedBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    freedBytes=0;

    if(variant.empty())
    {
        // Delete all variants of this model
        std::vector<size_t> toRemove;

        for(size_t i=0; i<m_entries.size(); ++i)
        {
            if(m_entries[i].modelName==modelName)
            {
                if(m_entries[i].hotReady||m_entries[i].isProtected)
                {
                    spdlog::warn("StorageManager: cannot delete {} variant {} — guarded (hotReady={}, protected={})",
                        modelName, m_entries[i].variant, m_entries[i].hotReady, m_entries[i].isProtected);
                    return false;
                }
                toRemove.push_back(i);
            }
        }

        if(toRemove.empty())
        {
            return false;
        }

        // Delete files and remove entries (in reverse order to preserve indices)
        for(auto it=toRemove.rbegin(); it!=toRemove.rend(); ++it)
        {
            std::filesystem::path filePath=m_modelsDir/m_entries[*it].filename;

            std::error_code ec;
            if(std::filesystem::exists(filePath, ec))
            {
                std::filesystem::remove(filePath, ec);
                if(ec)
                {
                    spdlog::error("StorageManager: failed to delete file {}: {}", filePath.string(), ec.message());
                    return false;
                }
            }

            freedBytes+=m_entries[*it].fileSizeBytes;
            spdlog::info("StorageManager: deleted {} variant {} ({})",
                m_entries[*it].modelName, m_entries[*it].variant, formatBytes(m_entries[*it].fileSizeBytes));
            m_entries.erase(m_entries.begin()+static_cast<ptrdiff_t>(*it));
        }
    }
    else
    {
        // Delete a specific variant
        for(auto it=m_entries.begin(); it!=m_entries.end(); ++it)
        {
            if(it->modelName==modelName&&it->variant==variant)
            {
                if(it->hotReady||it->isProtected)
                {
                    spdlog::warn("StorageManager: cannot delete {} variant {} — guarded", modelName, variant);
                    return false;
                }

                std::filesystem::path filePath=m_modelsDir/it->filename;

                std::error_code ec;
                if(std::filesystem::exists(filePath, ec))
                {
                    std::filesystem::remove(filePath, ec);
                    if(ec)
                    {
                        spdlog::error("StorageManager: failed to delete file {}: {}", filePath.string(), ec.message());
                        return false;
                    }
                }

                freedBytes=it->fileSizeBytes;
                spdlog::info("StorageManager: deleted {} variant {} ({})",
                    modelName, variant, formatBytes(it->fileSizeBytes));
                m_entries.erase(it);
                break;
            }
        }
    }

    if(freedBytes>0)
    {
        m_dirty=true;
        saveUsageData();
    }

    return freedBytes>0;
}

bool StorageManager::setHotReady(const std::string &modelName, const std::string &variant, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ModelFileEntry *entry=findEntry(modelName, variant);
    if(!entry)
    {
        return false;
    }

    entry->hotReady=enabled;
    m_dirty=true;
    return true;
}

bool StorageManager::setProtected(const std::string &modelName, const std::string &variant, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ModelFileEntry *entry=findEntry(modelName, variant);
    if(!entry)
    {
        return false;
    }

    entry->isProtected=enabled;
    m_dirty=true;
    return true;
}

std::vector<DownloadedModelFile> StorageManager::getModelStats(const std::string &modelName) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<DownloadedModelFile> result;
    for(const ModelFileEntry &entry:m_entries)
    {
        if(entry.modelName==modelName)
        {
            result.push_back(entryToPublic(entry));
        }
    }
    return result;
}

std::optional<DownloadedModelFile> StorageManager::getVariantStats(
    const std::string &modelName, const std::string &variant) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const ModelFileEntry *entry=findEntry(modelName, variant);
    if(!entry)
    {
        return std::nullopt;
    }
    return entryToPublic(*entry);
}

bool StorageManager::isGuarded(const std::string &modelName, const std::string &variant) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const ModelFileEntry *entry=findEntry(modelName, variant);
    if(!entry)
    {
        return false;
    }
    return entry->hotReady||entry->isProtected;
}

void StorageManager::flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if(!m_initialized||!m_dirty)
    {
        return;
    }

    saveUsageData();
    m_dirty=false;
}

void StorageManager::scanModelsDirectory()
{
    // NOTE: caller must hold m_mutex

    if(m_modelsDir.empty()||!std::filesystem::exists(m_modelsDir))
    {
        return;
    }

    std::error_code ec;

    for(const std::filesystem::directory_entry &dirEntry:std::filesystem::directory_iterator(m_modelsDir, ec))
    {
        if(!dirEntry.is_regular_file())
        {
            continue;
        }

        std::string filename=dirEntry.path().filename().string();

        // Only track GGUF files
        if(filename.size()<5||filename.substr(filename.size()-5)!=".gguf")
        {
            continue;
        }

        // Check if already tracked
        bool found=false;
        for(const ModelFileEntry &entry:m_entries)
        {
            if(entry.filename==filename)
            {
                found=true;
                break;
            }
        }

        if(!found)
        {
            // New file discovered on disk — add with unknown model/variant
            ModelFileEntry entry;
            entry.filename=filename;
            entry.fileSizeBytes=static_cast<int64_t>(dirEntry.file_size(ec));
            entry.downloadedAt=std::chrono::system_clock::now();
            entry.lastUsedAt=std::chrono::system_clock::now();

            // Try to infer model name and variant from filename
            // Typical pattern: ModelName-VariantName.gguf
            std::string stem=filename.substr(0, filename.size()-5);
            size_t lastDash=stem.rfind('-');
            if(lastDash!=std::string::npos&&lastDash>0)
            {
                entry.modelName=stem.substr(0, lastDash);
                entry.variant=stem.substr(lastDash+1);
            }
            else
            {
                entry.modelName=stem;
                entry.variant="default";
            }

            m_entries.push_back(entry);
            m_dirty=true;

            spdlog::info("StorageManager: discovered untracked GGUF file: {} ({})",
                filename, formatBytes(entry.fileSizeBytes));
        }
    }

    // Remove entries for files that no longer exist on disk
    auto removeIt=std::remove_if(m_entries.begin(), m_entries.end(),
        [this](const ModelFileEntry &entry)
        {
            std::filesystem::path filePath=m_modelsDir/entry.filename;
            std::error_code ec;
            bool exists=std::filesystem::exists(filePath, ec);
            if(!exists)
            {
                spdlog::info("StorageManager: removing entry for missing file: {}", entry.filename);
            }
            return !exists;
        });

    if(removeIt!=m_entries.end())
    {
        m_entries.erase(removeIt, m_entries.end());
        m_dirty=true;
    }
}

// ========== Cleanup ==========

void StorageManager::setCleanupPolicy(const CleanupPolicy &policy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cleanupPolicy=policy;
}

CleanupPolicy StorageManager::getCleanupPolicy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cleanupPolicy;
}

std::vector<CleanupCandidate> StorageManager::previewCleanup() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return collectCleanupCandidates();
}

int64_t StorageManager::runCleanup()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if(!m_cleanupPolicy.enabled)
    {
        return 0;
    }

    std::vector<CleanupCandidate> candidates=collectCleanupCandidates();

    if(candidates.empty())
    {
        return 0;
    }

    int64_t totalFreed=0;

    for(const CleanupCandidate &candidate:candidates)
    {
        // Delete the file
        std::filesystem::path filePath=m_modelsDir/candidate.filename;

        std::error_code ec;
        if(std::filesystem::exists(filePath, ec))
        {
            std::filesystem::remove(filePath, ec);
            if(ec)
            {
                spdlog::error("StorageManager cleanup: failed to delete {}: {}", filePath.string(), ec.message());
                continue;
            }
        }

        // Remove from entries
        for(auto it=m_entries.begin(); it!=m_entries.end(); ++it)
        {
            if(it->modelName==candidate.modelName&&it->variant==candidate.variant)
            {
                totalFreed+=it->fileSizeBytes;
                spdlog::info("StorageManager cleanup: deleted {} variant {} ({})",
                    it->modelName, it->variant, formatBytes(it->fileSizeBytes));
                m_entries.erase(it);
                break;
            }
        }
    }

    if(totalFreed>0)
    {
        m_dirty=true;
        saveUsageData();
    }

    spdlog::info("StorageManager cleanup: freed {}", formatBytes(totalFreed));
    return totalFreed;
}

// ========== Private Methods ==========

StorageManager::ModelFileEntry *StorageManager::findEntry(
    const std::string &modelName, const std::string &variant)
{
    for(ModelFileEntry &entry:m_entries)
    {
        if(entry.modelName==modelName&&entry.variant==variant)
        {
            return &entry;
        }
    }
    return nullptr;
}

const StorageManager::ModelFileEntry *StorageManager::findEntry(
    const std::string &modelName, const std::string &variant) const
{
    for(const ModelFileEntry &entry:m_entries)
    {
        if(entry.modelName==modelName&&entry.variant==variant)
        {
            return &entry;
        }
    }
    return nullptr;
}

void StorageManager::loadUsageData()
{
    // NOTE: caller must hold m_mutex

    std::filesystem::path usagePath=m_modelsDir/"model_usage.json";

    if(!std::filesystem::exists(usagePath))
    {
        return;
    }

    try
    {
        std::ifstream file(usagePath);
        if(!file.is_open())
        {
            return;
        }

        nlohmann::json data;
        file>>data;

        if(data.contains("storage_limit_bytes"))
        {
            m_storageLimitBytes=data["storage_limit_bytes"].get<int64_t>();
        }

        if(data.contains("cleanup_policy"))
        {
            const nlohmann::json &cp=data["cleanup_policy"];
            if(cp.contains("enabled")) m_cleanupPolicy.enabled=cp["enabled"].get<bool>();
            if(cp.contains("max_age_hours")) m_cleanupPolicy.maxAge=std::chrono::hours(cp["max_age_hours"].get<int>());
            if(cp.contains("check_interval_hours")) m_cleanupPolicy.checkInterval=std::chrono::hours(cp["check_interval_hours"].get<int>());
            if(cp.contains("target_free_percent")) m_cleanupPolicy.targetFreePercent=cp["target_free_percent"].get<double>();
        }

        if(data.contains("models")&&data["models"].is_array())
        {
            for(const nlohmann::json &m:data["models"])
            {
                ModelFileEntry entry;
                entry.modelName=m.value("model", "");
                entry.variant=m.value("variant", "");
                entry.filename=m.value("filename", "");
                entry.fileSizeBytes=m.value("file_size_bytes", int64_t(0));
                entry.downloadedAt=isoToTimePoint(m.value("downloaded_at", ""));
                entry.lastUsedAt=isoToTimePoint(m.value("last_used_at", ""));
                entry.usageCount=m.value("usage_count", 0);
                entry.hotReady=m.value("hot_ready", false);
                entry.isProtected=m.value("protected", false);

                if(!entry.filename.empty())
                {
                    m_entries.push_back(entry);
                }
            }
        }

        spdlog::info("StorageManager: loaded {} entries from {}", m_entries.size(), usagePath.string());
    }
    catch(const std::exception &e)
    {
        spdlog::warn("StorageManager: failed to load usage data: {}", e.what());
    }
}

void StorageManager::saveUsageData() const
{
    // NOTE: caller must hold m_mutex

    if(m_modelsDir.empty())
    {
        return;
    }

    std::filesystem::path usagePath=m_modelsDir/"model_usage.json";

    nlohmann::json data;
    data["version"]=1;
    data["storage_limit_bytes"]=m_storageLimitBytes;

    nlohmann::json cleanupJson;
    cleanupJson["enabled"]=m_cleanupPolicy.enabled;
    cleanupJson["max_age_hours"]=m_cleanupPolicy.maxAge.count();
    cleanupJson["check_interval_hours"]=m_cleanupPolicy.checkInterval.count();
    cleanupJson["target_free_percent"]=m_cleanupPolicy.targetFreePercent;
    data["cleanup_policy"]=cleanupJson;

    nlohmann::json models=nlohmann::json::array();
    for(const ModelFileEntry &entry:m_entries)
    {
        nlohmann::json m;
        m["model"]=entry.modelName;
        m["variant"]=entry.variant;
        m["filename"]=entry.filename;
        m["file_size_bytes"]=entry.fileSizeBytes;
        m["downloaded_at"]=timePointToIso(entry.downloadedAt);
        m["last_used_at"]=timePointToIso(entry.lastUsedAt);
        m["usage_count"]=entry.usageCount;
        m["hot_ready"]=entry.hotReady;
        m["protected"]=entry.isProtected;
        models.push_back(m);
    }
    data["models"]=models;

    try
    {
        std::ofstream file(usagePath);
        file<<data.dump(4);
        file.close();
    }
    catch(const std::exception &e)
    {
        spdlog::error("StorageManager: failed to save usage data: {}", e.what());
    }
}

std::vector<CleanupCandidate> StorageManager::collectCleanupCandidates() const
{
    // NOTE: caller must hold m_mutex

    std::vector<CleanupCandidate> candidates;

    auto now=std::chrono::system_clock::now();

    for(const ModelFileEntry &entry:m_entries)
    {
        // Skip guarded entries
        if(m_cleanupPolicy.respectHotReady&&entry.hotReady) continue;
        if(m_cleanupPolicy.respectProtected&&entry.isProtected) continue;

        // Skip entries that are currently Loaded or Ready in ModelRuntime
        // Note: we don't hold ModelRuntime's lock here, so this is a best-effort check
        std::optional<LoadedModel> runtimeState=ModelRuntime::instance().getModelState(entry.modelName);
        if(runtimeState.has_value())
        {
            ModelState state=runtimeState->state;
            if(state==ModelState::Loaded||state==ModelState::Ready)
            {
                continue;
            }
        }

        // Check staleness
        auto age=std::chrono::duration_cast<std::chrono::hours>(now-entry.lastUsedAt);
        if(age<m_cleanupPolicy.maxAge)
        {
            continue;
        }

        CleanupCandidate candidate;
        candidate.modelName=entry.modelName;
        candidate.variant=entry.variant;
        candidate.filename=entry.filename;
        candidate.fileSizeBytes=entry.fileSizeBytes;
        candidate.lastUsedAt=entry.lastUsedAt;
        candidate.usageCount=entry.usageCount;
        candidates.push_back(candidate);
    }

    // Sort by lastUsedAt ascending (oldest first)
    std::sort(candidates.begin(), candidates.end(),
        [](const CleanupCandidate &a, const CleanupCandidate &b)
        {
            return a.lastUsedAt<b.lastUsedAt;
        });

    return candidates;
}

DownloadedModelFile StorageManager::entryToPublic(const ModelFileEntry &entry) const
{
    DownloadedModelFile f;
    f.modelName=entry.modelName;
    f.variant=entry.variant;
    f.filename=entry.filename;
    f.filePath=m_modelsDir/entry.filename;
    f.fileSizeBytes=entry.fileSizeBytes;
    f.downloadedAt=entry.downloadedAt;
    f.lastUsedAt=entry.lastUsedAt;
    f.usageCount=entry.usageCount;
    f.hotReady=entry.hotReady;
    f.isProtected=entry.isProtected;

    // Cross-reference runtime state
    std::optional<LoadedModel> runtimeState=ModelRuntime::instance().getModelState(entry.modelName);
    if(runtimeState.has_value()&&runtimeState->variant==entry.variant)
    {
        switch(runtimeState->state)
        {
            case ModelState::Loaded:      f.runtimeState="Loaded"; break;
            case ModelState::Ready:       f.runtimeState="Ready"; break;
            case ModelState::Downloading: f.runtimeState="Downloading"; break;
            case ModelState::Unloading:   f.runtimeState="Unloading"; break;
            default:                      f.runtimeState="Unloaded"; break;
        }
    }
    else
    {
        f.runtimeState="Unloaded";
    }

    return f;
}

void StorageManager::startBackgroundTimer()
{
    if(m_timerRunning)
    {
        return;
    }

    m_timerRunning=true;
    m_timerThread=std::thread([this]()
    {
        // Flush every 5 minutes, cleanup on the cleanup interval
        constexpr int flushIntervalSeconds=300; // 5 minutes
        int elapsedSeconds=0;

        while(m_timerRunning)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            elapsedSeconds++;

            if(!m_timerRunning)
            {
                break;
            }

            // Periodic flush
            if(elapsedSeconds%flushIntervalSeconds==0)
            {
                flush();
            }

            // Periodic cleanup
            int cleanupIntervalSeconds=0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cleanupIntervalSeconds=static_cast<int>(m_cleanupPolicy.checkInterval.count()*3600);
            }

            if(cleanupIntervalSeconds>0&&elapsedSeconds%cleanupIntervalSeconds==0)
            {
                runCleanup();
            }
        }
    });
}

} // namespace arbiterAI
