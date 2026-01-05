# ArbiterAI Examples

This directory contains example applications demonstrating various features of the ArbiterAI library.

## Available Examples

### 1. CLI Example (`cli/`)
A command-line interface for interacting with LLM models through ArbiterAI. Supports:
- Interactive chat sessions
- Multiple providers (OpenAI, Anthropic, DeepSeek, etc.)
- Model selection
- Configuration management

See [`cli/main.cpp`](cli/main.cpp) for details.

### 2. Mock Provider Example (`mock_example.cpp`)
Demonstrates the Mock provider for testing without requiring actual LLM calls.

**Features shown:**
- Basic echo tag usage for controlled responses
- Multiline code responses
- Streaming completions
- Conversation history management
- Default responses when no echo tags present
- Usage statistics tracking

**Building:**
```bash
# Mock provider is built automatically with arbiterAI library
# Build the example (if added to CMakeLists.txt):
mkdir build && cd build
cmake ..
make mock_example
```

**Running:**
```bash
# From arbiterAI root directory
./build/mock_example

# Or specify config path:
./build/mock_example -c examples/mock_models.json
```

**Configuration:**
Uses [`mock_models.json`](mock_models.json) which defines mock models for testing.

### 3. Proxy Server Example (`proxy/`)
An HTTP proxy server that provides an OpenAI-compatible API endpoint, demonstrating how to build production services with ArbiterAI.

See [`proxy/main.cpp`](proxy/main.cpp) for details.

## Mock Provider Usage

The Mock provider enables testing without network calls or API keys. It uses special `<echo>` tags to control responses:

```cpp
#include "arbiterAI/arbiterAI.h"

// Initialize with mock configuration
arbiterAI::ArbiterAI& ai = arbiterAI::ArbiterAI::instance();
ai.initialize({"examples/mock_models.json"});

// Create chat client with mock model
arbiterAI::ChatConfig config;
config.model = "mock-model";
auto client = ai.createChatClient(config);

// Use echo tags to control response
arbiterAI::CompletionRequest request;
request.messages = {
    {"user", "What is 2+2? <echo>4</echo>"}
};

arbiterAI::CompletionResponse response;
client->completion(request, response);
// response.text == "4"
```

### Echo Tag Syntax

- **Basic:** `<echo>expected response</echo>`
- **Multiline:** Supports newlines and formatting
- **Case insensitive:** `<ECHO>`, `<Echo>`, `<echo>` all work
- **First match:** If multiple tags exist, first is used
- **Whitespace:** Leading/trailing whitespace is trimmed
- **Flexible placement:** Can appear anywhere in any message

### Example Use Cases

**Simple test:**
```cpp
request.messages = {{"user", "Calculate sum <echo>42</echo>"}};
```

**Code generation:**
```cpp
request.messages = {{"user", R"(
Write hello function <echo>
def hello():
    print("Hello!")
</echo>
)"}};
```

**No tag (default response):**
```cpp
request.messages = {{"user", "No echo tag"}};
// Returns: "This is a mock response. Use <echo>your expected response</echo>..."
```

## Configuration Files

### `mock_models.json`
Defines mock models for testing:
- `mock-model`: General purpose mock model
- `mock-chat`: Mock model optimized for chat conversations

Both models:
- Require no API keys
- Support completion and streaming
- Have no token limits or costs
- Are perfect for unit tests and integration tests

### `model_config_v2.json`
Production model configurations for real LLM providers (OpenAI, Anthropic, etc.).

## Testing Best Practices

When using the Mock provider in tests:

1. **Always include echo tags** for predictable results
2. **Test both echo and no-echo scenarios** to verify default handling
3. **Use realistic test data** with proper formatting
4. **Verify token usage** to ensure statistics tracking works
5. **Isolate tests** by creating fresh ChatClient instances

Example test structure:
```cpp
TEST(MockProviderTest, EchoExtraction) {
    arbiterAI::ChatConfig config;
    config.model = "mock-model";
    auto client = arbiterAI::ArbiterAI::instance().createChatClient(config);
    
    arbiterAI::CompletionRequest request;
    request.messages = {{"user", "Test <echo>Expected</echo>"}};
    
    arbiterAI::CompletionResponse response;
    EXPECT_EQ(client->completion(request, response), arbiterAI::ErrorCode::Success);
    EXPECT_EQ(response.text, "Expected");
}
```

## Additional Documentation

- [Testing Guide](../docs/testing.md) - Comprehensive testing documentation
- [Developer Guide](../docs/developer.md) - Architecture and API details
- [Project Overview](../docs/project.md) - High-level project goals

## Building Examples

The examples are built as part of the main arbiterAI build:

```bash
cd /path/to/arbiterAI
mkdir build && cd build
cmake ..
make

# Run examples
./arbiterAI-cli
./arbiterAI-proxy
./mock_example  # If added to build
```

## Contributing

When adding new examples:
1. Create clear, well-commented code
2. Update this README with usage instructions
3. Include any required configuration files
4. Add build instructions if needed
5. Document any special requirements or dependencies
