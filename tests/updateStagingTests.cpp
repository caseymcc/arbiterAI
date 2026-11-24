#include "updateStaging.h"
#include "installer.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

namespace fs=std::filesystem;

using arbiterAI::server::ProcessResult;
using arbiterAI::server::StageRequest;
using arbiterAI::server::StageResult;
using arbiterAI::server::StageStep;
using arbiterAI::server::UpdateHistory;

std::string readFile(const fs::path &path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    file<<text;
}

/// Copies a local file in place of the release URL; counts its calls.
class FakeDownloader:public arbiterAI::server::Downloader {
public:
    bool download(const std::string &url, const fs::path &file, const Progress &progress, std::string &error) override
    {
        calls++;
        lastUrl=url;
        if(fail)
        {
            error="download failed: Could not resolve host";
            return false;
        }

        std::error_code ec;
        fs::copy_file(source, file, fs::copy_options::overwrite_existing, ec);
        if(ec)
        {
            error=ec.message();
            return false;
        }
        int64_t size=static_cast<int64_t>(fs::file_size(file));
        if(progress)
        {
            progress(size/2, size);
            progress(size, size);
        }
        return true;
    }

    fs::path source;
    bool fail=false;
    int calls=0;
    std::string lastUrl;
};

class UpdateStagingTest:public ::testing::Test {
protected:
    void SetUp() override
    {
        m_dir=fs::temp_directory_path()/("arbiterai_staging_"+std::to_string(getpid())+"_"
            +::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(m_dir);
        m_root=m_dir/"root";
        m_cwd=m_dir/"work";
        fs::create_directories(m_cwd);

        writeFile(m_root/"bin"/"arbiterAI-server", "binary 1.0.0");
        writeFile(m_root/"share"/"arbiterAI"/"install.json", "{\"version\": \"1.0.0\"}");
        writeFile(m_cwd/"server.json", "{}");
    }

    void TearDown() override
    {
        fs::remove_all(m_dir);
    }

    /// A release tarball of \p version whose binary runs \p selfTest as a shell script.
    void makeRelease(const std::string &version, const std::string &selfTest, const std::string &markerVersion="")
    {
        fs::path build=m_dir/"build";
        fs::remove_all(build);
        std::string name="arbiterAI-server-v"+version+"-linux-x64";
        fs::path top=build/name;
        writeFile(top/"bin"/"arbiterAI-server", "#!/bin/sh\n"+selfTest+"\n");
        fs::permissions(top/"bin"/"arbiterAI-server", fs::perms::owner_all|fs::perms::group_read|fs::perms::group_exec);
        writeFile(top/"share"/"arbiterAI"/"install.json",
            "{\"version\": \""+(markerVersion.empty()?version:markerVersion)+"\"}");
        writeFile(top/"README.md", "readme");

        m_archive=m_dir/(name+".tar.gz");
        ProcessResult tar=arbiterAI::server::runProcess({"tar", "-czf", m_archive.string(), "-C", build.string(), name},
            fs::path(), std::chrono::seconds(30));
        ASSERT_EQ(tar.exitCode, 0)<<tar.output;

        std::string error;
        m_request.root=m_root;
        m_request.currentVersion="1.0.0";
        m_request.version=version;
        m_request.asset.name=name+".tar.gz";
        m_request.asset.url="https://github.com/caseymcc/arbiterAI/releases/download/v"+version+"/"+name+".tar.gz";
        m_request.asset.size=static_cast<int64_t>(fs::file_size(m_archive));
        m_request.asset.digest="sha256:"+arbiterAI::server::sha256File(m_archive, error);
        m_request.configPath=m_cwd/"server.json";
        m_request.cwd=m_cwd;
        m_request.selfTestTimeout=std::chrono::seconds(20);
        m_downloader.source=m_archive;
    }

    StageResult stage()
    {
        return arbiterAI::server::stageRelease(m_request, m_downloader, [this](StageStep step, double)
        {
            if(m_steps.empty() || m_steps.back() != step)
                m_steps.push_back(step);
        });
    }

    void expectInstallUntouched()
    {
        EXPECT_EQ(readFile(m_root/"bin"/"arbiterAI-server"), "binary 1.0.0");
        EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
        EXPECT_FALSE(fs::exists(m_root/"bin.prev"));
        EXPECT_FALSE(fs::exists(m_root/"update-state.json"));
        EXPECT_FALSE(fs::exists(m_root/".update"));
    }

    fs::path m_dir;
    fs::path m_root;
    fs::path m_cwd;
    fs::path m_archive;
    StageRequest m_request;
    FakeDownloader m_downloader;
    std::vector<StageStep> m_steps;
};

TEST(Sha256FileTest, HashesTheFile)
{
    fs::path file=fs::temp_directory_path()/("arbiterai_sha_"+std::to_string(getpid()));
    writeFile(file, "abc");
    std::string error;
    EXPECT_EQ(arbiterAI::server::sha256File(file, error), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    fs::remove(file);
}

TEST(RunProcessTest, ReportsTheExitCodeAndOutput)
{
    ProcessResult result=arbiterAI::server::runProcess({"/bin/sh", "-c", "echo out; echo err >&2; exit 7"},
        fs::path(), std::chrono::seconds(10));
    EXPECT_TRUE(result.started);
    EXPECT_EQ(result.exitCode, 7);
    EXPECT_NE(result.output.find("out"), std::string::npos);
    EXPECT_NE(result.output.find("err"), std::string::npos);
}

TEST(RunProcessTest, AChildThatCannotBeWaitedForIsNotASuccess)
{
    // with SIGCHLD ignored the kernel reaps the child: waitpid fails with ECHILD
    struct sigaction ignore{};
    ignore.sa_handler=SIG_IGN;
    struct sigaction previous{};
    sigaction(SIGCHLD, &ignore, &previous);
    ProcessResult result=arbiterAI::server::runProcess({"sh", "-c", "exit 0"}, fs::path(), std::chrono::seconds(10));
    sigaction(SIGCHLD, &previous, nullptr);

    EXPECT_TRUE(result.started);
    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("waitpid failed"), std::string::npos)<<result.output;
}

TEST(RunProcessTest, RunsInTheGivenDirectory)
{
    fs::path dir=fs::canonical(fs::temp_directory_path());
    ProcessResult result=arbiterAI::server::runProcess({"/bin/sh", "-c", "pwd"}, dir, std::chrono::seconds(10));
    EXPECT_EQ(result.output, dir.string()+"\n");
}

TEST(RunProcessTest, KillsTheChildAfterTheTimeout)
{
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    ProcessResult result=arbiterAI::server::runProcess({"/bin/sh", "-c", "sleep 30"}, fs::path(), std::chrono::milliseconds(300));
    EXPECT_TRUE(result.timedOut);
    EXPECT_LT(std::chrono::steady_clock::now()-start, std::chrono::seconds(10));
}

TEST(RunProcessTest, ChildGetsAnEmptySignalMaskAndNoInheritedFds)
{
    // As in the server: SIGTERM/SIGINT blocked, an fd opened without O_CLOEXEC.
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGINT);
    sigset_t previous;
    pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    // High, so it cannot be the fd the shell's glob opens to list the directory.
    int opened=open("/dev/null", O_RDONLY);
    int leaked=dup2(opened, 50);
    close(opened);
    ASSERT_EQ(leaked, 50);

