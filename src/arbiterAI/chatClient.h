/**
 * @file chatClient.h
 * @brief ChatClient interface for stateful chat interactions
 *
 * Provides a session-oriented interface for chat interactions with LLMs.
 * Each ChatClient instance maintains:
 * - Conversation state and message history
 * - Model-specific configuration
 * - Tool/function calling state
 * - Session-level caching
 * - Download status for local models
 */

#ifndef _arbiterAI_chatClient_h_
#define _arbiterAI_chatClient_h_

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/modelManager.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>
#include <mutex>

namespace arbiterAI
{

class BaseProvider;
class CacheManager;

/**
 * @struct ChatConfig
 * @brief Configuration for creating a ChatClient instance
 */
struct ChatConfig
{
    std::string model;                          ///< Model identifier (required)
    std::optional<double> temperature;          ///< Initial temperature setting
    std::optional<int> maxTokens;               ///< Maximum tokens per completion
    std::optional<std::string> systemPrompt;    ///< Optional system message
    std::optional<std::string> apiKey;          ///< API key override
    bool enableCache = false;                   ///< Enable session-level caching
    std::chrono::seconds cacheTTL{3600};        ///< Cache time-to-live
    std::optional<double> topP;                 ///< Top-p sampling parameter
    std::optional<double> presencePenalty;      ///< Presence penalty
    std::optional<double> frequencyPenalty;     ///< Frequency penalty
};

inline void to_json(nlohmann::json &j, const ChatConfig &c)
{
    j = nlohmann::json{
        {"model", c.model},
        {"enable_cache", c.enableCache},
        {"cache_ttl", c.cacheTTL.count()}
    };
    if (c.temperature.has_value()) j["temperature"] = c.temperature.value();
    if (c.maxTokens.has_value()) j["max_tokens"] = c.maxTokens.value();
    if (c.systemPrompt.has_value()) j["system_prompt"] = c.systemPrompt.value();
    if (c.apiKey.has_value()) j["api_key"] = c.apiKey.value();
    if (c.topP.has_value()) j["top_p"] = c.topP.value();
    if (c.presencePenalty.has_value()) j["presence_penalty"] = c.presencePenalty.value();
    if (c.frequencyPenalty.has_value()) j["frequency_penalty"] = c.frequencyPenalty.value();
}

inline void from_json(const nlohmann::json &j, ChatConfig &c)
{
    j.at("model").get_to(c.model);
    if (j.contains("temperature")) c.temperature = j.at("temperature").get<double>();
    if (j.contains("max_tokens")) c.maxTokens = j.at("max_tokens").get<int>();
    if (j.contains("system_prompt")) c.systemPrompt = j.at("system_prompt").get<std::string>();
    if (j.contains("api_key")) c.apiKey = j.at("api_key").get<std::string>();
    if (j.contains("enable_cache")) j.at("enable_cache").get_to(c.enableCache);
    if (j.contains("cache_ttl")) c.cacheTTL = std::chrono::seconds(j.at("cache_ttl").get<int>());
    if (j.contains("top_p")) c.topP = j.at("top_p").get<double>();
    if (j.contains("presence_penalty")) c.presencePenalty = j.at("presence_penalty").get<double>();
    if (j.contains("frequency_penalty")) c.frequencyPenalty = j.at("frequency_penalty").get<double>();
}

/**
 * @brief Callback type for streaming completion responses
 * @param chunk The text chunk received
 * @param done Whether this is the final chunk
 */
using StreamCallback = std::function<void(const std::string& chunk, bool done)>;

/**
 * @class ChatClient
 * @brief Stateful client for chat interactions with LLMs
 *
 * Each instance maintains its own:
 * - Conversation history
 * - Session configuration
 * - Tool definitions
 * - Usage statistics
 * - Cache (if enabled)
 *
 * Create via ArbiterAI::createChatClient() factory method.
 * A new instance should be created when the chat session restarts.
 */
class ChatClient
{
public:
    /**
     * @brief Construct a ChatClient
     * @param config Session configuration
     * @param provider Reference to the model provider
     * @param modelInfo Model information from ModelManager
     * @note Prefer using ArbiterAI::createChatClient() factory method
     */
    ChatClient(const ChatConfig& config,
               std::shared_ptr<BaseProvider> provider,
               const ModelInfo& modelInfo);

    ~ChatClient()=default;

    // Non-copyable
    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    // Movable
    ChatClient(ChatClient&&) noexcept = default;
    ChatClient& operator=(ChatClient&&) noexcept = default;

    // ========== Completion Methods ==========

