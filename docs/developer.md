# ArbiterAI Project Overview

This document provides a comprehensive overview of the `ArbiterAI` library, detailing its architecture, public API, configuration system, and core components.

### 1. Project Structure and Architecture

The `ArbiterAI` library is built around a modular and stateful architecture, with the [`ChatClient`](src/arbiterAI/chatClient.h) class acting as the primary entry point for chat interactions. This design moves away from the traditional stateless text-in/text-out completion interface to a session-oriented approach where each chat instance maintains its own state, context, and configuration.

The core architectural components are:

*   **[`ChatClient`](src/arbiterAI/chatClient.h):** The primary interface for chat interactions. Each client instance is created per chat session and maintains:
    *   Conversation state and message history
    *   Model-specific configuration and parameters
    *   Tool/function calling state and definitions
    *   Response caching for the session
    *   Download status and progress for local models (via the associated provider)
    *   Provides completion and streaming completion methods
    *   Manages the lifecycle of a single chat conversation
*   **[`ArbiterAI` Singleton](src/arbiterAI/arbiterAI.h):** Acts as a factory and lifecycle manager. Its responsibilities include:
    *   Initializing the library and utility managers
    *   Creating and managing `ChatClient` instances
    *   Coordinating provider instantiation and lifecycle
    *   Managing global configuration and utilities
*   **[`ModelManager`](src/arbiterAI/modelManager.h):** A singleton responsible for loading, parsing, and managing model configurations from local and remote sources. It acts as a service locator, enabling the `ChatClient` to find the appropriate provider for a given model.
*   **Provider System ([`BaseProvider`](src/arbiterAI/providers/baseProvider.h)):** A strategy pattern implementation where a common interface, `BaseProvider`, defines the contract for interacting with different LLM backends. Each `ChatClient` instance maintains a reference to its associated provider. Concrete implementations handle:
    *   Service-specific API interactions (e.g., OpenAI, Anthropic)
    *   Local model loading and inference (Llama.cpp)
    *   Model download status tracking for local models
    *   Tool/function execution support
*   **Utility Components:** A collection of helper classes that provide cross-cutting functionality such as caching ([`CacheManager`](src/arbiterAI/cacheManager.h:13)), cost tracking ([`CostManager`](src/arbiterAI/costManager.h)), and secure file downloading ([`ModelDownloader`](src/arbiterAI/modelDownloader.h), [`FileVerifier`](src/arbiterAI/fileVerifier.h)).

A typical request flow involves:
1. Application calls `ArbiterAI::createChatClient()` to create a new `ChatClient` instance for a chat session
2. The `ChatClient` consults the `ModelManager` to get model information and obtains a provider instance
3. Application interacts with the `ChatClient` for completions, which maintains state across requests
4. The `ChatClient` can query download status from its provider for local models
5. When the chat session ends, the `ChatClient` is destroyed, cleaning up session state
6. A new chat restart requires creating a new `ChatClient` instance

### 2. Public API

The public API is exposed through two main classes: the [`ArbiterAI`](src/arbiterAI/arbiterAI.h) singleton for library initialization and factory functions, and the [`ChatClient`](src/arbiterAI/chatClient.h) class for chat interactions.

**Main Class: `ArbiterAI` (Factory & Lifecycle)**

*   **`ArbiterAI& instance()`:** Retrieves the singleton instance.
*   **`ErrorCode initialize(...)`:** Initializes the library with model configurations. Must be called first.
*   **`std::shared_ptr<ChatClient> createChatClient(const ChatConfig& config)`:** Factory method to create a new `ChatClient` instance for a chat session. Each client maintains its own state and should be created per chat session.
*   **`ErrorCode getModelInfo(...)`:** Retrieves information about available models.
*   **`ErrorCode shutdown()`:** Cleanly shuts down the library and releases resources.

**Chat Interface: `ChatClient`**

Each client application should maintain an instance of `ChatClient` for chat interactions. A new instance should be created when the chat restarts.

