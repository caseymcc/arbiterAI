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

} // namespace arbiterAI