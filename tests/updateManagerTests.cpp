#include "updateManager.h"
#include "updateRoutes.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <vector>

namespace
{

using arbiterAI::server::ReleaseFetch;
using arbiterAI::server::UpdateCheckResult;
using arbiterAI::server::UpdateChecker;
using arbiterAI::server::UpdateStatus;

struct FakeLog {
    std::deque<ReleaseFetch> answers;
    std::vector<std::string> sentEtags;
};

class FakeSource:public arbiterAI::server::ReleaseSource {
public:
    explicit FakeSource(std::shared_ptr<FakeLog> log):m_log(std::move(log)) {}

    ReleaseFetch fetch(const std::string &etag) override
    {
        m_log->sentEtags.push_back(etag);
        ReleaseFetch answer=m_log->answers.front();
        m_log->answers.pop_front();
        return answer;
    }

private:
    std::shared_ptr<FakeLog> m_log;
};

nlohmann::json release(const std::string &tag, const std::string &createdAt="2026-11-01T00:00:00Z")
{
    nlohmann::json result;
    result["tag_name"]=tag;
    result["draft"]=false;
    result["prerelease"]=false;
    result["html_url"]="https://github.com/caseymcc/arbiterAI/releases/tag/"+tag;
    result["body"]="notes for "+tag;
    result["created_at"]=createdAt;
    result["published_at"]=createdAt;
    result["assets"]=nlohmann::json::array({
        {{"name", "arbiterAI-server-"+tag+"-linux-x64.tar.gz"},
         {"browser_download_url", "https://github.com/caseymcc/arbiterAI/releases/download/"+tag+"/arbiterAI-server-"+tag+"-linux-x64.tar.gz"},
         {"size", 12345},
         {"digest", "sha256:abc"}},
        {{"name", "arbiterAI-cli-"+tag+"-linux-x64.tar.gz"},
         {"browser_download_url", "https://example.com/cli"},
         {"size", 1},
         {"digest", "sha256:def"}}
    });
    return result;
}

ReleaseFetch ok(const nlohmann::json &releases, const std::string &etag="\"e1\"")
{
    ReleaseFetch fetch;
    fetch.status=200;
    fetch.body=releases.dump();
    fetch.etag=etag;
    return fetch;
}

ReleaseFetch status(int code)
{
    ReleaseFetch fetch;
    fetch.status=code;
    return fetch;
}

class UpdateCheckerTest:public ::testing::Test {
protected:
    std::unique_ptr<UpdateChecker> makeChecker(const std::string &current)
    {
        return std::make_unique<UpdateChecker>(std::make_unique<FakeSource>(m_log), current);
    }

    std::shared_ptr<FakeLog> m_log=std::make_shared<FakeLog>();
};

} // namespace

TEST_F(UpdateCheckerTest, NewerReleaseIsAnUpdate)
{
    m_log->answers.push_back(ok(nlohmann::json::array({release("v0.1.16"), release("v0.1.15")})));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    EXPECT_EQ(checker->check(), UpdateCheckResult::Updated);
    UpdateStatus status=checker->status();
    ASSERT_TRUE(status.latest.has_value());
    EXPECT_EQ(status.latest->tag, "v0.1.16");
    EXPECT_TRUE(status.updateAvailable);
    EXPECT_EQ(status.latest->htmlUrl, "https://github.com/caseymcc/arbiterAI/releases/tag/v0.1.16");
    EXPECT_EQ(status.latest->notes, "notes for v0.1.16");
    ASSERT_TRUE(status.latest->serverAsset.has_value());
    EXPECT_EQ(status.latest->serverAsset->name, "arbiterAI-server-v0.1.16-linux-x64.tar.gz");
    EXPECT_EQ(status.latest->serverAsset->url, "https://github.com/caseymcc/arbiterAI/releases/download/v0.1.16/arbiterAI-server-v0.1.16-linux-x64.tar.gz");
    EXPECT_EQ(status.latest->serverAsset->size, 12345);
    EXPECT_EQ(status.latest->serverAsset->digest, "sha256:abc");
}

TEST_F(UpdateCheckerTest, SameReleaseIsNoUpdate)
{
    m_log->answers.push_back(ok(nlohmann::json::array({release("v0.1.15")})));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    EXPECT_EQ(checker->check(), UpdateCheckResult::Updated);
    UpdateStatus status=checker->status();
    ASSERT_TRUE(status.latest.has_value());
    EXPECT_EQ(status.latest->tag, "v0.1.15");
    EXPECT_FALSE(status.updateAvailable);
}