    // Not through /bin/sh: dash clears the signal mask when it starts.
    ProcessResult mask=arbiterAI::server::runProcess({"grep", "SigBlk", "/proc/self/status"},
        fs::path(), std::chrono::seconds(10));
    ProcessResult fds=arbiterAI::server::runProcess({"ls", "-1", "/proc/self/fd"},
        fs::path(), std::chrono::seconds(10));

    close(leaked);
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);

    EXPECT_EQ(mask.output, "SigBlk:\t0000000000000000\n");
    EXPECT_EQ(("\n"+fds.output).find("\n50\n"), std::string::npos)<<fds.output;
    EXPECT_NE(("\n"+fds.output).find("\n2\n"), std::string::npos)<<fds.output;
}

class LocalAssetServer {
public:
    LocalAssetServer()
    {
        m_server.Get("/asset.tar.gz", [](const httplib::Request &, httplib::Response &res)
        {
            res.set_content(std::string(300000, 'x'), "application/octet-stream");
        });
        m_port=m_server.bind_to_any_port("127.0.0.1");
        m_thread=std::thread([this]() { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
    }

    ~LocalAssetServer()
    {
        m_server.stop();
        m_thread.join();
    }

    std::string url(const std::string &path) const
    {
        return "http://127.0.0.1:"+std::to_string(m_port)+path;
    }

private:
    httplib::Server m_server;
    int m_port=0;
    std::thread m_thread;
};

TEST(CprDownloaderTest, DownloadsToTheFileWithProgress)
{
    LocalAssetServer server;
    fs::path file=fs::temp_directory_path()/("arbiterai_download_"+std::to_string(getpid()));
    int64_t lastReceived=0;
    arbiterAI::server::CprDownloader downloader;
    std::string error;

    ASSERT_TRUE(downloader.download(server.url("/asset.tar.gz"), file, [&lastReceived](int64_t received, int64_t)
    {
        lastReceived=received;
    }, error))<<error;
    EXPECT_EQ(fs::file_size(file), 300000u);
    EXPECT_EQ(lastReceived, 300000);
    fs::remove(file);
}

TEST(CprDownloaderTest, HttpErrorIsAFailure)
{
    LocalAssetServer server;
    fs::path file=fs::temp_directory_path()/("arbiterai_download_404_"+std::to_string(getpid()));
    arbiterAI::server::CprDownloader downloader;
    std::string error;

    EXPECT_FALSE(downloader.download(server.url("/missing.tar.gz"), file, nullptr, error));
    EXPECT_EQ(error, "download failed: HTTP 404");
    fs::remove(file);
}

TEST_F(UpdateStagingTest, StagesAVerifiedRelease)
{
    makeRelease("1.1.0", "exit 0");
    StageResult result=stage();

    ASSERT_TRUE(result.ok)<<result.error;
    EXPECT_EQ(result.step, StageStep::Staged);
    EXPECT_EQ(arbiterAI::server::installedVersion(result.staged), "1.1.0");
    EXPECT_TRUE(fs::exists(result.staged/"bin"/"arbiterAI-server"));
    EXPECT_TRUE(fs::exists(result.staged/"share"));
    EXPECT_EQ(m_downloader.calls, 1);
    EXPECT_EQ(m_downloader.lastUrl, m_request.asset.url);
    EXPECT_EQ(m_steps, (std::vector<StageStep>{StageStep::CheckingSpace, StageStep::Downloading, StageStep::Verifying,
        StageStep::Unpacking, StageStep::SelfTesting, StageStep::Staged}));

    // Staging alone does not touch the install.
    EXPECT_EQ(readFile(m_root/"bin"/"arbiterAI-server"), "binary 1.0.0");
    EXPECT_FALSE(fs::exists(m_root/"bin.prev"));
    EXPECT_TRUE(arbiterAI::server::readUpdateHistory(m_root).failed.empty());
}

TEST_F(UpdateStagingTest, DigestMismatchLeavesTheInstallUntouched)
{
    makeRelease("1.1.0", "exit 0");
    m_request.asset.digest="sha256:"+std::string(64, '0');
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::Verifying);
    EXPECT_NE(result.error.find("SHA-256 mismatch"), std::string::npos)<<result.error;
    expectInstallUntouched();

    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_EQ(history.lastError, result.error);
    EXPECT_TRUE(history.isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, SelfTestOutputCutInsideAUtf8CharacterIsRecorded)
{
    // 602 bytes: the last 600 begin inside the three bytes of the dash
    makeRelease("1.1.0", "printf '\\342\\200\\224'; printf 'x%.0s' $(seq 599); exit 3");
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::SelfTesting);
    const std::string tail="..."+std::string(599, 'x');
    ASSERT_GE(result.error.size(), tail.size());
    EXPECT_EQ(result.error.substr(result.error.size()-tail.size()), tail);
    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_EQ(history.lastError, result.error);
    EXPECT_TRUE(history.isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, TarThatCannotStartIsNotMarkedFailed)
{
    makeRelease("1.1.0", "exit 0");
    const std::string path=getenv("PATH");
    fs::create_directories(m_dir/"empty");
    setenv("PATH", (m_dir/"empty").c_str(), 1);
    StageResult result=stage();
    setenv("PATH", path.c_str(), 1);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::Unpacking);
    EXPECT_NE(result.error.find("could not be started"), std::string::npos)<<result.error;
    expectInstallUntouched();
    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_EQ(history.lastError, result.error);
    EXPECT_FALSE(history.isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, FailedSelfTestLeavesTheInstallUntouched)
{
    makeRelease("1.1.0", "echo 'config rejected: unknown key' >&2; exit 3");
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::SelfTesting);
    EXPECT_NE(result.error.find("exited with status 3"), std::string::npos)<<result.error;
    EXPECT_NE(result.error.find("config rejected: unknown key"), std::string::npos)<<result.error;
    expectInstallUntouched();

    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_TRUE(history.isFailed("1.1.0"));
    ASSERT_EQ(history.updates.size(), 1u);
    EXPECT_EQ(history.updates[0].outcome, "failed");
    EXPECT_EQ(history.updates[0].from, "1.0.0");
}

TEST_F(UpdateStagingTest, ASelfTestThatExits143WhileStoppingIsNotMarkedFailed)
{
    // the real binary sigwait()s SIGTERM and _Exit(128+SIGTERM)s before it listens
    makeRelease("1.1.0", "exit 143");
    m_request.stopping=[]() { return true; };
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::SelfTesting);
    EXPECT_NE(result.error.find("the server is stopping"), std::string::npos)<<result.error;
    expectInstallUntouched();
    EXPECT_FALSE(arbiterAI::server::readUpdateHistory(m_root).isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, ASelfTestThatExits143WithoutAStopIsMarkedFailed)
{
    makeRelease("1.1.0", "exit 143");
    m_request.stopping=[]() { return false; };
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error.find("the server is stopping"), std::string::npos)<<result.error;
    EXPECT_TRUE(arbiterAI::server::readUpdateHistory(m_root).isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, ASelfTestKilledByAnotherSignalWhileStoppingIsMarkedFailed)
{
    makeRelease("1.1.0", "kill -KILL $$");
    bool asked=false;
    m_request.stopping=[&asked]() { asked=true; return true; };
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(asked);
    EXPECT_NE(result.error.find("was killed by"), std::string::npos)<<result.error;
    EXPECT_TRUE(arbiterAI::server::readUpdateHistory(m_root).isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, SelfTestRunsInTheServersDirectoryWithItsConfig)
{
    std::string script="test \"$(pwd)\" = '"+fs::canonical(m_cwd).string()+"' || { echo \"cwd $(pwd)\"; exit 5; }\n"
        "test \"$1 $2 $3\" = '--self-test -c "+(m_cwd/"server.json").string()+"' || { echo \"args $*\"; exit 6; }\n";
    makeRelease("1.1.0", script);

    StageResult result=stage();
    EXPECT_TRUE(result.ok)<<result.error;
}

TEST_F(UpdateStagingTest, HungSelfTestTimesOut)
{
    makeRelease("1.1.0", "sleep 30");
    m_request.selfTestTimeout=std::chrono::milliseconds(300);
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::SelfTesting);
    EXPECT_NE(result.error.find("timed out"), std::string::npos)<<result.error;
    expectInstallUntouched();
}

TEST_F(UpdateStagingTest, TooLittleFreeSpaceLeavesTheInstallUntouched)
{
    makeRelease("1.1.0", "exit 0");
    uintmax_t size=static_cast<uintmax_t>(m_request.asset.size);
    m_request.freeSpace=[size](const fs::path &)
    {
        return size*3-1;
    };
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::CheckingSpace);
    EXPECT_NE(result.error.find("not enough free space"), std::string::npos)<<result.error;
    EXPECT_EQ(m_downloader.calls, 0);
    expectInstallUntouched();

