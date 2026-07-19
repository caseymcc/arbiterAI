#ifndef _arbiterAI_providers_stableDiffusion_h_
#define _arbiterAI_providers_stableDiffusion_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

#include <map>
#include <mutex>
#include <string>

struct sd_ctx_t;

namespace arbiterAI
{

/**
 * @class StableDiffusion
 * @brief Local text-to-image provider backed by stable-diffusion.cpp
 *
 * Handles models configured with `"provider": "stable-diffusion"` and
 * `"mode": "image"`. The model weights path comes from the model config
 * (`file_path`, or the primary variant filename). Loaded sd contexts are cached
 * per model file path; generated images are PNG-encoded and returned as base64
 * in `GeneratedImage::b64Json` (OpenAI-compatible `b64_json`).
 */
class StableDiffusion : public BaseProvider
{
public:
    StableDiffusion();
    ~StableDiffusion() override;

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    ErrorCode generateImage(const ImageGenerationRequest &request,
        const ModelInfo &model,
        ImageGenerationResponse &response) override;

private:
    std::string resolveModelPath(const ModelInfo &model);

    /// Get (loading on first use) an sd context for the given model file.
    /// Returns nullptr on load failure. Thread-safe.
    sd_ctx_t *acquireContext(const std::string &modelPath);

    std::mutex m_mutex;              ///< Guards the context cache
    std::mutex m_inferenceMutex;     ///< Serializes generation (v1 limitation)
    std::map<std::string, sd_ctx_t *> m_contexts;
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_stableDiffusion_h_