TEST_F(UpdateCheckerTest, OlderReleaseIsNoUpdate)
{
    m_log->answers.push_back(ok(nlohmann::json::array({release("v0.1.14")})));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    checker->check();
    UpdateStatus status=checker->status();
    ASSERT_TRUE(status.latest.has_value());
    EXPECT_EQ(status.latest->tag, "v0.1.14");
    EXPECT_FALSE(status.updateAvailable);
}

TEST_F(UpdateCheckerTest, PrereleaseIsIgnored)
{
    nlohmann::json pre=release("v0.1.17");
    pre["prerelease"]=true;
    m_log->answers.push_back(ok(nlohmann::json::array({pre, release("v0.1.16")})));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    checker->check();
    ASSERT_TRUE(checker->status().latest.has_value());
    EXPECT_EQ(checker->status().latest->tag, "v0.1.16");
}

TEST_F(UpdateCheckerTest, DraftIsIgnored)
{
    nlohmann::json draft=release("v0.1.17");
    draft["draft"]=true;
    m_log->answers.push_back(ok(nlohmann::json::array({draft, release("v0.1.16")})));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    checker->check();
    ASSERT_TRUE(checker->status().latest.has_value());
    EXPECT_EQ(checker->status().latest->tag, "v0.1.16");
}

TEST_F(UpdateCheckerTest, MalformedTagsAreIgnored)
{
    m_log->answers.push_back(ok(nlohmann::json::array({
        release("v0.2.0-rc1"),
        release("0.3.0"),
        release("v0.4"),
        release("v0.5.0.1"),
        release("v05.0.0"),
        release("nightly"),
        release("v0.1.16")
    })));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    checker->check();
    ASSERT_TRUE(checker->status().latest.has_value());
    EXPECT_EQ(checker->status().latest->tag, "v0.1.16");
}

TEST_F(UpdateCheckerTest, NoOfficialReleaseIsNoUpdate)
{
    m_log->answers.push_back(ok(nlohmann::json::array({release("nightly")})));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    EXPECT_EQ(checker->check(), UpdateCheckResult::Updated);
    EXPECT_FALSE(checker->status().latest.has_value());
    EXPECT_FALSE(checker->status().updateAvailable);
    EXPECT_TRUE(checker->statusJson()["latest"].is_null());
}

TEST_F(UpdateCheckerTest, HighestSemverBeatsMostRecentlyCreated)
{
    // newest first, as GitHub lists them: a patch on the 0.1 line made after 0.2.0
    m_log->answers.push_back(ok(nlohmann::json::array({
        release("v0.1.20", "2026-11-20T00:00:00Z"),
        release("v0.2.0", "2026-11-10T00:00:00Z"),
        release("v0.1.19", "2026-11-05T00:00:00Z")
    })));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    checker->check();
    ASSERT_TRUE(checker->status().latest.has_value());
    EXPECT_EQ(checker->status().latest->tag, "v0.2.0");
}

TEST_F(UpdateCheckerTest, NotModifiedKeepsResultAndSendsEtag)
{
    m_log->answers.push_back(ok(nlohmann::json::array({release("v0.1.16")}), "\"etag-1\""));
    m_log->answers.push_back(status(304));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    checker->check();
    EXPECT_EQ(checker->check(), UpdateCheckResult::NotModified);

    ASSERT_EQ(m_log->sentEtags.size(), 2u);
    EXPECT_EQ(m_log->sentEtags[0], "");
    EXPECT_EQ(m_log->sentEtags[1], "\"etag-1\"");
    UpdateStatus status=checker->status();
    ASSERT_TRUE(status.latest.has_value());
    EXPECT_EQ(status.latest->tag, "v0.1.16");
    EXPECT_TRUE(status.updateAvailable);
    EXPECT_TRUE(status.lastError.empty());
}

TEST_F(UpdateCheckerTest, FetchErrorKeepsPreviousResult)
{
    ReleaseFetch offline;
    offline.error="Couldn't resolve host name";
    m_log->answers.push_back(ok(nlohmann::json::array({release("v0.1.16")})));
    m_log->answers.push_back(offline);
    m_log->answers.push_back(status(403));
    ReleaseFetch badJson;
    badJson.status=200;
    badJson.body="{not json";
    m_log->answers.push_back(badJson);
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    checker->check();
    std::chrono::system_clock::time_point firstCheck=*checker->status().checkedAt;

    EXPECT_EQ(checker->check(), UpdateCheckResult::Failed);
    EXPECT_EQ(checker->status().lastError, "Couldn't resolve host name");
    EXPECT_EQ(checker->check(), UpdateCheckResult::Failed);
    EXPECT_EQ(checker->status().lastError, "GitHub answered HTTP 403");
    EXPECT_EQ(checker->check(), UpdateCheckResult::Failed);
    EXPECT_EQ(checker->status().lastError, "release list is not valid JSON");

    UpdateStatus status=checker->status();
    ASSERT_TRUE(status.latest.has_value());
    EXPECT_EQ(status.latest->tag, "v0.1.16");
    EXPECT_TRUE(status.updateAvailable);
    EXPECT_EQ(*status.checkedAt, firstCheck);
}

