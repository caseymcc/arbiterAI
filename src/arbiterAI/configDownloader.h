#ifndef _ARBITERAI_CONFIGDOWNLOADER_H_
#define _ARBITERAI_CONFIGDOWNLOADER_H_

#include <string>
#include <filesystem>
#include <chrono>

namespace arbiterAI
{

enum class ConfigDownloadStatus {
    NotInitialized,
    Success,
    CloneFailed,
    FetchFailed,
    CheckoutFailed,
    FallbackToCache
};

class ConfigDownloader {
public:
    ConfigDownloader()=default;

    /// Initialize and sync the config repository.
    /// @param repoUrl       Git repository URL to clone/pull from.
    /// @param localPath     Local directory to clone into.
    /// @param version       Branch, tag, or commit to checkout. Default: "main".
    /// @return Status of the download operation.
    ConfigDownloadStatus initialize(
        const std::string &repoUrl,
        const std::filesystem::path &localPath,
        const std::string &version="main");

    /// Re-sync the config repository if the refresh interval has elapsed.
    /// @return Status of the refresh operation, or Success if no refresh was needed.
    ConfigDownloadStatus refresh();

    /// Set the minimum interval between remote syncs.
    /// @param interval Duration between refresh attempts.
    void setRefreshInterval(std::chrono::seconds interval);

    /// Get the local path where the config repo is stored.
    const std::filesystem::path &getLocalPath() const;

    /// Get the status of the last download/refresh operation.
    ConfigDownloadStatus getStatus() const;

    /// Check whether the local cache has any config files available.
    bool hasCachedConfig() const;

private:
    ConfigDownloadStatus cloneRepo();
    ConfigDownloadStatus fetchAndUpdate();
    ConfigDownloadStatus checkoutVersion();

    std::string m_repoUrl;
    std::filesystem::path m_localPath;
    std::string m_version="main";
    ConfigDownloadStatus m_status=ConfigDownloadStatus::NotInitialized;
    std::chrono::seconds m_refreshInterval{3600};
    std::chrono::steady_clock::time_point m_lastSync;
    bool m_initialized=false;
};

} // namespace arbiterAI

#endif//_ARBITERAI_CONFIGDOWNLOADER_H_
