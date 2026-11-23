#include "dashboardTest.h"

#include <filesystem>
#include <fstream>

extern std::string g_shotDir;

const std::string &shotDir()
{
    static std::string dir=[]()
    {
        std::string path=g_shotDir.empty()?::testing::TempDir()+"dashboard_shots":g_shotDir;
        std::filesystem::create_directories(path);
        return path;
    }();
    return dir;
}

nlohmann::json loadFixture(const std::string &name)
{
    std::ifstream file(std::string(DASHBOARD_FIXTURE_DIR)+"/"+name);
    EXPECT_TRUE(file.good())<<"no fixture "<<name;
    return nlohmann::json::parse(file, nullptr, false);
}

void DashboardTest::SetUp()
{
    gweni::app::HeadlessOptions options;
    options.size=gweni::Point(1280, 800);
    options.preloadGlyphFallbacks=false;
    m_headless=std::make_unique<gweni::app::Headless>(options);
    ASSERT_TRUE(m_headless->ok())<<m_headless->error();

    m_data.setJson("/api/version", loadFixture("version.json"));
    m_data.setJson("/api/stats", loadFixture("stats.json"));
    m_data.setJson("/api/hardware", loadFixture("hardware.json"));
    m_data.setJson("/api/stats/history?minutes=5", loadFixture("history.json"));
    m_data.setJson("/api/stats/swaps", loadFixture("swaps.json"));
    m_data.setJson("/api/scheduler/jobs", loadFixture("jobs.json"));
    m_data.setJson("/api/downloads", loadFixture("downloads.json"));
}

void DashboardTest::TearDown()
{
    m_app.reset();
    m_headless.reset();
}

void DashboardTest::start(const std::string &url)
{
    m_app=std::make_unique<arbiterAI::dashboard::DashboardApp>(m_headless->canvas(), m_data, url,
        [this](const std::string &path) { m_pushed.push_back(path); });
    frames(2);
}

void DashboardTest::click(gweni::controls::Base *control)
{
    const gweni::Rect &bounds=control->getBounds();
    gweni::Point centre=control->localPosToCanvas(gweni::Point(bounds.w/2, bounds.h/2));
    m_headless->click(centre.x, centre.y);
    frames(2);
}

std::string DashboardTest::shot(const std::string &name)
{
    std::string path=shotDir()+"/"+name+".png";
    EXPECT_TRUE(m_headless->savePng(path))<<path;
    return path;
}
