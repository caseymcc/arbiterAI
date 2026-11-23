#ifndef _ARBITERAI_DASHBOARD_TESTS_DASHBOARDTEST_H_
#define _ARBITERAI_DASHBOARD_TESTS_DASHBOARDTEST_H_

#include "app/dashboardApp.h"
#include "data/fixtureDataSource.h"

#include <gweni/app/headless.h>

#include <gtest/gtest.h>

#include <ctime>
#include <memory>
#include <string>
#include <vector>

/// --shots <dir> on the command line (tests/main.cpp); a temporary directory
/// otherwise
const std::string &shotDir();

/// The tests' clock: 2026-11-23T00:00:00Z, so the fixtures' ages do not move.
constexpr std::time_t FixtureNow=1795392000;

/// A fixture JSON file of tests/fixtures/.
nlohmann::json loadFixture(const std::string &name);

/// The app on a headless canvas (surfaceless EGL, llvmpipe), its data from
/// FixtureDataSource: the fixtures every tab polls are set before start().
/// The clock is stepped, 60 frames a second, so frames(120) is one poll.
class DashboardTest: public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    /// opens the app at \p url and draws its first frames
    void start(const std::string &url="/dashboard");
    void frames(int count) { m_headless->frame(count); }
    /// a click on the middle of \p control
    void click(gweni::controls::Base *control);
    /// \p name.png in shotDir(); \return its path
    std::string shot(const std::string &name);

    std::unique_ptr<gweni::app::Headless> m_headless;
    arbiterAI::dashboard::FixtureDataSource m_data;
    std::unique_ptr<arbiterAI::dashboard::DashboardApp> m_app;
    std::vector<std::string> m_pushed;
};

#endif//_ARBITERAI_DASHBOARD_TESTS_DASHBOARDTEST_H_
