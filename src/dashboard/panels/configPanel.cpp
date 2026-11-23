#include "configPanel.h"
#include "widgets.h"

#include <algorithm>
#include <cmath>

namespace arbiterAI
{
namespace dashboard
{

namespace
{

const int RowHeight=24;
const int LabelHeight=18;
const char *KvTypes[]={"f16", "q8_0", "q4_0"};

std::string modelKey(const std::string &model, const std::string &variant)
{
    return model+"||"+variant;
}

std::string modelLabel(const std::string &model, const std::string &variant)
{
    return variant.empty()?model:model+" ("+variant+")";
}

std::string contextLabel(int contextSize)
{
    return contextSize<=0?"Auto":formatFixed(contextSize/1024.0, 0)+"K";
}

/// a label docked at the top of \p parent, \p height units
gweni::controls::Label *addLine(gweni::controls::Base *parent, const std::string &text, int height,
    const gweni::Color &color=palette::muted)
{
    gweni::controls::Label *label=parent->newChild<gweni::controls::Label>();
    label->setText(text);
    label->setTextColor(color);
    label->setDock(gweni::DockPosition::Top);
    label->setHeight(height);
    return label;
}

/// "name  [combo]" docked at the top of \p parent
gweni::controls::ComboBox *addChoice(gweni::controls::Base *parent, const std::string &name,
    const std::vector<std::pair<std::string, std::string>> &items, const std::string &selected, int labelWidth=170)
{
    gweni::controls::Base *row=parent->newChild<gweni::controls::Base>();
    row->setDock(gweni::DockPosition::Top);
    row->setHeight(RowHeight);
    row->setMargin(gweni::Margin(0, 1, 0, 1));
    if(!name.empty())
    {
        gweni::controls::Label *label=row->newChild<gweni::controls::Label>();
        label->setText(name);
        label->setDock(gweni::DockPosition::Left);
        fixWidth(label, labelWidth);
    }
    gweni::controls::ComboBox *combo=row->newChild<gweni::controls::ComboBox>();
    combo->setDock(gweni::DockPosition::Center);
    for(const auto &[label, value]:items)
        combo->addItem(label, value);
    combo->selectItemByName(selected, false);
    return combo;
}

std::string boolOption(const nlohmann::json &options, const char *key)
{
    if(!options.is_object() || !options.contains(key) || !options[key].is_boolean())
        return std::string();
    return options[key].get<bool>()?"true":"false";
}

std::string selectedName(gweni::controls::ComboBox *combo)
{
    gweni::controls::Label *item=combo->getSelectedItem();
    return item?item->getName():std::string();
}

}//namespace

nlohmann::json StartupModel::toJson() const
{
    return {{"model", model}, {"variant", variant}, {"context_size", contextSize},
        {"runtime_options", runtimeOptions.is_object()?runtimeOptions:nlohmann::json::object()},
        {"devices", devices}, {"api_format", apiFormat}};
}

StartupModel StartupModel::fromJson(const nlohmann::json &json)
{
    StartupModel entry;
    entry.model=text(json, "model");
    entry.variant=text(json, "variant");
    entry.contextSize=static_cast<int>(number(json, "context_size"));
    if(json.contains("runtime_options") && json["runtime_options"].is_object())
        entry.runtimeOptions=json["runtime_options"];
    if(json.contains("devices") && json["devices"].is_array())
    {
        for(const nlohmann::json &device:json["devices"])
        {
            if(device.is_number_integer())
                entry.devices.push_back(device.get<int>());
        }
    }
    entry.apiFormat=text(json, "api_format");
    return entry;
}

ContextFit contextFit(const nlohmann::json &option, double vramMb)
{
    ContextFit fit;
    const double perThousand=number(option, "memory_per_1k_context_mb");
    if(perThousand<=0 || vramMb<=0)
        return fit;
    fit.known=true;
    const double baseMemory=number(option, "base_memory_mb");
    const double baseContext=number(option, "base_context_size");
    // whole 1K steps, as the slider moves
    auto atMemory=[&](double memory)
    {
        const double context=baseContext+(memory-baseMemory)/perThousand*1024.0;
        return context<=0?0:static_cast<int>(std::floor(context/1024.0))*1024;
    };
    fit.likely=atMemory(0.85*vramMb);
    fit.tight=atMemory(vramMb);
    return fit;
}

ConfigPanel::ConfigPanel(gweni::controls::Base *page, DataSource &data):
    m_data(data),
    m_page(page),
    m_vram(page, data, [this]()
    {
        m_data.getJson("/api/hardware", [this](const Response &response)
        {
            if(response.ok)
                updateHardware(response.json);
        });
    })
{
    m_scroll=page->newChild<gweni::controls::ScrollControl>();
    m_scroll->setDock(gweni::DockPosition::Center);
    m_scroll->setScroll(false, true);
    m_content=m_scroll->newChild<gweni::controls::Base>();
    m_content->setDock(gweni::DockPosition::Top);

    gweni::controls::Base *intro=addCard(m_content, "STARTUP CONFIGURATION", gweni::DockPosition::Top, 0, 60);
    addLine(intro, "Models listed here are loaded on server startup. Save & Load saves the server configuration and loads", LabelHeight, gweni::Color(200, 204, 220));
    addLine(intro, "the model now; Remove unloads it and removes it from the configuration.", LabelHeight, gweni::Color(200, 204, 220));

    m_message=m_content->newChild<gweni::controls::Label>();
    m_message->setDock(gweni::DockPosition::Top);
    m_message->setHeight(0);
    m_message->setText("");

    gweni::controls::Base *header=m_content->newChild<gweni::controls::Base>();
    header->setDock(gweni::DockPosition::Top);
    header->setHeight(RowHeight+8);
    gweni::controls::Label *title=header->newChild<gweni::controls::Label>();
    title->setText("STARTUP MODELS  - select models to load on startup and assign compute devices");
    title->setTextColor(palette::muted);
    title->setDock(gweni::DockPosition::Left);
    m_add=header->newChild<gweni::controls::Button>();
    m_add->setText("+ Add Model");
    m_add->setDock(gweni::DockPosition::Right);
    fixWidth(m_add, 110);
    // until the config arrives, which would replace what was added
    m_add->setDisabled(true);
    m_add->onPressCaller.addCallback(m_add, [this](gweni::event::Handler &, gweni::event::Info)
    {
        addModel();
    });

    m_list=m_content->newChild<gweni::controls::Base>();
    m_list->setDock(gweni::DockPosition::Top);

    m_gpuCard=addCard(m_content, "VRAM OVERRIDES", gweni::DockPosition::Top, 0, 0);
    addLine(m_gpuCard, "Override the reported VRAM of a GPU when the driver reports a wrong value, or to simulate other hardware. "
        "It applies to fit calculations at once.", LabelHeight, gweni::Color(200, 204, 220));
    m_gpuList=m_gpuCard->newChild<gweni::controls::Base>();
    m_gpuList->setDock(gweni::DockPosition::Top);

    m_updates=std::make_unique<UpdateSettings>(m_content, m_data);

    renderModels();
    renderGpus();
}

void ConfigPanel::load()
{
    if(m_loading)
        return;
    m_loading=true;

    m_data.getJson("/api/server/config", [this](const Response &response)
    {
        m_loading=false;
        if(!response.ok)
        {
            m_message->setText("Failed to load the server configuration: "+response.error);
            m_message->setTextColor(palette::bad);
            m_message->setHeight(RowHeight);
            layoutContent();
            return;
        }
        m_message->setText("");
        m_message->setHeight(0);
        updateConfig(response.json);
    });
    m_data.getJson("/api/hardware", [this](const Response &response)
    {
        if(response.ok)
            updateHardware(response.json);
    });
    for(const char *accelerator:{"vulkan", "cuda"})
    {
        m_data.getJson(std::string("/api/server/startup-options?accelerator=")+accelerator, [this](const Response &response)
        {
            if(response.ok)
                updateOptions(response.json);
        });
    }
}

void ConfigPanel::updateConfig(const nlohmann::json &config)
{
    if(!config.is_object())
        return;
    m_loaded=true;
    m_add->setDisabled(false);
    m_models.clear();
    m_statuses.clear();
    if(config.contains("startup_models") && config["startup_models"].is_array())
    {
        for(const nlohmann::json &entry:config["startup_models"])
            addEntry(StartupModel::fromJson(entry));
    }
    if(config.contains("updates"))
        m_updates->updateConfig(config["updates"]);
    renderModels();
}

void ConfigPanel::addEntry(StartupModel entry)
{
    entry.id=m_nextId++;
    m_models.push_back(std::move(entry));
}

size_t ConfigPanel::indexOf(uint64_t id) const
{
    for(size_t i=0; i<m_models.size(); ++i)
    {
        if(m_models[i].id==id)
            return i;
    }
    return m_models.size();
}

void ConfigPanel::updateHardware(const nlohmann::json &hardware)
{
    if(!hardware.is_object())
        return;
    m_gpus=hardware.value("gpus", nlohmann::json::array());
    renderGpus();
    renderModels();
}

void ConfigPanel::updateOptions(const nlohmann::json &options)
{
    if(!options.is_object() || !options.contains("options") || !options["options"].is_array())
        return;
    m_optionReplies[text(options, "accelerator")]=options["options"];

    // Vulkan's entries first, then those only another accelerator offers, as
    // the old page merged them, whichever reply arrives first
    m_options.clear();
    auto merge=[this](const nlohmann::json &list)
    {
        for(const nlohmann::json &option:list)
        {
            if(!findOption(text(option, "model"), text(option, "variant")))
                m_options.push_back(option);
        }
    };
    auto vulkan=m_optionReplies.find("vulkan");
    if(vulkan!=m_optionReplies.end())
        merge(vulkan->second);
    for(const auto &[accelerator, list]:m_optionReplies)
    {
        if(accelerator!="vulkan")
            merge(list);
    }
    renderModels();
}

const nlohmann::json *ConfigPanel::findOption(const std::string &model, const std::string &variant) const
{
    for(const nlohmann::json &option:m_options)
    {
        if(text(option, "model")==model && text(option, "variant")==variant)
            return &option;
    }
    return nullptr;
}

double ConfigPanel::devicesVram(const StartupModel &entry) const
{
    double total=0;
    for(const nlohmann::json &gpu:m_gpus)
    {
        const int index=static_cast<int>(number(gpu, "index"));
        if(entry.devices.empty() || std::find(entry.devices.begin(), entry.devices.end(), index)!=entry.devices.end())
            total+=number(gpu, "vram_total_mb");
    }
    return total;
}

void ConfigPanel::addModel()
{
    addEntry(StartupModel());
    renderModels();
}

void ConfigPanel::removeModel(size_t index)
{
    if(index>=m_models.size())
        return;
    const std::string name=m_models[index].model;
    m_statuses.erase(m_models[index].id);
    m_models.erase(m_models.begin()+static_cast<std::ptrdiff_t>(index));
    renderModels();

    auto save=[this]()
    {
        saveAll([this](bool ok, const std::string &error)
        {
            if(!ok)
                notify(m_page, gweni::controls::ToastKind::Negative, "Saving the configuration failed: "+error);
        });
    };
    if(name.empty())
    {
        save();
        return;
    }
    // unloaded whether or not it was loaded, as the old page did
    m_data.postJson("/api/models/"+urlEncode(name)+"/unload", nlohmann::json::object(), [save](const Response &)
    {
        save();
    });
}

nlohmann::json ConfigPanel::configBody() const
{
    nlohmann::json models=nlohmann::json::array();
    for(const StartupModel &entry:m_models)
    {
        if(!entry.model.empty())
            models.push_back(entry.toJson());
    }
    return {{"startup_models", models}};
}

void ConfigPanel::saveAll(std::function<void(bool ok, const std::string &error)> done)
{
    m_data.putJson("/api/server/config", configBody(), [done](const Response &response)
    {
        done(response.ok, response.error);
    });
}

void ConfigPanel::saveAndLoad(size_t index)
{
    if(index>=m_models.size())
        return;
    const StartupModel entry=m_models[index];
    // by id: a removal or a re-render may move the entry before the replies come
    const uint64_t id=entry.id;
    if(entry.model.empty())
    {
        setStatus(id, "Select a model first.", palette::bad);
        return;
    }

    setSaving(id, true);
    setStatus(id, "Saving config...", palette::muted);

    auto finish=[this, id](const std::string &text, const gweni::Color &color)
    {
        setStatus(id, text, color);
        setSaving(id, false);
    };

    // the catalog's api_format first (its outcome is not checked, as before)
    m_data.putJson("/api/models/config", {{"model", entry.model}, {"api_format", entry.apiFormat}}, [this, entry, id, finish](const Response &)
    {
        saveAll([this, entry, id, finish](bool ok, const std::string &error)
        {
            if(!ok)
            {
                finish("Failed to save config: "+error, palette::bad);
                return;
            }
            setStatus(id, "Loading model...", palette::muted);
            const nlohmann::json body={{"variant", entry.variant}, {"context_size", entry.contextSize},
                {"runtime_options", entry.runtimeOptions}, {"devices", entry.devices}};
            m_data.postJson("/api/models/"+urlEncode(entry.model)+"/load", body, [finish](const Response &response)
            {
                if(!response.ok)
                    finish(response.error, palette::bad);
                else if(text(response.json, "status")=="downloading")
                    finish("Model is downloading. It will load automatically when ready.", palette::good);
                else
                    finish("Model loaded successfully.", palette::good);
            });
        });
    });
}

void ConfigPanel::selectModel(size_t index, const std::string &model, const std::string &variant)
{
    if(index>=m_models.size())
        return;
    StartupModel &entry=m_models[index];
    entry.model=model;
    entry.variant=variant;
    // the option's defaults, as the old page took them
    if(const nlohmann::json *option=findOption(model, variant))
    {
        if(number(*option, "effective_context_size")>0)
            entry.contextSize=static_cast<int>(number(*option, "effective_context_size"));
        entry.apiFormat=text(*option, "api_format");
    }
    else
    {
        entry.apiFormat.clear();
    }
    renderModels();
}

void ConfigPanel::setContextSize(size_t index, int contextSize)
{
    if(index>=m_models.size())
        return;
    m_models[index].contextSize=std::max(0, contextSize);
    renderEntryContext(index);
}

void ConfigPanel::setDevice(size_t index, int gpu, bool on)
{
    if(index>=m_models.size())
        return;
    std::vector<int> &devices=m_models[index].devices;
    devices.erase(std::remove(devices.begin(), devices.end(), gpu), devices.end());
    if(on)
    {
        devices.push_back(gpu);
        std::sort(devices.begin(), devices.end());
    }
    if(index<m_entries.size())
        m_entries[index].noDevices->setHidden(!devices.empty());
    // fewer devices may hold less, as the old page re-clamped on a toggle
    setSliderRange(index);
    renderEntryContext(index);
}

void ConfigPanel::setApiFormat(size_t index, const std::string &format)
{
    if(index<m_models.size())
        m_models[index].apiFormat=format;
}

void ConfigPanel::setRuntimeOption(size_t index, const std::string &option, const std::string &value)
{
    if(index>=m_models.size())
        return;
    nlohmann::json &options=m_models[index].runtimeOptions;
    if(!options.is_object())
        options=nlohmann::json::object();
    if(value.empty())
        options.erase(option);
    else if(value=="true" || value=="false")
        options[option]=value=="true";
    else
        options[option]=value;
}

void ConfigPanel::setStatus(uint64_t id, const std::string &text, const gweni::Color &color)
{
    const size_t index=indexOf(id);
    if(index>=m_models.size())
        return;
    EntryStatus &status=m_statuses[id];
    status.text=text;
    status.color=color;
    if(index<m_entries.size())
    {
        m_entries[index].status->setText(text);
        m_entries[index].status->setTextColor(color);
    }
}

void ConfigPanel::setSaving(uint64_t id, bool saving)
{
    const size_t index=indexOf(id);
    if(index>=m_models.size())
        return;
    m_statuses[id].saving=saving;
    if(index<m_entries.size())
        m_entries[index].save->setDisabled(saving);
}

int ConfigPanel::limitContext(size_t index)
{
    StartupModel &entry=m_models[index];
    const nlohmann::json *option=findOption(entry.model, entry.variant);
    int maxContext=option && number(*option, "max_context_size")>0?static_cast<int>(number(*option, "max_context_size")):131072;
    if(option)
    {
        const ContextFit fit=contextFit(*option, devicesVram(entry));
        if(fit.tight>0 && fit.tight<maxContext)
        {
            maxContext=std::max(fit.tight, 1024);
            if(entry.contextSize>maxContext)
                entry.contextSize=maxContext;
        }
    }
    return maxContext;
}

void ConfigPanel::setSliderRange(size_t index)
{
    if(index>=m_entries.size() || index>=m_models.size())
        return;
    Entry &view=m_entries[index];
    view.maxContext=limitContext(index);
    // a new range moves the value, which is not an edit
    m_settingSlider=true;
    view.context->setRange(0.0f, static_cast<float>(view.maxContext));
    view.context->setNotchCount(view.maxContext/1024);
    view.context->setClampToNotches(true);
    view.context->setFloatValue(static_cast<float>(std::min(m_models[index].contextSize, view.maxContext)), false);
    m_settingSlider=false;
}

void ConfigPanel::renderEntryContext(size_t index)
{
    if(index>=m_entries.size() || index>=m_models.size())
        return;
    const StartupModel &entry=m_models[index];
    Entry &view=m_entries[index];

    const nlohmann::json *option=findOption(entry.model, entry.variant);
    const double vram=devicesVram(entry);
    const ContextFit fit=option?contextFit(*option, vram):ContextFit();

    view.contextLabel->setText("Context Size  "+contextLabel(entry.contextSize));
    if(entry.contextSize==0)
    {
        view.fit->setText("Auto: the server picks the largest context that fits the assigned devices' VRAM.");
        view.fit->setTextColor(palette::info);
    }
    else if(fit.known && fit.tight==0)
    {
        view.fit->setText("Does not fit in "+formatMb(vram)+" of VRAM");
        view.fit->setTextColor(palette::bad);
    }
    else if(fit.tight>0)
    {
        view.fit->setText("Fits comfortably up to "+contextLabel(fit.likely)+", tight up to "+contextLabel(fit.tight)
            +" on "+formatMb(vram)+" of VRAM");
        view.fit->setTextColor(entry.contextSize<=fit.likely?palette::good:(entry.contextSize<=fit.tight?palette::warn:palette::bad));
    }
    else
    {
        view.fit->setText("");
    }
}

void ConfigPanel::renderModels()
{
    gweni::controls::Base::List previous=m_list->getChildren();
    for(gweni::controls::Base *child:previous)
        child->delayedDelete();
    m_entries.clear();

    int listHeight=0;
    if(m_models.empty())
    {
        addLine(m_list, "No startup models configured. Click \"+ Add Model\" to add one.", RowHeight);
        listHeight=RowHeight;
    }

    std::vector<std::pair<std::string, std::string>> modelItems={{"-- Select Model --", modelKey("", "")}};
    for(const nlohmann::json &option:m_options)
        modelItems.push_back({modelLabel(text(option, "model"), text(option, "variant")), modelKey(text(option, "model"), text(option, "variant"))});
    const std::vector<std::pair<std::string, std::string>> defaults={{"Default", ""}, {"Enabled", "true"}, {"Disabled", "false"}};
    std::vector<std::pair<std::string, std::string>> kvTypes={{"Default", ""}};
    for(const char *type:KvTypes)
        kvTypes.push_back({type, type});

    for(size_t i=0; i<m_models.size(); ++i)
    {
        const StartupModel &entry=m_models[i];
        Entry view;
        int height=0;

        view.box=m_list->newChild<gweni::controls::Base>();
        view.box->setDock(gweni::DockPosition::Top);
        view.box->setPadding(gweni::Padding(10, 8, 10, 8));
        view.box->setMargin(gweni::Margin(0, 0, 12, 10));

        gweni::controls::Base *top=view.box->newChild<gweni::controls::Base>();
        top->setDock(gweni::DockPosition::Top);
        top->setHeight(RowHeight);
        height+=RowHeight;
        gweni::controls::Label *title=top->newChild<gweni::controls::Label>();
        title->setText("Model "+std::to_string(i+1));
        title->setTextColor(palette::accent);
        title->setDock(gweni::DockPosition::Left);
        view.remove=top->newChild<gweni::controls::Button>();
        view.remove->setText("Remove");
        view.remove->setTextColor(palette::bad);
        view.remove->setDock(gweni::DockPosition::Right);
        fixWidth(view.remove, 80);
        view.save=top->newChild<gweni::controls::Button>();
        view.save->setText("Save & Load");
        view.save->setDock(gweni::DockPosition::Right);
        fixWidth(view.save, 100);
        view.save->setMargin(gweni::Margin(0, 0, 6, 0));
        view.save->onPressCaller.addCallback(view.save, [this, i](gweni::event::Handler &, gweni::event::Info) { saveAndLoad(i); });
        view.remove->onPressCaller.addCallback(view.remove, [this, i](gweni::event::Handler &, gweni::event::Info) { removeModel(i); });

        view.status=addLine(view.box, "", LabelHeight);
        height+=LabelHeight;

        addLine(view.box, "Model & Variant", LabelHeight);
        view.model=addChoice(view.box, "", modelItems, modelKey(entry.model, entry.variant));
        height+=LabelHeight+RowHeight+2;
        if(!entry.model.empty() && !findOption(entry.model, entry.variant))
        {
            // a saved model missing from the catalog still shows as itself
            view.model->addItem(modelLabel(entry.model, entry.variant)+"  (not in the catalog)", modelKey(entry.model, entry.variant));
            view.model->selectItemByName(modelKey(entry.model, entry.variant), false);
        }
        view.model->onSelection.addCallback(view.model, [this, i](gweni::event::Handler &, gweni::event::Info)
        {
            const std::string key=selectedName(m_entries[i].model);
            const size_t split=key.find("||");
            selectModel(i, key.substr(0, split), split==std::string::npos?std::string():key.substr(split+2));
        });

        view.contextLabel=addLine(view.box, "", LabelHeight);
        view.context=view.box->newChild<gweni::controls::HorizontalSlider>();
        view.context->setDock(gweni::DockPosition::Top);
        view.context->setHeight(RowHeight);
        view.context->onValueChanged.addCallback(view.context, [this, i](gweni::event::Handler &, gweni::event::Info)
        {
            if(m_settingSlider)
                return;
            // a max that is not whole 1K steps: the last notch rounds past it
            const float value=m_entries[i].context->getFloatValue();
            setContextSize(i, std::min(static_cast<int>(std::lround(value/1024.0f))*1024, m_entries[i].maxContext));
        });
        view.fit=addLine(view.box, "", LabelHeight);
        height+=LabelHeight+RowHeight+LabelHeight;

        addLine(view.box, "Compute Devices", LabelHeight);
        height+=LabelHeight;
        for(const nlohmann::json &gpu:m_gpus)
        {
            const int index=static_cast<int>(number(gpu, "index"));
            gweni::controls::CheckBoxWithLabel *device=view.box->newChild<gweni::controls::CheckBoxWithLabel>();
            device->setDock(gweni::DockPosition::Top);
            device->setHeight(22);
            device->label()->setText("GPU"+std::to_string(index)+": "+text(gpu, "name")+" ("+formatMb(number(gpu, "vram_total_mb"))+")");
            device->checkBox()->setChecked(std::find(entry.devices.begin(), entry.devices.end(), index)!=entry.devices.end());
            device->checkBox()->onCheckChanged.addCallback(device, [this, i, index, device](gweni::event::Handler &, gweni::event::Info)
            {
                setDevice(i, index, device->checkBox()->isChecked());
            });
            view.devices.push_back(device);
            height+=22;
        }
        view.noDevices=addLine(view.box, "No devices selected - auto-assignment will be used.", LabelHeight, palette::warn);
        view.noDevices->setHidden(!entry.devices.empty());
        height+=LabelHeight;

        // none saved: the option's, shown and saved, as the old page did
        if(entry.apiFormat.empty())
        {
            if(const nlohmann::json *option=findOption(entry.model, entry.variant))
                m_models[i].apiFormat=text(*option, "api_format");
        }
        view.apiFormat=addChoice(view.box, "API Format", {{"Standard (OpenAI)", ""}, {"Harmony (auto-convert to OpenAI)", "harmony"}}, entry.apiFormat);
        view.apiFormat->onSelection.addCallback(view.apiFormat, [this, i](gweni::event::Handler &, gweni::event::Info)
        {
            setApiFormat(i, selectedName(m_entries[i].apiFormat));
        });
        addLine(view.box, "Harmony: the server parses the model's channel tags and converts the output to the OpenAI format.", LabelHeight);
        height+=RowHeight+2+LabelHeight;

        addLine(view.box, "Runtime Options", LabelHeight);
        height+=LabelHeight;
        const nlohmann::json &options=entry.runtimeOptions;
        view.kvk=addChoice(view.box, "KV Cache (K)", kvTypes, text(options, "kv_cache_type_k"));
        view.kvv=addChoice(view.box, "KV Cache (V)", kvTypes, text(options, "kv_cache_type_v"));
        view.flashAttn=addChoice(view.box, "Flash Attention", defaults, boolOption(options, "flash_attn"));
        view.noHostVisible=addChoice(view.box, "No Host-Visible VRAM", defaults, boolOption(options, "vulkan_no_host_visible_vram"));
        view.noHostVisible->setToolTipText("GGML_VK_DISABLE_HOST_VISIBLE_VIDMEM: skip the BAR-mapped heap, device-local memory only");
        height+=4*(RowHeight+2);
        const std::pair<gweni::controls::ComboBox *, const char *> runtime[]={{view.kvk, "kv_cache_type_k"},
            {view.kvv, "kv_cache_type_v"}, {view.flashAttn, "flash_attn"}, {view.noHostVisible, "vulkan_no_host_visible_vram"}};
        for(const auto &[combo, key]:runtime)
        {
            combo->onSelection.addCallback(combo, [this, i, combo=combo, key=std::string(key)](gweni::event::Handler &, gweni::event::Info)
            {
                setRuntimeOption(i, key, selectedName(combo));
            });
        }

        view.box->setHeight(height+16);
        listHeight+=height+16+10;
        auto status=m_statuses.find(entry.id);
        if(status!=m_statuses.end())
        {
            view.status->setText(status->second.text);
            view.status->setTextColor(status->second.color);
            view.save->setDisabled(status->second.saving);
        }
        m_entries.push_back(view);
        setSliderRange(i);
        renderEntryContext(i);
    }

    m_list->setHeight(listHeight);
    m_listHeight=listHeight;
    layoutContent();
}

void ConfigPanel::layoutContent()
{
    // the scrolled content is as tall as its parts: the intro card, the
    // message, the header, the models, the GPU card and the updates card,
    // with their margins
    m_content->setHeight(60+12+(m_message->getText().empty()?0:RowHeight)+RowHeight+8+m_listHeight+m_gpuCardHeight+12
        +UpdateSettings::Height+12);
}

void ConfigPanel::renderGpus()
{
    gweni::controls::Base::List previous=m_gpuList->getChildren();
    for(gweni::controls::Base *child:previous)
        child->delayedDelete();
    m_gpuRows.clear();

    if(m_gpus.empty())
    {
        addLine(m_gpuList, "No GPUs detected.", RowHeight);
        m_gpuList->setHeight(RowHeight);
        m_gpuCardHeight=RowHeight+LabelHeight+40;
        m_gpuCard->setHeight(m_gpuCardHeight);
        layoutContent();
        return;
    }

    for(const nlohmann::json &gpu:m_gpus)
    {
        const int index=static_cast<int>(number(gpu, "index"));
        const double total=number(gpu, "vram_total_mb");
        const bool overridden=flag(gpu, "vram_overridden");

        gweni::controls::Base *row=m_gpuList->newChild<gweni::controls::Base>();
        row->setDock(gweni::DockPosition::Top);
        row->setHeight(40);
        row->setMargin(gweni::Margin(0, 2, 0, 2));

        GpuRow view;
        // Override rightmost, so the rows line up whether Clear shows or not
        view.overrideButton=row->newChild<gweni::controls::Button>();
        view.overrideButton->setText("Override");
        view.overrideButton->setDock(gweni::DockPosition::Right);
        fixWidth(view.overrideButton, 80);
        view.overrideButton->onPressCaller.addCallback(view.overrideButton, [this, index, total](gweni::event::Handler &, gweni::event::Info) { m_vram.prompt(index, total); });
        view.clearButton=row->newChild<gweni::controls::Button>();
        view.clearButton->setText("Clear");
        view.clearButton->setDock(gweni::DockPosition::Right);
        fixWidth(view.clearButton, 60);
        view.clearButton->setMargin(gweni::Margin(0, 0, 6, 0));
        view.clearButton->setHidden(!overridden);
        view.clearButton->onPressCaller.addCallback(view.clearButton, [this, index](gweni::event::Handler &, gweni::event::Info) { m_vram.clear(index); });

        view.name=addLine(row, text(gpu, "name")+" ("+text(gpu, "backend")+")"+(overridden?"  [overridden]":""), 20, gweni::Color(220, 224, 240));
        view.memory=addLine(row, "VRAM: "+formatMb(total)+" total  -  Free: "+formatMb(number(gpu, "vram_free_mb")), 18);
        m_gpuRows.push_back(view);
    }
    m_gpuList->setHeight(static_cast<int>(m_gpus.size())*44);
    m_gpuCardHeight=static_cast<int>(m_gpus.size())*44+LabelHeight+40;
    m_gpuCard->setHeight(m_gpuCardHeight);
    layoutContent();
}

}//namespace dashboard
}//namespace arbiterAI
