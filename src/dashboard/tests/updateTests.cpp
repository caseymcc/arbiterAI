#include "dashboardTest.h"

#include <gweni/platforms/platform.h>

using arbiterAI::dashboard::DashboardApp;
using arbiterAI::dashboard::Tab;

TEST_F(DashboardTest, UpdateBadgeShowsWhenAnUpdateIsAvailable)
{
    m_data.setJson("/api/update", loadFixture("update.json"));
    start();

    EXPECT_TRUE(m_app->updateBadge()->visible());
    EXPECT_EQ(m_app->updateText(), "v0.1.15 → v0.1.16 available");
    EXPECT_EQ(m_app->version(), "v0.1.15   llama.cpp b9204");
    shot("header-update-available");
}

TEST_F(DashboardTest, NoUpdateKeepsThePlainHeader)
{
    m_data.setJson("/api/update", loadFixture("updateNone.json"));
    start();

    EXPECT_EQ(m_data.count("/api/update"), 1u);
    EXPECT_FALSE(m_app->updateBadge()->visible());
    EXPECT_EQ(m_app->version(), "v0.1.15   llama.cpp b9204");
    shot("header-no-update");
}

TEST_F(DashboardTest, AServerWithoutTheCheckKeepsThePlainHeader)
{
    // no /api/update reply: a 404, as from a server before the check
    start();

    EXPECT_EQ(m_data.count("/api/update"), 1u);
    EXPECT_FALSE(m_app->updateBadge()->visible());
    EXPECT_TRUE(m_app->connected());
}

TEST_F(DashboardTest, UpdateBadgeShowsOnEveryTab)
{
    m_data.setJson("/api/update", loadFixture("update.json"));
    start();

    click(m_app->tabButton(Tab::Storage));
    ASSERT_EQ(m_app->currentTab(), Tab::Storage);
    EXPECT_TRUE(m_app->updateBadge()->visible());

    click(m_app->tabButton(Tab::Config));
    ASSERT_EQ(m_app->currentTab(), Tab::Config);
    EXPECT_TRUE(m_app->updateBadge()->visible());
    shot("header-update-available-config");
}

TEST_F(DashboardTest, ClickingTheUpdateBadgeOpensTheReleaseNotes)
{
    std::vector<std::string> opened;
    gweni::platform::setOpenUrlHandler([&opened](const gweni::String &url)
    {
        opened.push_back(url);
        return true;
    });
    m_data.setJson("/api/update", loadFixture("update.json"));
    start();

    click(m_app->updateBadge());
    gweni::platform::setOpenUrlHandler(nullptr);

    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], "https://github.com/caseymcc/arbiterAI/releases/tag/v0.1.16");
}

TEST_F(DashboardTest, UpdateIsAskedForLessOftenThanStats)
{
    m_data.setJson("/api/update", loadFixture("updateNone.json"));
    start();
    ASSERT_EQ(m_data.count("/api/update"), 1u);

    frames(120*(DashboardApp::UpdatePollEvery-1)+5);
    EXPECT_EQ(m_data.count("/api/stats"), static_cast<size_t>(DashboardApp::UpdatePollEvery));
    EXPECT_EQ(m_data.count("/api/update"), 1u);

    frames(120);
    EXPECT_EQ(m_data.count("/api/update"), 2u);
}

TEST_F(DashboardTest, UpdateBadgeFollowsTheServer)
{
    m_data.setJson("/api/update", loadFixture("updateNone.json"));
    start();
    ASSERT_FALSE(m_app->updateBadge()->visible());

    m_data.setJson("/api/update", loadFixture("update.json"));
    frames(120*DashboardApp::UpdatePollEvery+5);
    EXPECT_TRUE(m_app->updateBadge()->visible());

    // installed (or withdrawn): the plain header again
    m_data.setJson("/api/update", loadFixture("updateNone.json"));
    frames(120*DashboardApp::UpdatePollEvery);
    EXPECT_FALSE(m_app->updateBadge()->visible());
}

TEST_F(DashboardTest, UpdateButtonShowsWhenTheServerCanInstall)
{
    m_data.setJson("/api/update", loadFixture("updateCanApply.json"));
    start();

    EXPECT_FALSE(m_app->updates().updateButton()->hidden());
    EXPECT_EQ(m_app->updates().updateButton()->getText(), "Update to v0.1.16");
    EXPECT_TRUE(m_app->updates().strip()->hidden());
    shot("update-can-apply");
}

