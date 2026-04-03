#include "arbiterAI/modelManager.h"
#include <gtest/gtest.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace arbiterAI
{

class ModelManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ModelManager::reset();
        // Create dummy config files in the expected directory structure
        std::filesystem::create_directory("config1");
        std::filesystem::create_directory("config1/models");
        std::ofstream outfile1("config1/models/model1.json");
        outfile1 << R"({
            "models": [
                {
                    "model": "model1",
                    "provider": "providerA",
                    "ranking": 90
                }
            ]
        })";
        outfile1.close();

        std::filesystem::create_directory("config2");
        std::filesystem::create_directory("config2/models");
        std::ofstream outfile2("config2/models/model2.json");
        outfile2 << R"({
            "models": [
                {
                    "model": "model2",
                    "provider": "providerB",
                    "ranking": 80
                }
            ]
        })";
        outfile2.close();

        std::filesystem::create_directory("override");
        std::ofstream override_outfile("override/override.json");
        override_outfile << R"({
            "models": [
                {
                    "model": "model1",
                    "provider": "override_provider",
                    "ranking": 100
                }
            ]
        })";
        override_outfile.close();
    }

    void TearDown() override
    {
        std::filesystem::remove_all("config1");
        std::filesystem::remove_all("config2");
        std::filesystem::remove_all("override");
    }
};

TEST_F(ModelManagerTest, InitializeWithMultipleConfigs)
{
    ModelManager &mm = ModelManager::instance();
    mm.initialize({"config1", "config2"});

    auto model1 = mm.getModelInfo("model1");
    auto model2 = mm.getModelInfo("model2");

    ASSERT_TRUE(model1.has_value());
    EXPECT_EQ(model1->provider, "providerA");
    ASSERT_TRUE(model2.has_value());
    EXPECT_EQ(model2->provider, "providerB");
}

TEST_F(ModelManagerTest, InitializeWithOverride)
{
    ModelManager &mm = ModelManager::instance();
    mm.initialize({"config1"}, "override");

    auto model1 = mm.getModelInfo("model1");
    ASSERT_TRUE(model1.has_value());
    EXPECT_EQ(model1->provider, "override_provider");
}

TEST_F(ModelManagerTest, VersionCompatibility)
{
    ModelInfo info;
    info.minClientVersion = "1.2.3";
    info.maxClientVersion = "2.0.0";

    EXPECT_TRUE(info.isCompatible("1.2.3"));
    EXPECT_TRUE(info.isCompatible("1.5.0"));
    EXPECT_TRUE(info.isCompatible("2.0.0"));
    EXPECT_FALSE(info.isCompatible("1.2.2"));
    EXPECT_FALSE(info.isCompatible("2.0.1"));
}

TEST_F(ModelManagerTest, SchemaCompatibility)
{
    ModelInfo info;
    info.minSchemaVersion = "1.1.0";
    info.configVersion = "1.2.0";

    EXPECT_TRUE(info.isSchemaCompatible("1.1.0"));
    EXPECT_TRUE(info.isSchemaCompatible("1.2.0"));
    EXPECT_FALSE(info.isSchemaCompatible("1.0.0"));
    EXPECT_FALSE(info.isSchemaCompatible("1.2.1"));
}

TEST_F(ModelManagerTest, ParseHardwareRequirements)
{
    std::filesystem::create_directory("config_hw");
    std::filesystem::create_directory("config_hw/models");
    std::ofstream outfile("config_hw/models/local_model.json");
    outfile << R"({
        "models": [
            {
                "model": "llama-7b",
                "provider": "llama",
                "ranking": 60,
                "hardware_requirements": {
                    "min_system_ram_mb": 8192,
                    "parameter_count": "7B"
                }
            }
        ]
    })";
    outfile.close();

    ModelManager &mm = ModelManager::instance();
    mm.initialize({"config_hw"});

    auto model = mm.getModelInfo("llama-7b");
    ASSERT_TRUE(model.has_value());
    ASSERT_TRUE(model->hardwareRequirements.has_value());
    EXPECT_EQ(model->hardwareRequirements->minSystemRamMb, 8192);
    EXPECT_EQ(model->hardwareRequirements->parameterCount, "7B");

    std::filesystem::remove_all("config_hw");
}

