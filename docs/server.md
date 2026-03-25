# ArbiterAI Server

Standalone HTTP server that wraps the ArbiterAI library, providing an OpenAI-compatible chat completions API, model management, runtime configuration, telemetry, and a live dashboard.

## Table of Contents

1. [Overview](#1-overview)
2. [Running the Server](#2-running-the-server)
3. [API Reference](#3-api-reference)
   - [OpenAI-Compatible Endpoints](#31-openai-compatible-endpoints)
   - [Model Management](#32-model-management)
   - [Model Config Injection](#33-model-config-injection)
   - [Telemetry](#34-telemetry)
   - [Health & Version](#35-health--version)
   - [Dashboard](#36-dashboard)
4. [Configuration Persistence](#4-configuration-persistence)
5. [Error Format](#5-error-format)

---

## 1. Overview

`arbiterAI-server` is a separate CMake target that links against the core `arbiterai` library. It uses [cpp-httplib](https://github.com/yhirose/cpp-httplib) for HTTP serving — this keeps `cpp-httplib` as a dependency of the server only, not the core library.

The server supports:

- **OpenAI-compatible API** — Drop-in replacement for `/v1/chat/completions`, `/v1/models`, and `/v1/embeddings`
- **Streaming** — Server-Sent Events (SSE) for real-time token delivery
- **Model lifecycle management** — Load, unload, pin, and download models at runtime
- **Runtime model config injection** — Add, update, or remove model configurations via REST without restarting
- **Telemetry** — System snapshots, inference history, swap history, and hardware info
- **Live dashboard** — Browser-based UI at `/dashboard`
- **CORS** — All responses include permissive CORS headers

---

## 2. Running the Server

```bash
# From inside the Docker container
./build/linux_x64_debug/arbiterAI-server [options]
```

### CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `-p, --port` | `8080` | HTTP port |
| `-H, --host` | `0.0.0.0` | Bind address |
| `-c, --config` | `config` | Model config directory path(s) |
| `-m, --model` | *(none)* | Default model to load on startup |
| `-v, --variant` | *(none)* | Default quantization variant (e.g., `Q4_K_M`) |
| `--override-path` | *(none)* | Path to write runtime model config overrides (enables persistence) |
| `--ram-budget` | `0` (auto 50%) | Ready-model RAM budget in MB |
| `--log-level` | `info` | Log level (`trace`, `debug`, `info`, `warn`, `error`) |
| `-h, --help` | | Print usage |

### Examples

```bash
# Start with defaults
./arbiterAI-server

# Custom port, load a model on startup
./arbiterAI-server -p 9090 -m gpt-4 --log-level debug

# Enable runtime config persistence
./arbiterAI-server --override-path /data/overrides.json

# Load a local model with a specific variant
./arbiterAI-server -m qwen2.5-7b-instruct -v Q4_K_M --ram-budget 8192
```

---

## 3. API Reference

All endpoints return JSON. Request bodies must be `Content-Type: application/json`.

### 3.1 OpenAI-Compatible Endpoints

These endpoints follow the [OpenAI API specification](https://platform.openai.com/docs/api-reference) so existing OpenAI client libraries can be used as-is by pointing them at the server's base URL.

#### `POST /v1/chat/completions`

Create a chat completion. Supports both streaming and non-streaming modes.

**Request body:**

```json
{
  "model": "gpt-4",
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "Hello!"}
  ],
  "temperature": 0.7,
  "max_tokens": 1024,
  "stream": false,
  "top_p": 1.0,
  "presence_penalty": 0.0,
  "frequency_penalty": 0.0,
  "stop": ["\n"],
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "get_weather",
        "description": "Get current weather",
        "parameters": {
          "type": "object",
          "properties": {
            "location": {"type": "string"}
          },
          "required": ["location"]
        }
      }
    }
  ],
  "tool_choice": "auto"
}
```

**Non-streaming response** (`stream: false` or omitted):

```json
{
  "id": "chatcmpl-abc123...",
  "object": "chat.completion",
  "created": 1711000000,
  "model": "gpt-4",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "Hello! How can I help?"
      },
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 12,
    "completion_tokens": 8,
    "total_tokens": 20
  }
}
```

**Streaming response** (`stream: true`):

Returns `text/event-stream` with Server-Sent Events. Each event is a `data:` line containing a JSON chunk:

```
data: {"id":"chatcmpl-abc123...","object":"chat.completion.chunk","created":1711000000,"model":"gpt-4","choices":[{"index":0,"delta":{"role":"assistant","content":"Hello"},"finish_reason":null}]}

data: {"id":"chatcmpl-abc123...","object":"chat.completion.chunk","created":1711000000,"model":"gpt-4","choices":[{"index":0,"delta":{"content":"!"},"finish_reason":null}]}

data: {"id":"chatcmpl-abc123...","object":"chat.completion.chunk","created":1711000000,"model":"gpt-4","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

If `stream_options.include_usage` is `true`, a final chunk with usage statistics is included before `[DONE]`.

**Notes:**

- `max_tokens` and `max_completion_tokens` are both accepted (OpenAI compatibility).
- `n`, `response_format`, `logprobs`, `user`, and `seed` are accepted but ignored.
- Tool calling follows the OpenAI `tools` array format.

#### `GET /v1/models`

List all available models.

**Response:**

```json
{
  "object": "list",
  "data": [
    {
      "id": "gpt-4",
      "object": "model",
      "created": 1711000000,
      "owned_by": "openai"
    }
  ]
}
```

#### `GET /v1/models/:id`

Get information about a specific model.

**Response (200):**

```json
{
  "id": "gpt-4",
  "object": "model",
  "created": 1711000000,
  "owned_by": "openai"
}
```

**Response (404):**

```json
{
  "error": {
    "message": "Model 'unknown' not found",
    "type": "not_found_error",
    "param": null,
    "code": null
  }
}
```

#### `POST /v1/embeddings`

Generate embeddings for input text.

**Request body:**

```json
{
  "model": "text-embedding-ada-002",
  "input": "Hello, world!"
}
```

**Response:**

```json
{
  "object": "list",
  "data": [
    {
      "object": "embedding",
      "index": 0,
      "embedding": [0.0023, -0.0091, ...]
    }
  ],
  "model": "text-embedding-ada-002",
  "usage": {
    "prompt_tokens": 4,
    "total_tokens": 4
  }
}
```

---

### 3.2 Model Management

Manage the runtime lifecycle of models — loading into VRAM, unloading, pinning for quick reload, and downloading.

#### `GET /api/models`

List all available models with hardware fit information for local models.

**Response:**

```json
{
  "models": [
    {
      "model": "qwen2.5-7b-instruct",
      "variant": "Q4_K_M",
      "can_run": true,
      "max_context_size": 32768,
      "limiting_factor": "vram",
      "estimated_vram_mb": 5120,
      "gpu_indices": [0]
    },
    {
      "model": "gpt-4",
      "variant": "",
      "can_run": true,
      "max_context_size": 0,
      "limiting_factor": "",
      "estimated_vram_mb": 0,
      "gpu_indices": []
    }
  ]
}
```

#### `GET /api/models/loaded`

List currently loaded or tracked models.

**Response:**

```json
{
  "models": [
    {
      "model": "qwen2.5-7b-instruct",
      "variant": "Q4_K_M",
      "state": "Loaded",
      "vram_usage_mb": 5120,
      "ram_usage_mb": 0,
      "estimated_vram_mb": 5120,
      "context_size": 4096,
      "gpu_indices": [0],
      "pinned": false
    }
  ]
}
```

Model states: `Unloaded`, `Downloading`, `Ready`, `Loaded`, `Unloading`.

#### `POST /api/models/:name/load`

Load a model into VRAM for inference.

**Query parameters:**

| Parameter | Description |
|-----------|-------------|
| `variant` | Quantization variant (e.g., `Q4_K_M`). Omit to auto-select. |
| `context` | Context size. Omit for model default. |

**Response (200):** `{"status": "loaded", "model": "qwen2.5-7b-instruct"}`

**Response (202):** `{"status": "downloading", "model": "qwen2.5-7b-instruct"}` — model file is being downloaded.

#### `POST /api/models/:name/unload`

Unload a model from VRAM. Pinned models move to `Ready` state instead.

**Response (200):** `{"status": "unloaded", "model": "qwen2.5-7b-instruct"}`

#### `POST /api/models/:name/pin`

Pin a model to keep it in RAM for quick reload after eviction from VRAM.

**Response (200):** `{"status": "pinned", "model": "qwen2.5-7b-instruct"}`

#### `POST /api/models/:name/unpin`

Unpin a model, allowing LRU eviction.

**Response (200):** `{"status": "unpinned", "model": "qwen2.5-7b-instruct"}`

#### `POST /api/models/:name/download`

Initiate a model download. Query parameter `variant` selects the quantization variant.

**Response (200):** `{"status": "already_available", "model": "..."}` — already downloaded.

**Response (202):** `{"status": "downloading", "model": "..."}` — download started.

#### `GET /api/models/:name/download`

Get download status for a model.

**Response:**

```json
{
  "model": "qwen2.5-7b-instruct",
  "state": "Downloading"
}
```

---

### 3.3 Model Config Injection

Add, update, retrieve, or remove model configurations at runtime without restarting the server. All config injection endpoints use the same JSON format as model config files.

#### `POST /api/models/config` — Add Model(s)

Add one or more new model configurations. Fails if any model already exists (atomic — no partial adds).

**Single model request:**

```json
{
  "model": "gpt-4o-mini",
  "provider": "openai",
  "ranking": 80,
  "max_tokens": 16384,
  "context_window": 128000,
  "pricing": {
    "prompt_token_cost": 0.00015,
    "completion_token_cost": 0.0006
  }
}
```

**Bulk request:**

```json
{
  "models": [
    {
      "model": "gpt-4o-mini",
      "provider": "openai",
      "ranking": 80
    },
    {
      "model": "my-local-model",
      "provider": "llama",
      "file_path": "/models/custom.gguf"
    }
  ]
}
```

**Response (201):** `{"added": ["gpt-4o-mini", "my-local-model"]}`

**Response (409):** Model already exists. No models are added.

**Response (400):** Validation error (missing required fields).

**Supported model config fields:**

| Field | Required | Description |
|-------|----------|-------------|
| `model` | Yes | Unique model identifier |
| `provider` | Yes | Provider type (`openai`, `anthropic`, `deepseek`, `openrouter`, `llama`, `mock`) |
| `ranking` | No | Preference order 0–100 (default: 50) |
| `mode` | No | Operation mode (default: `"chat"`) |
| `api_base` | No | Custom API endpoint URL |
| `file_path` | No | Local model file path |
| `api_key` | No | API key for this model |
| `context_window` | No | Context window size in tokens |
| `max_tokens` | No | Maximum tokens per completion |
| `max_input_tokens` | No | Maximum input tokens |
| `max_output_tokens` | No | Maximum output tokens |
| `pricing` | No | `{prompt_token_cost, completion_token_cost}` |
| `hardware_requirements` | No | `{min_system_ram_mb, parameter_count}` |
| `context_scaling` | No | `{base_context, max_context, vram_per_1k_context_mb}` |
| `variants` | No | Array of quantization variants (local models) |

**Variant object fields:**

| Field | Description |
|-------|-------------|
| `quantization` | Quantization format (e.g., `Q4_K_M`, `Q8_0`, `F16`) |
| `file_size_mb` | File size in MB |
| `min_vram_mb` | Minimum VRAM in MB |
| `recommended_vram_mb` | Recommended VRAM in MB |
| `download` | `{url, sha256, filename}` |

#### `PUT /api/models/config` — Add or Update Model(s)

Create new models or merge updated fields into existing models. Same request format as POST.

**Response (200):**

```json
{
  "updated": ["gpt-4o-mini"],
  "added": ["my-local-model"]
}
```

#### `GET /api/models/config/:name` — Get Model Config

Retrieve the full configuration for a model.

**Response (200):**

```json
{
  "model": "gpt-4o-mini",
  "provider": "openai",
  "mode": "chat",
  "ranking": 80,
  "context_window": 128000,
  "max_tokens": 16384,
  "max_input_tokens": 3072,
  "max_output_tokens": 1024,
  "pricing": {
    "prompt_token_cost": 0.00015,
    "completion_token_cost": 0.0006
  }
}
```

**Response (404):** Model not found.

#### `DELETE /api/models/config/:name` — Remove Model Config

Remove a model configuration. If the model is currently loaded, it is unloaded first.

**Response (200):** `{"status": "removed", "model": "gpt-4o-mini"}`

**Response (404):** Model not found.

#### Usage Examples

```bash
# Add a new cloud model
curl -X POST http://localhost:8080/api/models/config \
  -H "Content-Type: application/json" \
  -d '{
    "model": "gpt-4o-mini",
    "provider": "openai",
    "ranking": 80,
    "context_window": 128000,
    "pricing": {"prompt_token_cost": 0.00015, "completion_token_cost": 0.0006}
  }'

# Add a local GGUF model
curl -X POST http://localhost:8080/api/models/config \
  -H "Content-Type: application/json" \
  -d '{
    "model": "my-qwen-7b",
    "provider": "llama",
    "file_path": "/models/Qwen2.5-7B-Instruct-Q4_K_M.gguf",
    "context_window": 32768,
    "variants": [{
      "quantization": "Q4_K_M",
      "file_size_mb": 4680,
      "min_vram_mb": 5120,
      "download": {
        "url": "https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/main/Qwen2.5-7B-Instruct-Q4_K_M.gguf",
        "sha256": "abc123...",
        "filename": "Qwen2.5-7B-Instruct-Q4_K_M.gguf"
      }
    }]
  }'

# Update a model's ranking
curl -X PUT http://localhost:8080/api/models/config \
  -H "Content-Type: application/json" \
  -d '{"model": "gpt-4o-mini", "provider": "openai", "ranking": 95}'

# Get a model's config
curl http://localhost:8080/api/models/config/gpt-4o-mini

# Remove a model
curl -X DELETE http://localhost:8080/api/models/config/gpt-4o-mini

# Use the injected model immediately
curl -X POST http://localhost:8080/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"model": "gpt-4o-mini", "messages": [{"role": "user", "content": "Hello!"}]}'
```

---

### 3.4 Telemetry

#### `GET /api/stats`

Current system snapshot including hardware, loaded models, and performance metrics.

**Response:**

```json
{
  "hardware": {
    "total_ram_mb": 32768,
    "free_ram_mb": 16384,
    "cpu_cores": 12,
    "cpu_utilization_percent": 25.0,
    "gpus": [
      {
        "index": 0,
        "name": "NVIDIA RTX 3060",
        "backend": "CUDA",
        "vram_total_mb": 12288,
        "vram_free_mb": 8192,
        "compute_capability": 8.6,
        "utilization_percent": 10.0
      }
    ]
  },
  "models": [],
  "avg_tokens_per_second": 42.5,
  "active_requests": 0
}
```

#### `GET /api/stats/history`

Inference history within a time window.

**Query parameters:**

| Parameter | Default | Description |
|-----------|---------|-------------|
| `minutes` | `5` | Time window in minutes (1–60) |

**Response:**

```json
[
  {
    "model": "gpt-4",
    "variant": "",
    "tokens_per_second": 45.2,
    "prompt_tokens": 120,
    "completion_tokens": 80,
    "latency_ms": 150.0,
    "total_time_ms": 1800.0
  }
]
```

#### `GET /api/stats/swaps`

Model swap history.

**Response:**

```json
[
  {
    "from": "model-a",
    "to": "model-b",
    "time_ms": 350.0
  }
]
```

#### `GET /api/hardware`

Current hardware information (refreshed on each call).

**Response:**

```json
{
  "total_ram_mb": 32768,
  "free_ram_mb": 16384,
  "cpu_cores": 12,
  "cpu_utilization_percent": 25.0,
  "gpus": [
    {
      "index": 0,
      "name": "NVIDIA RTX 3060",
      "backend": "CUDA",
      "vram_total_mb": 12288,
      "vram_free_mb": 8192,
      "compute_capability": 8.6,
      "utilization_percent": 10.0
    }
  ]
}
```

---

### 3.5 Health & Version

#### `GET /health` (or `/v1/health`)

Health check.

**Response:** `{"status": "ok", "version": "0.2.5"}`

#### `GET /api/version`

Library version.

**Response:**

```json
{
  "version": "0.2.5",
  "major": 0,
  "minor": 2,
  "patch": 5
}
```

---

### 3.6 Dashboard

#### `GET /dashboard`

Returns an HTML page with a live-updating dashboard showing:

- System info (GPU names, VRAM, RAM, CPU)
- Loaded models with state, variant, context size, GPU assignment
- Performance charts (tokens/sec, memory usage)
- Model management controls (load/unload/pin)

Open in a browser: `http://localhost:8080/dashboard`

---

## 4. Configuration Persistence

By default, model configs added via the injection API exist only in memory and are lost when the server restarts.

To enable persistence, pass `--override-path`:

```bash
./arbiterAI-server --override-path /data/runtime_models.json
```

When set:

- Every `POST`, `PUT`, or `DELETE` to `/api/models/config` writes runtime-injected models to the specified file.
- The file uses the standard model config format (`schema_version` + `models` array).
- Writes are atomic (temp file + rename) to prevent corruption.
- On restart, pass the override file's parent directory as a config path, or use `ModelManager`'s `localOverridePath` parameter — the file is loaded after all other configs, so runtime injections take precedence.

**Override file format:**

```json
{
  "schema_version": "1.1.0",
  "models": [
    {
      "model": "gpt-4o-mini",
      "provider": "openai",
      "ranking": 95,
      "context_window": 128000,
      "max_tokens": 16384,
      "max_input_tokens": 3072,
      "max_output_tokens": 1024
    }
  ]
}
```

---

## 5. Error Format

All error responses follow the OpenAI error format:

```json
{
  "error": {
    "message": "Descriptive error message",
    "type": "invalid_request_error",
    "param": null,
    "code": null
  }
}
```

Error types used:

| Type | Description |
|------|-------------|
| `invalid_request_error` | Malformed request, validation failure, or operation error |
| `not_found_error` | Model or resource not found |
| `server_error` | Internal server error |

HTTP status codes:

| Code | Usage |
|------|-------|
| `200` | Success |
| `201` | Created (model config added) |
| `202` | Accepted (model downloading) |
| `400` | Bad request / validation error |
| `404` | Not found |
| `409` | Conflict (model already exists on POST) |
| `500` | Internal server error |

---

## Further Reading

- [Project Overview](project.md) — Goals, features, and supported providers
- [Developer Guide](developer.md) — Library architecture and API reference
- [Testing Guide](testing.md) — Mock provider and testing strategies
- [Development Process](development.md) — Build instructions and workflow
