#ifndef _ARBITERAI_SERVER_UPDATEAPPLIER_H_
#define _ARBITERAI_SERVER_UPDATEAPPLIER_H_

#include "installEnvironment.h"
#include "updateManager.h"
#include "updateStaging.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace arbiterAI
{
namespace server
{

struct ApplierConfig {
    std::filesystem::path root;
    std::string currentVersion;
    std::filesystem::path configPath;
    std::filesystem::path cwd;
    std::chrono::milliseconds selfTestTimeout=std::chrono::minutes(2);
    int drainTimeoutSeconds=60;///< reported for the dashboard's confirm dialog
};

/// Installs the latest release (POST /api/update/apply) or goes back to
/// <root>/bin.prev (POST /api/update/rollback) on a worker thread. Once the
/// install is swapped it asks the server to drain and restart; from then on
/// it refuses anything else.
class UpdateApplier {
public:
    /// The loaded models, as update-state.json's loaded_models.
    using ModelsSnapshot=std::function<nlohmann::json()>;
    /// Drains the server and restarts it from <root>/bin; called on the worker thread.
    using RestartRequest=std::function<void()>;

    enum class Start
    {
        Started,
        Busy,      ///< an update or rollback is running (409)
        Blocked,   ///< can_apply is false (403)
        NotLatest, ///< not the latest release, or not newer than the running version (409)
        NoAsset,   ///< the latest release has no server tarball (409)
        NoPrevious ///< no bin.prev to roll back to (409)
    };

    UpdateApplier(ApplierConfig config, std::unique_ptr<Downloader> downloader, ModelsSnapshot models, RestartRequest restart);
    ~UpdateApplier();

    /// Stages \p version, which must be \p latest, then swaps it in and restarts.
    Start apply(const std::string &version, const std::optional<ReleaseInfo> &latest, const ApplyCheck &check, std::string &message);

    /// Swaps bin.prev and share.prev back and restarts.
    Start rollback(const ApplyCheck &check, std::string &message);

    bool busy() const;

    /// state, progress, prev_version, update_error, rollback_available,
    /// rollback_version, drain_timeout_s.
    nlohmann::json statusJson() const;

    /// Waits for the worker thread (tests).
    void wait();

private:
    void runApply(std::string version, ReleaseAsset asset);
    void runRollback(std::string previous);
    void setState(const std::string &state, std::optional<double> progress);
    void finishFailed(const std::string &error);

    ApplierConfig m_config;
    std::unique_ptr<Downloader> m_downloader;
    ModelsSnapshot m_models;
    RestartRequest m_restart;

    mutable std::mutex m_mutex;
    bool m_busy=false;
    std::string m_state="idle";
    std::optional<double> m_progress;
    std::thread m_worker;
};

/// The HTTP status for an UpdateApplier::Start that is not Started.
int applyStartStatus(UpdateApplier::Start start);

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_UPDATEAPPLIER_H_
