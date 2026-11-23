#include "app/dashboardApp.h"

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

    arbiterAI::dashboard::DashboardApp app(headless.canvas());
    headless.frame(2);

    EXPECT_EQ(app.title()->getText(), "ArbiterAI dashboard");
    EXPECT_EQ(headless.pixelSize().x, 1280);
    if(!g_shotDir.empty())
        EXPECT_TRUE(headless.savePng(g_shotDir+"/smoke.png"));
}
