#include "updateApplier.h"
#include "installer.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <ctime>

namespace arbiterAI
{
namespace server
{

namespace
{

namespace fs=std::filesystem;

std::string nowUtc()
{
    std::time_t seconds=std::time(nullptr);
    std::tm utc{};
    gmtime_r(&seconds, &utc);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

/// When this process started, to the millisecond: execv keeps the PID, so
/// this is how the dashboard tells the restarted server from the old one.
std::string processStartedAt()
{
    static const std::string startedAt=[]()
    {
        std::timespec now{};
        clock_gettime(CLOCK_REALTIME, &now);
        std::tm utc{};
        gmtime_r(&now.tv_sec, &utc);

        char buffer[48];
        size_t length=std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &utc);
        std::snprintf(buffer+length, sizeof(buffer)-length, ".%03ldZ", now.tv_nsec/1000000);
        return std::string(buffer);
    }();
    return startedAt;
}

/// Where each stage sits in the overall progress, as [start, end).
struct StepSpan {
    double start;
    double end;
};

StepSpan stepSpan(StageStep step)
{
    switch(step)
    {
    case StageStep::CheckingSpace:
        return {0.0, 0.02};
    case StageStep::Downloading:
        return {0.02, 0.80};
    case StageStep::Verifying:
        return {0.80, 0.84};
    case StageStep::Unpacking:
        return {0.84, 0.88};
    case StageStep::SelfTesting:
        return {0.88, 0.97};
    case StageStep::Staged:
        return {0.97, 0.97};
    }
    return {0.0, 0.0};
}

std::string stripV(const std::string &version)
{
    return !version.empty() && version[0] == 'v'?version.substr(1):version;
}

} // anonymous namespace

UpdateApplier::UpdateApplier(ApplierConfig config, std::unique_ptr<Downloader> downloader, ModelsSnapshot models, RestartRequest restart):
    m_config(std::move(config)),
    m_downloader(std::move(downloader)),
    m_models(std::move(models)),
    m_restart(std::move(restart))
{
    processStartedAt();
}

UpdateApplier::~UpdateApplier()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping=true;
    }
    m_wake.notify_all();
    wait();
}

UpdateApplier::Start UpdateApplier::apply(const std::string &version, const std::optional<ReleaseInfo> &latest,
    const ApplyCheck &check, std::string &message, BusyProbe waitForIdle)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_busy)
    {
        message="an update is already running ("+m_state+")";
        return Start::Busy;
    }
    if(!check.canApply)
    {
        message=check.blockedReason;
        return Start::Blocked;
    }
    if(!latest)
    {
        message="no release is known yet: check for updates first";
        return Start::NotLatest;
    }

    std::string wanted=stripV(version);
    std::string latestVersion=latest->version.toString();
    if(wanted != latestVersion)
    {
        message="only the latest release (v"+latestVersion+") can be installed";
        return Start::NotLatest;
    }
    std::optional<SemVer> current=SemVer::parse(m_config.currentVersion, false);
    if(current && !(*current<latest->version))
    {
        message="v"+latestVersion+" is not newer than the running v"+m_config.currentVersion;
        return Start::NotLatest;
    }
    if(!latest->serverAsset || latest->serverAsset->url.empty())
    {
        message="v"+latestVersion+" has no "+serverAssetName(latest->tag)+" asset";
        return Start::NoAsset;
    }

    if(m_worker.joinable())
        m_worker.join();
    m_busy=true;
    m_state=stageStepName(StageStep::CheckingSpace);
    m_progress=0.0;
    m_worker=std::thread(&UpdateApplier::runApply, this, latestVersion, *latest->serverAsset, std::move(waitForIdle));
    message="installing v"+latestVersion;
    spdlog::info("Update: installing v{} (from v{})", latestVersion, m_config.currentVersion);
    return Start::Started;
}

UpdateApplier::Start UpdateApplier::rollback(const ApplyCheck &check, std::string &message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_busy)
    {
        message="an update is already running ("+m_state+")";
        return Start::Busy;
    }
    if(!check.canApply)
    {
        message=check.blockedReason;
        return Start::Blocked;
    }
    if(!hasPrevious(m_config.root))
    {
        message="there is no previous version to roll back to";
        return Start::NoPrevious;
    }

    std::string previous=previousVersion(m_config.root);
    if(m_worker.joinable())
        m_worker.join();
    m_busy=true;
    m_state="rolling_back";
    m_progress.reset();
    m_worker=std::thread(&UpdateApplier::runRollback, this, previous);
    message="rolling back to v"+previous;
    spdlog::info("Update: rolling back from v{} to v{}", m_config.currentVersion, previous);
    return Start::Started;
}

bool UpdateApplier::busy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_busy;
}

nlohmann::json UpdateApplier::statusJson() const
{
    nlohmann::json result;
    bool busy=false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        busy=m_busy;
        result["state"]=m_state;
        result["progress"]=m_progress?nlohmann::json(*m_progress):nlohmann::json(nullptr);
    }

    UpdateHistory history=readUpdateHistory(m_config.root);
    std::string error;
    std::optional<UpdateState> probation=readUpdateState(m_config.root, error);

    // In probation the history has no record of this update yet.
    std::string prev=history.prevVersion;
    if(probation && probation->to == m_config.currentVersion)
        prev=probation->from;

    result["prev_version"]=prev.empty()?nlohmann::json(nullptr):nlohmann::json(prev);
    result["update_error"]=history.lastError.empty()?nlohmann::json(nullptr):nlohmann::json(history.lastError);

    std::string previous=hasPrevious(m_config.root)?previousVersion(m_config.root):std::string();
    result["rollback_available"]=!busy && hasPrevious(m_config.root);
    result["rollback_version"]=previous.empty()?nlohmann::json(nullptr):nlohmann::json(previous);
    result["drain_timeout_s"]=m_config.drainTimeoutSeconds;
    result["started_at"]=processStartedAt();
    return result;
}

