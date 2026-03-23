# Testing Guide for ArbiterAI

This guide covers testing strategies and tools available in ArbiterAI, with special focus on the Mock provider for repeatable testing.

## Table of Contents

1. [Overview](#overview)
2. [Mock Provider](#mock-provider)
3. [Echo Tag Syntax](#echo-tag-syntax)
4. [Usage Examples](#usage-examples)
5. [Testing Best Practices](#testing-best-practices)
6. [Integration Testing](#integration-testing)
7. [Test Configuration](#test-configuration)

## Overview

ArbiterAI provides a Mock provider specifically designed for testing applications without requiring:
- Active network connections
- API keys or authentication
- Actual LLM inference
- External service dependencies

The Mock provider enables deterministic, repeatable testing by allowing test code to control expected responses through special echo tags embedded in messages.

## Mock Provider

### Features

The Mock provider ([`src/arbiterAI/providers/mock.h`](../src/arbiterAI/providers/mock.h)) provides:

- **Echo Mode**: Extract expected responses from `<echo>` tags in messages
- **Default Responses**: Returns a helpful message when no echo tags are found
- **Token Simulation**: Calculates realistic token usage statistics
- **Streaming Support**: Simulates streaming behavior with configurable chunk sizes
- **No Network Calls**: Completely local, no external dependencies
- **Deterministic**: Same input always produces same output

### Configuration

To use the Mock provider, configure a model with `"provider": "mock"` in your model configuration:

```json
{
  "models": [
    {
      "name": "mock-model",
      "provider": "mock",
      "ranking": 1,
      "description": "Mock provider for testing"
    }
  ]
}
```

Then create a `ChatClient` with the mock model:

```cpp
#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"

// Initialize ArbiterAI with config that includes mock model
arbiterAI::ArbiterAI& ai = arbiterAI::ArbiterAI::instance();
ai.initialize({"path/to/config"});

// Create chat client using mock model
arbiterAI::ChatConfig config;
config.model = "mock-model";
auto chatClient = ai.createChatClient(config);
```

## Echo Tag Syntax

### Basic Syntax

```
<echo>expected response text</echo>
```

### Rules

1. **Case Insensitive**: Tags work with any case: `<ECHO>`, `<Echo>`, `<echo>`
2. **Multiline Support**: Content can span multiple lines
3. **First Match**: If multiple tags exist, the first is used
4. **Whitespace Trimming**: Leading/trailing whitespace is automatically trimmed
5. **Placement Flexible**: Tags can appear anywhere in any message
6. **Multiple Messages**: Provider searches all messages in the request

### Examples

**Simple response:**
```
User: "What is 2+2? <echo>4</echo>"
Response: "4"
```

**Multiline response:**
```
User: "Write hello in Python <echo>
def hello():
    print('Hello, World!')
</echo>"
Response: "def hello():\n    print('Hello, World!')"
```

**Hidden in conversation:**
```
System: "You are a helpful assistant."
User: "Tell me about space <echo>Space is vast and full of stars.</echo>"
Response: "Space is vast and full of stars."
```

### No Echo Tag Behavior

If no echo tag is found, the Mock provider returns a default message:

```
This is a mock response. Use <echo>your expected response</echo> tags in your messages to control the output.
```

This helps identify when tests are missing echo tags.

## Usage Examples

### Unit Testing Example

```cpp
#include <gtest/gtest.h>
#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"

class MockProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        arbiterAI::ArbiterAI& ai = arbiterAI::ArbiterAI::instance();
        ai.initialize({"test_config.json"});

        arbiterAI::ChatConfig config;
        config.model = "mock-model";
        chatClient = ai.createChatClient(config);
    }

    std::shared_ptr<arbiterAI::ChatClient> chatClient;
};

TEST_F(MockProviderTest, EchoTagExtraction) {
    arbiterAI::CompletionRequest request;
    request.messages = {{"user", "Calculate sum <echo>42</echo>"}};

    arbiterAI::CompletionResponse response;
    auto result = chatClient->completion(request, response);

    EXPECT_EQ(result, arbiterAI::ErrorCode::Success);
    EXPECT_EQ(response.text, "42");
}

TEST_F(MockProviderTest, MultilineEcho) {
    arbiterAI::CompletionRequest request;
    request.messages = {
        {"user", "Generate code <echo>int main() {\n    return 0;\n}</echo>"}
    };

    arbiterAI::CompletionResponse response;
    auto result = chatClient->completion(request, response);

    EXPECT_EQ(result, arbiterAI::ErrorCode::Success);
    EXPECT_EQ(response.text, "int main() {\n    return 0;\n}");
}

TEST_F(MockProviderTest, DefaultResponse) {
    arbiterAI::CompletionRequest request;
    request.messages = {{"user", "No echo tag here"}};

    arbiterAI::CompletionResponse response;
    auto result = chatClient->completion(request, response);

    EXPECT_EQ(result, arbiterAI::ErrorCode::Success);
    EXPECT_TRUE(response.text.find("mock response") != std::string::npos);
}

TEST_F(MockProviderTest, TokenUsageSimulation) {
    arbiterAI::CompletionRequest request;
    request.messages = {{"user", "Test <echo>Response</echo>"}};

    arbiterAI::CompletionResponse response;
    chatClient->completion(request, response);

    // Mock provider simulates token usage
    EXPECT_GT(response.usage.total_tokens, 0);
    EXPECT_GT(response.usage.prompt_tokens, 0);
    EXPECT_GT(response.usage.completion_tokens, 0);
}
```

### Streaming Test Example

```cpp
TEST_F(MockProviderTest, StreamingCompletion) {
    arbiterAI::CompletionRequest request;
    request.messages = {
        {"user", "Stream this <echo>Hello streaming world!</echo>"}
    };

    std::string accumulated;
    int chunkCount = 0;

    auto callback = [&](const std::string& chunk, bool done) {
        if (!done) {
            accumulated += chunk;
            chunkCount++;
        }
    };

    auto result = chatClient->streamingCompletion(request, callback);

    EXPECT_EQ(result, arbiterAI::ErrorCode::Success);
    EXPECT_EQ(accumulated, "Hello streaming world!");
    EXPECT_GT(chunkCount, 1); // Should stream in multiple chunks
}
```

### Chat History Testing

```cpp
TEST_F(MockProviderTest, ConversationHistory) {
    // First message
    arbiterAI::CompletionRequest request1;
    request1.messages = {{"user", "First <echo>Response 1</echo>"}};
    arbiterAI::CompletionResponse response1;
    chatClient->completion(request1, response1);

    // Second message
    arbiterAI::CompletionRequest request2;
    request2.messages = {{"user", "Second <echo>Response 2</echo>"}};
    arbiterAI::CompletionResponse response2;
    chatClient->completion(request2, response2);

    // Check history
    auto history = chatClient->getHistory();
    EXPECT_EQ(history.size(), 4); // 2 user + 2 assistant messages
}
```

## Testing Best Practices

### 1. Always Include Echo Tags in Tests

```cpp
// GOOD: Explicit expected response
request.messages = {{"user", "Question <echo>Expected answer</echo>"}};

// BAD: Relies on default response (less clear intent)
request.messages = {{"user", "Question"}};
```

### 2. Test Both Success and Edge Cases

```cpp
// Test expected success
TEST_F(Test, ValidInput) {
    // ... with echo tag ...
}

// Test no echo tag scenario
TEST_F(Test, MissingEchoTag) {
    // ... verify default response handling ...
}

// Test empty echo tag
TEST_F(Test, EmptyEcho) {
    request.messages = {{"user", "<echo></echo>"}};
    // ... verify behavior ...
}
```

### 3. Use Raw String Literals for Multiline Responses

```cpp
request.messages = {{"user", R"(
Write a function <echo>
def calculate_sum(a, b):
    """Calculate sum of two numbers."""
    return a + b
</echo>
)"}};
```

### 4. Test Token Usage Tracking

```cpp
TEST_F(Test, UsageStatistics) {
    chatClient->completion(request, response);

    arbiterAI::UsageStats stats;
    chatClient->getUsageStats(stats);

    EXPECT_GT(stats.totalTokens, 0);
    EXPECT_GT(stats.completionCount, 0);
}
```

### 5. Isolate Tests with Fresh Clients

```cpp
TEST_F(Test, IsolatedSession) {
    // Each test gets a fresh client — no cross-contamination
    arbiterAI::ChatConfig config;
    config.model = "mock-model";
    auto isolatedClient = arbiterAI::ArbiterAI::instance().createChatClient(config);
    // ... test in isolation ...
}
```

## Integration Testing

### Testing Error Handling

```cpp
TEST_F(IntegrationTest, HandleMissingModel) {
    arbiterAI::ChatConfig config;
    config.model = "non-existent-model";

    auto client = arbiterAI::ArbiterAI::instance().createChatClient(config);
    EXPECT_EQ(client, nullptr); // Should fail gracefully
}
```

### Testing Cache Behavior

```cpp
TEST_F(IntegrationTest, CacheWithMock) {
    arbiterAI::ChatConfig config;
    config.model = "mock-model";
    config.enableCache = true;

    auto client = arbiterAI::ArbiterAI::instance().createChatClient(config);

    arbiterAI::CompletionRequest request;
    request.messages = {{"user", "Test <echo>Cached response</echo>"}};

    arbiterAI::CompletionResponse response1, response2;
    client->completion(request, response1);
    client->completion(request, response2); // Should be cached

    EXPECT_EQ(response1.text, response2.text);
    EXPECT_FALSE(response1.fromCache);
    EXPECT_TRUE(response2.fromCache);
}
```

### Testing Configuration Changes

```cpp
TEST_F(IntegrationTest, TemperatureChange) {
    auto client = arbiterAI::ArbiterAI::instance().createChatClient(config);

    client->setTemperature(0.3);
    EXPECT_DOUBLE_EQ(client->getTemperature(), 0.3);

    // Mock provider ignores temperature, but the API still works
    arbiterAI::CompletionRequest request;
    request.messages = {{"user", "Test <echo>Result</echo>"}};

    arbiterAI::CompletionResponse response;
    EXPECT_EQ(client->completion(request, response), arbiterAI::ErrorCode::Success);
}
```

## Test Configuration

### Sample Configuration File

Create a test configuration file (`test_models.json`):

```json
{
  "models": [
    {
      "name": "mock-model",
      "provider": "mock",
      "ranking": 1,
      "description": "Mock provider for testing",
      "capabilities": {
        "completion": true,
        "streaming": true,
        "embeddings": false
      }
    },
    {
      "name": "mock-chat",
      "provider": "mock",
      "ranking": 1,
      "description": "Mock chat model for testing conversations"
    }
  ]
}
```

See also [`examples/mock_models.json`](../examples/mock_models.json) for a ready-to-use configuration.

### Test Files

| File | Description |
|------|-------------|
| [`tests/mockProviderTests.cpp`](../tests/mockProviderTests.cpp) | Mock provider unit tests |
| [`tests/chatClientTests.cpp`](../tests/chatClientTests.cpp) | ChatClient tests |
| [`tests/arbiterAITests.cpp`](../tests/arbiterAITests.cpp) | Core library tests |
| [`tests/providerTests.cpp`](../tests/providerTests.cpp) | Provider integration tests |
| [`tests/modelManagerTests.cpp`](../tests/modelManagerTests.cpp) | Model configuration tests |
| [`tests/modelDownloaderTests.cpp`](../tests/modelDownloaderTests.cpp) | Download and verification tests |
| [`tests/configDownloaderTests.cpp`](../tests/configDownloaderTests.cpp) | Config download tests |

### Running Tests

From inside the Docker container:

```bash
./build/linux_x64_debug/arbiterai_tests
```

## Further Reading

- [Developer Guide](developer.md) — Architecture and API reference
- [Examples](../examples/README.md) — Working example code
- [`src/arbiterAI/providers/mock.h`](../src/arbiterAI/providers/mock.h) — Mock provider header
- [`src/arbiterAI/providers/mock.cpp`](../src/arbiterAI/providers/mock.cpp) — Mock provider implementation
