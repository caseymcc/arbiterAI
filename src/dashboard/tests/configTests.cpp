#include "dashboardTest.h"
#include "panels/widgets.h"

#include <gweni/controls/textBox.h>
#include <gweni/controls/toast.h>

using arbiterAI::dashboard::ConfigPanel;
using arbiterAI::dashboard::ContextFit;
using arbiterAI::dashboard::Tab;
using arbiterAI::dashboard::contextFit;
namespace palette=arbiterAI::dashboard::palette;

namespace
{

std::string selected(gweni::controls::ComboBox *combo)
{
    gweni::controls::Label *item=combo->getSelectedItem();
    return item?item->getName():std::string();
}

}//namespace

class ConfigTest: public DashboardTest
{
protected:
    ConfigPanel &config() { return m_app->config(); }

    void ok(const std::string &path, const std::string &method, const nlohmann::json &reply=nlohmann::json::object())
    {
        m_data.setJson(path, reply, method);
    }

    /// the methods and paths of the requests that are not polls (GETs)
    std::vector<std::string> writes()
    {
        std::vector<std::string> list;
        for(const auto &request:m_data.requests())
        {
            if(request.method!="GET")
                list.push_back(request.method+" "+request.path);
        }
        return list;
    }

    const nlohmann::json *lastBody(const std::string &method, const std::string &path)
    {
        const nlohmann::json *body=nullptr;
        for(const auto &request:m_data.requests())
        {
            if(request.method==method && request.path==path)
                body=&request.body;
        }
        return body;
    }
};

TEST(ConfigFit, LikelyAtEightyFivePercentTightAtAllOfTheVram)
{
    const nlohmann::json option={{"base_memory_mb", 65536}, {"base_context_size", 4096}, {"memory_per_1k_context_mb", 128}};
    ContextFit fit=contextFit(option, 122880);
    EXPECT_EQ(fit.likely, 315392);
    EXPECT_EQ(fit.tight, 462848);

    EXPECT_TRUE(fit.known);

    EXPECT_EQ(contextFit(option, 0).tight, 0);
    EXPECT_FALSE(contextFit(option, 0).known);
    EXPECT_FALSE(contextFit({{"base_memory_mb", 1}}, 1000).known);
    // less VRAM than the base: nothing fits, which is known
    EXPECT_EQ(contextFit(option, 32768).tight, 0);
    EXPECT_TRUE(contextFit(option, 32768).known);
}

TEST_F(ConfigTest, LoadsOnceWhenTheTabFirstShows)
{
    start("/dashboard");
    frames(125);
    EXPECT_EQ(m_data.count("/api/server/config"), 0u);

    click(m_app->tabButton(Tab::Config));
    EXPECT_EQ(m_data.count("/api/server/config"), 1u);
    EXPECT_TRUE(config().loaded());
    frames(250);
    EXPECT_EQ(m_data.count("/api/server/config"), 1u);
}

TEST_F(ConfigTest, ShowsTheSavedStartupModels)
{
    start("/dashboard/config");
    ASSERT_EQ(config().models().size(), 2u);
    ASSERT_EQ(config().entries().size(), 2u);

    const ConfigPanel::Entry &first=config().entries()[0];
    EXPECT_EQ(selected(first.model), "gpt-oss-120b||Q4_K_M");
    EXPECT_EQ(first.contextLabel->getText(), "Context Size  32K");
    EXPECT_FLOAT_EQ(first.context->getFloatValue(), 32768.0f);
    ASSERT_EQ(first.devices.size(), 2u);
    EXPECT_TRUE(first.devices[0]->checkBox()->isChecked());
    EXPECT_FALSE(first.devices[1]->checkBox()->isChecked());
    EXPECT_EQ(first.devices[1]->label()->getText(), "GPU1: NVIDIA GeForce RTX 4090 (24.0 GB)");
    EXPECT_TRUE(first.noDevices->hidden());
    EXPECT_EQ(selected(first.apiFormat), "harmony");
    EXPECT_EQ(selected(first.kvk), "q8_0");
    EXPECT_EQ(selected(first.kvv), "");
    EXPECT_EQ(selected(first.flashAttn), "true");
    EXPECT_EQ(selected(first.noHostVisible), "");

    const ConfigPanel::Entry &second=config().entries()[1];
    EXPECT_EQ(second.contextLabel->getText(), "Context Size  Auto");
    EXPECT_EQ(second.fit->getText(), "Auto: the server picks the largest context that fits the assigned devices' VRAM.");
    EXPECT_FALSE(second.noDevices->hidden());
}

