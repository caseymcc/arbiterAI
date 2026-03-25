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
class BaseProvider;
struct ModelInfo;
struct ModelFit;
struct LoadedModel;
struct SystemSnapshot;
struct InferenceStats;

/**
 * @struct VersionInfo
 * @brief Version information for the ArbiterAI library
 */
struct VersionInfo {
    int major;
    int minor;
    int patch;

    /// Returns the version as "major.minor.patch".
    std::string toString() const;
};

/**
 * @brief Get the compiled-in library version (does not require initialization)
 * @return VersionInfo with major, minor, patch fields
 */
VersionInfo getVersion();

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
    NotApplicable,  ///< Cloud provider - no download needed
    NotStarted,     ///< Download has not been initiated
    Pending,        ///< Download is queued
    InProgress,     ///< Download is actively in progress
    Completed,      ///< Download completed successfully
    Failed          ///< Download failed
};

/**
 * @struct DownloadProgress
 * @brief Detailed progress information for model downloads
 */
struct DownloadProgress
{
    DownloadStatus status = DownloadStatus::NotApplicable;
    int64_t bytesDownloaded = 0;    ///< Current bytes downloaded
    int64_t totalBytes = 0;          ///< Total file size in bytes
    float percentComplete = 0.0f;    ///< Download percentage (0-100)
    std::string errorMessage;        ///< Error details if status is Failed
    std::string modelName;           ///< Name of the model being downloaded
};

/**
 * @struct UsageStats
 * @brief Accumulated usage statistics for a chat session
 */
struct UsageStats
{
    int promptTokens = 0;           ///< Total prompt tokens used
    int completionTokens = 0;       ///< Total completion tokens generated
    int totalTokens = 0;            ///< Combined token count
    double estimatedCost = 0.0;     ///< Estimated cost for this session
    int cachedResponses = 0;        ///< Count of responses served from cache
    int completionCount = 0;        ///< Number of completions performed
};

inline void to_json(nlohmann::json &j, const UsageStats &u)
{
    j = nlohmann::json{
        {"prompt_tokens", u.promptTokens},
        {"completion_tokens", u.completionTokens},
        {"total_tokens", u.totalTokens},
        {"estimated_cost", u.estimatedCost},
        {"cached_responses", u.cachedResponses},
        {"completion_count", u.completionCount}
    };
}

inline void from_json(const nlohmann::json &j, UsageStats &u)
{
    if (j.contains("prompt_tokens")) j.at("prompt_tokens").get_to(u.promptTokens);
    if (j.contains("completion_tokens")) j.at("completion_tokens").get_to(u.completionTokens);
    if (j.contains("total_tokens")) j.at("total_tokens").get_to(u.totalTokens);
    if (j.contains("estimated_cost")) j.at("estimated_cost").get_to(u.estimatedCost);
    if (j.contains("cached_responses")) j.at("cached_responses").get_to(u.cachedResponses);
    if (j.contains("completion_count")) j.at("completion_count").get_to(u.completionCount);
}

/**
 * @struct ToolParameter
 * @brief Defines a parameter for a tool/function
 */
struct ToolParameter
{
    std::string name;               ///< Parameter name
    std::string type;               ///< Parameter type (string, number, boolean, object, array)
    std::string description;        ///< Description for the LLM
    bool required = false;          ///< Whether parameter is required
    nlohmann::json schema;          ///< Full JSON schema for complex types
};

/**
 * @struct ToolDefinition
 * @brief Defines a callable function/tool for the LLM
 */
struct ToolDefinition
{
    std::string name;                       ///< Function/tool name
    std::string description;                ///< Description for the LLM
    std::vector<ToolParameter> parameters;  ///< Parameter definitions
    nlohmann::json parametersSchema;        ///< Full JSON schema for parameters
};

inline void to_json(nlohmann::json &j, const ToolParameter &p)
{
    j = nlohmann::json{
        {"name", p.name},
        {"type", p.type},
        {"description", p.description},
        {"required", p.required}
    };
    if (!p.schema.is_null()) j["schema"] = p.schema;
}

inline void from_json(const nlohmann::json &j, ToolParameter &p)
{
    j.at("name").get_to(p.name);
    j.at("type").get_to(p.type);
    if (j.contains("description")) j.at("description").get_to(p.description);
    if (j.contains("required")) j.at("required").get_to(p.required);
    if (j.contains("schema")) p.schema = j.at("schema");
}

