#!/bin/bash

CONTAINER_NAME="hermesaxiom_dev"
DEFAULT_CACHE_DIR="$HOME/.cache/vcpkg"
VCPKG_CACHE_DIR="${VCPKG_CACHE_DIR:-$DEFAULT_CACHE_DIR}"
HOST_API_PORT=8080  # Default port for the host API

# Function to show usage
usage() {
    echo "Usage: $0 [-r] [-s] [-a] [-p PORT]"
    echo "  -r: Rebuild Docker image"
    echo "  -s: Stop running container before starting"
    echo "  -v: Path to vcpkg installation on host system"
    exit 1
}

# Parse command line options
REBUILD=0
STOP=0

while getopts "rsv:ap:" opt; do
    case $opt in
        r) REBUILD=1 ;;
        s) STOP=1 ;;
        v) VCPKG_CACHE_DIR="$OPTARG" ;;
        ?) usage ;;
    esac
done

# Remove existing container if requested
if [ $STOP -eq 1 ]; then
    echo "Removing existing container..."
    docker rm -f $CONTAINER_NAME 2>/dev/null
fi

# Check if image exists or rebuild is requested
if [ $REBUILD -eq 1 ] || ! docker image inspect hermesaxiom >/dev/null 2>&1; then
    echo "Building Docker image..."
    docker build -t hermesaxiom -f docker/Dockerfile .
fi

# Set up network options for container
NETWORK_OPTS="--add-host=host.docker.internal:host-gateway"
NETWORK_OPTS="$NETWORK_OPTS -e REACT_APP_API_URL=http://host.docker.internal:$HOST_API_PORT"

# Start container
echo "Starting development container..."
docker run -it --rm \
    --name $CONTAINER_NAME \
    -v $(pwd):/app \
    -v "$VCPKG_CACHE_DIR":/vcpkg_cache \
    $NETWORK_OPTS \
    hermesaxiom
