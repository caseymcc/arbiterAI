#include "logPanel.h"
#include "widgets.h"

#include <gweni/platforms/platform.h>

#include <cctype>
#include <cstdio>
#include <ctime>

namespace arbiterAI
{
namespace dashboard
{

namespace
{
const int HeaderHeight=24;
const int ExpandedHeight=260;
const char *Levels[]={"trace", "debug", "info", "warning", "error", "critical"};

gweni::Color levelColor(const std::string &level)
{
    if(level=="error" || level=="critical")
        return palette::bad;
    if(level=="warning")
        return palette::warn;
    if(level=="debug" || level=="trace")
        return palette::muted;
    return gweni::Color(200, 204, 220);
}
}

std::string formatLogTime(int64_t epochMs)
{
    const std::time_t seconds=static_cast<std::time_t>(epochMs/1000);
    std::tm tm{};
    localtime_r(&seconds, &tm);
    char text[32];
    std::snprintf(text, sizeof(text), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(epochMs%1000));
    return text;
}

LogPanel::LogPanel(gweni::controls::Base *parent, DataSource &data):
    m_data(data)
{
    m_panel=parent->newChild<gweni::controls::Base>();
    m_panel->setDock(gweni::DockPosition::Bottom);

    m_header=m_panel->newChild<gweni::controls::Button>();
    m_header->setDock(gweni::DockPosition::Top);
    m_header->setHeight(HeaderHeight);
    m_header->setTextAlignment(gweni::Alignment::Left | gweni::Alignment::CenterV);
    m_header->onPressCaller.addCallback(m_header, [this](gweni::event::Handler &, gweni::event::Info)
    {
        setExpanded(!m_expanded);
        poll();
    });

    m_body=m_panel->newChild<gweni::controls::Base>();
    m_body->setDock(gweni::DockPosition::Center);

    gweni::controls::Base *toolbar=m_body->newChild<gweni::controls::Base>();
    toolbar->setDock(gweni::DockPosition::Top);
    toolbar->setHeight(26);
    toolbar->setPadding(gweni::Padding(0, 2, 0, 2));

    m_levelBox=toolbar->newChild<gweni::controls::ComboBox>();
    m_levelBox->setDock(gweni::DockPosition::Left);
    fixWidth(m_levelBox, 120);
    m_levelBox->addItem("All levels", "");
    for(const char *level:Levels)
    {
        std::string label=level;
        label[0]=static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        m_levelBox->addItem(label, level);
    }
    m_levelBox->onSelection.addCallback(m_levelBox, [this](gweni::event::Handler &, gweni::event::Info)
    {
        gweni::controls::Label *item=m_levelBox->getSelectedItem();
        std::string level=item?item->getName():std::string();
        if(level!=m_level)
            setLevel(level);
    });

    m_liveBox=toolbar->newChild<gweni::controls::CheckBoxWithLabel>();
    m_liveBox->setDock(gweni::DockPosition::Left);
    fixWidth(m_liveBox, 70);
    m_liveBox->setMargin(gweni::Margin(12, 0, 0, 0));
    m_liveBox->label()->setText("Live");
    m_liveBox->checkBox()->setChecked(true);
    m_liveBox->checkBox()->onCheckChanged.addCallback(m_liveBox, [this](gweni::event::Handler &, gweni::event::Info)
    {
        setLive(live());
    });

    m_copy=toolbar->newChild<gweni::controls::Button>();
    m_copy->setText("Copy selected");
    m_copy->setDock(gweni::DockPosition::Left);
    fixWidth(m_copy, 110);
    m_copy->setMargin(gweni::Margin(12, 0, 0, 0));
    m_copy->onPressCaller.addCallback(m_copy, [this](gweni::event::Handler &, gweni::event::Info)
    {
        copySelection();
    });

    m_view=m_body->newChild<gweni::controls::LogView>();
    m_view->setDock(gweni::DockPosition::Center);
    m_view->setMaxRows(Count*5);
    // the skin's monospace face, so the times and levels line up
    if(gweni::skin::Base *skin=m_view->getSkin())
        m_view->setFont(skin->getDefaultMonoFont());

    setExpanded(false);
}

void LogPanel::setExpanded(bool expanded)
{
    m_expanded=expanded;
    m_header->setText(expanded?"v  Server Log":">  Server Log");
    m_body->setHidden(!expanded);
    m_panel->setHeight(expanded?ExpandedHeight:HeaderHeight);
}

std::string LogPanel::path() const
{
    std::string path="/api/logs?count="+std::to_string(Count);
    if(!m_level.empty())
        path+="&level="+urlEncode(m_level);
    return path;
}

void LogPanel::poll()
{
    if(!m_expanded || !live())
        return;
    const int generation=m_generation;
    m_data.getJson(path(), [this, generation](const Response &response)
    {
        if(response.ok && generation==m_generation)
            updateLogs(response.json);
    });
}

void LogPanel::reload()
{
    ++m_generation;
    m_view->clear();
    m_lastEpoch=-1;
    m_atLastEpoch=0;
    if(!m_expanded)
        return;
    // a reload always asks, live or not (as the old page's forced refresh)
    const int generation=m_generation;
    m_data.getJson(path(), [this, generation](const Response &response)
    {
        if(response.ok && generation==m_generation)
            updateLogs(response.json);
    });
}

void LogPanel::setLevel(const std::string &level)
{
    m_level=level;
    m_levelBox->selectItemByName(level, false);
    reload();
}

void LogPanel::setLive(bool live)
{
    if(m_liveBox->checkBox()->isChecked()!=live)
        m_liveBox->checkBox()->setChecked(live);
    m_view->setFollowing(live);
    if(live)
        reload();
}

void LogPanel::updateLogs(const nlohmann::json &logs)
{
    const nlohmann::json entries=logs.is_object()?logs.value("logs", nlohmann::json::array()):nlohmann::json::array();
    if(!entries.is_array())
        return;

    // the entries come oldest first: append those newer than the last one
    // shown, and of those at its time the ones past the count already shown
    const int64_t shownEpoch=m_lastEpoch;
    size_t atShownEpoch=0;
    for(const nlohmann::json &entry:entries)
    {
        const int64_t epoch=static_cast<int64_t>(number(entry, "epoch_ms"));
        if(epoch<shownEpoch)
            continue;
        if(epoch==shownEpoch && atShownEpoch++<m_atLastEpoch)
            continue;

        const std::string level=text(entry, "level", "info");
        std::string upper=level;
        for(char &c:upper)
            c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        upper.resize(9, ' ');
        m_view->append(gweni::controls::LogRow(formatLogTime(epoch)+"  "+upper+text(entry, "message"), levelColor(level)));
    }

    if(entries.empty())
        return;
    m_lastEpoch=static_cast<int64_t>(number(entries.back(), "epoch_ms"));
    m_atLastEpoch=0;
    for(const nlohmann::json &entry:entries)
    {
        if(static_cast<int64_t>(number(entry, "epoch_ms"))==m_lastEpoch)
            ++m_atLastEpoch;
    }
}

void LogPanel::copySelection()
{
    const std::string selected=m_view->getSelectedText();
    if(selected.empty() || !m_view->copySelection())
    {
        notify(m_panel, gweni::controls::ToastKind::Info, "Select log lines to copy first (click, Shift+click, Ctrl+A).");
        return;
    }
    size_t lines=1;
    for(char c:selected)
    {
        if(c=='\n')
            ++lines;
    }
    notify(m_panel, gweni::controls::ToastKind::Positive, "Copied "+std::to_string(lines)+(lines==1?" line":" lines"));
}

}//namespace dashboard
}//namespace arbiterAI
