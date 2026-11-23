#include "dashboardApp.h"
#include "panels/widgets.h"

#include <gweni/platforms/platform.h>

namespace arbiterAI
{
namespace dashboard
{

namespace
{

const char *TabNames[]={"Overview", "Storage", "Config"};

/// A TabControl that says when its tab changed, by a click or by the arrow
/// keys (TabButton's key handlers select the next tab without a press).
class Tabs: public gweni::controls::TabControl
{
public:
    GWENI_CONTROL_INLINE(Tabs, gweni::controls::TabControl) {}

    std::function<void()> onChanged;

    void onTabPressed(gweni::event::Info info) override
    {
        gweni::controls::TabButton *before=getCurrentButton();
        ParentClass::onTabPressed(info);
        if(getCurrentButton()!=before && onChanged)
            onChanged();
    }
};

/// The path of \p url: no scheme and host, no query or fragment, no trailing /.
std::string pathOf(const std::string &url)
{
    std::string path=url;

    size_t scheme=path.find("://");
    if(scheme!=std::string::npos)
    {
        size_t slash=path.find('/', scheme+3);
        path=slash==std::string::npos?"/":path.substr(slash);
    }
    size_t end=path.find_first_of("?#");
    if(end!=std::string::npos)
        path=path.substr(0, end);
    while(path.size()>1 && path.back()=='/')
        path.pop_back();
    return path;
}

}//namespace

Route routeForUrl(const std::string &url)
{
    Route route;
    std::string path=pathOf(url);

    const std::string base="/dashboard";
    std::string rest=path.rfind(base, 0)==0?path.substr(base.size()):std::string();
    if(rest=="/storage")
        route.tab=Tab::Storage;
    else if(rest=="/config")
        route.tab=Tab::Config;
    else if(rest=="/logs")
        route.logs=true;
    return route;
}

std::string pathForTab(Tab tab)
{
    switch(tab)
    {
    case Tab::Storage:
        return "/dashboard/storage";
    case Tab::Config:
        return "/dashboard/config";
    default:
        return "/dashboard";
    }
}

DashboardApp::DashboardApp(gweni::controls::Canvas *canvas, DataSource &data, const std::string &url,
    std::function<void(const std::string &path)> pushPath, std::function<std::time_t()> now):
    m_data(data),
    m_pushPath(std::move(pushPath)),
    m_canvas(canvas)
{
    Route route=routeForUrl(url);

    m_root=canvas->newChild<gweni::controls::Base>();
    m_root->setDock(gweni::DockPosition::Center);
    m_root->setPadding(gweni::Padding(12, 8, 12, 8));

    gweni::controls::Base *header=m_root->newChild<gweni::controls::Base>();
    header->setDock(gweni::DockPosition::Top);
    header->setHeight(32);

    gweni::controls::Label *title=header->newChild<gweni::controls::Label>();
    title->setText("ArbiterAI Dashboard");
    title->setTextColor(palette::accent);
    title->setDock(gweni::DockPosition::Left);
    title->setMargin(gweni::Margin(0, 0, 16, 0));

    m_version=header->newChild<gweni::controls::Label>();
    m_version->setText("");
    m_version->setTextColor(palette::muted);
    m_version->setDock(gweni::DockPosition::Left);

    m_update=header->newChild<gweni::controls::LabelClickable>();
    m_update->setTextColorOverride(palette::warn);
    m_update->setDock(gweni::DockPosition::Left);
    m_update->setMargin(gweni::Margin(16, 0, 0, 0));
    m_update->setToolTipText("Open the release notes");
    m_update->setHidden(true);
    m_update->onPressCaller.addCallback(m_update, [this](gweni::event::Handler &, gweni::event::Info)
    {
        if(!m_releaseUrl.empty())
            gweni::platform::openUrl(m_releaseUrl);
    });

    m_status=header->newChild<gweni::controls::Label>();
    m_status->setDock(gweni::DockPosition::Center);
    m_status->setTextAlignment(gweni::Alignment::Right | gweni::Alignment::CenterV);

    m_logPanel=std::make_unique<LogPanel>(m_root, m_data);
    m_logPanel->setExpanded(route.logs);

    Tabs *tabs=m_root->newChild<Tabs>();
    m_tabs=tabs;
    m_tabs->setDock(gweni::DockPosition::Center);
    for(int i=0; i<3; ++i)
    {
        gweni::controls::Base *page=m_root->newChild<gweni::controls::Base>();
        page->setPadding(gweni::Padding(8, 8, 8, 8));
        m_tabButtons[i]=m_tabs->addPage(TabNames[i], page);
    }
    m_overview=std::make_unique<OverviewPanel>(tabButton(Tab::Overview)->getPage(), m_data, [this]() { poll(); });
    m_storage=std::make_unique<StoragePanel>(tabButton(Tab::Storage)->getPage(), m_data, [this]() { pollStorage(); }, std::move(now));
    m_config=std::make_unique<ConfigPanel>(tabButton(Tab::Config)->getPage(), m_data);

    // the first poll below fills the tab the URL opens
    showTab(route.tab);
    tabs->onChanged=[this]() { tabChanged(); };
    setConnected(false, "connecting...");

    m_pollTimer=canvas->every(PollSeconds, [this]() { poll(); }, m_root);
    poll();
}

DashboardApp::~DashboardApp()
{
    m_canvas->cancel(m_pollTimer);
}

void DashboardApp::navigate(const std::string &url)
{
    Route route=routeForUrl(url);
    showTab(route.tab);
    if(route.logs && !m_logPanel->expanded())
    {
        m_logPanel->setExpanded(true);
        m_logPanel->poll();
    }
}

void DashboardApp::selectTab(Tab tab)
{
    if(tab==currentTab())
        return;
    tabButton(tab)->onPress(gweni::event::Info(tabButton(tab)));
}

void DashboardApp::tabChanged()
{
    const Tab tab=currentTab();
    if(!m_navigating && m_pushPath)
        m_pushPath(pathForTab(tab));
    // the Storage tab polls only while it shows: fill it now
    if(tab==Tab::Storage)
        pollStorage();
    // the Config tab loads once, when it first shows
    if(tab==Tab::Config && !m_config->loaded())
        m_config->load();
}

Tab DashboardApp::currentTab() const
{
    for(int i=0; i<3; ++i)
    {
        if(m_tabs->getCurrentButton()==m_tabButtons[i])
            return static_cast<Tab>(i);
    }
    return Tab::Overview;
}

void DashboardApp::poll()
{
    if(!m_haveVersion)
    {
        m_data.getJson("/api/version", [this](const Response &response)
        {
            if(!response.ok || !response.json.is_object())
                return;
            m_haveVersion=true;
            std::string version="v"+text(response.json, "version", "?");
            std::string llama=text(response.json, "llamaCppBuild");
            if(!llama.empty() && llama!="disabled")
                version+="   llama.cpp "+llama;
            m_version->setText(version);
        });
    }

    if(m_pollsUntilUpdate--==0)
    {
        m_pollsUntilUpdate=UpdatePollEvery-1;
        pollUpdate();
    }

    m_data.getJson("/api/stats", [this](const Response &response)
    {
        if(!response.ok)
        {
            setConnected(false, response.error);
            return;
        }
        setConnected(true);
        m_overview->updateStats(response.json);
    });
    m_data.getJson("/api/hardware", [this](const Response &response)
    {
        if(response.ok)
            m_overview->updateHardware(response.json);
    });
    m_data.getJson("/api/stats/history?minutes=5", [this](const Response &response)
    {
        if(response.ok)
            m_overview->updateHistory(response.json);
    });
    m_data.getJson("/api/stats/swaps", [this](const Response &response)
    {
        if(response.ok)
            m_overview->updateSwaps(response.json);
    });
    m_data.getJson("/api/scheduler/jobs", [this](const Response &response)
    {
        if(response.ok)
            m_overview->updateJobs(response.json);
    });
    m_data.getJson("/api/downloads", [this](const Response &response)
    {
        const nlohmann::json downloads=response.ok?response.json:nlohmann::json::object();
        m_overview->updateDownloads(downloads);
        m_storage->updateDownloads(downloads);
    });
    if(currentTab()==Tab::Storage)
        pollStorage();
    // until the configuration arrives (a server restarting), each poll asks again
    if(currentTab()==Tab::Config && !m_config->loaded())
        m_config->load();
    m_logPanel->poll();
}

void DashboardApp::pollStorage()
{
    m_data.getJson("/api/storage", [this](const Response &response)
    {
        if(response.ok)
            m_storage->updateStorage(response.json);
    });
    m_data.getJson("/api/storage/models", [this](const Response &response)
    {
        if(response.ok)
            m_storage->updateModels(response.json);
    });
    m_data.getJson("/api/storage/cleanup/preview", [this](const Response &response)
    {
        if(response.ok)
            m_storage->updateCleanup(response.json);
    });
}

void DashboardApp::pollUpdate()
{
    m_data.getJson("/api/update", [this](const Response &response)
    {
        // a server without the check (404) or one that failed keeps the plain header
        if(!response.ok || !response.json.is_object() || !flag(response.json, "update_available"))
        {
            m_update->setHidden(true);
            m_releaseUrl.clear();
            return;
        }

        m_releaseUrl=text(response.json, "release_url");
        m_update->setText("v"+text(response.json, "current", "?")+" \u2192 v"+text(response.json, "latest", "?")+" available");
        m_update->sizeToContents();
        m_update->setHidden(false);
    });
}

gweni::controls::TabButton *DashboardApp::tabButton(Tab tab) const
{
    return m_tabButtons[static_cast<int>(tab)];
}

void DashboardApp::showTab(Tab tab)
{
    m_navigating=true;
    selectTab(tab);
    m_navigating=false;
}

void DashboardApp::setConnected(bool connected, const std::string &error)
{
    m_connected=connected;
    if(connected)
    {
        m_status->setText("Connected");
        m_status->setTextColor(palette::good);
    }
    else
    {
        m_status->setText("Disconnected: "+error);
        m_status->setTextColor(palette::bad);
    }
}

}//namespace dashboard
}//namespace arbiterAI