TEST_F(UpdateCheckerTest, FailedListDoesNotReplaceEtag)
{
    ReleaseFetch notList=ok(nlohmann::json::object({{"message", "API rate limit exceeded"}}), "\"etag-bad\"");
    m_log->answers.push_back(ok(nlohmann::json::array({release("v0.1.16")}), "\"etag-1\""));
    m_log->answers.push_back(notList);
    m_log->answers.push_back(status(304));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    checker->check();
    EXPECT_EQ(checker->check(), UpdateCheckResult::Failed);
    checker->check();

    ASSERT_EQ(m_log->sentEtags.size(), 3u);
    EXPECT_EQ(m_log->sentEtags[2], "\"etag-1\"");
}

TEST_F(UpdateCheckerTest, StatusJsonHasTheApiFields)
{
    m_log->answers.push_back(ok(nlohmann::json::array({release("v0.1.16")})));
    std::unique_ptr<UpdateChecker> checker=makeChecker("0.1.15");

    nlohmann::json before=checker->statusJson();
    EXPECT_EQ(before["current"], "0.1.15");
    EXPECT_TRUE(before["latest"].is_null());
    EXPECT_EQ(before["update_available"], false);
    EXPECT_TRUE(before["checked_at"].is_null());
    EXPECT_TRUE(before["last_error"].is_null());

    checker->check();
    nlohmann::json after=checker->statusJson();
    EXPECT_EQ(after["current"], "0.1.15");
    EXPECT_EQ(after["latest"], "0.1.16");
    EXPECT_EQ(after["update_available"], true);
    EXPECT_EQ(after["release_url"], "https://github.com/caseymcc/arbiterAI/releases/tag/v0.1.16");
    EXPECT_EQ(after["release_notes"], "notes for v0.1.16");
    EXPECT_EQ(after["published_at"], "2026-11-01T00:00:00Z");
    ASSERT_TRUE(after["checked_at"].is_string());
    EXPECT_EQ(after["checked_at"].get<std::string>().size(), 20u);
    EXPECT_TRUE(after["last_error"].is_null());
}

namespace
{

using arbiterAI::server::UpdateManager;
using arbiterAI::server::UpdatePolicy;

/// Answers every fetch with one newer release, and counts them.
class CountingSource:public arbiterAI::server::ReleaseSource {
public:
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        int fetches=0;
    };

    explicit CountingSource(std::shared_ptr<State> state):m_state(std::move(state)) {}

    ReleaseFetch fetch(const std::string &) override
    {
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->fetches++;
        }
        m_state->changed.notify_all();
        return ok(nlohmann::json::array({release("v0.1.16")}));
    }

private:
    std::shared_ptr<State> m_state;
};

class UpdateManagerTest:public ::testing::Test {
protected:
    void SetUp() override
    {
        m_manager=std::make_unique<UpdateManager>(std::make_unique<CountingSource>(m_state), "0.1.15");
    }

    void TearDown() override
    {
        arbiterAI::server::setUpdateManager(nullptr);
        m_manager->stop();
    }

    bool waitForFetches(int count, std::chrono::milliseconds timeout=std::chrono::milliseconds(2000))
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        return m_state->changed.wait_for(lock, timeout, [&]() { return m_state->fetches>=count; });
    }

    int fetches()
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->fetches;
    }

    std::shared_ptr<CountingSource::State> m_state=std::make_shared<CountingSource::State>();
    std::unique_ptr<UpdateManager> m_manager;
};

UpdatePolicy policy(bool check, int intervalHours)
{
    UpdatePolicy result;
    result.check=check;
    result.intervalHours=intervalHours;
    return result;
}

} // namespace

TEST_F(UpdateManagerTest, ChecksAtStart)
{
    m_manager->start(policy(true, 24));

    EXPECT_TRUE(waitForFetches(1));
    // the next one is a day away
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(fetches(), 1);
}

