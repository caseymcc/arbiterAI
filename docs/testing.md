# Testing Guide for ArbiterAI

This guide covers testing strategies and tools available in ArbiterAI, with special focus on the Mock provider for repeatable testing.

## Table of Contents
1. [Overview](#overview)
2. [Mock Provider](#mock-provider)
3. [Echo Tag Syntax](#echo-tag-syntax)
4. [Usage Examples](#usage-examples)
5. [Testing Best Practices](#testing-best-practices)
6. [Integration Testing](#integration-testing)

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
- **Default Responses**: Returns helpful message when no echo tags found
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

Or create a `ChatClient` with the mock model:

```cpp
#include "arbiterAI/arbiterAI.h"

// Initialize ArbiterAI with config that includes mock model
ArbiterAI& ai = ArbiterAI::instance();
ai.initialize(configPaths);

// Create chat client using mock model
ChatConfig config;
config.model = "mock-model";
config.temperature = 0.7;  // Ignored by mock, but demonstrates API usage

auto chatClient = ai.createChatClient(config);
```

## Echo Tag Syntax

### Basic Syntax

The echo tag syntax is simple and flexible:

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

Simple response:
```
User: "What is 2+2? <echo>4</echo>"
Response: "4"
```

Multiline response:
```
User: "Write hello in Python <echo>
def hello():
    print('Hello, World!')
</echo>"
Response: "def hello():
    print('Hello, World!')"
```

Complex narrative:
```
User: "Explain quantum computing in simple terms. <echo>
Quantum computing uses quantum bits (qubits) that can exist in multiple 
states simultaneously, unlike classical bits that are either 0 or 1. 
This allows quantum computers to process certain types of problems 
exponentially faster than traditional computers.
</echo>"
```

Hidden in conversation:
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

class MockProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        ArbiterAI& ai = ArbiterAI::instance();
        // Assume config includes mock-model
        ai.initialize({"test_config.json"});
        
        ChatConfig config;
        config.model = "mock-model";
        chatClient = ai.createChatClient(config);
    }
    
    std::shared_ptr<ChatClient> chatClient;
};

TEST_F(MockProviderTest, EchoTagExtraction) {
    CompletionRequest request;
    request.messages = {
        {Role::User, "Calculate sum <echo>42</echo>"}
    };
    
    CompletionResponse response;
    ErrorCode result = chatClient->completion(request, response);
    
    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(response.text, "42");
}

TEST_F(MockProviderTest, MultilineEcho) {
    CompletionRequest request;
    request.messages = {
        {Role::User, "Generate code <echo>int main() {\n    return 0;\n}</echo>"}
    };
    
    CompletionResponse response;
    ErrorCode result = chatClient->completion(request, response);
    
    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(response.text, "int main() {\n    return 0;\n}");
}

TEST_F(MockProviderTest, DefaultResponse) {
    CompletionRequest request;
    request.messages = {
        {Role::User, "No echo tag here"}
    };
    
    CompletionResponse response;
    ErrorCode result = chatClient->completion(request, response);
    
    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_TRUE(response.text.find("mock response") != std::string::npos);
}

TEST_F(MockProviderTest, TokenUsageSimulation) {
    CompletionRequest request;
    request.messages = {
        {Role::User, "Test <echo>Response</echo>"}
    };
    
    CompletionResponse response;
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
    CompletionRequest request;
    request.messages = {
        {Role::User, "Stream this <echo>Hello streaming world!</echo>"}
    };
    
    std::string accumulated;
    int chunkCount = 0;
    
    auto callback = [&](const std::string& chunk, bool done) {
        if (!done) {
            accumulated += chunk;
            chunkCount++;
        }
    };
    
    ErrorCode result = chatClient->streamingCompletion(request, callback);
    
    EXPECT_EQ(result, ErrorCode::Success);
    EXPECT_EQ(accumulated, "Hello streaming world!");
    EXPECT_GT(chunkCount, 1); // Should stream in multiple chunks
}
```

### Chat History Testing

```cpp
TEST_F(MockProviderTest, ConversationHistory) {
    // First message
    CompletionRequest request1;
    request1.messages = {{Role::User, "First <echo>Response 1</echo>"}};
    CompletionResponse response1;
    chatClient->completion(request1, response1);
    
    // Second message
    CompletionRequest request2;
    request2.messages = {{Role::User, "Second <echo>Response 2</echo>"}};
    CompletionResponse response2;
    chatClient->completion(request2, response2);
    
    // Check history
    auto history = chatClient->getHistory();
    EXPECT_EQ(history.size(), 4); // 2 user + 2 assistant messages
    EXPECT_EQ(history[0].content, "First <echo>Response 1</echo>");
    EXPECT_EQ(history[1].content, "Response 1");
    EXPECT_EQ(history[2].content, "Second <echo>Response 2</echo>");
    EXPECT_EQ(history[3].content, "Response 2");
}
```

## Testing Best Practices

### 1. Always Include Echo Tags in Tests

```cpp
// GOOD: Explicit expected response
request.messages = {{Role::User, "Question <echo>Expected answer</echo>"}};

// BAD: Relies on default response (less clear intent)
request.messages = {{Role::User, "Question"}};
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
    request.messages = {{Role::User, "<echo></echo>"}};
    // ... verify behavior ...
}
```

### 3. Use Realistic Test Data

```cpp
// GOOD: Realistic multi-line code response
request.messages = {{Role::User, R"(
Write a function <echo>
def calculate_sum(a, b):
    """Calculate sum of two numbers."""
    return a + b
</echo>
)"}};

// Also good: Use raw string literals for readability
```

### 4. Test Token Usage Tracking

```cpp
TEST_F(Test, UsageStatistics) {
    chatClient->completion(request, response);
    
    UsageStats stats;
    chatClient->getUsageStats(stats);
    
    EXPECT_GT(stats.totalTokens, 0);
    EXPECT_GT(stats.completionCount, 0);
}
```

### 5. Isolate Tests with Fresh Clients

```cpp
TEST_F(Test, IsolatedSession) {
    // Each test gets fresh client from SetUp()
    // No cross-contamination of history or state
    
    ChatConfig config;
    config.model = "mock-model";
    auto isolatedClient = ArbiterAI::instance().createChatClient(config);
    // ... test in isolation ...
}
```

## Integration Testing

### Testing Error Handling

```cpp
TEST_F(IntegrationTest, HandleMissingModel) {
    ChatConfig config;
    config.model = "non-existent-model";
    
    auto client = ArbiterAI::instance().createChatClient(config);
    EXPECT_EQ(client, nullptr); // Should fail gracefully
}
```

### Testing Cache Behavior

```cpp
TEST_F(IntegrationTest, CacheWithMock) {
    ChatConfig config;
    config.model = "mock-model";
    config.enableCache = true;
    
    auto client = ArbiterAI::instance().createChatClient(config);
    
    CompletionRequest request;
    request.messages = {{Role::User, "Test <echo>Cached response</echo>"}};
    
    CompletionResponse response1, response2;
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
    auto client = ArbiterAI::instance().createChatClient(config);
    
    EXPECT_EQ(client->getTemperature(), 0.7);
    
    client->setTemperature(0.3);
    EXPECT_EQ(client->getTemperature(), 0.3);
    
    // Mock provider ignores temperature, but API still works
    CompletionRequest request;
    request.messages = {{Role::User, "Test <echo>Result</echo>"}};
    
    CompletionResponse response;
    EXPECT_EQ(client->completion(request, response), ErrorCode::Success);
}
```

## Sample Test Configuration

Create a test configuration file `test_models.json`:

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

## Conclusion

The Mock provider enables comprehensive testing of ArbiterAI-based applications without external dependencies. By using echo tags, tests can:

- Control exact expected outputs
- Verify conversation flow and history management
- Test error handling and edge cases
- Validate statistics and token tracking
- Ensure deterministic, repeatable results

For more information, see:
- [`src/arbiterAI/providers/mock.h`](../src/arbiterAI/providers/mock.h) - Mock provider interface
- [`src/arbiterAI/providers/mock.cpp`](../src/arbiterAI/providers/mock.cpp) - Implementation details
- [`developer.md`](developer.md) - Overall architecture documentation
