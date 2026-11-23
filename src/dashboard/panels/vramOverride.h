#ifndef _ARBITERAI_DASHBOARD_VRAMOVERRIDE_H_
#define _ARBITERAI_DASHBOARD_VRAMOVERRIDE_H_

#include "data/dataSource.h"

#include <gweni/controls.h>
#include <gweni/controls/dialogs/query.h>

#include <functional>
#include <string>

namespace arbiterAI
{
namespace dashboard
{

/// Setting and clearing a GPU's VRAM override (/api/hardware/vram-override),
/// for the Overview's and the Config tab's GPU rows alike: a gweni prompt for
/// the value, a toast for a bad value or a refusal, and \p refresh once the
/// server took it.
class VramOverride {
public:
    VramOverride(gweni::controls::Base *anchor, DataSource &data, std::function<void()> refresh);

    /// Asks for the GPU's VRAM in MB (\p currentMb shown), then set().
    void prompt(int gpuIndex, double currentMb);
    /// POST {gpu_index, vram_mb}; \p input must be a positive whole number.
    void set(int gpuIndex, const std::string &input);
    /// DELETE /api/hardware/vram-override/<index>
    void clear(int gpuIndex);

    /// the prompt while it is open
    gweni::dialogs::PromptDialog *dialog() const { return m_dialog; }

private:
    gweni::controls::Base *m_anchor;
    DataSource &m_data;
    std::function<void()> m_refresh;
    gweni::dialogs::PromptDialog *m_dialog=nullptr;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_VRAMOVERRIDE_H_
