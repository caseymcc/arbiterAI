# ArbiterAI Examples

This directory contains example applications demonstrating various features of the ArbiterAI library.

## Available Examples

### CLI Chat Client (`cli/`)

A command-line interface for interactive chat sessions with LLM models.

- Interactive multi-turn chat
- Multiple providers (OpenAI, Anthropic, DeepSeek, OpenRouter, local models)
- Model selection and configuration

See [`cli/main.cpp`](cli/main.cpp) for details.

**Build target:** `arbiterAI-cli`

### HTTP Proxy Server (`proxy/`)

An OpenAI-compatible HTTP proxy server demonstrating how to build production services with ArbiterAI.

**Build target:** `arbiterAI-proxy`

### Mock Provider Example (`mock_example.cpp`)

Demonstrates the Mock provider for testing without actual LLM calls. Shows echo tag usage, streaming, conversation history, and usage statistics.

**Note:** This example is not currently wired into CMakeLists.txt as a build target.

## Mock Provider Usage

The Mock provider enables testing without network calls or API keys. Use `<echo>` tags to control responses:

```cpp
#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/chatClient.h"

// Initialize with mock configuration
arbiterAI::ArbiterAI& ai = arbiterAI::ArbiterAI::instance();
ai.initialize({"examples/mock_models.json"});

// Create chat client with mock model
arbiterAI::ChatConfig config;
config.model = "mock-model";
auto client = ai.createChatClient(config);

// Use echo tags to control response
arbiterAI::CompletionRequest request;
request.messages = {{"user", "What is 2+2? <echo>4</echo>"}};

arbiterAI::CompletionResponse response;
client->completion(request, response);
// response.text == "4"
```

### Echo Tag Quick Reference

| Pattern | Result |
|---------|--------|
| `<echo>text</echo>` | `"text"` |
| `<echo>\nline1\nline2\n</echo>` | `"line1\nline2"` |
| `<ECHO>text</ECHO>` | `"text"` (case insensitive) |
| Multiple tags | First match is used |
| No tag | Default mock message |

See the [Testing Guide](../docs/testing.md) for comprehensive documentation.

## Configuration Files

| File | Description |
|------|-------------|
| [`mock_models.json`](mock_models.json) | Mock model definitions for testing (no API keys needed) |
| [`model_config_v2.json`](model_config_v2.json) | Production model configurations for real LLM providers |

## Building

All examples are built as part of the main ArbiterAI build:

```bash
# Inside Docker container
./build.sh
```

Build output is in `build/linux_x64_debug/`.

## Further Documentation

- [Testing Guide](../docs/testing.md) — Mock provider and testing strategies
- [Developer Guide](../docs/developer.md) — Architecture and API reference
- [Project Overview](../docs/project.md) — High-level project goals