TEST_F(ConfigTest, ModelListMergesTheVulkanAndCudaOptions)
{
    start("/dashboard/config");
    ASSERT_EQ(config().options().size(), 4u);
    EXPECT_EQ(arbiterAI::dashboard::text(config().options()[3], "model"), "llama-3.1-8b");
}

TEST_F(ConfigTest, ContextNoteShowsTheFitOnTheChosenDevices)
{
    start("/dashboard/config");
    gweni::controls::Label *fit=config().entries()[0].fit;
    EXPECT_EQ(fit->getText(), "Fits comfortably up to 308K, tight up to 452K on 120.0 GB of VRAM");
    EXPECT_TRUE(fit->getTextColor()==palette::good);

    // on GPU1 alone (24 GB) the model does not fit at all
    config().setDevice(0, 1, true);
    config().setDevice(0, 0, false);
    EXPECT_EQ(fit->getText(), "Does not fit in 24.0 GB of VRAM");
    EXPECT_TRUE(fit->getTextColor()==palette::bad);
}

TEST_F(ConfigTest, ChoosingAModelTakesItsDefaults)
{
    start("/dashboard/config");
    config().addModel();
    ASSERT_EQ(config().entries().size(), 3u);
    config().entries()[2].model->selectItemByName("llama-3.1-8b||Q4_K_M");
    frames(2);

    ASSERT_EQ(config().models().size(), 3u);
    EXPECT_EQ(config().models()[2].model, "llama-3.1-8b");
    EXPECT_EQ(config().models()[2].variant, "Q4_K_M");
    EXPECT_EQ(config().models()[2].contextSize, 16384);
    EXPECT_EQ(config().models()[2].apiFormat, "");
}

TEST_F(ConfigTest, EditsInTheControlsReachTheSavedBody)
{
    start("/dashboard/config");
    const ConfigPanel::Entry &second=config().entries()[1];
    second.devices[1]->checkBox()->setChecked(true);
    second.kvv->selectItemByName("f16");
    second.noHostVisible->selectItemByName("true");
    second.apiFormat->selectItemByName("harmony");
    second.context->setFloatValue(20480.0f, true);
    frames(2);

    const nlohmann::json body=config().configBody();
    ASSERT_EQ(body["startup_models"].size(), 2u);
    EXPECT_EQ(body["startup_models"][1], nlohmann::json({{"model", "qwen3-8b"}, {"variant", "Q8_0"}, {"context_size", 20480},
        {"runtime_options", {{"kv_cache_type_v", "f16"}, {"vulkan_no_host_visible_vram", true}}}, {"devices", {1}}, {"api_format", "harmony"}}));
    EXPECT_EQ(body["startup_models"][0]["runtime_options"], nlohmann::json({{"kv_cache_type_k", "q8_0"}, {"flash_attn", true}}));
}

TEST_F(ConfigTest, SaveAndLoadSavesTheConfigThenLoadsTheModel)
{
    ok("/api/models/config", "PUT");
    ok("/api/server/config", "PUT", loadFixture("serverConfig.json"));
    ok("/api/models/gpt-oss-120b/load", "POST", {{"status", "loaded"}});
    start("/dashboard/config");
    click(config().entries()[0].save);

    EXPECT_EQ(writes(), std::vector<std::string>({"PUT /api/models/config", "PUT /api/server/config", "POST /api/models/gpt-oss-120b/load"}));
    ASSERT_NE(lastBody("PUT", "/api/models/config"), nullptr);
    ASSERT_NE(lastBody("PUT", "/api/server/config"), nullptr);
    ASSERT_NE(lastBody("POST", "/api/models/gpt-oss-120b/load"), nullptr);
    EXPECT_EQ(*lastBody("PUT", "/api/models/config"), nlohmann::json({{"model", "gpt-oss-120b"}, {"api_format", "harmony"}}));
    EXPECT_EQ((*lastBody("PUT", "/api/server/config"))["startup_models"].size(), 2u);
    EXPECT_EQ(*lastBody("POST", "/api/models/gpt-oss-120b/load"), nlohmann::json({{"variant", "Q4_K_M"}, {"context_size", 32768},
        {"runtime_options", {{"kv_cache_type_k", "q8_0"}, {"flash_attn", true}}}, {"devices", {0}}}));
    EXPECT_EQ(config().entries()[0].status->getText(), "Model loaded successfully.");
    EXPECT_TRUE(config().entries()[0].status->getTextColor()==palette::good);
    EXPECT_FALSE(config().entries()[0].save->isDisabled());
    shot("config-saved");
}

