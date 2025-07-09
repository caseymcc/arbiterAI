/**
 * @file llama.h
 * @brief Llama model provider implementation
 *
 * Implements the BaseProvider interface for Llama-based models,
 * supporting both local and remote Llama model instances.
 */

#ifndef _hermesaxiom_providers_llama_h_
#define _hermesaxiom_providers_llama_h_

#include "hermesaxiom/providers/baseProvider.h"
#include <vector>

namespace hermesaxiom
{

/**
 * @class Llama
 * @brief Llama model provider implementation
 * @extends BaseProvider
 *
 * Features:
 * - Supports both CPU and GPU inference
 * - Implements text completion and embeddings
 * - Handles model loading/unloading
 */
class Llama : public BaseProvider
{
public:
    /**
     * @brief Construct a new Llama provider
     */
    Llama();
    ~Llama();

    /**
     * @brief Initialize Llama provider with models
     * @param models Vector of ModelInfo containing model configurations
     */
    void initialize(const std::vector<ModelInfo>& models) override;

    /**
     * @brief Perform text completion using Llama model
     * @param request Completion parameters
     * @param[out] response Completion results
     * @return ErrorCode indicating success or failure
     */
    ErrorCode completion(const CompletionRequest &request,
        CompletionResponse &response) override;

    /**
     * @brief Perform streaming text completion
     * @param request Completion parameters
     * @param callback Function to receive streaming chunks
     * @return ErrorCode indicating success or failure
     */
    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    /**
     * @brief Generate embeddings using Llama model
     * @param request Embedding generation parameters
     * @param[out] response Generated embeddings
     * @return ErrorCode indicating success or failure
     */
    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;
};

} // namespace hermesaxiom

#endif//_hermesaxiom_providers_llama_h_