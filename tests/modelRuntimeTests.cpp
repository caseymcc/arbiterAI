#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/hardwareDetector.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace arbiterAI
{

class ModelRuntimeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ModelRuntime::reset();
        ModelManager::reset();

        // Create a config directory with a local model that has variants
        std::filesystem::create_directories("rt_config/models");

        std::ofstream localModel("rt_config/models/local_model.json");
        localModel << R"({
            "models": [
                {
                    "model": "test-local-7b",
                    "provider": "llama",
                    "ranking": 50,
                    "context_window": 4096,
                    "max_tokens": 2048,
                    "hardware_requirements": {
                        "min_system_ram_mb": 4096,
                        "parameter_count": "7B"
                    },
                    "context_scaling": {
                        "base_context": 4096,
                        "max_context": 32768,
                        "vram_per_1k_context_mb": 64
                    },
                    "variants": [
                        {
                            "quantization": "Q4_K_M",
                            "file_size_mb": 4370,
                            "min_vram_mb": 128,
                            "recommended_vram_mb": 256,
                            "download": {
                                "url": "",
                                "sha256": "abc123",
                                "filename": "test-7b-q4.gguf"
                            }
                        },
                        {
                            "quantization": "Q8_0",
                            "file_size_mb": 8100,
                            "min_vram_mb": 256,
                            "recommended_vram_mb": 512,
                            "download": {
                                "url": "",
                                "sha256": "def456",
                                "filename": "test-7b-q8.gguf"
                            }
                        }
                    ]
                }
            ]
        })";
        localModel.close();

        // Create a cloud model config (no variants)
        std::ofstream cloudModel("rt_config/models/cloud_model.json");
        cloudModel << R"({
            "models": [
                {
                    "model": "mock-model",
                    "provider": "mock",
                    "ranking": 80,
                    "context_window": 8192,
                    "max_tokens": 4096
                },
                {
                    "model": "mock-model-2",
                    "provider": "mock",
                    "ranking": 70,
                    "context_window": 4096,
                    "max_tokens": 2048
                }
            ]
        })";
        cloudModel.close();

        ModelManager::instance().initialize({"rt_config"});
    }

    void TearDown() override
    {
        std::filesystem::remove_all("rt_config");
    }
};

// --- Basic load/unload ---

TEST_F(ModelRuntimeTest, LoadCloudModelSucceeds)
{
    ModelRuntime &rt=ModelRuntime::instance();

    ErrorCode result=rt.loadModel("mock-model");

    EXPECT_EQ(result, ErrorCode::Success);

    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Loaded);
    EXPECT_EQ(state->modelName, "mock-model");
}

TEST_F(ModelRuntimeTest, LoadUnknownModelFails)
{
    ModelRuntime &rt=ModelRuntime::instance();

    ErrorCode result=rt.loadModel("nonexistent-model");

    EXPECT_EQ(result, ErrorCode::ModelNotFound);
}

TEST_F(ModelRuntimeTest, LoadAlreadyLoadedReturnsSuccess)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    ErrorCode result=rt.loadModel("mock-model");

    EXPECT_EQ(result, ErrorCode::Success);
}

TEST_F(ModelRuntimeTest, UnloadModelSucceeds)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    ErrorCode result=rt.unloadModel("mock-model");

    EXPECT_EQ(result, ErrorCode::Success);

    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Unloaded);
}

TEST_F(ModelRuntimeTest, UnloadNotTrackedModelFails)
{
    ModelRuntime &rt=ModelRuntime::instance();

    ErrorCode result=rt.unloadModel("nonexistent-model");

    EXPECT_EQ(result, ErrorCode::ModelNotFound);
}

TEST_F(ModelRuntimeTest, UnloadAlreadyUnloadedSucceeds)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.unloadModel("mock-model");
    ErrorCode result=rt.unloadModel("mock-model");

    EXPECT_EQ(result, ErrorCode::Success);
}

// --- Pin/Unpin ---

