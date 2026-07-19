/**
 * @file stableDiffusionProviderTests.cpp
 * @brief Unit tests for the local stable-diffusion.cpp image provider.
 *
 * Only compiled when ARBITERAI_ENABLE_STABLE_DIFFUSION is on. The end-to-end
 * generation test skips unless SD_MODEL_PATH points at a real model file (SD
 * weights + inference are heavy), so the suite stays green without weights.
 */

#include "arbiterAI/providers/stableDiffusion.h"
#include "arbiterAI/modelManager.h"

#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>

namespace arbiterAI
{

class StableDiffusionProviderTest : public ::testing::Test
{
protected:
    StableDiffusion provider;
    ModelInfo model;

    void SetUp() override
    {
        model.model="sd-local";
        model.provider="stable-diffusion";
        model.mode=modes::Image;
        const char *path=std::getenv("SD_MODEL_PATH");
        if(path)
            model.filePath=std::string(path);
    }
};

// stable-diffusion is image-only: text methods must report NotImplemented.
TEST_F(StableDiffusionProviderTest, TextMethodsNotImplemented)
{
    CompletionRequest creq;
    CompletionResponse cresp;
    EXPECT_EQ(provider.completion(creq, model, cresp), ErrorCode::NotImplemented);

    EmbeddingRequest ereq;
    EmbeddingResponse eresp;
    EXPECT_EQ(provider.getEmbeddings(ereq, eresp), ErrorCode::NotImplemented);
}

// No model file configured → ModelNotFound before any generation work.
TEST_F(StableDiffusionProviderTest, MissingModelPathReported)
{
    ModelInfo noPath;
    noPath.model="sd-nopath";
    noPath.provider="stable-diffusion";
    noPath.mode=modes::Image;

    ImageGenerationRequest req;
    req.model="sd-nopath";
    req.prompt="a red cube";

    ImageGenerationResponse resp;
    EXPECT_EQ(provider.generateImage(req, noPath, resp), ErrorCode::ModelNotFound);
}

// End-to-end generation with a real model.
TEST_F(StableDiffusionProviderTest, GenerateProducesPng)
{
    if(!model.filePath.has_value() || !std::filesystem::exists(model.filePath.value()))
        GTEST_SKIP() << "SD_MODEL_PATH not set or file missing";

    ImageGenerationRequest req;
    req.model=model.model;
    req.prompt="a red cube on a white background";
    req.size="256x256";
    req.steps=4;

    ImageGenerationResponse resp;
    ErrorCode err=provider.generateImage(req, model, resp);
    EXPECT_EQ(err, ErrorCode::Success);
    ASSERT_FALSE(resp.images.empty());
    // base64 PNG starts with iVBORw0KGgo (the PNG signature encoded).
    EXPECT_EQ(resp.images[0].b64Json.rfind("iVBORw0KGgo", 0), 0u);
    EXPECT_EQ(resp.provider, "stable-diffusion");
}

} // namespace arbiterAI
