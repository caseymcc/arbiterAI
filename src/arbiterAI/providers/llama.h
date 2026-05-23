#ifndef _ARBITERAI_PROVIDERS_LLAMA_H_
#define _ARBITERAI_PROVIDERS_LLAMA_H_

#include "arbiterAI/providers/baseProvider.h"

#include <vector>
#include <string>
#include <functional>
#include <cstdint>

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

    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback,
        std::function<void()> waitCallback) override;

    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    DownloadStatus getDownloadStatus(const std::string &modelName,
        std::string &error) override;

    ErrorCode getAvailableModels(std::vector<std::string> &models) override;

private:
    /// Format messages into a prompt string using the model's chat template.
    std::string applyTemplate(llama_model *model,
        const std::vector<Message> &messages) const;

    /// Format messages into harmony special token format for gpt-oss models.
    std::string formatHarmonyPrompt(const CompletionRequest &request,
        const ModelInfo &modelInfo) const;

    /// Tokenize the prompt outside of the inference mutex.
    /// Returns the formatted prompt tokens ready for decode.
    ErrorCode tokenizePrompt(llama_model *model,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        std::vector<int32_t> &tokens, std::string &formattedPrompt);

    /// Run the inference loop (shared by completion and streaming).
    ErrorCode runInference(llama_model *model, llama_context *ctx,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        std::string &result, int &promptTokens, int &completionTokens,
        double &promptTimeMs, double &generationTimeMs,
        std::function<void(const std::string &)> streamCallback);

    /// Run inference with pre-tokenized prompt (avoids re-tokenizing under lock).
    ErrorCode runInferenceWithTokens(llama_model *model, llama_context *ctx,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        const std::vector<int32_t> &promptTokens,
        std::string &result, int &promptTokenCount, int &completionTokens,
        double &promptTimeMs, double &generationTimeMs,
        std::function<void(const std::string &)> streamCallback);
};

} // namespace arbiterAI

#endif//_ARBITERAI_PROVIDERS_LLAMA_H_