inline void to_json(nlohmann::json &j, const ToolDefinition &t)
{
    j = nlohmann::json{
        {"name", t.name},
        {"description", t.description},
        {"parameters", t.parameters}
    };
    if (!t.parametersSchema.is_null()) j["parameters_schema"] = t.parametersSchema;
}

inline void from_json(const nlohmann::json &j, ToolDefinition &t)
{
    j.at("name").get_to(t.name);
    if (j.contains("description")) j.at("description").get_to(t.description);
    if (j.contains("parameters")) j.at("parameters").get_to(t.parameters);
    if (j.contains("parameters_schema")) t.parametersSchema = j.at("parameters_schema");
}

/**
 * @struct ToolCall
 * @brief Represents a tool/function call made by the LLM
 */
struct ToolCall
{
    std::string id;                 ///< Unique identifier for the call
    std::string name;               ///< Name of the tool/function called
    nlohmann::json arguments;       ///< Arguments passed to the tool
};

inline void to_json(nlohmann::json &j, const ToolCall &t)
{
    j = nlohmann::json{
        {"id", t.id},
        {"name", t.name},
        {"arguments", t.arguments}
    };
}

inline void from_json(const nlohmann::json &j, ToolCall &t)
{
    if (j.contains("id")) j.at("id").get_to(t.id);
    j.at("name").get_to(t.name);
    if (j.contains("arguments")) t.arguments = j.at("arguments");
}

/**
 * @struct Message
 * @brief Represents a single message in a conversation
 *
 * Supports all OpenAI message roles:
 * - "system"/"user": standard messages with content
 * - "assistant": may include tool_calls when the model invokes tools
 * - "tool": includes tool_call_id linking the result back to a specific tool call
 */
struct Message
{
    std::string role;
    std::string content;
    std::optional<std::string> toolCallId;
    std::optional<std::vector<ToolCall>> toolCalls;
    std::optional<std::string> name;
};

inline void to_json(nlohmann::json &j, const Message &m)
{
    j=nlohmann::json{{"role", m.role}, {"content", m.content}};
    if(m.toolCallId.has_value()) j["tool_call_id"]=m.toolCallId.value();
    if(m.toolCalls.has_value()) j["tool_calls"]=m.toolCalls.value();
    if(m.name.has_value()) j["name"]=m.name.value();
}

inline void from_json(const nlohmann::json &j, Message &m)
{
    j.at("role").get_to(m.role);
    if(j.contains("content") && !j.at("content").is_null())
        j.at("content").get_to(m.content);
    if(j.contains("tool_call_id"))
        m.toolCallId=j.at("tool_call_id").get<std::string>();
    if(j.contains("tool_calls"))
        m.toolCalls=j.at("tool_calls").get<std::vector<ToolCall>>();
    if(j.contains("name"))
        m.name=j.at("name").get<std::string>();
}

/**
 * @struct ProviderConfig
 * @brief Configuration for a specific LLM connection
 */
struct ProviderConfig
{
    std::string name;                      ///< Connection name/identifier
    std::string provider;                  ///< Provider type (openai, anthropic, deepseek, etc.)
    std::optional<std::string> apiUrl;     ///< Custom API endpoint URL
    std::optional<std::string> apiKey;     ///< API key for authentication
    std::vector<std::string> models;       ///< Models available through this connection
};

inline void to_json(nlohmann::json &j, const ProviderConfig &p)
{
    j = nlohmann::json{
        {"name", p.name},
        {"provider", p.provider}
    };
    if (p.apiUrl.has_value()) j["api_url"] = p.apiUrl.value();
    if (p.apiKey.has_value()) j["api_key"] = p.apiKey.value();
    if (!p.models.empty()) j["models"] = p.models;
}

inline void from_json(const nlohmann::json &j, ProviderConfig &p)
{
    j.at("name").get_to(p.name);
    j.at("provider").get_to(p.provider);
    if (j.contains("api_url")) p.apiUrl = j.at("api_url").get<std::string>();
    if (j.contains("api_key")) p.apiKey = j.at("api_key").get<std::string>();
    if (j.contains("models")) j.at("models").get_to(p.models);
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
    std::optional<std::vector<ToolDefinition>> tools;  ///< Available tools for the model
    std::optional<std::string> tool_choice;            ///< Tool selection mode: "auto", "none", or specific tool name
};

