#ifndef _ARBITERAI_SERVER_UPDATEMANAGER_H_
#define _ARBITERAI_SERVER_UPDATEMANAGER_H_

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

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

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_UPDATEMANAGER_H_
