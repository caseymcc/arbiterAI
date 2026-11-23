#include "dashboardApp.h"

namespace arbiterAI
{
namespace dashboard
{

DashboardApp::DashboardApp(gweni::controls::Canvas *canvas)
{
    m_title=canvas->newChild<gweni::controls::Label>();
    m_title->setText("ArbiterAI dashboard");
    m_title->setDock(gweni::DockPosition::Top);
}

}//namespace dashboard
}//namespace arbiterAI
