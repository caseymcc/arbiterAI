/**
 * @file sherpaOnnxProviderTests.cpp
 * @brief Unit tests for the sherpa-onnx speech-to-text provider.
 *
 * Only compiled when ARBITERAI_ENABLE_SHERPA is on. The end-to-end test skips
 * unless SHERPA_MODEL_DIR points at a directory holding a real sherpa model +
 * tokens (via a sherpa_options config), so the suite stays green without models.
 */

#include "arbiterAI/providers/sherpaOnnx.h"
#include "arbiterAI/modelManager.h"

#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>

namespace arbiterAI
{

class SherpaOnnxProviderTest : public ::testing::Test
{
protected:
    SherpaOnnx provider;
    ModelInfo model;

    void SetUp() override
    {
        model.model="sherpa-stt";
        model.provider="sherpa-onnx";
        model.mode=modes::Transcription;
    }
};

// sherpa-onnx here is STT-only: text methods must report NotImplemented.
TEST_F(SherpaOnnxProviderTest, TextMethodsNotImplemented)
{
    CompletionRequest creq;
    CompletionResponse cresp;
    EXPECT_EQ(provider.completion(creq, model, cresp), ErrorCode::NotImplemented);

    EmbeddingRequest ereq;
    EmbeddingResponse eresp;
    EXPECT_EQ(provider.getEmbeddings(ereq, eresp), ErrorCode::NotImplemented);
}

// A valid 16 kHz mono WAV with no sherpa_options → decode ok, then ModelLoadError
// (no recognizer can be built).
TEST_F(SherpaOnnxProviderTest, MissingConfigReportsModelLoadError)
{
    // 0.1s of silence, 16 kHz mono 16-bit
    std::vector<uint8_t> wav;
    auto put32=[&](uint32_t v){ for(int i=0;i<4;++i) wav.push_back(uint8_t((v>>(8*i))&0xFF)); };
    auto put16=[&](uint16_t v){ for(int i=0;i<2;++i) wav.push_back(uint8_t((v>>(8*i))&0xFF)); };
    auto put=[&](const char*s){ wav.insert(wav.end(), s, s+4); };
    const uint32_t n=1600, dataBytes=n*2;
    put("RIFF"); put32(36+dataBytes); put("WAVE");
    put("fmt "); put32(16); put16(1); put16(1); put32(16000); put32(32000); put16(2); put16(16);
    put("data"); put32(dataBytes);
    for(uint32_t i=0;i<n;++i) put16(0);

    AudioTranscriptionRequest req;
    req.model=model.model;
    req.audio=wav;

    AudioTranscriptionResponse resp;
    EXPECT_EQ(provider.transcribe(req, model, resp), ErrorCode::ModelLoadError);
}

// Non-WAV bytes are rejected before touching the model.
TEST_F(SherpaOnnxProviderTest, RejectsNonWav)
{
    AudioTranscriptionRequest req;
    req.model=model.model;
    req.audio={'n','o','p','e'};

    AudioTranscriptionResponse resp;
    EXPECT_EQ(provider.transcribe(req, model, resp), ErrorCode::InvalidRequest);
}

} // namespace arbiterAI