TEST_F(ConfigTest, ADownloadingModelSaysItWillLoadWhenReady)
{
    ok("/api/models/config", "PUT");
    ok("/api/server/config", "PUT");
    ok("/api/models/gpt-oss-120b/load", "POST", {{"status", "downloading"}});
    start("/dashboard/config");
    config().saveAndLoad(0);
    EXPECT_EQ(config().entries()[0].status->getText(), "Model is downloading. It will load automatically when ready.");
}

TEST_F(ConfigTest, AFailedSaveShowsInPlaceAndLoadsNothing)
{
    ok("/api/models/config", "PUT");
    m_data.setFailure("/api/server/config", 500, "disk full", "PUT");
    start("/dashboard/config");
    config().saveAndLoad(0);
    EXPECT_EQ(config().entries()[0].status->getText(), "Failed to save config: disk full");
    EXPECT_TRUE(config().entries()[0].status->getTextColor()==palette::bad);
    EXPECT_EQ(m_data.count("/api/models/gpt-oss-120b/load", "POST"), 0u);
}

TEST_F(ConfigTest, AFailedLoadShowsTheServersReason)
{
    ok("/api/models/config", "PUT");
    ok("/api/server/config", "PUT");
    m_data.setFailure("/api/models/gpt-oss-120b/load", 409, "not enough VRAM", "POST");
    start("/dashboard/config");
    config().saveAndLoad(0);
    EXPECT_EQ(config().entries()[0].status->getText(), "not enough VRAM");
    EXPECT_TRUE(config().entries()[0].status->getTextColor()==palette::bad);
}

TEST_F(ConfigTest, SavingAnEntryWithoutAModelAsksForOne)
{
    start("/dashboard/config");
    config().addModel();
    config().saveAndLoad(2);
    EXPECT_EQ(config().entries()[2].status->getText(), "Select a model first.");
    EXPECT_TRUE(writes().empty());
}

TEST_F(ConfigTest, RemoveUnloadsTheModelAndSavesWithoutIt)
{
    ok("/api/models/gpt-oss-120b/unload", "POST");
    ok("/api/server/config", "PUT");
    start("/dashboard/config");
    click(config().entries()[0].remove);

    ASSERT_EQ(config().models().size(), 1u);
    EXPECT_EQ(config().models()[0].model, "qwen3-8b");
    EXPECT_EQ(writes(), std::vector<std::string>({"POST /api/models/gpt-oss-120b/unload", "PUT /api/server/config"}));
    const nlohmann::json *body=lastBody("PUT", "/api/server/config");
    ASSERT_NE(body, nullptr);
    ASSERT_EQ((*body)["startup_models"].size(), 1u);
    EXPECT_EQ((*body)["startup_models"][0]["model"], "qwen3-8b");
}

TEST_F(ConfigTest, AnUnreachableServerShowsAMessageAndIsAskedAgain)
{
    m_data.setFailure("/api/server/config", 0, "");
    start("/dashboard/config");
    EXPECT_FALSE(config().loaded());
    EXPECT_EQ(config().message(), "Failed to load the server configuration: the server cannot be reached");

    m_data.setJson("/api/server/config", loadFixture("serverConfig.json"));
    frames(125);
    EXPECT_TRUE(config().loaded());
    EXPECT_EQ(config().message(), "");
    EXPECT_EQ(config().models().size(), 2u);
}

