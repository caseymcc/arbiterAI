# Project Plan: hermesaxiom

## 1. Project Overview

*   **Project Name**: `hermesaxiom`
*   **Description**: A modern, high-performance C++17 library designed to provide a unified, embeddable interface for interacting with various Large Language Model (LLM) providers. It aims to simplify the process of integrating LLM capabilities into C++ applications by offering a single, consistent API. This library is envisioned as a foundational component for C++ developers building AI-powered applications that require robust, efficient, and flexible LLM integration without being tied to a single provider.
*   **Primary Goal**: To create a high-performance, easy-to-use, robust, and extensible C++ library that can be seamlessly integrated into other applications, abstracting away LLM provider complexities.
*   **Secondary Goal**: To develop a production-ready example HTTP proxy server that demonstrates the library's capabilities and provides an OpenAI-compatible endpoint, enabling easy migration for existing applications.
*   **Target Audience**: C++ developers, data scientists, and engineers building applications that require LLM integration, particularly those focused on performance, control, and multi-provider flexibility.

## 2. Core Features

*   **Unified API**: A single, intuitive API for both standard (synchronous) and streaming (asynchronous) responses, offering a consistent interaction model regardless of the underlying LLM provider.
*   **Provider Abstraction**: Implemented through a clean provider pattern using the hermesaxiom::providers::BaseLLM interface, enabling easy integration of new LLM services with minimal changes to core logic. This design ensures loose coupling and maintainability.
*   **Initial Supported Providers**:
    *   OpenAI
    *   Anthropic
    *   DeepSeek
    *   OpenRouter (to be added)
    *   `llama.cpp` (for local models, to be added)
*   **Configuration Management**: A robust [`ModelManager`](src/hermesaxiom/modelManager.h) to load model configurations and API keys from files, with fallback to environment variables.
*   **Clear Error Handling**: A well-defined [`ErrorCode`](src/hermesaxiom/hermesaxiom.h:15) enum for predictable and robust error reporting.
*   **Modern C++**: Utilizes C++17 features and established libraries (`cpr` for HTTP requests, `nlohmann_json` for JSON parsing) for reliability and performance.

## 3. Proposed Architecture

The library's architecture is designed to be modular and extensible. The client application interacts with the main `hermesaxiom` API, which uses the `ModelManager` to determine the correct provider for a given model. A factory will then create the appropriate provider instance to handle the request.

```mermaid
graph TD
    subgraph User Application
        A[Client Code]
    end

    subgraph hermesaxiom Library
        B[hermesaxiom API<br>(completion, streamingCompletion)]
        C[ModelManager]
        D{Provider Factory}
        subgraph LLM Providers
            E1[OpenAI_LLM]
            E2[Anthropic_LLM]
            E3[DeepSeek_LLM]
            E4[LlamaCpp_LLM]
            E5[OpenRouter_LLM]
        end
    end

    subgraph External Services
        F1[OpenAI API]
        F2[Anthropic API]
        F3[DeepSeek API]
        F4[llama.cpp server]
        F5[OpenRouter API]
    end

    A --> B
    B --> C
    B --> D
    C --"getModelInfo()"--> B
    D --"createProvider(model)"--> E1
    D --> E2
    D --> E3
    D --> E4
    D --> E5

    E1 --HTTP Request (cpr)--> F1
    E2 --HTTP Request (cpr)--> F2
    E3 --HTTP Request (cpr)--> F3
    E4 --HTTP Request (cpr)--> F4
    E5 --HTTP Request (cpr)--> F5
```

## 4. Development Roadmap

### Phase 1: Core Library Enhancement
*Goal: Solidify the existing library structure and add key providers.*
- [x] **Refine `ModelManager`**: Enhance configuration loading to support multiple config files and environment variables for API keys.
- [x] **Implement `llama.cpp` Provider**: Add support for local models by interacting with a `llama.cpp` server instance.
- [ ] **Unit Testing**: Integrate a testing framework (e.g., GoogleTest) and write initial tests for the `ModelManager` and providers.
- [ ] **Documentation**: Add Doxygen-style comments to all public headers to enable automated documentation generation.

### Phase 2: API and Feature Expansion
*Goal: Make the library more powerful and flexible.*
- [ ] **Dynamic Configuration Management**:
    - [ ] **Remote Configuration Loading**: Implement functionality to download and cache default model configurations from a central GitHub repository.
    - [ ] **Configuration Schema Versioning**: Introduce a `schema_version` to all configuration files to ensure backward compatibility and graceful handling of format changes.
    - [ ] **Llama Model Downloader**: For `llama.cpp` configurations, include fields for model download URLs and corresponding SHA256 hashes for automated download and verification.
- [ ] **Full Streaming Support**: Fully implement and test the `streamingCompletion` function across all providers.
- [ ] **Advanced Completion Options**: Extend [`CompletionRequest`](src/hermesaxiom/hermesaxiom.h:34) to include parameters like `top_p`, `stop` sequences, etc., and map them to provider-specific APIs.
- [ ] **Usage & Cost Tracking**: Enhance [`CompletionResponse`](src/hermesaxiom/hermesaxiom.h:42) to include detailed token usage (`prompt_tokens`, `completion_tokens`) and estimated cost.
- [ ] **Embeddings API**: Introduce a `hermesaxiom::embedding()` function for generating vector embeddings.

### Phase 3: Tooling, Deployment, and Extensibility
*Goal: Improve usability, demonstrate capabilities, and add advanced features.*
- [ ] **Implement `OpenRouter` Provider**: Add support for the OpenRouter API aggregator.
- [ ] **Example CLI Tool**: Create a command-line application to serve as a usage example.
- [ ] **Example HTTP Proxy Server**: Build a separate executable for an OpenAI-compatible proxy server using a C++ HTTP server library (e.g., `httplib`).
- [ ] **Packaging**: Configure CMake to generate package files, allowing other CMake projects to easily find and use `hermesaxiom` via `find_package`.
- [ ] **Continuous Integration**: Set up a GitHub Actions workflow to build the project and run tests automatically.

## 5. Suggestions for Future Development

*   **Advanced Caching**: Implement a local caching mechanism for LLM responses to reduce latency and API costs for repeated queries. This could be configurable on a per-request basis.
*   **Request Retries and Backoff**: Add automatic retry logic with exponential backoff for transient network errors or API rate limit issues, making the library more resilient.
*   **Performance Benchmarking Suite**: Create a dedicated benchmarking tool to measure the performance (latency, throughput) of different providers and the library's overhead. This would validate the "high-performance" goal and help users choose the best models for their needs.
*   **Plugin System for Custom Providers**: Design a more dynamic plugin system (e.g., using shared libraries) that would allow users to add their own providers without modifying the core library code. This would greatly enhance extensibility.