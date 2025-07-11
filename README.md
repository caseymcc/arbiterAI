# ArbiterAI

ArbiterAI is a modern, high-performance C++17 library designed to provide a unified, embeddable interface for interacting with various Large Language Model (LLM) providers.

## Building the Project

The project is built using Docker to ensure a consistent build environment.

1.  **Start the Docker container:**
    ```bash
    ./runDocker.sh
    ```
    This will start the container and attach a shell to it.

2.  **Build the application:**
    Inside the Docker container's shell, run the build script:
    ```bash
    ./build.sh
    ```
    To perform a clean rebuild, use:
    ```bash
    ./build.sh --rebuild
    ```

The application binaries will be located in the `build/` directory inside the container.

## Running Tests

Tests are run from within the Docker container.

1.  **Start the Docker container** (if not already running):
    ```bash
    ./runDocker.sh
    ```

2.  **Run the tests:**
    The tests are run as part of the build process. To run them explicitly, you can execute the test binary from the build directory:
    ```bash
    ./build/linux_x64_Debug/tests/arbiterAITests
    ```
    (The exact path may vary depending on the build configuration).