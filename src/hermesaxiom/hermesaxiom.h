/**
 * @file hermesaxiom.h
 * @brief Core API for HermesAxiom LLM integration library
 *
 * Provides interfaces for:
 * - Model management and initialization
 * - Text completion (standard and streaming)
 * - Embedding generation
 * - Model download status tracking
 */

#ifndef _hermesaxiom_hermes_h_
#define _hermesaxiom_hermes_h_

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <functional>
#include <variant>

namespace hermesaxiom
{

/**
 * @enum ErrorCode
 * @brief Error codes returned by HermesAxiom operations
 */
enum class ErrorCode
{
    Success=0,
    ApiKeyNotFound,
    UnknownModel,
    UnsupportedProvider,
    NetworkError,
    InvalidResponse,
    InvalidRequest,
    NotImplemented,
    ModelNotLoaded,
    GenerationError,
    ModelDownloading,
    DownloadFailed
};

/**
 * @enum DownloadStatus
 * @brief Status codes for model download operations
 */
enum class DownloadStatus
{
    NotStarted,
    InProgress,
    Completed,
    Failed
};

/**
 * @struct Message
 * @brief Represents a single message in a conversation
 */
struct Message
{
    std::string role;
    std::string content;
};

/**
 * @struct CompletionRequest
 * @brief Parameters for text completion requests
 */
struct CompletionRequest
{
    std::string model;
    std::vector<Message> messages;
    std::optional<float> temperature;
    std::optional<int> max_tokens;
    std::optional<std::string> api_key;
    std::optional<std::string> provider;
};

/**
 * @struct CompletionResponse
 * @brief Results from text completion requests
 */
struct CompletionResponse
{
    std::string text;
    std::string model;
    int tokens_used;
    std::string provider;  // "openai", "anthropic", etc.
};

/**
 * @struct EmbeddingRequest
 * @brief Parameters for embedding generation requests
 */
struct EmbeddingRequest
{
    std::string model;
    std::variant<std::string, std::vector<std::string>> input;
};

/**
 * @struct Embedding
 * @brief Single embedding vector with index
 */
struct Embedding
{
    int index;
    std::vector<float> embedding;
};

/**
 * @struct Usage
 * @brief Token usage statistics
 */
struct Usage
{
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;
};

/**
 * @struct EmbeddingResponse
 * @brief Results from embedding generation requests
 */
struct EmbeddingResponse
{
    std::string model;
    std::vector<Embedding> data;
    Usage usage;
};

/**
 * @class hermesaxiom
 * @brief Main interface for HermesAxiom LLM operations
 *
 * Provides methods for:
 * - Initializing the library
 * - Text completion (standard and streaming)
 * - Embedding generation
 * - Model download status tracking
 */
class hermesaxiom
{
public:
    hermesaxiom();
    ~hermesaxiom();

    /**
     * @brief Initialize the HermesAxiom library
     * @param configPaths List of paths to configuration files
     * @return ErrorCode indicating success or failure
     */
    ErrorCode initialize(const std::vector<std::filesystem::path> &configPaths);
    /**
     * @brief Check if a model requires an API key
     * @param model Name of the model to check
     * @return true if API key is required, false otherwise
     */
    bool doesModelNeedApiKey(const std::string &model);
    /**
     * @brief Check if a provider supports model downloads
     * @param provider Name of the provider to check
     * @return true if downloads are supported, false otherwise
     */
    bool supportModelDownload(const std::string &provider);
    /**
     * @brief Perform text completion
     * @param request Completion parameters
     * @param[out] response Completion results
     * @return ErrorCode indicating success or failure
     */
    ErrorCode completion(const CompletionRequest &request, CompletionResponse &response);
    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback); ///< Callback for streaming chunks
    /**
     * @brief Generate embeddings for input text
     * @param request Embedding generation parameters
     * @param[out] response Generated embeddings
     * @return ErrorCode indicating success or failure
     */
    ErrorCode getEmbeddings(const EmbeddingRequest &request, EmbeddingResponse &response);
    /**
     * @brief Get download status for a model
     * @param modelName Name of the model to check
     * @param[out] error Error message if download failed
     * @return ErrorCode indicating current status
     */
    ErrorCode getDownloadStatus(const std::string &modelName, std::string &error);
};

}//namespace hermesaxiom

#endif//_hermesaxiom_hermes_h