TEST_F(ModelManagerTest, ParseContextScaling)
{
    std::filesystem::create_directory("config_cs");
    std::filesystem::create_directory("config_cs/models");
    std::ofstream outfile("config_cs/models/scaled_model.json");
    outfile << R"({
        "models": [
            {
                "model": "llama-7b-scaled",
                "provider": "llama",
                "context_scaling": {
                    "base_context": 4096,
                    "max_context": 131072,
                    "vram_per_1k_context_mb": 64
                }
            }
        ]
    })";
    outfile.close();

    ModelManager &mm = ModelManager::instance();
    mm.initialize({"config_cs"});

    auto model = mm.getModelInfo("llama-7b-scaled");
    ASSERT_TRUE(model.has_value());
    ASSERT_TRUE(model->contextScaling.has_value());
    EXPECT_EQ(model->contextScaling->baseContext, 4096);
    EXPECT_EQ(model->contextScaling->maxContext, 131072);
    EXPECT_EQ(model->contextScaling->vramPer1kContextMb, 64);

    std::filesystem::remove_all("config_cs");
}

TEST_F(ModelManagerTest, ParseVariants)
{
    std::filesystem::create_directory("config_var");
    std::filesystem::create_directory("config_var/models");
    std::ofstream outfile("config_var/models/variant_model.json");
    outfile << R"({
        "models": [
            {
                "model": "llama-7b-variants",
                "provider": "llama",
                "variants": [
                    {
                        "quantization": "Q4_K_M",
                        "file_size_mb": 4370,
                        "min_vram_mb": 4096,
                        "recommended_vram_mb": 8192,
                        "download": {
                            "url": "https://example.com/model-q4.gguf",
                            "sha256": "abc123def456abc123def456abc123def456abc123def456abc123def456abcd",
                            "filename": "model-q4_k_m.gguf"
                        }
                    },
                    {
                        "quantization": "Q8_0",
                        "file_size_mb": 8100,
                        "min_vram_mb": 8192,
                        "recommended_vram_mb": 12288,
                        "download": {
                            "url": "https://example.com/model-q8.gguf",
                            "sha256": "def456abc123def456abc123def456abc123def456abc123def456abc123defg",
                            "filename": "model-q8_0.gguf"
                        }
                    }
                ]
            }
        ]
    })";
    outfile.close();

    ModelManager &mm = ModelManager::instance();
    mm.initialize({"config_var"});

    auto model = mm.getModelInfo("llama-7b-variants");
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->variants.size(), 2u);

    EXPECT_EQ(model->variants[0].quantization, "Q4_K_M");
    EXPECT_EQ(model->variants[0].fileSizeMb, 4370);
    EXPECT_EQ(model->variants[0].minVramMb, 4096);
    EXPECT_EQ(model->variants[0].recommendedVramMb, 8192);
    EXPECT_EQ(model->variants[0].download.url, "https://example.com/model-q4.gguf");
    EXPECT_EQ(model->variants[0].download.filename, "model-q4_k_m.gguf");

    EXPECT_EQ(model->variants[1].quantization, "Q8_0");
    EXPECT_EQ(model->variants[1].fileSizeMb, 8100);
    EXPECT_EQ(model->variants[1].minVramMb, 8192);
    EXPECT_EQ(model->variants[1].recommendedVramMb, 12288);

    std::filesystem::remove_all("config_var");
}

