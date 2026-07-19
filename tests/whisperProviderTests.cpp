/**
 * @file whisperProviderTests.cpp
 * @brief Unit tests for the local whisper.cpp speech-to-text provider.
 *
 * Only compiled when ARBITERAI_ENABLE_WHISPER is on. Tests that need a real
 * model file skip when WHISPER_MODEL_PATH is unset or the file is absent, so
 * the suite stays green in environments without whisper weights.
 */

#include "arbiterAI/providers/whisper.h"
#include "arbiterAI/modelManager.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

namespace arbiterAI
{

namespace
{

/// Build a minimal 16 kHz mono 16-bit PCM WAV containing `sampleCount` samples.
std::vector<uint8_t> makeWav(uint32_t sampleCount)
{
    const uint32_t sampleRate=16000;
    const uint16_t channels=1;
    const uint16_t bits=16;
    const uint32_t dataBytes=sampleCount*2;
    const uint32_t byteRate=sampleRate*channels*bits/8;

    std::vector<uint8_t> b;
    auto put32=[&](uint32_t v){ for(int i=0;i<4;++i) b.push_back(static_cast<uint8_t>((v>>(8*i))&0xFF)); };
    auto put16=[&](uint16_t v){ for(int i=0;i<2;++i) b.push_back(static_cast<uint8_t>((v>>(8*i))&0xFF)); };
    auto putStr=[&](const char *s){ b.insert(b.end(), s, s+4); };

    putStr("RIFF"); put32(36+dataBytes); putStr("WAVE");
    putStr("fmt "); put32(16); put16(1); put16(channels);
    put32(sampleRate); put32(byteRate); put16(channels*bits/8); put16(bits);
    putStr("data"); put32(dataBytes);
    for(uint32_t i=0;i<sampleCount;++i) put16(0); // silence

    return b;
}

} // namespace

class WhisperProviderTest : public ::testing::Test
{
protected:
    Whisper provider;
    ModelInfo model;

    void SetUp() override
    {
        model.model="whisper-local";
        model.provider="whisper";
        model.mode=modes::Transcription;
        const char *path=std::getenv("WHISPER_MODEL_PATH");
        if(path)
            model.filePath=std::string(path);
    }
};

// whisper is STT-only: text methods must report NotImplemented.
TEST_F(WhisperProviderTest, TextMethodsNotImplemented)
{
    CompletionRequest creq;
    CompletionResponse cresp;
    EXPECT_EQ(provider.completion(creq, model, cresp), ErrorCode::NotImplemented);

    EmbeddingRequest ereq;
    EmbeddingResponse eresp;
    EXPECT_EQ(provider.getEmbeddings(ereq, eresp), ErrorCode::NotImplemented);
}

// No model file configured → ModelNotFound (before any audio decoding).
TEST_F(WhisperProviderTest, MissingModelPathReported)
{
    ModelInfo noPath;
    noPath.model="whisper-nopath";
    noPath.provider="whisper";
    noPath.mode=modes::Transcription;

    AudioTranscriptionRequest req;
    req.model="whisper-nopath";
    req.audio=makeWav(1600);

    AudioTranscriptionResponse resp;
    EXPECT_EQ(provider.transcribe(req, noPath, resp), ErrorCode::ModelNotFound);
}

// Non-WAV bytes are rejected as InvalidRequest (requires a configured model
// so we reach the decode step).
TEST_F(WhisperProviderTest, RejectsNonWavAudio)
{
    if(!model.filePath.has_value() || !std::filesystem::exists(model.filePath.value()))
        GTEST_SKIP() << "WHISPER_MODEL_PATH not set or file missing";

    AudioTranscriptionRequest req;
    req.model=model.model;
    req.audio={'n','o','t','a','w','a','v'};

    AudioTranscriptionResponse resp;
    EXPECT_EQ(provider.transcribe(req, model, resp), ErrorCode::InvalidRequest);
}

// End-to-end transcription of silence with a real model.
TEST_F(WhisperProviderTest, TranscribeSilence)
{
    if(!model.filePath.has_value() || !std::filesystem::exists(model.filePath.value()))
        GTEST_SKIP() << "WHISPER_MODEL_PATH not set or file missing";

    AudioTranscriptionRequest req;
    req.model=model.model;
    req.audio=makeWav(16000); // 1 second of silence

    AudioTranscriptionResponse resp;
    EXPECT_EQ(provider.transcribe(req, model, resp), ErrorCode::Success);
    EXPECT_EQ(resp.provider, "whisper");
    EXPECT_NEAR(resp.duration, 1.0, 0.01);
}

} // namespace arbiterAI