*   **`ErrorCode completion(CompletionRequest& request, CompletionResponse& response)`:** Executes a blocking text completion request using the client's session context.
*   **`ErrorCode streamingCompletion(CompletionRequest& request, StreamCallback callback)`:** Executes a non-blocking completion request, streaming the response via a callback while maintaining session state.
*   **`ErrorCode addMessage(const Message& message)`:** Adds a message to the conversation history.
*   **`std::vector<Message> getHistory()`:** Retrieves the current conversation history.
*   **`ErrorCode clearHistory()`:** Clears the conversation history.
*   **`ErrorCode setTools(const std::vector<ToolDefinition>& tools)`:** Configures function/tool calling capabilities for this session.
*   **`ErrorCode getDownloadStatus(DownloadStatus& status)`:** Gets the current download status and progress for local models from the associated provider. Returns N/A for cloud-based providers.
*   **`ErrorCode setTemperature(float temperature)`:** Updates the temperature parameter for this session.
*   **`ErrorCode setMaxTokens(int maxTokens)`:** Updates the maximum token limit for this session.
*   **`ErrorCode getUsageStats(UsageStats& stats)`:** Retrieves token usage and cost statistics for this chat session.
*   **`ErrorCode getCachedResponseCount()`:** Returns the number of responses served from cache in this session.

**Core Data Structures**

*   **[`ChatConfig`](src/arbiterAI/chatClient.h):** Defines the initial configuration for a `ChatClient` instance (e.g., `model`, initial `temperature`, `maxTokens`, cache settings).
*   **[`CompletionRequest`](src/arbiterAI/arbiterAI.h):** Defines the parameters for a completion request (e.g., `messages`, `temperature`, `tools`). When used with `ChatClient`, some parameters may be inherited from the session configuration.
*   **[`CompletionResponse`](src/arbiterAI/arbiterAI.h):** Contains the results of a completion, including the generated `text`, `usage` statistics, `cost`, and any `tool_calls`.
*   **[`Message`](src/arbiterAI/arbiterAI.h):** Represents a single conversational turn with a `role` and `content`.
*   **[`ToolDefinition`](src/arbiterAI/chatClient.h):** Defines a callable function/tool with its schema, parameters, and description.
*   **[`DownloadStatus`](src/arbiterAI/providers/baseProvider.h):** Contains information about model download progress including `status` (pending, downloading, complete, error), `bytesDownloaded`, `totalBytes`, and `percentComplete`.

### 3. Model Configuration

The configuration system is designed to be flexible, layered, and dynamically updatable.

*   **Configuration Files:** Model definitions are stored in JSON files (e.g., [`examples/model_config_v2.json`](examples/model_config_v2.json)), which are validated against a formal schema ([`schemas/model_config.schema.json`](schemas/model_config.schema.json)). Each model entry specifies its `provider`, `ranking`, and other metadata, including optional `download` info for local models.
*   **[`ConfigDownloader`](src/arbiterAI/configDownloader.h):** This utility fetches the latest model configurations from a remote Git repository upon initialization. This allows for updating model definitions without releasing a new version of the library.
*   **[`ModelManager`](src/arbiterAI/modelManager.h):** This class orchestrates the configuration process. It loads configurations from the downloaded remote repository, any additional user-specified local paths, and a final override path. This layered approach allows for easy customization and extension of the model catalog. The `ModelManager` parses these files and populates a collection of [`ModelInfo`](src/arbiterAI/modelManager.h) objects that are queried at runtime.

### 4. Provider System

The provider system abstracts the implementation details of various LLM backends and is now tightly integrated with the `ChatClient` architecture.

*   **[`BaseProvider` Interface](src/arbiterAI/providers/baseProvider.h):** This abstract class defines the essential contract for all providers. Key capabilities include:
    *   `completion` and `streamingCompletion` methods for text generation
    *   `getEmbeddings` for vector embeddings
    *   `getDownloadStatus` for tracking local model download progress
    *   Tool/function calling support
    *   Session state management support
    
    Each `ChatClient` instance maintains a reference to its provider instance, allowing the client to query model-specific information and download status.

