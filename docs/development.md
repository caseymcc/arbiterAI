# Development Process

This document outlines the development workflow for ArbiterAI.

## Building

ArbiterAI uses CMake with vcpkg for dependency management. A Docker environment ensures consistent builds.

```bash
# Start the Docker container
./runDocker.sh

# Build inside the container
./build.sh

# Clean rebuild
./build.sh --rebuild
```

Build output is located in `build/linux_x64_debug/`.

## Running Tests

```bash
# From inside the Docker container
./build/linux_x64_debug/arbiterai_tests
```

## Task Tracking

Tasks are maintained as markdown files in the `docs/` directory structure.

### Directory Structure

- **`docs/development/tasks/completed/`** — Completed task files
- **`docs/tasks/`** — Active or planned tasks

### Task File Format

Task files use Markdown and include:

- **Title** — Clear description of the task
- **Description** — Detailed requirements and context
- **Acceptance Criteria** — What must be true for the task to be done
- **Progress** — Checklists or notes on progress

## Project Structure

```
arbiterAI/
├── CMakeLists.txt              # Build configuration
├── vcpkg.json                  # Dependency manifest
├── build.sh                    # Build script
├── runDocker.sh                # Docker environment launcher
├── src/arbiterAI/              # Library source code
│   ├── arbiterAI.h/cpp         # Main API and data structures
│   ├── chatClient.h/cpp        # Stateful chat client
│   ├── modelManager.h/cpp      # Model configuration management
│   ├── cacheManager.h/cpp      # Response caching
│   ├── costManager.h/cpp       # Spending limits and tracking
│   ├── modelDownloader.h/cpp   # Async model downloading
│   ├── fileVerifier.h/cpp      # SHA256 file verification
│   ├── configDownloader.h/cpp  # Remote config fetching
│   └── providers/              # LLM provider implementations
│       ├── baseProvider.h/cpp  # Abstract provider interface
│       ├── openai.h/cpp        # OpenAI provider
│       ├── anthropic.h/cpp     # Anthropic provider
│       ├── deepseek.h/cpp      # DeepSeek provider
│       ├── openrouter.h/cpp    # OpenRouter provider
│       ├── llama.h/cpp         # Llama.cpp local provider
│       └── mock.h/cpp          # Mock testing provider
├── tests/                      # Google Test test files
├── examples/                   # Example applications
│   ├── cli/                    # CLI chat client
│   ├── proxy/                  # HTTP proxy server
│   └── mock_example.cpp        # Mock provider demo
├── schemas/                    # JSON schemas
├── docs/                       # Documentation
├── cmake/                      # CMake modules and toolchains
├── docker/                     # Dockerfile
└── vcpkg/                      # Custom vcpkg ports
```
