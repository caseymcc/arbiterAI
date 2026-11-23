#include "installer.h"
#include "updateManager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>

#include <fcntl.h>
#include <unistd.h>

namespace arbiterAI
{
namespace server
{

const char *const serverBinaryName="arbiterAI-server";

namespace
{

namespace fs=std::filesystem;

const char *const stateFileName="update-state.json";
const char *const historyFileName="update-history.json";

std::string stringField(const nlohmann::json &object, const char *key)
{
    nlohmann::json::const_iterator it=object.find(key);
    if(it == object.end() || !it->is_string())
        return std::string();
    return it->get<std::string>();
}

std::string nowUtc()
{
    std::time_t seconds=std::time(nullptr);
    std::tm utc{};
    gmtime_r(&seconds, &utc);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

bool readJsonFile(const fs::path &path, nlohmann::json &value, std::string &error)
{
    std::ifstream file(path);
    if(!file.is_open())
    {
        error="cannot open "+path.string();
        return false;
    }

    value=nlohmann::json::parse(file, nullptr, false);
    if(value.is_discarded() || !value.is_object())
    {
        error=path.string()+" is not a JSON object";
        return false;
    }
    return true;
}

/// Writes next to \p path and renames over it, so a crash leaves the old file or the new one.
bool writeJsonFile(const fs::path &path, const nlohmann::json &value, std::string &error)
{
    fs::path temp=path;
    temp+=".tmp";

    {
        std::ofstream file(temp, std::ios::trunc);
        if(!file.is_open())
        {
            error="cannot write "+temp.string();
            return false;
        }
        // an error text cut inside a UTF-8 sequence must not throw
        file<<value.dump(4, ' ', false, nlohmann::json::error_handler_t::replace)<<"\n";
        file.flush();
        if(!file.good())
        {
            error="cannot write "+temp.string();
            return false;
        }
    }

    std::error_code ec;
    fs::rename(temp, path, ec);
    if(ec)
    {
        error="cannot rename "+temp.string()+" to "+path.string()+": "+ec.message();
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool movePath(const fs::path &from, const fs::path &to, std::string &error)
{
    std::error_code ec;
    fs::rename(from, to, ec);
    if(ec)
    {
        error="cannot move "+from.string()+" to "+to.string()+": "+ec.message();
        return false;
    }
    return true;
}

bool removeTree(const fs::path &path, std::string &error)
{
    std::error_code ec;
    fs::remove_all(path, ec);
    if(ec)
    {
        error="cannot remove "+path.string()+": "+ec.message();
        return false;
    }
    return true;
}

bool isDirectory(const fs::path &path)
{
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool pathExists(const fs::path &path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

/// A rename of \p from to \p to that move() has done.
struct Move {
    fs::path from;
    fs::path to;
};

/// Runs renames in order; on a failure undoes the done ones in reverse.
bool moveAll(const std::vector<Move> &moves, std::string &error)
{
    for(size_t i=0; i<moves.size(); ++i)
    {
        if(movePath(moves[i].from, moves[i].to, error))
            continue;

        for(size_t j=i; j>0; --j)
        {
            std::string undoError;
            if(!movePath(moves[j-1].to, moves[j-1].from, undoError))
                spdlog::error("Install swap: {}", undoError);
        }
        return false;
    }
    return true;
}

std::string displayVersion(const std::string &version)
{
    return "v"+version;
}

} // anonymous namespace

fs::path installRootFor(const fs::path &exe)
{
    fs::path dir=exe.parent_path();
    if(dir.filename() != "bin")
        return fs::path();
    return dir.parent_path();
}

fs::path runningInstallRoot()
{
    std::error_code ec;
    fs::path exe=fs::read_symlink("/proc/self/exe", ec);
    if(ec)
        return fs::path();
    return installRootFor(exe);
}

fs::path installMarkerPath(const fs::path &root)
{
    return root/"share"/"arbiterAI"/"install.json";
}

fs::path installedBinaryPath(const fs::path &root)
{
    return root/"bin"/serverBinaryName;
}

std::string installedVersion(const fs::path &root)
{
    nlohmann::json marker;
    std::string error;
    if(!readJsonFile(installMarkerPath(root), marker, error))
        return std::string();
    return stringField(marker, "version");
}

std::string previousVersion(const fs::path &root)
{
    nlohmann::json marker;
    std::string error;
    if(!readJsonFile(root/"share.prev"/"arbiterAI"/"install.json", marker, error))
        return std::string();
    return stringField(marker, "version");
}

std::optional<UpdateState> readUpdateState(const fs::path &root, std::string &error)
{
    error.clear();

    fs::path path=root/stateFileName;
    std::error_code ec;
    if(!fs::exists(path, ec))
        return std::nullopt;

    nlohmann::json value;
    if(!readJsonFile(path, value, error))
        return std::nullopt;

    UpdateState state;
    state.from=stringField(value, "from");
    state.to=stringField(value, "to");
    nlohmann::json::const_iterator models=value.find("loaded_models");
    if(models != value.end() && models->is_array())
        state.loadedModels=*models;
    nlohmann::json::const_iterator attempt=value.find("attempt");
    if(attempt != value.end() && attempt->is_number_integer())
        state.attempt=attempt->get<int>();
    return state;
}

bool writeUpdateState(const fs::path &root, const UpdateState &state, std::string &error)
{
    nlohmann::json value={
        {"from", state.from},
        {"to", state.to},
        {"loaded_models", state.loadedModels},
        {"attempt", state.attempt}
    };
    return writeJsonFile(root/stateFileName, value, error);
}

bool removeUpdateState(const fs::path &root, std::string &error)
{
    std::error_code ec;
    fs::remove(root/stateFileName, ec);
    if(ec)
    {
        error="cannot remove "+(root/stateFileName).string()+": "+ec.message();
        return false;
    }
    return true;
}

bool UpdateHistory::isFailed(const std::string &version) const
{
    return std::find(failed.begin(), failed.end(), version) != failed.end();
}

void UpdateHistory::addFailed(const std::string &version)
{
    if(!version.empty() && !isFailed(version))
        failed.push_back(version);
}

std::recursive_mutex &updateHistoryMutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

UpdateHistory readUpdateHistory(const fs::path &root)
{
    UpdateHistory history;

    fs::path path=root/historyFileName;
    std::error_code ec;
    if(!fs::exists(path, ec))
        return history;

    nlohmann::json value;
    std::string error;
    if(!readJsonFile(path, value, error))
    {
        spdlog::warn("Ignoring the update history: {}", error);
        return history;
    }

    history.lastError=stringField(value, "last_error");
    history.prevVersion=stringField(value, "prev_version");

    nlohmann::json::const_iterator failed=value.find("failed");
    if(failed != value.end() && failed->is_array())
    {
        for(const nlohmann::json &version:*failed)
        {
            if(version.is_string())
                history.addFailed(version.get<std::string>());
        }
    }

    nlohmann::json::const_iterator updates=value.find("updates");
    if(updates != value.end() && updates->is_array())
    {
        for(const nlohmann::json &entry:*updates)
        {
            if(!entry.is_object())
                continue;

            UpdateRecord record;
            record.from=stringField(entry, "from");
            record.to=stringField(entry, "to");
            record.outcome=stringField(entry, "outcome");
            record.time=stringField(entry, "time");
            record.error=stringField(entry, "error");
            history.updates.push_back(record);
        }
    }
    return history;
}

bool writeUpdateHistory(const fs::path &root, const UpdateHistory &history, std::string &error)
{
    nlohmann::json updates=nlohmann::json::array();
    size_t first=history.updates.size()>maxHistoryRecords?history.updates.size()-maxHistoryRecords:0;
    for(size_t i=first; i<history.updates.size(); ++i)
    {
        const UpdateRecord &record=history.updates[i];
        nlohmann::json entry={
            {"from", record.from},
            {"to", record.to},
            {"outcome", record.outcome},
            {"time", record.time}
        };
        if(!record.error.empty())
            entry["error"]=record.error;
        updates.push_back(entry);
    }

    nlohmann::json value={
        {"updates", updates},
        {"last_error", history.lastError},
        {"prev_version", history.prevVersion},
        {"failed", history.failed}
    };
    return writeJsonFile(root/historyFileName, value, error);
}

bool swapIn(const fs::path &root, const fs::path &staged, std::string &error)
{
    if(!isDirectory(staged/"bin") || !isDirectory(staged/"share"))
    {
        error=staged.string()+" has no bin/ and share/";
        return false;
    }
    if(!isDirectory(root/"bin"))
    {
        error=root.string()+" has no bin/";
        return false;
    }

    if(!removeTree(root/"bin.prev", error) || !removeTree(root/"share.prev", error))
        return false;

    std::vector<Move> moves;
    moves.push_back({root/"bin", root/"bin.prev"});
    if(isDirectory(root/"share"))
        moves.push_back({root/"share", root/"share.prev"});
    moves.push_back({staged/"bin", root/"bin"});
    moves.push_back({staged/"share", root/"share"});
    return moveAll(moves, error);
}

bool hasPrevious(const fs::path &root)
{
    return isDirectory(root/"bin.prev");
}

bool swapBack(const fs::path &root, std::string &error)
{
    if(!hasPrevious(root))
    {
        error="there is no "+(root/"bin.prev").string()+" to roll back to";
        return false;
    }

    if(!removeTree(root/"bin.failed", error) || !removeTree(root/"share.failed", error))
        return false;

    std::vector<Move> moves;
    if(isDirectory(root/"bin"))
        moves.push_back({root/"bin", root/"bin.failed"});
    if(isDirectory(root/"share.prev"))
    {
        if(isDirectory(root/"share"))
            moves.push_back({root/"share", root/"share.failed"});
        moves.push_back({root/"share.prev", root/"share"});
    }
    moves.push_back({root/"bin.prev", root/"bin"});
    if(!moveAll(moves, error))
        return false;

    std::string cleanupError;
    if(!removeTree(root/"bin.failed", cleanupError) || !removeTree(root/"share.failed", cleanupError))
        spdlog::warn("Rollback: {}", cleanupError);
    return true;
}

void ExecRestarter::restart(const fs::path &binary, const std::vector<std::string> &args, std::string &error)
{
    std::string path=binary.string();
    std::vector<char *> argv;
    argv.push_back(path.data());
    for(size_t i=1; i<args.size(); ++i)
        argv.push_back(const_cast<char *>(args[i].c_str()));
    argv.push_back(nullptr);

    std::error_code ec;
    for(const fs::directory_entry &entry:fs::directory_iterator("/proc/self/fd", ec))
    {
        int fd=std::atoi(entry.path().filename().c_str());
        if(fd>2)
            fcntl(fd, F_SETFD, fcntl(fd, F_GETFD)|FD_CLOEXEC);
    }

    spdlog::default_logger()->flush();
    execv(path.c_str(), argv.data());
    error="execv "+path+": "+std::strerror(errno);
}

ProbationResult checkProbation(const fs::path &root, const std::string &version, bool startingServer)
{
    ProbationResult result;

    std::string error;
    std::optional<UpdateState> state=readUpdateState(root, error);
    if(!state)
    {
        if(!error.empty())
        {
            // Unreadable: treat it as a swap that never completed.
            removeUpdateState(root, error);
            result.outcome=ProbationOutcome::Discarded;
            result.message="discarded an unreadable update state";
        }
        return result;
    }
    result.state=*state;

    if(state->to != version)
    {
        removeUpdateState(root, error);
        result.outcome=ProbationOutcome::Discarded;
        result.message="discarded the update state for "+displayVersion(state->to)+": "+displayVersion(version)+" is installed";
        return result;
    }

    if(state->attempt <= 0)
    {
        if(!startingServer)
        {
            result.outcome=ProbationOutcome::Pending;
            result.message=displayVersion(version)+" has not started since the update";
            return result;
        }

        UpdateState started=*state;
        started.attempt=1;
        if(!writeUpdateState(root, started, error))
            spdlog::warn("Update probation: {}", error);
        result.state=started;
        result.outcome=ProbationOutcome::Started;
        result.message="first start of "+displayVersion(version)+" after the update from "+displayVersion(state->from);
        return result;
    }

    // The history is written before the swap: a crash in between keeps the
    // failed mark (so --seed does not install this version again), and the
    // state file left behind rolls back again on the next start.
    std::lock_guard<std::recursive_mutex> historyLock(updateHistoryMutex());
    UpdateHistory history=readUpdateHistory(root);
    history.lastError=displayVersion(version)+" failed to start, rolled back";
    history.prevVersion.clear();
    history.addFailed(version);
    history.updates.push_back({state->from, state->to, "rolled_back", nowUtc(), history.lastError});
    if(!writeUpdateHistory(root, history, error))
        spdlog::warn("Update probation: {}", error);

    std::string swapError;
    bool swapped=swapBack(root, swapError);
    if(!swapped)
    {
        history.lastError=displayVersion(version)+" failed to start and could not be rolled back: "+swapError;
        history.updates.back().outcome="failed";
        history.updates.back().error=history.lastError;
        if(!writeUpdateHistory(root, history, error))
            spdlog::warn("Update probation: {}", error);
    }

    if(!removeUpdateState(root, error))
        spdlog::warn("Update probation: {}", error);

    result.outcome=swapped?ProbationOutcome::RolledBack:ProbationOutcome::Failed;
    result.message=history.lastError;
    return result;
}

ProbationResult runStartupProbation(const fs::path &root, const std::string &version,
    Restarter &restarter, const std::vector<std::string> &args)
{
    ProbationResult result=checkProbation(root, version, true);
    if(result.outcome != ProbationOutcome::RolledBack)
        return result;

    spdlog::warn("Update probation: {}; restarting {}", result.message, installedBinaryPath(root).string());
    std::string error;
    restarter.restart(installedBinaryPath(root), args, error);

    result.outcome=ProbationOutcome::Failed;
    result.message+="; restarting the old version failed: "+error;
    return result;
}

bool markHealthy(const fs::path &root, const std::string &version)
{
    // the HealthTimer and the stop handler may both get here: one records it
    std::lock_guard<std::recursive_mutex> historyLock(updateHistoryMutex());
    std::string error;
    std::optional<UpdateState> state=readUpdateState(root, error);
    if(!state || state->to != version)
        return false;

    UpdateHistory history=readUpdateHistory(root);
    history.lastError.clear();
    history.prevVersion=state->from;
    history.updates.push_back({state->from, state->to, "updated", nowUtc(), std::string()});
    if(!writeUpdateHistory(root, history, error))
        spdlog::warn("Update probation: {}", error);

    if(!removeUpdateState(root, error))
    {
        spdlog::warn("Update probation: {}", error);
        return false;
    }
    return true;
}

namespace
{

/// Copies <image>/bin and <image>/share to <root>/.update/seed, keeping modes.
bool copyInstall(const fs::path &image, const fs::path &root, fs::path &seed, std::string &error)
{
    seed=root/".update"/"seed";
    if(!removeTree(root/".update", error))
        return false;

    std::error_code ec;
    fs::create_directories(seed, ec);
    for(const char *dir:{"bin", "share"})
    {
        if(ec)
            break;
        fs::copy(image/dir, seed/dir, fs::copy_options::recursive|fs::copy_options::copy_symlinks, ec);
    }
    if(ec)
    {
        error="cannot copy "+image.string()+" to "+seed.string()+": "+ec.message();
        std::string cleanupError;
        removeTree(root/".update", cleanupError);
        return false;
    }
    return true;
}

/// A newer image that could not be installed (a full volume, say) leaves
/// the volume's own install, which still starts.
SeedResult keptOnError(const fs::path &root, SeedResult result, const std::string &error)
{
    result.message="cannot install v"+result.imageVersion+": "+error;
    if(pathExists(installedBinaryPath(root)) && installedVersion(root) == result.volumeVersion)
    {
        result.outcome=SeedOutcome::KeptOnError;
        result.message="kept v"+result.volumeVersion+", "+result.message;
    }
    return result;
}

} // anonymous namespace

SeedResult seedInstall(const fs::path &image, const fs::path &root)
{
    SeedResult result;
    result.imageVersion=installedVersion(image);
    std::optional<SemVer> imageVersion=SemVer::parse(result.imageVersion, false);
    if(!imageVersion || !pathExists(installedBinaryPath(image)))
    {
        result.message=image.empty()
            ?"this binary is not in a release install's bin/ directory"
            :"no release install at "+image.string()+" (bin/, share/arbiterAI/install.json)";
        return result;
    }

    std::error_code ec;
    fs::create_directories(root, ec);
    if(ec)
    {
        result.message="cannot create "+root.string()+": "+ec.message();
        return result;
    }

    // The rollback half of probation; attempt 0 stays for the server.
    std::string volume=installedVersion(root);
    if(!volume.empty())
    {
        ProbationResult probation=checkProbation(root, volume, false);
        if(probation.outcome != ProbationOutcome::None)
            spdlog::info("Seed: {}", probation.message);
    }

    // A marker without a binary is a copy that never finished.
    bool installed=pathExists(installedBinaryPath(root)) && !installedVersion(root).empty();
    result.volumeVersion=installed?installedVersion(root):std::string();

    std::string error;
    fs::path seed;
    if(!installed)
    {
        if(!copyInstall(image, root, seed, error)
            || !removeTree(root/"bin", error) || !removeTree(root/"share", error)
            || !moveAll({{seed/"share", root/"share"}, {seed/"bin", root/"bin"}}, error))
        {
            result.message=error;
            return result;
        }
        removeTree(root/".update", error);
        result.outcome=SeedOutcome::Installed;
        result.message="installed v"+result.imageVersion+" into "+root.string();
        return result;
    }

    std::optional<SemVer> volumeVersion=SemVer::parse(result.volumeVersion, false);
    if(volumeVersion && !(*volumeVersion<*imageVersion))
    {
        result.outcome=SeedOutcome::Kept;
        result.message="kept v"+result.volumeVersion+" (the image has v"+result.imageVersion+")";
        return result;
    }
    if(readUpdateHistory(root).isFailed(result.imageVersion))
    {
        result.outcome=SeedOutcome::KeptFailed;
        result.message="kept v"+result.volumeVersion+": v"+result.imageVersion+" failed here before";
        return result;
    }

    if(!copyInstall(image, root, seed, error))
        return keptOnError(root, result, error);

    UpdateState state;
    state.from=result.volumeVersion;
    state.to=result.imageVersion;
    state.attempt=0;
    bool swapped=writeUpdateState(root, state, error) && swapIn(root, seed, error);
    if(!swapped)
    {
        std::string removeError;
        removeUpdateState(root, removeError);
    }
    removeTree(root/".update", error);
    if(!swapped)
        return keptOnError(root, result, error);

    result.outcome=SeedOutcome::Updated;
    result.message="updated v"+result.volumeVersion+" to v"+result.imageVersion+" (v"+result.volumeVersion+" kept as .prev)";
    return result;
}

HealthTimer::~HealthTimer()
{
    stop();
}

void HealthTimer::start(fs::path root, std::string version, std::chrono::milliseconds delay)
{
    stop();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_stopping=false;
    m_thread=std::thread([this, root=std::move(root), version=std::move(version), delay]()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_cv.wait_for(lock, delay, [this]() { return m_stopping; }))
            return;
        lock.unlock();

        if(markHealthy(root, version))
            spdlog::info("Update to v{} is healthy", version);
    });
}

void HealthTimer::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping=true;
    }
    m_cv.notify_all();
    if(m_thread.joinable())
        m_thread.join();
}

} // namespace server
} // namespace arbiterAI
