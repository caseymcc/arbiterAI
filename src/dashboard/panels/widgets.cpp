#include "widgets.h"

#include <cstdio>

namespace arbiterAI
{
namespace dashboard
{

gweni::controls::Label *addHeading(gweni::controls::Base *parent, const std::string &text)
{
    gweni::controls::Label *heading=parent->newChild<gweni::controls::Label>();
    heading->setText(text);
    heading->setTextColor(palette::muted);
    heading->setDock(gweni::DockPosition::Top);
    heading->setMargin(gweni::Margin(0, 4, 0, 6));
    return heading;
}

gweni::controls::Label *addStatRow(gweni::controls::Base *parent, const std::string &name)
{
    gweni::controls::Base *row=parent->newChild<gweni::controls::Base>();
    row->setDock(gweni::DockPosition::Top);
    row->setHeight(22);

    gweni::controls::Label *label=row->newChild<gweni::controls::Label>();
    label->setText(name);
    label->setDock(gweni::DockPosition::Left);
    label->setWidth(180);

    gweni::controls::Label *value=row->newChild<gweni::controls::Label>();
    value->setText("-");
    value->setDock(gweni::DockPosition::Center);
    value->setTextAlignment(gweni::Alignment::Right | gweni::Alignment::CenterV);
    return value;
}

std::string formatRate(double tokensPerSecond)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f t/s", tokensPerSecond);
    return text;
}

}//namespace dashboard
}//namespace arbiterAI