TEST_F(DashboardTest, ABlockedUpdateShowsTheManualStepsAndTheReason)
{
    m_data.setJson("/api/update", loadFixture("updateBlocked.json"));
    start();

    EXPECT_TRUE(m_app->updates().updateButton()->hidden());
    EXPECT_FALSE(m_app->updates().strip()->hidden());
    EXPECT_EQ(m_app->updates().stripText(), "v0.1.16 cannot be installed from the dashboard: the install root /opt/arbiterAI "
        "is part of the container image: mount a volume at /opt/arbiterAI (see compose.yaml) or pull the new image");
    EXPECT_EQ(m_app->updates().stripDetail().rfind("To update by hand: download arbiterAI-server-v0.1.16-linux-x64.tar.gz", 0), 0u)
        <<m_app->updates().stripDetail();
    shot("update-blocked");
}

TEST_F(DashboardTest, TheUpdateButtonAsksFirstWithModelsJobsAndRollback)
{
    m_data.setJson("/api/update", loadFixture("updateCanApply.json"));
    m_data.setJson("/api/models/loaded", loadFixture("loadedModels.json"));
    m_data.setJson("/api/update/apply", loadFixture("updateInProgress.json"), "POST");
    start();

    click(m_app->updates().updateButton());
    ASSERT_NE(m_app->updates().confirmDialog(), nullptr);
    EXPECT_EQ(m_data.count("/api/update/apply", "POST"), 0u);
    const std::string &text=m_app->updates().confirmText();
    EXPECT_NE(text.find("gpt-oss-120b (Q4_K_M), qwen3-8b (Q8_0), llama-3.1-8b (Q4_K_M)"), std::string::npos)<<text;
    EXPECT_NE(text.find("Active jobs: 2, drained for up to 60 s, then cut off"), std::string::npos)<<text;
    EXPECT_NE(text.find("systemd restarts it and it is rolled back"), std::string::npos)<<text;
    frames(10);
    shot("update-confirm");

    m_app->updates().confirmDialog()->close(gweni::dialogs::Result::Ok);
    frames(2);
    ASSERT_EQ(m_data.count("/api/update/apply", "POST"), 1u);
    for(const auto &request:m_data.requests())
    {
        if(request.method=="POST")
            EXPECT_EQ(request.body, (nlohmann::json{{"version", "0.1.16"}}));
    }
}

TEST_F(DashboardTest, CancellingTheDialogInstallsNothing)
{
    m_data.setJson("/api/update", loadFixture("updateCanApply.json"));
    start();

    click(m_app->updates().updateButton());
    ASSERT_NE(m_app->updates().confirmDialog(), nullptr);
    m_app->updates().confirmDialog()->close(gweni::dialogs::Result::Cancel);
    frames(2);
    EXPECT_EQ(m_data.count("/api/update/apply", "POST"), 0u);
}

TEST_F(DashboardTest, WithoutASupervisorTheDialogSaysThereIsNoRollback)
{
    nlohmann::json status=loadFixture("updateCanApply.json");
    status["supervisor"]=nullptr;
    m_data.setJson("/api/update", status);
    start();

    m_app->updates().confirmApply();
    EXPECT_NE(m_app->updates().confirmText().find("it is not rolled back: no supervisor declared (see README)"), std::string::npos)
        <<m_app->updates().confirmText();
}

TEST_F(DashboardTest, AnInstallShowsItsStageAndIsPolledEveryRound)
{
    m_data.setJson("/api/update", loadFixture("updateInProgress.json"));
    start();

    EXPECT_TRUE(m_app->updates().updateButton()->hidden());
    EXPECT_FALSE(m_app->updates().progressLabel()->hidden());
    EXPECT_EQ(m_app->updates().progressLabel()->getText(), "Installing v0.1.16: downloading 42%");
    shot("update-in-progress");

    // the first poll asked for it, as always; now every poll does
    frames(120*3);
    EXPECT_EQ(m_data.count("/api/update"), 4u);
}

