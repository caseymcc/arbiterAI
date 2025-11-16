# ArbiterAI Project Overview

This document provides a comprehensive overview of the ArbiterAI library, its architecture, API, and key features.

## 1. Project Goal

ArbiterAI is a C++ library designed to provide a unified, flexible, and easy-to-use interface for interacting with a variety of Large Language Models (LLMs) from different providers. It abstracts away the complexities of individual provider APIs, allowing developers to switch between models and providers with minimal code changes.

Key features include:
- **Provider Abstraction**: A single interface for multiple LLM providers (both remote APIs and local models).
- **Model Configuration Management**: A robust system for defining and managing model properties and capabilities through JSON configuration files.
- **Remote Configuration**: Automatically fetches and updates model configurations from a central Git repository.
- **Caching**: Built-in response caching to reduce latency and API costs.
- **Cost Management**: Tracks and limits spending on paid LLM APIs.
- **Local Model Support**: Integrated support for running local models via `llama.cpp`.
- **Model Downloads**: Manages the download and verification of model files for local execution.

## 2. Library Structure

The library is organized into several key components, each with a distinct responsibility.

- **`ArbiterAI`** (`arbiterAI.h`, `arbiterAI.cpp`):
  - The main public-facing class and the primary entry point for all library operations.
  - It is implemented as a singleton (`ArbiterAI::instance()`).
  - It orchestrates the interactions between the other components (ModelManager, CacheManager, CostManager, and Providers).

- **`ModelManager`** (`modelManager.h`, `modelManager.cpp`):
  - Responsible for loading, managing, and providing access to model configurations.
  - It reads model definitions from JSON files, validates them, and stores them in memory.
  - It can fetch configurations from a remote repository and merge them with local overrides.

- **Providers** (`src/arbiterAI/providers/`):
  - This directory contains the concrete implementations for each supported LLM provider.
  - All providers inherit from a common `BaseProvider` class, which defines the standard interface for operations like `completion`, `streamingCompletion`, and `getEmbeddings`.
  - Current providers include: `OpenAI`, `Anthropic`, `Deepseek`, `OpenRouter`, and `Llama` (for local models).

- **`CacheManager`** (`cacheManager.h`, `cacheManager.cpp`):
  - Implements a cache for `CompletionRequest` / `CompletionResponse` pairs.
  - When enabled, it intercepts completion requests and returns a cached response if one exists, avoiding a call to the provider's API.

- **`CostManager`** (`costManager.h`, `costManager.cpp`):
  - Tracks the cumulative cost of API calls based on token usage and pricing information defined in the model configurations.
  - It can be configured with a spending limit to prevent budget overruns.

- **`ConfigDownloader`** (`configDownloader.h`, `configDownloader.cpp`):
  - A utility class used by `ModelManager` to clone or pull updates from a remote Git repository containing model configuration files.

- **`ModelDownloader`** (`modelDownloader.h`, `modelDownloader.cpp`):
  - Handles the asynchronous download of model files for local providers like `llama.cpp`. It supports progress tracking and file integrity verification (SHA256).

## 3. Model Configuration

The behavior and availability of models are entirely defined by JSON configuration files.

### Configuration Loading

1.  **Remote Repository**: On initialization, `ModelManager` uses `ConfigDownloader` to fetch a base set of model configurations from a remote Git repository (default: `https://github.com/caseymcc/arbiterAI_config.git`).
2.  **Local Directories**: The library can then load configurations from additional local directories specified during initialization.
3.  **Overrides**: Configurations from local files will override the ones fetched remotely if they define the same model, allowing for user-specific modifications (e.g., adding API keys).

### Configuration Schema

Each JSON file can contain an array of "models". Key fields for each model object include:

- `model`: (string, required) The unique name for the model (e.g., "gpt-4o").
- `provider`: (string, required) The name of the provider to use for this model (e.g., "openai", "llama").
- `ranking`: (integer) A number from 0-100 to rank models, influencing which one is chosen by default.
- `pricing`: (object) Contains `prompt_token_cost` and `completion_token_cost` for cost tracking.
- `context_window`: (integer) The maximum context window size for the model.
- `api_base`: (string, optional) The base URL for the API endpoint.
- `download`: (object, optional) For local models, contains the `url`, `sha256` hash, and `cachePath` for the model file.

A formal schema is defined in `schemas/model_config.schema.json` which can be used for validation.

## 4. Core API

All interactions with the library are done through the `ArbiterAI` singleton class.

### Initialization

```cpp
#include "arbiterAI/arbiterAI.h"

// Get the singleton instance
arbiterAI::ArbiterAI& ai = arbiterAI::ArbiterAI::instance();

// Initialize with a list of local config paths
std::vector<std::filesystem::path> configPaths = {"/path/to/my/configs"};
ai.initialize(configPaths);
```

### API Methods

- **`ErrorCode completion(const CompletionRequest& request, CompletionResponse& response)`**:
  - Performs a standard, blocking completion request.

- **`ErrorCode streamingCompletion(const CompletionRequest& request, std::function<void(const std::string&)> callback)`**:
  - Performs a non-blocking completion request, invoking the callback function for each received chunk of the response.

- **`std::vector<CompletionResponse> batchCompletion(const std::vector<CompletionRequest>& requests)`**:
  - Sends multiple completion requests in parallel. It groups requests by provider to optimize API calls.

- **`ErrorCode getEmbeddings(const EmbeddingRequest& request, EmbeddingResponse& response)`**:
  - Requests vector embeddings for a given input string or list of strings.

- **`ErrorCode getDownloadStatus(const std::string& modelName, std::string& error)`**:
  - Checks the download status of a model file (`NotStarted`, `InProgress`, `Completed`, `Failed`).

### Data Structures

- **`CompletionRequest`**: Contains all parameters for a completion call, including `model`, a vector of `Message` objects, `temperature`, `max_tokens`, etc.
- **`CompletionResponse`**: Contains the result of a completion, including the generated `text`, `model` name, `usage` statistics, and calculated `cost`.
- **`EmbeddingRequest` / `EmbeddingResponse`**: Structures for embedding requests and their results.
- **`Message`**: A simple struct with `role` and `content` to represent a part of a conversation.

## 5. Building and Usage

The project uses CMake for building. The `CMakeLists.txt` file defines the build targets, including the main library (`libarbiterai.a`), tests, and examples.

- **Examples**: The `examples/` directory contains sample code showing how to use the library.
  - `cli/main.cpp`: A command-line interface for performing completions.
  - `proxy/main.cpp`: A proxy server example.
- **Tests**: The `tests/` directory contains unit tests for various components of the library.
