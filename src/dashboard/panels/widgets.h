#ifndef _ARBITERAI_DASHBOARD_WIDGETS_H_
#define _ARBITERAI_DASHBOARD_WIDGETS_H_

#include <gweni/controls.h>
#include <gweni/controls/dataTable.h>
#include <gweni/controls/progressBar.h>
#include <gweni/controls/toast.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

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
const gweni::Color warn(240, 192, 64);
const gweni::Color bad(244, 67, 54);
const gweni::Color info(100, 181, 246);
}

/// A label docked at the top of \p parent.
gweni::controls::Label *addHeading(gweni::controls::Base *parent, const std::string &text);

/// A card: a titled block docked at \p dock of \p parent, \p width or \p height
/// units (0: what the dock gives it); returns the block, its children docked
/// below the title.
gweni::controls::Base *addCard(gweni::controls::Base *parent, const std::string &title, gweni::DockPosition dock,
    int width, int height);

/// \p control \p width units wide whatever its contents ask for (a dock Left
/// or Right otherwise sizes it to them).
void fixWidth(gweni::controls::Base *control, int width);

/// A "name ... value" row docked at the top of \p parent; returns the value.
gweni::controls::Label *addStatRow(gweni::controls::Base *parent, const std::string &name);

/// A table docked at the top of \p parent, \p height units, with \p columns
/// (caption and width; the last stretches).
gweni::controls::DataTable *addTable(gweni::controls::Base *parent, int height,
    const std::vector<std::pair<std::string, int>> &columns);

/// One table row per entry of \p rows; no rows: one row showing \p empty.
void setRows(gweni::controls::DataTable *table, const std::vector<std::vector<std::string>> &rows,
    const std::string &empty);

/// \p downloads (/api/downloads' "downloads") as rows in \p container, which
/// is sized to them: the model, a progress bar, and the bytes, speed and ETA.
void showDownloads(gweni::controls::Base *container, const nlohmann::json &downloads);

/// A toast on \p control's canvas.
void notify(gweni::controls::Base *control, gweni::controls::ToastKind kind, const std::string &text);

/// "12.3 t/s"
std::string formatRate(double tokensPerSecond);
/// "512 MB", "1.5 GB"
std::string formatMb(double mb);
/// "512 B", "1.5 MB", "2.0 GB"
std::string formatBytes(double bytes);
/// "131,072"
std::string formatThousands(long long value);
/// "%.Nf"
std::string formatFixed(double value, int decimals);
/// A path segment percent-encoded (encodeURIComponent).
std::string urlEncode(const std::string &text);

/// \p json[key] as a number, 0 when it is missing or not one.
double number(const nlohmann::json &json, const char *key);
/// \p json[key] as text, \p fallback when it is missing or not a string.
std::string text(const nlohmann::json &json, const char *key, const std::string &fallback=std::string());

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_WIDGETS_H_
