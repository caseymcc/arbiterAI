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

## Releases

### Version Management

The project version lives in `CMakeLists.txt` (`project(arbiterAI VERSION X.Y.Z)`). **Do not change the version manually in PRs** — it is managed exclusively by the release workflows.

The version is embedded into the compiled library at build time via a CMake-generated header. Applications can query it at runtime:

```cpp
auto ver = arbiterAI::getVersion();
// ver.major, ver.minor, ver.patch, ver.toString()
```

### Point Releases (Automatic)

Every PR merge into `main` or a `release/**` branch automatically:

1. Increments the patch version (`X.Y.Z` → `X.Y.Z+1`)
2. Commits the version bump with `[skip ci]`
3. Creates a `vX.Y.Z` tag
4. Publishes a GitHub Release with the PR title and link

### Major/Minor Releases (Manual)

Triggered from the GitHub Actions page → **Release Major/Minor** → **Run workflow**:

1. Select `major` or `minor` release type
2. Select the source branch (e.g., `main`)
3. The workflow creates a `release/X.Y` branch, sets the version to `X.Y.0`, builds the project, and publishes a GitHub Release with build artifacts

### Release Branch Convention

- `release/0.2` — minor release branch for 0.2.x
- `release/1.0` — major release branch for 1.0.x

PRs can be opened against release branches for hotfixes. Each merge triggers a point release on that branch.

### Tag Format

All tags follow the format `vX.Y.Z` (e.g., `v0.1.3`, `v0.2.0`, `v1.0.0`).

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
