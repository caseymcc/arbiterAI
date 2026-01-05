/**
 * @file mock.cpp
 * @brief Implementation of Mock provider for testing
 */

#include "arbiterAI/providers/mock.h"
#include <algorithm>
#include <thread>
#include <chrono>

namespace arbiterAI
{

Mock::Mock()
    : BaseProvider("mock")
{
}

ErrorCode Mock::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    // Extract all message content to search for echo tags
    std::string allContent;
    for (const auto &msg : request.messages)
    {
        allContent += msg.content + "\n";
    }

    // Try to extract echo content
    std::string echoContent;
    if (extractEchoContent(allContent, echoContent))
    {
        response.text = echoContent;
    }
    else
    {
        // No echo tags found, use default response
        response.text = DEFAULT_RESPONSE;
    }

    // Calculate simulated token usage
    response.usage = calculateMockUsage(allContent, response.text);
    response.model = request.model;
    response.fromCache = false;

    return ErrorCode::Success;
}

ErrorCode Mock::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    // Extract all message content to search for echo tags
    std::string allContent;
    for (const auto &msg : request.messages)
    {
        allContent += msg.content + "\n";
    }

    // Try to extract echo content
    std::string echoContent;
    if (extractEchoContent(allContent, echoContent))
    {
        // Stream the echo content in chunks
        size_t pos = 0;
        while (pos < echoContent.length())
        {
            size_t chunkSize = std::min(STREAM_CHUNK_SIZE, echoContent.length() - pos);
            std::string chunk = echoContent.substr(pos, chunkSize);
            callback(chunk);
            pos += chunkSize;

            // Small delay to simulate streaming behavior
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    else
    {
        // No echo tags found, stream default response
        size_t pos = 0;
        std::string defaultResp = DEFAULT_RESPONSE;
        while (pos < defaultResp.length())
        {
            size_t chunkSize = std::min(STREAM_CHUNK_SIZE, defaultResp.length() - pos);
            std::string chunk = defaultResp.substr(pos, chunkSize);
            callback(chunk);
            pos += chunkSize;

            // Small delay to simulate streaming behavior
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Send final empty chunk to signal completion (if the provider expects it)
    callback("");

    return ErrorCode::Success;
}

ErrorCode Mock::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    // Embeddings not needed for testing completions
    return ErrorCode::NotImplemented;
}

ErrorCode Mock::getAvailableModels(std::vector<std::string>& models)
{
    models.clear();
    models.push_back("mock-model");
    return ErrorCode::Success;
}

bool Mock::extractEchoContent(const std::string &message, std::string &echoContent) const
{
    // Use regex to find <echo>...</echo> tags (supporting multiline and greedy matching)
    std::regex echoPattern(R"(<echo>([\s\S]*?)</echo>)", std::regex::icase);
    std::smatch match;

    if (std::regex_search(message, match, echoPattern))
    {
        if (match.size() > 1)
        {
            echoContent = match[1].str();
            
            // Trim leading/trailing whitespace
            auto start = echoContent.find_first_not_of(" \t\n\r");
            auto end = echoContent.find_last_not_of(" \t\n\r");
            
            if (start != std::string::npos && end != std::string::npos)
            {
                echoContent = echoContent.substr(start, end - start + 1);
            }
            else if (start == std::string::npos)
            {
                echoContent = ""; // All whitespace
            }
            
            return true;
        }
    }

    return false;
}

Usage Mock::calculateMockUsage(const std::string &prompt, const std::string &completion) const
{
    Usage usage;
    
    // Simulate token counting (rough approximation: 1 token ≈ 4 characters)
    usage.prompt_tokens = static_cast<int>(prompt.length() / 4);
    usage.completion_tokens = static_cast<int>(completion.length() / 4);
    usage.total_tokens = usage.prompt_tokens + usage.completion_tokens;

    return usage;
}

} // namespace arbiterAI
