#include "autoUpdate.h"
#include "updateRoutes.h"
#include "installer.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace
{

namespace fs=std::filesystem;

using arbiterAI::server::AutoDecision;
using arbiterAI::server::AutoInputs;
using arbiterAI::server::AutoUpdater;
using arbiterAI::server::ReleaseInfo;
using arbiterAI::server::Supervisor;
using arbiterAI::server::SystemTime;
using arbiterAI::server::UpdatePolicy;

const SystemTime publishedAt=*arbiterAI::server::parseUtcTime("2026-11-20T12:00:00Z");

ReleaseInfo release(const std::string &version)
{
    ReleaseInfo info;
    info.tag="v"+version;
    info.version=*arbiterAI::server::SemVer::parse(version, false);
    info.publishedAt="2026-11-20T12:00:00Z";
    arbiterAI::server::ReleaseAsset asset;
    asset.name="arbiterAI-server-v"+version+"-linux-x64.tar.gz";
    asset.url="https://github.com/caseymcc/arbiterAI/releases/download/v"+version+"/"+asset.name;
    asset.size=1000;
    asset.digest="sha256:"+std::string(64, '0');
    info.serverAsset=asset;
    return info;
}

/// Everything auto-update needs, holding: a patch release two days old, at
/// noon, after an hour of idle time, under systemd.
AutoInputs ready()
{
    AutoInputs inputs;
    inputs.policy.autoApply=true;
    inputs.currentVersion="1.0.0";
    inputs.latest=release("1.0.1");
    inputs.check={true, ""};
    inputs.supervisor=Supervisor::Systemd;
    inputs.now=publishedAt+std::chrono::hours(48);
    inputs.localMinuteOfDay=12*60;
    inputs.idleSince=inputs.now-std::chrono::hours(1);
    return inputs;
}

TEST(AutoPolicyTest, InstallsWhenEverythingHolds)
{
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(ready());
    EXPECT_TRUE(decision.apply)<<decision.status;
    EXPECT_TRUE(decision.enabled);
    EXPECT_EQ(decision.status, "installing v1.0.1");
}

TEST(AutoPolicyTest, OffByDefault)
{
    AutoInputs inputs=ready();
    inputs.policy=UpdatePolicy();
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_FALSE(decision.enabled);
    EXPECT_EQ(decision.status, "off");
}

TEST(AutoPolicyTest, AllowApplyOffTurnsItOff)
{
    AutoInputs inputs=ready();
    inputs.policy.allowApply=false;
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_FALSE(decision.enabled);
    EXPECT_EQ(decision.status, "off: updates.allow_apply is false in the config file");
}

TEST(AutoPolicyTest, WaitsWithNoSupervisor)
{
    AutoInputs inputs=ready();
    inputs.supervisor=Supervisor::None;
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_FALSE(decision.enabled);
    EXPECT_EQ(decision.status, "off: no supervisor declared (see README)");
}

TEST(AutoPolicyTest, UpToDateDoesNothing)
{
    AutoInputs inputs=ready();
    inputs.latest=release("1.0.0");
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "up to date");
}

TEST(AutoPolicyTest, PatchScopeIgnoresAMinorBump)
{
    AutoInputs inputs=ready();
    inputs.latest=release("1.1.0");
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "v1.1.0 is a minor update, outside the 'patch' scope: install it from the dashboard");
}

TEST(AutoPolicyTest, MinorScopeTakesAMinorBumpButNotAMajor)
{
    AutoInputs inputs=ready();
    inputs.policy.autoApplyScope="minor";
    inputs.latest=release("1.1.0");
    EXPECT_TRUE(arbiterAI::server::decideAutoUpdate(inputs).apply);

    inputs.latest=release("2.0.0");
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "v2.0.0 is a major update, outside the 'minor' scope: install it from the dashboard");

    inputs.policy.autoApplyScope="major";
    EXPECT_TRUE(arbiterAI::server::decideAutoUpdate(inputs).apply);
}

TEST(AutoPolicyTest, SkipsAFailedVersion)
{
    AutoInputs inputs=ready();
    inputs.failed={"1.0.1"};
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "skipping v1.0.1: it failed here before (install it from the dashboard to try again)");

    // A newer release is not skipped.
    inputs.latest=release("1.0.2");
    EXPECT_TRUE(arbiterAI::server::decideAutoUpdate(inputs).apply);
}

