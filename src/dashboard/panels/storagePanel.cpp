#include "storagePanel.h"
#include "widgets.h"

#include <algorithm>
#include <cstdio>

namespace arbiterAI
{
namespace dashboard
{

namespace
{

const int TableHeight=420;

gweni::Color ageColor(Age age)
{
    switch(age)
    {
    case Age::Fresh:
        return palette::good;
    case Age::Stale:
        return palette::warn;
    default:
        return palette::bad;
    }
}

gweni::Color stateColor(const std::string &state)
{
    if(state=="Loaded")
        return palette::good;
    if(state=="Ready")
        return palette::info;
    if(state=="Downloading")
        return palette::warn;
    return palette::muted;
}

}//namespace

Age ageOf(const std::string &iso, std::time_t now)
{
    std::tm tm{};
    if(iso.empty() || std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec)!=6)
        return Age::Old;
    tm.tm_year-=1900;
    tm.tm_mon-=1;

    const std::time_t then=timegm(&tm);
    const long long days=static_cast<long long>(std::difftime(now, then))/(60*60*24);
    if(days>30)
        return Age::Old;
    if(days>14)
        return Age::Stale;
    return Age::Fresh;
}

std::string dateOf(const std::string &iso)
{
    return iso.size()>=10?iso.substr(0, 10):"-";
}

StoragePanel::StoragePanel(gweni::controls::Base *page, DataSource &data, std::function<void()> refresh,
    std::function<std::time_t()> now):
    m_data(data),
    m_refresh(std::move(refresh)),
    m_now(now?std::move(now):[]() { return std::time(nullptr); }),
    m_page(page)
{
    gweni::controls::Base *storage=addCard(page, "STORAGE", gweni::DockPosition::Top, 0, 100);
    gweni::controls::Base *labels=storage->newChild<gweni::controls::Base>();
    labels->setDock(gweni::DockPosition::Top);
    labels->setHeight(20);
    m_used=labels->newChild<gweni::controls::Label>();
    m_used->setText("Used: -");
    m_used->setDock(gweni::DockPosition::Left);
    m_limit=labels->newChild<gweni::controls::Label>();
    m_limit->setText("Limit: -");
    m_limit->setDock(gweni::DockPosition::Center);
    m_limit->setTextAlignment(gweni::Alignment::Right | gweni::Alignment::CenterV);

    m_bar=storage->newChild<gweni::controls::ProgressBar>();
    m_bar->setDock(gweni::DockPosition::Top);
    m_bar->setHeight(22);
    m_bar->setAutoLabel(false);
    m_bar->setProgress(0.0f);

    gweni::controls::Base *footer=storage->newChild<gweni::controls::Base>();
    footer->setDock(gweni::DockPosition::Top);
    footer->setHeight(20);
    m_cleanup=footer->newChild<gweni::controls::Label>();
    m_cleanup->setText("Auto-cleanup: -");
    m_cleanup->setDock(gweni::DockPosition::Left);
    m_candidates=footer->newChild<gweni::controls::Label>();
    m_candidates->setText("");
    m_candidates->setTextColor(palette::warn);
    m_candidates->setDock(gweni::DockPosition::Center);
    m_candidates->setTextAlignment(gweni::Alignment::Right | gweni::Alignment::CenterV);

    m_downloadsCard=addCard(page, "ACTIVE DOWNLOADS", gweni::DockPosition::Top, 0, 0);
    m_downloads=m_downloadsCard->newChild<gweni::controls::Base>();
    m_downloads->setDock(gweni::DockPosition::Top);
    m_downloadsCard->setHidden(true);

    gweni::controls::Base *files=addCard(page, "MODEL FILES", gweni::DockPosition::Top, 0, TableHeight+30);
    m_models=addTable(files, TableHeight, {{"Model", 200}, {"Variant", 90}, {"Size", 80}, {"Downloaded", 100},
        {"Last Used", 100}, {"Uses", 60}, {"State", 90}, {"Hot Ready", 80}, {"Protected", 80}, {"Actions", 80}});
    setRows(m_models, {}, "No downloaded models");
}

void StoragePanel::updateStorage(const nlohmann::json &storage)
{
    const double used=number(storage, "used_by_models_bytes");
    const double limit=number(storage, "storage_limit_bytes");
    const double total=limit>0?limit:used+number(storage, "free_disk_bytes");
    const double share=total>0?used/total:0.0;

    m_used->setText("Used: "+formatBytes(used));
    m_limit->setText(limit>0?"Limit: "+formatBytes(limit):"Limit: All free space");
    m_bar->setProgress(static_cast<float>(std::clamp(share, 0.0, 1.0)));
    m_bar->setText(formatBytes(used)+" / "+formatBytes(total)+" ("+formatFixed(share*100.0, 1)+"%)");
    m_cleanup->setText(std::string("Auto-cleanup: ")+(storage.value("cleanup_enabled", false)?"ON":"OFF"));
}

void StoragePanel::updateCleanup(const nlohmann::json &preview)
{
    const int count=static_cast<int>(number(preview, "candidate_count"));
    if(count<=0)
    {
        m_candidates->setText("");
        return;
    }
    std::string label=std::to_string(count)+" cleanup candidate"+(count>1?"s":"");
    const std::string freeable=text(preview, "total_freeable_display");
    if(!freeable.empty())
        label+=", "+freeable+" freeable";
    m_candidates->setText(label);
}

void StoragePanel::updateDownloads(const nlohmann::json &downloads)
{
    const nlohmann::json list=downloads.is_object()?downloads.value("downloads", nlohmann::json::array()):nlohmann::json::array();
    const std::string shown=list.dump();
    if(shown==m_shownDownloads)
        return;
    m_shownDownloads=shown;

    showDownloads(m_downloads, list);
    m_downloadsCard->setHidden(!list.is_array() || list.empty());
    m_downloadsCard->setHeight(static_cast<int>(list.is_array()?list.size():0)*24+30);
}

void StoragePanel::updateModels(const nlohmann::json &models)
{
    const nlohmann::json list=models.is_object()?models.value("models", nlohmann::json::array()):nlohmann::json::array();
    const std::string shown=list.dump();
    if(shown==m_shownModels)
        return;
    m_shownModels=shown;

    if(!list.is_array() || list.empty())
    {
        setRows(m_models, {}, "No downloaded models");
        return;
    }

    m_models->clear();
    const std::time_t now=m_now();
    for(const nlohmann::json &model:list)
    {
        const std::string name=text(model, "model");
        const std::string variant=text(model, "variant");
        const std::string state=text(model, "runtime_state", "Unloaded");
        const bool hotReady=model.value("hot_ready", false);
        const bool isProtected=model.value("protected", false);
        const bool guarded=hotReady || isProtected;
        const gweni::Color color=ageColor(ageOf(text(model, "last_used_at"), now));

        gweni::controls::DataTableRow *row=m_models->addRow();
        const std::string size=text(model, "file_size_display");
        const std::vector<std::string> cells={name, variant.empty()?"-":variant,
            size.empty()?formatBytes(number(model, "file_size_bytes")):size,
            dateOf(text(model, "downloaded_at")), dateOf(text(model, "last_used_at")),
            formatFixed(number(model, "usage_count"), 0), state};
        for(size_t i=0; i<cells.size(); ++i)
        {
            row->setCellText(static_cast<int>(i), cells[i]);
            row->getCell(static_cast<int>(i))->setTextColor(color);
        }
        row->getCell(6)->setTextColor(stateColor(state));

        auto toggle=[this, row, name, variant](int column, const std::string &flag, bool on)
        {
            gweni::controls::Button *button=m_models->newChild<gweni::controls::Button>();
            button->setText(on?"ON":"OFF");
            button->setName(flag);
            button->setTextColor(on?palette::good:palette::muted);
            button->onPressCaller.addCallback(button, [this, name, variant, flag, on](gweni::event::Handler &, gweni::event::Info)
            {
                setFlag(name, variant, flag, !on);
            });
            row->setCellContents(column, button);
        };
        toggle(7, "hot-ready", hotReady);
        toggle(8, "protected", isProtected);

        gweni::controls::Button *remove=m_models->newChild<gweni::controls::Button>();
        remove->setText("Delete");
        remove->setName("delete");
        remove->setTextColor(guarded?palette::muted:palette::bad);
        remove->setDisabled(guarded);
        remove->setToolTipText(guarded?"Clear hot ready and protected first":"Delete the model file");
        remove->onPressCaller.addCallback(remove, [this, name, variant, guarded](gweni::event::Handler &, gweni::event::Info)
        {
            if(!guarded)
                confirmDelete(name, variant);
        });
        row->setCellContents(9, remove);
    }
}

void StoragePanel::setFlag(const std::string &name, const std::string &variant, const std::string &flag, bool on)
{
    const std::string path="/api/models/"+urlEncode(name)+"/variants/"+urlEncode(variant)+"/"+flag;
    auto done=[this, flag](const Response &response)
    {
        if(!response.ok && !(response.status>=200 && response.status<300))
            notify(m_page, gweni::controls::ToastKind::Negative, "Changing "+flag+" failed: "+response.error);
        if(m_refresh)
            m_refresh();
    };
    if(on)
        m_data.postJson(path, nlohmann::json::object(), done);
    else
        m_data.del(path, done);
}

void StoragePanel::confirmDelete(const std::string &name, const std::string &variant)
{
    m_confirm=gweni::dialogs::confirm(m_page->getCanvas(), "Delete model file",
        "Delete "+name+" "+variant+"? This cannot be undone.", gweni::dialogs::Buttons::OkCancel,
        [this, name, variant](gweni::dialogs::Result result, const gweni::String &)
        {
            m_confirm=nullptr;
            if(result==gweni::dialogs::Result::Ok)
                deleteFiles(name, variant);
        });
}

void StoragePanel::deleteFiles(const std::string &name, const std::string &variant)
{
    std::string path="/api/models/"+urlEncode(name)+"/files";
    if(!variant.empty())
        path+="?variant="+urlEncode(variant);
    m_data.del(path, [this](const Response &response)
    {
        if(response.status==409)
            notify(m_page, gweni::controls::ToastKind::Warning,
                response.error.empty()?"Cannot delete: variant is guarded":response.error);
        else if(!response.ok && !(response.status>=200 && response.status<300))
            notify(m_page, gweni::controls::ToastKind::Negative, "Delete failed: "+response.error);
        if(m_refresh)
            m_refresh();
    });
}

}//namespace dashboard
}//namespace arbiterAI
