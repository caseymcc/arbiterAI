#include "configDownloader.h"

#include <git2.h>
#include <iostream>
#include <spdlog/spdlog.h>

namespace arbiterAI
{
void ConfigDownloader::initialize(const std::string &repoUrl, const std::filesystem::path &localPath, const std::string &version)
{
    m_localPath=localPath;
    git_libgit2_init();
    git_repository *repo=nullptr;

    if(std::filesystem::exists(localPath)&&!std::filesystem::is_empty(localPath))
    {
        spdlog::info("Checking for configuration updates in existing repository...");
        if(git_repository_open(&repo, localPath.c_str())<0)
        {
            spdlog::error("Failed to open repository at {}", localPath.string());
            git_libgit2_shutdown();
            return;
        }

        spdlog::info("Fetching latest updates from remote...");
        git_remote *remote=nullptr;
        if(git_remote_lookup(&remote, repo, "origin")<0)
        {
            spdlog::error("Failed to lookup remote 'origin'");
        }
        else
        {
            git_fetch_options fetch_opts=GIT_FETCH_OPTIONS_INIT;
            if(git_remote_fetch(remote, NULL, &fetch_opts, NULL)<0)
            {
                const git_error *e=git_error_last();
                spdlog::error("Failed to fetch from remote: {}", e->message);
            }
            git_remote_free(remote);
        }
    }
    else
    {
        spdlog::info("Cloning remote configurations from {}...", repoUrl);
        git_clone_options clone_opts=GIT_CLONE_OPTIONS_INIT;
        if(git_clone(&repo, repoUrl.c_str(), localPath.c_str(), &clone_opts)<0)
        {
            const git_error *e=git_error_last();
            spdlog::error("Failed to clone repository: {}", e->message);
            git_libgit2_shutdown();
            return;
        }
        spdlog::info("Successfully cloned configurations.");
    }

    if(repo)
    {
        spdlog::info("Checking out version: {}", version);
        git_object *obj=nullptr;
        std::string ref_name="refs/tags/"+version;
        if(git_revparse_single(&obj, repo, version.c_str())!=0)
        {
            // If version is not a SHA, try as a branch
            ref_name="refs/remotes/origin/"+version;
            if(git_revparse_single(&obj, repo, ref_name.c_str())!=0)
            {
                // If not a branch, try as a tag
                ref_name="refs/tags/"+version;
                if(git_revparse_single(&obj, repo, ref_name.c_str())!=0)
                {
                    const git_error *e=git_error_last();
                    spdlog::error("Failed to find version '{}': {}", version, e?e->message:"unknown error");
                    git_repository_free(repo);
                    git_libgit2_shutdown();
                    return;
                }
            }
        }

        git_checkout_options checkout_opts=GIT_CHECKOUT_OPTIONS_INIT;
        checkout_opts.checkout_strategy=GIT_CHECKOUT_FORCE;

        if(git_checkout_tree(repo, obj, &checkout_opts)!=0)
        {
            const git_error *e=git_error_last();
            spdlog::error("Failed to checkout version '{}': {}", version, e?e->message:"unknown error");
        }
        else
        {
            spdlog::info("Successfully checked out version '{}'", version);
            // Detach HEAD
            if(git_repository_set_head_detached(repo, git_object_id(obj))!=0)
            {
                const git_error *e=git_error_last();
                spdlog::error("Failed to detach HEAD: {}", e?e->message:"unknown error");
            }
        }

        git_object_free(obj);
        git_repository_free(repo);
    }

    git_libgit2_shutdown();
}

const std::filesystem::path &ConfigDownloader::getLocalPath() const
{
    return m_localPath;
}
}