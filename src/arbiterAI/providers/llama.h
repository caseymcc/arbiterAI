#ifndef _ARBITERAI_PROVIDERS_LLAMA_H_
#define _ARBITERAI_PROVIDERS_LLAMA_H_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/chatFormat.h"

#include <vector>
#include <string>
#include <functional>
#include <cstdint>

// Forward declarations for llama.cpp types
struct llama_model;
struct llama_context;
struct mtmd_context;
struct mtmd_input_chunks;

namespace arbiterAI
{

/// A tokenized multimodal prompt: interleaved text-token and image-embedding
/// chunks produced by libmtmd.  Owns the chunk list.
class MultimodalPrompt {
public:
    MultimodalPrompt()=default;
    ~MultimodalPrompt();

    MultimodalPrompt(const MultimodalPrompt &)=delete;
    MultimodalPrompt &operator=(const MultimodalPrompt &)=delete;

    /// Take ownership of a chunk list produced by mtmd_tokenize().
    void reset(mtmd_context *ctx, mtmd_input_chunks *chunks);

    mtmd_context *context() const { return m_ctx; }
    mtmd_input_chunks *chunks() const { return m_chunks; }
    bool valid() const { return m_ctx!=nullptr&&m_chunks!=nullptr; }

    /// Total token count across all chunks (image chunks included).
    size_t tokenCount() const;

private:
    mtmd_context *m_ctx=nullptr;      // not owned
    mtmd_input_chunks *m_chunks=nullptr; // owned
};

/// How many leading tokens of promptTokens are already present in the KV
/// cache (per cachedTokens) and can skip prefill.  Always leaves at least
/// one prompt token to decode so the final position has fresh logits.
int kvPrefixReuseLength(const std::vector<int32_t> &cachedTokens,
    const std::vector<int32_t> &promptTokens);

/// Remove every occurrence of the given literal strings from text.
/// Used to keep control-token markup out of message content: the templated
/// prompt is tokenized with special-token parsing enabled, so a control token
/// appearing inside user text would otherwise be tokenized as the real thing
/// and let a caller forge turn boundaries.
std::string stripTokenStrings(const std::string &text,
    const std::vector<std::string> &tokenStrings);

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

    /// Human-readable detail for the most recent failure on this instance.
    /// Set alongside a returned error code so callers can report *why* a
    /// generation failed rather than just the error code. Empty on success.
    const std::string &lastErrorDetail() const { return m_lastErrorDetail; }

    /// Tokenize the prompt outside of the inference mutex.
    /// Only reads llama_model/vocab (thread-safe without context lock).
    /// @param chatPrompt  Receives the template-derived prompt and its response
    ///                    parser when the model has a usable chat template and
    ///                    no api_format override; null otherwise.
    ErrorCode tokenizePrompt(llama_model *model,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        std::vector<int32_t> &tokens, std::string &formattedPrompt,
        std::shared_ptr<ChatPrompt> *chatPrompt=nullptr);

    /// Tokenize a prompt that carries images into libmtmd chunks.
    /// mtmd_tokenize() is thread-safe on a shared context, so this runs on the
    /// tokenizer thread like its text-only counterpart.
    /// @param textTokens  Receives the text-chunk tokens (no image tokens), used
    ///                    to seed the sampler and for prompt statistics.
    ErrorCode tokenizeMultimodalPrompt(llama_model *model, mtmd_context *mtmdCtx,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        MultimodalPrompt &prompt, std::vector<int32_t> &textTokens,
        std::string &formattedPrompt,
        std::shared_ptr<ChatPrompt> *chatPrompt=nullptr);

    /// Run inference with pre-tokenized prompt (requires inference lock held).
    /// When `multimodal` is non-null the prompt is prefilled from its chunks
    /// instead of promptTokens (which is then only used for the sampler).
    ErrorCode runInferenceWithTokens(llama_model *model, llama_context *ctx,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        const std::vector<int32_t> &promptTokens,
        std::string &result, int &promptTokenCount, int &completionTokens,
        double &promptTimeMs, double &generationTimeMs,
        std::function<void(const std::string &)> streamCallback,
        std::function<bool()> shouldAbort=nullptr,
        const MultimodalPrompt *multimodal=nullptr);

private:
    /// Render a prompt through the model's template-derived chat format, or
    /// null when an api_format override is set or the model has no template.
    std::shared_ptr<ChatPrompt> applyChatFormat(const std::string &modelName,
        const CompletionRequest &request, const ModelInfo &modelInfo) const;

    /// Copy messages with any of the model's control-token strings removed from
    /// their content (see stripTokenStrings).  `extraMarkers` are additional
    /// literals to strip, e.g. the mtmd media marker.
    std::vector<Message> sanitizeMessages(llama_model *model,
        const std::vector<Message> &messages,
        const std::vector<std::string> &extraMarkers={}) const;

    /// Format messages into a prompt string using the model's chat template.
    std::string applyTemplate(llama_model *model,
        const std::vector<Message> &messages) const;

    /// Format messages into harmony special token format for gpt-oss models.
    std::string formatHarmonyPrompt(const CompletionRequest &request,
        const ModelInfo &modelInfo) const;

    /// Route a request to the text or multimodal inference path (shared by the
    /// direct completion/streaming entry points, which bypass the scheduler).
    ErrorCode runInferenceDispatch(llama_model *model, llama_context *ctx,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        std::string &result, int &promptTokens, int &completionTokens,
        double &promptTimeMs, double &generationTimeMs,
        std::function<void(const std::string &)> streamCallback);

    /// Run the inference loop (shared by completion and streaming).
    ErrorCode runInference(llama_model *model, llama_context *ctx,
        const CompletionRequest &request, const ModelInfo &modelInfo,
        std::string &result, int &promptTokens, int &completionTokens,
        double &promptTimeMs, double &generationTimeMs,
        std::function<void(const std::string &)> streamCallback);

    /// Detail for the most recent failure (see lastErrorDetail()).
    std::string m_lastErrorDetail;
};

} // namespace arbiterAI

#endif//_ARBITERAI_PROVIDERS_LLAMA_H_
