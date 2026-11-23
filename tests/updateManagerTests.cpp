#include "updateManager.h"

#include <gtest/gtest.h>

#include <deque>
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
