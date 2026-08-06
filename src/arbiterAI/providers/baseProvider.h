#ifndef _arbiterAI_providers_baseProvider_h_
#define _arbiterAI_providers_baseProvider_h_

#include "arbiterAI/modelManager.h"
#include "arbiterAI/arbiterAI.h"
#include <functional>
#include <vector>

namespace arbiterAI
{

struct EmbeddingRequest;
struct EmbeddingResponse;
struct AudioTranscriptionRequest;
struct AudioTranscriptionResponse;
struct AudioEmbeddingRequest;
struct AudioEmbeddingResponse;
struct AudioClassificationRequest;
struct AudioClassificationResponse;
struct SpeechRequest;
struct SpeechResponse;
struct ImageGenerationRequest;
struct ImageGenerationResponse;

/**
 * @class BaseProvider
 * @brief Abstract base class for model providers
 *
 * Provides common interface for:
 * - Text completion
 * - Embedding generation
 * - Download status tracking
 * - Configuration management
 * - Error handling
 *
 * Each ChatClient instance maintains a reference to its provider,
 * allowing clients to query download status for local models.
 */
class BaseProvider
{
public:
    BaseProvider(const std::string provider);
    virtual ~BaseProvider() = default;

    virtual void initialize(const std::vector<ModelInfo> &models) {}

    /**
     * @brief Perform text completion
     * @param request Completion parameters
     * @param model Model information
     * @param[out] response Completion results
     * @return ErrorCode indicating success or failure
     */
    virtual ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) = 0;

    /**
     * @brief Perform streaming text completion
     * @param request Completion parameters
     * @param callback Function to receive streaming chunks
     * @return ErrorCode indicating success or failure
     */
    virtual ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) = 0;

    /**
     * @brief Perform streaming text completion with queue wait notification
     * @param request Completion parameters
     * @param callback Function to receive streaming chunks
     * @param waitCallback Called periodically while waiting for backend availability
     * @return ErrorCode indicating success or failure
     */
    virtual ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback,
        std::function<void()> waitCallback);


    /**
     * @brief Process multiple completion requests in batch
     * @param requests Vector of completion requests
     * @return Vector of completion responses
     */
    virtual std::vector<CompletionResponse> batchCompletion(const std::vector<CompletionRequest> &requests);

    /**
     * @brief Generate embeddings for input text
     * @param request Embedding generation parameters
     * @param[out] response Generated embeddings
     * @return ErrorCode indicating success or failure
     */
    virtual ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) = 0;

    /**
     * @brief Transcribe audio to text (speech-to-text)
     *
     * Default implementation returns ErrorCode::NotImplemented. Providers that
     * support STT override this.
     */
    virtual ErrorCode transcribe(const AudioTranscriptionRequest &request,
        const ModelInfo &model,
        AudioTranscriptionResponse &response);

    /**
     * @brief Compute a speaker-embedding (voice fingerprint) for audio
     *
     * Default implementation returns ErrorCode::NotImplemented. Providers that
     * support speaker embeddings (sherpa-onnx) override this.
     */
    virtual ErrorCode embedAudio(const AudioEmbeddingRequest &request,
        const ModelInfo &model,
        AudioEmbeddingResponse &response);

    /**
     * @brief Classify audio into sound-event labels
     *
     * Default implementation returns ErrorCode::NotImplemented. Providers that
     * support audio tagging (sherpa-onnx) override this.
     */
    virtual ErrorCode classifyAudio(const AudioClassificationRequest &request,
        const ModelInfo &model,
        AudioClassificationResponse &response);

    /**
     * @brief Synthesize speech from text (text-to-speech)
     *
     * Default implementation returns ErrorCode::NotImplemented. Providers that
     * support TTS override this.
     */
    virtual ErrorCode synthesizeSpeech(const SpeechRequest &request,
        const ModelInfo &model,
        SpeechResponse &response);

    /**
     * @brief Generate image(s) from a text prompt (diffusion)
     *
     * Default implementation returns ErrorCode::NotImplemented. Providers that
     * support image generation override this.
     */
    virtual ErrorCode generateImage(const ImageGenerationRequest &request,
        const ModelInfo &model,
        ImageGenerationResponse &response);

    /**
     * @brief Get download status for a model (legacy interface)
     * @param modelName Name of the model
     * @param[out] error Error message if failed
     * @return DownloadStatus enum value
     *
     * For cloud providers, returns DownloadStatus::NotApplicable.
     * For local providers, returns current download state.
     */
    virtual DownloadStatus getDownloadStatus(const std::string &modelName, std::string &error);

    /**
     * @brief Get detailed download progress for a model
     * @param modelName Name of the model
     * @param[out] progress Detailed progress information
     * @return ErrorCode::Success
     *
     * For cloud providers, sets status to NotApplicable.
     * For local providers, provides detailed progress including bytes and percentage.
     */
    virtual ErrorCode getDownloadProgress(const std::string &modelName, DownloadProgress &progress);

    /**
     * @brief Get list of available models from this provider
     * @param[out] models Vector of model names
     * @return ErrorCode indicating success or failure
     */
    virtual ErrorCode getAvailableModels(std::vector<std::string>& models);

    /**
     * @brief Get the provider name
     * @return Provider identifier string
     */
    std::string getProviderName() const { return m_provider; }

    /**
     * @brief Set the API endpoint URL for this provider
     * @param url The endpoint URL
     */
    virtual void setApiUrl(const std::string &url) {}

    /**
     * @brief Set the API key for this provider
     * @param key The API key
     */
    virtual void setApiKey(const std::string &key) { m_apiKey = key; }

protected:
    ErrorCode getApiKey(const std::string &modelName,
        const std::optional<std::string> &requestApiKey, std::string &apiKey);

    /**
     * @brief Resolve a local-engine model's weight file, downloading if needed.
     *
     * Resolution order:
     *  1. If the model's `file_path` is set and the file exists, use it.
     *  2. Otherwise, if the model's primary variant carries download info
     *     (`variants[].download.url` / `files[]`), download every file to the
     *     StorageManager models directory (skipping ones already present) and
     *     return the primary file's local path — same auto-download behavior as
     *     llama GGUFs. Multi-file variants (e.g. TTS model + tokenizer + voice)
     *     are all fetched.
     *  3. Otherwise return `file_path` as-is (may be empty; caller errors).
     *
     * The download is synchronous (blocks the load/first request). Returns an
     * empty string only if a required download fails.
     */
    std::string resolveDownloadableModelFile(const ModelInfo &model);

protected:
    std::string m_provider;
    std::string m_apiKey;  ///< API key set via setApiKey()
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_baseProvider_h_
