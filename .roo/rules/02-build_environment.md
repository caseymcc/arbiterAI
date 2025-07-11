### **Build Environment:**
All `ArbiterAI` build, execution, debugging, and testing occurs within a Docker container located at `docker/Dockerfile`.

  * ### Managing the Docker Container
    The runDocker.sh script, located in the project root, manages the Docker container lifecycle.
    * `./runDocker.sh [command]`: Starts (or creates and starts) the container. If already running, it attaches. If `[command]` are provided, they are executed within the running container via docker exec.
    * `./runDocker.sh --stop`: Stops and removes the container.
    * `./runDocker.sh --restart`: Restarts an existing container and attaches.
    * `./runDocker.sh --rebuild`: Rebuilds the Docker image and then starts/restarts the container. Only use --rebuild if Dockerfile or its dependencies change.
    Dockerfile or its dependencies change.
  * ### Building the Application
    Execute build.sh inside the running Docker container from the project root. Use `./runDocker.sh ./build.sh`.
    * `./build.sh`: Builds the application.
    * `./build.sh --rebuild`: Cleans and then builds the application.
    * `./build.sh --rebuild-cmake`: Deletes CMake build directory, re-runs CMake, and then builds. Only use if something is wrong with the cmake build, `build.sh` alone will run cmake if any of the cmake files change.
  * The application binaries is located at `build/${OS}_${ARCH}_${BUILD_TYPE}` within the container.
  * `build.sh` has to be ran using `runDocker.sh`