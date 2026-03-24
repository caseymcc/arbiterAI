/**
 * @file chatClient.cpp
 * @brief Implementation of ChatClient class
 */

#include "arbiterAI/chatClient.h"
#include "arbiterAI/providers/baseProvider.h"
#include "arbiterAI/cacheManager.h"

#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace arbiterAI
{

namespace
{
    /**
     * @brief Generate a unique session ID
     */
    std::string generateSessionId()
    {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 0xFFFF);

        std::stringstream ss;
        ss << "chat_" << std::hex << timestamp << "_" << dis(gen);
        return ss.str();
    }
}

ChatClient::ChatClient(const ChatConfig& config,
                       std::shared_ptr<BaseProvider> provider,
                       const ModelInfo& modelInfo)
    : m_config(config)
    , m_modelInfo(modelInfo)
    , m_sessionId(generateSessionId())
    , m_provider(provider)
    , m_stats{}
{
    // Initialize session-level cache if enabled
    if (m_config.enableCache)
    {
        std::filesystem::path cachePath = std::filesystem::temp_directory_path() / "arbiterai_cache" / m_sessionId;
        m_cache = std::make_unique<CacheManager>(cachePath, m_config.cacheTTL);
    }

    initializeSession();
}

void ChatClient::initializeSession()
{
    // Add system prompt if configured
    if (m_config.systemPrompt.has_value() && !m_config.systemPrompt.value().empty())
    {
        Message systemMsg;
        systemMsg.role = "system";
        systemMsg.content = m_config.systemPrompt.value();
        m_history.push_back(systemMsg);
    }
}

// ========== Completion Methods ==========

ErrorCode ChatClient::completion(const CompletionRequest& request, CompletionResponse& response)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Build the full request with session context
    CompletionRequest fullRequest = buildRequest(request);

    // Check cache first if enabled
    if (m_cache)
    {
        auto cached = m_cache->get(fullRequest);
        if (cached.has_value())
        {
            response = cached.value();
            response.fromCache = true;
            m_stats.cachedResponses++;
            return ErrorCode::Success;
        }
    }

    // Perform completion via provider
    ErrorCode result = m_provider->completion(fullRequest, m_modelInfo, response);

    if (result == ErrorCode::Success)
    {
        // Add user messages to history if they were in the request
        for (const auto& msg : request.messages)
        {
            if (msg.role != "system")  // Don't duplicate system messages
            {
                m_history.push_back(msg);
            }
        }

        // Add assistant response to history
        if (!response.text.empty())
        {
            Message assistantMsg;
            assistantMsg.role = "assistant";
            assistantMsg.content = response.text;
            m_history.push_back(assistantMsg);
        }

        // Update statistics
        updateStats(response);

        // Cache the response if enabled
        if (m_cache)
        {
            m_cache->put(fullRequest, response);
        }
    }

    return result;
}

ErrorCode ChatClient::streamingCompletion(const CompletionRequest& request, StreamCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Build the full request with session context
    CompletionRequest fullRequest = buildRequest(request);

    // Accumulate the full response for history
    std::string fullResponse;

    // Wrap the callback to accumulate response
    auto wrappedCallback = [&fullResponse, &callback](const std::string& chunk)
    {
        fullResponse += chunk;
        callback(chunk, false);
    };

    // Perform streaming completion via provider
    ErrorCode result = m_provider->streamingCompletion(fullRequest, wrappedCallback);

    if (result == ErrorCode::Success)
    {
        // Signal completion
        callback("", true);

        // Add user messages to history
        for (const auto& msg : request.messages)
        {
            if (msg.role != "system")
            {
                m_history.push_back(msg);
            }
        }

        // Add assistant response to history
        if (!fullResponse.empty())
        {
            Message assistantMsg;
            assistantMsg.role = "assistant";
            assistantMsg.content = fullResponse;
            m_history.push_back(assistantMsg);
        }

        // Update basic stats (token counts may not be accurate for streaming)
        m_stats.completionCount++;
    }

    return result;
}

CompletionRequest ChatClient::buildRequest(const CompletionRequest& userRequest) const
{
    CompletionRequest fullRequest;

    // Set model
    fullRequest.model = m_config.model;

    // Build message list: history + new messages
    fullRequest.messages = m_history;
    for (const auto& msg : userRequest.messages)
    {
        fullRequest.messages.push_back(msg);
    }

    // Apply session configuration (can be overridden by request)
    fullRequest.temperature = userRequest.temperature.has_value()
        ? userRequest.temperature
        : m_config.temperature;

    fullRequest.max_tokens = userRequest.max_tokens.has_value()
        ? userRequest.max_tokens
        : m_config.maxTokens;

    fullRequest.api_key = userRequest.api_key.has_value()
        ? userRequest.api_key
        : m_config.apiKey;

    fullRequest.top_p = userRequest.top_p.has_value()
        ? userRequest.top_p
        : m_config.topP;

    fullRequest.presence_penalty = userRequest.presence_penalty.has_value()
        ? userRequest.presence_penalty
        : m_config.presencePenalty;

    fullRequest.frequency_penalty = userRequest.frequency_penalty.has_value()
        ? userRequest.frequency_penalty
        : m_config.frequencyPenalty;

    // Add tools if configured
    if (!m_tools.empty())
    {
        fullRequest.tools = m_tools;
    }
    else if (userRequest.tools.has_value())
    {
        fullRequest.tools = userRequest.tools;
    }

    fullRequest.tool_choice = userRequest.tool_choice;
    fullRequest.stop = userRequest.stop;

    return fullRequest;
}

