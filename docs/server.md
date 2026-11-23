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
   - [Health, Version & Updates](#37-health-version--updates)
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
- **Telemetry** — System snapshots, inference history, swap history, active scheduler jobs, and hardware info
- **Live dashboard** — A WebAssembly app at `/dashboard` (Overview, Storage and Config tabs, and the server log), embedded in the server binary
- **CORS** — All responses include permissive CORS headers

---

## 2. Running the Server

```bash
# From inside the Docker container
./build/linux_x64_debug/arbiterAI-server [options]
```

### CLI Options

| Option | Description |
|--------|-------------|
| `-c, --config <path>` | Path to server configuration JSON file (**required**, except with `--version` / `--help`) |
| `--self-test` | With `-c`: check that the server would start, then exit (see below) |
| `--version` | Print `arbiterAI-server <version>` and the llama.cpp build, exit `0` |
| `-h, --help` | Print usage |

#### Self-test

`arbiterAI-server --self-test -c <config>` runs the normal startup up to, but not including, `listen()`:
it parses the config file, loads the model configs (`ArbiterAI::initialize`) and detects hardware. It
exits `0` and prints `self-test passed: …` when a real start would get that far, or exits non-zero with
the reason on stderr: `1` for a missing config file, invalid JSON or no loadable model config; a value of
the wrong type (`"port": "abc"`) aborts with the JSON library's message, as a real start would. The
`startup_models` entries are parsed too, but not loaded.

It is safe to run next to a live server, in that server's working directory:

- **No network sync of `arbiterAI_config`.** A normal start clones or fetches the
  [`arbiterAI_config`](https://github.com/caseymcc/arbiterAI_config) repository into
  `./arbiterAI_config` in the working directory. The self-test turns that off
  (`ModelManager::setConfigSync(false)`): it reads the existing checkout as it is, and if there is none it
  skips the remote configs and relies on `model_config_paths` alone. It never clones, fetches or checks out.
- **Nothing else is written.** No log files (output goes to stderr only), no `models_dir`,
  `injected_config_dir` or log directory is created, and the storage manager is not initialized.
- **No port and no background work.** It loads no models, starts no scheduler or cleanup timer and binds no port.

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
    "startup_defaults": {
      "cpu": {"model": "", "variant": ""},
      "cuda": {"model": "", "variant": ""},
      "vulkan": {"model": "", "variant": ""}
    },
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
    },
    "updates": {
        "check": true,
        "interval_hours": 24,
        "drain_timeout_s": 60
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
| `startup_defaults` | `object` | `{}` | Per-accelerator startup defaults used on restart. Keys: `cpu`, `cuda`, `vulkan`, each with `model` and optional `variant`. If unset, the server falls back to `default_model` / `default_variant`. |
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

**`updates` object:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `check` | `bool` | `true` | Check GitHub Releases for a newer server release, at startup and then every `interval_hours` (see [`GET /api/update`](#get-apiupdate)) |
| `interval_hours` | `int` | `24` | Hours between checks (1–8760) |
| `allow_apply` | `bool` | `true` | Allow installing the latest release from the dashboard (see `can_apply` in [`GET /api/update`](#get-apiupdate)). Read from the file only: `PUT /api/server/config` refuses it |
| `auto_apply` | `bool` | `false` | Install a new release on its own, when every condition under [Auto-update](#auto-update) holds |
| `auto_apply_scope` | `string` | `"patch"` | Which releases auto-update installs: `"patch"` (same major.minor), `"minor"` (same major) or `"major"` (any) |
| `auto_apply_delay_hours` | `int` | `24` | Hours a release must have been published before auto-update installs it (0–8760) |
| `auto_apply_window` | `string` | `""` | `"HH:MM-HH:MM"` in local time (may wrap past midnight, e.g. `"23:00-05:00"`) when auto-update may install; empty: any time |
| `idle_minutes` | `int` | `10` | Minutes with no HTTP request, scheduler job or download before auto-update installs (0–1440) |
| `drain_timeout_s` | `int` | `60` | Seconds a graceful stop waits for in-flight work (see [Stopping the Server](#stopping-the-server)) |

`check`, `interval_hours`, `auto_apply`, `auto_apply_scope`, `auto_apply_delay_hours`,
`auto_apply_window` and `idle_minutes` can be changed without a restart through
`PUT /api/server/config`, e.g. `{"updates": {"check": false, "interval_hours": 6}}`: the change is
saved to the config file and applied at once (auto-update reads it at its next check, within
15 s). Only these keys are accepted (`drain_timeout_s` is read from the file at startup); a request that sets `allow_apply` is refused with 400; any other keys already in the file's
`updates` block are kept as they are. `GET /api/server/config` returns the current values as
`updates`. An invalid `updates` block in the file is ignored at startup with a warning: the defaults apply, except that `allow_apply` and `auto_apply` are off, so a typo in any key cannot turn installing back on. `apply_blocked_reason` then names the invalid key.

### Stopping the Server

On `SIGTERM` or `SIGINT` (`systemctl stop`, `docker stop`, Ctrl-C) the server drains before it exits:

1. New requests get `503` with `Retry-After` and the error code `server_shutting_down`.
2. In-flight requests (streamed responses until their last chunk is written), scheduler jobs and model downloads get up to `updates.drain_timeout_s` to finish.
3. The server stops listening, stops the inference scheduler, unloads every model, logs `shutdown complete` and exits `0`.

A second signal exits at once (`128+signal`). A signal that arrives before the server is listening (while it clones `arbiterAI_config`, loads startup models or runs `--self-test`) also exits at once, with `128+signal`: there is nothing to drain yet. If requests are still running 5 seconds after the drain timeout, the server logs that and exits `1` without unloading models; downloads still running then are abandoned.

### Examples

```bash
# Start with a config file
./arbiterAI-server --config /etc/arbiterai/server_config.json

# Short form
./arbiterAI-server -c server_config.json

# Check a config (or a new binary) without starting the server
./arbiterAI-server --self-test -c server_config.json

./arbiterAI-server --version
```

### Running as a service

The release tarball (`arbiterAI-server-vX.Y.Z-linux-x64.tar.gz`) ships two supported ways to run the
server, each of which restarts it when it crashes:

- **systemd**: `share/arbiterAI/systemd/arbiterai-server.service`, with the example config
  `share/arbiterAI/examples/server.systemd.json`.
- **Docker**: the image `ghcr.io/caseymcc/arbiterai-server:vX.Y.Z` / `:latest`, built from the tarball by
  [`docker/server.Dockerfile`](../docker/server.Dockerfile), run with
  [`docker/compose.yaml`](../docker/compose.yaml) (also in the tarball as `share/arbiterAI/docker/compose.yaml`)
  and the example config `share/arbiterAI/examples/server.docker.json`. The image's release sits
  in `/opt/arbiterAI-dist`; the server runs from the install volume `/opt/arbiterAI`, which the
  entrypoint (`share/arbiterAI/docker/entrypoint.sh`, under tini) brings up to the image with
  `arbiterAI-server --seed /opt/arbiterAI` before every start. `--seed <root>` first rolls back a
  version that crashed during its probation, then copies the image into an empty root, or swaps a
  newer image in (with `bin.prev` and a probation of its own) unless that version is in `failed`,
  and otherwise leaves the root alone. A newer image that cannot be copied in (a full volume, say)
  leaves the root's own version running (`Seed: kept vA, cannot install vB: …` on stderr). So an update from the dashboard survives a new container,
  `docker compose pull` of a newer image still updates, and an older image never downgrades.

The install steps are in the tarball's README ([`packaging/server/README.md`](../packaging/server/README.md)):
"Running under systemd" and "Running under Docker". Both declare their restart policy to the server
with `ARBITERAI_SUPERVISOR` (`systemd` / `docker`).

To build the tarball and the image from a checkout instead of a release:

```bash
./runDocker.sh ./build.sh release
./runDocker.sh ./scripts/packageServer.sh build/linux_x64_release "$(./scripts/version.sh get)" dist
docker build -f docker/server.Dockerfile --build-arg VERSION="$(./scripts/version.sh get)" \
    -t ghcr.io/caseymcc/arbiterai-server:latest dist
./scripts/checkServerImage.sh ghcr.io/caseymcc/arbiterai-server:latest "$(./scripts/version.sh get)"
```

`scripts/checkServerImage.sh` is the image check CI runs on every PR: `--version`, `--self-test` on a
read-only `/config`, a start followed by a clean `docker stop`, a crash that the restart policy
recovers, and an install volume that is seeded on the first start (with tini as PID 1 and apply
allowed under `supervisor: docker`) and kept by the next container.

#### Checking a service install by hand

These are the checks for a new install (or a change to the unit, the image or the drain). They need a
host with systemd or Docker.

**systemd** (clean Ubuntu 24.04 host, following the README's "Running under systemd" steps):

1. The server starts:
   ```bash
   systemctl status arbiterai-server          # active (running)
   curl -s localhost:8080/health              # {"status":"ok",...}
   ```
2. A stop drains and exits cleanly, well within `TimeoutStopSec=120`:
   ```bash
   time systemctl stop arbiterai-server
   journalctl -u arbiterai-server -n 20 | grep "shutdown complete"
   systemctl show arbiterai-server -p ExecMainStatus -p Result   # ExecMainStatus=0, Result=success
   ```
3. A crash is restarted after `RestartSec=5`:
   ```bash
   systemctl start arbiterai-server
   OLD=$(systemctl show -p MainPID --value arbiterai-server)
   kill -SEGV "$OLD"
   sleep 8
   journalctl -u arbiterai-server -n 200 | grep "FATAL SIGNAL"
   systemctl show arbiterai-server -p MainPID -p NRestarts       # a new MainPID, NRestarts=1
   curl -s localhost:8080/health
   ```

**Docker Compose** (following the README's "Running under Docker" steps):

1. `docker compose up -d`, then `curl -s localhost:8080/health` answers.
2. A stop is clean, not Docker's SIGKILL after 10 s:
   ```bash
   time docker compose stop                    # a second or two, not 10 s
   docker inspect -f '{{.State.ExitCode}}' "$(docker compose ps -aq arbiterai-server)"   # 0
   docker compose logs arbiterai-server | grep "shutdown complete"
   ```
3. A crash of the server restarts the container. Signal the server itself: `docker kill -s SEGV`
   signals PID 1, which is `tini`; tini does not forward synchronous signals such as SIGSEGV, and as
   PID 1 it ignores them, so nothing happens.
   ```bash
   docker compose start
   docker compose exec arbiterai-server sh -c \
       'for p in /proc/[0-9]*; do [ "$(cat $p/comm)" = arbiterAI-serve ] && kill -SEGV ${p#/proc/}; done; true'
   sleep 5
   docker inspect -f '{{.RestartCount}}' "$(docker compose ps -q arbiterai-server)"   # 1
   docker compose logs arbiterai-server | grep "FATAL SIGNAL"
   curl -s localhost:8080/health
   ```
4. A GPU is detected, on an NVIDIA or an AMD/Intel host: uncomment the matching GPU block in
   `compose.yaml`, `docker compose up -d` again, then
   ```bash
   curl -s localhost:8080/api/hardware        # "gpus" lists the card, with its VRAM
   ```

#### Checking self-update by hand

These need two published releases: **A**, one with self-update, and a newer **B** (say `0.1.20`
and `0.1.21`). Replace `A` and `B` in the commands. `POST /api/update/check` makes the server see B
at once instead of at its next daily check. Each check below says what to look for.

**systemd** (A installed from its tarball as in the README's "Running under systemd"):

1. The Update button installs B in place, with no restart that systemd sees:
   ```bash
   curl -s -X POST localhost:8080/api/update/check | jq '{latest, can_apply, supervisor}'   # "B", true, "systemd"
   PID=$(systemctl show -p MainPID --value arbiterai-server)
   # dashboard: Update to vB, then OK in the dialog (or: curl -X POST localhost:8080/api/update/apply -d '{"version":"B"}')
   until [ "$(curl -s localhost:8080/api/version | jq -r .version)" = B ]; do sleep 1; done
   systemctl show -p MainPID -p NRestarts arbiterai-server   # MainPID=$PID, NRestarts=0
   curl -s localhost:8080/api/update | jq '{current, prev_version, rollback_available}'      # "B", "A", true
   journalctl -u arbiterai-server -n 300 | grep -E "Update:|Restarting as"
   ```
   The dashboard page reloads by itself and shows "Updated from vA" under the header.
2. A crash during probation, the first 60 s after the restart, is rolled back. Do step 3 first:
   apply only installs a version newer than the running one, so B can be installed again only once
   A runs (the button still offers B then, though auto-update skips it).
   ```bash
   curl -s -X POST localhost:8080/api/update/apply -d '{"version":"B"}'
   until [ "$(curl -s localhost:8080/api/version | jq -r .version)" = B ]; do sleep 1; done
   kill -SEGV "$(systemctl show -p MainPID --value arbiterai-server)"     # within 60 s
   sleep 10
   curl -s localhost:8080/api/version | jq -r .version                      # A
   curl -s localhost:8080/api/update | jq '{update_error, auto}'           # "vB failed to start, rolled back"
   ls /opt/arbiterAI                                                        # no bin.prev, no update-state.json
   ```
3. Roll back by hand. After a good update has run for a minute, "Roll back to vA" under the header,
   or the command below. The PID stays the same, as in step 1.
   ```bash
   curl -s -X POST localhost:8080/api/update/rollback
   ```
4. A clean restart during probation keeps B: `systemctl restart arbiterai-server` within 60 s of an
   update, and `/api/version` still says B afterwards.

**Docker Compose** (as in the README's "Running under Docker", with
`image: ghcr.io/caseymcc/arbiterai-server:vA` in `compose.yaml`):

1. The first start seeds the install volume, and the Update button installs B there:
   ```bash
   docker compose up -d
   docker compose logs arbiterai-server | grep "Seed: installed vA"
   curl -s -X POST localhost:8080/api/update/check | jq '{latest, can_apply, supervisor}'   # "B", true, "docker"
   # dashboard: Update to vB, OK
   until [ "$(curl -s localhost:8080/api/version | jq -r .version)" = B ]; do sleep 1; done
   docker inspect -f '{{.RestartCount}}' "$(docker compose ps -q arbiterai-server)"            # 0
   docker compose exec arbiterai-server cat /proc/1/comm                                      # tini
   ```
2. The volume survives a new container: `docker compose down && docker compose up -d`, then
   `/api/version` says B and the logs say `Seed: kept vB (the image has vA)`.
3. A crash during probation is rolled back on the restart Docker does (install B again first, as for
   systemd, then within 60 s):
   ```bash
   docker compose exec arbiterai-server sh -c \
       'for p in /proc/[0-9]*; do [ "$(cat $p/comm)" = arbiterAI-serve ] && kill -SEGV ${p#/proc/}; done; true'
   sleep 10
   curl -s localhost:8080/api/version | jq -r .version                      # A
   curl -s localhost:8080/api/update | jq .update_error                     # "vB failed to start, rolled back"
   ```
4. `docker compose pull` of a newer image updates the volume. Start from an empty volume on A
   (`docker compose down`, `docker volume rm <project>_install`, `docker compose up -d`), then
   change the image tag to `vB` (or `latest`):
   ```bash
   docker compose pull && docker compose up -d
   docker compose logs arbiterai-server | grep "Seed: updated vA to vB"
   curl -s localhost:8080/api/version | jq -r .version                      # B
   ```
   Going back to `vA` in `compose.yaml` leaves the volume on B (`Seed: kept vB`). Deleting the
   volume resets it.

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
    "job_id": 17,
    "cancelled": false,
    "tokens_per_second": 45.2,
    "prompt_tokens": 120,
    "completion_tokens": 80,
    "latency_ms": 150.0,
    "total_time_ms": 1800.0
  }
]
```

#### `GET /api/scheduler/jobs`

Active inference scheduler jobs (local models only). Jobs flow through the
pipeline stages `queued` → `tokenizing` → `waiting` → `inferring`; completed
and cancelled jobs are not listed. Returns `[]` when the scheduler is not
running.

**Response:**

```json
[
  {
    "id": 17,
    "model": "my-local-model",
    "stage": "inferring",
    "streaming": true,
    "prompt_tokens": 120,
    "completion_tokens": 34,
    "queue_position": 0,
    "elapsed_ms": 2150.0
  }
]
```

| Field | Description |
|-------|-------------|
| `id` | Scheduler job ID (matches `job_id` in inference history) |
| `stage` | `queued`, `tokenizing`, `waiting`, or `inferring` |
| `streaming` | Whether the request is a streaming completion |
| `prompt_tokens` | Prompt token count (available once tokenized) |
| `completion_tokens` | Tokens generated so far (streaming jobs only) |
| `queue_position` | Position in the accelerator queue (`0` = running) |
| `elapsed_ms` | Time since the job was submitted |

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

### 3.7 Health, Version & Updates

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

#### `GET /api/update`

The result of the last check for a newer `arbiterAI-server` release. The server checks
GitHub Releases of the repository compiled into it (CMake `ARBITERAI_UPDATE_REPO`, default
`caseymcc/arbiterAI`; never a runtime setting) on its own thread: at startup and then every
`updates.interval_hours` while `updates.check` is on. Requests never wait on a check, and a
check sends the last `ETag` as `If-None-Match`, so an unchanged list (304) does not count
against GitHub's unauthenticated limit of 60 requests an hour.

The latest release is the highest version among releases that are neither drafts nor
prereleases and are tagged exactly `vX.Y.Z`; other tags are ignored. A failed check (offline,
rate limit, an HTTP error, a body that is not a release list) logs one warning, sets
`last_error` and keeps the previous result.

**Response:**

```json
{
  "current": "0.1.15",
  "latest": "0.1.16",
  "update_available": true,
  "release_url": "https://github.com/caseymcc/arbiterAI/releases/tag/v0.1.16",
  "release_notes": "## What's changed\n...",
  "published_at": "2026-11-20T12:00:00Z",
  "checked_at": "2026-11-23T08:00:00Z",
  "last_error": null,
  "can_apply": false,
  "apply_blocked_reason": "the working directory (/opt/arbiterAI/bin) is inside /opt/arbiterAI/bin, which an update replaces: move it out of the install",
  "supervisor": "systemd",
  "state": "idle",
  "progress": null,
  "prev_version": "0.1.15",
  "update_error": null,
  "rollback_available": true,
  "rollback_version": "0.1.15",
  "drain_timeout_s": 60,
  "started_at": "2026-11-23T09:14:02.117Z",
  "auto": {
    "enabled": true,
    "status": "waiting for the window 02:00-04:00 (local time)",
    "next_attempt": "2026-11-24T02:00:00Z"
  }
}
```

| Field | Description |
|-------|-------------|
| `current` | The running server's version |
| `latest` | The latest release's version, `null` before the first successful check or when there is none |
| `update_available` | `latest` is newer than `current` |
| `release_url`, `release_notes`, `published_at` | The latest release's page, notes (Markdown) and publish time; `null` with `latest` |
| `checked_at` | When a check last got an answer (UTC), `null` before that |
| `last_error` | Why the last check failed, `null` when it succeeded |
| `can_apply` | The server can install the latest release itself. Only when every one of these holds: it runs from a release tarball's install (`share/arbiterAI/install.json` next to `bin/`), `updates.allow_apply` is on, the install root is writable, in a container the install root is a mount point (a volume), and neither the working directory nor a path the config names (`--config`, `model_config_paths`, `models_dir`, `override_path`, `injected_config_dir`, `logging.directory`) is inside `<root>/bin` or `<root>/share`, which an update replaces |
| `apply_blocked_reason` | The first condition of `can_apply` that fails, naming the path or config key; `null` when `can_apply` is true |
| `supervisor` | What restarts the server after a crash (and so rolls back a bad update): `"systemd"` when `ARBITERAI_SUPERVISOR=systemd` and systemd set `INVOCATION_ID`, `"docker"` when `ARBITERAI_SUPERVISOR=docker` and `/.dockerenv` or `/run/.containerenv` exists, otherwise `null`. The variable alone, copied into a shell, does not count |
| `state` | What apply or rollback is doing: `idle`, then `checking_space`, `downloading`, `verifying`, `unpacking`, `self_testing`, `installing` and `restarting` for an apply (an auto-update waits in `waiting_idle` between `self_testing` and `installing`), or `rolling_back` and `restarting` for a rollback |
| `progress` | 0–1 over the whole apply while one runs, `null` otherwise |
| `prev_version` | The version this one was updated from: during probation from `update-state.json`, after it from `update-history.json`; `null` when the running version was not installed by an update |
| `update_error` | Why the last update attempt failed (or that a new version failed to start and was rolled back), from `update-history.json`; `null` after a successful update. Separate from `last_error`, which is about the release check |
| `rollback_available` | `<root>/bin.prev` exists and nothing is running: [`POST /api/update/rollback`](#post-apiupdaterollback) can go back to it |
| `rollback_version` | The version in `bin.prev`, `null` when there is none |
| `drain_timeout_s` | `updates.drain_timeout_s`: how long an apply or rollback waits for running requests before it restarts |
| `started_at` | When this process started (UTC, to the millisecond). The restart keeps the PID, so this is what changes when the new process answers |
| `auto.enabled` | `updates.auto_apply` is on and nothing turns it off (`allow_apply`, a supervisor) |
| `auto.status` | What auto-update is doing or waiting for, e.g. `"off"`, `"off: no supervisor declared (see README)"`, `"up to date"`, `"waiting: v0.1.16 installs 24 h after it was published"`, `"waiting for the server to be idle (1 HTTP request(s) running)"`, `"skipping v0.1.16: it failed here before …"`, `"installing v0.1.16"` |
| `auto.next_attempt` | When the condition it waits for next holds (UTC), when that is known; `null` otherwise |

#### `POST /api/update/check`

Checks now, on the request's thread, whatever `updates.check` says, and returns the same
object as `GET /api/update`. It gives up after 15 seconds when GitHub does not answer.

#### `POST /api/update/apply`

Installs the latest release in place: `{"version": "0.1.16"}` (a leading `v` is accepted). Only
the current `latest` of `GET /api/update` is accepted, and only when it is newer than the running
version; there is no way to install another version. Answers `202` with the `GET /api/update`
object once the install has started on its own thread, `409` while an apply or rollback runs
(`code: update_running`), for any other version (`not_latest`) or a release without the server
tarball (`no_asset`), `403` while `can_apply` is false (`apply_blocked`, with the reason) and
`400` for a body without `version`.

The steps, each shown in `state` and `progress`:

1. at least 3× the tarball's size must be free in the install root;
2. the tarball is downloaded to `<root>/.update/`;
3. its size and SHA-256 must match the release asset's `size` and `digest`;
4. it is unpacked with `tar`, and its `share/arbiterAI/install.json` must name the version;
5. `<staged>/bin/arbiterAI-server --self-test -c <config>` runs in the server's working
   directory and must exit 0 within 2 minutes (a binary that does not load, or a version that
   rejects the current config, stops here).

A failure in any step leaves the install as it was, removes `.update/` and sets
`update_error`. A failure of the release itself (digest, archive, self-test) also adds the
version to the `failed` list in `update-history.json`, which auto-update skips; a failed
download, a full disk (also while unpacking: the archive unpacks to about 3.2x its size) or a
`tar` that cannot be started does not.

On success the server writes `<root>/update-state.json` (`from`, `to`, the loaded models with
their variant, context, runtime options, devices and pin state, `attempt: 0`), moves `bin/` and
`share/` to `bin.prev/` and `share.prev/`, moves the new ones in, drains like a SIGTERM stop (up
to `updates.drain_timeout_s`), unloads the models and `execv`s `<root>/bin/arbiterAI-server` with
the same arguments and working directory. The process ID does not change, so systemd and
Docker do not see a restart. The new version reloads the models that were loaded and pins the
ones that were pinned.

The new version is in probation until it has served for 60 seconds or is stopped cleanly. If
it is started again before that (it crashed), it moves `bin.prev`/`share.prev` back, records
"vX failed to start, rolled back" and the version in `failed`, and `execv`s the old version.
That needs something to start it again after a crash: systemd or Docker (see `supervisor`).

#### Auto-update

With `updates.auto_apply` on, the server runs the same install as
[`POST /api/update/apply`](#post-apiupdateapply) by itself. It checks every 15 seconds, and
installs only when all of these hold:

- `updates.allow_apply` is on, and a supervisor is declared (`supervisor` is not `null`):
  without one a crash after the update would leave the server down;
- `latest` is newer than the running version and within `auto_apply_scope`;
- `latest` is not in the `failed` list of `update-history.json` (a release that failed to
  install or start, or that was rolled back by hand): it is skipped until a newer one
  appears, though the dashboard still offers it;
- `can_apply` is true and no apply or rollback is running;
- `latest` was published at least `auto_apply_delay_hours` ago;
- the local time is inside `auto_apply_window`, when one is set;
- the server has been idle for `idle_minutes`: every 15-second check in that time found no
  HTTP request open (cloud-model requests bypass the scheduler, so the drain controller's
  request count is what sees them), no scheduler job and no download. Idle is sampled, so a
  request shorter than 15 s can fall between two checks. Work that comes in while the release
  downloads and self-tests is not cut off: after staging, the install waits (`state`
  `waiting_idle`) until nothing is open, checking every second, before it swaps and drains. Still
  busy after 10 minutes, it removes the staged files and tries again an hour later.

`auto.status` says which of these it is waiting for. After an attempt that fails without
marking the release failed (a download error, a full disk), it waits an hour before the next.

#### `POST /api/update/rollback`

Goes back to the version in `bin.prev` (`rollback_version`), through the same drain and
restart. The version rolled back from is added to `failed`, so auto-update does not install it
again. If the swap back fails, the install stays as it is, `update_error` says why ("rolling
back to vX failed: ...") and nothing is added to `failed`. Answers `202`, `409` while something runs (`update_running`) or when there is no
`bin.prev` (`no_previous`), `403` while `can_apply` is false.

---

### 3.8 Dashboard

The dashboard is one WebAssembly app, written in C++ on [gweni](https://github.com/caseymcc/gweni)
(WebGL 2) and embedded in the server binary. It reads the same `/api/*` endpoints documented above;
it has no API of its own.

#### Routes

| Route | Serves |
|---|---|
| `GET /dashboard` | the app's page, on the **Overview** tab |
| `GET /dashboard/storage` | the same page, on the **Storage** tab |
| `GET /dashboard/config` | the same page, on the **Config** tab |
| `GET /dashboard/logs` | the same page, on the Overview with the **server log** open |
| `GET /dashboard/app.js`, `app.wasm`, `app.data` | the app (Emscripten's loader, the module, and the skin's fonts and images) |

Switching tabs updates the URL (history `pushState`), so reloads, bookmarks and back/forward
keep the tab. Every file is sent with its MIME type (`application/wasm` for the module), gzipped
when the request's `Accept-Encoding` allows gzip (raw otherwise), with an `ETag` (a SHA-256 of
its bytes made at build time), `Cache-Control: no-cache` and `Vary: Accept-Encoding`; a matching
`If-None-Match` gets `304 Not Modified`, so a reload transfers nothing until the server is
rebuilt.

#### What it shows

The app polls the server every 2 seconds. While the server restarts the header shows
**Disconnected** with the reason and the page keeps working; failed actions show a toast, and
questions use gweni dialogs (no browser `alert`/`confirm`/`prompt`).

When [`GET /api/update`](#get-apiupdate) reports a newer release, the header shows
`v0.1.15 → v0.1.16 available` next to the version on every tab; a click opens the release notes
(`release_url`) in a new tab. The app asks for it on its first poll and every 30 seconds after;
with no update (or a server without the check) the header shows the plain version.

Installing it (see [`POST /api/update/apply`](#post-apiupdateapply)):

- **Update to vX** next to the badge, while `can_apply` is true. It opens a dialog first that
  lists the loaded models (loaded again after the restart), the active jobs (drained for up to
  `drain_timeout_s`, then cut off) and whether a crash would be rolled back (`supervisor`).
- While `can_apply` is false, a line under the header gives `apply_blocked_reason` and the steps to
  update by hand instead of the button.
- While the install runs, the header shows its stage and progress (`state`, `progress`), and
  `/api/update` is asked for on every poll. Once the server restarts, the page reloads by
  itself: when `/api/version` reports another version, or when the restarted server answers
  `state: idle` (a rollback to the same version), because the new server serves its own
  dashboard.
- After an update, the line under the header says "Updated from vX" (`prev_version`) with a
  **Roll back to vX** button while `rollback_available`, or gives `update_error` after a failure.
- The Config tab's **Updates** card shows "Restart on crash: systemd / docker / none (no
  supervisor declared, see README)", the auto-update status (`auto.status`, `auto.next_attempt`),
  whether `allow_apply` is on (set in the file only), and the auto-update policy
  (`auto_apply`, `auto_apply_scope`, `auto_apply_delay_hours`, `auto_apply_window`,
  `idle_minutes`), saved with `PUT /api/server/config`.

- **Overview** — performance (prompt/generation tokens/s, active requests, total inferences,
  model swaps), system memory and CPU, a tokens/s chart (prompt on the left axis, generation on
  the right, the last two minutes, 0 while idle), the GPUs (memory bar, unified-memory figures,
  the Vulkan heap breakdown as a tooltip, CUDA utilisation, the models on each GPU, VRAM override
  set/clear), the loaded models (load/unload/pin/unpin, the per-device allocation as a tooltip),
  active requests with the downloads in progress, and the last ten inferences and swaps.
- **Storage** — the storage bar (used against the limit, or the free space), auto-cleanup and the
  cleanup preview, active downloads (progress, size, speed, ETA), and the model files with size,
  download and last-use dates, uses and state, coloured by age (green up to 14 days, amber up to
  30, red older or never used), Hot Ready / Protected toggles, and a delete that is disabled for
  a guarded variant and asks for confirmation. It polls only while it shows.
- **Config** — the startup models (model and variant, context size with the fit on the chosen
  devices, compute devices, API format, KV cache types, flash attention, no host-visible VRAM),
  **Save & Load** (saves `/api/server/config`, then loads the model; the outcome is shown on the
  entry) and **Remove**, and the VRAM overrides. It loads when first opened and is not polled, so
  edits are not overwritten.
- **Server log** — a panel under every tab: the last 200 entries of `/api/logs`, a level filter,
  **Live** (off freezes the view so lines can be selected while the server keeps logging), and
  **Copy selected** to the clipboard.

The page needs WebGL 2. Being drawn on a canvas, it has no browser find (Ctrl-F), no screen-reader
access and no free text selection (log lines are selected and copied in the log panel); text is
greyscale-antialiased.

#### Size

| File | Raw | gzip |
|---|---|---|
| `app.wasm` | 2.8 MB | 1.1 MB |
| `app.data` | 1.3 MB | 0.7 MB |
| `app.js` | 170 KB | 43 KB |
| page | 1.3 KB | 0.7 KB |

About 1.9 MB over the wire on the first load, then a `304` per file. The raw and gzipped copies
add about 6 MB to `arbiterAI-server`.

#### Building

The app lives in `src/dashboard/`, a CMake project of its own with its own vcpkg manifest (gweni
comes from the overlay port in `src/dashboard/ports/gweni`, pinned to a gweni commit, until gweni
publishes its vcpkg registry). The main build runs it twice (`cmake/webDashboard.cmake`):

- for `wasm32-emscripten`, always Release, giving the assets, which
  `cmake/embedDashboardAssets.cmake` gzips, hashes and embeds (`.incbin`) in `arbiterAI-server`;
- for `x64-linux`, giving `build/<tree>/dashboard/native/dashboard_tests`: the same panels rendered
  headless (surfaceless EGL on Mesa llvmpipe, no GPU, no server) from the JSON fixtures in
  `src/dashboard/tests/fixtures/`. `--shots <dir>` writes a PNG per tab. CI runs it after the
  build and keeps the PNGs as the `dashboard-shots` artifact.

`ARBITERAI_WEB_DASHBOARD` turns this on; it defaults to ON when `EMSDK` names an emsdk, as the
Docker image's does (emsdk 6.0.13). A build without emsdk (`-DARBITERAI_WEB_DASHBOARD=OFF`, or no
`EMSDK`) still builds the server, which then answers `/dashboard` with a short "Dashboard not
built" page; `-DARBITERAI_WEB_DASHBOARD=ON` without emsdk is a configure error.

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
