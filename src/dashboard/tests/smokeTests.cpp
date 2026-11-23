#include "app/dashboardApp.h"
#include "data/fixtureDataSource.h"

#include <gweni/app/headless.h>

#include <gtest/gtest.h>

#include <string>

extern std::string g_shotDir;

TEST(DashboardSmoke, RendersHeadless)
{
    gweni::app::HeadlessOptions options;
    options.size=gweni::Point(1280, 800);
    options.preloadGlyphFallbacks=false;
    gweni::app::Headless headless(options);
    ASSERT_TRUE(headless.ok()) << headless.error();

    arbiterAI::dashboard::FixtureDataSource data;
    data.setJson("/api/version", {{"version", "0.1.15"}, {"llamaCppBuild", "b9204"}});
    data.setJson("/api/stats", {{"avg_prompt_tokens_per_second", 812.5}, {"avg_generation_tokens_per_second", 41.25}, {"active_requests", 2}});

    arbiterAI::dashboard::DashboardApp app(headless.canvas(), data, "/dashboard");
    headless.frame(2);

    EXPECT_EQ(app.version(), "v0.1.15   llama.cpp b9204");
    EXPECT_EQ(app.overview().promptSpeed(), "812.5 t/s");
    if(!g_shotDir.empty())
        EXPECT_TRUE(headless.savePng(g_shotDir+"/smoke.png"));
}
