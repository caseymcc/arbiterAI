#include "installEnvironment.h"
#include "updateRoutes.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{

namespace fs=std::filesystem;

using arbiterAI::server::ApplyCheck;
using arbiterAI::server::ApplyContext;
using arbiterAI::server::Supervisor;

class FakeEnvironment:public arbiterAI::server::Environment {
public:
    std::string variable(const std::string &name) const override
    {
        std::map<std::string, std::string>::const_iterator it=variables.find(name);
        return it == variables.end()?std::string():it->second;
    }

    bool fileExists(const fs::path &path) const override
    {
        return files.count(path.string()) != 0;
    }

    std::string mountInfo() const override
    {
        return mounts;
    }

    bool isWritable(const fs::path &path) const override
    {
        return readOnly.count(path.string()) == 0;
    }

    std::map<std::string, std::string> variables;
    std::set<std::string> files;
    std::string mounts;
    std::set<std::string> readOnly;
};

const char *const hostMountInfo=
    "22 1 259:2 / / rw,relatime shared:1 - ext4 /dev/nvme0n1p2 rw\n"
    "25 22 0:5 / /dev rw,nosuid shared:2 - devtmpfs udev rw\n";

/// An installed server at /opt/arbiterAI with nothing in its way.
class CanApplyTest:public ::testing::Test {
protected:
    void SetUp() override
    {
        m_context.root="/opt/arbiterAI";
        m_context.cwd="/var/lib/arbiterAI";
        m_context.allowApply=true;
        m_context.configPaths={
            {"--config", "/etc/arbiterAI/server.json"},
            {"model_config_paths", "config"},
            {"models_dir", "/var/lib/arbiterAI/models"}
        };
        m_env.files.insert("/opt/arbiterAI/share/arbiterAI/install.json");
        m_env.mounts=hostMountInfo;
    }

    ApplyCheck check() const
    {
        return arbiterAI::server::checkCanApply(m_context, m_env);
    }

    void enterContainer()
    {
        m_env.files.insert("/.dockerenv");
    }

    ApplyContext m_context;
    FakeEnvironment m_env;
};

TEST(SupervisorTest, SystemdNeedsInvocationId)
{
    FakeEnvironment env;
    env.variables["ARBITERAI_SUPERVISOR"]="systemd";
    EXPECT_EQ(arbiterAI::server::detectSupervisor(env), Supervisor::None);

    env.variables["INVOCATION_ID"]="0123456789abcdef";
    EXPECT_EQ(arbiterAI::server::detectSupervisor(env), Supervisor::Systemd);
    EXPECT_EQ(arbiterAI::server::supervisorJson(Supervisor::Systemd), "systemd");
}

TEST(SupervisorTest, DockerNeedsAContainerMarker)
{
    FakeEnvironment env;
    env.variables["ARBITERAI_SUPERVISOR"]="docker";
    EXPECT_EQ(arbiterAI::server::detectSupervisor(env), Supervisor::None);

    env.files.insert("/run/.containerenv");
    EXPECT_EQ(arbiterAI::server::detectSupervisor(env), Supervisor::Docker);

    FakeEnvironment dockerenv;
    dockerenv.variables["ARBITERAI_SUPERVISOR"]="docker";
    dockerenv.files.insert("/.dockerenv");
    EXPECT_EQ(arbiterAI::server::detectSupervisor(dockerenv), Supervisor::Docker);
}

TEST(SupervisorTest, VariableThatDoesNotMatchTheEnvironmentIsNone)
{
    // Copied from a unit file into a shell under systemd's session: no INVOCATION_ID.
    FakeEnvironment shell;
    shell.variables["ARBITERAI_SUPERVISOR"]="systemd";
    shell.files.insert("/.dockerenv");
    EXPECT_EQ(arbiterAI::server::detectSupervisor(shell), Supervisor::None);

    // Declared docker but running as a systemd service on the host.
    FakeEnvironment service;
    service.variables["ARBITERAI_SUPERVISOR"]="docker";
    service.variables["INVOCATION_ID"]="0123456789abcdef";
    EXPECT_EQ(arbiterAI::server::detectSupervisor(service), Supervisor::None);
    EXPECT_TRUE(arbiterAI::server::supervisorJson(Supervisor::None).is_null());
}

