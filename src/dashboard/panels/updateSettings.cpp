#include "updateSettings.h"
#include "widgets.h"

#include <cstdlib>

namespace arbiterAI
{
namespace dashboard
{

namespace
{

const int RowHeight=24;

gweni::controls::Label *addLine(gweni::controls::Base *parent, const std::string &text)
{
    gweni::controls::Label *label=parent->newChild<gweni::controls::Label>();
    label->setText(text);
    label->setDock(gweni::DockPosition::Top);
    label->setHeight(20);
    return label;
}

/// "name  [box]" docked at the top of \p parent
template<typename Control>
Control *addField(gweni::controls::Base *parent, const std::string &name, int width)
{
    gweni::controls::Base *row=parent->newChild<gweni::controls::Base>();
    row->setDock(gweni::DockPosition::Top);
    row->setHeight(RowHeight);
    row->setMargin(gweni::Margin(0, 1, 0, 1));
    gweni::controls::Label *label=row->newChild<gweni::controls::Label>();
    label->setText(name);
    label->setDock(gweni::DockPosition::Left);
    fixWidth(label, 330);
    Control *control=row->newChild<Control>();
    control->setDock(gweni::DockPosition::Left);
    fixWidth(control, width);
    return control;
}

std::string selectedName(gweni::controls::ComboBox *combo)
{
    gweni::controls::Label *item=combo->getSelectedItem();
    return item?item->getName():std::string();
}

}//namespace

UpdateSettings::UpdateSettings(gweni::controls::Base *parent, DataSource &data):
    m_data(data)
{
    m_card=addCard(parent, "UPDATES", gweni::DockPosition::Top, 0, Height);

    m_supervisor=addLine(m_card, "Restart on crash: ...");
    m_auto=addLine(m_card, "Auto-update: ...");
    m_apply=addLine(m_card, "");
    m_apply->setTextColor(palette::muted);

    m_autoApply=m_card->newChild<gweni::controls::CheckBoxWithLabel>();
    m_autoApply->setDock(gweni::DockPosition::Top);
    m_autoApply->setHeight(RowHeight);
    m_autoApply->setMargin(gweni::Margin(0, 6, 0, 1));
    m_autoApply->label()->setText("Install new releases automatically when the server is idle");

    m_scope=addField<gweni::controls::ComboBox>(m_card, "Which releases", 330);
    m_scope->addItem("Patch releases (same major.minor)", "patch");
    m_scope->addItem("Minor releases (same major)", "minor");
    m_scope->addItem("Any release", "major");
    m_scope->selectItemByName("patch", false);

    m_delay=addField<gweni::controls::TextBoxNumeric>(m_card, "Hours after a release is published", 80);
    m_window=addField<gweni::controls::TextBox>(m_card, "Only between (HH:MM-HH:MM local; empty: any time)", 120);
    m_idle=addField<gweni::controls::TextBoxNumeric>(m_card, "Minutes the server must be idle first", 80);

    gweni::controls::Base *row=m_card->newChild<gweni::controls::Base>();
    row->setDock(gweni::DockPosition::Top);
    row->setHeight(RowHeight+4);
    row->setMargin(gweni::Margin(0, 6, 0, 0));
    m_save=row->newChild<gweni::controls::Button>();
    m_save->setText("Save");
    m_save->setDock(gweni::DockPosition::Left);
    fixWidth(m_save, 90);
    m_save->onPressCaller.addCallback(m_save, [this](gweni::event::Handler &, gweni::event::Info)
    {
        save();
    });
    m_message=row->newChild<gweni::controls::Label>();
    m_message->setDock(gweni::DockPosition::Center);
    m_message->setMargin(gweni::Margin(12, 0, 0, 0));
    m_message->setText("");
}

void UpdateSettings::updateConfig(const nlohmann::json &updates)
{
    if(!updates.is_object())
        return;
    m_autoApply->checkBox()->setChecked(flag(updates, "auto_apply"));
    m_scope->selectItemByName(text(updates, "auto_apply_scope", "patch"), false);
    m_delay->setText(std::to_string(static_cast<int>(updates.contains("auto_apply_delay_hours")?number(updates, "auto_apply_delay_hours"):24)));
    m_window->setText(text(updates, "auto_apply_window"));
    m_idle->setText(std::to_string(static_cast<int>(updates.contains("idle_minutes")?number(updates, "idle_minutes"):10)));
    const bool allowed=!updates.contains("allow_apply") || flag(updates, "allow_apply");
    m_apply->setText(allowed
        ?"Installing from the dashboard: allowed (updates.allow_apply, set in the config file)"
        :"Installing from the dashboard: off (updates.allow_apply is false in the config file)");
}

void UpdateSettings::updateStatus(const nlohmann::json &status)
{
    if(!status.is_object() || !status.contains("supervisor"))
    {
        m_supervisor->setText("Restart on crash: unknown (this server does not report it)");
        m_auto->setText("Auto-update: not available on this server");
        return;
    }

    const std::string supervisor=text(status, "supervisor");
    m_supervisor->setText(supervisor.empty()
        ?"Restart on crash: none (no supervisor declared, see README)"
        :"Restart on crash: "+supervisor);
    m_supervisor->setTextColor(supervisor.empty()?palette::warn:palette::good);

    const nlohmann::json autoStatus=status.contains("auto") && status["auto"].is_object()?status["auto"]:nlohmann::json::object();
    std::string line="Auto-update: "+text(autoStatus, "status", "off");
    const std::string next=text(autoStatus, "next_attempt");
    if(!next.empty())
        line+=" (next attempt "+next+")";
    m_auto->setText(line);
}

nlohmann::json UpdateSettings::body() const
{
    return {
        {"auto_apply", m_autoApply->checkBox()->isChecked()},
        {"auto_apply_scope", selectedName(m_scope)},
        {"auto_apply_delay_hours", std::atoi(m_delay->getText().c_str())},
        {"auto_apply_window", m_window->getText()},
        {"idle_minutes", std::atoi(m_idle->getText().c_str())}
    };
}

void UpdateSettings::save()
{
    m_message->setText("Saving...");
    m_message->setTextColor(palette::muted);
    m_data.putJson("/api/server/config", {{"updates", body()}}, [this](const Response &response)
    {
        if(!response.ok)
        {
            m_message->setText("Save failed: "+response.error);
            m_message->setTextColor(palette::bad);
            return;
        }
        m_message->setText("Saved; auto-update uses it at its next check");
        m_message->setTextColor(palette::good);
        if(response.json.is_object() && response.json.contains("updates"))
            updateConfig(response.json["updates"]);
    });
}

}//namespace dashboard
}//namespace arbiterAI
