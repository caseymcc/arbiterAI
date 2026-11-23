#include "updateStaging.h"
#include "installer.h"

#include <cpr/cpr.h>
#include <picosha2.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fstream>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace arbiterAI
{
namespace server
{

namespace
{

namespace fs=std::filesystem;

const size_t maxProcessOutput=16*1024;

std::string nowUtc()
{
    std::time_t seconds=std::time(nullptr);
    std::tm utc{};
    gmtime_r(&seconds, &utc);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

/// The end of a child's output, for an error message.
std::string outputTail(const std::string &output)
{
    const size_t tail=600;
    std::string text=output.size()>tail?"..."+output.substr(output.size()-tail):output;
    while(!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    return text;
}

std::string describeEnd(const ProcessResult &result)
{
    if(!result.started)
        return "could not be started: "+result.output;
    if(result.timedOut)
        return "timed out";
    if(result.signal != 0)
        return std::string("was killed by ")+strsignal(result.signal);
    return "exited with status "+std::to_string(result.exitCode);
}

void recordFailure(const StageRequest &request, const std::string &error, bool releaseAtFault)
{
    UpdateHistory history=readUpdateHistory(request.root);
    history.lastError=error;
    if(releaseAtFault)
        history.addFailed(request.version);
    history.updates.push_back({request.currentVersion, request.version, "failed", nowUtc(), error});

    std::string writeError;
    if(!writeUpdateHistory(request.root, history, writeError))
        spdlog::warn("Update: {}", writeError);
}

} // anonymous namespace

bool CprDownloader::download(const std::string &url, const fs::path &file, const Progress &progress, std::string &error)
{
    std::ofstream out(file, std::ios::binary|std::ios::trunc);
    if(!out.is_open())
    {
        error="cannot write "+file.string();
        return false;
    }

    cpr::Response response=cpr::Download(out,
        cpr::Url{url},
        cpr::Header{{"User-Agent", "arbiterAI-server"}, {"Accept", "application/octet-stream"}},
        cpr::ConnectTimeout{std::chrono::seconds(15)},
        cpr::LowSpeed{1, std::chrono::seconds(60)},
        cpr::ProgressCallback([&progress](cpr::cpr_pf_arg_t downloadTotal, cpr::cpr_pf_arg_t downloadNow,
            cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t)
        {
            if(progress)
                progress(static_cast<int64_t>(downloadNow), static_cast<int64_t>(downloadTotal));
            return true;
        }));
    out.close();

    if(response.error.code != cpr::ErrorCode::OK)
    {
        error="download failed: "+response.error.message;
        return false;
    }
    if(response.status_code != 200)
    {
        error="download failed: HTTP "+std::to_string(response.status_code);
        return false;
    }
    if(!out.good())
    {
        error="cannot write "+file.string();
        return false;
    }
    return true;
}

ProcessResult runProcess(const std::vector<std::string> &argv, const fs::path &cwd, std::chrono::milliseconds timeout)
{
    ProcessResult result;
    if(argv.empty())
    {
        result.output="no command";
        return result;
    }

    int pipeFds[2];
    if(pipe2(pipeFds, O_CLOEXEC) != 0)
    {
        result.output=std::string("pipe: ")+std::strerror(errno);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], 1);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], 2);
    if(!cwd.empty())
        posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    // Every fd the server opened without O_CLOEXEC (sockets, logs, GPU) stays behind.
    posix_spawn_file_actions_addclosefrom_np(&actions, 3);

    // The server blocks SIGTERM/SIGINT in every thread for its signal thread.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t mask;
    sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attr, &mask);
    sigset_t defaults;
    sigemptyset(&defaults);
    for(int sig:{SIGTERM, SIGINT, SIGHUP, SIGQUIT, SIGPIPE, SIGCHLD})
        sigaddset(&defaults, sig);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF|POSIX_SPAWN_SETPGROUP);

    std::vector<char *> args;
    for(const std::string &arg:argv)
        args.push_back(const_cast<char *>(arg.c_str()));
    args.push_back(nullptr);

    pid_t pid=0;
    int spawnError=posix_spawnp(&pid, args[0], &actions, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(pipeFds[1]);

    if(spawnError != 0)
    {
        close(pipeFds[0]);
        result.output=argv[0]+": "+std::strerror(spawnError);
        return result;
    }
    result.started=true;

    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+timeout;
    bool open=true;
    char buffer[4096];
    while(open)
    {
        std::chrono::milliseconds left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
        if(left.count()<=0)
            break;

        pollfd fd{pipeFds[0], POLLIN, 0};
        int ready=poll(&fd, 1, static_cast<int>(std::min<int64_t>(left.count(), 1000)));
        if(ready<0 && errno != EINTR)
            break;
        if(ready<=0)
            continue;

        ssize_t count=read(pipeFds[0], buffer, sizeof(buffer));
        if(count<0 && errno == EINTR)
            continue;
        if(count<=0)
        {
            open=false;
            continue;
        }
        result.output.append(buffer, static_cast<size_t>(count));
        if(result.output.size()>maxProcessOutput)
            result.output.erase(0, result.output.size()-maxProcessOutput);
    }
    close(pipeFds[0]);

    int status=0;
    while(true)
    {
        pid_t done=waitpid(pid, &status, WNOHANG);
        if(done == pid)
            break;
        if(done<0 && errno != EINTR)
            break;
        if(std::chrono::steady_clock::now()>=deadline)
        {
            result.timedOut=true;
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if(WIFEXITED(status))
        result.exitCode=WEXITSTATUS(status);
    else if(WIFSIGNALED(status))
        result.signal=WTERMSIG(status);
    return result;
}

std::string sha256File(const fs::path &file, std::string &error)
{
    std::ifstream in(file, std::ios::binary);
    if(!in.is_open())
    {
        error="cannot read "+file.string();
        return std::string();
    }

    picosha2::hash256_one_by_one hasher;
    std::vector<unsigned char> buffer(1<<20);
    while(in)
    {
        in.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        std::streamsize count=in.gcount();
        if(count>0)
            hasher.process(buffer.begin(), buffer.begin()+count);
    }
    if(in.bad())
    {
        error="cannot read "+file.string();
        return std::string();
    }
    hasher.finish();

    std::string hex;
    picosha2::get_hash_hex_string(hasher, hex);
    return hex;
}

fs::path stagingDir(const fs::path &root)
{
    return root/".update";
}

const char *stageStepName(StageStep step)
{
    switch(step)
    {
    case StageStep::CheckingSpace:
        return "checking_space";
    case StageStep::Downloading:
        return "downloading";
    case StageStep::Verifying:
        return "verifying";
    case StageStep::Unpacking:
        return "unpacking";
    case StageStep::SelfTesting:
        return "self_testing";
    case StageStep::Staged:
        return "staged";
    }
    return "";
}

void removeStaging(const fs::path &root)
{
    std::error_code ec;
    fs::remove_all(stagingDir(root), ec);
    if(ec)
        spdlog::warn("Update: cannot remove {}: {}", stagingDir(root).string(), ec.message());
}

StageResult stageRelease(const StageRequest &request, Downloader &downloader, const StageProgress &progress)
{
    StageResult result;
    std::string label="v"+request.version;

    auto report=[&progress](StageStep step, double fraction)
    {
        if(progress)
            progress(step, fraction);
    };
    auto fail=[&](StageStep step, const std::string &error, bool releaseAtFault)
    {
        spdlog::error("Update to {} failed while {}: {}", label, stageStepName(step), error);
        removeStaging(request.root);
        recordFailure(request, error, releaseAtFault);
        result.ok=false;
        result.step=step;
        result.error=error;
        return result;
    };

    // A crash during an earlier attempt can leave one behind.
    removeStaging(request.root);

    report(StageStep::CheckingSpace, 0.0);
    if(request.asset.size<=0)
        return fail(StageStep::CheckingSpace, "the release asset "+request.asset.name+" has no size", true);

    uintmax_t needed=static_cast<uintmax_t>(request.asset.size)*3;
    uintmax_t available=0;
    if(request.freeSpace)
    {
        available=request.freeSpace(request.root);
    }
    else
    {
        std::error_code ec;
        fs::space_info space=fs::space(request.root, ec);
        if(ec)
            return fail(StageStep::CheckingSpace, "cannot read the free space of "+request.root.string()+": "+ec.message(), false);
        available=space.available;
    }
    if(available<needed)
    {
        return fail(StageStep::CheckingSpace, "not enough free space in "+request.root.string()+": "
            +std::to_string(needed/(1024*1024))+" MiB needed (3x the download), "
            +std::to_string(available/(1024*1024))+" MiB free", false);
    }

    std::error_code ec;
    fs::create_directories(stagingDir(request.root), ec);
    if(ec)
        return fail(StageStep::Downloading, "cannot create "+stagingDir(request.root).string()+": "+ec.message(), false);

    report(StageStep::Downloading, 0.0);
    fs::path archive=stagingDir(request.root)/request.asset.name;
    std::string error;
    int64_t expectedSize=request.asset.size;
    bool downloaded=downloader.download(request.asset.url, archive, [&report, expectedSize](int64_t received, int64_t)
    {
        report(StageStep::Downloading, std::min(1.0, static_cast<double>(received)/static_cast<double>(expectedSize)));
    }, error);
    if(!downloaded)
        return fail(StageStep::Downloading, error, false);

    report(StageStep::Verifying, 0.0);
    uintmax_t size=fs::file_size(archive, ec);
    if(ec || size != static_cast<uintmax_t>(request.asset.size))
    {
        return fail(StageStep::Verifying, "the download is "+std::to_string(ec?0:size)+" bytes, the release says "
            +std::to_string(request.asset.size), false);
    }

    const std::string prefix="sha256:";
    if(request.asset.digest.rfind(prefix, 0) != 0)
        return fail(StageStep::Verifying, "the release asset has no sha256 digest", true);
    std::string expected=request.asset.digest.substr(prefix.size());
    std::string actual=sha256File(archive, error);
    if(actual.empty())
        return fail(StageStep::Verifying, error, false);
    if(actual != expected)
        return fail(StageStep::Verifying, "SHA-256 mismatch: the download is "+actual+", the release says "+expected, true);

    report(StageStep::Unpacking, 0.0);
    fs::path unpackDir=stagingDir(request.root)/"unpack";
    fs::create_directories(unpackDir, ec);
    if(ec)
        return fail(StageStep::Unpacking, "cannot create "+unpackDir.string()+": "+ec.message(), false);

    ProcessResult tar=runProcess({"tar", "-xzf", archive.string(), "-C", unpackDir.string(), "--no-same-owner"},
        fs::path(), std::chrono::minutes(5));
    if(!tar.started || tar.timedOut || tar.signal != 0 || tar.exitCode != 0)
        return fail(StageStep::Unpacking, "tar "+describeEnd(tar)+": "+outputTail(tar.output), true);
    fs::remove(archive, ec);

    fs::path staged=unpackDir/("arbiterAI-server-v"+request.version+"-linux-x64");
    std::string stagedVersion=installedVersion(staged);
    if(stagedVersion != request.version)
    {
        return fail(StageStep::Unpacking, "the archive's install.json names version '"+stagedVersion
            +"', expected "+request.version, true);
    }
    fs::path binary=installedBinaryPath(staged);
    if(access(binary.c_str(), X_OK) != 0)
        return fail(StageStep::Unpacking, "the archive has no executable bin/"+std::string(serverBinaryName), true);

    report(StageStep::SelfTesting, 0.0);
    ProcessResult selfTest=runProcess({binary.string(), "--self-test", "-c", request.configPath.string()},
        request.cwd, request.selfTestTimeout);
    if(!selfTest.started || selfTest.timedOut || selfTest.signal != 0 || selfTest.exitCode != 0)
    {
        return fail(StageStep::SelfTesting, label+" --self-test "+describeEnd(selfTest)+": "+outputTail(selfTest.output), true);
    }

    report(StageStep::Staged, 1.0);
    spdlog::info("Update to {} staged in {}", label, staged.string());
    result.ok=true;
    result.step=StageStep::Staged;
    result.staged=staged;
    return result;
}

} // namespace server
} // namespace arbiterAI
