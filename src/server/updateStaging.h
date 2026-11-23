#ifndef _ARBITERAI_SERVER_UPDATESTAGING_H_
#define _ARBITERAI_SERVER_UPDATESTAGING_H_

#include "updateManager.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace arbiterAI
{
namespace server
{

/// Fetches a release asset; cpr in the server, a local copy in tests.
class Downloader {
public:
    /// Called with the bytes received so far and the total (0 when unknown).
    using Progress=std::function<void(int64_t received, int64_t total)>;

    virtual ~Downloader()=default;

    virtual bool download(const std::string &url, const std::filesystem::path &file, const Progress &progress, std::string &error)=0;
};

class CprDownloader:public Downloader {
public:
    bool download(const std::string &url, const std::filesystem::path &file, const Progress &progress, std::string &error) override;
};

/// How a child process ended.
struct ProcessResult {
    bool started=false;
    bool timedOut=false;
    int exitCode=-1;   ///< when it exited
    int signal=0;      ///< when a signal ended it
    std::string output;///< stdout and stderr, the last 16 KiB
};

/// Runs \p argv (no shell) in \p cwd with an empty signal mask, default
/// signal handlers, stdin from /dev/null and no fds but 0-2; kills its process
/// group after \p timeout.
ProcessResult runProcess(const std::vector<std::string> &argv, const std::filesystem::path &cwd, std::chrono::milliseconds timeout);

/// The SHA-256 of a file, lower-case hex; empty with \p error when it cannot be read.
std::string sha256File(const std::filesystem::path &file, std::string &error);

/// <root>/.update
std::filesystem::path stagingDir(const std::filesystem::path &root);

enum class StageStep
{
    CheckingSpace,
    Downloading,
    Verifying,
    Unpacking,
    SelfTesting,
    Staged
};

/// "checking_space", "downloading", "verifying", "unpacking", "self_testing", "staged"
const char *stageStepName(StageStep step);

struct StageRequest {
    std::filesystem::path root;
    std::string currentVersion;
    std::string version;///< X.Y.Z, the release to stage
    ReleaseAsset asset;
    std::filesystem::path configPath;///< the running server's --config
    std::filesystem::path cwd;       ///< the running server's working directory
    std::chrono::milliseconds selfTestTimeout=std::chrono::minutes(2);
    /// Free bytes on the root's filesystem; std::filesystem::space when empty.
    std::function<uintmax_t(const std::filesystem::path &)> freeSpace;
};

struct StageResult {
    bool ok=false;
    StageStep step=StageStep::CheckingSpace;///< the step that failed, Staged on success
    std::filesystem::path staged;///< the unpacked release (bin/, share/) on success
    std::string error;
};

/// Called as the pipeline moves on; \p fraction is 0-1 within the step.
using StageProgress=std::function<void(StageStep step, double fraction)>;

/// Checks free space (3x the asset), downloads the asset to <root>/.update,
/// checks its SHA-256 against the digest, unpacks it, checks the staged
/// install.json version and runs the staged --self-test in the server's cwd.
/// The install is never touched. On a failure .update is removed and the
/// history gets last_error (and the version in failed when the release
/// itself is at fault: digest, contents or self-test).
StageResult stageRelease(const StageRequest &request, Downloader &downloader, const StageProgress &progress);

/// Removes <root>/.update.
void removeStaging(const std::filesystem::path &root);

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_UPDATESTAGING_H_