void UpdateApplier::wait()
{
    if(m_worker.joinable())
        m_worker.join();
}

void UpdateApplier::setState(const std::string &state, std::optional<double> progress)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state=state;
    m_progress=progress;
}

void UpdateApplier::finishFailed(const std::string &error)
{
    if(!error.empty())
        spdlog::error("Update: {}", error);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_busy=false;
    m_state="idle";
    m_progress.reset();
}

bool UpdateApplier::waitUntilIdle(const BusyProbe &busy, std::string &reason)
{
    const std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+m_config.idleWaitTimeout;
    while(true)
    {
        reason=busy();
        if(reason.empty())
            return true;
        if(std::chrono::steady_clock::now()>=deadline)
            return false;

        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_wake.wait_for(lock, m_config.idlePollInterval, [this]() { return m_stopping; }))
        {
            reason="the server is stopping";
            return false;
        }
    }
}

void UpdateApplier::runApply(std::string version, ReleaseAsset asset, BusyProbe waitForIdle)
{
    StageRequest request;
    request.root=m_config.root;
    request.currentVersion=m_config.currentVersion;
    request.version=version;
    request.asset=asset;
    request.configPath=m_config.configPath;
    request.cwd=m_config.cwd;
    request.selfTestTimeout=m_config.selfTestTimeout;

    StageResult staged=stageRelease(request, *m_downloader, [this](StageStep step, double fraction)
    {
        StepSpan span=stepSpan(step);
        setState(stageStepName(step), span.start+(span.end-span.start)*fraction);
    });
    if(!staged.ok)
    {
        // stageRelease has recorded the failure and removed .update.
        finishFailed(std::string());
        return;
    }

    if(waitForIdle)
    {
        setState("waiting_idle", 0.97);
        std::string busy;
        if(!waitUntilIdle(waitForIdle, busy))
        {
            removeStaging(m_config.root);
            finishFailed("auto-update to v"+version+" put off, the server is still busy after staging: "+busy);
            return;
        }
    }

    setState("installing", 0.98);

    UpdateState state;
    state.from=m_config.currentVersion;
    state.to=version;
    state.loadedModels=m_models?m_models():nlohmann::json::array();
    state.attempt=0;

    std::string error;
    bool installed=writeUpdateState(m_config.root, state, error);
    if(installed && !swapIn(m_config.root, staged.staged, error))
    {
        installed=false;
        std::string removeError;
        removeUpdateState(m_config.root, removeError);
    }
    removeStaging(m_config.root);

    if(!installed)
    {
        std::lock_guard<std::recursive_mutex> historyLock(updateHistoryMutex());
        UpdateHistory history=readUpdateHistory(m_config.root);
        history.lastError="installing v"+version+" failed: "+error;
        history.updates.push_back({m_config.currentVersion, version, "failed", nowUtc(), history.lastError});
        std::string writeError;
        if(!writeUpdateHistory(m_config.root, history, writeError))
            spdlog::warn("Update: {}", writeError);
        finishFailed(history.lastError);
        return;
    }

    spdlog::info("Update: v{} installed in {}, restarting", version, m_config.root.string());
    setState("restarting", 1.0);
    if(m_restart)
        m_restart();
}

void UpdateApplier::runRollback(std::string previous)
{
    std::lock_guard<std::recursive_mutex> historyLock(updateHistoryMutex());
    const UpdateHistory before=readUpdateHistory(m_config.root);
    UpdateHistory history=before;
    // Skipped by auto-update from now on, like a version that failed to start.
    history.addFailed(m_config.currentVersion);
    history.prevVersion.clear();
    history.lastError.clear();
    // the update that is undone, as a crash rollback records it
    history.updates.push_back({previous, m_config.currentVersion, "rolled_back", nowUtc(), std::string()});

    // Written before the swap: a crash between the two must not lose the
    // failed mark, or --seed would install this version again.
    std::string error;
    if(!writeUpdateHistory(m_config.root, history, error))
        spdlog::warn("Update: {}", error);
    if(!swapBack(m_config.root, error))
    {
        history=before;
        history.lastError="rolling back to v"+previous+" failed: "+error;
        std::string writeError;
        if(!writeUpdateHistory(m_config.root, history, writeError))
            spdlog::warn("Update: {}", writeError);
        finishFailed(history.lastError);
        return;
    }
    if(!removeUpdateState(m_config.root, error))
        spdlog::warn("Update: {}", error);

    spdlog::info("Update: rolled back to v{}, restarting", previous);
    setState("restarting", std::nullopt);
    if(m_restart)
        m_restart();
}

int applyStartStatus(UpdateApplier::Start start)
{
    switch(start)
    {
    case UpdateApplier::Start::Started:
        return 202;
    case UpdateApplier::Start::Blocked:
        return 403;
    default:
        return 409;
    }
}

} // namespace server
} // namespace arbiterAI
