#include "arbiterAI/modelFitCalculator.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace arbiterAI
{

int ModelFitCalculator::sumFreeVram(const SystemInfo &hw, const std::vector<int> &gpuIndices)
{
    int total=0;
    for(int idx:gpuIndices)
    {
        if(idx>=0&&idx<static_cast<int>(hw.gpus.size()))
        {
            total+=hw.gpus[idx].vramFreeMb;
        }
    }
    return total;
}

std::vector<int> ModelFitCalculator::allGpuIndices(const SystemInfo &hw)
{
    std::vector<int> indices;
    for(int i=0; i<static_cast<int>(hw.gpus.size()); ++i)
    {
        indices.push_back(i);
    }
    return indices;
}

int ModelFitCalculator::estimateMaxContext(
    int availableVramMb,
    int variantMinVramMb,
    const ContextScaling &scaling)
{
    if(scaling.vramPer1kContextMb<=0)
    {
        return scaling.baseContext;
    }

    // Available VRAM beyond what the model itself needs at base context
    int extraVramMb=availableVramMb-variantMinVramMb;
    if(extraVramMb<=0)
    {
        return scaling.baseContext;
    }

    // Each 1K additional context tokens costs vramPer1kContextMb MB
    int extraContextTokens=(extraVramMb/scaling.vramPer1kContextMb)*1024;
    int maxContext=scaling.baseContext+extraContextTokens;

    // Clamp to the model's maximum supported context
    if(maxContext>scaling.maxContext)
    {
        maxContext=scaling.maxContext;
    }

    return maxContext;
}

ModelFit ModelFitCalculator::calculateModelFit(
    const ModelInfo &model,
    const ModelVariant &variant,
    const SystemInfo &hw)
{
    ModelFit fit;
    fit.model=model.model;
    fit.variant=variant.quantization;
    fit.canRun=false;

    // Check system RAM requirement
    if(model.hardwareRequirements.has_value())
    {
        int requiredRamMb=model.hardwareRequirements->minSystemRamMb;

        if(requiredRamMb>0&&hw.totalRamMb<requiredRamMb)
        {
            fit.limitingFactor="ram";
            return fit;
        }
    }

    // Determine which GPUs to use
    std::vector<int> gpuIndices=allGpuIndices(hw);
    int totalFreeVram=sumFreeVram(hw, gpuIndices);

    // Check minimum VRAM requirement
    if(variant.minVramMb>0)
    {
        if(totalFreeVram<variant.minVramMb)
        {
            // Can't run even with all GPUs — check if CPU-only fallback is possible
            if(model.hardwareRequirements.has_value()&&
                model.hardwareRequirements->minSystemRamMb>0&&
                hw.freeRamMb>=variant.fileSizeMb)
            {
                // CPU-only fallback: can run but slowly, with base context
                fit.canRun=true;
                fit.maxContextSize=model.contextScaling.has_value()
                    ? model.contextScaling->baseContext
                    : model.contextWindow;
                fit.estimatedVramUsageMb=0;
                fit.limitingFactor="vram";
                return fit;
            }

            fit.limitingFactor="vram";
            return fit;
        }
    }

    // Try to find minimal set of GPUs needed (prefer fewer GPUs)
    std::vector<int> selectedGpus;
    if(!gpuIndices.empty())
    {
        // Sort GPUs by free VRAM descending to prefer the most capable GPU
        std::vector<int> sortedGpus=gpuIndices;
        std::sort(sortedGpus.begin(), sortedGpus.end(),
            [&hw](int a, int b)
            {
                return hw.gpus[a].vramFreeMb>hw.gpus[b].vramFreeMb;
            });

        int accumulated=0;
        for(int idx:sortedGpus)
        {
            selectedGpus.push_back(idx);
            accumulated+=hw.gpus[idx].vramFreeMb;
            if(accumulated>=variant.minVramMb)
            {
                break;
            }
        }
    }

    fit.gpuIndices=selectedGpus;
    fit.canRun=true;

    // Calculate max context size
    int availableVram=sumFreeVram(hw, selectedGpus);
    if(model.contextScaling.has_value())
    {
        fit.maxContextSize=estimateMaxContext(
            availableVram, variant.minVramMb, model.contextScaling.value());
    }
    else
    {
        fit.maxContextSize=model.contextWindow;
    }

    // Estimate VRAM usage at the calculated context size
    fit.estimatedVramUsageMb=variant.minVramMb;
    if(model.contextScaling.has_value()&&model.contextScaling->vramPer1kContextMb>0)
    {
        int extraContext=fit.maxContextSize-model.contextScaling->baseContext;
        if(extraContext>0)
        {
            fit.estimatedVramUsageMb+=(extraContext/1024)*model.contextScaling->vramPer1kContextMb;
        }
    }

    return fit;
}

std::vector<ModelFit> ModelFitCalculator::calculateFittableModels(
    const std::vector<ModelInfo> &models,
    const SystemInfo &hw)
{
    std::vector<ModelFit> results;

    for(const ModelInfo &model:models)
    {
        if(model.variants.empty())
        {
            // Cloud-only model — skip hardware fit calculation
            continue;
        }

        for(const ModelVariant &variant:model.variants)
        {
            ModelFit fit=calculateModelFit(model, variant, hw);
            results.push_back(fit);
        }
    }

    return results;
}

} // namespace arbiterAI