TEST_F(ModelRuntimeTest, PinModelSucceeds)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    ErrorCode result=rt.pinModel("mock-model");

    EXPECT_EQ(result, ErrorCode::Success);

    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->pinned);
}

TEST_F(ModelRuntimeTest, PinNotTrackedModelFails)
{
    ModelRuntime &rt=ModelRuntime::instance();

    ErrorCode result=rt.pinModel("nonexistent-model");

    EXPECT_EQ(result, ErrorCode::ModelNotFound);
}

TEST_F(ModelRuntimeTest, UnpinModelSucceeds)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.pinModel("mock-model");
    ErrorCode result=rt.unpinModel("mock-model");

    EXPECT_EQ(result, ErrorCode::Success);

    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_FALSE(state->pinned);
}

TEST_F(ModelRuntimeTest, UnloadPinnedModelMovesToReady)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.pinModel("mock-model");
    rt.unloadModel("mock-model");

    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Ready);
}

TEST_F(ModelRuntimeTest, UnloadUnpinnedModelGoesToUnloaded)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.unloadModel("mock-model");

    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Unloaded);
}

// --- Swap ---

TEST_F(ModelRuntimeTest, SwapModelWhenIdle)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    ErrorCode result=rt.swapModel("mock-model-2");

    EXPECT_EQ(result, ErrorCode::Success);

    // Old model should be unloaded (not pinned)
    auto oldState=rt.getModelState("mock-model");
    ASSERT_TRUE(oldState.has_value());
    EXPECT_EQ(oldState->state, ModelState::Unloaded);

    // New model should be loaded
    auto newState=rt.getModelState("mock-model-2");
    ASSERT_TRUE(newState.has_value());
    EXPECT_EQ(newState->state, ModelState::Loaded);
}

TEST_F(ModelRuntimeTest, SwapPinnedModelMovesToReady)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.pinModel("mock-model");
    rt.swapModel("mock-model-2");

    auto oldState=rt.getModelState("mock-model");
    ASSERT_TRUE(oldState.has_value());
    EXPECT_EQ(oldState->state, ModelState::Ready);

    auto newState=rt.getModelState("mock-model-2");
    ASSERT_TRUE(newState.has_value());
    EXPECT_EQ(newState->state, ModelState::Loaded);
}

TEST_F(ModelRuntimeTest, SwapDuringInferenceQueues)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.beginInference("mock-model");

    ErrorCode result=rt.swapModel("mock-model-2");

    // Should return queued status
    EXPECT_EQ(result, ErrorCode::ModelDownloading);

    // mock-model should still be loaded (inference active)
    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Loaded);

    // mock-model-2 should NOT be tracked yet
    auto newState=rt.getModelState("mock-model-2");
    EXPECT_FALSE(newState.has_value());
}

TEST_F(ModelRuntimeTest, EndInferenceDrainsSwapQueue)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.beginInference("mock-model");

    // Queue a swap
    rt.swapModel("mock-model-2");

    // End inference — should drain the queue and execute swap
    rt.endInference();

    EXPECT_FALSE(rt.isInferenceActive());

    // mock-model should be unloaded now
    auto oldState=rt.getModelState("mock-model");
    ASSERT_TRUE(oldState.has_value());
    EXPECT_EQ(oldState->state, ModelState::Unloaded);

    // mock-model-2 should be loaded
    auto newState=rt.getModelState("mock-model-2");
    ASSERT_TRUE(newState.has_value());
    EXPECT_EQ(newState->state, ModelState::Loaded);
}

TEST_F(ModelRuntimeTest, MultipleQueuedSwapsOnlyExecutesLatest)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.beginInference("mock-model");

    // Queue multiple swaps — only the last should execute
    rt.swapModel("mock-model-2");
    rt.swapModel("mock-model"); // swap back to mock-model

    rt.endInference();

    // mock-model should be loaded (the latest swap target)
    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Loaded);
}

// --- Inference state ---

TEST_F(ModelRuntimeTest, BeginEndInferenceTracksState)
{
    ModelRuntime &rt=ModelRuntime::instance();

    EXPECT_FALSE(rt.isInferenceActive());

    rt.loadModel("mock-model");
    rt.beginInference("mock-model");
    EXPECT_TRUE(rt.isInferenceActive());

    rt.endInference();
    EXPECT_FALSE(rt.isInferenceActive());
}

