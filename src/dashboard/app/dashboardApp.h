#ifndef _ARBITERAI_DASHBOARD_DASHBOARDAPP_H_
#define _ARBITERAI_DASHBOARD_DASHBOARDAPP_H_

#include <gweni/controls.h>

namespace arbiterAI
{
namespace dashboard
{

class DashboardApp
{
public:
    explicit DashboardApp(gweni::controls::Canvas *canvas);

    gweni::controls::Label *title() const { return m_title; }

private:
    gweni::controls::Label *m_title;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_DASHBOARDAPP_H_