TEST_F(UpdateManagerTest, ChecksAgainEveryInterval)
{
    m_manager->setIntervalUnit(std::chrono::milliseconds(20));
    m_manager->start(policy(true, 1));

    EXPECT_TRUE(waitForFetches(3));
}

TEST_F(UpdateManagerTest, CheckOffMakesNoRequest)
{
    m_manager->setIntervalUnit(std::chrono::milliseconds(10));
    m_manager->start(policy(false, 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(fetches(), 0);
}

TEST_F(UpdateManagerTest, TurningCheckOnLiveStartsChecking)
{
    m_manager->start(policy(false, 24));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(fetches(), 0);

    m_manager->setPolicy(policy(true, 24));
    EXPECT_TRUE(waitForFetches(1));
}

TEST_F(UpdateManagerTest, TurningCheckOffLiveStopsChecking)
{
    m_manager->setIntervalUnit(std::chrono::milliseconds(10));
    m_manager->start(policy(true, 1));
    ASSERT_TRUE(waitForFetches(2));

    m_manager->setPolicy(policy(false, 1));
    // a check the thread had already begun may still land
    int afterOff=fetches()+1;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_LE(fetches(), afterOff);
}

TEST_F(UpdateManagerTest, ShorterIntervalLiveTakesEffect)
{
    m_manager->setIntervalUnit(std::chrono::milliseconds(10));
    m_manager->start(policy(true, 1000));
    ASSERT_TRUE(waitForFetches(1));

    m_manager->setPolicy(policy(true, 1));
    EXPECT_TRUE(waitForFetches(3));
}

TEST_F(UpdateManagerTest, StopDoesNotWaitForTheInterval)
{
    m_manager->setIntervalUnit(std::chrono::milliseconds(100));
    m_manager->start(policy(true, 30));
    ASSERT_TRUE(waitForFetches(1));
    // let the thread reach its wait for the next check
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::chrono::steady_clock::time_point before=std::chrono::steady_clock::now();
    m_manager->stop();
    EXPECT_LT(std::chrono::steady_clock::now()-before, std::chrono::seconds(1));
}

TEST_F(UpdateManagerTest, GetUpdateRouteReturnsStatus)
{
    m_manager->start(policy(false, 24));
    m_manager->checkNow();
    arbiterAI::server::setUpdateManager(m_manager.get());

    httplib::Request req;
    httplib::Response res;
    arbiterAI::server::handleGetUpdate(req, res);

    EXPECT_EQ(res.status, -1);
    nlohmann::json body=nlohmann::json::parse(res.body);
    EXPECT_EQ(body["current"], "0.1.15");
    EXPECT_EQ(body["latest"], "0.1.16");
    EXPECT_EQ(body["update_available"], true);
    EXPECT_EQ(fetches(), 1);
}

TEST_F(UpdateManagerTest, CheckRouteChecksNow)
{
    m_manager->start(policy(false, 24));
    arbiterAI::server::setUpdateManager(m_manager.get());

    httplib::Request req;
    httplib::Response res;
    arbiterAI::server::handleCheckUpdate(req, res);

    EXPECT_EQ(fetches(), 1);
    nlohmann::json body=nlohmann::json::parse(res.body);
    EXPECT_EQ(body["latest"], "0.1.16");
    EXPECT_TRUE(body["checked_at"].is_string());
}

TEST_F(UpdateManagerTest, RoutesWithoutManagerAnswer503)
{
    httplib::Request req;
    httplib::Response res;
    arbiterAI::server::handleGetUpdate(req, res);
    EXPECT_EQ(res.status, 503);

    httplib::Response checkRes;
    arbiterAI::server::handleCheckUpdate(req, checkRes);
    EXPECT_EQ(checkRes.status, 503);
}

TEST(UpdatePolicyTest, ReadsKeysAndKeepsDefaults)
{
    UpdatePolicy result;
    std::string error;
    ASSERT_TRUE(arbiterAI::server::readUpdatePolicy(nlohmann::json::object({{"interval_hours", 6}}), result, error));
    EXPECT_TRUE(result.check);
    EXPECT_EQ(result.intervalHours, 6);

    ASSERT_TRUE(arbiterAI::server::readUpdatePolicy(nlohmann::json::object({{"check", false}}), result, error));
    EXPECT_FALSE(result.check);
    EXPECT_EQ(result.intervalHours, 6);
}

TEST(UpdatePolicyTest, RejectsBadValues)
{
    UpdatePolicy result;
    std::string error;
    EXPECT_FALSE(arbiterAI::server::readUpdatePolicy(nlohmann::json::object({{"check", "yes"}}), result, error));
    EXPECT_EQ(error, "'updates.check' must be a boolean");
    EXPECT_FALSE(arbiterAI::server::readUpdatePolicy(nlohmann::json::object({{"interval_hours", 0}}), result, error));
    EXPECT_FALSE(arbiterAI::server::readUpdatePolicy(nlohmann::json::object({{"interval_hours", 1.5}}), result, error));
    EXPECT_FALSE(arbiterAI::server::readUpdatePolicy(nlohmann::json::array(), result, error));
    EXPECT_TRUE(result.check);
    EXPECT_EQ(result.intervalHours, 24);
}

TEST(UpdatePolicyTest, MergeKeepsOtherUpdateKeys)
{
    nlohmann::json cfg={{"port", 8080}, {"updates", {{"check", true}, {"allow_apply", false}}}};
    UpdatePolicy result;
    std::string error;
    ASSERT_TRUE(arbiterAI::server::mergeUpdatesConfig(cfg, {{"interval_hours", 6}, {"auto_apply", true}}, result, error));

    EXPECT_EQ(cfg["updates"]["interval_hours"], 6);
    EXPECT_EQ(cfg["updates"]["check"], true);
    EXPECT_EQ(cfg["updates"]["allow_apply"], false);
    EXPECT_FALSE(cfg["updates"].contains("auto_apply"));
    EXPECT_EQ(cfg["port"], 8080);
    EXPECT_TRUE(result.check);
    EXPECT_EQ(result.intervalHours, 6);
}

TEST(UpdatePolicyTest, MergeRejectsBadValuesAndLeavesConfig)
{
    nlohmann::json cfg={{"updates", {{"check", true}}}};
    nlohmann::json original=cfg;
    UpdatePolicy result;
    std::string error;
    EXPECT_FALSE(arbiterAI::server::mergeUpdatesConfig(cfg, {{"interval_hours", -1}}, result, error));
    EXPECT_EQ(cfg, original);

    nlohmann::json badFile={{"updates", {{"check", "yes"}}}};
    nlohmann::json badOriginal=badFile;
    EXPECT_FALSE(arbiterAI::server::mergeUpdatesConfig(badFile, {{"interval_hours", 6}}, result, error));
    EXPECT_EQ(badFile, badOriginal);
}

namespace
{

class LocalGitHub {
public:
    LocalGitHub()
    {
        m_server.Get("/repos/owner/name/releases", [this](const httplib::Request &req, httplib::Response &res)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_ifNoneMatch.push_back(req.get_header_value("If-None-Match"));
                m_perPage=req.get_param_value("per_page");
            }
            if(req.get_header_value("If-None-Match") == "\"v1\"")
            {
                res.status=304;
                return;
            }
            res.set_header("ETag", "\"v1\"");
            res.set_content(nlohmann::json::array({release("v0.1.16")}).dump(), "application/json");
        });
        m_port=m_server.bind_to_any_port("127.0.0.1");
        m_thread=std::thread([this]() { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
    }

    ~LocalGitHub()
    {
        m_server.stop();
        m_thread.join();
    }

    std::string base() const { return "http://127.0.0.1:"+std::to_string(m_port); }

    std::vector<std::string> ifNoneMatch()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_ifNoneMatch;
    }

    std::string perPage()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_perPage;
    }

private:
    httplib::Server m_server;
    int m_port=0;
    std::thread m_thread;
    std::mutex m_mutex;
    std::vector<std::string> m_ifNoneMatch;
    std::string m_perPage;
};

} // namespace

TEST(GitHubReleaseSourceTest, SendsIfNoneMatchAndReadsNotModified)
{
    LocalGitHub github;
    UpdateChecker checker(std::make_unique<arbiterAI::server::GitHubReleaseSource>("owner/name", github.base()), "0.1.15");

    EXPECT_EQ(checker.check(), UpdateCheckResult::Updated);
    EXPECT_EQ(checker.check(), UpdateCheckResult::NotModified);

    std::vector<std::string> sent=github.ifNoneMatch();
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0], "");
    EXPECT_EQ(sent[1], "\"v1\"");
    EXPECT_EQ(github.perPage(), "100");
    EXPECT_TRUE(checker.status().updateAvailable);
}

TEST(GitHubReleaseSourceTest, UnreachableIsAnError)
{
    std::string base;
    {
        LocalGitHub github;
        base=github.base();
    }
    arbiterAI::server::GitHubReleaseSource source("owner/name", base);

    ReleaseFetch fetch=source.fetch("");
    EXPECT_FALSE(fetch.error.empty());
    EXPECT_EQ(fetch.status, 0);
}