void ChatClient::updateStats(const CompletionResponse& response)
{
    m_stats.promptTokens += response.usage.prompt_tokens;
    m_stats.completionTokens += response.usage.completion_tokens;
    m_stats.totalTokens += response.usage.total_tokens;
    m_stats.estimatedCost += response.cost;
    m_stats.completionCount++;
}

// ========== Conversation Management ==========

ErrorCode ChatClient::addMessage(const Message& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.push_back(message);
    return ErrorCode::Success;
}

std::vector<Message> ChatClient::getHistory() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history;
}

ErrorCode ChatClient::clearHistory()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();

    // Re-add system prompt if configured
    if (m_config.systemPrompt.has_value() && !m_config.systemPrompt.value().empty())
    {
        Message systemMsg;
        systemMsg.role = "system";
        systemMsg.content = m_config.systemPrompt.value();
        m_history.push_back(systemMsg);
    }

    return ErrorCode::Success;
}

size_t ChatClient::getHistorySize() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history.size();
}

// ========== Tool/Function Calling ==========

ErrorCode ChatClient::setTools(const std::vector<ToolDefinition>& tools)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tools = tools;
    return ErrorCode::Success;
}

std::vector<ToolDefinition> ChatClient::getTools() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tools;
}

ErrorCode ChatClient::clearTools()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tools.clear();
    return ErrorCode::Success;
}

ErrorCode ChatClient::addToolResult(const std::string& toolCallId, const std::string& result)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    Message toolResultMsg;
    toolResultMsg.role = "tool";
    toolResultMsg.content = result;
    // Note: Some APIs require tool_call_id in the message
    // This could be extended to support that via Message struct

    m_history.push_back(toolResultMsg);
    return ErrorCode::Success;
}

// ========== Configuration ==========

ErrorCode ChatClient::setTemperature(double temperature)
{
    if (temperature < 0.0 || temperature > 2.0)
    {
        return ErrorCode::InvalidRequest;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.temperature = temperature;
    return ErrorCode::Success;
}

double ChatClient::getTemperature() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config.temperature.value_or(1.0);
}

ErrorCode ChatClient::setMaxTokens(int maxTokens)
{
    if (maxTokens <= 0)
    {
        return ErrorCode::InvalidRequest;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.maxTokens = maxTokens;
    return ErrorCode::Success;
}

int ChatClient::getMaxTokens() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config.maxTokens.value_or(m_modelInfo.maxTokens);
}

std::string ChatClient::getModel() const
{
    return m_config.model;
}

std::string ChatClient::getProvider() const
{
    return m_modelInfo.provider;
}

ErrorCode ChatClient::switchModel(const std::string &newModel)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Look up the new model
    std::optional<ModelInfo> modelInfo = ModelManager::instance().getModelInfo(newModel);

    if(!modelInfo)
    {
        return ErrorCode::UnknownModel;
    }

    // Get or create the provider for the new model
    auto provider = ArbiterAI::instance().getSharedProvider(modelInfo->provider);

    if(!provider)
    {
        return ErrorCode::UnsupportedProvider;
    }

    // Update session state — preserve history and tools
    m_config.model = newModel;
    m_modelInfo = *modelInfo;
    m_provider = provider;

    // Reset stats for the new model
    m_stats = UsageStats{};

    return ErrorCode::Success;
}

// ========== Download Status ==========

ErrorCode ChatClient::getDownloadStatus(DownloadProgress& progress)
{
    std::string error;
    DownloadStatus status = m_provider->getDownloadStatus(m_config.model, error);

    progress.status = status;
    progress.modelName = m_config.model;
    progress.errorMessage = error;

    // Note: Detailed progress (bytes, percent) would need to be tracked by the provider
    // This is a simplified implementation
    switch (status)
    {
        case DownloadStatus::NotApplicable:
        case DownloadStatus::NotStarted:
            progress.bytesDownloaded = 0;
            progress.totalBytes = 0;
            progress.percentComplete = 0.0f;
            break;
        case DownloadStatus::Completed:
            progress.percentComplete = 100.0f;
            break;
        case DownloadStatus::Failed:
            progress.percentComplete = 0.0f;
            break;
        default:
            break;
    }

    return ErrorCode::Success;
}

// ========== Statistics ==========

ErrorCode ChatClient::getUsageStats(UsageStats& stats) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    stats = m_stats;
    return ErrorCode::Success;
}

int ChatClient::getCachedResponseCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats.cachedResponses;
}

ErrorCode ChatClient::resetStats()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats = UsageStats{};
    return ErrorCode::Success;
}

// ========== Session ID ==========

std::string ChatClient::getSessionId() const
{
    return m_sessionId;
}

std::string ChatClient::generateCacheKey(const CompletionRequest& request) const
{
    // Generate a cache key based on the request
    nlohmann::json j = request;
    return std::to_string(std::hash<std::string>{}(j.dump()));
}

} // namespace arbiterAI