TEST_F(ModelManagerTest, NoHardwareFieldsForCloudModels)
{
    ModelManager &mm = ModelManager::instance();
    mm.initialize({"config1"});

    auto model = mm.getModelInfo("model1");
    ASSERT_TRUE(model.has_value());
    EXPECT_FALSE(model->hardwareRequirements.has_value());
    EXPECT_FALSE(model->contextScaling.has_value());
    EXPECT_TRUE(model->variants.empty());
}

TEST_F(ModelManagerTest, OverrideVariants)
{
    // Create initial config with variants
    std::filesystem::create_directory("config_v1");
    std::filesystem::create_directory("config_v1/models");
    std::ofstream outfile1("config_v1/models/model.json");
    outfile1 << R"({
        "models": [
            {
                "model": "local-model",
                "provider": "llama",
                "variants": [
                    {
                        "quantization": "Q4_K_M",
                        "file_size_mb": 4000
                    }
                ]
            }
        ]
    })";
    outfile1.close();

    // Create override config with different variants
    std::filesystem::create_directory("override_v");
    std::ofstream outfile2("override_v/model_override.json");
    outfile2 << R"({
        "models": [
            {
                "model": "local-model",
                "provider": "llama",
                "variants": [
                    {
                        "quantization": "Q8_0",
                        "file_size_mb": 8000
                    },
                    {
                        "quantization": "F16",
                        "file_size_mb": 14000
                    }
                ]
            }
        ]
    })";
    outfile2.close();

    ModelManager &mm = ModelManager::instance();
    mm.initialize({"config_v1"}, "override_v");

    auto model = mm.getModelInfo("local-model");
    ASSERT_TRUE(model.has_value());
    // Override should replace variants entirely
    ASSERT_EQ(model->variants.size(), 2u);
    EXPECT_EQ(model->variants[0].quantization, "Q8_0");
    EXPECT_EQ(model->variants[1].quantization, "F16");

    std::filesystem::remove_all("config_v1");
    std::filesystem::remove_all("override_v");
}

// ========== Runtime Config Injection Tests ==========

class ModelManagerConfigInjectionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ModelManager::reset();

        std::filesystem::create_directory("config_inject");
        std::filesystem::create_directory("config_inject/models");
        std::ofstream outfile("config_inject/models/base.json");
        outfile<<R"({
            "models": [
                {
                    "model": "existing-model",
                    "provider": "mock",
                    "ranking": 50,
                    "max_tokens": 1024
                }
            ]
        })";
        outfile.close();

        ModelManager::instance().initialize({"config_inject"});
    }

    void TearDown() override
    {
        std::filesystem::remove_all("config_inject");
        std::filesystem::remove_all("test_overrides");
    }
};

TEST_F(ModelManagerConfigInjectionTest, AddModelFromJson_ValidModel)
{
    nlohmann::json modelJson={
        {"model", "new-model"},
        {"provider", "mock"},
        {"ranking", 80},
        {"max_tokens", 4096}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().addModelFromJson(modelJson, error))<<error;

    auto info=ModelManager::instance().getModelInfo("new-model");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->provider, "mock");
    EXPECT_EQ(info->ranking, 80);
    EXPECT_EQ(info->maxTokens, 4096);

    auto provider=ModelManager::instance().getProvider("new-model");
    ASSERT_TRUE(provider.has_value());
    EXPECT_EQ(provider.value(), "mock");
}

TEST_F(ModelManagerConfigInjectionTest, AddModelFromJson_InvalidSchema)
{
    // Missing required 'provider' field
    nlohmann::json modelJson={
        {"model", "bad-model"}
    };

    std::string error;
    EXPECT_FALSE(ModelManager::instance().addModelFromJson(modelJson, error));
    EXPECT_FALSE(error.empty());
}

TEST_F(ModelManagerConfigInjectionTest, AddModelFromJson_DuplicateModel)
{
    nlohmann::json modelJson={
        {"model", "existing-model"},
        {"provider", "mock"}
    };

    std::string error;
    EXPECT_FALSE(ModelManager::instance().addModelFromJson(modelJson, error));
    EXPECT_NE(error.find("already exists"), std::string::npos);
}

