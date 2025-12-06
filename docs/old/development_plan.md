# **Project Plan: arbiterAI**

## **1\. Project Overview**

* **Project Name:** arbiterAI  
* **Description:** A modern, high-performance C++17 library designed to provide a unified, embeddable interface for interacting with various Large Language Model (LLM) providers. It aims to simplify the process of integrating LLM capabilities into C++ applications by offering a single, consistent API. This library is envisioned as a foundational component for C++ developers building AI-powered applications that require robust, efficient, and flexible LLM integration without being tied to a single provider. The library will include robust configuration management that can retrieve model definitions directly from a GitHub repository, incorporating schema versioning to ensure backward compatibility. For local models like Llama, configurations will specify model download URLs and cryptographic hashes for verification. Furthermore, it will support efficient batch completions for scenarios involving numerous file updates, provide detailed cost tracking, estimation, and allow for setting usage limits, and incorporate intelligent caching for frequently requested completions to optimize performance and reduce API expenses.  
* **Primary Goal:** To create a high-performance, easy-to-use, robust, and extensible C++ library that can be seamlessly integrated into other applications, abstracting away LLM provider complexities. A key aspect of this goal is to provide **comprehensive local LLM support, including the ability to calculate required storage space and estimate hardware (GPU) resource consumption, enabling intelligent model deployment and execution on acceleration devices.**  
* **Secondary Goal:** To develop a production-ready example HTTP proxy server that demonstrates the library's capabilities and provides an OpenAI-compatible endpoint, enabling easy migration for existing applications.  
* **Target Audience:** C++ developers, data scientists, and engineers building applications that require LLM integration, particularly those focused on performance, control, and multi-provider flexibility, and those who need to manage local LLM deployments efficiently.

## **2\. Core Features**

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

## **3\. Proposed Architecture**

The library's architecture is designed to be modular and extensible. The client application interacts with the main arbiterAI API, which uses the ModelManager to determine the correct provider for a given model. A factory will then create the appropriate provider instance to handle the request. For local LLMs, the llama.cpp provider will interact with a local server instance, which can leverage hardware acceleration (GPUs) if configured. The library will also be designed to query system resources to inform model deployment decisions.

graph TD  
    subgraph User Application  
        A\[Client Code\]  
    end

    subgraph arbiterAI Library  
        B\[arbiterAI API\<br\>(completion, streamingCompletion, embedding)\]  
        C\[ModelManager\]  
        D{Provider Factory}  
        subgraph LLM Providers  
            E1\[OpenAI\_LLM\]  
            E2\[Anthropic\_LLM\]  
            E3\[DeepSeek\_LLM\]  
            E4\[LlamaCpp\_LLM\<br\>(Local Model/GPU Support)\]  
            E5\[OpenRouter\_LLM\]  
        end  
        H\[Resource Estimator\<br\>(Disk/VRAM)\]  
    end

    subgraph External Services  
        F1\[OpenAI API\]  
        F2\[Anthropic API\]  
        F3\[DeepSeek API\]  
        F4\[llama.cpp server\]  
        F5\[OpenRouter API\]  
        G\[GitHub Repository\<br\>(Model Configs)\]  
        I\[System Hardware\<br\>(GPU, CPU, Disk)\]  
    end

    A \--\> B  
    B \--\> C  
    B \--\> D  
    C \--"getModelInfo()"--\> B  
    D \--"createProvider(model)"--\> E1  
    D \--\> E2  
    D \--\> E3  
    D \--\> E4  
    D \--\> E5  
    C \--"loadFromGithub()"--\> G

    E1 \--HTTP Request (cpr)--\> F1  
    E2 \--HTTP Request (cpr)--\> F2  
    E3 \--HTTP Request (cpr)--\> F3  
    E4 \--HTTP Request (cpr)--\> F4  
    E5 \--HTTP Request (cpr)--\> F5  
    E4 \--Queries--\> I  
    H \--Queries--\> I  
    B \--Uses--\> H

## **4\. Development Roadmap**

### **Phase 1: Core Library Enhancement**

*Goal: Solidify the existing library structure and add key providers.*

