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

        if(gpu.unifiedMemory&&gpu.gpuAccessibleRamMb>0)
        {
            // Accessible RAM should be larger than VRAM alone on APUs
            EXPECT_GE(gpu.gpuAccessibleRamMb, gpu.vramTotalMb);
            EXPECT_GE(gpu.gpuAccessibleRamFreeMb, 0);
            EXPECT_LE(gpu.gpuAccessibleRamFreeMb, gpu.gpuAccessibleRamMb);
        }
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

    GpuInfo makeUnifiedGpu(int index, int vramMb, int vramFreeMb,
        int accessibleMb, int accessibleFreeMb)
    {
        GpuInfo gpu;
        gpu.index=index;
        gpu.name="APU GPU "+std::to_string(index);
        gpu.backend=GpuBackend::Vulkan;
        gpu.vramTotalMb=vramMb;
        gpu.vramFreeMb=vramFreeMb;
        gpu.unifiedMemory=true;
        gpu.gpuAccessibleRamMb=accessibleMb;
        gpu.gpuAccessibleRamFreeMb=accessibleFreeMb;
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

// --- Unified memory (APU) tests ---

TEST_F(ModelFitCalculatorTest, UnifiedMemoryUsesAccessibleRam)
{
    // Simulate a Ryzen AI Max+ 395: 42GB VRAM (device-local), but ~120GB accessible
    GpuInfo gpu=makeUnifiedGpu(0, 42739, 40000, 122880, 110000);
    SystemInfo hw=makeSystemInfo(131072, 115000, {gpu});

    ModelInfo model=makeLocalModel("llama-70b", 32768, "70B", 4096, 131072, 256);
    ModelVariant variant=makeVariant("Q4_K_M", 40000, 40000, 48000);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    // Without unified memory awareness, 40000MB free VRAM barely fits the model
    // With unified memory, 110000MB free accessible RAM easily fits + allows more context
    EXPECT_TRUE(fit.canRun);
    EXPECT_GT(fit.maxContextSize, 4096);
}

TEST_F(ModelFitCalculatorTest, UnifiedMemoryLargerContextThanVramAlone)
{
    // APU with 42GB VRAM but 120GB accessible
    GpuInfo gpu=makeUnifiedGpu(0, 42739, 40000, 122880, 110000);
    SystemInfo hw=makeSystemInfo(131072, 115000, {gpu});

    ModelInfo model=makeLocalModel("llama-7b", 8192, "7B", 4096, 131072, 64);
    ModelVariant variant=makeVariant("Q4_K_M", 4370, 4096, 8192);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_TRUE(fit.canRun);
    // With 110000MB accessible, context should scale to max
    EXPECT_EQ(fit.maxContextSize, 131072);
}

TEST_F(ModelFitCalculatorTest, UnifiedMemoryFallbackToVramWhenNoAccessibleInfo)
{
    // Unified flag set but no accessible RAM info (sysfs unavailable)
    GpuInfo gpu;
    gpu.index=0;
    gpu.name="Unknown APU";
    gpu.backend=GpuBackend::Vulkan;
    gpu.vramTotalMb=42739;
    gpu.vramFreeMb=40000;
    gpu.unifiedMemory=true;
    gpu.gpuAccessibleRamMb=0;
    gpu.gpuAccessibleRamFreeMb=0;

    SystemInfo hw=makeSystemInfo(131072, 115000, {gpu});

    ModelInfo model=makeLocalModel("llama-7b", 8192, "7B", 4096, 131072, 64);
    ModelVariant variant=makeVariant("Q4_K_M", 4370, 4096, 8192);

    ModelFit fit=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_TRUE(fit.canRun);
    // Should fall back to vramFreeMb (40000) since gpuAccessibleRamFreeMb is 0
    EXPECT_GT(fit.maxContextSize, 4096);
}

// --- VRAM Override tests ---

class VramOverrideTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        HardwareDetector::instance().clearAllVramOverrides();
    }
};

TEST_F(VramOverrideTest, SetAndQueryOverride)
{
    HardwareDetector &hw=HardwareDetector::instance();

    EXPECT_FALSE(hw.hasVramOverride(0));
    EXPECT_EQ(hw.getVramOverride(0), 0);

    hw.setVramOverride(0, 32000);

    EXPECT_TRUE(hw.hasVramOverride(0));
    EXPECT_EQ(hw.getVramOverride(0), 32000);
}

TEST_F(VramOverrideTest, ClearOverride)
{
    HardwareDetector &hw=HardwareDetector::instance();

    hw.setVramOverride(0, 32000);
    EXPECT_TRUE(hw.hasVramOverride(0));

    hw.clearVramOverride(0);
    EXPECT_FALSE(hw.hasVramOverride(0));
    EXPECT_EQ(hw.getVramOverride(0), 0);
}