TEST_F(ModelManagerConfigInjectionTest, UpdateModelFromJson_NewModel)
{
    nlohmann::json modelJson={
        {"model", "brand-new"},
        {"provider", "mock"},
        {"ranking", 70}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().updateModelFromJson(modelJson, error))<<error;

    auto info=ModelManager::instance().getModelInfo("brand-new");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->ranking, 70);
}

TEST_F(ModelManagerConfigInjectionTest, UpdateModelFromJson_ExistingModel)
{
    nlohmann::json modelJson={
        {"model", "existing-model"},
        {"provider", "mock"},
        {"ranking", 95},
        {"max_tokens", 8192}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().updateModelFromJson(modelJson, error))<<error;

    auto info=ModelManager::instance().getModelInfo("existing-model");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->ranking, 95);
    EXPECT_EQ(info->maxTokens, 8192);
}

TEST_F(ModelManagerConfigInjectionTest, UpdateModelFromJson_ProviderChange)
{
    nlohmann::json modelJson={
        {"model", "existing-model"},
        {"provider", "openai"},
        {"ranking", 50}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().updateModelFromJson(modelJson, error))<<error;

    auto provider=ModelManager::instance().getProvider("existing-model");
    ASSERT_TRUE(provider.has_value());
    EXPECT_EQ(provider.value(), "openai");
}

TEST_F(ModelManagerConfigInjectionTest, RemoveModel_Existing)
{
    ASSERT_TRUE(ModelManager::instance().removeModel("existing-model"));

    auto info=ModelManager::instance().getModelInfo("existing-model");
    EXPECT_FALSE(info.has_value());

    auto provider=ModelManager::instance().getProvider("existing-model");
    EXPECT_FALSE(provider.has_value());
}

TEST_F(ModelManagerConfigInjectionTest, RemoveModel_NonExistent)
{
    EXPECT_FALSE(ModelManager::instance().removeModel("no-such-model"));
}

TEST_F(ModelManagerConfigInjectionTest, ModelInfoToJson_RoundTrip)
{
    nlohmann::json modelJson={
        {"model", "roundtrip-model"},
        {"provider", "mock"},
        {"ranking", 75},
        {"context_window", 8192},
        {"max_tokens", 4096},
        {"max_input_tokens", 6144},
        {"max_output_tokens", 2048}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().addModelFromJson(modelJson, error))<<error;

    auto info=ModelManager::instance().getModelInfo("roundtrip-model");
    ASSERT_TRUE(info.has_value());

    nlohmann::json serialized=ModelManager::modelInfoToJson(info.value());
    EXPECT_EQ(serialized["model"], "roundtrip-model");
    EXPECT_EQ(serialized["provider"], "mock");
    EXPECT_EQ(serialized["ranking"], 75);
    EXPECT_EQ(serialized["context_window"], 8192);
    EXPECT_EQ(serialized["max_tokens"], 4096);
    EXPECT_EQ(serialized["max_input_tokens"], 6144);
    EXPECT_EQ(serialized["max_output_tokens"], 2048);
}

