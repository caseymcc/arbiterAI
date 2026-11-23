#include "installEnvironment.h"
#include "installer.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <unistd.h>

namespace arbiterAI
{
namespace server
{

namespace
{

namespace fs=std::filesystem;

fs::path resolve(const fs::path &path, const fs::path &cwd)
{
    fs::path absolute=path.is_relative()?cwd/path:path;
    std::error_code ec;
    fs::path canonical=fs::weakly_canonical(absolute, ec);
    if(ec)
        return absolute.lexically_normal();
    return canonical;
}

/// \p path is \p dir or below it.
bool isInside(const fs::path &path, const fs::path &dir)
{
    fs::path::const_iterator it=path.begin();
    for(const fs::path &part:dir)
    {
        if(part.empty())
            continue;
        if(it == path.end() || *it != part)
            return false;
        ++it;
    }
    return true;
}

/// mountinfo escapes space, tab, newline and backslash as \ooo.
std::string unescapeMountField(const std::string &field)
{
    std::string result;
    for(size_t i=0; i<field.size(); ++i)
    {
        if(field[i] == '\\' && i+3<field.size())
        {
            std::string digits=field.substr(i+1, 3);
            if(digits.find_first_not_of("01234567") == std::string::npos)
            {
                result.push_back(static_cast<char>(std::stoi(digits, nullptr, 8)));
                i+=3;
                continue;
            }
        }
        result.push_back(field[i]);
    }
    return result;
}

} // anonymous namespace

std::string SystemEnvironment::variable(const std::string &name) const
{
    const char *value=std::getenv(name.c_str());
    return value?std::string(value):std::string();
}

bool SystemEnvironment::fileExists(const fs::path &path) const
{
    std::error_code ec;
    return fs::exists(path, ec);
}

std::string SystemEnvironment::mountInfo() const
{
    std::ifstream file("/proc/self/mountinfo");
    std::stringstream text;
    text<<file.rdbuf();
    return text.str();
}

bool SystemEnvironment::isWritable(const fs::path &path) const
{
    return access(path.c_str(), W_OK) == 0;
}

Supervisor detectSupervisor(const Environment &env)
{
    std::string declared=env.variable("ARBITERAI_SUPERVISOR");
    if(declared == "systemd" && !env.variable("INVOCATION_ID").empty())
        return Supervisor::Systemd;
    if(declared == "docker" && inContainer(env))
        return Supervisor::Docker;
    return Supervisor::None;
}

nlohmann::json supervisorJson(Supervisor supervisor)
{
    switch(supervisor)
    {
    case Supervisor::Systemd:
        return "systemd";
    case Supervisor::Docker:
        return "docker";
    default:
        return nullptr;
    }
}

bool inContainer(const Environment &env)
{
    return env.fileExists("/.dockerenv") || env.fileExists("/run/.containerenv");
}

std::vector<fs::path> mountPoints(const std::string &mountInfo)
{
    std::vector<fs::path> result;

    std::istringstream lines(mountInfo);
    std::string line;
    while(std::getline(lines, line))
    {
        // id parent major:minor root mount-point ...
        std::istringstream fields(line);
        std::string field;
        for(int i=0; i<5 && (fields>>field); ++i)
        {
            if(i == 4)
                result.push_back(fs::path(unescapeMountField(field)).lexically_normal());
        }
    }
    return result;
}

ApplyCheck checkCanApply(const ApplyContext &context, const Environment &env)
{
    ApplyCheck check;

    if(context.root.empty() || !env.fileExists(installMarkerPath(context.root)))
    {
        check.blockedReason="not installed from a release tarball: no share/arbiterAI/install.json next to the binary";
        return check;
    }

    if(!context.allowApply)
    {
        check.blockedReason="updates.allow_apply is false in the config file";
        return check;
    }

    fs::path root=resolve(context.root, context.cwd);

    if(inContainer(env))
    {
        std::vector<fs::path> mounts=mountPoints(env.mountInfo());
        bool mounted=false;
        for(const fs::path &mount:mounts)
        {
            if(mount == root)
                mounted=true;
        }
        if(!mounted)
        {
            check.blockedReason="the install root "+root.string()+" is part of the container image: "
                "mount a volume at /opt/arbiterAI (see compose.yaml) or pull the new image";
            return check;
        }
    }

    if(!env.isWritable(root))
    {
        check.blockedReason="the install root "+root.string()+" is not writable";
        return check;
    }

    std::vector<std::pair<std::string, fs::path>> paths;
    paths.push_back({"the working directory", context.cwd});
    for(const std::pair<std::string, fs::path> &entry:context.configPaths)
        paths.push_back({"'"+entry.first+"'", entry.second});

    for(const std::pair<std::string, fs::path> &entry:paths)
    {
        if(entry.second.empty())
            continue;

        fs::path path=resolve(entry.second, context.cwd);
        for(const char *dir:{"bin", "share"})
        {
            if(!isInside(path, root/dir))
                continue;

            check.blockedReason=entry.first+" ("+path.string()+") is inside "+(root/dir).string()
                +", which an update replaces: move it out of the install";
            return check;
        }
    }

    check.canApply=true;
    return check;
}

} // namespace server
} // namespace arbiterAI
