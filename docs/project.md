# Project: ArbiterAI

## Description

A modern, high-performance C++17 library designed to provide a unified, embeddable interface for interacting with various Large Language Model (LLM) providers. It simplifies the process of integrating LLM capabilities into C++ applications by offering a single, consistent API. ArbiterAI is a foundational component for C++ developers building AI-powered applications that require robust, efficient, and flexible LLM integration without being tied to a single provider.

## Goals

**Primary Goal:** Create a high-performance, easy-to-use, robust, and extensible C++ library that can be seamlessly integrated into other applications, abstracting away LLM provider complexities.

**Secondary Goal:** Develop a standalone server application that uses the ArbiterAI library to serve an OpenAI-compatible API, model management endpoints, and a live stats dashboard for local model inference.

## Target Audience

C++ developers, data scientists, and engineers building applications that require LLM integration — particularly those focused on performance, control, and multi-provider flexibility.

## Unique Selling Proposition

- **Unified C++ Interface** — Simplifies LLM integration with a consistent API across diverse providers
- **High Performance** — Leverages modern C++17 features and efficient third-party libraries
- **Extensibility** — Clear provider pattern makes it straightforward to add new LLM services or local models
- **Robust Error Handling** — Clear and predictable error reporting for debugging and application stability

## Core Features

### Unified API

A single, intuitive API for both standard (synchronous) and streaming (asynchronous) responses, offering a consistent interaction model regardless of the underlying LLM provider.

### Session-Oriented Chat

The [`ChatClient`](../src/arbiterAI/chatClient.h) provides a stateful, session-oriented interface that manages conversation history, tool definitions, caching, and usage statistics per session.

### Provider Abstraction

Implemented through a clean provider pattern using the [`BaseProvider`](../src/arbiterAI/providers/baseProvider.h) interface, enabling easy integration of new LLM services with minimal changes to core logic.

### Supported Providers

| Provider | Type | Description |
|----------|------|-------------|
| [OpenAI](../src/arbiterAI/providers/openai.h) | Cloud | Full support for OpenAI's ChatCompletion API |
| [Anthropic](../src/arbiterAI/providers/anthropic.h) | Cloud | Integration with Anthropic's Messages API |
| [DeepSeek](../src/arbiterAI/providers/deepseek.h) | Cloud | Support for DeepSeek's ChatCompletion API |
| [OpenRouter](../src/arbiterAI/providers/openrouter.h) | Cloud | Unified interface for models via the OpenRouter aggregation service |
| [Llama.cpp](../src/arbiterAI/providers/llama.h) | Local | Integration with llama.cpp for local model inference (currently disabled in build) |
| [Mock](../src/arbiterAI/providers/mock.h) | Testing | Deterministic testing provider with echo tag support |

### Configuration Management

The [`ModelManager`](../src/arbiterAI/modelManager.h) dynamically loads model configurations from multiple sources:

- **JSON configuration files** with schema validation ([`schemas/model_config.schema.json`](../schemas/model_config.schema.json))
- **Remote configuration updates** via Git through [`ConfigDownloader`](../src/arbiterAI/configDownloader.h)
- **Environment variables** for API keys (e.g., `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`)
- **Layered configuration** — remote, local, and override paths with clear precedence

### Error Handling

A well-defined `ErrorCode` enum for predictable, granular error reporting across all operations.

### Tool/Function Calling

Support for LLM tool/function calling with JSON schema-based parameter definitions via `ToolDefinition` and `ToolCall` structures.

### Response Caching

Optional [`CacheManager`](../src/arbiterAI/cacheManager.h) provides TTL-based caching at both session and global scope to reduce API costs and latency.

### Cost Tracking

The [`CostManager`](../src/arbiterAI/costManager.h) tracks spending with global and per-session limits, persisting cost state across application restarts.

### Streaming Support

Asynchronous `streamingCompletion` function handles Server-Sent Events (SSE) for real-time token delivery in interactive applications.

## Third-Party Libraries

| Library | Purpose |
|---------|---------|
| [cpr](https://github.com/libcpr/cpr) | HTTP requests |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing and generation |
| [nlohmann/json-schema-validator](https://github.com/pboettch/json-schema-validator) | JSON schema validation |
| [spdlog](https://github.com/gabime/spdlog) | Logging |
| [libgit2](https://libgit2.org/) | Git operations for config downloads |
| [picosha2](https://github.com/okdshin/PicoSHA2) | SHA256 hashing for file verification |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | HTTP server (proxy example) |
| [Google Test](https://github.com/google/googletest) | Testing framework |

## Further Documentation

- [Developer Guide](developer.md) — Architecture, API reference, and component details
- [Testing Guide](testing.md) — Mock provider and testing strategies
- [Development Process](development.md) — Workflow and task tracking
- [Examples](../examples/README.md) — Example applications and usage patterns