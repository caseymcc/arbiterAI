#ifndef _ARBITERAI_DASHBOARD_WIDGETS_H_
#define _ARBITERAI_DASHBOARD_WIDGETS_H_

#include <gweni/controls.h>

#include <string>

namespace arbiterAI
{
namespace dashboard
{

/// The dashboard's colours, the old pages' palette.
namespace palette
{
const gweni::Color accent(122, 162, 247);
const gweni::Color muted(140, 146, 170);
const gweni::Color good(76, 175, 80);
const gweni::Color warn(255, 193, 7);
const gweni::Color bad(244, 67, 54);
}

/// A label docked at the top of \p parent.
gweni::controls::Label *addHeading(gweni::controls::Base *parent, const std::string &text);

/// A "name ... value" row docked at the top of \p parent; returns the value.
gweni::controls::Label *addStatRow(gweni::controls::Base *parent, const std::string &name);

/// "12.3 t/s"
std::string formatRate(double tokensPerSecond);

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_WIDGETS_H_