TEST(AutoPolicyTest, WaitsDuringThePublishDelay)
{
    AutoInputs inputs=ready();
    inputs.now=publishedAt+std::chrono::hours(23);
    inputs.idleSince=inputs.now-std::chrono::hours(1);
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "waiting: v1.0.1 installs 24 h after it was published");
    ASSERT_TRUE(decision.nextAttempt.has_value());
    EXPECT_EQ(*decision.nextAttempt, publishedAt+std::chrono::hours(24));
}

TEST(AutoPolicyTest, WaitsOutsideTheWindow)
{
    AutoInputs inputs=ready();
    inputs.policy.autoApplyWindow="02:00-04:00";
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "waiting for the window 02:00-04:00 (local time)");
    ASSERT_TRUE(decision.nextAttempt.has_value());
    EXPECT_EQ(*decision.nextAttempt, inputs.now+std::chrono::hours(14));

    inputs.localMinuteOfDay=3*60;
    EXPECT_TRUE(arbiterAI::server::decideAutoUpdate(inputs).apply);
    inputs.localMinuteOfDay=4*60;
    EXPECT_FALSE(arbiterAI::server::decideAutoUpdate(inputs).apply);
}

TEST(AutoPolicyTest, WindowMayWrapPastMidnight)
{
    AutoInputs inputs=ready();
    inputs.policy.autoApplyWindow="23:00-01:00";
    inputs.localMinuteOfDay=23*60+30;
    EXPECT_TRUE(arbiterAI::server::decideAutoUpdate(inputs).apply);
    inputs.localMinuteOfDay=30;
    EXPECT_TRUE(arbiterAI::server::decideAutoUpdate(inputs).apply);
    inputs.localMinuteOfDay=12*60;
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    ASSERT_TRUE(decision.nextAttempt.has_value());
    EXPECT_EQ(*decision.nextAttempt, inputs.now+std::chrono::hours(11));
}

TEST(AutoPolicyTest, WaitsWhileOneHttpRequestIsOpen)
{
    AutoInputs inputs=ready();
    inputs.busy="1 HTTP request(s)";
    inputs.idleSince.reset();
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "waiting for the server to be idle (1 HTTP request(s) running)");
}

TEST(AutoPolicyTest, WaitsUntilIdleForIdleMinutes)
{
    AutoInputs inputs=ready();
    inputs.idleSince=inputs.now-std::chrono::minutes(4);
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "waiting for 10 idle minutes");
    ASSERT_TRUE(decision.nextAttempt.has_value());
    EXPECT_EQ(*decision.nextAttempt, inputs.now+std::chrono::minutes(6));
}

TEST(AutoPolicyTest, WaitsWhileApplyIsBlocked)
{
    AutoInputs inputs=ready();
    inputs.check={false, "the install root /opt/arbiterAI is not writable"};
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "blocked: the install root /opt/arbiterAI is not writable");
}

TEST(AutoPolicyTest, WaitsWhileAnUpdateRuns)
{
    AutoInputs inputs=ready();
    inputs.applierBusy=true;
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.status, "an update is running");
}

TEST(AutoPolicyTest, WaitsAfterAFailedAttempt)
{
    AutoInputs inputs=ready();
    inputs.retryAfter=inputs.now+std::chrono::minutes(30);
    AutoDecision decision=arbiterAI::server::decideAutoUpdate(inputs);
    EXPECT_FALSE(decision.apply);
    EXPECT_EQ(decision.nextAttempt, inputs.retryAfter);
}

TEST(AutoPolicyTest, ParsesTimeWindows)
{
    int start=0;
    int end=0;
    ASSERT_TRUE(arbiterAI::server::parseTimeWindow("23:15-01:05", start, end));
    EXPECT_EQ(start, 23*60+15);
    EXPECT_EQ(end, 65);
    EXPECT_FALSE(arbiterAI::server::parseTimeWindow("24:00-01:00", start, end));
    EXPECT_FALSE(arbiterAI::server::parseTimeWindow("2:00-4:00", start, end));
    EXPECT_FALSE(arbiterAI::server::parseTimeWindow("02:00-02:00", start, end));
    EXPECT_FALSE(arbiterAI::server::parseTimeWindow("02:00", start, end));
}