* \[x\] **Refine ModelManager:** Enhance configuration loading to support multiple config files and environment variables for API keys.  
* \[x\] **Implement llama.cpp Provider:** Add support for local models by interacting with a llama.cpp server instance.  
* \[x\] **Unit Testing:** Integrate a testing framework (e.g., GoogleTest) and write initial tests for the ModelManager and providers.  
* \[x\] **Documentation:** Add Doxygen-style comments to all public headers to enable automated documentation generation.

### **Phase 2: API and Feature Expansion**

*Goal: Make the library more powerful and flexible, incorporating advanced configuration and efficiency features.*

* \[x\] **Embeddings API:** Introduce a arbiterAI::embedding() function for generating vector embeddings from text, supporting various embedding models and providers.  
* \[x\] **Full Streaming Support:** Fully implement and test the streamingCompletion function across all providers, ensuring robust and efficient asynchronous response handling.  
* \[x\] **Dynamic Configuration Management:**  
  * \[x\] **Llama Model Download & Verification:** For llama.cpp configurations, extend the schema to include fields for model download URLs and corresponding SHA256 hashes, enabling automated, secure download and integrity verification of local models.  
  * \[x\] **Provide ranking/prefered models:** Include in the model configuration a way of ranking the models and provide a way for the library to select preferred models if suggestions are not provided.  
  * \[x\] **Default model and embedding models** Provide a set of default model configurations.  
  * \[x\] **Configuration Schema Versioning:** Introduce a schema\_version field to all configuration files and implement robust logic within the ModelManager to handle different schema versions gracefully, ensuring backward compatibility and preventing older software versions from attempting to load incompatible new schemas.  
* \[X\] **Testing** Create tests for the above tasks and make sure everything is functioning.  
* \[x\] **Project Renaming:** The project was renamed from hermesaxiom to arbiterAI to better reflect its purpose as a neutral arbiter between different LLM providers.

### **Phase 3: Remote Configuration**

*Goal: Provide remote configurations that can be updated outside of the library updates.*

