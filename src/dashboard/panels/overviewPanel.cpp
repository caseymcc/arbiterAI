#include "overviewPanel.h"
#include "widgets.h"

namespace arbiterAI
{
namespace dashboard
{

OverviewPanel::OverviewPanel(gweni::controls::Base *page)
{
    gweni::controls::Base *performance=page->newChild<gweni::controls::Base>();
    performance->setDock(gweni::DockPosition::Top);
    performance->setWidth(420);
    performance->setHeight(110);
    addHeading(performance, "PERFORMANCE");
    m_promptSpeed=addStatRow(performance, "Prompt Speed");
    m_generationSpeed=addStatRow(performance, "Generation Speed");
    m_activeRequests=addStatRow(performance, "Active Requests");
}

void OverviewPanel::updateStats(const nlohmann::json &stats)
{
    m_promptSpeed->setText(formatRate(stats.value("avg_prompt_tokens_per_second", 0.0)));
    m_generationSpeed->setText(formatRate(stats.value("avg_generation_tokens_per_second", 0.0)));
    m_activeRequests->setText(std::to_string(stats.value("active_requests", 0)));
}

}//namespace dashboard
}//namespace arbiterAI
