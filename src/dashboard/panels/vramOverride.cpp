#include "vramOverride.h"
#include "widgets.h"

#include <cstdlib>

namespace arbiterAI
{
namespace dashboard
{

VramOverride::VramOverride(gweni::controls::Base *anchor, DataSource &data, std::function<void()> refresh):
    m_anchor(anchor),
    m_data(data),
    m_refresh(std::move(refresh))
{
}

void VramOverride::prompt(int gpuIndex, double currentMb)
{
    const std::string current=formatFixed(currentMb, 0);
    m_dialog=gweni::dialogs::prompt(m_anchor->getCanvas(), "VRAM override",
        "Enter VRAM override in MB for GPU "+std::to_string(gpuIndex)+" (current: "+current+" MB):", current,
        [this, gpuIndex](gweni::dialogs::Result result, const gweni::String &input)
        {
            m_dialog=nullptr;
            if(result==gweni::dialogs::Result::Ok)
                set(gpuIndex, input);
        });
}

void VramOverride::set(int gpuIndex, const std::string &input)
{
    char *end=nullptr;
    const long vramMb=std::strtol(input.c_str(), &end, 10);
    if(input.empty() || *end!='\0' || vramMb<=0)
    {
        notify(m_anchor, gweni::controls::ToastKind::Warning, "Invalid value: enter a positive whole number of MB.");
        return;
    }

    m_data.postJson("/api/hardware/vram-override", {{"gpu_index", gpuIndex}, {"vram_mb", vramMb}}, [this](const Response &response)
    {
        if(!response.ok)
            notify(m_anchor, gweni::controls::ToastKind::Negative, "VRAM override failed: "+response.error);
        else if(m_refresh)
            m_refresh();
    });
}

void VramOverride::clear(int gpuIndex)
{
    m_data.del("/api/hardware/vram-override/"+std::to_string(gpuIndex), [this](const Response &response)
    {
        if(!response.ok)
            notify(m_anchor, gweni::controls::ToastKind::Negative, "Clearing the VRAM override failed: "+response.error);
        else if(m_refresh)
            m_refresh();
    });
}

}//namespace dashboard
}//namespace arbiterAI
