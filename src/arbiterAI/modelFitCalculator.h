#ifndef _ARBITERAI_MODELFITCALCULATOR_H_
#define _ARBITERAI_MODELFITCALCULATOR_H_

#include <string>
#include <vector>

#include "hardwareDetector.h"
#include "modelManager.h"

namespace arbiterAI
{

struct ModelFit {
    std::string model;
    std::string variant;
    bool canRun=false;
    int maxContextSize=0;
    std::string limitingFactor;
    int estimatedVramUsageMb=0;
    std::vector<int> gpuIndices;
};

class ModelFitCalculator {
public:
    /// Calculate fit for a specific model variant against current hardware.
    static ModelFit calculateModelFit(
        const ModelInfo &model,
        const ModelVariant &variant,
        const SystemInfo &hw);

    /// Calculate fit for all variants of all provided models.
    static std::vector<ModelFit> calculateFittableModels(
        const std::vector<ModelInfo> &models,
        const SystemInfo &hw);

private:
    /// Sum free VRAM across a set of GPU indices.
    /// For unified memory GPUs, uses gpuAccessibleRamFreeMb when available.
    static int sumFreeVram(const SystemInfo &hw, const std::vector<int> &gpuIndices);

    /// Sum total VRAM across a set of GPU indices.
    /// For unified memory GPUs, uses gpuAccessibleRamMb when available.
    static int sumTotalVram(const SystemInfo &hw, const std::vector<int> &gpuIndices);

    /// Get all GPU indices from the system info.
    static std::vector<int> allGpuIndices(const SystemInfo &hw);

    /// Estimate maximum context size given available VRAM and model scaling info.
    static int estimateMaxContext(
        int availableVramMb,
        int variantMinVramMb,
        const ContextScaling &scaling);
};

} // namespace arbiterAI

#endif//_ARBITERAI_MODELFITCALCULATOR_H_
