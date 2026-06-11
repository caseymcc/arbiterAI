# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ArbiterAI is a C++17 library providing a unified, embeddable interface across multiple LLM
providers (OpenAI, Anthropic, DeepSeek, OpenRouter, llama.cpp local models, and a Mock provider for
testing). It also ships a standalone OpenAI-compatible HTTP server (`arbiterAI-server`) with model
lifecycle management, telemetry, and a live dashboard.

## Build, test, run — everything goes through Docker

All building, testing, and running happens **inside the Docker container** (`docker/Dockerfile`). The
host is not guaranteed to have the toolchain (CMake + vcpkg + llama.cpp) or dependencies. Dependencies
are managed by vcpkg (`vcpkg.json`).

```bash
./runDocker.sh                  # start/attach the container (bind-mounts repo at /app)
./runDocker.sh ./build.sh       # build (runs CMake automatically if cmake files changed)
./runDocker.sh ./build.sh --rebuild        # clean rebuild of the app
./runDocker.sh ./build.sh --rebuild-cmake  # nuke CMake dir + re-run CMake (only if cmake is broken)
./runDocker.sh --rebuild        # rebuild the Docker *image* (only when Dockerfile changes)
./runDocker.sh --stop           # stop and remove the container
```

Build output: `build/${OS}_${ARCH}_${BUILD_TYPE}`, default `build/linux_x64_debug/`.
Targets: `arbiterai` (library), `arbiterai_tests`, `arbiterAI-cli`, `arbiterAI-proxy`, `arbiterAI-server`.

### Tests (Google Test)

```bash
./runDocker.sh ./build/linux_x64_debug/arbiterai_tests
./runDocker.sh ./build/linux_x64_debug/arbiterai_tests --gtest_filter='ModelManager*'   # single suite/test
```

### Working rules

- Run binaries/commands through `./runDocker.sh ...`. Do **not** use host `python`/`pip`/`pytest` or host virtualenvs — the container is the environment.
- Do **not** launch `arbiterAI-server` yourself; ask the user to launch it so it doesn't occupy the agent terminal.
- Avoid `2>&1` redirection — the user needs to see live output.

## Configuration model

Model/provider configs are JSON, loaded by `ModelManager` (singleton) with schema validation
(`schemas/`). The default configs live in the **`arbiterAI_config` git submodule** (`arbiterAI_config/configs/defaults/{models,backends}/`).
`ArbiterAI::initialize()` takes a list of config directories. The server merges these with runtime-injected
configs (added/updated/removed via REST without restart) and can persist them via an override path.

## Architecture

Layered, strategy-pattern core (see `docs/developer.md` for the full API reference):

```
ArbiterAI (singleton factory + lifecycle)   ── src/arbiterAI/arbiterAI.{h,cpp}
  ├─ createChatClient() → ChatClient (stateful per-session: history, tools, cache, stats)
  ├─ owns ModelManager (singleton: config load, schema validation, model lookup, ConfigDownloader)
  └─ stateless convenience: completion(), streamingCompletion(), batchCompletion(), getEmbeddings()
        │ delegates to
   BaseProvider (abstract)  ── src/arbiterAI/providers/baseProvider.h
     OpenAI · Anthropic · DeepSeek · OpenRouter · Llama (local) · Mock
```

- **Providers** are instantiated by a `switch` in `arbiterAI.cpp` keyed on the provider string (`createProvider`-style factory). To add a provider: create `providers/<name>.{h,cpp}` subclassing `BaseProvider`, add it to that switch, add the source to `CMakeLists.txt`, and add a model config JSON.
- **Error handling is error-code based** (`ErrorCode` enum), not exceptions — follow this; avoid try/catch where an error code works.

### Local model subsystem (llama.cpp)

Distinct from the cloud providers, this is the heavier piece:

- **`ModelRuntime`** (`modelRuntime.{h,cpp}`) — multi-model loading into VRAM/RAM, swap queueing, LRU eviction, GGUF-aware load-failure classification (`LoadFailureReason`/`LoadErrorDetail`).
- **`InferenceScheduler`** (`inferenceScheduler.{h,cpp}`) — request pipeline with stages (Queued → Tokenizing → WaitingAccelerator → Inferring → Complete), and `TokenChannel` for streaming tokens from the accelerator thread to the HTTP thread.
- **`HardwareDetector`** — GPU/VRAM/RAM/CPU detection; **`ModelFitCalculator`** — whether a model fits available hardware.
- **`ModelDownloader`** / **`StorageManager`** — download GGUF files (libgit2 / HTTP), track storage, hot-ready/protected flags, cleanup.
- **`TelemetryCollector`** — inference stats and system snapshots, surfaced by the server.

### Server (`src/server/`)

Separate CMake target linking `arbiterai` + cpp-httplib (httplib is a server-only dependency, kept out of
the core library). `routes.cpp` defines the OpenAI-compatible endpoints (`/v1/chat/completions`,
`/v1/models`, `/v1/embeddings` with SSE streaming), model management, telemetry (`/api/stats`), and config
injection. `dashboard.h`/`dashboardConfig.h` are embedded HTML/JS for the `/dashboard` UI. The server takes a
single required config file: `arbiterAI-server -c <config.json>`. See `docs/server.md`.

### Testing without API keys

The **Mock provider** (`providers/mock.{h,cpp}`) returns deterministic responses driven by `<echo>...</echo>`
tags in messages — no network or keys. Use `"provider": "mock"` in a model config. See `docs/testing.md`.

## Code style (from `.roo/rules-code/` and `.github/instructions/`)

- Files: **camelCase** names, `.h`/`.cpp`/`.inl`. Header guards `_PROJECT_FILENAME_EXT_`, **no `#pragma once`**.
- Braces: open brace on a **new line** for namespaces/functions/control blocks; **same line** for struct/class definitions in headers.
- Naming: Types `PascalCase`; functions/methods `camelCase`; class members `m_camelCase`; locals/struct vars `camelCase`; macros `UPPER_CASE`.
- Spacing: no space around `=`, `::`, unary operators, or between a keyword/function name and `(`; spaces around comparison/logical operators; comma after, not before.
- Pointers/refs bind to the variable: `type *var`, `type &var`. Minimize `auto`. Minimize comments — none for obvious code.
- Includes: `""` for local files, `<>` for libraries. Namespaces: prefer explicit qualification over `using` directives; aliases allowed.

## Docs map

`docs/developer.md` (architecture + API), `docs/server.md` (server API), `docs/testing.md` (mock/echo),
`docs/project.md` (goals/providers), `docs/tasks/` (active task plans). The `docs/old/` and
`docs/development/tasks/completed/` dirs are historical.
