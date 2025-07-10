#!/bin/bash

IMAGE_NAME="hermesaxiom-dev"
CONTAINER_NAME="hermesaxiom-dev-container"

REBUILD=false
STOP=false
RESTART=false
COMMAND_ARGS=()
STOPPED=false

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --rebuild)
            REBUILD=true
            shift
            ;;
        --stop)
            STOP=true
            shift
            ;;
        --restart)
            RESTART=true
            shift
            ;;
        *)
            COMMAND_ARGS=("$@")
            break
            ;;
    esac
done

if [ "$STOP" = true ] || [ "$RESTART" = true ] || [ "$REBUILD" = true ]; then
    if [ "$(docker ps -a -q -f name=$CONTAINER_NAME)" ]; then
        echo "Stopping and removing container..."
        docker stop $CONTAINER_NAME
        docker rm $CONTAINER_NAME
        STOPPED=true
    fi
fi

if [ "$STOP" = true ]; then
    if [ "$STOPPED" = false ]; then
        echo "No running container to stop."
    else
        echo "Container stopped and removed."
    fi
    exit 0
fi

if [ ! "$(docker ps -q -f name=$CONTAINER_NAME)" ] || [ "$REBUILD" = true ]; then
    if [ ! "$(docker ps -aq -f status=exited -f name=$CONTAINER_NAME)" ] || [ "$REBUILD" = true ]; then
        # Check if image exists, build if not
        if [ -z "$(docker images -q $IMAGE_NAME)" ] || [ "$REBUILD" = true ]; then
            if [ "$REBUILD" = true ]; then
                echo "Rebuilding Docker image..."
            else
                echo "Docker image not found. Building..."
            fi
            docker build -t $IMAGE_NAME -f docker/Dockerfile .
        fi
        echo "Starting new container..."
        docker run -d -it --name $CONTAINER_NAME -v "$(pwd)":/app -v "$(pwd)/models":/models -v "$(readlink -f "$(pwd)/../vcpkg_cache")":/vcpkg_cache -e VCPKG_OVERLAY_TRIPLETS=/app/triplets $IMAGE_NAME
    else
        echo "Restarting existing container..."
        docker start $CONTAINER_NAME
    fi
fi

if [ ${#COMMAND_ARGS[@]} -gt 0 ]; then
    docker exec -i "$CONTAINER_NAME" /bin/bash -c "${COMMAND_ARGS[*]}"
else
    echo "Attaching to container..."
    docker exec -it "$CONTAINER_NAME" /bin/bash
fi