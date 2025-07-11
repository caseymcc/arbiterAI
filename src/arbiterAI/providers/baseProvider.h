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
 * - Configuration management
 * - Error handling
 */
class BaseProvider
{
public:
    BaseProvider(const std::string provider);
    virtual ~BaseProvider()=default;

    virtual void initialize(const std::vector<ModelInfo> &models) {}

    virtual ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response)=0;

    virtual ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback)=0;

    virtual std::vector<CompletionResponse> batchCompletion(const std::vector<CompletionRequest> &requests);

    virtual ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response)=0;

    virtual DownloadStatus getDownloadStatus(const std::string &modelName, std::string &error);

protected:
    ErrorCode getApiKey(const std::string &modelName,
        const std::optional<std::string> &requestApiKey, std::string &apiKey);

protected:
    std::string m_provider;
};

} // namespace arbiterAI

#endif//_arbiterAI_providers_baseProvider_h_
