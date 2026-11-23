#ifndef _ARBITERAI_DASHBOARD_LOGPANEL_H_
#define _ARBITERAI_DASHBOARD_LOGPANEL_H_

#include "data/dataSource.h"

#include <gweni/controls.h>
#include <gweni/controls/checkBox.h>
#include <gweni/controls/comboBox.h>
#include <gweni/controls/logView.h>

#include <cstdint>
#include <string>

namespace arbiterAI
{
namespace dashboard
{

/// The server log, a panel under every tab: a header that folds it, and when
/// expanded a toolbar (level filter, Live, Copy) over the last Count entries
/// of /api/logs. Live off freezes the view, so lines can be selected and
/// copied while the server keeps logging; turning it on or changing the
/// level reloads it.
class LogPanel
{
public:
    static constexpr int Count=200;

    LogPanel(gweni::controls::Base *parent, DataSource &data);

    bool expanded() const { return m_expanded; }
    /// folds or unfolds it; the next poll() fills it (a header click polls at once)
    void setExpanded(bool expanded);

    /// One poll: asks for the entries when expanded and live.
    void poll();
    /// \p level "" for every level, else trace, debug, info, warning, error
    /// or critical: the view is cleared and reloaded.
    void setLevel(const std::string &level);
    const std::string &level() const { return m_level; }
    void setLive(bool live);
    bool live() const { return m_liveBox->checkBox()->isChecked(); }
    /// the selected lines on the clipboard, with a toast saying so
    void copySelection();

    /// /api/logs?count=Count[&level=]
    std::string path() const;
    /// a reply of path(): the entries not shown yet are appended
    void updateLogs(const nlohmann::json &logs);

    gweni::controls::Button *header() const { return m_header; }
    gweni::controls::Base *body() const { return m_body; }
    gweni::controls::LogView *view() const { return m_view; }
    gweni::controls::ComboBox *levelBox() const { return m_levelBox; }
    gweni::controls::CheckBoxWithLabel *liveBox() const { return m_liveBox; }
    gweni::controls::Button *copyButton() const { return m_copy; }

private:
    void reload();

    DataSource &m_data;
    gweni::controls::Base *m_panel;
    gweni::controls::Button *m_header;
    gweni::controls::Base *m_body;
    gweni::controls::ComboBox *m_levelBox;
    gweni::controls::CheckBoxWithLabel *m_liveBox;
    gweni::controls::Button *m_copy;
    gweni::controls::LogView *m_view;
    bool m_expanded=true;
    std::string m_level;

    /// the newest entry shown, and how many shown entries share its time
    int64_t m_lastEpoch=-1;
    size_t m_atLastEpoch=0;
    /// replies to requests made before the last reload are dropped
    int m_generation=0;
};

/// "HH:MM:SS.mmm" of \p epochMs in local time.
std::string formatLogTime(int64_t epochMs);

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_LOGPANEL_H_
