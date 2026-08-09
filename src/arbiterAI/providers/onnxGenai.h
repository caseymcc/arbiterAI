#ifndef _arbiterAI_providers_onnxGenai_h_
#define _arbiterAI_providers_onnxGenai_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

struct OgaModel;
struct OgaTokenizer;

namespace arbiterAI
{

/**
 * @class OnnxGenai
 * @brief Local text generation via ONNX Runtime GenAI.
 *
 * Handles models with `"provider": "onnx-genai"`. Where the llama provider runs
 * GGUF files, this runs ONNX model *directories* — an OGA model is a folder
 * containing `genai_config.json` alongside the .onnx weights — so `file_path`
 * points at the directory.
 *
 * The reason this provider exists is the execution-provider seam. ONNX Runtime
 * reaches accelerators llama.cpp does not, notably the Ryzen AI NPU, and which
 * one is used is a runtime choice rather than a build-time one. That choice is
 * config, not code:
 *
 *   "onnx_options": {
 *     "execution_providers": ["VitisAI"],
 *     "provider_options": { "VitisAI": { "config_file": "vaip_config.json" } },
 *     "max_length": 4096
 *   }
 *
 * Omit `execution_providers` to use whatever the model's own genai_config.json
 * specifies (CPU for a stock export).
 *
 * Models are cached per directory, since loading one is expensive.
 */
class OnnxGenai : public BaseProvider
{
public:
    OnnxGenai();
    ~OnnxGenai() override;

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback,
        std::function<void()> waitCallback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    DownloadStatus getDownloadStatus(const std::string &modelName,
        std::string &error) override;

    ErrorCode getAvailableModels(std::vector<std::string> &models) override;

    /// Human-readable detail for the most recent failure. Empty on success.
    const std::string &lastErrorDetail() const { return m_lastErrorDetail; }

    /// Drop every cached model (used by tests, and when freeing memory).
    static void clearCache();

    /// A loaded OGA model and its tokenizer, cached per model directory.
    /// Defined in the .cpp so this header stays free of OGA's types.
    struct LoadedOnnxModelData;

private:
    using LoadedOnnxModel=LoadedOnnxModelData;

    /// Load (or fetch from cache) the model directory for this config.
    /// Returns false and sets m_lastErrorDetail on failure.
    bool acquireModel(const ModelInfo &modelInfo, LoadedOnnxModel &out);

    /// Run generation, streaming each decoded chunk to `onChunk` when set.
    ErrorCode generate(const CompletionRequest &request, const ModelInfo &modelInfo,
        const LoadedOnnxModel &loaded,
        std::string &result, int &promptTokens, int &completionTokens,
        const std::function<void(const std::string &)> &onChunk);

    std::string m_lastErrorDetail;
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_onnxGenai_h_