TEST_F(ModelManagerConfigInjectionTest, ModelInfoToJson_WithVariants)
{
    nlohmann::json modelJson={
        {"model", "variant-model"},
        {"provider", "llama"},
        {"variants", nlohmann::json::array({
            {
                {"quantization", "Q4_K_M"},
                {"file_size_mb", 4370},
                {"min_vram_mb", 4096},
                {"recommended_vram_mb", 8192},
                {"download", {
                    {"url", "https://example.com/model.gguf"},
                    {"sha256", "abc123def456abc123def456abc123def456abc123def456abc123def456abcd"},
                    {"filename", "model.gguf"}
                }}
            }
        })},
        {"hardware_requirements", {
            {"min_system_ram_mb", 8192},
            {"parameter_count", "7B"}
        }},
        {"context_scaling", {
            {"base_context", 4096},
            {"max_context", 131072},
            {"vram_per_1k_context_mb", 64}
        }}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().addModelFromJson(modelJson, error))<<error;

    auto info=ModelManager::instance().getModelInfo("variant-model");
    ASSERT_TRUE(info.has_value());

    nlohmann::json serialized=ModelManager::modelInfoToJson(info.value());
    ASSERT_TRUE(serialized.contains("variants"));
    ASSERT_EQ(serialized["variants"].size(), 1u);
    EXPECT_EQ(serialized["variants"][0]["quantization"], "Q4_K_M");
    EXPECT_EQ(serialized["variants"][0]["file_size_mb"], 4370);
    EXPECT_EQ(serialized["variants"][0]["download"]["url"], "https://example.com/model.gguf");

    ASSERT_TRUE(serialized.contains("hardware_requirements"));
    EXPECT_EQ(serialized["hardware_requirements"]["min_system_ram_mb"], 8192);
    EXPECT_EQ(serialized["hardware_requirements"]["parameter_count"], "7B");

    ASSERT_TRUE(serialized.contains("context_scaling"));
    EXPECT_EQ(serialized["context_scaling"]["max_context"], 131072);
}

TEST_F(ModelManagerConfigInjectionTest, SaveOverrides_WritesFile)
{
    nlohmann::json modelJson={
        {"model", "runtime-model"},
        {"provider", "mock"},
        {"ranking", 60}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().addModelFromJson(modelJson, error))<<error;

    std::filesystem::path overridePath="test_overrides/runtime.json";
    std::filesystem::create_directories("test_overrides");

    ASSERT_TRUE(ModelManager::instance().saveOverrides(overridePath));
    ASSERT_TRUE(std::filesystem::exists(overridePath));

    std::ifstream file(overridePath);
    nlohmann::json saved=nlohmann::json::parse(file);

    EXPECT_EQ(saved["schema_version"], "1.1.0");
    ASSERT_TRUE(saved.contains("models"));
    ASSERT_EQ(saved["models"].size(), 1u);
    EXPECT_EQ(saved["models"][0]["model"], "runtime-model");
    EXPECT_EQ(saved["models"][0]["provider"], "mock");
}

TEST_F(ModelManagerConfigInjectionTest, SaveOverrides_OnlyRuntimeModels)
{
    // "existing-model" was loaded from config, not runtime
    nlohmann::json modelJson={
        {"model", "injected-model"},
        {"provider", "mock"},
        {"ranking", 40}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().addModelFromJson(modelJson, error))<<error;

    std::filesystem::path overridePath="test_overrides/runtime.json";
    std::filesystem::create_directories("test_overrides");

    ASSERT_TRUE(ModelManager::instance().saveOverrides(overridePath));

    std::ifstream file(overridePath);
    nlohmann::json saved=nlohmann::json::parse(file);

    // Should only contain the injected model, not the existing one
    ASSERT_EQ(saved["models"].size(), 1u);
    EXPECT_EQ(saved["models"][0]["model"], "injected-model");
}

TEST_F(ModelManagerConfigInjectionTest, ValidateModelJson_MinimalFields)
{
    nlohmann::json modelJson={
        {"model", "minimal"},
        {"provider", "mock"}
    };

    std::string error;
    EXPECT_TRUE(ModelManager::instance().addModelFromJson(modelJson, error))<<error;
}

