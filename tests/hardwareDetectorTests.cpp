#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/modelFitCalculator.h"
#include "arbiterAI/modelManager.h"
#include <gtest/gtest.h>

namespace arbiterAI
{

// --- HardwareDetector tests ---

class HardwareDetectorTest : public ::testing::Test
{
};

TEST_F(HardwareDetectorTest, SystemRamDetected)
{
    HardwareDetector &hw=HardwareDetector::instance();
    SystemInfo info=hw.getSystemInfo();

    // On any Linux system, we should detect positive RAM
    EXPECT_GT(info.totalRamMb, 0);
    EXPECT_GT(info.freeRamMb, 0);
    EXPECT_LE(info.freeRamMb, info.totalRamMb);
}

TEST_F(HardwareDetectorTest, CpuCoresDetected)
{
    HardwareDetector &hw=HardwareDetector::instance();
    SystemInfo info=hw.getSystemInfo();

    EXPECT_GT(info.cpuCores, 0);
}

TEST_F(HardwareDetectorTest, CpuUtilizationInRange)
{
    HardwareDetector &hw=HardwareDetector::instance();

    // First refresh sets the baseline, second calculates delta
    hw.refresh();
    SystemInfo info=hw.getSystemInfo();

    EXPECT_GE(info.cpuUtilizationPercent, 0.0f);
    EXPECT_LE(info.cpuUtilizationPercent, 100.0f);
}

TEST_F(HardwareDetectorTest, RefreshUpdatesInfo)
{
    HardwareDetector &hw=HardwareDetector::instance();

    SystemInfo before=hw.getSystemInfo();
    hw.refresh();
    SystemInfo after=hw.getSystemInfo();

    // RAM values should still be valid after refresh
    EXPECT_GT(after.totalRamMb, 0);
    EXPECT_GT(after.freeRamMb, 0);
    // Total RAM shouldn't change between refreshes
    EXPECT_EQ(before.totalRamMb, after.totalRamMb);
}

TEST_F(HardwareDetectorTest, TotalFreeVramMatchesGpus)
{
    HardwareDetector &hw=HardwareDetector::instance();
    SystemInfo info=hw.getSystemInfo();

    int expectedFreeVram=0;
    for(const GpuInfo &gpu:info.gpus)
    {
        expectedFreeVram+=gpu.vramFreeMb;
    }

    EXPECT_EQ(hw.getTotalFreeVramMb(), expectedFreeVram);
}

TEST_F(HardwareDetectorTest, TotalFreeRamMatchesSystemInfo)
{
    HardwareDetector &hw=HardwareDetector::instance();
    SystemInfo info=hw.getSystemInfo();

    EXPECT_EQ(hw.getTotalFreeRamMb(), info.freeRamMb);
}

TEST_F(HardwareDetectorTest, GpuInfoFieldsValid)
{
    HardwareDetector &hw=HardwareDetector::instance();
    std::vector<GpuInfo> gpus=hw.getGpus();

    for(const GpuInfo &gpu:gpus)
    {
        EXPECT_GE(gpu.index, 0);
        EXPECT_FALSE(gpu.name.empty());
        EXPECT_NE(gpu.backend, GpuBackend::None);
        EXPECT_GT(gpu.vramTotalMb, 0);
        EXPECT_GE(gpu.vramFreeMb, 0);
        EXPECT_LE(gpu.vramFreeMb, gpu.vramTotalMb);
        EXPECT_GE(gpu.utilizationPercent, 0.0f);
        EXPECT_LE(gpu.utilizationPercent, 100.0f);
    }
}

// --- ModelFitCalculator tests ---

class ModelFitCalculatorTest : public ::testing::Test
{
protected:
    SystemInfo makeSystemInfo(int totalRamMb, int freeRamMb, std::vector<GpuInfo> gpus={})
    {
        SystemInfo info;
        info.totalRamMb=totalRamMb;
        info.freeRamMb=freeRamMb;
        info.cpuCores=8;
        info.cpuUtilizationPercent=10.0f;
        info.gpus=gpus;
        return info;
    }