TEST_F(DashboardTest, TheRestartReloadsThePageOnceTheVersionChanges)
{
    int reloads=0;
    m_data.setJson("/api/update", loadFixture("updateRestarting.json"));
    m_app=std::make_unique<DashboardApp>(m_headless->canvas(), m_data, "/dashboard", nullptr,
        []() { return FixtureNow; }, [&reloads]() { reloads++; });
    frames(2);
    EXPECT_EQ(m_app->updates().progressLabel()->getText(), "Restarting the server...");

    // the old process still answers: no reload
    frames(120*2);
    EXPECT_EQ(reloads, 0);

    nlohmann::json version=loadFixture("version.json");
    version["version"]="0.1.16";
    m_data.setJson("/api/version", version);
    frames(120);
    EXPECT_EQ(reloads, 1);
    frames(120*2);
    EXPECT_EQ(reloads, 1);
}

TEST_F(DashboardTest, ARestartIntoTheSameVersionReloadsOnceTheServerIsBack)
{
    // a rollback after a crash: the new process runs the version the page started with
    int reloads=0;
    m_data.setJson("/api/update", loadFixture("updateRestarting.json"));
    m_app=std::make_unique<DashboardApp>(m_headless->canvas(), m_data, "/dashboard", nullptr,
        []() { return FixtureNow; }, [&reloads]() { reloads++; });
    frames(2);

    m_data.setJson("/api/update", loadFixture("updateRolledBack.json"));
    frames(120);
    EXPECT_EQ(reloads, 1);
}

TEST_F(DashboardTest, AfterAnUpdateTheStripSaysFromWhatAndOffersRollback)
{
    nlohmann::json version=loadFixture("version.json");
    version["version"]="0.1.16";
    m_data.setJson("/api/version", version);
    m_data.setJson("/api/update", loadFixture("updateAfter.json"));
    m_data.setJson("/api/update/rollback", loadFixture("updateRestarting.json"), "POST");
    start();

    EXPECT_EQ(m_app->updates().stripText(), "Updated from v0.1.15");
    ASSERT_FALSE(m_app->updates().rollbackButton()->hidden());
    EXPECT_EQ(m_app->updates().rollbackButton()->getText(), "Roll back to v0.1.15");
    shot("update-after");

    click(m_app->updates().rollbackButton());
    ASSERT_NE(m_app->updates().confirmDialog(), nullptr);
    EXPECT_EQ(m_data.count("/api/update/rollback", "POST"), 0u);
    m_app->updates().confirmDialog()->close(gweni::dialogs::Result::Ok);
    frames(2);
    EXPECT_EQ(m_data.count("/api/update/rollback", "POST"), 1u);
    EXPECT_EQ(m_app->updates().progressLabel()->getText(), "Restarting the server...");
}

TEST_F(DashboardTest, AFailedUpdateShowsItsError)
{
    m_data.setJson("/api/update", loadFixture("updateRolledBack.json"));
    start();

    EXPECT_EQ(m_app->updates().stripText(), "Last update: v0.1.16 failed to start, rolled back");
    EXPECT_TRUE(m_app->updates().rollbackButton()->hidden());
    // the release is still offered by hand
    EXPECT_FALSE(m_app->updates().updateButton()->hidden());
    shot("update-rolled-back");
}

TEST_F(DashboardTest, AServerBeforeApplyShowsOnlyTheBadge)
{
    m_data.setJson("/api/update", loadFixture("update.json"));
    start();

    EXPECT_TRUE(m_app->updateBadge()->visible());
    EXPECT_TRUE(m_app->updates().updateButton()->hidden());
    EXPECT_TRUE(m_app->updates().strip()->hidden());
}

TEST_F(DashboardTest, TheHeaderVersionIsAskedForAgainAfterAReconnect)
{
    start();
    ASSERT_EQ(m_data.count("/api/version"), 1u);
    frames(120*2);
    EXPECT_EQ(m_data.count("/api/version"), 1u);

    m_data.setFailure("/api/stats", 0, "unreachable");
    frames(120);
    m_data.setJson("/api/stats", loadFixture("stats.json"));
    nlohmann::json version=loadFixture("version.json");
    version["version"]="0.1.16";
    m_data.setJson("/api/version", version);
    frames(120*2);
    EXPECT_EQ(m_app->version().rfind("v0.1.16", 0), 0u)<<m_app->version();
}