*   **Remote API Providers:** Classes like [`OpenAI`](src/arbiterAI/providers/openai.h) and [`Anthropic`](src/arbiterAI/providers/anthropic.h) implement the `BaseProvider` interface by making HTTP requests to their respective web services. They:
    *   Handle API-specific request formatting and response parsing
    *   Manage authentication and headers
    *   Return N/A or not-applicable status for `getDownloadStatus` since they are cloud-based
    *   Support tool/function calling according to each API's specification

*   **Local Provider ([`Llama`](src/arbiterAI/providers/llama.h)):** This provider interfaces with the `llama.cpp` library to run models locally. It:
    *   Manages model loading into memory and local inference
    *   Handles model file downloads and verification
    *   Provides detailed download status information via `getDownloadStatus`
    *   Tracks download progress (bytes downloaded, total size, percentage complete)
    *   Supports model caching to avoid re-downloads
    *   Can be queried by the `ChatClient` to provide download status to the user interface

### 5. Utility Components

A set of utility classes handles common, non-core tasks, promoting separation of concerns.

*   **[`CacheManager`](src/arbiterAI/cacheManager.h):** An optional component that provides session-level and global caching for `CompletionResponse` objects. It helps reduce costs and improve latency by storing and retrieving results for identical requests. The `ChatClient` maintains its own cache instance for session-specific caching, with support for:
    *   Time-to-live (TTL) for cache entries
    *   Session-scoped and global cache scopes
    *   Automatic cache invalidation
    *   Cache statistics tracking per session
    
*   **[`CostManager`](src/arbiterAI/costManager.h):** An optional service that tracks and enforces spending limits for API calls. It:
    *   Persists total cost to a file across sessions
    *   Provides per-session cost tracking via `ChatClient`
    *   Allows limits to be enforced globally or per-session
    *   Integrates with `ChatClient` to report usage statistics
    
*   **[`ModelDownloader`](src/arbiterAI/modelDownloader.h):** A utility used by local providers to download large model files asynchronously. It:
    *   Returns a `std::future` for non-blocking downloads
    *   Provides progress callbacks that feed into `BaseProvider::getDownloadStatus`
    *   Supports resume capability for interrupted downloads
    *   Integrates with `FileVerifier` for integrity checking
    
*   **[`FileVerifier`](src/arbiterAI/fileVerifier.h):** A security component integrated with the `ModelDownloader`. It:
    *   Computes SHA256 hashes of downloaded files
    *   Compares against expected hashes from model configuration
    *   Ensures file integrity and prevents corruption or tampering
    *   Reports verification status through the provider's download status

### 6. Usage Pattern

The typical usage pattern with the new `ChatClient` architecture:

```cpp
// Initialize the library once at startup
ArbiterAI& ai = ArbiterAI::instance();
ai.initialize(configPaths);

// Create a ChatClient for a new chat session
ChatConfig config;
config.model = "gpt-4";
config.temperature = 0.7;
config.maxTokens = 2000;
config.enableCache = true;

auto chatClient = ai.createChatClient(config);

// Check download status for local models
DownloadStatus status;
chatClient->getDownloadStatus(status);
if (status.status == DownloadStatus::Downloading) {
    std::cout << "Download progress: " << status.percentComplete << "%" << std::endl;
}

// Configure tools/functions if needed
std::vector<ToolDefinition> tools = { /* tool definitions */ };
chatClient->setTools(tools);

// Interact with the chat
CompletionRequest request;
request.messages = {{Role::User, "Hello!"}};

CompletionResponse response;
chatClient->completion(request, response);

// Or use streaming
chatClient->streamingCompletion(request, [](const std::string& chunk) {
    std::cout << chunk;
});

// Chat history is automatically maintained
auto history = chatClient->getHistory();

// When chat session ends or restarts, destroy and create new client
chatClient.reset();
auto newChatClient = ai.createChatClient(config); // Fresh session
```