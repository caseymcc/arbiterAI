# Project Overview
- **Project Name**: arbiterAI

- **Description**: A modern, high-performance C++17 library designed to provide a unified, embeddable interface for interacting with various Large Language Model (LLM) providers. It aims to simplify the process of integrating LLM capabilities into C++ applications by offering a single, consistent API. This library is envisioned as a foundational component for C++ developers building AI-powered applications that require robust, efficient, and flexible LLM integration without being tied to a single provider.

- **Primary Goal**: To create a high-performance, easy-to-use, robust, and extensible C++ library that can be seamlessly integrated into other applications, abstracting away LLM provider complexities.

- **Secondary Goal**: To develop a production-ready example HTTP proxy server that demonstrates the library's capabilities and provides an OpenAI-compatible endpoint, enabling easy migration for existing applications.

- Target Audience: C++ developers, data scientists, and engineers building applications that require LLM integration, particularly those focused on performance, control, and multi-provider flexibility.

- Unique Selling Proposition (USP):

  - Unified C++ Interface: Simplifies LLM integration for C++ applications with a consistent API across diverse providers.

  - High Performance: Leverages modern C++17 features and efficient third-party libraries for optimal performance.

  - Extensibility: Designed with a clear provider pattern, making it straightforward to add new LLM services or local models.

  - Robust Error Handling: Provides clear and predictable error reporting to facilitate debugging and application stability.

# Core Features
- Unified API: A single, intuitive API for both standard (synchronous) and streaming (asynchronous) responses, offering a consistent interaction model regardless of the underlying LLM provider.

- Provider Abstraction: Implemented through a clean provider pattern using the arbiterAI::providers::BaseLLM interface, enabling easy integration of new LLM services with minimal changes to core logic. This design ensures loose coupling and maintainability.

- Initial Supported Providers:

  - OpenAI: Full support for OpenAI's ChatCompletion and Completion APIs.

  - Anthropic: Integration with Anthropic's Messages API.

  - DeepSeek: Support for DeepSeek's ChatCompletion API.

  - llama.cpp (for local models): Integration with the llama.cpp libraries and HTTP server, allowing seamless use of local, self-hosted LLMs.

  - OpenRouter (to be added): A unified interface for numerous models accessible via the OpenRouter aggregation service.



Configuration Management: A robust arbiterAI::ModelManager to dynamically load model configurations, provider endpoints, and API keys from multiple sources:

File-based: Support for YAML/JSON configuration files (e.g., models.yaml, config.json).

Environment Variables: Secure fallback and override capabilities using environment variables (e.g., OPENAI_API_KEY, ANTHROPIC_API_KEY).

Prioritization: Clear rules for configuration precedence (e.g., environment variables override file configs).

Clear Error Handling: A well-defined ErrorCode enum, combined with custom exception types (e.g., arbiterAI::LLMError, arbiterAI::ConfigError), for predictable, robust, and granular error reporting, allowing client applications to handle specific failure modes effectively.

Modern C++ Design: Utilizes C++17 features (e.g., std::optional, std::variant, structured bindings) and established, high-quality third-party libraries (cpr for asynchronous HTTP requests, nlohmann_json for efficient JSON parsing and generation, spdlog for logging) for reliability, performance, and maintainability.

Streaming Support: Asynchronous streamingCompletion function designed to handle Server-Sent Events (SSE) for real-time token delivery, crucial for interactive applications.