TEST_F(ModelManagerConfigInjectionTest, AddModelFromJson_AllOptionalFields)
{
    nlohmann::json modelJson={
        {"model", "full-model"},
        {"provider", "openai"},
        {"ranking", 90},
        {"mode", "chat"},
        {"api_base", "https://custom.api.com/v1"},
        {"context_window", 128000},
        {"max_tokens", 16384},
        {"max_input_tokens", 120000},
        {"max_output_tokens", 16384},
        {"pricing", {
            {"prompt_token_cost", 0.00015},
            {"completion_token_cost", 0.0006}
        }}
    };

    std::string error;
    ASSERT_TRUE(ModelManager::instance().addModelFromJson(modelJson, error))<<error;

    auto info=ModelManager::instance().getModelInfo("full-model");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->mode, "chat");
    EXPECT_TRUE(info->apiBase.has_value());
    EXPECT_EQ(info->apiBase.value(), "https://custom.api.com/v1");
    EXPECT_EQ(info->contextWindow, 128000);
    EXPECT_DOUBLE_EQ(info->pricing.prompt_token_cost, 0.00015);
    EXPECT_DOUBLE_EQ(info->pricing.completion_token_cost, 0.0006);
}

TEST_F(ModelManagerTest, ParseSplitGGUFVariant)
{
    std::filesystem::create_directory("config_split");
    std::filesystem::create_directory("config_split/models");
    std::ofstream outfile("config_split/models/split_model.json");
    outfile << R"({
        "models": [
            {
                "model": "big-model-120b",
                "provider": "llama",
                "variants": [
                    {
                        "quantization": "Q4_K_M",
                        "file_size_mb": 70000,
                        "min_vram_mb": 40000,
                        "recommended_vram_mb": 48000,
                        "download": {
                            "url": "https://example.com/big-model-Q4_K_M-00001-of-00002.gguf",
                            "sha256": "aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa1",
                            "filename": "big-model-Q4_K_M-00001-of-00002.gguf"
                        },
                        "files": [
                            {
                                "url": "https://example.com/big-model-Q4_K_M-00001-of-00002.gguf",
                                "sha256": "aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa1",
                                "filename": "big-model-Q4_K_M-00001-of-00002.gguf"
                            },
                            {
                                "url": "https://example.com/big-model-Q4_K_M-00002-of-00002.gguf",
                                "sha256": "bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb2",
                                "filename": "big-model-Q4_K_M-00002-of-00002.gguf"
                            }
                        ]
                    }
                ]
            }
        ]
    })";
    outfile.close();

    ModelManager &mm=ModelManager::instance();
    mm.initialize({"config_split"});

    auto model=mm.getModelInfo("big-model-120b");
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->variants.size(), 1u);

    const ModelVariant &v=model->variants[0];
    EXPECT_EQ(v.quantization, "Q4_K_M");
    EXPECT_EQ(v.fileSizeMb, 70000);

    // Single-file download field (backward compat)
    EXPECT_EQ(v.download.url, "https://example.com/big-model-Q4_K_M-00001-of-00002.gguf");
    EXPECT_EQ(v.download.filename, "big-model-Q4_K_M-00001-of-00002.gguf");

    // Multi-file files array
    ASSERT_EQ(v.files.size(), 2u);
    EXPECT_EQ(v.files[0].filename, "big-model-Q4_K_M-00001-of-00002.gguf");
    EXPECT_EQ(v.files[1].filename, "big-model-Q4_K_M-00002-of-00002.gguf");
    EXPECT_EQ(v.files[1].sha256, "bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb2");

    // Helper methods
    EXPECT_TRUE(v.isSplit());
    EXPECT_EQ(v.getPrimaryFilename(), "big-model-Q4_K_M-00001-of-00002.gguf");

    std::vector<VariantDownload> allFiles=v.getAllFiles();
    ASSERT_EQ(allFiles.size(), 2u);
    EXPECT_EQ(allFiles[0].filename, "big-model-Q4_K_M-00001-of-00002.gguf");
    EXPECT_EQ(allFiles[1].filename, "big-model-Q4_K_M-00002-of-00002.gguf");

    std::filesystem::remove_all("config_split");
}

