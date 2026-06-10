#include "arbiterAI/configDownloader.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <memory>
#include <httplib.h>
#include <thread>
#include <git2.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace arbiterAI
{

class ConfigDownloaderTest : public ::testing::Test
{
protected:
    std::string remote_repo_path;
    std::string local_repo_path;

    /// Poll until the git daemon accepts TCP connections (or timeout).
    static bool waitForDaemon(int port, std::chrono::seconds timeout)
    {
        auto deadline=std::chrono::steady_clock::now()+timeout;

        while(std::chrono::steady_clock::now()<deadline)
        {
            int sock=socket(AF_INET, SOCK_STREAM, 0);
            if(sock>=0)
            {
                sockaddr_in addr{};
                addr.sin_family=AF_INET;
                addr.sin_port=htons(port);
                addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

                int result=connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
                close(sock);
                if(result==0)
                    return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    void SetUp() override
    {
        local_repo_path=(std::filesystem::temp_directory_path()/"test_repo").string();
        remote_repo_path=(std::filesystem::temp_directory_path()/"remote_repo.git").string();

        // Clean up any leftovers from a previous (crashed) run
        std::system("pkill -x git-daemon >/dev/null 2>&1");
        std::filesystem::remove_all(local_repo_path);
        std::filesystem::remove_all(remote_repo_path);

        // Create a bare git repository to act as the remote.
        // libgit2 must be initialized first — without it
        // git_repository_init produces an invalid repository.
        git_libgit2_init();

        git_repository *repo=nullptr;
        ASSERT_EQ(git_repository_init(&repo, remote_repo_path.c_str(), 1), 0);
        git_repository_free(repo);

        std::thread server_thread([]()
        {
            std::string base_path=std::filesystem::temp_directory_path().string();
            std::string command="git daemon --export-all --port=8080 --reuseaddr --base-path="+base_path+" >/dev/null 2>&1";

            std::system(command.c_str());
        });
        server_thread.detach();

        // Wait until the daemon is actually accepting connections
        ASSERT_TRUE(waitForDaemon(8080, std::chrono::seconds(10)))
            <<"git daemon did not start listening on port 8080";
    }

    void TearDown() override
    {
        std::system("pkill -x git-daemon >/dev/null 2>&1");
        std::filesystem::remove_all(local_repo_path);
        std::filesystem::remove_all(remote_repo_path);
        git_libgit2_shutdown();
    }
};

TEST_F(ConfigDownloaderTest, InitializeClonesRepo)
{
    ConfigDownloader downloader;
    downloader.initialize("git://localhost:8080/remote_repo.git", local_repo_path);
    EXPECT_TRUE(std::filesystem::exists(local_repo_path+"/.git"));
}

} // namespace arbiterAI