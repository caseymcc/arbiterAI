/**
 * @file arbiterAI.h
 * @brief Core API for ArbiterAI LLM integration library
 *
 * Provides interfaces for:
 * - Model management and initialization
 * - Text completion (standard and streaming)
 * - Embedding generation
 * - Model download status tracking
 */

#ifndef _arbiterAI_arbiterAI_h_
#define _arbiterAI_arbiterAI_h_

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <functional>
#include <variant>

#include <nlohmann/json.hpp>

namespace arbiterAI
{

class CacheManager;
class CostManager;

/**
 * @enum ErrorCode
 * @brief Error codes returned by ArbiterAI operations
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
    GenerationError,
    ModelNotFound,
    ModelNotLoaded,
    ModelLoadError,
    ModelDownloading,
    ModelDownloadFailed
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

inline void to_json(nlohmann::json &j, const Message &m)
{
    j=nlohmann::json{ {"role", m.role}, {"content", m.content} };
}

inline void from_json(const nlohmann::json &j, Message &m)
{
    j.at("role").get_to(m.role);
    j.at("content").get_to(m.content);
}

/**
 * @struct CompletionRequest
* @brief Parameters for text completion requests
*/
struct CompletionRequest
{
    std::string model;
    std::vector<Message> messages;
    std::optional<double> temperature;
    std::optional<int> max_tokens;
    std::optional<std::string> api_key;
    std::optional<std::string> provider;
    std::optional<double> top_p;
    std::optional<double> presence_penalty;
    std::optional<double> frequency_penalty;
    std::optional<std::vector<std::string>> stop;
};

inline void to_json(nlohmann::json &j, const CompletionRequest &r)
{
    j=nlohmann::json{
        {"model", r.model},
        {"messages", r.messages}
    };
    if(r.temperature.has_value()) j["temperature"]=r.temperature.value();
    if(r.max_tokens.has_value()) j["max_tokens"]=r.max_tokens.value();
    if(r.api_key.has_value()) j["api_key"]=r.api_key.value();
    if(r.provider.has_value()) j["provider"]=r.provider.value();
    if(r.top_p.has_value()) j["top_p"]=r.top_p.value();
    if(r.presence_penalty.has_value()) j["presence_penalty"]=r.presence_penalty.value();
    if(r.frequency_penalty.has_value()) j["frequency_penalty"]=r.frequency_penalty.value();
    if(r.stop.has_value()) j["stop"]=r.stop.value();
}

inline void from_json(const nlohmann::json &j, CompletionRequest &r)
{
    j.at("model").get_to(r.model);
    j.at("messages").get_to(r.messages);
    if(j.contains("temperature")) r.temperature=j.at("temperature").get<double>();
    if(j.contains("max_tokens")) r.max_tokens=j.at("max_tokens").get<int>();
    if(j.contains("api_key")) r.api_key=j.at("api_key").get<std::string>();
    if(j.contains("provider")) r.provider=j.at("provider").get<std::string>();
    if(j.contains("top_p")) r.top_p=j.at("top_p").get<double>();
    if(j.contains("presence_penalty")) r.presence_penalty=j.at("presence_penalty").get<double>();
    if(j.contains("frequency_penalty")) r.frequency_penalty=j.at("frequency_penalty").get<double>();
    if(j.contains("stop")) r.stop=j.at("stop").get<std::vector<std::string>>();
}

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

inline void to_json(nlohmann::json &j, const Usage &u)
{
    j=nlohmann::json{
        {"prompt_tokens", u.prompt_tokens},
        {"completion_tokens", u.completion_tokens},
        {"total_tokens", u.total_tokens}
    };
}

inline void from_json(const nlohmann::json &j, Usage &u)
{
    j.at("prompt_tokens").get_to(u.prompt_tokens);
    j.at("completion_tokens").get_to(u.completion_tokens);
    j.at("total_tokens").get_to(u.total_tokens);
}

/**
 * @struct CompletionResponse
* @brief Results from text completion requests
*/
struct CompletionResponse
{
    std::string text;
    std::string model;
    Usage usage;
    std::string provider;  // "openai", "anthropic", etc.
    double cost=0.0;
};

inline void to_json(nlohmann::json &j, const CompletionResponse &r)
{
    j=nlohmann::json{
        {"text", r.text},
        {"model", r.model},
        {"usage", r.usage},
        {"provider", r.provider},
        {"cost", r.cost}
    };
}

inline void from_json(const nlohmann::json &j, CompletionResponse &r)
{
    j.at("text").get_to(r.text);
    j.at("model").get_to(r.model);
    j.at("usage").get_to(r.usage);
    j.at("provider").get_to(r.provider);
    if(j.contains("cost"))
    {
        j.at("cost").get_to(r.cost);
    }
}

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
 * @class arbiterAI
 * @brief Main interface for ArbiterAI LLM operations
 *
 * Provides methods for:
 * - Initializing the library
 * - Text completion (standard and streaming)
 * - Embedding generation
 * - Model download status tracking
 */
class arbiterAI
{
public:
    arbiterAI(
        bool enableCache=false,
        const std::filesystem::path &cacheDir="",
        std::chrono::seconds ttl=std::chrono::seconds(0),
        double spendingLimit=-1.0,
        const std::filesystem::path &costStateFile=""
    );
    ~arbiterAI();

    /**
    * @brief Initialize the ArbiterAI library
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
    std::vector<CompletionResponse> batchCompletion(const std::vector<CompletionRequest> &requests);
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

private:
    std::unique_ptr<CacheManager> m_cacheManager;
    std::unique_ptr<CostManager> m_costManager;
};

}//namespace arbiterAI

#endif//_arbiterAI_hermes_h
