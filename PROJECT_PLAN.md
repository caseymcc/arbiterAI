# Project Plan: hermesaxiom

## 1. Project Overview

*   **Project Name**: `hermesaxiom`
*   **Description**: A modern C++17 library designed to provide a unified, embeddable interface for interacting with various Large Language Model (LLM) providers. It aims to simplify the process of integrating LLM capabilities into C++ applications by offering a single, consistent API, inspired by the simplicity of `liteLLM`.
*   **Primary Goal**: Create a high-performance, easy-to-use, and extensible C++ library that can be seamlessly integrated into other applications.
*   **Secondary Goal**: Develop an example HTTP proxy server to demonstrate the library's capabilities and provide an OpenAI-compatible endpoint.

## 2. Core Features

*   **Unified API**: A single, intuitive `hermesaxiom::completion()` function for both standard and streaming responses.
*   **Provider Abstraction**: A clean provider pattern (using the [`BaseLLM`](src/hermesaxiom/providers/base_llm.h) interface) to easily add new LLM providers.
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
1.  **Refine `ModelManager`**: Enhance configuration loading to support multiple config files and environment variables for API keys.
2.  **Implement `llama.cpp` Provider**: Add support for local models by interacting with a `llama.cpp` server instance.
3.  **Implement `OpenRouter` Provider**: Add support for the OpenRouter API aggregator.
4.  **Unit Testing**: Integrate a testing framework (e.g., GoogleTest) and write initial tests for the `ModelManager` and providers.
5.  **Documentation**: Add Doxygen-style comments to all public headers to enable automated documentation generation.

### Phase 2: API and Feature Expansion
*Goal: Make the library more powerful and flexible.*
1.  **Full Streaming Support**: Fully implement and test the `streamingCompletion` function across all providers.
2.  **Advanced Completion Options**: Extend [`CompletionRequest`](src/hermesaxiom/hermesaxiom.h:34) to include parameters like `top_p`, `stop` sequences, etc., and map them to provider-specific APIs.
3.  **Usage & Cost Tracking**: Enhance [`CompletionResponse`](src/hermesaxiom/hermesaxiom.h:42) to include detailed token usage (`prompt_tokens`, `completion_tokens`) and estimated cost.
4.  **Embeddings API**: Introduce a `hermesaxiom::embedding()` function for generating vector embeddings.

### Phase 3: Tooling and Deployment
*Goal: Improve usability and demonstrate the library's capabilities.*
1.  **Example CLI Tool**: Create a command-line application to serve as a usage example.
2.  **Example HTTP Proxy Server**: Build a separate executable for an OpenAI-compatible proxy server using a C++ HTTP server library (e.g., `httplib`).
3.  **Packaging**: Configure CMake to generate package files, allowing other CMake projects to easily find and use `hermesaxiom` via `find_package`.
4.  **Continuous Integration**: Set up a GitHub Actions workflow to build the project and run tests automatically.