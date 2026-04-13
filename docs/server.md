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
   - [Storage Management](#35-storage-management)
   - [Logs](#36-logs)
   - [Health & Version](#37-health--version)
   - [Dashboard](#38-dashboard)
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
- **Storage management** — Track downloaded model files, set hot ready / protected flags, configure automated cleanup, monitor disk usage and download progress with speed and ETA
- **Telemetry** — System snapshots, inference history, swap history, and hardware info
- **Live dashboard** — Browser-based UI at `/dashboard` with storage bar, download progress, and model management
- **CORS** — All responses include permissive CORS headers

---

## 2. Running the Server

```bash
# From inside the Docker container
./build/linux_x64_debug/arbiterAI-server [options]
```

### CLI Options

The server accepts only two command-line options:

| Option | Description |
|--------|-------------|
| `-c, --config <path>` | Path to server configuration JSON file (**required**) |
| `-h, --help` | Print usage |

### Configuration File

All server settings are defined in a JSON configuration file. See [`examples/server_config.json`](../examples/server_config.json) for a complete example.

```json
{
    "host": "0.0.0.0",
    "port": 8080,
    "model_config_paths": ["config"],
    "models_dir": "/models",
    "default_model": "",
    "default_variant": "",
    "override_path": "",
    "ram_budget_mb": 0,
    "max_concurrent_downloads": 2,
    "storage": {
        "limit": "0",
        "cleanup_enabled": true,
        "cleanup_max_age_days": 30,
        "cleanup_interval_hours": 24
    },
    "hardware": {
        "vram_overrides": {
            "0": 32000
        },
        "default_backend_priority": ["vulkan"]
    },
    "logging": {
        "level": "info",
        "directory": "",
        "rotate_hour": 0,
        "retain_days": 7
    }
}
```

#### Configuration Reference

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `host` | `string` | `"0.0.0.0"` | Bind address |
| `port` | `int` | `8080` | HTTP port |
| `model_config_paths` | `string[]` | `["config"]` | Model config directory paths |
| `models_dir` | `string` | `"/models"` | Directory for downloaded model files |
| `default_model` | `string` | `""` | Model to load on startup |
| `default_variant` | `string` | `""` | Default quantization variant (e.g., `Q4_K_M`) |
| `override_path` | `string` | `""` | Path to write runtime model config overrides |
| `ram_budget_mb` | `int` | `0` | Ready-model RAM budget in MB (`0` = auto 50%) |
| `max_concurrent_downloads` | `int` | `2` | Maximum simultaneous model downloads |

**`storage` object:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `limit` | `string` | `"0"` | Max storage for model files (e.g., `"50G"`, `"500M"`). `"0"` = all free disk. |
| `cleanup_enabled` | `bool` | `true` | Enable automated storage cleanup |
| `cleanup_max_age_days` | `int` | `30` | Days since last use before cleanup candidacy |
| `cleanup_interval_hours` | `int` | `24` | Hours between automated cleanup runs |

**`hardware` object:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `vram_overrides` | `object` | `{}` | GPU index → VRAM MB overrides (e.g., `{"0": 32000}`) |
| `default_backend_priority` | `string[]` | `[]` | Default GPU backend preference for models without their own `backend_priority` (e.g., `["vulkan"]`). Empty = all backends. |

**`logging` object:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `level` | `string` | `"info"` | Log level (`trace`, `debug`, `info`, `warn`, `error`) |
| `directory` | `string` | `""` | Directory for log files (empty = console only) |
| `rotate_hour` | `int` | `0` | Hour of day (0–23) to rotate log files |
| `retain_days` | `int` | `7` | Number of daily log files to keep |

### Examples

```bash
# Start with a config file
./arbiterAI-server --config /etc/arbiterai/server_config.json

# Short form
./arbiterAI-server -c server_config.json
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

**Response (400):** Model load failed. The response includes structured error details so callers can programmatically react to the failure.

```json
{
  "error": {
    "message": "Model architecture is not supported by this llama.cpp build",
    "type": "invalid_request_error",
    "code": "model_load_error",
    "param": "model",
    "details": {
      "model": "qwen3.5-27b",
      "variant": "Q4_K_M",
      "context_requested": 4096,
      "error_code": "model_load_error",
      "reason": "unsupported_arch",
      "recoverable": false,
      "action": "update_server",
      "suggestion": "Update the server to a newer version that supports this model architecture, or use a different model.",
      "llama_log": "llama_model_load: error loading model architecture: unknown model architecture: 'qwen35'\nllama_model_load_from_file_impl: failed to load model"
    }
  }
}
```

**`details.reason` values:**

| Reason | Description | Recoverable |
|--------|-------------|-------------|
| `file_not_found` | GGUF file does not exist at the expected path | Yes |
| `file_corrupt` | GGUF header invalid, bad magic, or file truncated | Yes |
| `insufficient_vram` | Not enough GPU memory to load model at requested context | Yes |
| `insufficient_ram` | Not enough system RAM | Yes |
| `context_too_large` | Requested context size exceeds model or hardware limits | Yes |
| `unsupported_arch` | Model architecture not supported by this llama.cpp build | No |
| `backend_error` | Generic llama.cpp internal error | No |
| `unknown` | Could not classify the failure | No |

**`details.action` values:**

| Action | Description |
|--------|-------------|
| `redownload` | Re-download the model file (file missing) |
| `delete_and_redownload` | Delete the corrupt file, then re-download |
| `reduce_context` | Retry with a smaller context size |
| `use_smaller_variant` | Try a smaller quantization variant |
| `update_server` | Update the server to a newer version |
| `check_logs` | Inspect the `llama_log` field or server logs for details |

**`details.recoverable`** is `true` when the caller can take an automated action (re-download, reduce context, switch variant) to resolve the failure. When `false`, human intervention or a server update is required.

**Response (507):** Insufficient storage — the model file won't fit within the configured storage limit. Includes `available_bytes`, `required_bytes`, and `storage_limit_bytes` for programmatic decision-making.

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

**Response (507):** Insufficient storage. Same format as the load endpoint.

#### `GET /api/models/:name/download`

Get download status for a model. Includes speed and ETA when download is active.

**Response:**

```json
{
  "model": "qwen2.5-7b-instruct",
  "state": "Downloading",
  "bytes_downloaded": 1250000000,
  "total_bytes": 4680000000,
  "percent_complete": 26.7,
  "speed_mbps": 85.3,
  "eta_seconds": 38
}
```

When not downloading:

```json
{
  "model": "qwen2.5-7b-instruct",
  "state": "Loaded"
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

### 3.5 Storage Management

Manage downloaded model files on disk — track usage, set protection flags, configure automated cleanup, and monitor active downloads.

#### Concepts

- **Hot Ready** — Per-variant flag. Keeps model weights in system RAM after VRAM eviction for fast reload. Hot ready variants are protected from deletion.
- **Protected** — Per-variant flag. Prevents deletion by both manual delete requests and automated cleanup. Must be cleared before the file can be removed.
- **Guarded** — A variant is "guarded" if either hot ready or protected is set.

#### `GET /api/storage`

Current storage overview.

**Response:**

```json
{
  "models_directory": "/models",
  "total_disk_bytes": 500107862016,
  "free_disk_bytes": 350000000000,
  "used_by_models_bytes": 12500000000,
  "storage_limit_bytes": 53687091200,
  "available_for_models_bytes": 41187091200,
  "model_count": 3,
  "cleanup_enabled": true
}
```

#### `GET /api/storage/models`

List all downloaded model files with usage statistics and flags.

**Query parameters:**

| Parameter | Default | Description |
|-----------|---------|-------------|
| `sort` | `last_used` | Sort by: `last_used`, `size`, `name`, `downloads` |

**Response:**

```json
{
  "models": [
    {
      "model": "qwen2.5-7b-instruct",
      "variant": "Q4_K_M",
      "filename": "Qwen2.5-7B-Instruct-Q4_K_M.gguf",
      "file_path": "/models/Qwen2.5-7B-Instruct-Q4_K_M.gguf",
      "file_size_bytes": 4680000000,
      "file_size_display": "4.4 GB",
      "downloaded_at": "2025-01-15T10:30:00Z",
      "last_used_at": "2025-01-20T14:22:00Z",
      "usage_count": 47,
      "hot_ready": true,
      "protected": false,
      "runtime_state": "Loaded"
    }
  ],
  "total_count": 1,
  "total_size_bytes": 4680000000
}
```

#### `GET /api/storage/models/:name`

Get storage stats for all variants of a model.

**Response:**

```json
{
  "model": "qwen2.5-7b-instruct",
  "variants": [
    {
      "variant": "Q4_K_M",
      "filename": "Qwen2.5-7B-Instruct-Q4_K_M.gguf",
      "file_size_bytes": 4680000000,
      "usage_count": 47,
      "hot_ready": true,
      "protected": false
    }
  ]
}
```

#### `GET /api/storage/models/:name/variants/:variant`

Get storage stats for a specific variant.

**Response (200):** Single variant object (same fields as above).

**Response (404):** Variant not found.

#### `PUT /api/storage/limit`

Set the storage limit.

**Request body:**

```json
{
  "limit_bytes": 53687091200
}
```

**Response (200):**

```json
{
  "storage_limit_bytes": 53687091200,
  "available_for_models_bytes": 41187091200
}
```

#### `DELETE /api/models/:name/files`

Delete downloaded files for a model. Specify `variant` query parameter to delete a single variant, or omit to delete all variants.

**Query parameters:**

| Parameter | Description |
|-----------|-------------|
| `variant` | Specific variant to delete. Omit to delete all. |

**Response (200):**

```json
{
  "status": "deleted",
  "model": "qwen2.5-7b-instruct",
  "freed_bytes": 4680000000
}
```

**Response (409):** Variant is guarded (hot ready or protected). Clear the flag first.

```json
{
  "error": {
    "message": "Cannot delete: variant is guarded (hot_ready or protected). Clear flags first.",
    "type": "invalid_request_error",
    "param": null,
    "code": null
  },
  "hot_ready": true,
  "protected": false
}
```

**Response (404):** Model or variant not found.

#### `POST /api/models/:name/variants/:variant/hot-ready`

Enable hot ready for a variant.

**Response (200):** `{"status": "hot_ready_set", "model": "...", "variant": "..."}`

**Response (404):** Variant not found.

#### `DELETE /api/models/:name/variants/:variant/hot-ready`

Disable hot ready for a variant.

**Response (200):** `{"status": "hot_ready_cleared", "model": "...", "variant": "..."}`

#### `POST /api/models/:name/variants/:variant/protected`

Enable protection for a variant.

**Response (200):** `{"status": "protected_set", "model": "...", "variant": "..."}`

**Response (404):** Variant not found.

#### `DELETE /api/models/:name/variants/:variant/protected`

Disable protection for a variant.

**Response (200):** `{"status": "protected_cleared", "model": "...", "variant": "..."}`

#### `GET /api/storage/cleanup/preview`

Preview what automated cleanup would delete without actually deleting anything.

**Response:**

```json
{
  "candidate_count": 2,
  "total_reclaimable_bytes": 12500000000,
  "candidates": [
    {
      "model": "old-model",
      "variant": "Q8_0",
      "filename": "old-model-q8.gguf",
      "file_size_bytes": 8100000000,
      "last_used_at": "2024-12-01T00:00:00Z",
      "usage_count": 3
    }
  ]
}
```

#### `POST /api/storage/cleanup/run`

Execute cleanup immediately. Deletes unguarded, unloaded variants that exceed the configured max age.

**Response:**

```json
{
  "freed_bytes": 8100000000,
  "deleted_count": 1
}
```

#### `GET /api/storage/cleanup/config`

Get the current cleanup policy.

**Response:**

```json
{
  "enabled": true,
  "max_age_hours": 720,
  "check_interval_hours": 24,
  "target_free_percent": 20.0,
  "respect_hot_ready": true,
  "respect_protected": true
}
```

#### `PUT /api/storage/cleanup/config`

Update the cleanup policy.

**Request body:** Same format as the GET response. All fields are optional — only provided fields are updated.

**Response (200):** Updated policy (same format as GET).

#### `GET /api/downloads`

List all active downloads with progress, speed, and ETA.

**Response:**

```json
{
  "downloads": [
    {
      "model": "qwen2.5-7b-instruct",
      "variant": "Q4_K_M",
      "state": "Downloading",
      "bytes_downloaded": 1250000000,
      "total_bytes": 4680000000,
      "percent_complete": 26.7,
      "speed_mbps": 85.3,
      "eta_seconds": 38
    }
  ]
}
```

---

### 3.6 Logs

#### `GET /api/logs`

Retrieve recent server log entries from the in-memory ring buffer. Useful for debugging model load failures, provider errors, and server behaviour without SSH access.

**Query parameters:**

| Parameter | Default | Description |
|-----------|---------|-------------|
| `count` | `200` | Number of log entries to return (max `1000`) |
| `level` | *(all)* | Filter by minimum level: `trace`, `debug`, `info`, `warning`, `error`, `critical` |

**Response:**

```json
{
  "logs": [
    {
      "timestamp": "2025-01-15T14:30:05.123Z",
      "epoch_ms": 1736952605123,
      "level": "info",
      "message": "Loading model qwen2.5-7b-instruct variant Q4_K_M context 4096"
    },
    {
      "timestamp": "2025-01-15T14:30:06.456Z",
      "epoch_ms": 1736952606456,
      "level": "error",
      "message": "Model load failed: insufficient VRAM"
    }
  ]
}
```

The ring buffer holds the most recent 1000 entries. Entries are returned in chronological order (oldest first). The dashboard polls this endpoint to display a live scrolling log panel.

---

### 3.7 Health & Version

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

### 3.8 Dashboard

#### `GET /dashboard`

Returns an HTML page with a live-updating dashboard showing:

- System info (GPU names, VRAM, RAM, CPU)
- Loaded models with state, variant, context size, GPU assignment
- Performance charts (tokens/sec, memory usage)
- Model management controls (load/unload/pin)
- **Downloaded models** — Storage bar (used/limit), table of all downloaded GGUF files with size, download date, last used, usage count, runtime state, and toggle buttons for hot ready / protected flags
- **Download progress** — Active downloads with progress bar, bytes transferred, speed (MB/s), and ETA
- **Row age coloring** — Fresh (green, <14 days), stale (yellow, 14–30 days), old (red, >30 days)
- Model deletion (guarded variants show disabled delete button with tooltip)
- **Server log panel** — Collapsible live-scrolling log viewer with level filtering (trace/debug/info/warning/error/critical) and auto-scroll toggle. Polls `/api/logs` every 2 seconds.

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
| `409` | Conflict (model already exists on POST, or variant is guarded on DELETE) |
| `500` | Internal server error |
| `507` | Insufficient storage (download or load rejected) |

---

## Further Reading

- [Project Overview](project.md) — Goals, features, and supported providers
- [Developer Guide](developer.md) — Library architecture and API reference
- [Testing Guide](testing.md) — Mock provider and testing strategies
- [Development Process](development.md) — Build instructions and workflow
