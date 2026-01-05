/**
 * @file mock.h
 * @brief Mock/Echo provider for testing
 *
 * This provider enables repeatable testing without requiring actual LLM calls.
 * It supports an "echo mode" where responses are extracted from special tags
 * in the input messages, allowing tests to control expected outputs.
 *
 * Usage:
 * 1. Set model provider to "mock" in configuration
 * 2. Include <echo>expected response</echo> tags in user messages
 * 3. The provider will extract and return the content within the tags
 *
 * Example:
 *   User message: "Calculate 2+2 <echo>4</echo>"
 *   Provider response: "4"
 *
 * If no echo tags are found, returns a default mock response.
 */

#ifndef _arbiterAI_providers_mock_h_
#define _arbiterAI_providers_mock_h_

#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/modelManager.h"

#include <nlohmann/json.hpp>
#include <string>
#include <regex>

namespace arbiterAI
{

/**
 * @class Mock
 * @brief Mock provider for testing with echo mode support
 *
 * Features:
 * - Extracts expected responses from <echo> tags in messages
 * - Returns predictable, repeatable responses
 * - Simulates token usage for testing statistics
 * - Supports both blocking and streaming completions
 * - No network calls or actual LLM inference
 */
class Mock : public BaseProvider
{
public:
    Mock();

    /**
     * @brief Perform mock completion
     *
     * Searches all messages for <echo>...</echo> tags and returns
     * the content within the first tag found. If no tags exist,
     * returns a default mock response.
     *
     * @param request Completion parameters
     * @param model Model information (ignored for mock)
     * @param[out] response Mocked completion results
     * @return ErrorCode::Success
     */
    ErrorCode completion(const CompletionRequest &request,
        const ModelInfo &model,
        CompletionResponse &response) override;

    /**
     * @brief Perform mock streaming completion
     *
     * Extracts echo tag content and streams it in chunks to simulate
     * real streaming behavior.
     *
     * @param request Completion parameters
     * @param callback Function to receive streaming chunks
     * @return ErrorCode::Success
     */
    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback) override;

    /**
     * @brief Mock embeddings (not implemented)
     *
     * Returns NotImplemented error as embeddings are not needed for
     * completion testing.
     *
     * @param request Embedding parameters
     * @param[out] response Embedding results
     * @return ErrorCode::NotImplemented
     */
    ErrorCode getEmbeddings(const EmbeddingRequest &request,
        EmbeddingResponse &response) override;

    /**
     * @brief Get available mock models
     *
     * Returns a single mock model identifier.
     *
     * @param[out] models Vector to populate with model names
     * @return ErrorCode::Success
     */
    ErrorCode getAvailableModels(std::vector<std::string>& models) override;

private:
    /**
     * @brief Extract content from echo tags in a message
     *
     * Searches for <echo>...</echo> tags and returns the content within.
     * Supports multiline content and multiple tags (returns first match).
     *
     * @param message Message to search
     * @param[out] echoContent Extracted content (empty if no tags found)
     * @return true if echo tag was found, false otherwise
     */
    bool extractEchoContent(const std::string &message, std::string &echoContent) const;

    /**
     * @brief Calculate simulated token usage
     *
     * Provides realistic token counts for testing statistics tracking.
     *
     * @param prompt Input message content
     * @param completion Generated response content
     * @return Usage struct with token counts
     */
    Usage calculateMockUsage(const std::string &prompt, const std::string &completion) const;

    static constexpr const char* DEFAULT_RESPONSE = 
        "This is a mock response. Use <echo>your expected response</echo> tags in your messages to control the output.";
    
    static constexpr size_t STREAM_CHUNK_SIZE = 10; ///< Characters per streaming chunk
};

} // namespace arbiterAI

#endif // _arbiterAI_providers_mock_h_