inline void to_json(nlohmann::json &j, const CompletionRequest &r)
{
    j = nlohmann::json{
        {"model", r.model},
        {"messages", r.messages}
    };
    if (r.temperature.has_value()) j["temperature"] = r.temperature.value();
    if (r.max_tokens.has_value()) j["max_tokens"] = r.max_tokens.value();
    if (r.api_key.has_value()) j["api_key"] = r.api_key.value();
    if (r.provider.has_value()) j["provider"] = r.provider.value();
    if (r.top_p.has_value()) j["top_p"] = r.top_p.value();
    if (r.presence_penalty.has_value()) j["presence_penalty"] = r.presence_penalty.value();
    if (r.frequency_penalty.has_value()) j["frequency_penalty"] = r.frequency_penalty.value();
    if (r.stop.has_value()) j["stop"] = r.stop.value();
    if (r.tools.has_value()) j["tools"] = r.tools.value();
    if (r.tool_choice.has_value()) j["tool_choice"] = r.tool_choice.value();
}

inline void from_json(const nlohmann::json &j, CompletionRequest &r)
{
    j.at("model").get_to(r.model);
    j.at("messages").get_to(r.messages);
    if (j.contains("temperature")) r.temperature = j.at("temperature").get<double>();
    if (j.contains("max_tokens")) r.max_tokens = j.at("max_tokens").get<int>();
    if (j.contains("api_key")) r.api_key = j.at("api_key").get<std::string>();
    if (j.contains("provider")) r.provider = j.at("provider").get<std::string>();
    if (j.contains("top_p")) r.top_p = j.at("top_p").get<double>();
    if (j.contains("presence_penalty")) r.presence_penalty = j.at("presence_penalty").get<double>();
    if (j.contains("frequency_penalty")) r.frequency_penalty = j.at("frequency_penalty").get<double>();
    if (j.contains("stop")) r.stop = j.at("stop").get<std::vector<std::string>>();
    if (j.contains("tools")) r.tools = j.at("tools").get<std::vector<ToolDefinition>>();
    if (j.contains("tool_choice")) r.tool_choice = j.at("tool_choice").get<std::string>();
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
    double cost = 0.0;
    std::vector<ToolCall> toolCalls;  ///< Tool calls made by the model
    std::string finishReason;          ///< Reason completion finished (stop, tool_calls, length, etc.)
    bool fromCache = false;            ///< Whether response was served from cache
};

inline void to_json(nlohmann::json &j, const CompletionResponse &r)
{
    j = nlohmann::json{
        {"text", r.text},
        {"model", r.model},
        {"usage", r.usage},
        {"provider", r.provider},
        {"cost", r.cost},
        {"finish_reason", r.finishReason},
        {"from_cache", r.fromCache}
    };
    if (!r.toolCalls.empty()) j["tool_calls"] = r.toolCalls;
}