    /**
     * @brief Perform a blocking text completion
     * @param request Completion parameters (messages can be empty to use history)
     * @param[out] response Completion results
     * @return ErrorCode indicating success or failure
     *
     * If request.messages is empty, uses the current conversation history.
     * The response is automatically added to history.
     */
    ErrorCode completion(const CompletionRequest& request, CompletionResponse& response);

    /**
     * @brief Perform a streaming text completion
     * @param request Completion parameters
     * @param callback Function called for each chunk
     * @return ErrorCode indicating success or failure
     */
    ErrorCode streamingCompletion(const CompletionRequest& request, StreamCallback callback);

    // ========== Conversation Management ==========

    /**
     * @brief Add a message to the conversation history
     * @param message Message to add
     * @return ErrorCode::Success
     */
    ErrorCode addMessage(const Message& message);

    /**
     * @brief Get the current conversation history
     * @return Vector of messages in order
     */
    std::vector<Message> getHistory() const;

    /**
     * @brief Clear the conversation history
     * @return ErrorCode::Success
     *
     * Note: System prompt (if configured) will be re-added
     */
    ErrorCode clearHistory();

    /**
     * @brief Get the number of messages in history
     * @return Message count
     */
    size_t getHistorySize() const;

    // ========== Tool/Function Calling ==========

    /**
     * @brief Set available tools for the model
     * @param tools Vector of tool definitions
     * @return ErrorCode::Success
     */
    ErrorCode setTools(const std::vector<ToolDefinition>& tools);

    /**
     * @brief Get currently configured tools
     * @return Vector of tool definitions
     */
    std::vector<ToolDefinition> getTools() const;

    /**
     * @brief Clear all configured tools
     * @return ErrorCode::Success
     */
    ErrorCode clearTools();

    /**
     * @brief Add a tool result to the conversation
     * @param toolCallId ID of the tool call being responded to
     * @param result Result of the tool execution
     * @return ErrorCode::Success
     */
    ErrorCode addToolResult(const std::string& toolCallId, const std::string& result);

    // ========== Configuration ==========

    /**
     * @brief Update the temperature parameter
     * @param temperature New temperature value (0.0-2.0)
     * @return ErrorCode::Success or InvalidRequest if out of range
     */
    ErrorCode setTemperature(double temperature);

    /**
     * @brief Get the current temperature setting
     * @return Current temperature value
     */
    double getTemperature() const;

    /**
     * @brief Update the max tokens parameter
     * @param maxTokens New max tokens value
     * @return ErrorCode::Success or InvalidRequest if invalid
     */
    ErrorCode setMaxTokens(int maxTokens);

    /**
     * @brief Get the current max tokens setting
     * @return Current max tokens value
     */
    int getMaxTokens() const;

    /**
     * @brief Get the model name for this session
     * @return Model identifier string
     */
    std::string getModel() const;

    /**
     * @brief Get the provider name for this session
     * @return Provider identifier string
     */
    std::string getProvider() const;

    // ========== Download Status ==========

    /**
     * @brief Get download status for local models
     * @param[out] progress Download progress information
     * @return ErrorCode::Success
     *
     * For cloud providers, returns DownloadStatus::NotApplicable
     */
    ErrorCode getDownloadStatus(DownloadProgress& progress);

    // ========== Statistics ==========

    /**
     * @brief Get accumulated usage statistics for this session
     * @param[out] stats Usage statistics
     * @return ErrorCode::Success
     */
    ErrorCode getUsageStats(UsageStats& stats) const;

    /**
     * @brief Get the number of cached responses served
     * @return Count of cache hits
     */
    int getCachedResponseCount() const;

    /**
     * @brief Reset session statistics
     * @return ErrorCode::Success
     */
    ErrorCode resetStats();

    // ========== Session ID ==========

    /**
     * @brief Get the unique session ID
     * @return Session identifier string
     */
    std::string getSessionId() const;

private:
    /**
     * @brief Build a completion request with session context
     */
    CompletionRequest buildRequest(const CompletionRequest& userRequest) const;

    /**
     * @brief Update statistics after a completion
     */
    void updateStats(const CompletionResponse& response);

    /**
     * @brief Generate cache key for a request
     */
    std::string generateCacheKey(const CompletionRequest& request) const;

    /**
     * @brief Initialize session (add system prompt, etc.)
     */
    void initializeSession();

    // Configuration
    ChatConfig m_config;
    ModelInfo m_modelInfo;
    std::string m_sessionId;

    // Provider reference
    std::shared_ptr<BaseProvider> m_provider;

    // Session state
    std::vector<Message> m_history;
    std::vector<ToolDefinition> m_tools;
    UsageStats m_stats;

    // Session-level cache
    std::unique_ptr<CacheManager> m_cache;

    // Thread safety
    mutable std::mutex m_mutex;
};

} // namespace arbiterAI

#endif // _arbiterAI_chatClient_h_
