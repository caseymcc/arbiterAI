#ifndef _ARBITERAI_SERVER_AUTOUPDATE_H_
#define _ARBITERAI_SERVER_AUTOUPDATE_H_

#include "installEnvironment.h"
#include "updateApplier.h"
#include "updateManager.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace arbiterAI
{
namespace server
{

using SystemTime=std::chrono::system_clock::time_point;

/// What one auto-update decision is made from.
struct AutoInputs {
    UpdatePolicy policy;
    std::string currentVersion;
    std::optional<ReleaseInfo> latest;
    std::vector<std::string> failed;
    ApplyCheck check;
    Supervisor supervisor=Supervisor::None;
    bool applierBusy=false;
    SystemTime now;
    int localMinuteOfDay=0;          ///< minutes after local midnight at now
    std::string busy;                ///< what is running right now ("2 HTTP requests"); empty when idle
    std::optional<SystemTime> idleSince;///< since when nothing has run; none while busy
    std::optional<SystemTime> retryAfter;///< no new attempt before this (after a failed one)
};

struct AutoDecision {
    bool apply=false;
    bool enabled=false;              ///< auto_apply is on and nothing turns it off
    std::string status;
    std::optional<SystemTime> nextAttempt;
};

/// Whether to install latest now and, if not, what auto-update waits for.
/// It installs only when auto_apply and allow_apply are on, a supervisor is
/// declared, latest is newer and within auto_apply_scope, not in failed,
/// can_apply holds, nothing else is being installed, latest was published
/// auto_apply_delay_hours ago, now is inside auto_apply_window and the server
/// has been idle for idle_minutes.
AutoDecision decideAutoUpdate(const AutoInputs &inputs);

/// "2026-11-20T12:00:00Z"; nothing when \p text is not that.
std::optional<SystemTime> parseUtcTime(const std::string &text);

/// Runs decideAutoUpdate every interval on its own thread, and the apply it asks for.
class AutoUpdater {
public:
    struct Sources {
        std::function<UpdatePolicy()> policy;
        std::function<UpdateStatus()> status;
        std::function<std::vector<std::string>()> failed;
        std::function<ApplyCheck()> check;
        std::function<Supervisor()> supervisor;
        /// What is running right now; empty when the server is idle.
        std::function<std::string()> busy;
        std::function<SystemTime()> now;
        std::function<int(SystemTime)> localMinuteOfDay;
    };

    AutoUpdater(Sources sources, UpdateApplier &applier, std::string currentVersion);
    ~AutoUpdater();

    /// How long to wait after a failed auto attempt before the next one.
    void setRetryDelay(std::chrono::seconds delay);

    void start(std::chrono::milliseconds interval);
    void stop();

    /// One decision, and the apply when it says so. The thread calls it; so do tests.
    AutoDecision tick();

    /// {enabled, status, next_attempt}
    nlohmann::json statusJson() const;

private:
    void run(std::chrono::milliseconds interval);

    Sources m_sources;
    UpdateApplier &m_applier;
    std::string m_currentVersion;
    std::chrono::seconds m_retryDelay=std::chrono::hours(1);

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stopping=false;
    std::thread m_thread;

    std::optional<SystemTime> m_idleSince;
    std::optional<SystemTime> m_retryAfter;
    std::string m_attempted;///< the version of the auto apply in flight
    AutoDecision m_last;
};

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_AUTOUPDATE_H_
