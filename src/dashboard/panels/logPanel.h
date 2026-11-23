#ifndef _ARBITERAI_DASHBOARD_LOGPANEL_H_
#define _ARBITERAI_DASHBOARD_LOGPANEL_H_

#include <gweni/controls.h>

namespace arbiterAI
{
namespace dashboard
{

/// The server log, a panel under every tab: a header that folds it, and the
/// log below it when expanded.
class LogPanel
{
public:
    explicit LogPanel(gweni::controls::Base *parent);

    bool expanded() const { return m_expanded; }
    void setExpanded(bool expanded);

    gweni::controls::Button *header() const { return m_header; }
    gweni::controls::Base *body() const { return m_body; }

private:
    gweni::controls::Base *m_panel;
    gweni::controls::Button *m_header;
    gweni::controls::Base *m_body;
    bool m_expanded=true;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_LOGPANEL_H_
