# ArbiterAI

A modern, high-performance C++17 library that provides a unified, embeddable interface for interacting with multiple Large Language Model (LLM) providers. ArbiterAI simplifies LLM integration for C++ applications by offering a single, consistent API across diverse backends — from cloud services to local models.

## Key Features

- **Unified API** — Single interface for completions and streaming across all providers
- **Session-Oriented Chat** — Stateful `ChatClient` with conversation history, tool calling, and per-session caching
- **Multiple Providers** — OpenAI, Anthropic, DeepSeek, OpenRouter, llama.cpp (local), and a Mock provider for testing
- **Model Management** — Dynamic configuration from JSON files with schema validation and remote updates
- **Cost Tracking** — Per-session and global spending limits with persistent state
- **Response Caching** — TTL-based caching to reduce costs and latency
- **Tool/Function Calling** — Define tools with JSON schemas for LLM function calling
- **Mock Provider** — Deterministic testing with `<echo>` tags — no API keys or network required

## Quick Start

```cpp
#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"

// Initialize the library
arbiterAI::ArbiterAI& ai = arbiterAI::ArbiterAI::instance();
ai.initialize({"path/to/config"});

// Create a chat session
arbiterAI::ChatConfig config;
config.model = "gpt-4";
config.temperature = 0.7;
auto client = ai.createChatClient(config);

// Send a message
arbiterAI::CompletionRequest request;
request.messages = {{"user", "Hello!"}};
arbiterAI::CompletionResponse response;
client->completion(request, response);
std::cout << response.text << std::endl;
```

## Building

The project uses CMake and vcpkg for dependency management. A Docker environment is provided for consistent builds.

1. **Start the Docker container:**
    ```bash
    ./runDocker.sh
    ```

2. **Build inside the container:**
    ```bash
    ./build.sh
    ```
    For a clean rebuild:
    ```bash
    ./build.sh --rebuild
    ```

Build output is located in `build/linux_x64_debug/`.

## Running Tests

From inside the Docker container:

```bash
./build/linux_x64_debug/arbiterai_tests
```

Tests use Google Test and include coverage for all core components, providers, and the mock provider.

## Documentation

| Document | Description |
|----------|-------------|
| [Project Overview](docs/project.md) | Goals, features, and supported providers |
| [Developer Guide](docs/developer.md) | Architecture, API reference, and component details |
| [Testing Guide](docs/testing.md) | Mock provider, echo tags, and testing strategies |
| [Development Process](docs/development.md) | Workflow and task tracking |
| [Examples](examples/README.md) | CLI, proxy, and mock provider examples |

## Mock Provider

ArbiterAI includes a Mock provider for testing without API keys or network calls. Use `<echo>` tags to control responses:

```cpp
arbiterAI::ChatConfig config;
config.model = "mock-model";
auto client = ai.createChatClient(config);

arbiterAI::CompletionRequest request;
request.messages = {{"user", "What is 2+2? <echo>4</echo>"}};

arbiterAI::CompletionResponse response;
client->completion(request, response);
// response.text == "4"
```

See the [Testing Guide](docs/testing.md) for full echo tag syntax and examples.

## License

See [LICENSE](LICENSE) for details.
