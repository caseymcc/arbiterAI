#include "arbiterAI/modelManager.h"
#include <gtest/gtest.h>
#include <fstream>

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

} // namespace arbiterAI