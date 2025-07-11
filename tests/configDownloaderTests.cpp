#include "arbiterAI/configDownloader.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <memory>
#include <httplib.h>
#include <thread>
#include <git2.h>

namespace arbiterAI
{

class ConfigDownloaderTest : public ::testing::Test
{
protected:
    std::string remote_repo_path;
    std::string local_repo_path;
    pid_t server_pid;

    void SetUp() override
    {
        local_repo_path = (std::filesystem::temp_directory_path() / "test_repo").string();
        remote_repo_path = (std::filesystem::temp_directory_path() / "remote_repo.git").string();

        // Create a bare git repository to act as the remote
        git_repository *repo = nullptr;
        git_repository_init(&repo, remote_repo_path.c_str(), 1);
        git_repository_free(repo);

        server_pid = fork();
        if (server_pid == 0)
        {
            // Child process
            execlp("git", "git", "daemon", "--verbose", "--export-all", "--port=8080", "--reuseaddr",
                   ("--base-path=" + std::filesystem::temp_directory_path().string()).c_str(),
                   (char *)nullptr);
            exit(1); // Should not be reached
        }
        // Give the server a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override
    {
        // Kill the git daemon process
        if (server_pid > 0)
        {
            kill(server_pid, SIGTERM);
            waitpid(server_pid, nullptr, 0);
        }
        std::filesystem::remove_all(local_repo_path);
        std::filesystem::remove_all(remote_repo_path);
    }
};

TEST_F(ConfigDownloaderTest, InitializeClonesRepo)
{
    ConfigDownloader downloader;
    downloader.initialize("git://localhost:8080/remote_repo.git", local_repo_path);
    EXPECT_TRUE(std::filesystem::exists(local_repo_path + "/.git"));
}

} // namespace arbiterAI