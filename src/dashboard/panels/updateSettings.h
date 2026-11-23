#ifndef _ARBITERAI_DASHBOARD_UPDATESETTINGS_H_
#define _ARBITERAI_DASHBOARD_UPDATESETTINGS_H_

#include "data/dataSource.h"

#include <gweni/controls.h>
#include <gweni/controls/checkBox.h>
#include <gweni/controls/comboBox.h>
#include <gweni/controls/textBox.h>

#include <nlohmann/json.hpp>

#include <string>

namespace arbiterAI
{
namespace dashboard
{

/// The Config tab's Updates card: what restarts the server on a crash, what
/// auto-update is doing (from /api/update), and the auto-update policy (the
/// "updates" keys of /api/server/config), saved with PUT /api/server/config.
/// allow_apply is shown, not edited: the server takes it from its file only.
class UpdateSettings {
public:
    static constexpr int Height=290;

    UpdateSettings(gweni::controls::Base *parent, DataSource &data);

    /// /api/server/config's "updates"
    void updateConfig(const nlohmann::json &updates);
    /// a /api/update reply
    void updateStatus(const nlohmann::json &status);

    /// the edited policy as PUT /api/server/config's "updates" takes it
    nlohmann::json body() const;
    void save();

    const std::string &supervisorText() const { return m_supervisor->getText(); }
    const std::string &autoText() const { return m_auto->getText(); }
    const std::string &applyText() const { return m_apply->getText(); }
    const std::string &message() const { return m_message->getText(); }
    gweni::controls::CheckBoxWithLabel *autoApply() const { return m_autoApply; }
    gweni::controls::ComboBox *scope() const { return m_scope; }
    gweni::controls::TextBox *delay() const { return m_delay; }
    gweni::controls::TextBox *window() const { return m_window; }
    gweni::controls::TextBox *idle() const { return m_idle; }
    gweni::controls::Button *saveButton() const { return m_save; }
    gweni::controls::Base *card() const { return m_card; }

private:
    DataSource &m_data;
    gweni::controls::Base *m_card;
    gweni::controls::Label *m_supervisor;
    gweni::controls::Label *m_auto;
    gweni::controls::Label *m_apply;
    gweni::controls::CheckBoxWithLabel *m_autoApply;
    gweni::controls::ComboBox *m_scope;
    gweni::controls::TextBox *m_delay;
    gweni::controls::TextBox *m_window;
    gweni::controls::TextBox *m_idle;
    gweni::controls::Button *m_save;
    gweni::controls::Label *m_message;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_UPDATESETTINGS_H_
