#include "updateApplier.h"
#include "updateRoutes.h"
#include "installer.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace
{

namespace fs=std::filesystem;

using arbiterAI::server::ApplyCheck;
using arbiterAI::server::ProcessResult;
using arbiterAI::server::ReleaseInfo;
using arbiterAI::server::UpdateApplier;
using arbiterAI::server::UpdateHistory;
using arbiterAI::server::UpdateState;

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

/// Copies a local file; can be held at the start of a download until released.
class FakeDownloader:public arbiterAI::server::Downloader {
public:
    bool download(const std::string &, const fs::path &file, const Progress &progress, std::string &error) override
    {
        {
            std::unique_lock<std::mutex> lock(mutex);
            started=true;
            cv.notify_all();
            cv.wait(lock, [this]() { return !hold; });
        }
        if(progress)
            progress(1, 2);

        std::error_code ec;
        fs::copy_file(source, file, fs::copy_options::overwrite_existing, ec);
        if(ec)
        {
            error=ec.message();
            return false;
        }
        return true;
    }

    void waitStarted()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]() { return started; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        hold=false;
        cv.notify_all();
    }

    fs::path source;
    std::mutex mutex;
    std::condition_variable cv;
    bool hold=false;
    bool started=false;
};

const ApplyCheck allowed{true, ""};

