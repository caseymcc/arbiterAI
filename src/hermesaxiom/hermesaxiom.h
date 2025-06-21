#ifndef _hermesaxiom_hermes_h_
#define _hermesaxiom_hermes_h_

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <functional>

namespace hermesaxiom
{

enum class ErrorCode
{
    Success=0,
    ApiKeyNotFound,
    UnknownModel,
    UnsupportedProvider,
    NetworkError,
    InvalidResponse,
    InvalidRequest,
    NotImplemented
};

struct Message
{
    std::string role;
    std::string content;
};

struct CompletionRequest
{
    std::string model;
    std::vector<Message> messages;
    std::optional<float> temperature;
    std::optional<int> max_tokens;
    std::optional<std::string> api_key;
};

struct CompletionResponse
{
    std::string text;
    std::string model;
    int tokens_used;
    std::string provider;  // "openai", "anthropic", etc.
};

// Library initialization
ErrorCode initialize(const std::vector<std::filesystem::path> &configPaths);

// Check if a model requires an API key
bool doesModelNeedApiKey(const std::string &model);

// Main completion function (similar to litellm.completion)
ErrorCode completion(const CompletionRequest &request, CompletionResponse &response);

// Streaming completion function
ErrorCode streamingCompletion(const CompletionRequest &request, 
    std::function<void(const std::string&)> callback);

}//namespace hermesaxiom

#endif//_hermesaxiom_hermes_h
