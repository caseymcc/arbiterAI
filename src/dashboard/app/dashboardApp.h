#ifndef _ARBITERAI_DASHBOARD_DASHBOARDAPP_H_
#define _ARBITERAI_DASHBOARD_DASHBOARDAPP_H_

#include "data/dataSource.h"
#include "panels/configPanel.h"
#include "panels/logPanel.h"
#include "panels/overviewPanel.h"
#include "panels/storagePanel.h"

#include <gweni/controls.h>

#include <ctime>
#include <functional>
#include <memory>
#include <string>

namespace arbiterAI
{
namespace dashboard
{

enum class Tab
{
    Overview,
    Storage,
    Config
};

/// Where a dashboard path leads: /dashboard, /dashboard/storage,
/// /dashboard/config, /dashboard/logs (the Overview with the log expanded).
struct Route
{
    Tab tab=Tab::Overview;
    bool logs=false;
};

/// \p url a path or a whole URL; its query and fragment are ignored.
Route routeForUrl(const std::string &url);
std::string pathForTab(Tab tab);

/// The whole dashboard on a canvas: a header with the version and the
/// connection status, the tabs, and the log panel under them. It polls the
/// server every PollSeconds through \p data, natively or in a page alike.
class DashboardApp
{
public:
    static constexpr double PollSeconds=2.0;

    /// \p url the page's URL, which picks the tab; \p pushPath is told the
    /// path of each tab the user switches to (the page's history).
    /// \p now the clock the Storage tab's age colours are measured against
    DashboardApp(gweni::controls::Canvas *canvas, DataSource &data, const std::string &url,
        std::function<void(const std::string &path)> pushPath=nullptr, std::function<std::time_t()> now=nullptr);
    ~DashboardApp();

    /// The page's URL changed (back, forward): show its tab, pushing nothing.
    void navigate(const std::string &url);
    /// The user picked \p tab, as a click on it does.
    void selectTab(Tab tab);
    Tab currentTab() const;

    /// One round of requests; the timer calls it every PollSeconds.
    void poll();
    /// the Storage tab's requests (poll() makes them while it shows)
    void pollStorage();

    const std::string &version() const { return m_version->getText(); }
    const std::string &status() const { return m_status->getText(); }
    bool connected() const { return m_connected; }

    OverviewPanel &overview() { return *m_overview; }
    StoragePanel &storage() { return *m_storage; }
    ConfigPanel &config() { return *m_config; }
    LogPanel &logPanel() { return *m_logPanel; }
    gweni::controls::TabButton *tabButton(Tab tab) const;

private:
    void showTab(Tab tab);
    void setConnected(bool connected, const std::string &error=std::string());

    DataSource &m_data;
    std::function<void(const std::string &path)> m_pushPath;

    gweni::controls::Canvas *m_canvas;
    gweni::controls::Base *m_root;
    gweni::controls::Label *m_version;
    gweni::controls::Label *m_status;
    gweni::controls::TabControl *m_tabs;
    gweni::controls::TabButton *m_tabButtons[3];
    std::unique_ptr<OverviewPanel> m_overview;
    std::unique_ptr<StoragePanel> m_storage;
    std::unique_ptr<ConfigPanel> m_config;
    std::unique_ptr<LogPanel> m_logPanel;
    gweni::controls::TimerHandle m_pollTimer;

    bool m_haveVersion=false;
    bool m_connected=false;
    bool m_navigating=false;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_DASHBOARDAPP_H_
