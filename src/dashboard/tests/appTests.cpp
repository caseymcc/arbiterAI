#include "dashboardTest.h"

#include <filesystem>

using arbiterAI::dashboard::Route;
using arbiterAI::dashboard::Tab;
using arbiterAI::dashboard::routeForUrl;

TEST(Route, TabFromThePath)
{
    EXPECT_EQ(routeForUrl("/dashboard").tab, Tab::Overview);
    EXPECT_EQ(routeForUrl("/dashboard/").tab, Tab::Overview);
    EXPECT_EQ(routeForUrl("/dashboard/storage").tab, Tab::Storage);
    EXPECT_EQ(routeForUrl("/dashboard/config?x=1#y").tab, Tab::Config);
    EXPECT_EQ(routeForUrl("http://host:8080/dashboard/storage").tab, Tab::Storage);

    Route logs=routeForUrl("/dashboard/logs");
    EXPECT_EQ(logs.tab, Tab::Overview);
    EXPECT_TRUE(logs.logs);
    EXPECT_FALSE(routeForUrl("/dashboard").logs);
}

TEST(Route, BaseIsKeptForTheNextPages)
{
    Route route=routeForUrl("/dashboard/next/config");
    EXPECT_EQ(route.base, "/dashboard/next");
    EXPECT_EQ(route.tab, Tab::Config);
    EXPECT_EQ(routeForUrl("/dashboard/config").base, "/dashboard");
    EXPECT_EQ(arbiterAI::dashboard::pathForTab("/dashboard/next", Tab::Storage), "/dashboard/next/storage");
    EXPECT_EQ(arbiterAI::dashboard::pathForTab("/dashboard", Tab::Overview), "/dashboard");
}

TEST_F(DashboardTest, ShowsTheServerVersion)
{
    start();
    EXPECT_EQ(m_app->version(), "v0.1.15   llama.cpp b9204");
}

TEST_F(DashboardTest, ShowsStatsFromTheServer)
{
    start();
    EXPECT_TRUE(m_app->connected());
    EXPECT_EQ(m_app->status(), "Connected");
    EXPECT_EQ(m_app->overview().promptSpeed(), "812.5 t/s");
    EXPECT_EQ(m_app->overview().generationSpeed(), "41.8 t/s");
    EXPECT_EQ(m_app->overview().activeRequests(), "2");
}

TEST_F(DashboardTest, PollsEveryTwoSeconds)
{
    start();
    EXPECT_EQ(m_data.count("/api/stats"), 1u);
    frames(60);
    EXPECT_EQ(m_data.count("/api/stats"), 1u);
    frames(65);
    EXPECT_EQ(m_data.count("/api/stats"), 2u);
    frames(120);
    EXPECT_EQ(m_data.count("/api/stats"), 3u);
    // the version is asked for once it has arrived
    EXPECT_EQ(m_data.count("/api/version"), 1u);
}

TEST_F(DashboardTest, NewStatsReplaceTheOldOnTheNextPoll)
{
    start();
    nlohmann::json stats=loadFixture("stats.json");
    stats["active_requests"]=7;
    m_data.setJson("/api/stats", stats);
    frames(125);
    EXPECT_EQ(m_app->overview().activeRequests(), "7");
}

TEST_F(DashboardTest, FailedFetchShowsDisconnectedAndRecovers)
{
    m_data.setFailure("/api/stats", 0, "");
    start();
    EXPECT_FALSE(m_app->connected());
    EXPECT_EQ(m_app->status(), "Disconnected: the server cannot be reached");
    shot("disconnected");

    m_data.setFailure("/api/stats", 503, "server is restarting");
    frames(125);
    EXPECT_EQ(m_app->status(), "Disconnected: server is restarting");

    m_data.setJson("/api/stats", loadFixture("stats.json"));
    frames(120);
    EXPECT_TRUE(m_app->connected());
    EXPECT_EQ(m_app->status(), "Connected");
    EXPECT_EQ(m_app->overview().promptSpeed(), "812.5 t/s");
}

TEST_F(DashboardTest, VersionIsAskedAgainUntilItArrives)
{
    m_data.setFailure("/api/version", 0, "");
    start();
    EXPECT_EQ(m_app->version(), "");

    m_data.setJson("/api/version", loadFixture("version.json"));
    frames(125);
    EXPECT_EQ(m_app->version(), "v0.1.15   llama.cpp b9204");
    EXPECT_EQ(m_data.count("/api/version"), 2u);
}

TEST_F(DashboardTest, OpensTheTabFromThePath)
{
    start("/dashboard/config");
    EXPECT_EQ(m_app->currentTab(), Tab::Config);
    EXPECT_FALSE(m_app->logPanel().expanded());
    EXPECT_TRUE(m_pushed.empty());
}

TEST_F(DashboardTest, LogsPathExpandsTheLogPanel)
{
    start("/dashboard/logs");
    EXPECT_EQ(m_app->currentTab(), Tab::Overview);
    EXPECT_TRUE(m_app->logPanel().expanded());
    EXPECT_FALSE(m_app->logPanel().body()->hidden());
}

TEST_F(DashboardTest, ClickingATabShowsItAndPushesItsPath)
{
    start("/dashboard/next");
    click(m_app->tabButton(Tab::Storage));
    EXPECT_EQ(m_app->currentTab(), Tab::Storage);
    ASSERT_EQ(m_pushed.size(), 1u);
    EXPECT_EQ(m_pushed[0], "/dashboard/next/storage");

    click(m_app->tabButton(Tab::Overview));
    ASSERT_EQ(m_pushed.size(), 2u);
    EXPECT_EQ(m_pushed[1], "/dashboard/next");
}

TEST_F(DashboardTest, NavigatingShowsTheTabWithoutPushing)
{
    start();
    m_app->navigate("/dashboard/storage");
    EXPECT_EQ(m_app->currentTab(), Tab::Storage);
    m_app->navigate("/dashboard/logs");
    EXPECT_EQ(m_app->currentTab(), Tab::Overview);
    EXPECT_TRUE(m_app->logPanel().expanded());
    EXPECT_TRUE(m_pushed.empty());
}

TEST_F(DashboardTest, LogHeaderFoldsAndUnfoldsThePanel)
{
    start();
    ASSERT_FALSE(m_app->logPanel().expanded());
    click(m_app->logPanel().header());
    EXPECT_TRUE(m_app->logPanel().expanded());
    click(m_app->logPanel().header());
    EXPECT_FALSE(m_app->logPanel().expanded());
}

TEST_F(DashboardTest, WritesAPngPerTab)
{
    start();
    const std::pair<Tab, const char *> tabs[]={{Tab::Overview, "overview"}, {Tab::Storage, "storage"}, {Tab::Config, "config"}};
    for(const auto &[tab, name]:tabs)
    {
        m_app->selectTab(tab);
        frames(2);
        ASSERT_EQ(m_app->currentTab(), tab);
        std::string path=shot(name);
        ASSERT_TRUE(std::filesystem::exists(path))<<path;
        EXPECT_GT(std::filesystem::file_size(path), 1000u)<<path;
    }
}
