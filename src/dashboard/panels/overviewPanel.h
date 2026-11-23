#ifndef _ARBITERAI_DASHBOARD_OVERVIEWPANEL_H_
#define _ARBITERAI_DASHBOARD_OVERVIEWPANEL_H_

#include <gweni/controls.h>
#include <nlohmann/json.hpp>

namespace arbiterAI
{
namespace dashboard
{

/// The Overview tab: performance and the server's state, from /api/stats.
class OverviewPanel
{
public:
    explicit OverviewPanel(gweni::controls::Base *page);

    void updateStats(const nlohmann::json &stats);

    const std::string &promptSpeed() const { return m_promptSpeed->getText(); }
    const std::string &generationSpeed() const { return m_generationSpeed->getText(); }
    const std::string &activeRequests() const { return m_activeRequests->getText(); }

private:
    gweni::controls::Label *m_promptSpeed;
    gweni::controls::Label *m_generationSpeed;
    gweni::controls::Label *m_activeRequests;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_OVERVIEWPANEL_H_
