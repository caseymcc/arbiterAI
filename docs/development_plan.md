# Project Plan: arbiterAI

## 1\. Project Overview

  * **Project Name:** arbiterAI

  * **Description:** A modern, high-performance C++17 library designed to provide a unified, embeddable interface for interacting with various Large Language Model (LLM) providers. It aims to simplify the process of integrating LLM capabilities into C++ applications by offering a single, consistent API. This library is envisioned as a foundational component for C++ developers building AI-powered applications that require robust, efficient, and flexible LLM integration without being tied to a single provider. The library will include robust configuration management that can retrieve model definitions directly from a GitHub repository, incorporating schema versioning to ensure backward compatibility. For local models like Llama, configurations will specify model download URLs and cryptographic hashes for verification. Furthermore, it will support efficient batch completions for scenarios involving numerous file updates, provide detailed cost tracking, estimation, and allow for setting usage limits, and incorporate intelligent caching for frequently requested completions to optimize performance and reduce API expenses.

  * **Primary Goal:** To create a high-performance, easy-to-use, robust, and extensible C++ library that can be seamlessly integrated into other applications, abstracting away LLM provider complexities.

  * **Secondary Goal:** To develop a production-ready example HTTP proxy server that demonstrates the library's capabilities and provides an OpenAI-compatible endpoint, enabling easy migration for existing applications.

  * **Target Audience:** C++ developers, data scientists, and engineers building applications that require LLM integration, particularly those focused on performance, control, and multi-provider flexibility.

## 2\. Core Features

  * **Unified API:** A single, intuitive API for both standard (synchronous) and streaming (asynchronous) responses, offering a consistent interaction model regardless of the underlying LLM provider.

  * **Provider Abstraction:** Implemented through a clean provider pattern using the arbiterAI::providers::BaseLLM interface, enabling easy integration of new LLM services with minimal changes to core logic. This design ensures loose coupling and maintainability.

  * **Initial Supported Providers:**

      * OpenAI
      * Anthropic
      * DeepSeek
      * OpenRouter (to be added)
      * llama.cpp (for local models, to be added)

  * **Configuration Management:** A robust ModelManager to load model configurations and API keys from files, with fallback to environment variables, supporting remote fetching and versioning.

  * **Clear Error Handling:** A well-defined ErrorCode enum for predictable and robust error reporting.

  * **Modern C++:** Utilizes C++17 features and established libraries (cpr for HTTP requests, nlohmann\_json for JSON parsing) for reliability and performance.

## 3\. Proposed Architecture

The library's architecture is designed to be modular and extensible. The client application interacts with the main arbiterAI API, which uses the ModelManager to determine the correct provider for a given model. A factory will then create the appropriate provider instance to handle the request.

```mermaid
graph TD
    subgraph User Application
        A[Client Code]
    end

    subgraph arbiterAI Library
        B[arbiterAI API<br>(completion, streamingCompletion)]
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
        G[GitHub Repository<br>(Model Configs)]
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
    C --"loadFromGithub()"--> G

    E1 --HTTP Request (cpr)--> F1
    E2 --HTTP Request (cpr)--> F2
    E3 --HTTP Request (cpr)--> F3
    E4 --HTTP Request (cpr)--> F4
    E5 --HTTP Request (cpr)--> F5
```

## 4\. Development Roadmap

### Phase 1: Core Library Enhancement

*Goal: Solidify the existing library structure and add key providers.*

  - [x] **Refine ModelManager:** Enhance configuration loading to support multiple config files and environment variables for API keys.
  - [x] **Implement llama.cpp Provider:** Add support for local models by interacting with a llama.cpp server instance.
  - [x] **Unit Testing:** Integrate a testing framework (e.g., GoogleTest) and write initial tests for the ModelManager and providers.
  - [x] **Documentation:** Add Doxygen-style comments to all public headers to enable automated documentation generation.

### Phase 2: API and Feature Expansion

