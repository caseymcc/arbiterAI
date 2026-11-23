#include "logPanel.h"
#include "widgets.h"

namespace arbiterAI
{
namespace dashboard
{

namespace
{
const int HeaderHeight=24;
const int ExpandedHeight=240;
}

LogPanel::LogPanel(gweni::controls::Base *parent)
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
    });

    m_body=m_panel->newChild<gweni::controls::Base>();
    m_body->setDock(gweni::DockPosition::Center);

    setExpanded(false);
}

void LogPanel::setExpanded(bool expanded)
{
    m_expanded=expanded;
    m_header->setText(expanded?"v  Server Log":">  Server Log");
    m_body->setHidden(!expanded);
    m_panel->setHeight(expanded?ExpandedHeight:HeaderHeight);
}

}//namespace dashboard
}//namespace arbiterAI
