#include "arbiterAI/configDownloader.h"

#include <git2.h>
#include <spdlog/spdlog.h>

namespace arbiterAI
{

ConfigDownloadStatus ConfigDownloader::initialize(
    const std::string &repoUrl,
    const std::filesystem::path &localPath,
    const std::string &version)
{
    m_repoUrl=repoUrl;
    m_localPath=localPath;
    m_version=version;
    m_initialized=true;

    git_libgit2_init();

    ConfigDownloadStatus status;

    if(std::filesystem::exists(localPath)&&!std::filesystem::is_empty(localPath))
    {
        // Existing repo — fetch updates and checkout
        status=fetchAndUpdate();
    }
    else
    {
        // Fresh clone
        status=cloneRepo();
    }

    if(status==ConfigDownloadStatus::Success)
    {
        status=checkoutVersion();
    }

    // If remote operations failed but we have a cached copy, fall back to it
    if(status!=ConfigDownloadStatus::Success&&hasCachedConfig())
    {
        spdlog::warn("Remote sync failed, falling back to cached config at {}", m_localPath.string());
        status=ConfigDownloadStatus::FallbackToCache;
    }

    m_status=status;
    m_lastSync=std::chrono::steady_clock::now();

    git_libgit2_shutdown();
    return m_status;
}

ConfigDownloadStatus ConfigDownloader::refresh()
{
    if(!m_initialized)
    {
        return ConfigDownloadStatus::NotInitialized;
    }

    // Skip refresh if interval has not elapsed
    auto elapsed=std::chrono::steady_clock::now()-m_lastSync;
    if(elapsed<m_refreshInterval)
    {
        return ConfigDownloadStatus::Success;
    }

    git_libgit2_init();

    ConfigDownloadStatus status=fetchAndUpdate();
    if(status==ConfigDownloadStatus::Success)
    {
        status=checkoutVersion();
    }

    if(status!=ConfigDownloadStatus::Success&&hasCachedConfig())
    {
        spdlog::warn("Refresh failed, continuing with cached config");
        status=ConfigDownloadStatus::FallbackToCache;
    }

    m_status=status;
    m_lastSync=std::chrono::steady_clock::now();

    git_libgit2_shutdown();
    return m_status;
}

void ConfigDownloader::setRefreshInterval(std::chrono::seconds interval)
{
    m_refreshInterval=interval;
}

const std::filesystem::path &ConfigDownloader::getLocalPath() const
{
    return m_localPath;
}

ConfigDownloadStatus ConfigDownloader::getStatus() const
{
    return m_status;
}

bool ConfigDownloader::hasCachedConfig() const
{
    if(!std::filesystem::exists(m_localPath))
    {
        return false;
    }

    // Check if the local path contains any JSON files (configs may be in subdirectories)
    for(const auto &entry:std::filesystem::recursive_directory_iterator(m_localPath))
    {
        if(entry.path().extension()==".json")
        {
            return true;
        }
    }
    return false;
}

ConfigDownloadStatus ConfigDownloader::cloneRepo()
{
    spdlog::info("Cloning config repository from {}...", m_repoUrl);

    git_repository *repo=nullptr;
    git_clone_options cloneOpts=GIT_CLONE_OPTIONS_INIT;

    int error=git_clone(&repo, m_repoUrl.c_str(), m_localPath.c_str(), &cloneOpts);
    if(error<0)
    {
        const git_error *e=git_error_last();
        spdlog::error("Failed to clone config repository: {}", e?e->message:"unknown error");
        if(repo)
        {
            git_repository_free(repo);
        }
        return ConfigDownloadStatus::CloneFailed;
    }

    spdlog::info("Successfully cloned config repository");
    git_repository_free(repo);
    return ConfigDownloadStatus::Success;
}

ConfigDownloadStatus ConfigDownloader::fetchAndUpdate()
{
    spdlog::info("Fetching config updates from remote...");

    git_repository *repo=nullptr;
    int error=git_repository_open(&repo, m_localPath.c_str());
    if(error<0)
    {
        const git_error *e=git_error_last();
        spdlog::error("Failed to open config repository at {}: {}", m_localPath.string(), e?e->message:"unknown error");
        return ConfigDownloadStatus::FetchFailed;
    }

    git_remote *remote=nullptr;
    error=git_remote_lookup(&remote, repo, "origin");
    if(error<0)
    {
        const git_error *e=git_error_last();
        spdlog::error("Failed to lookup remote 'origin': {}", e?e->message:"unknown error");
        git_repository_free(repo);
        return ConfigDownloadStatus::FetchFailed;
    }

    git_fetch_options fetchOpts=GIT_FETCH_OPTIONS_INIT;
    error=git_remote_fetch(remote, nullptr, &fetchOpts, nullptr);

    git_remote_free(remote);
    git_repository_free(repo);

    if(error<0)
    {
        const git_error *e=git_error_last();
        spdlog::error("Failed to fetch from remote: {}", e?e->message:"unknown error");
        return ConfigDownloadStatus::FetchFailed;
    }

    spdlog::info("Successfully fetched config updates");
    return ConfigDownloadStatus::Success;
}

ConfigDownloadStatus ConfigDownloader::checkoutVersion()
{
    git_repository *repo=nullptr;
    int error=git_repository_open(&repo, m_localPath.c_str());
    if(error<0)
    {
        const git_error *e=git_error_last();
        spdlog::error("Failed to open repository for checkout: {}", e?e->message:"unknown error");
        return ConfigDownloadStatus::CheckoutFailed;
    }

    spdlog::info("Checking out version: {}", m_version);

    // Try to resolve the version — prefer remote branch refs first so that
    // a fetch+checkout always picks up the latest remote commit rather than
    // a stale local branch ref that was never fast-forwarded.
    git_object *obj=nullptr;

    // 1. Try as a remote-tracking branch (most common path after fetch)
    std::string remoteBranch="refs/remotes/origin/"+m_version;
    error=git_revparse_single(&obj, repo, remoteBranch.c_str());

    if(error!=0)
    {
        // 2. Try as a direct ref / local branch / SHA
        error=git_revparse_single(&obj, repo, m_version.c_str());
    }

    if(error!=0)
    {
        // 3. Try as a tag
        std::string tag="refs/tags/"+m_version;
        error=git_revparse_single(&obj, repo, tag.c_str());
    }

    if(error!=0)
    {
        const git_error *e=git_error_last();
        spdlog::error("Failed to find version '{}': {}", m_version, e?e->message:"unknown error");
        git_repository_free(repo);
        return ConfigDownloadStatus::CheckoutFailed;
    }

    git_checkout_options checkoutOpts=GIT_CHECKOUT_OPTIONS_INIT;
    checkoutOpts.checkout_strategy=GIT_CHECKOUT_FORCE;

    error=git_checkout_tree(repo, obj, &checkoutOpts);
    if(error!=0)
    {
        const git_error *e=git_error_last();
        spdlog::error("Failed to checkout version '{}': {}", m_version, e?e->message:"unknown error");
        git_object_free(obj);
        git_repository_free(repo);
        return ConfigDownloadStatus::CheckoutFailed;
    }

    // Detach HEAD to the checked-out commit
    error=git_repository_set_head_detached(repo, git_object_id(obj));
    if(error!=0)
    {
        const git_error *e=git_error_last();
        spdlog::warn("Failed to detach HEAD: {}", e?e->message:"unknown error");
        // Non-fatal — checkout succeeded
    }

    spdlog::info("Successfully checked out version '{}'", m_version);

    git_object_free(obj);
    git_repository_free(repo);
    return ConfigDownloadStatus::Success;
}

} // namespace arbiterAI
