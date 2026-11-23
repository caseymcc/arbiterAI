#ifndef _ARBITERAI_DASHBOARD_CONFIGPANEL_H_
#define _ARBITERAI_DASHBOARD_CONFIGPANEL_H_

#include "data/dataSource.h"
#include "panels/updateSettings.h"
#include "panels/vramOverride.h"

#include <gweni/controls.h>
#include <gweni/controls/checkBox.h>
#include <gweni/controls/comboBox.h>
#include <gweni/controls/horizontalSlider.h>
#include <gweni/controls/scrollControl.h>

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arbiterAI
{
namespace dashboard
{

/// One startup model as /api/server/config's startup_models holds it.
struct StartupModel {
    std::string model;
    std::string variant;
    /// 0: the largest that fits the devices (the server's "auto")
    int contextSize=0;
    nlohmann::json runtimeOptions=nlohmann::json::object();
    std::vector<int> devices;
    std::string apiFormat;
    /// the panel's handle for the entry while it is edited; not saved
    uint64_t id=0;

    nlohmann::json toJson() const;
    static StartupModel fromJson(const nlohmann::json &json);
};

/// The context limits a model fits on some VRAM, from its startup option's
/// memory figures (base memory at a base context, plus memory per 1K tokens):
/// "likely" at 85% of the VRAM, "tight" at all of it. 0 when unknown or when
/// nothing fits; \a known tells the two apart.
struct ContextFit {
    int likely=0;
    int tight=0;
    bool known=false;
};
ContextFit contextFit(const nlohmann::json &option, double vramMb);

/// The Config tab (the old Configuration page): the startup models, each with
/// its model and variant, context size, compute devices, API format and
/// runtime options (KV cache types, flash attention, no host-visible VRAM),
/// saved to /api/server/config and loaded at once; the VRAM overrides; and
/// the update settings (UpdateSettings).
/// It loads once (load()) and is not polled, so edits are never overwritten.
class ConfigPanel {
public:
    ConfigPanel(gweni::controls::Base *page, DataSource &data);

    /// /api/server/config, /api/hardware and the vulkan and cuda startup
    /// options; again until the config arrives (a server restarting)
    void load();
    bool loaded() const { return m_loaded; }

    void updateConfig(const nlohmann::json &config);
    void updateHardware(const nlohmann::json &hardware);
    /// a /api/server/startup-options reply, merged into the model list:
    /// Vulkan's entry wins for a model and variant both accelerators offer
    void updateOptions(const nlohmann::json &options);

    void addModel();
    /// the model is unloaded and the config saved without it
    void removeModel(size_t index);
    /// PUT /api/models/config (api_format), PUT /api/server/config, then
    /// POST /api/models/<model>/load; each step's outcome in the entry's status
    void saveAndLoad(size_t index);
    /// the edited entries as PUT /api/server/config takes them
    nlohmann::json configBody() const;

    void selectModel(size_t index, const std::string &model, const std::string &variant);
    void setContextSize(size_t index, int contextSize);
    void setDevice(size_t index, int gpu, bool on);
    void setApiFormat(size_t index, const std::string &format);
    /// \p option kv_cache_type_k, kv_cache_type_v, flash_attn or
    /// vulkan_no_host_visible_vram; \p value "" (default), a type, "true" or "false"
    void setRuntimeOption(size_t index, const std::string &option, const std::string &value);

    const std::vector<StartupModel> &models() const { return m_models; }
    const std::vector<nlohmann::json> &options() const { return m_options; }
    const std::string &message() const { return m_message->getText(); }

    /// the controls of an entry, as built for it
    struct Entry {
        gweni::controls::Base *box=nullptr;
        gweni::controls::Label *status=nullptr;
        gweni::controls::Button *save=nullptr;
        gweni::controls::Button *remove=nullptr;
        gweni::controls::ComboBox *model=nullptr;
        gweni::controls::HorizontalSlider *context=nullptr;
        /// the slider's max: the option's, lowered to what the devices hold
        int maxContext=0;
        gweni::controls::Label *contextLabel=nullptr;
        gweni::controls::Label *fit=nullptr;
        std::vector<gweni::controls::CheckBoxWithLabel *> devices;
        gweni::controls::Label *noDevices=nullptr;
        gweni::controls::ComboBox *apiFormat=nullptr;
        gweni::controls::ComboBox *kvk=nullptr;
        gweni::controls::ComboBox *kvv=nullptr;
        gweni::controls::ComboBox *flashAttn=nullptr;
        gweni::controls::ComboBox *noHostVisible=nullptr;
    };
    const std::vector<Entry> &entries() const { return m_entries; }
    gweni::controls::Button *addButton() const { return m_add; }
    struct GpuRow {
        gweni::controls::Label *name=nullptr;
        gweni::controls::Label *memory=nullptr;
        gweni::controls::Button *overrideButton=nullptr;
        gweni::controls::Button *clearButton=nullptr;
    };
    const std::vector<GpuRow> &gpuRows() const { return m_gpuRows; }
    VramOverride &vram() { return m_vram; }
    UpdateSettings &updates() { return *m_updates; }
    gweni::controls::ScrollControl *scroll() const { return m_scroll; }

private:
    const nlohmann::json *findOption(const std::string &model, const std::string &variant) const;
    /// the devices' VRAM, or every GPU's with none chosen
    double devicesVram(const StartupModel &entry) const;
    void renderModels();
    void renderEntryContext(size_t index);
    /// the entry's context max, lowering its saved value to what its devices
    /// hold; the placeholder max (no option yet) lowers nothing
    int limitContext(size_t index);
    void setSliderRange(size_t index);
    void renderGpus();
    void layoutContent();
    /// the entry's index now, or m_models.size() once it is removed
    size_t indexOf(uint64_t id) const;
    void setStatus(uint64_t id, const std::string &text, const gweni::Color &color);
    void setSaving(uint64_t id, bool saving);
    void addEntry(StartupModel entry);
    void saveAll(std::function<void(bool ok, const std::string &error)> done);

    DataSource &m_data;
    gweni::controls::Base *m_page;
    VramOverride m_vram;
    gweni::controls::ScrollControl *m_scroll;
    gweni::controls::Base *m_content;
    gweni::controls::Label *m_message;
    gweni::controls::Button *m_add;
    gweni::controls::Base *m_list;
    gweni::controls::Base *m_gpuCard;
    gweni::controls::Base *m_gpuList;
    std::unique_ptr<UpdateSettings> m_updates;

    std::vector<StartupModel> m_models;
    std::vector<Entry> m_entries;
    /// each entry's status line and whether a save is in flight, kept by id
    /// across re-renders
    struct EntryStatus {
        std::string text;
        gweni::Color color;
        bool saving=false;
    };
    std::map<uint64_t, EntryStatus> m_statuses;
    uint64_t m_nextId=1;
    std::vector<nlohmann::json> m_options;
    /// each accelerator's options, as its reply gave them
    std::map<std::string, nlohmann::json> m_optionReplies;
    nlohmann::json m_gpus=nlohmann::json::array();
    std::vector<GpuRow> m_gpuRows;
    int m_listHeight=0;
    int m_gpuCardHeight=0;
    bool m_loaded=false;
    bool m_loading=false;
    bool m_settingSlider=false;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_CONFIGPANEL_H_