inline void from_json(const nlohmann::json &j, CompletionResponse &r)
{
    j.at("text").get_to(r.text);
    j.at("model").get_to(r.model);
    j.at("usage").get_to(r.usage);
    j.at("provider").get_to(r.provider);
    if (j.contains("cost")) j.at("cost").get_to(r.cost);
    if (j.contains("tool_calls")) j.at("tool_calls").get_to(r.toolCalls);
    if (j.contains("finish_reason")) j.at("finish_reason").get_to(r.finishReason);
    if (j.contains("from_cache")) j.at("from_cache").get_to(r.fromCache);
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
 * @class ArbiterAI
 * @brief Main interface for ArbiterAI LLM operations
 *
 * Provides methods for:
 * - Initializing the library
 * - Text completion (standard and streaming)
 * - Embedding generation
 * - Model download status tracking
 * - ChatClient factory for stateful sessions
 */

// Forward declaration
class ChatClient;
struct ChatConfig;

class ArbiterAI
{
public:
    static ArbiterAI &instance();

    /// Get the library version.
    static VersionInfo getVersion();

    ArbiterAI(
        bool enableCache = false,
        const std::filesystem::path &cacheDir = "",
        std::chrono::seconds ttl = std::chrono::seconds(0),
        double spendingLimit = -1.0,
        const std::filesystem::path &costStateFile = "");
    ~ArbiterAI();

    /**
    * @brief Initialize the ArbiterAI library
    * @param configPaths List of paths to configuration directories (will search for providers.json in each)
    * @return ErrorCode indicating success or failure
    */
    ErrorCode initialize(const std::vector<std::filesystem::path> &configPaths);

    // ========== ChatClient Factory ==========

    /**
     * @brief Create a new ChatClient for a chat session
     * @param config Configuration for the chat client
     * @return Shared pointer to the new ChatClient, or nullptr on failure
     *
     * Each client maintains its own state and should be created per chat session.
     * Create a new client when the chat restarts.
     *
     * @code
     * ChatConfig config;
     * config.model = "gpt-4";
     * config.temperature = 0.7;
     * auto client = ArbiterAI::instance().createChatClient(config);
     * @endcode
     */
    std::shared_ptr<ChatClient> createChatClient(const ChatConfig& config);

    /**
     * @brief Create a ChatClient with default configuration for a model
     * @param model Model identifier
     * @return Shared pointer to the new ChatClient, or nullptr on failure
     */
    std::shared_ptr<ChatClient> createChatClient(const std::string& model);

    // ========== Model Information ==========

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
     * @brief Get information about a model
     * @param modelName Name of the model
     * @param[out] info Model information
     * @return ErrorCode::Success if found, ErrorCode::UnknownModel otherwise
     */
    ErrorCode getModelInfo(const std::string& modelName, ModelInfo& info);

    /**
     * @brief Get list of available models
     * @param[out] models Vector of model names
     * @return ErrorCode::Success
     */
    ErrorCode getAvailableModels(std::vector<std::string>& models);

    // ========== Stateless Completion (Convenience) ==========

    /**
     * @brief Perform text completion (stateless convenience method)
     * @param request Completion parameters
     * @param[out] response Completion results
     * @return ErrorCode indicating success or failure
     *
     * @note For multi-turn conversations, prefer using ChatClient
     */
    ErrorCode completion(const CompletionRequest &request, CompletionResponse &response);

    /**
     * @brief Perform streaming completion (stateless convenience method)
     * @param request Completion parameters
     * @param callback Function to receive streaming chunks
     * @return ErrorCode indicating success or failure
     */
    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback);

    /**
     * @brief Process multiple completion requests in batch
     * @param requests Vector of completion requests
     * @return Vector of completion responses
     */
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

    // ========== Local Model Management ==========

    /**
     * @brief Load a local model into VRAM for inference
     * @param model Model name
     * @param variant Quantization variant (empty = auto-select)
     * @param contextSize Context size (0 = model default)
     * @return ErrorCode indicating success, ModelDownloading, or failure
     */
    ErrorCode loadModel(const std::string &model, const std::string &variant="", int contextSize=0);

    /**
     * @brief Unload a model from VRAM/RAM
     * @param model Model name
     * @return ErrorCode indicating success or failure
     */
    ErrorCode unloadModel(const std::string &model);

    /**
     * @brief Pin a model to keep it in RAM for quick reload
     * @param model Model name
     * @return ErrorCode indicating success or failure
     */
    ErrorCode pinModel(const std::string &model);

    /**
     * @brief Unpin a model, allowing LRU eviction
     * @param model Model name
     * @return ErrorCode indicating success or failure
     */
    ErrorCode unpinModel(const std::string &model);

    /**
     * @brief Get hardware fit capabilities for all local models
     * @return Vector of ModelFit results
     */
    std::vector<ModelFit> getLocalModelCapabilities();

    /**
     * @brief Get state of all loaded/tracked models
     * @return Vector of LoadedModel states
     */
    std::vector<LoadedModel> getLoadedModels();

    // ========== Telemetry ==========

    /**
     * @brief Get a current system snapshot including hardware, loaded models, and performance
     * @return SystemSnapshot with current state
     */
    SystemSnapshot getTelemetrySnapshot() const;

    /**
     * @brief Get inference history within a time window
     * @param window Time window to query (e.g., 5 minutes)
     * @return Vector of InferenceStats entries within the window
     */
    std::vector<InferenceStats> getInferenceHistory(std::chrono::minutes window) const;

    /**
     * @brief Shutdown the library and clean up resources
     * @return ErrorCode::Success
     */
    ErrorCode shutdown();

    bool initialized = false;
    std::map<std::string, std::unique_ptr<class BaseProvider>> providers;
    std::map<std::string, std::string> connectionModels;  ///< Maps model name to connection name

    /**
     * @brief Get or create a shared provider reference
     * @param providerName Provider identifier
     * @return Shared pointer to the provider, or nullptr if unknown
     */
    std::shared_ptr<BaseProvider> getSharedProvider(const std::string& providerName);

private:
    /**
     * @brief Load provider configuration from a JSON file
     * @param configPath Path to providers.json configuration file
     */
    void loadProviderConfig(const std::filesystem::path& configPath);

    std::unique_ptr<CacheManager> m_cacheManager;
    std::unique_ptr<CostManager> m_costManager;
};

}//namespace arbiterAI

#endif//_arbiterAI_hermes_h
