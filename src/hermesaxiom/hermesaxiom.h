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

enum class DownloadStatus
{
    NotStarted,
    InProgress,
    Completed,
    Failed
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
    std::variant<std::string, std::vector<std::string>> input;
};

struct Embedding
{
    int index;
    std::vector<float> embedding;
};

struct Usage
{
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;
};

struct EmbeddingResponse
{
    std::string model;
    std::vector<Embedding> data;
    Usage usage;
};

class hermesaxiom
{
public:
    hermesaxiom();
    ~hermesaxiom();

    ErrorCode initialize(const std::vector<std::filesystem::path> &configPaths);
    bool doesModelNeedApiKey(const std::string &model);
    bool supportModelDownload(const std::string &provider);
    ErrorCode completion(const CompletionRequest &request, CompletionResponse &response);
    ErrorCode streamingCompletion(const CompletionRequest &request,
        std::function<void(const std::string &)> callback);
    ErrorCode getEmbeddings(const EmbeddingRequest &request, EmbeddingResponse &response);
    ErrorCode getDownloadStatus(const std::string &modelName, std::string &error);
};

}//namespace hermesaxiom

#endif//_hermesaxiom_hermes_h
