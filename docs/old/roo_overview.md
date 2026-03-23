# ArbiterAI Project Overview

This document provides a comprehensive overview of the `ArbiterAI` library, detailing its architecture, public API, configuration system, and core components.

### 1. Project Structure and Architecture

The `ArbiterAI` library is built around a modular and extensible architecture, with the [`ArbiterAI`](src/arbiterAI/arbiterAI.h:239) class acting as the central singleton and primary entry point. This class serves as a facade, orchestrating the interactions between various specialized components to handle user requests.

The core architectural components are:

*   **[`ArbiterAI` Singleton](src/arbiterAI/arbiterAI.h:239):** Manages the library's lifecycle and coordinates all major operations. It initializes utility managers, handles request routing, and manages provider instances.
*   **[`ModelManager`](src/arbiterAI/modelManager.h:71):** A singleton responsible for loading, parsing, and managing model configurations from local and remote sources. It acts as a service locator, enabling the `ArbiterAI` class to find the appropriate provider for a given model.
*   **Provider System ([`BaseProvider`](src/arbiterAI/providers/baseProvider.h:25)):** A strategy pattern implementation where a common interface, `BaseProvider`, defines the contract for interacting with different LLM backends. Concrete implementations handle the specific details of each service (e.g., OpenAI, Anthropic) or local model runner (Llama.cpp).
*   **Utility Components:** A collection of helper classes that provide cross-cutting functionality such as caching ([`CacheManager`](src/arbiterAI/cacheManager.h:13)), cost tracking ([`CostManager`](src/arbiterAI/costManager.h:9)), and secure file downloading ([`ModelDownloader`](src/arbiterAI/modelDownloader.h:17), [`FileVerifier`](src/arbiterAI/fileVerifier.h:16)).

A typical request flow involves the `ArbiterAI` class first consulting the `ModelManager` to get information about the requested model. It then uses this information to select and invoke the correct provider, while optionally engaging the `CacheManager` and `CostManager` to optimize performance and enforce usage limits.

### 2. Public API

The public API is exposed primarily through the [`ArbiterAI`](src/arbiterAI/arbiterAI.h:239) class and a set of supporting data structures.

**Main Class: `ArbiterAI`**

*   **`ArbiterAI& instance()`:** Retrieves the singleton instance.
*   **`ErrorCode initialize(...)`:** Initializes the library with model configurations. Must be called first.
*   **`ErrorCode completion(...)`:** Executes a standard, blocking text completion request.
*   **`ErrorCode streamingCompletion(...)`:** Executes a non-blocking completion request, streaming the response via a callback.
*   **`std::vector<CompletionResponse> batchCompletion(...)`:** Processes multiple completion requests in a batch.
*   **`ErrorCode getEmbeddings(...)`:** Generates vector embeddings for input text.
*   **`ErrorCode getDownloadStatus(...)`:** Checks the download status of a local model.

**Core Data Structures**

*   **[`CompletionRequest`](src/arbiterAI/arbiterAI.h:91):** Defines the parameters for a completion request (e.g., `model`, `messages`, `temperature`).
*   **[`CompletionResponse`](src/arbiterAI/arbiterAI.h:166):** Contains the results of a completion, including the generated `text`, `usage` statistics, and `cost`.
*   **[`EmbeddingRequest`](src/arbiterAI/arbiterAI.h:202) / [`EmbeddingResponse`](src/arbiterAI/arbiterAI.h:222):** Structures for embedding generation.
*   **[`Message`](src/arbiterAI/arbiterAI.h:70):** Represents a single conversational turn with a `role` and `content`.

### 3. Model Configuration

The configuration system is designed to be flexible, layered, and dynamically updatable.

*   **Configuration Files:** Model definitions are stored in JSON files (e.g., [`examples/model_config_v2.json`](examples/model_config_v2.json)), which are validated against a formal schema ([`schemas/model_config.schema.json`](schemas/model_config.schema.json)). Each model entry specifies its `provider`, `ranking`, and other metadata, including optional `download` info for local models.
*   **[`ConfigDownloader`](src/arbiterAI/configDownloader.h:9):** This utility fetches the latest model configurations from a remote Git repository upon initialization. This allows for updating model definitions without releasing a new version of the library.
*   **[`ModelManager`](src/arbiterAI/modelManager.h:71):** This class orchestrates the configuration process. It loads configurations from the downloaded remote repository, any additional user-specified local paths, and a final override path. This layered approach allows for easy customization and extension of the model catalog. The `ModelManager` parses these files and populates a collection of [`ModelInfo`](src/arbiterAI/modelManager.h:47) objects that are queried at runtime.

### 4. Provider System

The provider system abstracts the implementation details of various LLM backends.

*   **[`BaseProvider` Interface](src/arbiterAI/providers/baseProvider.h:25):** This abstract class defines the essential contract for all providers, with pure virtual methods for `completion`, `streamingCompletion`, and `getEmbeddings`. This ensures a consistent interaction model for the rest of the application.
*   **Remote API Providers:** Classes like [`OpenAI`](src/arbiterAI/providers/openai.h:13) and [`Anthropic`](src/arbiterAI/providers/anthropic.h:12) implement the `BaseProvider` interface by making HTTP requests to their respective web services. They are responsible for API-specific request formatting, header construction, and response parsing.
*   **Local Provider ([`Llama`](src/arbiterAI/providers/llama.h:39)):** This provider interfaces with the `llama.cpp` library to run models locally. It manages the complexities of loading models into memory, performing local inference, and handling model file downloads and verification.

### 5. Utility Components

A set of utility classes handles common, non-core tasks, promoting separation of concerns.

*   **[`CacheManager`](src/arbiterAI/cacheManager.h:13):** An optional component that provides a file-based cache for `CompletionResponse` objects. It helps reduce costs and improve latency by storing and retrieving results for identical requests, with support for a time-to-live (TTL).
*   **[`CostManager`](src/arbiterAI/costManager.h:9):** An optional service that tracks and enforces spending limits for API calls. It persists the total cost to a file, allowing limits to be maintained across sessions.
*   **[`ModelDownloader`](src/arbiterAI/modelDownloader.h:17):** A utility used by local providers to download large model files asynchronously. It returns a `std::future` so the application can remain responsive during the download.
*   **[`FileVerifier`](src/arbiterAI/fileVerifier.h:16):** A security component integrated with the `ModelDownloader`. It computes the SHA256 hash of a downloaded file and compares it against the expected hash from the model configuration to ensure file integrity and prevent corruption or tampering.