TEST(AutoPolicyConfigTest, ReadsAndValidatesTheAutoKeys)
{
    UpdatePolicy policy;
    std::string error;
    ASSERT_TRUE(arbiterAI::server::readUpdatePolicy({{"auto_apply", true}, {"auto_apply_scope", "minor"},
        {"auto_apply_delay_hours", 0}, {"auto_apply_window", "22:00-06:00"}, {"idle_minutes", 30}}, policy, error))<<error;
    EXPECT_TRUE(policy.autoApply);
    EXPECT_EQ(policy.autoApplyScope, "minor");
    EXPECT_EQ(policy.autoApplyDelayHours, 0);
    EXPECT_EQ(policy.autoApplyWindow, "22:00-06:00");
    EXPECT_EQ(policy.idleMinutes, 30);

    UpdatePolicy unchanged;
    EXPECT_FALSE(arbiterAI::server::readUpdatePolicy({{"auto_apply_scope", "all"}}, unchanged, error));
    EXPECT_EQ(error, "'updates.auto_apply_scope' must be \"patch\", \"minor\" or \"major\"");
    EXPECT_FALSE(arbiterAI::server::readUpdatePolicy({{"auto_apply_window", "late"}}, unchanged, error));
    EXPECT_FALSE(arbiterAI::server::readUpdatePolicy({{"idle_minutes", -1}}, unchanged, error));
    EXPECT_FALSE(arbiterAI::server::readUpdatePolicy({{"auto_apply", "yes"}}, unchanged, error));
    EXPECT_EQ(unchanged.autoApplyScope, "patch");
}

TEST(AutoPolicyConfigTest, PutMergesTheAutoKeysButNotAllowApply)
{
    nlohmann::json cfg={{"updates", {{"allow_apply", true}}}};
    UpdatePolicy policy;
    std::string error;
    ASSERT_TRUE(arbiterAI::server::mergeUpdatesConfig(cfg, {{"auto_apply", true}, {"auto_apply_window", "01:00-03:00"},
        {"idle_minutes", 5}}, policy, error))<<error;
    EXPECT_EQ(cfg["updates"]["auto_apply"], true);
    EXPECT_EQ(cfg["updates"]["auto_apply_window"], "01:00-03:00");
    EXPECT_EQ(cfg["updates"]["idle_minutes"], 5);
    EXPECT_TRUE(policy.autoApply);
    EXPECT_EQ(policy.idleMinutes, 5);

    nlohmann::json before=cfg;
    EXPECT_FALSE(arbiterAI::server::mergeUpdatesConfig(cfg, {{"auto_apply", true}, {"allow_apply", false}}, policy, error));
    EXPECT_EQ(cfg, before);
}

/// A downloader that always fails, so an attempt ends at once without touching anything.
class FailingDownloader:public arbiterAI::server::Downloader {
public:
    explicit FailingDownloader(int &calls):m_calls(calls) {}

    bool download(const std::string &, const fs::path &, const Progress &, std::string &error) override
    {
        m_calls++;
        error="download failed: offline";
        return false;
    }

private:
    int &m_calls;
};