    // Not the release's fault: auto-update may try it again.
    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_EQ(history.lastError, result.error);
    EXPECT_FALSE(history.isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, ExactlyThreeTimesTheAssetIsEnough)
{
    makeRelease("1.1.0", "exit 0");
    uintmax_t size=static_cast<uintmax_t>(m_request.asset.size);
    m_request.freeSpace=[size](const fs::path &)
    {
        return size*3;
    };
    StageResult result=stage();
    EXPECT_TRUE(result.ok)<<result.error;
}

TEST_F(UpdateStagingTest, FailedDownloadIsNotMarkedFailed)
{
    makeRelease("1.1.0", "exit 0");
    m_downloader.fail=true;
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::Downloading);
    expectInstallUntouched();
    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_EQ(history.lastError, "download failed: Could not resolve host");
    EXPECT_FALSE(history.isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, ArchiveWithAnotherVersionIsRefused)
{
    makeRelease("1.1.0", "exit 0", "1.2.0");
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::Unpacking);
    EXPECT_NE(result.error.find("'1.2.0'"), std::string::npos)<<result.error;
    expectInstallUntouched();
    EXPECT_TRUE(arbiterAI::server::readUpdateHistory(m_root).isFailed("1.1.0"));
}

TEST_F(UpdateStagingTest, TruncatedDownloadIsRefused)
{
    makeRelease("1.1.0", "exit 0");
    m_request.asset.size+=10;
    StageResult result=stage();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.step, StageStep::Verifying);
    expectInstallUntouched();
}

TEST_F(UpdateStagingTest, LeftoverStagingFromACrashIsCleared)
{
    makeRelease("1.1.0", "exit 0");
    writeFile(m_root/".update"/"unpack"/"stale"/"file", "old");
    StageResult result=stage();

    ASSERT_TRUE(result.ok)<<result.error;
    EXPECT_FALSE(fs::exists(m_root/".update"/"unpack"/"stale"));
}

} // anonymous namespace
