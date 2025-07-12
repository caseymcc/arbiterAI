# syntax=docker/dockerfile:1
ARG DOCKER_VERSION=1.0.0
FROM ubuntu:22.04

# Install basic build tools and Python 3
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
    && apt-get remove -y nodejs npm \
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
CMD ["bash"]