TEST(SupervisorTest, UndeclaredIsNone)
{
    FakeEnvironment env;
    env.variables["INVOCATION_ID"]="0123456789abcdef";
    env.files.insert("/.dockerenv");
    EXPECT_EQ(arbiterAI::server::detectSupervisor(env), Supervisor::None);
}

TEST(MountInfoTest, ReadsAndUnescapesMountPoints)
{
    std::vector<fs::path> mounts=arbiterAI::server::mountPoints(
        "22 1 259:2 / / rw,relatime shared:1 - ext4 /dev/nvme0n1p2 rw\n"
        "731 700 259:2 /var/lib/docker/volumes/arbiterai_install/_data /opt/arbiterAI rw,relatime - ext4 /dev/nvme0n1p2 rw\n"
        "732 700 259:2 /home/me/my\\040models /models\\040dir rw - ext4 /dev/nvme0n1p2 rw\n");

    ASSERT_EQ(mounts.size(), 3u);
    EXPECT_EQ(mounts[0], fs::path("/"));
    EXPECT_EQ(mounts[1], fs::path("/opt/arbiterAI"));
    EXPECT_EQ(mounts[2], fs::path("/models dir"));
}

TEST_F(CanApplyTest, AllowedWhenEverythingHolds)
{
    ApplyCheck result=check();
    EXPECT_TRUE(result.canApply)<<result.blockedReason;
    EXPECT_EQ(result.blockedReason, "");
}

TEST_F(CanApplyTest, BlockedWithoutTheInstallMarker)
{
    m_env.files.clear();
    ApplyCheck result=check();
    EXPECT_FALSE(result.canApply);
    EXPECT_NE(result.blockedReason.find("install.json"), std::string::npos)<<result.blockedReason;

    // A source build: the binary is not in a bin/ directory.
    m_context.root.clear();
    EXPECT_FALSE(check().canApply);
}

TEST_F(CanApplyTest, BlockedWhenAllowApplyIsOff)
{
    m_context.allowApply=false;
    ApplyCheck result=check();
    EXPECT_FALSE(result.canApply);
    EXPECT_EQ(result.blockedReason, "updates.allow_apply is false in the config file");
}

TEST_F(CanApplyTest, AnInvalidUpdatesBlockSaysARestartIsNeeded)
{
    m_context.allowApply=false;
    m_context.updatesError="'updates.idle_minutes' must be an integer";
    ApplyCheck result=check();
    EXPECT_FALSE(result.canApply);
    EXPECT_EQ(result.blockedReason, "the 'updates' config block was invalid when the server started, so apply is off until it restarts: 'updates.idle_minutes' must be an integer");
}

TEST_F(CanApplyTest, BlockedWhenTheRootIsNotWritable)
{
    m_env.readOnly.insert("/opt/arbiterAI");
    ApplyCheck result=check();
    EXPECT_FALSE(result.canApply);
    EXPECT_EQ(result.blockedReason, "the install root /opt/arbiterAI is not writable");
}

TEST_F(CanApplyTest, BlockedInAContainerWhoseRootIsNotAMountPoint)
{
    enterContainer();
    ApplyCheck result=check();
    EXPECT_FALSE(result.canApply);
    EXPECT_NE(result.blockedReason.find("mount a volume at /opt/arbiterAI (see compose.yaml) or pull the new image"),
        std::string::npos)<<result.blockedReason;
}

TEST_F(CanApplyTest, AllowedInAContainerWithTheRootOnAVolume)
{
    enterContainer();
    m_env.mounts=std::string(hostMountInfo)
        +"731 22 259:2 /var/lib/docker/volumes/arbiterai_install/_data /opt/arbiterAI rw,relatime - ext4 /dev/nvme0n1p2 rw\n";
    ApplyCheck result=check();
    EXPECT_TRUE(result.canApply)<<result.blockedReason;
}

TEST_F(CanApplyTest, BlockedWhenTheWorkingDirectoryIsInBin)
{
    m_context.cwd="/opt/arbiterAI/bin";
    ApplyCheck result=check();
    EXPECT_FALSE(result.canApply);
    EXPECT_NE(result.blockedReason.find("the working directory"), std::string::npos)<<result.blockedReason;
    EXPECT_NE(result.blockedReason.find("/opt/arbiterAI/bin"), std::string::npos)<<result.blockedReason;
}