// --- GetModelStates ---

TEST_F(ModelRuntimeTest, GetModelStatesReturnsAll)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.loadModel("mock-model-2");

    auto states=rt.getModelStates();

    EXPECT_EQ(states.size(), 2u);
}

TEST_F(ModelRuntimeTest, GetModelStateEmpty)
{
    ModelRuntime &rt=ModelRuntime::instance();

    auto state=rt.getModelState("nonexistent");

    EXPECT_FALSE(state.has_value());
}

// --- Ready RAM budget ---

TEST_F(ModelRuntimeTest, SetReadyRamBudget)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.setReadyRamBudget(2048);
    EXPECT_EQ(rt.getReadyRamBudget(), 2048);
}

TEST_F(ModelRuntimeTest, DefaultReadyRamBudgetIsHalfSystemRam)
{
    ModelRuntime &rt=ModelRuntime::instance();

    SystemInfo hw=HardwareDetector::instance().getSystemInfo();
    int expected=hw.totalRamMb/2;

    EXPECT_EQ(rt.getReadyRamBudget(), expected);
}

// --- Promote from Ready to Loaded ---

TEST_F(ModelRuntimeTest, LoadReadyModelPromotesToLoaded)
{
    ModelRuntime &rt=ModelRuntime::instance();

    // Load, pin, then unload to put in Ready state
    rt.loadModel("mock-model");
    rt.pinModel("mock-model");
    rt.unloadModel("mock-model");

    auto state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Ready);

    // Loading again should promote to Loaded
    ErrorCode result=rt.loadModel("mock-model");
    EXPECT_EQ(result, ErrorCode::Success);

    state=rt.getModelState("mock-model");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->state, ModelState::Loaded);
}

// --- Reset ---

TEST_F(ModelRuntimeTest, ResetClearsAll)
{
    ModelRuntime &rt=ModelRuntime::instance();

    rt.loadModel("mock-model");
    rt.loadModel("mock-model-2");
    rt.beginInference("mock-model");

    ModelRuntime::reset();

    EXPECT_FALSE(rt.isInferenceActive());
    EXPECT_TRUE(rt.getModelStates().empty());
}

// --- Local model with variants ---

TEST_F(ModelRuntimeTest, LoadLocalModelWithVariant)
{
    ModelRuntime &rt=ModelRuntime::instance();

    ErrorCode result=rt.loadModel("test-local-7b", "Q4_K_M");

    // This will succeed or fail depending on hardware fit
    // On a typical test machine, the small min_vram_mb (128MB) should allow it
    if(result==ErrorCode::Success)
    {
        auto state=rt.getModelState("test-local-7b");
        ASSERT_TRUE(state.has_value());
        EXPECT_EQ(state->variant, "Q4_K_M");
        EXPECT_EQ(state->state, ModelState::Loaded);
    }
    else
    {
        // ModelLoadError if hardware doesn't support it (e.g., no GPU in CI)
        EXPECT_EQ(result, ErrorCode::ModelLoadError);
    }
}

TEST_F(ModelRuntimeTest, LoadLocalModelAutoSelectsVariant)
{
    ModelRuntime &rt=ModelRuntime::instance();

    ErrorCode result=rt.loadModel("test-local-7b");

    // Auto-selection should pick a variant
    if(result==ErrorCode::Success)
    {
        auto state=rt.getModelState("test-local-7b");
        ASSERT_TRUE(state.has_value());
        EXPECT_FALSE(state->variant.empty());
    }
}

TEST_F(ModelRuntimeTest, LoadLocalModelInvalidVariantFails)
{
    ModelRuntime &rt=ModelRuntime::instance();

    ErrorCode result=rt.loadModel("test-local-7b", "INVALID_QUANT");

    EXPECT_EQ(result, ErrorCode::ModelNotFound);
}

} // namespace arbiterAI
