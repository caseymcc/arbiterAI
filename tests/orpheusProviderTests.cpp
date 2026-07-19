/**
 * @file orpheusProviderTests.cpp
 * @brief Unit tests for the local Orpheus TTS provider (dlopen engine).
 *
 * Only compiled when ARBITERAI_ENABLE_ORPHEUS is on. The end-to-end synthesis
 * test skips unless both ORPHEUS_MODEL_PATH and ARBITER_ORPHEUS_ENGINE (a built
 * liborpheus_engine.so) are present, so the suite stays green without them.
 */

#include "arbiterAI/providers/orpheus.h"
#include "arbiterAI/modelManager.h"

#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>

namespace arbiterAI
{

class OrpheusProviderTest : public ::testing::Test
{
protected:
    Orpheus provider;
    ModelInfo model;

    void SetUp() override
    {
        model.model="orpheus-local";
        model.provider="orpheus";
        model.mode=modes::Speech;
        const char *path=std::getenv("ORPHEUS_MODEL_PATH");
        if(path)
            model.filePath=std::string(path);
    }
};

// Orpheus is TTS-only: text methods must report NotImplemented.
TEST_F(OrpheusProviderTest, TextMethodsNotImplemented)
{
    CompletionRequest creq;
    CompletionResponse cresp;
    EXPECT_EQ(provider.completion(creq, model, cresp), ErrorCode::NotImplemented);

    EmbeddingRequest ereq;
    EmbeddingResponse eresp;
    EXPECT_EQ(provider.getEmbeddings(ereq, eresp), ErrorCode::NotImplemented);
}

// No model file configured → ModelNotFound before touching the engine.
TEST_F(OrpheusProviderTest, MissingModelPathReported)
{
    ModelInfo noPath;
    noPath.model="orpheus-nopath";
    noPath.provider="orpheus";
    noPath.mode=modes::Speech;

    SpeechRequest req;
    req.model="orpheus-nopath";
    req.input="hello world";

    SpeechResponse resp;
    EXPECT_EQ(provider.synthesizeSpeech(req, noPath, resp), ErrorCode::ModelNotFound);
}

// With a model configured but no engine library available, the provider must
// fail cleanly (ModelLoadError) rather than crash.
TEST_F(OrpheusProviderTest, MissingEngineReportedCleanly)
{
    if(!model.filePath.has_value())
        GTEST_SKIP() << "ORPHEUS_MODEL_PATH not set";
    if(std::getenv("ARBITER_ORPHEUS_ENGINE"))
        GTEST_SKIP() << "engine present — covered by SynthesizeProducesWav";

    SpeechRequest req;
    req.model=model.model;
    req.input="hello";

    SpeechResponse resp;
    EXPECT_EQ(provider.synthesizeSpeech(req, model, resp), ErrorCode::ModelLoadError);
}

// Proves the provider actually dlopen's + invokes the engine library: with the
// engine present, the result must NOT be ModelLoadError (engine-not-loaded) or
// ModelNotFound. A bogus model yields GenerationError (engine ran, chatllm
// rejected the model); a real model yields Success.
TEST_F(OrpheusProviderTest, ProviderLoadsRealEngine)
{
    const char *engine=std::getenv("ARBITER_ORPHEUS_ENGINE");
    if(!engine)
        GTEST_SKIP() << "ARBITER_ORPHEUS_ENGINE not set";

    if(!model.filePath.has_value())
        model.filePath=std::string("/nonexistent/orpheus.gguf");

    SpeechRequest req;
    req.model=model.model;
    req.input="hello world";

    SpeechResponse resp;
    ErrorCode err=provider.synthesizeSpeech(req, model, resp);
    EXPECT_NE(err, ErrorCode::ModelLoadError);   // engine .so loaded fine
    EXPECT_NE(err, ErrorCode::ModelNotFound);    // model path was provided
}

// End-to-end synthesis with a real model + engine library.
TEST_F(OrpheusProviderTest, SynthesizeProducesWav)
{
    const char *engine=std::getenv("ARBITER_ORPHEUS_ENGINE");
    if(!engine || !model.filePath.has_value() || !std::filesystem::exists(model.filePath.value()))
        GTEST_SKIP() << "ARBITER_ORPHEUS_ENGINE / ORPHEUS_MODEL_PATH not set";

    SpeechRequest req;
    req.model=model.model;
    req.input="Hello from arbiter A I.";

    SpeechResponse resp;
    ErrorCode err=provider.synthesizeSpeech(req, model, resp);
    EXPECT_EQ(err, ErrorCode::Success);
    ASSERT_GE(resp.audio.size(), 4u);
    EXPECT_EQ(std::string(resp.audio.begin(), resp.audio.begin()+4), "RIFF");
    EXPECT_EQ(resp.provider, "orpheus");
}

} // namespace arbiterAI
