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
