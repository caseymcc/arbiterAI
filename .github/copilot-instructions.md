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

## Active Tasks

- **[docs/tasks/local_model_management.md](../docs/tasks/local_model_management.md)** — Plan for llama.cpp local model management (hardware detection, model swapping, telemetry, standalone server)
