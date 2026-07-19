/**
 * @file vibevoiceProviderTests.cpp
 * @brief Unit tests for the local vibevoice.cpp text-to-speech provider.
 *
 * Only compiled when ARBITERAI_ENABLE_VIBEVOICE is on. The end-to-end synthesis
 * test skips unless VIBEVOICE_MODEL_PATH points at a real TTS gguf (with a
 * sibling tokenizer.gguf / voice.gguf), so the suite stays green without models.
 */

#include "arbiterAI/providers/vibevoice.h"
#include "arbiterAI/modelManager.h"

#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>

namespace arbiterAI
{

class VibeVoiceProviderTest : public ::testing::Test
{
protected:
    VibeVoice provider;
    ModelInfo model;

    void SetUp() override
    {
        model.model="vibevoice-local";
        model.provider="vibevoice";
        model.mode=modes::Speech;
        const char *path=std::getenv("VIBEVOICE_MODEL_PATH");
        if(path)
            model.filePath=std::string(path);
    }
};

// vibevoice is TTS-only: text methods must report NotImplemented.
TEST_F(VibeVoiceProviderTest, TextMethodsNotImplemented)
{
    CompletionRequest creq;
    CompletionResponse cresp;
    EXPECT_EQ(provider.completion(creq, model, cresp), ErrorCode::NotImplemented);

    EmbeddingRequest ereq;
    EmbeddingResponse eresp;
    EXPECT_EQ(provider.getEmbeddings(ereq, eresp), ErrorCode::NotImplemented);
}

// No model file configured → ModelNotFound before any engine work.
TEST_F(VibeVoiceProviderTest, MissingModelPathReported)
{
    ModelInfo noPath;
    noPath.model="vibevoice-nopath";
    noPath.provider="vibevoice";
    noPath.mode=modes::Speech;

    SpeechRequest req;
    req.model="vibevoice-nopath";
    req.input="hello world";

    SpeechResponse resp;
    EXPECT_EQ(provider.synthesizeSpeech(req, noPath, resp), ErrorCode::ModelNotFound);
}

// End-to-end synthesis with a real model.
TEST_F(VibeVoiceProviderTest, SynthesizeProducesWav)
{
    if(!model.filePath.has_value() || !std::filesystem::exists(model.filePath.value()))
        GTEST_SKIP() << "VIBEVOICE_MODEL_PATH not set or file missing";

    SpeechRequest req;
    req.model=model.model;
    req.input="Speaker 0: Hello from arbiter A I.";

    SpeechResponse resp;
    ErrorCode err=provider.synthesizeSpeech(req, model, resp);
    EXPECT_EQ(err, ErrorCode::Success);
    ASSERT_FALSE(resp.audio.empty());
    // WAV files start with the "RIFF" magic.
    ASSERT_GE(resp.audio.size(), 4u);
    EXPECT_EQ(std::string(resp.audio.begin(), resp.audio.begin()+4), "RIFF");
    EXPECT_EQ(resp.format, "wav");
    EXPECT_EQ(resp.provider, "vibevoice");
}

} // namespace arbiterAI
