#ifndef _ARBITERAI_DASHBOARD_UPDATECONTROLS_H_
#define _ARBITERAI_DASHBOARD_UPDATECONTROLS_H_

#include "data/dataSource.h"

#include <gweni/controls.h>
#include <gweni/controls/dialogs/query.h>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace arbiterAI
{
namespace dashboard
{

/// Installing a server update, from /api/update: in the header, the Update
/// button (when the latest release can be installed) or the install's
/// progress; under it, a strip with what there is to know: the steps to
/// update by hand when the server cannot, "updated from vX" with a Rollback
/// button, or the last update's error. Both ask first, in a dialog listing
/// what the restart affects. Once the server restarts into another version
/// the page is reloaded, since that server serves its own dashboard.
class UpdateControls {
public:
    /// \p header the header row, the controls docked at its left after what
    /// it holds; \p root where the strip docks at the top (create this right
    /// after the header). \p reload reloads the page; \p refresh asks for
    /// /api/update again.
    UpdateControls(gweni::controls::Base *header, gweni::controls::Base *root, DataSource &data,
        std::function<void()> reload, std::function<void()> refresh);

    /// a /api/update reply; an empty object (a server without it) shows nothing
    void update(const nlohmann::json &status);

    /// an install or a rollback is running or restarting: /api/update and
    /// /api/version are asked for on every poll
    bool active() const { return m_installing || m_restarting || m_busy; }
    /// while restarting: the version, to reload once it changed
    void pollRestart();

    /// the dialog before POST /api/update/apply, with the loaded models and
    /// the active jobs
    void confirmApply();
    /// the dialog before POST /api/update/rollback
    void confirmRollback();

    gweni::controls::Button *updateButton() const { return m_updateButton; }
    gweni::controls::Button *rollbackButton() const { return m_rollbackButton; }
    gweni::controls::Label *progressLabel() const { return m_progressLabel; }
    gweni::controls::Base *strip() const { return m_strip; }
    const std::string &stripText() const { return m_stripLine->getText(); }
    const std::string &stripDetail() const { return m_stripDetail->getText(); }
    gweni::dialogs::Dialog *confirmDialog() const { return m_confirm; }
    const std::string &confirmText() const { return m_confirmText; }

private:
    void post(const std::string &path, const nlohmann::json &body, const std::string &what);
    void showStrip(const std::string &line, const gweni::Color &color, const std::string &detail);
    void openConfirm(const std::string &title, const std::string &text, std::function<void()> onOk);

    DataSource &m_data;
    std::function<void()> m_reload;
    std::function<void()> m_refresh;
    gweni::controls::Canvas *m_canvas;

    gweni::controls::Button *m_updateButton;
    gweni::controls::Label *m_progressLabel;
    gweni::controls::Base *m_strip;
    gweni::controls::Label *m_stripLine;
    gweni::controls::Label *m_stripDetail;
    gweni::controls::Button *m_rollbackButton;
    gweni::dialogs::Dialog *m_confirm=nullptr;
    std::string m_confirmText;

    nlohmann::json m_status=nlohmann::json::object();
    /// the version that was running when the restart began
    std::string m_versionBefore;
    bool m_installing=false;
    /// the server reports an install or a rollback in progress
    bool m_busy=false;
    bool m_restarting=false;
    bool m_reloaded=false;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_UPDATECONTROLS_H_
