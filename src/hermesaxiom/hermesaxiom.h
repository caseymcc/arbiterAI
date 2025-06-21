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
    NotImplemented,
    ModelNotLoaded,
    GenerationError
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
    std::optional<std::string> provider;
};

struct CompletionResponse
{
    std::string text;
    std::string model;
    int tokens_used;
    std::string provider;  // "openai", "anthropic", etc.
};

struct EmbeddingRequest
{
    std::string model;
    std::string input;
    std::optional<std::string> api_key;
    std::optional<std::string> provider;
};

struct EmbeddingResponse
{
    std::vector<float> embedding;
    std::string model;
    int tokens_used;
    std::string provider;
};

// Library initialization
ErrorCode initialize(const std::vector<std::filesystem::path> &configPaths);

// Check if a model requires an API key
bool doesModelNeedApiKey(const std::string &model);

// Main completion function (similar to litellm.completion)
ErrorCode completion(const CompletionRequest &request, CompletionResponse &response);

// Streaming completion function
ErrorCode streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback);

// Embedding function
ErrorCode getEmbeddings(const EmbeddingRequest &request, EmbeddingResponse &response);

}//namespace hermesaxiom

#endif//_hermesaxiom_hermes_h
