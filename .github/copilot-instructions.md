# ArbiterAI – Copilot Instructions

A C++17 library providing a unified interface for multiple LLM providers.

## Key Documentation

- **[docs/project.md](../docs/project.md)** — Project goals, supported providers, core features, and third-party libraries
- **[docs/developer.md](../docs/developer.md)** — Architecture, full API reference (ArbiterAI, ChatClient, data structures), configuration, and provider system
- **[docs/testing.md](../docs/testing.md)** — Mock provider usage, echo tag syntax, and test examples
- **[docs/development.md](../docs/development.md)** — Build instructions, project structure, and task tracking

## Quick Context

- **Entry point:** `ArbiterAI` singleton (factory) → creates `ChatClient` instances (stateful, per-session)
- **Providers:** OpenAI, Anthropic, DeepSeek, OpenRouter, Llama.cpp (disabled), Mock (testing)
- **Build:** CMake + vcpkg, inside Docker (`./runDocker.sh` then `./build.sh`), output in `build/linux_x64_debug/`
- **Tests:** Google Test — `./build/linux_x64_debug/arbiterai_tests` (must be run inside Docker)
- **Language-specific formatting rules** are in `.github/instructions/`

## Important Rules

1. **All commands** must go through `./runDocker.sh ...`.
2. **All development** (building, testing, running) must be done inside the Docker container. The host environment is not guaranteed to have the correct tools or dependencies.
3. **Do not** use `python`, `pip`, `pytest` — the host may not have the correct Python version or dependencies.
4. **Do not** create or use a virtualenv on the host. The container is the virtualenv.
5. The project source is **bind-mounted** at `/app` inside the container. Edits to files on the host are immediately visible inside the container.
6. If you change the `Dockerfile`, run `./runDocker.sh --rebuild`.
7. Don't launch the server, ask the user to launch so that its not running in the agents terminal.

## Active Tasks

- **[docs/tasks/local_model_management.md](../docs/tasks/local_model_management.md)** — Plan for llama.cpp local model management (hardware detection, model swapping, telemetry, standalone server)
