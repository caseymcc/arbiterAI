#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/telemetryCollector.h"
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
    ArbiterAI ai;
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

// --- Multimodal dispatch + modality-aware pricing ---

class ArbiterAIMultimodalTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ModelManager::reset();
        m_wasInitialized=ArbiterAI::instance().initialized;
        ArbiterAI::instance().initialized=true;
    }

    void TearDown() override
    {
        ArbiterAI::instance().initialized=m_wasInitialized;
    }

    bool m_wasInitialized=false;
};

TEST_F(ArbiterAIMultimodalTest, GenerateImageComputesCost)
{
    ModelInfo model;
    model.model="mock-image";
    model.provider="mock";
    model.mode=modes::Image;
    model.pricing.image_cost=0.04;
    ModelManager::instance().addModel(model);

    ImageGenerationRequest request;
    request.model="mock-image";
    request.prompt="a blue sphere";
    request.n=3;

    ImageGenerationResponse response;
    ErrorCode err=ArbiterAI::instance().generateImage(request, response);

    EXPECT_EQ(err, ErrorCode::Success);
    ASSERT_EQ(response.images.size(), 3u);
    EXPECT_DOUBLE_EQ(response.cost, 3*0.04);
}

TEST_F(ArbiterAIMultimodalTest, SynthesizeSpeechComputesCost)
{
    ModelInfo model;
    model.model="mock-speech";
    model.provider="mock";
    model.mode=modes::Speech;
    model.pricing.audio_output_cost_per_character=0.001;
    ModelManager::instance().addModel(model);

    SpeechRequest request;
    request.model="mock-speech";
    request.input="hello"; // 5 characters

    SpeechResponse response;
    ErrorCode err=ArbiterAI::instance().synthesizeSpeech(request, response);

    EXPECT_EQ(err, ErrorCode::Success);
    EXPECT_DOUBLE_EQ(response.cost, 5*0.001);
}

TEST_F(ArbiterAIMultimodalTest, TranscribeSucceeds)
{
    ModelInfo model;
    model.model="mock-transcribe";
    model.provider="mock";
    model.mode=modes::Transcription;
    ModelManager::instance().addModel(model);

    AudioTranscriptionRequest request;
    request.model="mock-transcribe";
    request.audio={0x01, 0x02, 0x03};

    AudioTranscriptionResponse response;
    ErrorCode err=ArbiterAI::instance().transcribe(request, response);

    EXPECT_EQ(err, ErrorCode::Success);
    EXPECT_FALSE(response.text.empty());
}

TEST_F(ArbiterAIMultimodalTest, UnknownModelRejected)
{
    ImageGenerationRequest request;
    request.model="does-not-exist";
    request.prompt="x";

    ImageGenerationResponse response;
    EXPECT_EQ(ArbiterAI::instance().generateImage(request, response), ErrorCode::UnknownModel);
}

TEST_F(ArbiterAIMultimodalTest, DispatchRecordsModalityTelemetry)
{
    TelemetryCollector::reset();

    ModelInfo model;
    model.model="mock-image-tel";
    model.provider="mock";
    model.mode=modes::Image;
    ModelManager::instance().addModel(model);

    ImageGenerationRequest request;
    request.model="mock-image-tel";
    request.prompt="a green triangle";
    request.n=2;

    ImageGenerationResponse response;
    ASSERT_EQ(ArbiterAI::instance().generateImage(request, response), ErrorCode::Success);

    SystemSnapshot snap=TelemetryCollector::instance().getSnapshot();
    EXPECT_EQ(snap.requestsByModality["image"], 1);
    EXPECT_EQ(snap.imagesGenerated, 2);
}

} // namespace arbiterAI