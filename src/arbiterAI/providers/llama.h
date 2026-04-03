#ifndef _ARBITERAI_PROVIDERS_LLAMA_H_
#define _ARBITERAI_PROVIDERS_LLAMA_H_

#include "arbiterAI/providers/baseProvider.h"

#include <vector>
#include <string>
#include <functional>

// Forward declarations for llama.cpp types
struct llama_model;
struct llama_context;

namespace arbiterAI
{

class Llama : public BaseProvider {
public:
    Llama();
    ~Llama();

    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    DownloadStatus getDownloadStatus(const std::string &modelName,
        std::string &error) override;

    ErrorCode getAvailableModels(std::vector<std::string> &models) override;

private:
    /// Format messages into a prompt string using the model's chat template.
    std::string applyTemplate(llama_model *model,
        const std::vector<Message> &messages) const;

    /// Run the inference loop (shared by completion and streaming).
    ErrorCode runInference(llama_model *model, llama_context *ctx,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        std::string &result, int &promptTokens, int &completionTokens,
        double &promptTimeMs, double &generationTimeMs,
        std::function<void(const std::string &)> streamCallback);
};

} // namespace arbiterAI

#endif//_ARBITERAI_PROVIDERS_LLAMA_H_