TEST_F(ConfigTest, VramOverridesForEachGpu)
{
    ok("/api/hardware/vram-override", "POST");
    start("/dashboard/config");
    const auto &rows=config().gpuRows();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].name->getText(), "AMD Radeon Graphics (RADV GFX1151) (Vulkan)");
    EXPECT_EQ(rows[0].memory->getText(), "VRAM: 120.0 GB total  -  Free: 87.2 GB");
    EXPECT_TRUE(rows[0].clearButton->hidden());
    EXPECT_EQ(rows[1].name->getText(), "NVIDIA GeForce RTX 4090 (CUDA)  [overridden]");
    EXPECT_FALSE(rows[1].clearButton->hidden());

    const size_t hardware=m_data.count("/api/hardware");
    // the VRAM card is below the startup models
    config().scroll()->scrollToBottom();
    frames(2);
    shot("config-vram");
    click(rows[0].overrideButton);
    ASSERT_NE(config().vram().dialog(), nullptr);
    config().vram().dialog()->getField()->setText("65536");
    config().vram().dialog()->close(gweni::dialogs::Result::Ok);
    ASSERT_NE(lastBody("POST", "/api/hardware/vram-override"), nullptr);
    EXPECT_EQ(*lastBody("POST", "/api/hardware/vram-override"), nlohmann::json({{"gpu_index", 0}, {"vram_mb", 65536}}));
    // the GPU list is read again once the server took it
    EXPECT_GT(m_data.count("/api/hardware"), hardware);
}

TEST_F(ConfigTest, EntriesWithoutAModelAreNotSaved)
{
    start("/dashboard/config");
    config().addModel();
    ASSERT_EQ(config().models().size(), 3u);
    const nlohmann::json body=config().configBody();
    ASSERT_EQ(body["startup_models"].size(), 2u);
    EXPECT_EQ(body["startup_models"][1]["model"], "qwen3-8b");
}

TEST_F(ConfigTest, ASavedContextAboveThePlaceholderMaxIsKept)
{
    // the config arrives before the options: until then the slider's max is
    // a placeholder, which must not lower the saved value
    nlohmann::json saved=loadFixture("serverConfig.json");
    saved["startup_models"][0]["context_size"]=262144;
    m_data.setJson("/api/server/config", saved);
    nlohmann::json vulkan=loadFixture("startupOptionsVulkan.json");
    vulkan["options"][0]["max_context_size"]=262144;
    m_data.setJson("/api/server/startup-options?accelerator=vulkan", vulkan);
    start("/dashboard/config");

    EXPECT_EQ(config().models()[0].contextSize, 262144);
    EXPECT_EQ(config().configBody()["startup_models"][0]["context_size"], 262144);
    EXPECT_FLOAT_EQ(config().entries()[0].context->getFloatValue(), 262144.0f);
}

TEST_F(ConfigTest, AContextPastWhatTheDevicesHoldIsLowered)
{
    // 120 GB holds 452K of this model, so a saved 512K is lowered, as the old page did
    nlohmann::json saved=loadFixture("serverConfig.json");
    saved["startup_models"][0]["context_size"]=524288;
    m_data.setJson("/api/server/config", saved);
    nlohmann::json vulkan=loadFixture("startupOptionsVulkan.json");
    vulkan["options"][0]["max_context_size"]=1048576;
    m_data.setJson("/api/server/startup-options?accelerator=vulkan", vulkan);
    start("/dashboard/config");

    EXPECT_EQ(config().models()[0].contextSize, 462848);
}

TEST_F(ConfigTest, VulkanOptionsWinWhicheverReplyArrivesFirst)
{
    start("/dashboard");
    config().updateOptions(loadFixture("startupOptionsCuda.json"));
    config().updateOptions(loadFixture("startupOptionsVulkan.json"));

    const std::vector<nlohmann::json> &options=config().options();
    ASSERT_EQ(options.size(), 4u);
    // Vulkan's own order, then what only CUDA offers
    EXPECT_EQ(arbiterAI::dashboard::text(options[0], "model"), "gpt-oss-120b");
    EXPECT_EQ(arbiterAI::dashboard::text(options[2], "model"), "qwen3-8b");
    EXPECT_EQ(arbiterAI::dashboard::number(options[2], "base_memory_mb"), 9000);
    EXPECT_EQ(arbiterAI::dashboard::text(options[3], "model"), "llama-3.1-8b");
}

TEST_F(ConfigTest, RepliesThatAreNotObjectsAreIgnored)
{
    start("/dashboard/config");
    EXPECT_NO_THROW(config().updateHardware(nlohmann::json::array()));
    EXPECT_NO_THROW(config().updateOptions(nlohmann::json("busy")));
    EXPECT_NO_THROW(config().updateConfig(nlohmann::json::array()));
    EXPECT_EQ(config().gpuRows().size(), 2u);
    EXPECT_EQ(config().options().size(), 4u);
    EXPECT_EQ(config().models().size(), 2u);
}
