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

protected:
    ErrorCode getApiKey(const std::string &modelName,
        const std::optional<std::string> &requestApiKey, std::string &apiKey);

protected:
    std::string m_provider;
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_baseProvider_h_
