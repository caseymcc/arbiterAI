#include "updateControls.h"
#include "widgets.h"

namespace arbiterAI
{
namespace dashboard
{

namespace
{

/// what the install is doing, by /api/update's state
std::string stageText(const std::string &state)
{
    if(state=="checking_space")
        return "checking free space";
    if(state=="downloading")
        return "downloading";
    if(state=="verifying")
        return "verifying the download";
    if(state=="unpacking")
        return "unpacking";
    if(state=="self_testing")
        return "testing the new version";
    if(state=="waiting_idle")
        return "waiting for running work to finish";
    if(state=="installing")
        return "installing";
    return state;
}

/// "gpt-oss-120b (Q4_K_M), qwen3-8b (Q8_0)" from /api/models/loaded
std::string modelList(const nlohmann::json &loaded)
{
    std::string list;
    if(!loaded.is_object() || !loaded.contains("models") || !loaded["models"].is_array())
        return list;
    for(const nlohmann::json &model:loaded["models"])
    {
        std::string state=text(model, "state");
        if(state!="Loaded" && state!="Ready")
            continue;
        std::string variant=text(model, "variant");
        list+=(list.empty()?"":", ")+text(model, "model", "?")+(variant.empty()?"":" ("+variant+")");
    }
    return list;
}

}//namespace

UpdateControls::UpdateControls(gweni::controls::Base *header, gweni::controls::Base *root, DataSource &data,
    std::function<void()> reload, std::function<void()> refresh):
    m_data(data),
    m_reload(std::move(reload)),
    m_refresh(std::move(refresh)),
    m_canvas(header->getCanvas())
{
    m_updateButton=header->newChild<gweni::controls::Button>();
    m_updateButton->setDock(gweni::DockPosition::Left);
    // no vertical margin: the header is as tall as its tallest control
    m_updateButton->setMargin(gweni::Margin(8, 0, 0, 0));
    m_updateButton->setHidden(true);
    m_updateButton->onPressCaller.addCallback(m_updateButton, [this](gweni::event::Handler &, gweni::event::Info)
    {
        confirmApply();
    });

    m_progressLabel=header->newChild<gweni::controls::Label>();
    m_progressLabel->setDock(gweni::DockPosition::Left);
    m_progressLabel->setMargin(gweni::Margin(12, 0, 0, 0));
    m_progressLabel->setTextColor(palette::info);
    m_progressLabel->setHidden(true);

    m_strip=root->newChild<gweni::controls::Base>();
    m_strip->setDock(gweni::DockPosition::Top);
    m_strip->setPadding(gweni::Padding(8, 2, 8, 2));
    m_strip->setHidden(true);

    gweni::controls::Base *row=m_strip->newChild<gweni::controls::Base>();
    row->setDock(gweni::DockPosition::Top);
    row->setHeight(24);
    m_rollbackButton=row->newChild<gweni::controls::Button>();
    m_rollbackButton->setDock(gweni::DockPosition::Right);
    m_rollbackButton->setHidden(true);
    m_rollbackButton->onPressCaller.addCallback(m_rollbackButton, [this](gweni::event::Handler &, gweni::event::Info)
    {
        confirmRollback();
    });
    m_stripLine=row->newChild<gweni::controls::Label>();
    m_stripLine->setDock(gweni::DockPosition::Center);
    m_stripLine->setTextAlignment(gweni::Alignment::Left | gweni::Alignment::CenterV);

    m_stripDetail=m_strip->newChild<gweni::controls::Label>();
    m_stripDetail->setDock(gweni::DockPosition::Top);
    m_stripDetail->setHeight(18);
    m_stripDetail->setTextColor(palette::muted);
}

void UpdateControls::update(const nlohmann::json &status)
{
    m_status=status.is_object()?status:nlohmann::json::object();
    // a server from before apply reports none of this: only #899's badge
    const bool known=m_status.contains("can_apply");
    const std::string state=text(m_status, "state", "idle");
    const std::string current=text(m_status, "current");
    const std::string latest=text(m_status, "latest", "?");

    const std::string started=text(m_status, "started_at");
    if(!m_restarting)
    {
        m_versionBefore=current;
        m_startedBefore=started;
    }
    if(state=="restarting" || state=="rolling_back")
        m_restarting=true;
    else if(m_restarting && state=="idle")
    {
        // the restart keeps the PID: a new process shows by its start time
        if(started!=m_startedBefore || current!=m_versionBefore)
        {
            if(!m_reloaded && m_reload)
            {
                m_reloaded=true;
                m_reload();
            }
            return;
        }
        // the same process is idle again: the rollback failed, its error follows
        m_restarting=false;
        m_installing=false;
    }
    if(m_installing && state=="idle")
        m_installing=false;

    const bool busy=known && state!="idle";
    m_busy=busy;
    m_updateButton->setText("Update to v"+latest);
    m_updateButton->sizeToContents();
    m_updateButton->setHidden(!known || !flag(m_status, "update_available") || !flag(m_status, "can_apply")
        || busy || m_installing);

    m_progressLabel->setHidden(!busy);
    if(busy)
    {
        std::string progress;
        const bool hasProgress=m_status.contains("progress") && m_status["progress"].is_number();
        if(state=="restarting")
            progress="Restarting the server...";
        else if(state=="rolling_back")
            progress="Rolling back to v"+text(m_status, "rollback_version", "?")+"...";
        else
            progress="Installing v"+latest+": "+stageText(state)
                +(hasProgress?" "+formatFixed(number(m_status, "progress")*100.0, 0)+"%":"");
        m_progressLabel->setText(progress);
        m_progressLabel->sizeToContents();
    }

    const bool rollback=known && flag(m_status, "rollback_available") && !busy;
    m_rollbackButton->setText("Roll back to v"+text(m_status, "rollback_version", "?"));
    m_rollbackButton->sizeToContents();
    m_rollbackButton->setHidden(!rollback);

    const std::string reason=text(m_status, "apply_blocked_reason");
    const std::string error=text(m_status, "update_error");
    const std::string previous=text(m_status, "prev_version");
    if(!known)
        showStrip("", palette::muted, "");
    else if(flag(m_status, "update_available") && !flag(m_status, "can_apply") && !busy)
        showStrip("v"+latest+" cannot be installed from the dashboard: "+reason, palette::warn,
            "To update by hand: download arbiterAI-server-v"+latest+"-linux-x64.tar.gz from the release page, "
            "unpack it over the install (bin/ and share/), then restart the service.");
    else if(!error.empty())
        showStrip("Last update: "+error, palette::bad, "");
    else if(!previous.empty())
        showStrip("Updated from v"+previous, palette::good, "");
    else
        showStrip("", palette::muted, "");
}

void UpdateControls::unreachable()
{
    if(!active())
    {
        update(nlohmann::json::object());
        return;
    }
    // an accepted install or rollback that stops answering is restarting:
    // the drain refuses every request (503), then the port closes for the exec
    m_restarting=true;
    m_busy=true;
    m_updateButton->setHidden(true);
    m_rollbackButton->setHidden(true);
    m_progressLabel->setText("Restarting the server...");
    m_progressLabel->sizeToContents();
    m_progressLabel->setHidden(false);
}

void UpdateControls::showStrip(const std::string &line, const gweni::Color &color, const std::string &detail)
{
    m_stripLine->setText(line);
    m_stripLine->setTextColor(color);
    m_stripDetail->setText(detail);
    m_stripDetail->setHidden(detail.empty());
    const bool shown=!line.empty() || !m_rollbackButton->hidden();
    m_strip->setHeight(shown?28+(detail.empty()?0:18):0);
    m_strip->setHidden(!shown);
}

void UpdateControls::pollRestart()
{
    if(!m_restarting || m_reloaded)
        return;
    m_data.getJson("/api/version", [this](const Response &response)
    {
        if(!response.ok || m_reloaded)
            return;
        std::string version=text(response.json, "version");
        if(!version.empty() && version!=m_versionBefore && m_reload)
        {
            m_reloaded=true;
            m_reload();
        }
    });
}

void UpdateControls::confirmApply()
{
    const std::string latest=text(m_status, "latest", "?");
    const std::string supervisor=text(m_status, "supervisor");
    const int drain=static_cast<int>(number(m_status, "drain_timeout_s"));

    m_data.getJson("/api/models/loaded", [this, latest, supervisor, drain](const Response &models)
    {
        const std::string loaded=models.ok?modelList(models.json):std::string();
        m_data.getJson("/api/scheduler/jobs", [this, latest, supervisor, drain, loaded](const Response &jobs)
        {
            size_t active=jobs.ok && jobs.json.is_array()?jobs.json.size():0;
            std::string text="Install v"+latest+" and restart the server.\n\n";
            text+="Loaded models (loaded again after the restart): "+(loaded.empty()?std::string("none"):loaded)+"\n\n";
            text+="Active jobs: "+std::to_string(active)
                +(active>0?", drained for up to "+std::to_string(drain>0?drain:60)+" s, then cut off":"")+"\n\n";
            text+=supervisor.empty()
                ?"If v"+latest+" crashes on start it is not rolled back: no supervisor declared (see README)."
                :"If v"+latest+" crashes on start, "+supervisor+" restarts it and it is rolled back.";
            openConfirm("Install v"+latest, text, [this, latest]()
            {
                post("/api/update/apply", {{"version", latest}}, "Update");
            });
        });
    });
}

void UpdateControls::confirmRollback()
{
    const std::string previous=text(m_status, "rollback_version", "?");
    const std::string current=text(m_status, "current", "?");
    openConfirm("Roll back to v"+previous,
        "Go back to v"+previous+" and restart the server. Auto-update will not install v"+current+" again.",
        [this]()
        {
            post("/api/update/rollback", nlohmann::json::object(), "Rollback");
        });
}

void UpdateControls::openConfirm(const std::string &title, const std::string &text, std::function<void()> onOk)
{
    m_confirmText=text;
    m_confirm=gweni::dialogs::confirm(m_canvas, title, text, gweni::dialogs::Buttons::OkCancel,
        [this, onOk](gweni::dialogs::Result result, const gweni::String &)
        {
            m_confirm=nullptr;
            if(result==gweni::dialogs::Result::Ok)
                onOk();
        });
}

void UpdateControls::post(const std::string &path, const nlohmann::json &body, const std::string &what)
{
    m_data.postJson(path, body, [this, what](const Response &response)
    {
        if(!response.ok)
        {
            notify(m_strip, gweni::controls::ToastKind::Negative, what+" failed: "+response.error);
            return;
        }
        m_installing=true;
        if(response.json.is_object())
            update(response.json);
        if(m_refresh)
            m_refresh();
    });
}

}//namespace dashboard
}//namespace arbiterAI