* \[x\] **Configuration Location:** Move the configurations (configs directory) to its own repo [https://github.com/caseymcc/arbiterAI\_config.git](https://github.com/caseymcc/arbiterAI_config.git) (repo is created on github but not initialized)  
* \[x\] **Remote Configuration Loading:** Implement functionality to download and cache default model configurations from the central GitHub repository, enabling dynamic updates and centralized management.  
* \[x\] **Check For Updates:** When the library opens query the GitHub repo for any updates.  
* \[x\] **Configuration Versioning within Repository:** Beyond schema versioning, implement a system for versioning the configuration files themselves within the arbiterAI\_config repository (e.g., using Git tags or a dedicated versioning scheme). This allows the library to request specific configuration versions and roll back if a new configuration introduces issues.  
* \[x\] **User Notification/Logging for Updates:** Implement logging to inform the user or application developer when configurations are updated (or fail to update) from the remote repository. This provides transparency and aids in debugging.  
* \[x\] **Environment-Specific Overrides:** Consider allowing users to provide local configuration files that can override specific settings from the remotely loaded configurations. This provides flexibility for environment-specific adjustments (e.g., different API keys for development vs. production) without modifying the central remote configurations.

### **Phase 4: Improve performance**

*Goal: Improve functionality and performance.*

* \[x\] **Advanced Completion Options:** Extend CompletionRequest to include a comprehensive set of parameters (e.g., top\_p, stop sequences, temperature, presence\_penalty, frequency\_penalty) and map them accurately to provider-specific APIs.  
* \[x\] **Completion Efficiency & Management:**  
  * \[x\] **Batch Completions:** Implement an API for sending multiple completion requests in a single batch to supported providers, optimizing throughput and reducing overhead for scenarios with many small updates or independent queries.  
  * \[x\] **Advanced Caching:** Implement a configurable, persistent local caching mechanism for LLM responses, significantly reducing latency and API costs for repeated identical queries. This cache should be tunable (e.g., TTL, size limits).  
  * \[x\] **Cost Tracking, Estimation & Limits:** Enhance CompletionResponse to include detailed token usage (prompt\_tokens, completion\_tokens) and estimated cost per request. Implement an internal mechanism to track cumulative costs across providers, provide real-time cost estimation, and enforce configurable usage limits or trigger alerts when thresholds are approached.

### **Phase 5: Tooling, Deployment, and Extensibility**

*Goal: Improve usability, demonstrate capabilities, and add advanced features.*

* \[x\] **Implement OpenRouter Provider:** Add support for the OpenRouter API aggregator, expanding the range of accessible models and providers.  
* \[x\] **Example CLI Tool:** Create a command-line application to serve as a practical usage example of the arbiterAI library, demonstrating various completion and embedding functionalities.  
* \[x\] **Example HTTP Proxy Server:** Build a separate executable for an OpenAI-compatible HTTP proxy server using a C++ HTTP server library (e.g., httplib), showcasing how arbiterAI can power a backend service and enabling easy migration for existing OpenAI API users.  
* \[x\] **Packaging:** Configure CMake to generate package files, allowing other CMake projects to easily find and use arbiterAI via find\_package.  
* \[x\] **Continuous Integration:** Set up a GitHub Actions workflow to automatically build the project, run all unit tests, and potentially run integration tests on pull requests and commits, ensuring code quality and stability.

## **5\. Suggestions for Future Development**

* **Local LLM Resource Management & Optimization:**  
  * **Model Storage Calculation:** Implement functionality to calculate the exact disk space required for a given local LLM model (e.g., based on its GGUF file size, quantization level, and other metadata). This will aid users in planning storage requirements before download.  
  * **Hardware Acceleration (GPU) VRAM Estimation:** Develop a sophisticated mechanism to estimate the Video RAM (VRAM) required to load and run a specified local LLM model on a GPU. This estimation should consider factors such as:  
    * Model size and quantization.  
    * Context window size.  
    * Batch size for inference.  
    * Overhead for the llama.cpp runtime and operating system.  
  * **Intelligent Device Selection & Offloading:**  
    * **Automatic Device Detection:** Integrate with platform-specific APIs (e.g., NVML for NVIDIA GPUs, ROCm for AMD GPUs, Metal for Apple Silicon) to automatically detect available GPUs and their VRAM capacity.  
    * **Optimal Device Selection:** Allow the library to intelligently select the most suitable GPU for a given model based on estimated VRAM requirements and available resources.  
    * **Layer Offloading Configuration:** Provide explicit control or intelligent defaults for offloading specific model layers to the CPU when VRAM is insufficient, balancing performance with memory constraints.  
  * **System Resource Monitoring Integration:** Implement real-time monitoring of system resources (CPU, RAM, GPU VRAM, disk I/O) to inform dynamic load balancing, prevent out-of-memory errors, and provide diagnostic information.  
* **Request Retries and Backoff:** Add automatic retry logic with exponential backoff for transient network errors or API rate limit issues, making the library more resilient to temporary service disruptions.  
* **Performance Benchmarking Suite:** Create a dedicated benchmarking tool to measure the performance (latency, throughput, resource consumption) of different providers and the library's overhead. This would validate the "high-performance" goal and help users choose the best models for their needs.  
* **Plugin System for Custom Providers:** Design a more dynamic plugin system (e.g., using shared libraries) that would allow users to add their own providers without modifying the core library code. This would greatly enhance extensibility and allow for closed-source or niche provider integrations.  
* **Observability (Logging & Metrics):** Integrate a robust logging framework and expose key metrics (e.g., request counts, error rates, average latencies) to facilitate monitoring and debugging in production environments.  
* **Advanced Prompt Engineering Features:** Explore adding built-in utilities for common prompt engineering patterns, such as templating, few-shot examples management, and support for structured outputs (e.g., JSON schema adherence).  
* **Quantification of Performance Goals:** Define specific, measurable performance targets (e.g., "achieve \<100ms latency for single-turn completions on gpt-3.5-turbo," "process 100 batch completion requests per second for local Llama-7B on an RTX 4090"). This will provide clear benchmarks for success.  
* **Comprehensive Error Reporting and Diagnostics:** Enhance error messages to be more user-friendly and provide actionable advice. Implement diagnostic tools that can help identify issues with configuration, network connectivity, or provider-specific errors.