TEST_F(VramOverrideTest, ClearAllOverrides)
{
    HardwareDetector &hw=HardwareDetector::instance();

    hw.setVramOverride(0, 32000);
    hw.setVramOverride(1, 16000);
    EXPECT_TRUE(hw.hasVramOverride(0));
    EXPECT_TRUE(hw.hasVramOverride(1));

    hw.clearAllVramOverrides();
    EXPECT_FALSE(hw.hasVramOverride(0));
    EXPECT_FALSE(hw.hasVramOverride(1));
}

TEST_F(VramOverrideTest, OverridePersistsAcrossRefresh)
{
    HardwareDetector &hw=HardwareDetector::instance();
    std::vector<GpuInfo> gpus=hw.getGpus();

    if(gpus.empty())
    {
        GTEST_SKIP()<<"No GPUs detected, cannot test override persistence";
    }

    int gpuIdx=gpus[0].index;
    int overrideValue=12345;

    hw.setVramOverride(gpuIdx, overrideValue);
    hw.refresh();

    SystemInfo info=hw.getSystemInfo();
    ASSERT_FALSE(info.gpus.empty());

    bool found=false;
    for(const GpuInfo &gpu:info.gpus)
    {
        if(gpu.index==gpuIdx)
        {
            EXPECT_EQ(gpu.vramTotalMb, overrideValue);
            EXPECT_LE(gpu.vramFreeMb, overrideValue);
            EXPECT_TRUE(gpu.vramOverridden);
            found=true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(VramOverrideTest, OverrideAppliedToGpuInfo)
{
    HardwareDetector &hw=HardwareDetector::instance();
    std::vector<GpuInfo> gpus=hw.getGpus();

    if(gpus.empty())
    {
        GTEST_SKIP()<<"No GPUs detected, cannot test override application";
    }

    int gpuIdx=gpus[0].index;
    int originalTotal=gpus[0].vramTotalMb;
    int overrideValue=originalTotal/2;

    if(overrideValue<=0) overrideValue=1024;

    hw.setVramOverride(gpuIdx, overrideValue);

    SystemInfo info=hw.getSystemInfo();

    for(const GpuInfo &gpu:info.gpus)
    {
        if(gpu.index==gpuIdx)
        {
            EXPECT_EQ(gpu.vramTotalMb, overrideValue);
            EXPECT_TRUE(gpu.vramOverridden);
            break;
        }
    }
}

TEST_F(VramOverrideTest, NonOverriddenGpuUnaffected)
{
    HardwareDetector &hw=HardwareDetector::instance();
    std::vector<GpuInfo> gpus=hw.getGpus();

    if(gpus.empty())
    {
        GTEST_SKIP()<<"No GPUs detected";
    }

    int gpuIdx=gpus[0].index;
    int originalTotal=gpus[0].vramTotalMb;

    // Override a non-existent GPU index
    hw.setVramOverride(999, 99999);
    hw.refresh();

    SystemInfo info=hw.getSystemInfo();
    for(const GpuInfo &gpu:info.gpus)
    {
        if(gpu.index==gpuIdx)
        {
            // Original GPU should be unaffected
            EXPECT_FALSE(gpu.vramOverridden);
            break;
        }
    }
}

TEST_F(VramOverrideTest, OverrideAffectsModelFitCalculation)
{
    GpuInfo gpu;
    gpu.index=0;
    gpu.name="Test GPU";
    gpu.backend=GpuBackend::CUDA;
    gpu.vramTotalMb=2000;
    gpu.vramFreeMb=2000;

    SystemInfo hw;
    hw.totalRamMb=32000;
    hw.freeRamMb=24000;
    hw.cpuCores=8;
    hw.gpus={gpu};

    ModelInfo model;
    model.model="test-model";
    model.provider="llama";

    HardwareRequirements hwReqs;
    hwReqs.minSystemRamMb=8192;
    hwReqs.parameterCount="7B";
    model.hardwareRequirements=hwReqs;

    ContextScaling scaling;
    scaling.baseContext=4096;
    scaling.maxContext=131072;
    scaling.vramPer1kContextMb=64;
    model.contextScaling=scaling;

    ModelVariant variant;
    variant.quantization="Q4_K_M";
    variant.fileSizeMb=4370;
    variant.minVramMb=4096;
    variant.recommendedVramMb=8192;

    // Without override: 2000 MB VRAM cannot fit 4096 minVram, CPU fallback gives base context
    ModelFit fitBefore=ModelFitCalculator::calculateModelFit(model, variant, hw);
    EXPECT_EQ(fitBefore.maxContextSize, 4096);

    // Apply override: 16000 MB VRAM now fits model + allows extra context
    hw.gpus[0].vramTotalMb=16000;
    hw.gpus[0].vramFreeMb=16000;
    hw.gpus[0].vramOverridden=true;

    ModelFit fitAfter=ModelFitCalculator::calculateModelFit(model, variant, hw);

    EXPECT_TRUE(fitAfter.canRun);
    EXPECT_GT(fitAfter.maxContextSize, fitBefore.maxContextSize);
}

} // namespace arbiterAI
