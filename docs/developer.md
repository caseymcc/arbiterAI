# Developer Guide

Technical reference for the ArbiterAI library.

## Table of Contents

1. [Architecture](#1-architecture)
2. [Public API](#2-public-api)
3. [Data Structures](#3-data-structures)
4. [Provider System](#4-provider-system)
5. [Utility Components](#5-utility-components)
6. [Usage Patterns](#6-usage-patterns)
7. [Configuration](#7-configuration)

---

## 1. Architecture

ArbiterAI follows a layered architecture:

```
┌──────────────────────────────────────────────────┐
│              Application Code                    │
└───────────────┬──────────────────────────────────┘
                │ creates
┌───────────────▼──────────────────────────────────┐
│  ArbiterAI (singleton / factory)                 │
│  - initialize(), createChatClient()              │
│  - Stateless convenience: completion(), etc.     │
└───────┬────────────────────┬─────────────────────┘
        │ creates             │ owns
┌───────▼────────┐  ┌────────▼─────────────────────┐
│  ChatClient    │  │  ModelManager (singleton)     │
│  (per session) │  │  - Config loading & schema    │
│  - History     │  │  - Model lookup               │
│  - Tools       │  │  - ConfigDownloader           │
│  - Stats       │  └──────────────────────────────┘
│  - Cache       │
└───────┬────────┘
        │ delegates to
┌───────▼────────────────────────────────────────┐
│  BaseProvider (strategy pattern)               │
│  ├─ OpenAI     ├─ DeepSeek    ├─ Mock          │
│  ├─ Anthropic  ├─ OpenRouter  └─ Llama (local) │
└────────────────────────────────────────────────┘
```

### Core Components

- **[`ArbiterAI`](../src/arbiterAI/arbiterAI.h)** — Singleton that acts as a factory and lifecycle manager. Initializes providers, creates `ChatClient` instances, and provides stateless convenience methods.
- **[`ChatClient`](../src/arbiterAI/chatClient.h)** — Stateful, per-session interface managing conversation history, tool definitions, caching, and usage statistics. Created via `ArbiterAI::createChatClient()`.
- **[`BaseProvider`](../src/arbiterAI/providers/baseProvider.h)** — Abstract interface for LLM backends. Concrete implementations handle provider-specific API formatting, authentication, and response parsing.
- **[`ModelManager`](../src/arbiterAI/modelManager.h)** — Singleton that loads and manages model configurations from JSON files with schema validation.
- **Utility Components** — Cross-cutting functionality including caching ([`CacheManager`](../src/arbiterAI/cacheManager.h)), cost tracking ([`CostManager`](../src/arbiterAI/costManager.h)), model downloading ([`ModelDownloader`](../src/arbiterAI/modelDownloader.h)), and file verification ([`FileVerifier`](../src/arbiterAI/fileVerifier.h)).

### Planned Components

See [Local Model Management Task](tasks/local_model_management.md) for upcoming additions:

- **`HardwareDetector`** — GPU/RAM/CPU detection (NVML + Vulkan)
- **`ModelRuntime`** — Multi-model loading, swap queueing, LRU eviction (refactor of `LlamaInterface`)
- **`TelemetryCollector`** — Inference stats and system snapshots
- **Standalone Server** — Separate `arbiterAI-server` application providing an OpenAI-compatible API, model management endpoints, and a live stats dashboard

---

## 2. Public API

### `ArbiterAI` (defined in [`arbiterAI.h`](../src/arbiterAI/arbiterAI.h))

| Method | Description |
|--------|-------------|
| `static ArbiterAI &instance()` | Get the singleton instance |
| `ErrorCode initialize(const std::vector<path> &configPaths)` | Initialize the library with config directories |
| `std::shared_ptr<ChatClient> createChatClient(const ChatConfig &config)` | Create a stateful chat session |
| `std::shared_ptr<ChatClient> createChatClient(const std::string &model)` | Create a chat session with default config |
| `bool doesModelNeedApiKey(const std::string &model)` | Check if model requires API key |
| `bool supportModelDownload(const std::string &provider)` | Check if provider supports downloads |
| `ErrorCode getModelInfo(const std::string &modelName, ModelInfo &info)` | Get model information |
| `ErrorCode getAvailableModels(std::vector<std::string> &models)` | List available models |
| `ErrorCode completion(const CompletionRequest &request, CompletionResponse &response)` | Stateless completion (convenience) |
| `ErrorCode streamingCompletion(const CompletionRequest &request, callback)` | Stateless streaming completion |
| `std::vector<CompletionResponse> batchCompletion(const std::vector<CompletionRequest> &requests)` | Batch completion |
| `ErrorCode getEmbeddings(const EmbeddingRequest &request, EmbeddingResponse &response)` | Generate embeddings |
| `ErrorCode getDownloadStatus(const std::string &modelName, std::string &error)` | Get model download status |
| `ErrorCode shutdown()` | Clean up resources |

### `ChatClient` (defined in [`chatClient.h`](../src/arbiterAI/chatClient.h))

**Completion:**

| Method | Description |
|--------|-------------|
| `ErrorCode completion(const CompletionRequest &request, CompletionResponse &response)` | Blocking completion with session context |
| `ErrorCode streamingCompletion(const CompletionRequest &request, StreamCallback callback)` | Streaming completion |

**Conversation Management:**

| Method | Description |
|--------|-------------|
| `ErrorCode addMessage(const Message &message)` | Add a message to history |
| `std::vector<Message> getHistory() const` | Get conversation history |
| `ErrorCode clearHistory()` | Clear history (re-adds system prompt) |
| `size_t getHistorySize() const` | Get message count |

**Tool/Function Calling:**

| Method | Description |
|--------|-------------|
| `ErrorCode setTools(const std::vector<ToolDefinition> &tools)` | Set available tools |
| `std::vector<ToolDefinition> getTools() const` | Get configured tools |
| `ErrorCode clearTools()` | Clear all tools |
| `ErrorCode addToolResult(const std::string &toolCallId, const std::string &result)` | Add tool result to conversation |

**Configuration:**

| Method | Description |
|--------|-------------|
| `ErrorCode setTemperature(double temperature)` | Set temperature (0.0-2.0) |
| `double getTemperature() const` | Get current temperature |
| `ErrorCode setMaxTokens(int maxTokens)` | Set max tokens |
| `int getMaxTokens() const` | Get max tokens |
| `std::string getModel() const` | Get model name |
| `std::string getProvider() const` | Get provider name |

**Status & Statistics:**

| Method | Description |
|--------|-------------|
| `ErrorCode getDownloadStatus(DownloadProgress &progress)` | Get download progress (local models) |
| `ErrorCode getUsageStats(UsageStats &stats) const` | Get accumulated usage statistics |
| `int getCachedResponseCount() const` | Get cache hit count |
| `ErrorCode resetStats()` | Reset session statistics |
| `std::string getSessionId() const` | Get unique session ID |

### Session Lifecycle

1. Initialize `ArbiterAI` with configuration paths
2. Create a `ChatClient` via `createChatClient()`
3. Optionally set tools, temperature, etc.
4. Call `completion()` or `streamingCompletion()` — messages are automatically added to history
5. Query stats, history, or download status as needed
6. A new chat restart requires creating a new `ChatClient` instance

---

## 3. Data Structures

All data structures are defined in [`arbiterAI.h`](../src/arbiterAI/arbiterAI.h) unless noted.

### `ErrorCode`

```cpp
enum class ErrorCode {
    Success, ApiKeyNotFound, UnknownModel, UnsupportedProvider,
    NetworkError, InvalidResponse, InvalidRequest, NotImplemented,
    GenerationError, ModelNotFound, ModelNotLoaded, ModelLoadError,
    ModelDownloading, ModelDownloadFailed
};
```

### `Message`

```cpp
struct Message {
    std::string role;       // "system", "user", "assistant", "tool"
    std::string content;
};
```

### `ChatConfig` (defined in [`chatClient.h`](../src/arbiterAI/chatClient.h))

| Field | Type | Description |
|-------|------|-------------|
| `model` | `std::string` | Model identifier (required) |
| `temperature` | `std::optional<double>` | Sampling temperature |
| `maxTokens` | `std::optional<int>` | Max tokens per completion |
| `systemPrompt` | `std::optional<std::string>` | System message |
| `apiKey` | `std::optional<std::string>` | API key override |
| `enableCache` | `bool` | Enable session-level caching (default: `false`) |
| `cacheTTL` | `std::chrono::seconds` | Cache time-to-live (default: 3600) |
| `topP` | `std::optional<double>` | Top-p sampling parameter |
| `presencePenalty` | `std::optional<double>` | Presence penalty |
| `frequencyPenalty` | `std::optional<double>` | Frequency penalty |

### `CompletionRequest`

| Field | Type | Description |
|-------|------|-------------|
| `model` | `std::string` | Model identifier |
| `messages` | `std::vector<Message>` | Conversation messages |
| `temperature` | `std::optional<double>` | Sampling temperature |
| `max_tokens` | `std::optional<int>` | Maximum tokens |
| `api_key` | `std::optional<std::string>` | API key override |
| `provider` | `std::optional<std::string>` | Provider override |
| `top_p` | `std::optional<double>` | Top-p sampling |
| `presence_penalty` | `std::optional<double>` | Presence penalty |
| `frequency_penalty` | `std::optional<double>` | Frequency penalty |
| `stop` | `std::optional<std::vector<std::string>>` | Stop sequences |
| `tools` | `std::optional<std::vector<ToolDefinition>>` | Available tools |
| `tool_choice` | `std::optional<std::string>` | Tool selection mode |

### `CompletionResponse`

| Field | Type | Description |
|-------|------|-------------|
| `text` | `std::string` | Generated text |
| `model` | `std::string` | Model used |
| `usage` | `Usage` | Token usage statistics |
| `provider` | `std::string` | Provider used |
| `cost` | `double` | Estimated cost |
| `toolCalls` | `std::vector<ToolCall>` | Tool calls from model |
| `finishReason` | `std::string` | Reason completion finished |
| `fromCache` | `bool` | Whether served from cache |

### `Usage`

```cpp
struct Usage {
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;
};
```

### `UsageStats`

| Field | Type | Description |
|-------|------|-------------|
| `promptTokens` | `int` | Total prompt tokens |
| `completionTokens` | `int` | Total completion tokens |
| `totalTokens` | `int` | Combined count |
| `estimatedCost` | `double` | Session cost estimate |
| `cachedResponses` | `int` | Cache hits |
| `completionCount` | `int` | Number of completions |

### Tool Structures

**`ToolParameter`:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | `std::string` | Parameter name |
| `type` | `std::string` | Type (string, number, boolean, object, array) |
| `description` | `std::string` | Description for the LLM |
| `required` | `bool` | Whether required |
| `schema` | `nlohmann::json` | Full JSON schema for complex types |

**`ToolDefinition`:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | `std::string` | Tool name |
| `description` | `std::string` | Description for the LLM |
| `parameters` | `std::vector<ToolParameter>` | Parameter definitions |
| `parametersSchema` | `nlohmann::json` | Full JSON schema |

**`ToolCall`:**

| Field | Type | Description |
|-------|------|-------------|
| `id` | `std::string` | Unique call identifier |
| `name` | `std::string` | Tool/function name |
| `arguments` | `nlohmann::json` | Arguments passed |

### Download Structures

**`DownloadStatus`:**

```cpp
enum class DownloadStatus {
    NotApplicable, NotStarted, Pending, InProgress, Completed, Failed
};
```

**`DownloadProgress`:**

| Field | Type | Description |
|-------|------|-------------|
| `status` | `DownloadStatus` | Current status |
| `bytesDownloaded` | `int64_t` | Bytes downloaded |
| `totalBytes` | `int64_t` | Total file size |
| `percentComplete` | `float` | Percentage (0-100) |
| `errorMessage` | `std::string` | Error details if failed |
| `modelName` | `std::string` | Model being downloaded |

### Embedding Structures

**`EmbeddingRequest`:**

| Field | Type | Description |
|-------|------|-------------|
| `model` | `std::string` | Model identifier |
| `input` | `std::variant<std::string, std::vector<std::string>>` | Text to embed |

**`EmbeddingResponse`:**

| Field | Type | Description |
|-------|------|-------------|
| `model` | `std::string` | Model used |
| `data` | `std::vector<Embedding>` | Embedding vectors with indices |
| `usage` | `Usage` | Token usage |

### Model Configuration (defined in [`modelManager.h`](../src/arbiterAI/modelManager.h))

**`ModelInfo`:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `model` | `std::string` | | Model identifier |
| `provider` | `std::string` | | Provider type |
| `mode` | `std::string` | `"chat"` | Operation mode |
| `configVersion` | `std::string` | `"1.1.0"` | Schema version |
| `minSchemaVersion` | `std::string` | `"1.0.0"` | Minimum compatible version |
| `ranking` | `int` | `50` | Priority ranking (0-100) |
| `apiBase` | `std::optional<std::string>` | | Custom API endpoint |
| `filePath` | `std::optional<std::string>` | | Local model file path |
| `apiKey` | `std::optional<std::string>` | | API key |
| `download` | `std::optional<DownloadMetadata>` | | Download URL + SHA256 |
| `contextWindow` | `int` | `4096` | Context window size |
| `maxTokens` | `int` | `2048` | Max tokens |
| `maxInputTokens` | `int` | `3072` | Max input tokens |
| `maxOutputTokens` | `int` | `1024` | Max output tokens |
| `pricing` | `Pricing` | | Token costs |

**`DownloadMetadata`:**

| Field | Type | Description |
|-------|------|-------------|
| `url` | `std::string` | Download URL |
| `sha256` | `std::string` | File hash for verification |
| `cachePath` | `std::string` | Local cache path |

**`Pricing`:**

| Field | Type | Description |
|-------|------|-------------|
| `prompt_token_cost` | `double` | Cost per prompt token |
| `completion_token_cost` | `double` | Cost per completion token |

---

## 4. Provider System

The provider system abstracts LLM backends using a strategy pattern.

### `BaseProvider` (defined in [`baseProvider.h`](../src/arbiterAI/providers/baseProvider.h))

| Method | Description |
|--------|-------------|
| `virtual ErrorCode completion(request, model, response) = 0` | Text completion |
| `virtual ErrorCode streamingCompletion(request, callback) = 0` | Streaming completion |
| `virtual std::vector<CompletionResponse> batchCompletion(requests)` | Batch completion |
| `virtual ErrorCode getEmbeddings(request, response) = 0` | Generate embeddings |
| `virtual DownloadStatus getDownloadStatus(modelName, error)` | Legacy download status |
| `virtual ErrorCode getDownloadProgress(modelName, progress)` | Detailed download progress |
| `virtual ErrorCode getAvailableModels(models)` | List provider models |
| `std::string getProviderName() const` | Get provider name |
| `virtual void setApiUrl(const std::string &url)` | Set API endpoint |
| `virtual void setApiKey(const std::string &key)` | Set API key |

### Cloud Providers

[`OpenAI`](../src/arbiterAI/providers/openai.h), [`Anthropic`](../src/arbiterAI/providers/anthropic.h), [`DeepSeek`](../src/arbiterAI/providers/deepseek.h), and [`OpenRouter`](../src/arbiterAI/providers/openrouter.h) implement `BaseProvider` by making HTTP requests to their respective APIs. They handle:

- Authentication via API keys (environment variables or config)
- Request/response format translation to/from unified structures
- Streaming via Server-Sent Events (SSE)
- Tool/function calling support

### Local Provider

[`Llama`](../src/arbiterAI/providers/llama.h) delegates to [`LlamaInterface`](../src/arbiterAI/providers/llamaInterface.h) for local model inference via llama.cpp. Currently disabled in the build. See the [Local Model Management Task](tasks/local_model_management.md) for the planned refactor into `ModelRuntime` with multi-model and multi-GPU support.

### Testing Provider

[`Mock`](../src/arbiterAI/providers/mock.h) provides deterministic responses via `<echo>` tags for testing. See the [Testing Guide](testing.md) for details.

### Adding a New Provider

1. Create `src/arbiterAI/providers/newProvider.h/cpp`
2. Inherit from `BaseProvider` and implement pure virtual methods
3. Register the provider name in `ArbiterAI::createProvider()`
4. Add model configurations to your config files
5. Add the provider to the schema's provider enum in `schemas/model_config.schema.json`

---

## 5. Utility Components

### `CacheManager` ([`cacheManager.h`](../src/arbiterAI/cacheManager.h))

TTL-based response caching:
- Session-scope caching (per `ChatClient` when `enableCache` is set)
- Global-scope caching (via `ArbiterAI`)
- Configurable time-to-live
- Cache key generation from request content

### `CostManager` ([`costManager.h`](../src/arbiterAI/costManager.h))

Spending tracking and limits:
- Per-session and global spending limits
- Callback when limits are reached
- Cost state persistence across restarts
- Per-model cost calculation based on `Pricing`

### `ModelDownloader` ([`modelDownloader.h`](../src/arbiterAI/modelDownloader.h))

Async model downloading:
- Progress callback support
- Resume interrupted downloads
- SHA256 file verification via `FileVerifier`
- GitHub API integration for config downloads
- Asynchronous downloading via `std::future`

### `ConfigDownloader` ([`configDownloader.h`](../src/arbiterAI/configDownloader.h))

Remote configuration fetching (skeleton — being fleshed out for the config repo integration):
- Git-based clone/pull via libgit2
- Version/tag pinning
- Fallback to local cache

### `FileVerifier` ([`fileVerifier.h`](../src/arbiterAI/fileVerifier.h))

SHA256 file verification:
- Interface `IFileVerifier` for testability
- `FileVerifier` implementation using PicoSHA2

### `ModelManager` ([`modelManager.h`](../src/arbiterAI/modelManager.h))

Model configuration management:
- JSON config loading with schema validation
- Layered configuration (remote, local, override)
- Model lookup by name or provider
- Ranking-based model ordering

---

## 6. Usage Patterns

### Basic Completion (via ChatClient)

```cpp
#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"

auto &ai = arbiterAI::ArbiterAI::instance();
ai.initialize({"config/"});

arbiterAI::ChatConfig config;
config.model = "gpt-4";
config.temperature = 0.7;

auto client = ai.createChatClient(config);

arbiterAI::CompletionRequest request;
request.messages = {{"user", "Hello!"}};

arbiterAI::CompletionResponse response;
client->completion(request, response);
// response.text contains the reply
// Message is automatically added to history
```

### Streaming

```cpp
auto callback = [](const std::string &chunk, bool done) {
    std::cout << chunk;
    if (done) std::cout << std::endl;
};

client->streamingCompletion(request, callback);
```

### Tool Calling

```cpp
arbiterAI::ToolDefinition tool;
tool.name = "get_weather";
tool.description = "Get current weather";
tool.parameters = {{"location", "string", "City name", true, {}}};

client->setTools({tool});

// After completion, check for tool calls:
if (!response.toolCalls.empty())
{
    for (const auto &call : response.toolCalls)
    {
        std::string result = executeMyTool(call.name, call.arguments);
        client->addToolResult(call.id, result);
    }
    // Continue the conversation
    arbiterAI::CompletionRequest followUp;
    client->completion(followUp, response);
}
```

### Stateless Convenience

```cpp
arbiterAI::CompletionRequest request;
request.model = "gpt-4";
request.messages = {{"user", "Quick question"}};

arbiterAI::CompletionResponse response;
ai.completion(request, response);
```

---

## 7. Configuration

### Model Configuration Files

Models are defined in JSON files validated against [`schemas/model_config.schema.json`](../schemas/model_config.schema.json). See [`examples/model_config_v2.json`](../examples/model_config_v2.json) for an example.

### Environment Variables

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI API key |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `DEEPSEEK_API_KEY` | DeepSeek API key |
| `OPENROUTER_API_KEY` | OpenRouter API key |

### Configuration Precedence

1. **Request-level** — API key or provider override in `CompletionRequest`
2. **Session-level** — Settings in `ChatConfig`
3. **File-level** — Model config JSON files
4. **Environment** — Environment variables for API keys

---

## Further Reading

- [Project Overview](project.md) — Goals, features, and third-party libraries
- [Testing Guide](testing.md) — Mock provider and testing strategies
- [Development Process](development.md) — Build instructions and project structure
- [Local Model Management](tasks/local_model_management.md) — Planned llama.cpp expansion and standalone server