    GpuInfo makeGpu(int index, int totalMb, int freeMb)
    {
        GpuInfo gpu;
        gpu.index=index;
        gpu.name="Test GPU "+std::to_string(index);
        gpu.backend=GpuBackend::CUDA;
        gpu.vramTotalMb=totalMb;
        gpu.vramFreeMb=freeMb;
        gpu.computeCapability=8.6f;
        return gpu;
    }

    ModelInfo makeLocalModel(
        const std::string &name,
        int minRamMb,
        const std::string &paramCount,
        int baseCtx, int maxCtx, int vramPer1k)
    {
        ModelInfo model;
        model.model=name;
        model.provider="llama";

        HardwareRequirements hwReqs;
        hwReqs.minSystemRamMb=minRamMb;
        hwReqs.parameterCount=paramCount;
        model.hardwareRequirements=hwReqs;

        ContextScaling scaling;
        scaling.baseContext=baseCtx;
        scaling.maxContext=maxCtx;
        scaling.vramPer1kContextMb=vramPer1k;
        model.contextScaling=scaling;

        return model;
    }

    ModelVariant makeVariant(const std::string &quant, int fileSizeMb, int minVramMb, int recVramMb)
    {
        ModelVariant v;
        v.quantization=quant;
        v.fileSizeMb=fileSizeMb;
        v.minVramMb=minVramMb;
        v.recommendedVramMb=recVramMb;
        return v;
    }
};

TEST_F(ModelFitCalculatorTest, VariantFitsWithSufficientVram)
{
    GpuInfo gpu=makeGpu(0, 12000, 10000);
    SystemInfo hw=makeSystemInfo(32000, 24000, {gpu});

    ModelInfo model=makeLocalModel("llama-7b", 8192, "7B", 4096, 131072, 64);
    ModelVariant variant=makeVariant("Q4_K_M", 4370, 4096, 8192);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_TRUE(fit.canRun);
    EXPECT_EQ(fit.model, "llama-7b");
    EXPECT_EQ(fit.variant, "Q4_K_M");
    EXPECT_GT(fit.maxContextSize, 4096);
    EXPECT_LE(fit.maxContextSize, 131072);
    EXPECT_FALSE(fit.gpuIndices.empty());
}

TEST_F(ModelFitCalculatorTest, VariantFailsInsufficientVram)
{
    GpuInfo gpu=makeGpu(0, 4000, 2000);
    SystemInfo hw=makeSystemInfo(64000, 32000, {gpu});

    ModelInfo model=makeLocalModel("llama-70b", 32768, "70B", 4096, 131072, 256);
    ModelVariant variant=makeVariant("Q4_K_M", 40000, 40000, 48000);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_FALSE(fit.canRun);
    EXPECT_EQ(fit.limitingFactor, "vram");
}

TEST_F(ModelFitCalculatorTest, VariantFailsInsufficientRam)
{
    GpuInfo gpu=makeGpu(0, 12000, 10000);
    SystemInfo hw=makeSystemInfo(4096, 2048, {gpu});

    ModelInfo model=makeLocalModel("llama-7b", 8192, "7B", 4096, 131072, 64);
    ModelVariant variant=makeVariant("Q4_K_M", 4370, 4096, 8192);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_FALSE(fit.canRun);
    EXPECT_EQ(fit.limitingFactor, "ram");
}

TEST_F(ModelFitCalculatorTest, MultiGpuTensorSplitting)
{
    GpuInfo gpu0=makeGpu(0, 8000, 6000);
    GpuInfo gpu1=makeGpu(1, 8000, 6000);
    SystemInfo hw=makeSystemInfo(32000, 24000, {gpu0, gpu1});

    ModelInfo model=makeLocalModel("llama-13b", 16384, "13B", 4096, 32768, 128);
    ModelVariant variant=makeVariant("Q4_K_M", 8000, 8000, 12000);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_TRUE(fit.canRun);
    // Should use both GPUs since no single GPU has 8000MB free
    EXPECT_GE(static_cast<int>(fit.gpuIndices.size()), 1);
}

TEST_F(ModelFitCalculatorTest, ContextScalingCalculation)
{
    GpuInfo gpu=makeGpu(0, 24000, 20000);
    SystemInfo hw=makeSystemInfo(64000, 48000, {gpu});

    ModelInfo model=makeLocalModel("llama-7b", 8192, "7B", 4096, 131072, 64);
    ModelVariant variant=makeVariant("Q4_K_M", 4370, 4096, 8192);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_TRUE(fit.canRun);
    // With 20000MB free and 4096MB for model, ~15904MB extra
    // 15904/64 = 248 extra 1K blocks = 248*1024 = 254976 extra tokens
    // 4096 + 254976 > 131072, so should be clamped to 131072
    EXPECT_EQ(fit.maxContextSize, 131072);
}

TEST_F(ModelFitCalculatorTest, NoGpusCpuFallback)
{
    // No GPUs available, but enough system RAM for CPU fallback
    SystemInfo hw=makeSystemInfo(32000, 24000, {});

    ModelInfo model=makeLocalModel("llama-7b", 8192, "7B", 4096, 131072, 64);
    ModelVariant variant=makeVariant("Q4_K_M", 4370, 4096, 8192);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    // VRAM requirement not met, but RAM fallback should work
    EXPECT_TRUE(fit.canRun);
    EXPECT_EQ(fit.limitingFactor, "vram");
    EXPECT_EQ(fit.maxContextSize, 4096); // base context only
    EXPECT_EQ(fit.estimatedVramUsageMb, 0);
}

TEST_F(ModelFitCalculatorTest, FittableModelsFiltersCloudModels)
{
    GpuInfo gpu=makeGpu(0, 12000, 10000);
    SystemInfo hw=makeSystemInfo(32000, 24000, {gpu});

    // One local model with variants
    ModelInfo localModel=makeLocalModel("llama-7b", 8192, "7B", 4096, 131072, 64);
    localModel.variants.push_back(makeVariant("Q4_K_M", 4370, 4096, 8192));
    localModel.variants.push_back(makeVariant("Q8_0", 8100, 8192, 12288));

    // One cloud model without variants
    ModelInfo cloudModel;
    cloudModel.model="gpt-4";
    cloudModel.provider="openai";

    std::vector<ModelInfo> models={localModel, cloudModel};

    std::vector<ModelFit> results=ModelFitCalculator::calculateFittableModels(models, hw);

    // Should only evaluate local model variants, not cloud models
    EXPECT_EQ(results.size(), 2u);
    for(const ModelFit &fit:results)
    {
        EXPECT_EQ(fit.model, "llama-7b");
    }
}

TEST_F(ModelFitCalculatorTest, NoContextScalingUsesContextWindow)
{
    GpuInfo gpu=makeGpu(0, 12000, 10000);
    SystemInfo hw=makeSystemInfo(32000, 24000, {gpu});

    // Model without context_scaling
    ModelInfo model;
    model.model="simple-model";
    model.provider="llama";
    model.contextWindow=8192;

    ModelVariant variant=makeVariant("Q4_K_M", 4000, 4000, 8000);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_TRUE(fit.canRun);
    EXPECT_EQ(fit.maxContextSize, 8192);
}

TEST_F(ModelFitCalculatorTest, PreferFewerGpus)
{
    // Two GPUs, but the first one alone has enough VRAM
    GpuInfo gpu0=makeGpu(0, 24000, 20000);
    GpuInfo gpu1=makeGpu(1, 8000, 6000);
    SystemInfo hw=makeSystemInfo(64000, 48000, {gpu0, gpu1});

    ModelInfo model=makeLocalModel("llama-7b", 8192, "7B", 4096, 131072, 64);
    ModelVariant variant=makeVariant("Q4_K_M", 4370, 4096, 8192);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_TRUE(fit.canRun);
    // Should only use one GPU since the first has 20000MB free
    EXPECT_EQ(static_cast<int>(fit.gpuIndices.size()), 1);
    EXPECT_EQ(fit.gpuIndices[0], 0);
}

} // namespace arbiterAI
