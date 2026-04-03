# syntax=docker/dockerfile:1
ARG DOCKER_VERSION=1.2.0
FROM ubuntu:24.04

# Install basic build tools, Python 3, and GPU libraries.
# Vulkan headers + glslc are needed at build time for llama.cpp's Vulkan backend.
# At runtime, GPU inference requires a Vulkan ICD on the host; without one
# llama.cpp falls back to CPU-only.
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ninja-build \
    python3 \
    python3-pip \
    python3-dev \
    python3-venv \
    bison \
    libxmu-dev \
    libxi-dev \
    libgl-dev \
    libx11-dev \
    libxcursor-dev \
    libxrandr-dev \
    libxinerama-dev \
    libglu1-mesa-dev \
    vulkan-tools \
    libvulkan-dev \
    mesa-vulkan-drivers \
    glslc \
    glslang-tools \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://deb.nodesource.com/setup_18.x | bash - && \
    apt-get install -y nodejs && \
    rm -rf /var/lib/apt/lists/*

# Setup Python environment
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1

# Install vcpkg
WORKDIR /opt
RUN git clone https://github.com/Microsoft/vcpkg.git && \
    ./vcpkg/bootstrap-vcpkg.sh

# Add vcpkg to PATH
ENV PATH="/opt/vcpkg:${PATH}"

# Set working directory
WORKDIR /app

# Configure CMake to use vcpkg
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_DEFAULT_BINARY_CACHE=/vcpkg_cache

ENV CMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build command (to be run when container starts)
CMD ["tail", "-f", "/dev/null"]