class AutoUpdaterTest:public ::testing::Test {
protected:
    void SetUp() override
    {
        m_root=fs::temp_directory_path()/("arbiterai_auto_"+std::to_string(getpid())+"_"
            +::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(m_root);
        fs::create_directories(m_root/"bin");

        arbiterAI::server::ApplierConfig config;
        config.root=m_root;
        config.currentVersion="1.0.0";
        m_applier=std::make_unique<arbiterAI::server::UpdateApplier>(config,
            std::make_unique<FailingDownloader>(m_downloads), nullptr, nullptr);

        m_policy.autoApply=true;
        m_now=publishedAt+std::chrono::hours(48);

        AutoUpdater::Sources sources;
        sources.policy=[this]() { return m_policy; };
        sources.status=[]()
        {
            arbiterAI::server::UpdateStatus status;
            status.latest=release("1.0.1");
            status.updateAvailable=true;
            return status;
        };
        sources.failed=[]() { return std::vector<std::string>(); };
        sources.check=[]() { return arbiterAI::server::ApplyCheck{true, ""}; };
        sources.supervisor=[]() { return Supervisor::Docker; };
        sources.busy=[this]() { return m_busy; };
        sources.now=[this]() { return m_now; };
        sources.localMinuteOfDay=[](SystemTime) { return 12*60; };
        m_updater=std::make_unique<AutoUpdater>(sources, *m_applier, "1.0.0");
    }

    void TearDown() override
    {
        arbiterAI::server::setAutoUpdater(nullptr);
        m_updater.reset();
        m_applier.reset();
        fs::remove_all(m_root);
    }

    /// A tick at m_now, then wait for an apply it started to end.
    AutoDecision tickAt(std::chrono::minutes offset)
    {
        m_now=publishedAt+std::chrono::hours(48)+offset;
        AutoDecision decision=m_updater->tick();
        m_applier->wait();
        return decision;
    }

    fs::path m_root;
    int m_downloads=0;
    std::unique_ptr<arbiterAI::server::UpdateApplier> m_applier;
    UpdatePolicy m_policy;
    SystemTime m_now;
    std::string m_busy;
    std::unique_ptr<AutoUpdater> m_updater;
};

TEST_F(AutoUpdaterTest, IdleTimeRestartsWhenWorkIsSeen)
{
    EXPECT_FALSE(tickAt(std::chrono::minutes(0)).apply);
    m_busy="1 HTTP request(s)";
    EXPECT_FALSE(tickAt(std::chrono::minutes(5)).apply);
    m_busy.clear();
    EXPECT_FALSE(tickAt(std::chrono::minutes(6)).apply);
    // Ten minutes after the first tick, but only nine since the request.
    AutoDecision early=tickAt(std::chrono::minutes(15));
    EXPECT_FALSE(early.apply);
    EXPECT_EQ(early.status, "waiting for 10 idle minutes");
    EXPECT_EQ(m_downloads, 0);

    AutoDecision due=tickAt(std::chrono::minutes(16));
    EXPECT_TRUE(due.apply)<<due.status;
    EXPECT_EQ(m_downloads, 1);
}

TEST_F(AutoUpdaterTest, AFailedAttemptWaitsForTheRetryDelay)
{
    m_policy.idleMinutes=0;
    ASSERT_TRUE(tickAt(std::chrono::minutes(0)).apply);
    ASSERT_EQ(m_downloads, 1);

    AutoDecision after=tickAt(std::chrono::minutes(1));
    EXPECT_FALSE(after.apply);
    EXPECT_EQ(after.status, "the last attempt at v1.0.1 failed; trying again later");
    EXPECT_EQ(m_downloads, 1);

    EXPECT_FALSE(tickAt(std::chrono::minutes(59)).apply);
    EXPECT_TRUE(tickAt(std::chrono::minutes(61)).apply);
    EXPECT_EQ(m_downloads, 2);
}

TEST_F(AutoUpdaterTest, PolicyChangesApplyAtTheNextTick)
{
    m_policy.autoApply=false;
    EXPECT_EQ(tickAt(std::chrono::minutes(0)).status, "off");
    m_policy.autoApply=true;
    m_policy.autoApplyWindow="02:00-04:00";
    EXPECT_EQ(tickAt(std::chrono::minutes(1)).status, "waiting for the window 02:00-04:00 (local time)");
    EXPECT_EQ(m_downloads, 0);
}

TEST_F(AutoUpdaterTest, GetUpdateReportsTheAutoStatus)
{
    m_policy.autoApplyWindow="02:00-04:00";
    tickAt(std::chrono::minutes(0));
    arbiterAI::server::setAutoUpdater(m_updater.get());

    nlohmann::json status=m_updater->statusJson();
    EXPECT_EQ(status["enabled"], true);
    EXPECT_EQ(status["status"], "waiting for the window 02:00-04:00 (local time)");
    EXPECT_EQ(status["next_attempt"], "2026-11-23T02:00:00Z");

    arbiterAI::server::UpdateManager manager(nullptr, "1.0.0");
    arbiterAI::server::setUpdateManager(&manager);
    httplib::Request req;
    httplib::Response res;
    arbiterAI::server::handleGetUpdate(req, res);
    arbiterAI::server::setUpdateManager(nullptr);
    EXPECT_EQ(nlohmann::json::parse(res.body)["auto"], status);
}

} // anonymous namespace
