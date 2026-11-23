#ifndef _ARBITERAI_SERVER_INSTALLER_H_
#define _ARBITERAI_SERVER_INSTALLER_H_

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace arbiterAI
{
namespace server
{

/// The server binary's name under <root>/bin.
extern const char *const serverBinaryName;

/// The install root of the executable at \p exe: the parent of its bin/
/// directory. Empty when \p exe is not directly in a directory named bin.
std::filesystem::path installRootFor(const std::filesystem::path &exe);

/// installRootFor(/proc/self/exe); resolve it before any swap moves the binary.
std::filesystem::path runningInstallRoot();

/// <root>/share/arbiterAI/install.json
std::filesystem::path installMarkerPath(const std::filesystem::path &root);

/// <root>/bin/arbiterAI-server
std::filesystem::path installedBinaryPath(const std::filesystem::path &root);

/// The "version" of the install marker under \p root; empty when there is
/// no readable marker.
std::string installedVersion(const std::filesystem::path &root);

/// The version of the install in <root>/share.prev; empty when there is none.
std::string previousVersion(const std::filesystem::path &root);

/// <root>/update-state.json: an update in probation.
struct UpdateState {
    std::string from;
    std::string to;
    nlohmann::json loadedModels=nlohmann::json::array();///< [{model, variant, context, runtime_options, devices, pinned}]
    int attempt=0;
};

/// The state under \p root. Nothing, with \p error empty, when there is none;
/// nothing with \p error set when the file cannot be read.
std::optional<UpdateState> readUpdateState(const std::filesystem::path &root, std::string &error);
bool writeUpdateState(const std::filesystem::path &root, const UpdateState &state, std::string &error);
bool removeUpdateState(const std::filesystem::path &root, std::string &error);

struct UpdateRecord {
    std::string from;
    std::string to;
    std::string outcome;///< "updated", "rolled_back", "failed"
    std::string time;
    std::string error;
};

/// <root>/update-history.json: kept across updates.
struct UpdateHistory {
    std::vector<UpdateRecord> updates;
    std::string lastError;
    std::string prevVersion;///< the version the running one was updated from
    std::vector<std::string> failed;

    bool isFailed(const std::string &version) const;
    void addFailed(const std::string &version);
};

/// The history under \p root; empty when there is none or it cannot be read.
UpdateHistory readUpdateHistory(const std::filesystem::path &root);
bool writeUpdateHistory(const std::filesystem::path &root, const UpdateHistory &history, std::string &error);
/// Held around each read-modify-write of the history: the health timer, the
/// stop handler and the applier thread all update it.
std::recursive_mutex &updateHistoryMutex();

/// Moves <root>/bin and <root>/share to bin.prev and share.prev (replacing
/// older ones), then <staged>/bin and <staged>/share into their place. On a
/// failure what was moved is moved back.
bool swapIn(const std::filesystem::path &root, const std::filesystem::path &staged, std::string &error);

/// True when <root>/bin.prev exists.
bool hasPrevious(const std::filesystem::path &root);

/// Moves bin.prev and share.prev back into place and deletes the bin and
/// share they replace.
bool swapBack(const std::filesystem::path &root, std::string &error);

/// Replaces the process with another binary; execv in the server.
class Restarter {
public:
    virtual ~Restarter()=default;

    /// Only returns on a failure, with \p error set.
    virtual void restart(const std::filesystem::path &binary, const std::vector<std::string> &args, std::string &error)=0;
};

/// Marks every fd above stderr close-on-exec (log files, GPU devices, sockets
/// a library opened without it), then execv.
class ExecRestarter:public Restarter {
public:
    void restart(const std::filesystem::path &binary, const std::vector<std::string> &args, std::string &error) override;
};

enum class ProbationOutcome
{
    None,      ///< no update in probation
    Discarded, ///< the state was for another version: the swap never happened
    Started,   ///< first start of the new version: attempt is now 1
    Pending,   ///< attempt 0 left alone (seed mode)
    RolledBack,///< the new version had started before: *.prev is back
    Failed     ///< a rollback was due but could not be done
};

struct ProbationResult {
    ProbationOutcome outcome=ProbationOutcome::None;
    UpdateState state;
    std::string message;
};

/// The update-state check for \p version, the version installed under \p root.
/// \p startingServer: the server is about to start, so a first attempt is
/// counted; otherwise (--seed) an attempt 0 state is left for the server.
ProbationResult checkProbation(const std::filesystem::path &root, const std::string &version, bool startingServer);

/// checkProbation() at the top of main: after a rollback, restarts
/// <root>/bin/arbiterAI-server with \p args. Returns only when the process
/// should carry on (or the restart failed: then outcome is Failed).
ProbationResult runStartupProbation(const std::filesystem::path &root, const std::string &version,
    Restarter &restarter, const std::vector<std::string> &args);

/// The version under probation is healthy: deletes the state file when its
/// "to" is \p version and records the update. False when there was none.
bool markHealthy(const std::filesystem::path &root, const std::string &version);

enum class SeedOutcome
{
    Installed, ///< the volume was empty: the image's install was copied in
    Updated,   ///< the image is newer: swapped in with .prev, in probation
    Kept,      ///< the volume has the same or a newer version
    KeptFailed,///< the image's version failed on this volume before
    Failed     ///< nothing could be done; see message
};

struct SeedResult {
    SeedOutcome outcome=SeedOutcome::Failed;
    std::string imageVersion;
    std::string volumeVersion;///< before seeding
    std::string message;
};

/// --seed: brings the install at \p root (the Docker volume) up to the one at
/// \p image (the image's own install). First rolls back a version that
/// failed its probation (attempt >= 1; an attempt 0 is left for the server).
/// Then an empty volume gets a copy; a newer image, unless its version is in
/// failed, is swapped in with .prev and an attempt 0 state, so a crash rolls
/// it back; anything else is left alone.
SeedResult seedInstall(const std::filesystem::path &image, const std::filesystem::path &root);

/// markHealthy() once the server has served for a while.
class HealthTimer {
public:
    ~HealthTimer();

    void start(std::filesystem::path root, std::string version, std::chrono::milliseconds delay);

    /// Stops the timer without marking anything healthy.
    void stop();

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stopping=false;
    std::thread m_thread;
};

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_INSTALLER_H_
