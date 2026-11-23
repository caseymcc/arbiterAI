#ifndef _ARBITERAI_SERVER_UPDATEMANAGER_H_
#define _ARBITERAI_SERVER_UPDATEMANAGER_H_

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace arbiterAI
{
namespace server
{

/// A release version, from a tag of exactly "vX.Y.Z" (or "X.Y.Z" for the
/// running version).
struct SemVer {
    int major=0;
    int minor=0;
    int patch=0;

    /// "vX.Y.Z" when \p requireV, else "X.Y.Z"; nothing for any other text
    /// ("v1.2", "v1.2.3-rc1", "release-1.2.3", leading zeros).
    static std::optional<SemVer> parse(const std::string &text, bool requireV);

    std::string toString() const;

    bool operator<(const SemVer &other) const;
    bool operator==(const SemVer &other) const;
};

/// The server tarball of a release (arbiterAI-server-vX.Y.Z-linux-x64.tar.gz).
struct ReleaseAsset {
    std::string name;
    std::string url;
    int64_t size=0;
    std::string digest;///< "sha256:..."
};

struct ReleaseInfo {
    std::string tag;
    SemVer version;
    std::string htmlUrl;
    std::string notes;
    std::string publishedAt;
    std::optional<ReleaseAsset> serverAsset;
};

/// One GET of the repository's release list. status 0 with \p error set when
/// the request did not get an answer.
struct ReleaseFetch {
    int status=0;
    std::string body;
    std::string etag;
    std::string error;
};

/// Where the release list comes from: GitHub in the server, a fake in tests.
class ReleaseSource {
public:
    virtual ~ReleaseSource()=default;

    /// The release list, sent with If-None-Match: \p etag when it is not empty.
    virtual ReleaseFetch fetch(const std::string &etag)=0;
};

/// The server asset's name for \p tag.
std::string serverAssetName(const std::string &tag);

/// The highest-versioned release of a GitHub release list that is not a draft,
/// not a prerelease and tagged exactly vX.Y.Z. Nothing when there is none.
/// \p error is set when \p releases is not a list.
std::optional<ReleaseInfo> selectLatestRelease(const nlohmann::json &releases, std::string &error);

enum class UpdateCheckResult
{
    Updated,    ///< a new list was read
    NotModified,///< 304: the last result stands
    Failed      ///< no usable answer: the last result stands, lastError is set
};

struct UpdateStatus {
    std::string current;
    std::optional<ReleaseInfo> latest;
    bool updateAvailable=false;
    std::optional<std::chrono::system_clock::time_point> checkedAt;///< last check that got an answer
    std::string lastError;///< of the last check; empty when it succeeded
};

/// Checks a ReleaseSource for a release newer than the running version. Checks
/// are serialized; status() never waits on one.
class UpdateChecker {
public:
    UpdateChecker(std::unique_ptr<ReleaseSource> source, std::string currentVersion);

    UpdateCheckResult check();

    UpdateStatus status() const;

    /// The /api/update object: current, latest, update_available, release_url,
    /// release_notes, published_at, checked_at, last_error.
    nlohmann::json statusJson() const;

private:
    UpdateCheckResult fail(const std::string &error);

    std::unique_ptr<ReleaseSource> m_source;
    std::optional<SemVer> m_currentVersion;
    std::string m_etag;

    std::mutex m_checkMutex;
    mutable std::mutex m_statusMutex;
    UpdateStatus m_status;
};

/// The release list of a GitHub repository ("owner/name"), from
/// \p apiBase/repos/<repo>/releases?per_page=100.
class GitHubReleaseSource:public ReleaseSource {
public:
    explicit GitHubReleaseSource(std::string repo, std::string apiBase="https://api.github.com");

    ReleaseFetch fetch(const std::string &etag) override;

private:
    std::string m_url;
};

/// The "updates" block of the server config that this task reads.
struct UpdatePolicy {
    bool check=true;
    int intervalHours=24;
};

/// Reads check and interval_hours from an "updates" object, keeping the
/// defaults in \p policy for keys it lacks. False with \p error on a wrong type
/// or an interval_hours under 1.
bool readUpdatePolicy(const nlohmann::json &updates, UpdatePolicy &policy, std::string &error);

/// Puts the check and interval_hours of \p body (a PUT /api/server/config
/// "updates" object) into the "updates" object of \p cfg, leaving its other
/// keys alone; \p policy is the merged block's. False with \p error, and \p cfg
/// unchanged, when the request or the merged block is invalid.
bool mergeUpdatesConfig(nlohmann::json &cfg, const nlohmann::json &body, UpdatePolicy &policy, std::string &error);

/// The "updates" object of GET /api/server/config.
nlohmann::json updatePolicyJson(const UpdatePolicy &policy);

/// Runs an UpdateChecker on its own thread: once at start, then every
/// interval while the policy has check on. Nothing waits on that thread.
class UpdateManager {
public:
    UpdateManager(std::unique_ptr<ReleaseSource> source, std::string currentVersion);
    ~UpdateManager();

    /// How long an hour of interval_hours is (tests shorten it).
    void setIntervalUnit(std::chrono::milliseconds unit);

    void start(const UpdatePolicy &policy);
    void setPolicy(const UpdatePolicy &policy);
    UpdatePolicy policy() const;

    /// A check on the calling thread, whatever the policy; returns statusJson().
    nlohmann::json checkNow();
    nlohmann::json statusJson() const;

    /// Stops and joins the thread; waits for a check in flight.
    void stop();

private:
    void run();

    UpdateChecker m_checker;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    UpdatePolicy m_policy;
    std::chrono::milliseconds m_intervalUnit=std::chrono::hours(1);
    bool m_policyChanged=false;
    bool m_stopping=false;
    std::thread m_thread;
};

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_UPDATEMANAGER_H_
