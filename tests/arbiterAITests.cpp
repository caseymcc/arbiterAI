#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/modelManager.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace arbiterAI
{

class ArbiterAITest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ModelManager::reset();
    }
};

//TEST_F(ArbiterAITest, GetDownloadStatus)
//{
//    arbiterAI ai;
//    ai.initialize({});
//    std::string error;
//
//    // Initialize ModelManager so the model can be found.
//    ModelManager &mm = ModelManager::instance();
//    ModelInfo model;
//    model.model = "non-existent-model";
//    model.provider = "llama";
//    mm.addModel(model);
//
//    EXPECT_EQ(ai.getDownloadStatus("non-existent-model", error), DownloadStatus::NotStarted);
//}

TEST_F(ArbiterAITest, SupportModelDownload)
{
    arbiterAI ai;
    ModelManager &mm = ModelManager::instance();

    ModelInfo localModel;
    localModel.model = "local-model";
    localModel.provider = "llama"; // Llama provider supports downloads
    mm.addModel(localModel);

    ModelInfo remoteModel;
    remoteModel.model = "remote-model";
    remoteModel.provider = "openai"; // OpenAI provider does not support downloads
    mm.addModel(remoteModel);

    EXPECT_TRUE(ai.supportModelDownload("llama"));
    EXPECT_FALSE(ai.supportModelDownload("openai"));
    EXPECT_FALSE(ai.supportModelDownload("unknown-provider"));
}

} // namespace arbiterAI