*Goal: Make the library more powerful and flexible, incorporating advanced configuration and efficiency features.

  - [x] **Embeddings API:** Introduce a `arbiterAI::embedding()` function for generating vector embeddings from text, supporting various embedding models and providers.
  - [x] **Full Streaming Support:** Fully implement and test the `streamingCompletion` function across all providers, ensuring robust and efficient asynchronous response handling.
  - [ ] **Dynamic Configuration Management:**
    - [ ] **Llama Model Download & Verification:** For `llama.cpp` configurations, extend the schema to include fields for model download URLs and corresponding SHA256 hashes, enabling automated, secure download and integrity verification of local models.
    - [ ] **Provide ranking/prefered models:** Include in the model configuration a way of ranking the models and provide a way for the library to select preferred models if suggestions are not provided.
    - [ ] **Default model and embedding models** Provide a set of default model configurations.
    - [ ] **Remote Configuration Loading:** Implement functionality to download and cache default model configurations from a central GitHub repository, enabling dynamic updates and centralized management.
    - [ ] **Configuration Schema Versioning:** Introduce a `schema_version` field to all configuration files and implement robust logic within the ModelManager to handle different schema versions gracefully, ensuring backward compatibility and preventing older software versions from attempting to load incompatible new schemas.
  - [ ] **Testing** Create tests for the above tasks and make sure eveything is functioning.

### Phase 3: Improve performance

*Goal: Improve fucntionality and performance.*

  - [ ] **Advanced Completion Options:** Extend `CompletionRequest` to include a comprehensive set of parameters (e.g., `top_p`, `stop sequences`, `temperature`, `presence_penalty`, `frequency_penalty`) and map them accurately to provider-specific APIs.
  - [ ] **Completion Efficiency & Management:**
      - [ ] **Batch Completions:** Implement an API for sending multiple completion requests in a single batch to supported providers, optimizing throughput and reducing overhead for scenarios with many small updates or independent queries.
      - [ ] **Advanced Caching:** Implement a configurable, persistent local caching mechanism for LLM responses, significantly reducing latency and API costs for repeated identical queries. This cache should be tunable (e.g., TTL, size limits).
      - [ ] **Cost Tracking, Estimation & Limits:** Enhance `CompletionResponse` to include detailed token usage (`prompt_tokens`, `completion_tokens`) and estimated cost per request. Implement an internal mechanism to track cumulative costs across providers, provide real-time cost estimation, and enforce configurable usage limits or trigger alerts when thresholds are approached.

### Phase 4: Tooling, Deployment, and Extensibility

*Goal: Improve usability, demonstrate capabilities, and add advanced features.*
  - [ ] **Implement OpenRouter Provider:** Add support for the OpenRouter API aggregator, expanding the range of accessible models and providers.
  - [ ] **Example CLI Tool:** Create a command-line application to serve as a practical usage example of the `arbiterAI` library, demonstrating various completion and embedding functionalities.
  - [ ] **Example HTTP Proxy Server:** Build a separate executable for an OpenAI-compatible HTTP proxy server using a C++ HTTP server library (e.g., httplib), showcasing how `arbiterAI` can power a backend service and enabling easy migration for existing OpenAI API users.
  - [ ] **Packaging:** Configure CMake to generate package files, allowing other CMake projects to easily find and use `arbiterAI` via `find_package`.
  - [ ] **Continuous Integration:** Set up a GitHub Actions workflow to automatically build the project, run all unit tests, and potentially run integration tests on pull requests and commits, ensuring code quality and stability.

## 5\. Suggestions for Future Development

  * **Request Retries and Backoff:** Add automatic retry logic with exponential backoff for transient network errors or API rate limit issues, making the library more resilient to temporary service disruptions.

  * **Performance Benchmarking Suite:** Create a dedicated benchmarking tool to measure the performance (latency, throughput, resource consumption) of different providers and the library's overhead. This would validate the "high-performance" goal and help users choose the best models for their needs.

  * **Plugin System for Custom Providers:** Design a more dynamic plugin system (e.g., using shared libraries) that would allow users to add their own providers without modifying the core library code. This would greatly enhance extensibility and allow for closed-source or niche provider integrations.

  * **Observability (Logging & Metrics):** Integrate a robust logging framework and expose key metrics (e.g., request counts, error rates, average latencies) to facilitate monitoring and debugging in production environments.