class UpdateApplierTest:public ::testing::Test {
protected:
    void SetUp() override
    {
        m_dir=fs::temp_directory_path()/("arbiterai_applier_"+std::to_string(getpid())+"_"
            +::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(m_dir);
        m_root=m_dir/"root";
        m_cwd=m_dir/"work";
        fs::create_directories(m_cwd);
        writeFile(m_cwd/"server.json", "{}");
        writeFile(m_root/"bin"/"arbiterAI-server", "binary 1.0.0");
        writeFile(m_root/"share"/"arbiterAI"/"install.json", "{\"version\": \"1.0.0\"}");

        makeRelease("1.1.0", "exit 0");
    }

    void TearDown() override
    {
        arbiterAI::server::setUpdateApplier(nullptr);
        arbiterAI::server::setUpdateManager(nullptr);
        arbiterAI::server::setApplyContext(arbiterAI::server::ApplyContext(), nullptr);
        m_applier.reset();
        std::error_code ec;
        fs::permissions(m_root/"bin.prev", fs::perms::owner_all, ec);
        fs::remove_all(m_dir);
    }

    /// A release tarball of \p version whose binary runs \p selfTest as a script, and m_latest for it.
    void makeRelease(const std::string &version, const std::string &selfTest)
    {
        fs::path build=m_dir/"build";
        fs::remove_all(build);
        std::string name="arbiterAI-server-v"+version+"-linux-x64";
        writeFile(build/name/"bin"/"arbiterAI-server", "#!/bin/sh\n"+selfTest+"\n");
        fs::permissions(build/name/"bin"/"arbiterAI-server", fs::perms::owner_all);
        writeFile(build/name/"share"/"arbiterAI"/"install.json", "{\"version\": \""+version+"\"}");

        m_archive=m_dir/(name+".tar.gz");
        ProcessResult tar=arbiterAI::server::runProcess({"tar", "-czf", m_archive.string(), "-C", build.string(), name},
            fs::path(), std::chrono::seconds(30));
        ASSERT_EQ(tar.exitCode, 0)<<tar.output;

        std::string error;
        ReleaseInfo release;
        release.tag="v"+version;
        release.version=*arbiterAI::server::SemVer::parse(version, false);
        arbiterAI::server::ReleaseAsset asset;
        asset.name=name+".tar.gz";
        asset.url="https://github.com/caseymcc/arbiterAI/releases/download/v"+version+"/"+asset.name;
        asset.size=static_cast<int64_t>(fs::file_size(m_archive));
        asset.digest="sha256:"+arbiterAI::server::sha256File(m_archive, error);
        release.serverAsset=asset;
        m_latest=release;
    }

    UpdateApplier &applier(const std::string &currentVersion="1.0.0")
    {
        arbiterAI::server::ApplierConfig config;
        config.root=m_root;
        config.currentVersion=currentVersion;
        config.configPath=m_cwd/"server.json";
        config.cwd=m_cwd;
        config.selfTestTimeout=std::chrono::seconds(20);
        config.idleWaitTimeout=m_idleWaitTimeout;
        config.idlePollInterval=std::chrono::milliseconds(10);

        std::unique_ptr<FakeDownloader> downloader=std::make_unique<FakeDownloader>();
        downloader->source=m_archive;
        m_downloader=downloader.get();
        m_applier=std::make_unique<UpdateApplier>(config, std::move(downloader),
            []()
            {
                return nlohmann::json::array({{{"model", "qwen"}, {"variant", "Q4"}, {"context", 8192},
                    {"runtime_options", {{"flash_attn", true}}}, {"devices", {0}}, {"pinned", true}}});
            },
            [this]()
            {
                m_restarts++;
            });
        return *m_applier;
    }

    fs::path m_dir;
    fs::path m_root;
    fs::path m_cwd;
    fs::path m_archive;
    std::optional<ReleaseInfo> m_latest;
    std::unique_ptr<UpdateApplier> m_applier;
    FakeDownloader *m_downloader=nullptr;
    std::atomic<int> m_restarts{0};
    std::chrono::milliseconds m_idleWaitTimeout=std::chrono::seconds(20);
};

/// Waits until the applier reports \p state, up to 20 s.
bool waitForState(UpdateApplier &applier, const std::string &state)
{
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<deadline)
    {
        if(applier.statusJson()["state"] == state)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

TEST_F(UpdateApplierTest, ApplyInstallsWithPrevAndAsksForARestart)
{
    std::string message;
    ASSERT_EQ(applier().apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::Started)<<message;
    m_applier->wait();

    EXPECT_EQ(m_restarts, 1);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");
    EXPECT_EQ(readFile(m_root/"bin.prev"/"arbiterAI-server"), "binary 1.0.0");
    EXPECT_FALSE(fs::exists(m_root/".update"));
    EXPECT_EQ(m_applier->statusJson()["state"], "restarting");
    EXPECT_TRUE(m_applier->busy());
}

TEST_F(UpdateApplierTest, ApplyWritesTheStateFileWithTheLoadedModels)
{
    std::string message;
    ASSERT_EQ(applier().apply("v1.1.0", m_latest, allowed, message), UpdateApplier::Start::Started)<<message;
    m_applier->wait();

    std::string error;
    std::optional<UpdateState> state=arbiterAI::server::readUpdateState(m_root, error);
    ASSERT_TRUE(state.has_value())<<error;
    EXPECT_EQ(state->from, "1.0.0");
    EXPECT_EQ(state->to, "1.1.0");
    EXPECT_EQ(state->attempt, 0);
    ASSERT_EQ(state->loadedModels.size(), 1u);
    EXPECT_EQ(state->loadedModels[0]["model"], "qwen");
    EXPECT_EQ(state->loadedModels[0]["pinned"], true);
    EXPECT_EQ(state->loadedModels[0]["runtime_options"]["flash_attn"], true);
}

TEST_F(UpdateApplierTest, OnlyTheLatestReleaseIsAccepted)
{
    std::string message;
    EXPECT_EQ(applier().apply("1.0.5", m_latest, allowed, message), UpdateApplier::Start::NotLatest);
    EXPECT_EQ(message, "only the latest release (v1.1.0) can be installed");
    EXPECT_EQ(arbiterAI::server::applyStartStatus(UpdateApplier::Start::NotLatest), 409);
    EXPECT_FALSE(m_applier->busy());
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
}

TEST_F(UpdateApplierTest, ALatestThatIsNotNewerIsRefused)
{
    std::string message;
    EXPECT_EQ(applier("1.1.0").apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::NotLatest);
    EXPECT_EQ(message, "v1.1.0 is not newer than the running v1.1.0");

    EXPECT_EQ(m_applier->apply("1.1.0", std::nullopt, allowed, message), UpdateApplier::Start::NotLatest);
}

TEST_F(UpdateApplierTest, ApplyIsRefusedWhileBlocked)
{
    std::string message;
    ApplyCheck blocked{false, "updates.allow_apply is false in the config file"};
    EXPECT_EQ(applier().apply("1.1.0", m_latest, blocked, message), UpdateApplier::Start::Blocked);
    EXPECT_EQ(message, "updates.allow_apply is false in the config file");
    EXPECT_EQ(arbiterAI::server::applyStartStatus(UpdateApplier::Start::Blocked), 403);
}

TEST_F(UpdateApplierTest, SecondApplyWhileOneRunsIsBusyAndStatusShowsTheStage)
{
    UpdateApplier &updater=applier();
    m_downloader->hold=true;

    std::string message;
    ASSERT_EQ(updater.apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::Started)<<message;
    m_downloader->waitStarted();

    EXPECT_EQ(updater.apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::Busy);
    EXPECT_EQ(updater.rollback(allowed, message), UpdateApplier::Start::Busy);
    nlohmann::json status=updater.statusJson();
    EXPECT_EQ(status["state"], "downloading");
    EXPECT_TRUE(status["progress"].is_number());

    m_downloader->release();
    updater.wait();
    EXPECT_EQ(m_restarts, 1);
}

TEST_F(UpdateApplierTest, FailedStagingLeavesTheApplierIdleWithTheError)
{
    m_latest->serverAsset->digest="sha256:"+std::string(64, '0');

    std::string message;
    ASSERT_EQ(applier().apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::Started)<<message;
    m_applier->wait();

    EXPECT_EQ(m_restarts, 0);
    EXPECT_FALSE(m_applier->busy());
    nlohmann::json status=m_applier->statusJson();
    EXPECT_EQ(status["state"], "idle");
    EXPECT_TRUE(status["progress"].is_null());
    ASSERT_TRUE(status["update_error"].is_string());
    EXPECT_NE(status["update_error"].get<std::string>().find("SHA-256 mismatch"), std::string::npos);
    EXPECT_FALSE(fs::exists(m_root/"update-state.json"));
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
}

TEST_F(UpdateApplierTest, FailedSwapRemovesTheStateFileAndKeepsTheInstall)
{
    // A bin.prev that cannot be removed makes the swap fail.
    writeFile(m_root/"bin.prev"/"locked"/"file", "x");
    fs::permissions(m_root/"bin.prev", fs::perms::owner_read|fs::perms::owner_exec);

    std::string message;
    ASSERT_EQ(applier().apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::Started)<<message;
    m_applier->wait();

    EXPECT_EQ(m_restarts, 0);
    EXPECT_FALSE(m_applier->busy());
    EXPECT_FALSE(fs::exists(m_root/"update-state.json"));
    EXPECT_FALSE(fs::exists(m_root/".update"));
    EXPECT_EQ(readFile(m_root/"bin"/"arbiterAI-server"), "binary 1.0.0");
    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_EQ(history.lastError.rfind("installing v1.1.0 failed: ", 0), 0u)<<history.lastError;
}

TEST_F(UpdateApplierTest, RollbackSwapsPrevBackAndAsksForARestart)
{
    std::string message;
    ASSERT_EQ(applier().apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::Started)<<message;
    m_applier->wait();
    ASSERT_EQ(m_restarts, 1);

    // What the new version sees once it runs: probation, then the user rolls back.
    UpdateApplier &updated=applier("1.1.0");
    nlohmann::json before=updated.statusJson();
    EXPECT_EQ(before["rollback_available"], true);
    EXPECT_EQ(before["rollback_version"], "1.0.0");
    EXPECT_EQ(before["drain_timeout_s"], 60);

    ASSERT_EQ(updated.rollback(allowed, message), UpdateApplier::Start::Started)<<message;
    updated.wait();

    EXPECT_EQ(m_restarts, 2);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
    EXPECT_EQ(readFile(m_root/"bin"/"arbiterAI-server"), "binary 1.0.0");
    EXPECT_FALSE(arbiterAI::server::hasPrevious(m_root));
    EXPECT_FALSE(fs::exists(m_root/"update-state.json"));

    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_TRUE(history.isFailed("1.1.0"));
    ASSERT_FALSE(history.updates.empty());
    EXPECT_EQ(history.updates.back().outcome, "rolled_back");
    // the update that was undone, as a crash rollback records it
    EXPECT_EQ(history.updates.back().from, "1.0.0");
    EXPECT_EQ(history.updates.back().to, "1.1.0");
}

TEST_F(UpdateApplierTest, AFailedRollbackRecordsItsErrorAndMarksNothingFailed)
{
    std::string message;
    ASSERT_EQ(applier().apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::Started)<<message;
    m_applier->wait();
    ASSERT_EQ(m_restarts, 1);

    // A bin.failed that cannot be removed makes the swap back fail.
    writeFile(m_root/"bin.failed"/"locked"/"file", "x");
    fs::permissions(m_root/"bin.failed", fs::perms::owner_read|fs::perms::owner_exec);

    UpdateApplier &updated=applier("1.1.0");
    ASSERT_EQ(updated.rollback(allowed, message), UpdateApplier::Start::Started)<<message;
    updated.wait();
    fs::permissions(m_root/"bin.failed", fs::perms::owner_all);

    EXPECT_EQ(m_restarts, 1);
    EXPECT_FALSE(updated.busy());
    nlohmann::json status=updated.statusJson();
    EXPECT_EQ(status["state"], "idle");
    ASSERT_TRUE(status["update_error"].is_string())<<status.dump();
    EXPECT_EQ(status["update_error"].get<std::string>().rfind("rolling back to v1.0.0 failed: ", 0), 0u)<<status.dump();
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");
    EXPECT_FALSE(arbiterAI::server::readUpdateHistory(m_root).isFailed("1.1.0"));
}

TEST_F(UpdateApplierTest, AnAutoApplyWaitsForIdleBeforeTheSwap)
{
    std::atomic<bool> requestOpen{true};
    UpdateApplier &updater=applier();
    std::string message;
    ASSERT_EQ(updater.apply("1.1.0", m_latest, allowed, message,
        [&requestOpen]() { return requestOpen?std::string("1 HTTP request(s)"):std::string(); }),
        UpdateApplier::Start::Started)<<message;

    ASSERT_TRUE(waitForState(updater, "waiting_idle"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(m_restarts, 0);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
    EXPECT_FALSE(fs::exists(m_root/"update-state.json"));

    requestOpen=false;
    updater.wait();
    EXPECT_EQ(m_restarts, 1);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");
}

TEST_F(UpdateApplierTest, AnAutoApplyStillBusyAfterStagingIsPutOff)
{
    m_idleWaitTimeout=std::chrono::milliseconds(200);
    std::string message;
    ASSERT_EQ(applier().apply("1.1.0", m_latest, allowed, message,
        []() { return std::string("1 scheduler job(s)"); }), UpdateApplier::Start::Started)<<message;
    m_applier->wait();

    EXPECT_EQ(m_restarts, 0);
    EXPECT_FALSE(m_applier->busy());
    EXPECT_EQ(m_applier->statusJson()["state"], "idle");
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
    EXPECT_FALSE(fs::exists(m_root/".update"));
    EXPECT_FALSE(fs::exists(m_root/"update-state.json"));
    EXPECT_FALSE(arbiterAI::server::readUpdateHistory(m_root).isFailed("1.1.0"));
}

TEST_F(UpdateApplierTest, StatusSaysWhenThisProcessStarted)
{
    nlohmann::json first=applier().statusJson();
    ASSERT_TRUE(first["started_at"].is_string())<<first.dump();
    std::string started=first["started_at"].get<std::string>();
    EXPECT_EQ(started.size(), std::string("2026-11-23T09:14:02.117Z").size())<<started;
    EXPECT_EQ(started.back(), 'Z');
    EXPECT_EQ(applier("1.1.0").statusJson()["started_at"], started);
}

TEST_F(UpdateApplierTest, RollbackWithoutPrevIsRefused)
{
    std::string message;
    EXPECT_EQ(applier().rollback(allowed, message), UpdateApplier::Start::NoPrevious);
    EXPECT_EQ(m_applier->statusJson()["rollback_available"], false);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
}

TEST_F(UpdateApplierTest, PrevVersionComesFromProbationThenTheHistory)
{
    std::string message;
    ASSERT_EQ(applier().apply("1.1.0", m_latest, allowed, message), UpdateApplier::Start::Started)<<message;
    m_applier->wait();

    // The new version during probation: no history record yet.
    UpdateApplier &updated=applier("1.1.0");
    EXPECT_EQ(updated.statusJson()["prev_version"], "1.0.0");

    // Healthy: the state file goes, the history keeps it.
    ASSERT_TRUE(arbiterAI::server::markHealthy(m_root, "1.1.0"));
    EXPECT_EQ(updated.statusJson()["prev_version"], "1.0.0");
}

class ListSource:public arbiterAI::server::ReleaseSource {
public:
    explicit ListSource(nlohmann::json releases):m_releases(std::move(releases)) {}

    arbiterAI::server::ReleaseFetch fetch(const std::string &) override
    {
        arbiterAI::server::ReleaseFetch result;
        result.status=200;
        result.body=m_releases.dump();
        return result;
    }

private:
    nlohmann::json m_releases;
};

class FakeEnvironment:public arbiterAI::server::Environment {
public:
    std::string variable(const std::string &) const override { return std::string(); }
    /// The real files, but not this test host's container markers.
    bool fileExists(const fs::path &path) const override
    {
        return path != "/.dockerenv" && path != "/run/.containerenv" && fs::exists(path);
    }
    std::string mountInfo() const override { return std::string(); }
    bool isWritable(const fs::path &) const override { return true; }
};

TEST_F(UpdateApplierTest, ApplyRouteStartsTheLatestAndRefusesOthers)
{
    const arbiterAI::server::ReleaseAsset &asset=*m_latest->serverAsset;
    nlohmann::json releases=nlohmann::json::array({{
        {"tag_name", "v1.1.0"}, {"draft", false}, {"prerelease", false},
        {"html_url", "https://github.com/caseymcc/arbiterAI/releases/tag/v1.1.0"}, {"body", ""},
        {"published_at", "2026-11-01T00:00:00Z"},
        {"assets", {{{"name", asset.name}, {"browser_download_url", asset.url}, {"size", asset.size}, {"digest", asset.digest}}}}
    }});
    arbiterAI::server::UpdateManager manager(std::make_unique<ListSource>(releases), "1.0.0");
    manager.checkNow();
    arbiterAI::server::setUpdateManager(&manager);

    arbiterAI::server::ApplyContext context;
    context.root=m_root;
    context.cwd=m_cwd;
    arbiterAI::server::setApplyContext(context, std::make_shared<FakeEnvironment>());
    arbiterAI::server::setUpdateApplier(&applier());

    httplib::Request bad;
    bad.body="{\"ver\": 1}";
    httplib::Response badRes;
    arbiterAI::server::handleApplyUpdate(bad, badRes);
    EXPECT_EQ(badRes.status, 400);

    httplib::Request old;
    old.body="{\"version\": \"1.0.9\"}";
    httplib::Response oldRes;
    arbiterAI::server::handleApplyUpdate(old, oldRes);
    EXPECT_EQ(oldRes.status, 409);
    EXPECT_EQ(nlohmann::json::parse(oldRes.body)["error"]["code"], "not_latest");

    httplib::Request req;
    req.body="{\"version\": \"1.1.0\"}";
    httplib::Response res;
    arbiterAI::server::handleApplyUpdate(req, res);
    EXPECT_EQ(res.status, 202)<<res.body;
    m_applier->wait();
    EXPECT_EQ(m_restarts, 1);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");

    httplib::Response again;
    arbiterAI::server::handleApplyUpdate(req, again);
    EXPECT_EQ(again.status, 409);
    EXPECT_EQ(nlohmann::json::parse(again.body)["error"]["code"], "update_running");

    httplib::Request get;
    httplib::Response status;
    arbiterAI::server::handleGetUpdate(get, status);
    nlohmann::json body=nlohmann::json::parse(status.body);
    EXPECT_EQ(body["state"], "restarting");
    EXPECT_EQ(body["can_apply"], true);
}

TEST_F(UpdateApplierTest, RollbackRouteWithoutPrevIs409)
{
    arbiterAI::server::UpdateManager manager(std::make_unique<ListSource>(nlohmann::json::array()), "1.0.0");
    arbiterAI::server::setUpdateManager(&manager);
    arbiterAI::server::ApplyContext context;
    context.root=m_root;
    context.cwd=m_cwd;
    arbiterAI::server::setApplyContext(context, std::make_shared<FakeEnvironment>());
    arbiterAI::server::setUpdateApplier(&applier());

    httplib::Request req;
    httplib::Response res;
    arbiterAI::server::handleRollbackUpdate(req, res);
    EXPECT_EQ(res.status, 409);
    EXPECT_EQ(nlohmann::json::parse(res.body)["error"]["code"], "no_previous");
}

} // anonymous namespace