TEST_F(CanApplyTest, BlockedWhenAConfigPathIsInShareAndNamesTheKey)
{
    m_context.configPaths.push_back({"override_path", "/opt/arbiterAI/share/arbiterAI/overrides.json"});
    ApplyCheck result=check();
    EXPECT_FALSE(result.canApply);
    EXPECT_NE(result.blockedReason.find("'override_path'"), std::string::npos)<<result.blockedReason;
    EXPECT_NE(result.blockedReason.find("/opt/arbiterAI/share"), std::string::npos)<<result.blockedReason;
}

TEST_F(CanApplyTest, RelativeConfigPathsResolveAgainstTheWorkingDirectory)
{
    // cwd is the root itself: "config" is fine, "share/configs" is not.
    m_context.cwd="/opt/arbiterAI";
    EXPECT_TRUE(check().canApply)<<check().blockedReason;

    m_context.configPaths.push_back({"model_config_paths", "share/configs"});
    ApplyCheck result=check();
    EXPECT_FALSE(result.canApply);
    EXPECT_NE(result.blockedReason.find("'model_config_paths'"), std::string::npos)<<result.blockedReason;
}

TEST_F(CanApplyTest, APathThatOnlySharesAPrefixIsNotInside)
{
    m_context.configPaths.push_back({"logging.directory", "/opt/arbiterAI/binlogs"});
    m_context.configPaths.push_back({"injected_config_dir", "/opt/arbiterAI/shared"});
    ApplyCheck result=check();
    EXPECT_TRUE(result.canApply)<<result.blockedReason;
}

class NoReleases:public arbiterAI::server::ReleaseSource {
public:
    arbiterAI::server::ReleaseFetch fetch(const std::string &) override
    {
        arbiterAI::server::ReleaseFetch result;
        result.error="offline";
        return result;
    }
};

TEST(UpdateRouteApplyTest, GetUpdateReportsCanApplyAndSupervisor)
{
    arbiterAI::server::UpdateManager manager(std::make_unique<NoReleases>(), "0.1.15");
    arbiterAI::server::setUpdateManager(&manager);

    ApplyContext context;
    context.root="/opt/arbiterAI";
    context.cwd="/opt/arbiterAI/bin";
    std::shared_ptr<FakeEnvironment> env=std::make_shared<FakeEnvironment>();
    env->files.insert("/opt/arbiterAI/share/arbiterAI/install.json");
    env->variables["ARBITERAI_SUPERVISOR"]="systemd";
    env->variables["INVOCATION_ID"]="0123456789abcdef";
    arbiterAI::server::setApplyContext(context, env);

    httplib::Request req;
    httplib::Response res;
    arbiterAI::server::handleGetUpdate(req, res);
    nlohmann::json body=nlohmann::json::parse(res.body);
    EXPECT_EQ(body["current"], "0.1.15");
    EXPECT_EQ(body["can_apply"], false);
    ASSERT_TRUE(body["apply_blocked_reason"].is_string())<<body.dump();
    EXPECT_NE(body["apply_blocked_reason"].get<std::string>().find("the working directory"), std::string::npos);
    EXPECT_EQ(body["supervisor"], "systemd");

    context.cwd="/var/lib/arbiterAI";
    arbiterAI::server::setApplyContext(context, env);
    httplib::Response allowed;
    arbiterAI::server::handleGetUpdate(req, allowed);
    nlohmann::json allowedBody=nlohmann::json::parse(allowed.body);
    EXPECT_EQ(allowedBody["can_apply"], true);
    EXPECT_TRUE(allowedBody["apply_blocked_reason"].is_null());

    arbiterAI::server::setApplyContext(ApplyContext(), nullptr);
    arbiterAI::server::setUpdateManager(nullptr);
}

TEST(UpdateRouteApplyTest, WithoutAContextApplyIsBlocked)
{
    arbiterAI::server::setApplyContext(ApplyContext(), nullptr);
    nlohmann::json status=arbiterAI::server::applyStatusJson();
    EXPECT_EQ(status["can_apply"], false);
    EXPECT_TRUE(status["supervisor"].is_null());
}

} // anonymous namespace
