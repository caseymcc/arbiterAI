#ifndef _ARBITERAI_SERVER_INSTALLENVIRONMENT_H_
#define _ARBITERAI_SERVER_INSTALLENVIRONMENT_H_

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace arbiterAI
{
namespace server
{

/// What apply needs to know about where the server runs; tests fake it.
class Environment {
public:
    virtual ~Environment()=default;

    /// The value of an environment variable; empty when it is unset.
    virtual std::string variable(const std::string &name) const=0;
    virtual bool fileExists(const std::filesystem::path &path) const=0;
    /// The text of /proc/self/mountinfo.
    virtual std::string mountInfo() const=0;
    virtual bool isWritable(const std::filesystem::path &path) const=0;
};

class SystemEnvironment:public Environment {
public:
    std::string variable(const std::string &name) const override;
    bool fileExists(const std::filesystem::path &path) const override;
    std::string mountInfo() const override;
    bool isWritable(const std::filesystem::path &path) const override;
};

enum class Supervisor
{
    None,
    Systemd,
    Docker
};

/// ARBITERAI_SUPERVISOR when it matches where the server runs: "systemd"
/// needs INVOCATION_ID, "docker" needs /.dockerenv or /run/.containerenv.
Supervisor detectSupervisor(const Environment &env);

/// "systemd", "docker", or null for None.
nlohmann::json supervisorJson(Supervisor supervisor);

/// /.dockerenv or /run/.containerenv exists.
bool inContainer(const Environment &env);

/// The mount points listed in a /proc/self/mountinfo text, unescaped.
std::vector<std::filesystem::path> mountPoints(const std::string &mountInfo);

/// What can_apply is decided from.
struct ApplyContext {
    std::filesystem::path root;///< empty when the binary is not in a bin/ directory
    std::filesystem::path cwd;
    bool allowApply=true;
    /// why the config's "updates" block was rejected, which also turns allowApply off
    std::string updatesError;
    /// Paths the running server uses, by config key; relative ones are relative to cwd.
    std::vector<std::pair<std::string, std::filesystem::path>> configPaths;
};

struct ApplyCheck {
    bool canApply=false;
    std::string blockedReason;
};

/// can_apply and apply_blocked_reason: the install marker exists, allow_apply
/// is on, a container's root is a mount point, the root is writable, and
/// neither cwd nor a config path is inside <root>/bin or <root>/share.
ApplyCheck checkCanApply(const ApplyContext &context, const Environment &env);

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_INSTALLENVIRONMENT_H_
