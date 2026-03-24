# ChatClient Architecture Implementation Tasks

This document tracks the implementation tasks for refactoring ArbiterAI from a stateless text-in/text-out completion interface to a stateful ChatClient-based architecture.

## Overview

The goal is to create a `ChatClient` class that:
- Acts as the primary interface for chat interactions
- Maintains conversation state, message history, and session configuration
- Manages tool/function calling state and definitions
- Provides session-level caching
- Exposes download status from the associated provider for local models
- Is created per chat session (new instance on chat restart)

---

## Phase 1: Core Data Structures

- [x] **1.1** Create `ChatConfig` structure in `chatClient.h`
  - `model`: Model identifier string
  - `temperature`: Initial temperature setting
  - `maxTokens`: Maximum token limit
  - `enableCache`: Whether to enable session caching
  - `systemPrompt`: Optional initial system message

- [x] **1.2** Create `ToolDefinition` structure
  - `name`: Function/tool name
  - `description`: Description for the LLM
  - `parameters`: JSON schema for parameters
  - `required`: List of required parameters

- [x] **1.3** Create `DownloadStatus` structure in `baseProvider.h`
  - `status`: Enum (NotApplicable, Pending, Downloading, Complete, Error)
  - `bytesDownloaded`: Current bytes downloaded
  - `totalBytes`: Total file size
  - `percentComplete`: Download percentage
  - `errorMessage`: Error details if applicable

- [x] **1.4** Create `UsageStats` structure
  - `promptTokens`: Total prompt tokens used
  - `completionTokens`: Total completion tokens used
  - `totalTokens`: Combined token count
  - `estimatedCost`: Estimated cost for this session
  - `cachedResponses`: Count of cached responses served

---

## Phase 2: ChatClient Class Implementation

- [x] **2.1** Create `chatClient.h` header file
  - Define `ChatClient` class interface
  - Include all public method declarations
  - Forward declarations for dependencies

- [x] **2.2** Create `chatClient.cpp` implementation file
  - Constructor accepting `ChatConfig`
  - Store reference to associated provider

- [x] **2.3** Implement conversation state management
  - `addMessage(const Message& message)`: Add to history
  - `getHistory()`: Return conversation history
  - `clearHistory()`: Clear conversation state
  - Internal message storage (vector of Messages)

- [x] **2.4** Implement completion methods
  - `completion(CompletionRequest&, CompletionResponse&)`: Blocking completion
  - `streamingCompletion(CompletionRequest&, StreamCallback)`: Streaming completion
  - Integrate session context into requests automatically

- [x] **2.5** Implement configuration methods
  - `setTemperature(float)`: Update temperature
  - `setMaxTokens(int)`: Update max tokens
  - Store and apply session-specific settings

- [x] **2.6** Implement tool/function calling support
  - `setTools(const std::vector<ToolDefinition>&)`: Configure tools
  - `getTools()`: Retrieve current tool configuration
  - `clearTools()`: Remove all tools
  - Integrate tools into completion requests

- [x] **2.7** Implement download status passthrough
  - `getDownloadStatus(DownloadStatus&)`: Query provider for download status
  - Handle N/A status for cloud providers

- [x] **2.8** Implement session statistics
  - `getUsageStats(UsageStats&)`: Return session usage
  - `getCachedResponseCount()`: Return cache hit count
  - Track usage across all completions in session

- [x] **2.9** Implement session-level caching
  - Integrate with `CacheManager` for session scope
  - Cache key generation including session context
  - Cache invalidation on configuration changes

---

## Phase 3: BaseProvider Updates

- [x] **3.1** Add `getDownloadStatus()` method to `BaseProvider` interface
  - Pure virtual method returning `ErrorCode`
  - Output parameter for `DownloadStatus`

- [x] **3.2** Implement `getDownloadStatus()` in remote providers (OpenAI, Anthropic, etc.)
  - Return `DownloadStatus::NotApplicable` status
  - No download tracking needed for cloud providers

- [ ] **3.3** Implement `getDownloadStatus()` in `Llama` provider
  - Track download progress from `ModelDownloader`
  - Report accurate bytes downloaded, total, and percentage
  - Handle error states
  - *Note: Llama provider is currently disabled in build*

