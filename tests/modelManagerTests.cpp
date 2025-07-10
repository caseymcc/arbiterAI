#include "hermesaxiom/modelManager.h"
#include <gtest/gtest.h>
#include <vector>

namespace hermesaxiom
{

class ModelManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Reset the singleton instance before each test
        ModelManager::reset();
    }
};

TEST_F(ModelManagerTest, ModelRankingSortsCorrectly)
{
    ModelManager &manager=ModelManager::instance();

    // Add models out of order
    manager.addModel({ "modelD", "provider2", "chat", "1.1.0", "1.0.0", 25 });
    manager.addModel({ "modelA", "provider1", "chat", "1.1.0", "1.0.0", 50 });
    manager.addModel({ "modelC", "provider2", "chat", "1.1.0", "1.0.0", 50 });
    manager.addModel({ "modelB", "provider1", "chat", "1.1.0", "1.0.0", 75 });

    auto sorted=manager.getModelsByRanking();

    // Verify ranking order (highest first)
    ASSERT_EQ(sorted.size(), 4);
    EXPECT_EQ(sorted[0].model, "modelB");
    EXPECT_EQ(sorted[1].model, "modelA");
    EXPECT_EQ(sorted[2].model, "modelC");
    EXPECT_EQ(sorted[3].model, "modelD");

    // Verify case-insensitive alphabetical order for same ranking
    manager.addModel({ "aModel", "provider1", "chat", "1.1.0", "1.0.0", 50 });

    sorted=manager.getModelsByRanking();
    ASSERT_EQ(sorted.size(), 5);

    // Find the models with ranking 50
    std::vector<std::string> rank50Models;
    for(const auto &m:sorted)
    {
        if(m.ranking==50)
        {
            rank50Models.push_back(m.model);
        }
    }

    // Expect ["aModel", "modelA", "modelC"]
    ASSERT_EQ(rank50Models.size(), 3);
    EXPECT_EQ(rank50Models[0], "aModel");
    EXPECT_EQ(rank50Models[1], "modelA");
    EXPECT_EQ(rank50Models[2], "modelC");
}

TEST_F(ModelManagerTest, SchemaVersionCompatibility)
{
    ModelManager &manager=ModelManager::instance();

    ModelInfo v1_0_0;
    v1_0_0.model="oldModel";
    v1_0_0.provider="provider1";
    v1_0_0.ranking=50;
    v1_0_0.minSchemaVersion="1.0.0";
    v1_0_0.configVersion="1.0.0";

    ModelInfo v1_1_0;
    v1_1_0.model="newModel";
    v1_1_0.provider="provider1";
    v1_1_0.ranking=60;
    v1_1_0.minSchemaVersion="1.1.0";
    v1_1_0.configVersion="1.1.0";

    manager.addModel(v1_0_0);
    manager.addModel(v1_1_0);

    auto sorted=manager.getModelsByRanking();
    ASSERT_EQ(sorted.size(), 2);
    EXPECT_EQ(sorted[0].model, "newModel");
    EXPECT_EQ(sorted[1].model, "oldModel");

    EXPECT_TRUE(v1_0_0.isSchemaCompatible("1.0.0"));
    EXPECT_TRUE(v1_0_0.isSchemaCompatible("1.1.0"));
    EXPECT_FALSE(v1_1_0.isSchemaCompatible("1.0.0"));
    EXPECT_TRUE(v1_1_0.isSchemaCompatible("1.1.0"));
    EXPECT_TRUE(v1_1_0.isSchemaCompatible("1.2.0"));
}

} // namespace hermesaxiom