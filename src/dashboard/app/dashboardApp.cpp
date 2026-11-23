#include "dashboardApp.h"
#include "panels/widgets.h"

namespace arbiterAI
{
namespace dashboard
{

namespace
{

const char *TabNames[]={"Overview", "Storage", "Config"};

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

    // the new dashboard is at /dashboard/next until it replaces the old pages
    if(path=="/dashboard/next" || path.rfind("/dashboard/next/", 0)==0)
        route.base="/dashboard/next";

    std::string rest=path.size()>route.base.size()?path.substr(route.base.size()):std::string();
    if(rest=="/storage")
        route.tab=Tab::Storage;
    else if(rest=="/config")
        route.tab=Tab::Config;
    else if(rest=="/logs")
        route.logs=true;
    return route;
}

std::string pathForTab(const std::string &base, Tab tab)
{
    switch(tab)
    {
    case Tab::Storage:
        return base+"/storage";
    case Tab::Config:
        return base+"/config";
    default:
        return base;
    }
}

DashboardApp::DashboardApp(gweni::controls::Canvas *canvas, DataSource &data, const std::string &url,
    std::function<void(const std::string &path)> pushPath):
    m_data(data),
    m_pushPath(std::move(pushPath)),
    m_canvas(canvas)
{
    Route route=routeForUrl(url);
    m_base=route.base;

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

    m_status=header->newChild<gweni::controls::Label>();
    m_status->setDock(gweni::DockPosition::Center);
    m_status->setTextAlignment(gweni::Alignment::Right | gweni::Alignment::CenterV);

    m_logPanel=std::make_unique<LogPanel>(m_root);
    m_logPanel->setExpanded(route.logs);

    m_tabs=m_root->newChild<gweni::controls::TabControl>();
    m_tabs->setDock(gweni::DockPosition::Center);
    for(int i=0; i<3; ++i)
    {
        gweni::controls::Base *page=m_root->newChild<gweni::controls::Base>();
        page->setPadding(gweni::Padding(8, 8, 8, 8));
        m_tabButtons[i]=m_tabs->addPage(TabNames[i], page);
        m_tabButtons[i]->onPressCaller.addCallback(m_tabButtons[i], [this, i](gweni::event::Handler &, gweni::event::Info)
        {
            if(!m_navigating && m_pushPath)
                m_pushPath(pathForTab(m_base, static_cast<Tab>(i)));
        });
    }
    m_overview=std::make_unique<OverviewPanel>(tabButton(Tab::Overview)->getPage());
    addHeading(tabButton(Tab::Storage)->getPage(), "Downloaded models: coming with the Storage panel");
    addHeading(tabButton(Tab::Config)->getPage(), "Startup configuration: coming with the Config panel");

    showTab(route.tab);
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
    if(route.logs)
        m_logPanel->setExpanded(true);
}

void DashboardApp::selectTab(Tab tab)
{
    if(tab==currentTab())
        return;
    tabButton(tab)->onPress(gweni::event::Info(tabButton(tab)));
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
            if(!response.ok)
                return;
            m_haveVersion=true;
            std::string text="v"+response.json.value("version", std::string("?"));
            std::string llama=response.json.value("llamaCppBuild", std::string());
            if(!llama.empty() && llama!="disabled")
                text+="   llama.cpp "+llama;
            m_version->setText(text);
        });
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
