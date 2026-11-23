#include "installer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace
{

namespace fs=std::filesystem;

using arbiterAI::server::ProbationOutcome;
using arbiterAI::server::ProbationResult;
using arbiterAI::server::UpdateHistory;
using arbiterAI::server::UpdateState;

std::string readFile(const fs::path &path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    file<<text;
}

/// An install of \p version under \p root: bin/arbiterAI-server holds the
/// version as text, share/ has the install marker.
void makeInstall(const fs::path &root, const std::string &version)
{
    writeFile(root/"bin"/"arbiterAI-server", "binary "+version);
    writeFile(root/"share"/"arbiterAI"/"install.json", "{\"version\": \""+version+"\"}");
}

class FakeRestarter:public arbiterAI::server::Restarter {
public:
    void restart(const fs::path &binary, const std::vector<std::string> &args, std::string &error) override
    {
        calls++;
        lastBinary=binary;
        lastArgs=args;
        error="fake restart";
    }

    int calls=0;
    fs::path lastBinary;
    std::vector<std::string> lastArgs;
};

class InstallerTest:public ::testing::Test {
protected:
    void SetUp() override
    {
        m_root=fs::temp_directory_path()/("arbiterai_installer_"+std::to_string(getpid())+"_"
            +::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(m_root);
        fs::create_directories(m_root);
    }

    void TearDown() override
    {
        fs::remove_all(m_root);
    }

    /// <root> at version 1.0.0 swapped to 1.1.0, as an apply leaves it.
    void makeUpdatedInstall(int attempt)
    {
        makeInstall(m_root, "1.0.0");
        makeInstall(m_root/"staged", "1.1.0");
        std::string error;
        ASSERT_TRUE(arbiterAI::server::swapIn(m_root, m_root/"staged", error))<<error;

        UpdateState state;
        state.from="1.0.0";
        state.to="1.1.0";
        state.attempt=attempt;
        state.loadedModels=nlohmann::json::array({{{"model", "m"}, {"pinned", true}}});
        ASSERT_TRUE(arbiterAI::server::writeUpdateState(m_root, state, error))<<error;
    }

    bool stateExists() const
    {
        return fs::exists(m_root/"update-state.json");
    }

    fs::path m_root;
};

TEST(InstallRoot, IsTheParentOfBin)
{
    EXPECT_EQ(arbiterAI::server::installRootFor("/opt/arbiterAI/bin/arbiterAI-server"), fs::path("/opt/arbiterAI"));
    EXPECT_TRUE(arbiterAI::server::installRootFor("/app/build/linux_x64_debug/arbiterAI-server").empty());
}

TEST_F(InstallerTest, InstalledVersionReadsTheMarker)
{
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "");
    makeInstall(m_root, "1.2.3");
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.2.3");
}

TEST_F(InstallerTest, SwapKeepsPrevAndSwapBackRestoresIt)
{
    makeInstall(m_root, "1.0.0");
    makeInstall(m_root/"staged", "1.1.0");

    std::string error;
    ASSERT_TRUE(arbiterAI::server::swapIn(m_root, m_root/"staged", error))<<error;
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");
    EXPECT_EQ(readFile(m_root/"bin"/"arbiterAI-server"), "binary 1.1.0");
    EXPECT_EQ(readFile(m_root/"bin.prev"/"arbiterAI-server"), "binary 1.0.0");
    EXPECT_TRUE(fs::exists(m_root/"share.prev"/"arbiterAI"/"install.json"));
    EXPECT_TRUE(arbiterAI::server::hasPrevious(m_root));

    ASSERT_TRUE(arbiterAI::server::swapBack(m_root, error))<<error;
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
    EXPECT_EQ(readFile(m_root/"bin"/"arbiterAI-server"), "binary 1.0.0");
    EXPECT_FALSE(arbiterAI::server::hasPrevious(m_root));
    EXPECT_FALSE(fs::exists(m_root/"share.prev"));
    EXPECT_FALSE(fs::exists(m_root/"bin.failed"));
    EXPECT_FALSE(fs::exists(m_root/"share.failed"));
}

TEST_F(InstallerTest, SwapReplacesAnOlderPrev)
{
    makeInstall(m_root, "1.1.0");
    writeFile(m_root/"bin.prev"/"arbiterAI-server", "binary 1.0.0");
    writeFile(m_root/"share.prev"/"arbiterAI"/"install.json", "{\"version\": \"1.0.0\"}");
    makeInstall(m_root/"staged", "1.2.0");

    std::string error;
    ASSERT_TRUE(arbiterAI::server::swapIn(m_root, m_root/"staged", error))<<error;
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.2.0");
    EXPECT_EQ(readFile(m_root/"bin.prev"/"arbiterAI-server"), "binary 1.1.0");
    EXPECT_EQ(readFile(m_root/"share.prev"/"arbiterAI"/"install.json"), "{\"version\": \"1.1.0\"}");
}

TEST_F(InstallerTest, SwapWithoutAStagedShareLeavesTheInstallAlone)
{
    makeInstall(m_root, "1.0.0");
    writeFile(m_root/"staged"/"bin"/"arbiterAI-server", "binary 1.1.0");

    std::string error;
    EXPECT_FALSE(arbiterAI::server::swapIn(m_root, m_root/"staged", error));
    EXPECT_EQ(readFile(m_root/"bin"/"arbiterAI-server"), "binary 1.0.0");
    EXPECT_FALSE(arbiterAI::server::hasPrevious(m_root));
}

TEST_F(InstallerTest, NoStateIsNoProbation)
{
    makeInstall(m_root, "1.0.0");
    ProbationResult result=arbiterAI::server::checkProbation(m_root, "1.0.0", true);
    EXPECT_EQ(result.outcome, ProbationOutcome::None);
}

TEST_F(InstallerTest, FirstStartMovesAttemptZeroToOne)
{
    makeUpdatedInstall(0);

    ProbationResult result=arbiterAI::server::checkProbation(m_root, "1.1.0", true);
    EXPECT_EQ(result.outcome, ProbationOutcome::Started);
    EXPECT_EQ(result.state.loadedModels.size(), 1u);
    EXPECT_EQ(result.state.loadedModels[0]["model"], "m");

    std::string error;
    std::optional<UpdateState> state=arbiterAI::server::readUpdateState(m_root, error);
    ASSERT_TRUE(state.has_value())<<error;
    EXPECT_EQ(state->attempt, 1);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");
}

TEST_F(InstallerTest, AttemptOneRollsBackAndRecordsTheVersionAsFailed)
{
    makeUpdatedInstall(1);

    ProbationResult result=arbiterAI::server::checkProbation(m_root, "1.1.0", true);
    EXPECT_EQ(result.outcome, ProbationOutcome::RolledBack);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
    EXPECT_EQ(readFile(m_root/"bin"/"arbiterAI-server"), "binary 1.0.0");
    EXPECT_FALSE(stateExists());

    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_TRUE(history.isFailed("1.1.0"));
    EXPECT_EQ(history.lastError, "v1.1.0 failed to start, rolled back");
    EXPECT_EQ(history.prevVersion, "");
    ASSERT_EQ(history.updates.size(), 1u);
    EXPECT_EQ(history.updates[0].outcome, "rolled_back");
}

TEST_F(InstallerTest, RollbackAtStartupRestartsTheOldBinaryWithTheSameArgs)
{
    makeUpdatedInstall(1);

    FakeRestarter restarter;
    std::vector<std::string> args={"/whatever/arbiterAI-server", "-c", "/etc/arbiterAI/server.json"};
    ProbationResult result=arbiterAI::server::runStartupProbation(m_root, "1.1.0", restarter, args);

    EXPECT_EQ(restarter.calls, 1);
    EXPECT_EQ(restarter.lastBinary, m_root/"bin"/"arbiterAI-server");
    EXPECT_EQ(restarter.lastArgs, args);
    EXPECT_EQ(result.outcome, ProbationOutcome::Failed);
}

TEST_F(InstallerTest, FirstStartDoesNotRestart)
{
    makeUpdatedInstall(0);

    FakeRestarter restarter;
    ProbationResult result=arbiterAI::server::runStartupProbation(m_root, "1.1.0", restarter, {"arbiterAI-server"});
    EXPECT_EQ(result.outcome, ProbationOutcome::Started);
    EXPECT_EQ(restarter.calls, 0);
}

TEST_F(InstallerTest, StateForAnotherVersionIsDiscarded)
{
    makeUpdatedInstall(1);

    // 1.0.0 runs: the state is for 1.1.0, so the swap never reached this binary.
    ProbationResult result=arbiterAI::server::checkProbation(m_root, "1.0.0", true);
    EXPECT_EQ(result.outcome, ProbationOutcome::Discarded);
    EXPECT_FALSE(stateExists());
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");
    EXPECT_FALSE(arbiterAI::server::readUpdateHistory(m_root).isFailed("1.1.0"));
}

TEST_F(InstallerTest, SeedModeLeavesAttemptZeroForTheServer)
{
    makeUpdatedInstall(0);

    ProbationResult result=arbiterAI::server::checkProbation(m_root, "1.1.0", false);
    EXPECT_EQ(result.outcome, ProbationOutcome::Pending);

    std::string error;
    std::optional<UpdateState> state=arbiterAI::server::readUpdateState(m_root, error);
    ASSERT_TRUE(state.has_value())<<error;
    EXPECT_EQ(state->attempt, 0);
}

TEST_F(InstallerTest, SeedModeRollsBackAPendingRollback)
{
    makeUpdatedInstall(1);

    ProbationResult result=arbiterAI::server::checkProbation(m_root, "1.1.0", false);
    EXPECT_EQ(result.outcome, ProbationOutcome::RolledBack);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.0.0");
    EXPECT_TRUE(arbiterAI::server::readUpdateHistory(m_root).isFailed("1.1.0"));
}

TEST_F(InstallerTest, RollbackWithoutPrevRecordsTheFailure)
{
    makeInstall(m_root, "1.1.0");
    UpdateState state;
    state.from="1.0.0";
    state.to="1.1.0";
    state.attempt=1;
    std::string error;
    ASSERT_TRUE(arbiterAI::server::writeUpdateState(m_root, state, error))<<error;

    ProbationResult result=arbiterAI::server::checkProbation(m_root, "1.1.0", true);
    EXPECT_EQ(result.outcome, ProbationOutcome::Failed);
    EXPECT_FALSE(stateExists());
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");
    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_TRUE(history.isFailed("1.1.0"));
    EXPECT_NE(history.lastError.find("could not be rolled back"), std::string::npos)<<history.lastError;
}

TEST_F(InstallerTest, CleanStopDuringProbationKeepsTheUpdate)
{
    makeUpdatedInstall(0);
    arbiterAI::server::checkProbation(m_root, "1.1.0", true);

    EXPECT_TRUE(arbiterAI::server::markHealthy(m_root, "1.1.0"));
    EXPECT_FALSE(stateExists());

    UpdateHistory history=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_TRUE(history.failed.empty());
    EXPECT_EQ(history.prevVersion, "1.0.0");
    ASSERT_EQ(history.updates.size(), 1u);
    EXPECT_EQ(history.updates[0].outcome, "updated");

    // The next start finds no probation, so nothing rolls back.
    EXPECT_EQ(arbiterAI::server::checkProbation(m_root, "1.1.0", true).outcome, ProbationOutcome::None);
    EXPECT_EQ(arbiterAI::server::installedVersion(m_root), "1.1.0");
}

TEST_F(InstallerTest, MarkHealthyLeavesAStateForAnotherVersion)
{
    makeUpdatedInstall(0);

    // The running 1.0.0 has just applied 1.1.0: its health must not end that probation.
    EXPECT_FALSE(arbiterAI::server::markHealthy(m_root, "1.0.0"));
    EXPECT_TRUE(stateExists());
}

TEST_F(InstallerTest, HistoryRoundTrips)
{
    UpdateHistory history;
    history.lastError="boom";
    history.prevVersion="1.0.0";
    history.addFailed("1.1.0");
    history.addFailed("1.1.0");
    history.updates.push_back({"1.0.0", "1.1.0", "rolled_back", "2026-11-23T00:00:00Z", "boom"});

    std::string error;
    ASSERT_TRUE(arbiterAI::server::writeUpdateHistory(m_root, history, error))<<error;
    UpdateHistory read=arbiterAI::server::readUpdateHistory(m_root);
    EXPECT_EQ(read.lastError, "boom");
    EXPECT_EQ(read.prevVersion, "1.0.0");
    EXPECT_EQ(read.failed, std::vector<std::string>{"1.1.0"});
    ASSERT_EQ(read.updates.size(), 1u);
    EXPECT_EQ(read.updates[0].error, "boom");
}

TEST_F(InstallerTest, HealthTimerMarksHealthyAfterTheDelay)
{
    makeUpdatedInstall(1);

    arbiterAI::server::HealthTimer timer;
    timer.start(m_root, "1.1.0", std::chrono::milliseconds(50));
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(stateExists() && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(stateExists());
    EXPECT_EQ(arbiterAI::server::readUpdateHistory(m_root).prevVersion, "1.0.0");
}

TEST_F(InstallerTest, HealthTimerStoppedEarlyMarksNothing)
{
    makeUpdatedInstall(1);

    arbiterAI::server::HealthTimer timer;
    timer.start(m_root, "1.1.0", std::chrono::seconds(30));
    timer.stop();
    EXPECT_TRUE(stateExists());
}

} // anonymous namespace
