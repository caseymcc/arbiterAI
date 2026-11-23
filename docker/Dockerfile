# syntax=docker/dockerfile:1
ARG DOCKER_VERSION=1.3.0
FROM ubuntu:24.04

# Install basic build tools, Python 3, and GPU libraries.
# Vulkan headers + glslc are needed at build time for llama.cpp's Vulkan backend.
# ROCm HIP SDK is needed at build time for llama.cpp's HIP/ROCm backend.
# At runtime, GPU inference requires a Vulkan ICD or ROCm driver on the host;
# without one llama.cpp falls back to CPU-only.
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
    libgl1-mesa-dev \
    libgl1-mesa-dri \
    libopengl-dev \
    libegl1 \
    libegl-mesa0 \
    vulkan-tools \
    libvulkan-dev \
    mesa-vulkan-drivers \
    spirv-headers \
    glslc \
    glslang-tools \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Install ROCm HIP SDK (build-time only — no kernel driver needed in container)
# Use hiplibsdk usecase to get HIP development libraries and CMake configs
# (the plain "hip" usecase only installs runtime, missing hip-lang-config.cmake)
RUN wget -q https://repo.radeon.com/amdgpu-install/7.2.1/ubuntu/noble/amdgpu-install_7.2.1.70201-1_all.deb && \
    apt-get update && \
    apt-get install -y ./amdgpu-install_7.2.1.70201-1_all.deb && \
    apt-get update && \
    amdgpu-install -y --usecase=hiplibsdk --no-dkms && \
    rm -f amdgpu-install_7.2.1.70201-1_all.deb && \
    rm -rf /var/lib/apt/lists/*

ENV ROCM_PATH=/opt/rocm
ENV HIP_PATH=/opt/rocm
ENV PATH="${ROCM_PATH}/bin:${PATH}"
ENV CMAKE_PREFIX_PATH="${ROCM_PATH}:${CMAKE_PREFIX_PATH}"

RUN curl -fsSL https://deb.nodesource.com/setup_18.x | bash - && \
    apt-get install -y nodejs && \
    rm -rf /var/lib/apt/lists/*

# Emscripten for the web dashboard (src/dashboard, ARBITERAI_WEB_DASHBOARD), the
# version gweni is tested against. contrib.glfw3 (gweni::app on the web) is
# fetched into the cache now so a build needs no network; the cache is owned by
# the build user because Emscripten builds its system libraries into it lazily.
# Playwright's Chromium headless shell gives a WebGL 2 browser for checking and
# capturing the dashboard.
ARG EMSDK_VERSION=6.0.13
ARG PLAYWRIGHT_VERSION=1.65.0
ENV EMSDK=/opt/emsdk
ENV EMSDK_NODE=${EMSDK}/node/current/bin/node
ENV PATH=${EMSDK}:${EMSDK}/upstream/emscripten:${PATH}
ENV NODE_PATH=/opt/playwright/node_modules
ENV PLAYWRIGHT_BROWSERS_PATH=/opt/playwright/browsers
RUN git clone --depth 1 --branch ${EMSDK_VERSION} https://github.com/emscripten-core/emsdk.git ${EMSDK} && \
    ${EMSDK}/emsdk install ${EMSDK_VERSION} && \
    ${EMSDK}/emsdk activate ${EMSDK_VERSION} && \
    ln -s $(dirname $(dirname $(ls ${EMSDK}/node/*/bin/node | head -1))) ${EMSDK}/node/current && \
    rm -rf ${EMSDK}/downloads ${EMSDK}/.git && \
    embuilder build contrib.glfw3 && \
    mkdir -p /opt/playwright && \
    PATH=${EMSDK}/node/current/bin:${PATH} npm install --prefix /opt/playwright --no-audit --no-fund playwright@${PLAYWRIGHT_VERSION} && \
    PATH=${EMSDK}/node/current/bin:${PATH} /opt/playwright/node_modules/.bin/playwright install chromium-headless-shell && \
    apt-get update && \
    PATH=${EMSDK}/node/current/bin:${PATH} /opt/playwright/node_modules/.bin/playwright install-deps chromium-headless-shell && \
    rm -rf /var/lib/apt/lists/* && \
    chown -R ubuntu:ubuntu ${EMSDK} /opt/playwright

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