- [x] **3.4** Add tool/function calling support to `BaseProvider` interface (if not present)
  - Method signatures for tool execution
  - Tool result handling

---

## Phase 4: ArbiterAI Singleton Updates

- [x] **4.1** Add `createChatClient()` factory method
  - Accept `ChatConfig` parameter
  - Return `std::shared_ptr<ChatClient>`
  - Instantiate appropriate provider for requested model

- [x] **4.2** Update `ArbiterAI` to manage `ChatClient` lifecycle
  - Optional tracking of active clients
  - Clean shutdown of clients

- [x] **4.3** Keep or deprecate existing stateless methods
  - Decide if `completion()` on ArbiterAI should remain for simple use cases
  - If keeping, document as convenience wrapper

- [x] **4.4** Add `getModelInfo()` method if not present
  - Allow querying available models
  - Return model capabilities and metadata

---

## Phase 5: CacheManager Updates

- [x] **5.1** Add session-scoped caching support
  - Session identifier in cache keys
  - Session-specific TTL settings

- [x] **5.2** Add cache statistics methods
  - Track hits/misses per session
  - Expose statistics to `ChatClient`

---

## Phase 6: CostManager Updates

- [x] **6.1** Add per-session cost tracking
  - Associate costs with `ChatClient` instances
  - Aggregate session costs into global tracking

- [x] **6.2** Add session-level spending limits (optional)
  - Per-session cost caps
  - Callback or error when limit reached

---

## Phase 7: ModelDownloader Integration

- [x] **7.1** Add progress callback support
  - Callback function for download progress updates
  - Feed progress into provider's download status

- [x] **7.2** Add resume capability (if not present)
  - Support resuming interrupted downloads
  - Track partial download state

---

## Phase 8: Testing

- [x] **8.1** Unit tests for `ChatClient`
  - Test session state management
  - Test completion with context
  - Test tool configuration
  - Test download status queries

- [x] **8.2** Unit tests for `DownloadStatus` in providers
  - Test local provider download tracking
  - Test cloud provider N/A status

- [x] **8.3** Integration tests
  - Full chat session lifecycle
  - Multiple concurrent `ChatClient` instances
  - Session restart scenarios

- [x] **8.4** Update existing tests
  - Migrate tests from old API to new ChatClient API
  - Ensure backward compatibility if stateless methods kept
  - *Note: Existing tests pass with the new code*

---

## Phase 9: Documentation

- [x] **9.1** Update `developer.md` with new architecture
  - Document ChatClient-based design
  - Update API documentation
  - Add usage examples

- [x] **9.2** Update code comments and headers
  - Doxygen comments for new classes
  - Deprecation notices for old methods if applicable

- [x] **9.3** Create migration guide
  - Document changes from old to new API
  - Code examples for migration

---

## Implementation Notes

### ChatClient Lifecycle
```cpp
// Create per chat session
auto client = ArbiterAI::instance().createChatClient(config);

// Use throughout session
client->completion(request, response);

// On chat restart - destroy and create new
client.reset();
client = ArbiterAI::instance().createChatClient(config);
```

### Download Status Flow
```
ModelDownloader -> Llama Provider -> ChatClient -> Application UI
                   (tracks progress)  (queries)    (displays)
```

### Session State
- Message history stored in ChatClient
- Automatically prepended to completion requests
- Cleared on client destruction or explicit clear

---

## Dependencies

- `BaseProvider` changes must be completed before `ChatClient` can query download status
- `ChatConfig` and `ToolDefinition` structures needed before `ChatClient` implementation
- `DownloadStatus` structure needed in `BaseProvider` before provider implementations

---

## Estimated Effort

| Phase | Estimated Time |
|-------|----------------|
| Phase 1: Data Structures | 2-3 hours |
| Phase 2: ChatClient | 8-12 hours |
| Phase 3: BaseProvider | 4-6 hours |
| Phase 4: ArbiterAI | 2-3 hours |
| Phase 5: CacheManager | 2-3 hours |
| Phase 6: CostManager | 1-2 hours |
| Phase 7: ModelDownloader | 2-3 hours |
| Phase 8: Testing | 6-8 hours |
| Phase 9: Documentation | 2-3 hours |
| **Total** | **29-43 hours** |