TEST_F(ModelManagerTest, SingleFileVariantBackwardCompat)
{
    // Single-file variant without "files" array should still work
    ModelManager &mm=ModelManager::instance();
    mm.initialize({"config1"});

    auto model=mm.getModelInfo("model1");
    ASSERT_TRUE(model.has_value());

    // Build a single-file variant via JSON injection
    nlohmann::json modelJson={
        {"model", "single-file-model"},
        {"provider", "llama"},
        {"variants", {{
            {"quantization", "Q8_0"},
            {"file_size_mb", 8100},
            {"min_vram_mb", 8192},
            {"download", {
                {"url", "https://example.com/single.gguf"},
                {"sha256", "abc123def456abc123def456abc123def456abc123def456abc123def456abcd"},
                {"filename", "single.gguf"}
            }}
        }}}
    };

    std::string error;
    ASSERT_TRUE(mm.addModelFromJson(modelJson, error))<<error;

    auto info=mm.getModelInfo("single-file-model");
    ASSERT_TRUE(info.has_value());
    ASSERT_EQ(info->variants.size(), 1u);

    const ModelVariant &v=info->variants[0];

    // files array should be empty
    EXPECT_TRUE(v.files.empty());
    EXPECT_FALSE(v.isSplit());

    // getAllFiles() should fall back to the single download field
    std::vector<VariantDownload> allFiles=v.getAllFiles();
    ASSERT_EQ(allFiles.size(), 1u);
    EXPECT_EQ(allFiles[0].filename, "single.gguf");
    EXPECT_EQ(allFiles[0].url, "https://example.com/single.gguf");

    // getPrimaryFilename() should return the single download filename
    EXPECT_EQ(v.getPrimaryFilename(), "single.gguf");
}

TEST_F(ModelManagerTest, SplitVariantSerializationRoundTrip)
{
    nlohmann::json modelJson={
        {"model", "roundtrip-split"},
        {"provider", "llama"},
        {"variants", {{
            {"quantization", "Q4_K_M"},
            {"file_size_mb", 70000},
            {"min_vram_mb", 40000},
            {"recommended_vram_mb", 48000},
            {"download", {
                {"url", "https://example.com/shard-00001-of-00003.gguf"},
                {"sha256", "aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa1"},
                {"filename", "shard-00001-of-00003.gguf"}
            }},
            {"files", {
                {{"url", "https://example.com/shard-00001-of-00003.gguf"},
                 {"sha256", "aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa111aaa1"},
                 {"filename", "shard-00001-of-00003.gguf"}},
                {{"url", "https://example.com/shard-00002-of-00003.gguf"},
                 {"sha256", "bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb222bbb2"},
                 {"filename", "shard-00002-of-00003.gguf"}},
                {{"url", "https://example.com/shard-00003-of-00003.gguf"},
                 {"sha256", "ccc333ccc333ccc333ccc333ccc333ccc333ccc333ccc333ccc333ccc333ccc3"},
                 {"filename", "shard-00003-of-00003.gguf"}}
            }}
        }}}
    };

    ModelManager &mm=ModelManager::instance();
    mm.initialize({"config1"});

    std::string error;
    ASSERT_TRUE(mm.addModelFromJson(modelJson, error))<<error;

    auto info=mm.getModelInfo("roundtrip-split");
    ASSERT_TRUE(info.has_value());

    // Serialize back to JSON
    nlohmann::json outputJson=ModelManager::modelInfoToJson(info.value());

    // Verify files array is present in output
    ASSERT_TRUE(outputJson.contains("variants"));
    ASSERT_EQ(outputJson["variants"].size(), 1u);
    ASSERT_TRUE(outputJson["variants"][0].contains("files"));
    ASSERT_EQ(outputJson["variants"][0]["files"].size(), 3u);
    EXPECT_EQ(outputJson["variants"][0]["files"][2]["filename"].get<std::string>(), "shard-00003-of-00003.gguf");

    // Verify download field is also present (backward compat)
    ASSERT_TRUE(outputJson["variants"][0].contains("download"));
    EXPECT_EQ(outputJson["variants"][0]["download"]["filename"].get<std::string>(), "shard-00001-of-00003.gguf");
}

} // namespace arbiterAI