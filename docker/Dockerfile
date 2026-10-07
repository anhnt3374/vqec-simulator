# syntax=docker/dockerfile:1
#
# One Dockerfile for every runtime class. Select it with --build-arg TARGET_RUNTIME=cpu|gpu
# (npu stages are added here when an NPU target is chosen).
#
# Stages:
#   dev      toolchain + every third-party dependency; used by scripts/dev.sh with the repo mounted
#   build    compiles and installs the project into /opt/vqec
#   runtime  slim image with the binary, WebUI and migrations (used by docker/compose.yaml)
#
# All dependency versions are pinned here and nowhere else.

ARG TARGET_RUNTIME=cpu
ARG UBUNTU_IMAGE=ubuntu:22.04
ARG CUDA_DEVEL_IMAGE=nvidia/cuda:12.9.1-cudnn-devel-ubuntu22.04
ARG CUDA_RUNTIME_IMAGE=nvidia/cuda:12.9.1-cudnn-runtime-ubuntu22.04

FROM ${UBUNTU_IMAGE} AS devbase-cpu
FROM ${CUDA_DEVEL_IMAGE} AS devbase-gpu
FROM ${UBUNTU_IMAGE} AS runbase-cpu
FROM ${CUDA_RUNTIME_IMAGE} AS runbase-gpu

# ---------------------------------------------------------------------------------------------
# dev
# ---------------------------------------------------------------------------------------------
FROM devbase-${TARGET_RUNTIME} AS dev-common
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ccache clang-format clang-tidy cmake gdb git ninja-build pkg-config \
        ca-certificates curl \
        libspdlog-dev nlohmann-json3-dev libyaml-cpp-dev libgtest-dev libgmock-dev \
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
        libopencv-dev \
        libsqlite3-dev libpq-dev libcurl4-openssl-dev \
        libjsoncpp-dev uuid-dev zlib1g-dev libssl-dev libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*

# Drogon (HTTP/WebSocket adapter). Not packaged for Ubuntu 22.04, so build the pinned tag.
ARG DROGON_VERSION=v1.9.13
RUN git clone --depth 1 --branch "${DROGON_VERSION}" --recurse-submodules --shallow-submodules \
        https://github.com/drogonframework/drogon.git /tmp/drogon \
    && cmake -S /tmp/drogon -B /tmp/drogon/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_EXAMPLES=OFF -DBUILD_CTL=OFF -DBUILD_ORM=OFF -DBUILD_TESTING=OFF \
        -DBUILD_YAML_CONFIG=OFF \
    && cmake --build /tmp/drogon/build \
    && cmake --install /tmp/drogon/build \
    && rm -rf /tmp/drogon

FROM dev-common AS dev-cpu
ARG ONNXRUNTIME_VERSION=1.30.0
RUN mkdir -p /opt/onnxruntime \
    && curl -fsSL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz" \
        | tar -xz -C /opt/onnxruntime --strip-components=1

FROM dev-common AS dev-gpu
ARG TENSORRT_VERSION=10.16.1.11-1+cuda12.9
ARG ONNXRUNTIME_VERSION=1.30.0
# Every TensorRT package is pinned explicitly: apt would otherwise pull newer CUDA-13 builds of
# the dependencies and fail to resolve.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libnvinfer10="${TENSORRT_VERSION}" \
        libnvinfer-dev="${TENSORRT_VERSION}" \
        libnvinfer-headers-dev="${TENSORRT_VERSION}" \
        libnvinfer-safe-headers-dev="${TENSORRT_VERSION}" \
        libnvinfer-plugin10="${TENSORRT_VERSION}" \
        libnvinfer-plugin-dev="${TENSORRT_VERSION}" \
        libnvinfer-headers-plugin-dev="${TENSORRT_VERSION}" \
        libnvonnxparsers10="${TENSORRT_VERSION}" \
        libnvonnxparsers-dev="${TENSORRT_VERSION}" \
    && rm -rf /var/lib/apt/lists/*
RUN mkdir -p /opt/onnxruntime \
    && curl -fsSL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-gpu_cuda12-${ONNXRUNTIME_VERSION}.tgz" \
        | tar -xz -C /opt/onnxruntime --strip-components=1

FROM dev-${TARGET_RUNTIME} AS dev
ENV CMAKE_PREFIX_PATH=/opt/onnxruntime
WORKDIR /workspace

# ---------------------------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------------------------
FROM dev AS build
ARG TARGET_RUNTIME
COPY . /workspace
RUN cmake --preset "${TARGET_RUNTIME}-release" -DCMAKE_CXX_COMPILER_LAUNCHER= -DVQEC_BUILD_TESTS=OFF \
    && cmake --build --preset "${TARGET_RUNTIME}-release" \
    && cmake --install "build/${TARGET_RUNTIME}-release" --prefix /opt/vqec

# ---------------------------------------------------------------------------------------------
# runtime
# ---------------------------------------------------------------------------------------------
FROM runbase-${TARGET_RUNTIME} AS runtime-common
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates libspdlog1 libyaml-cpp0.7 \
        libavcodec58 libavformat58 libavutil56 libswscale5 \
        libopencv-core4.5d libopencv-imgproc4.5d libopencv-imgcodecs4.5d \
        libsqlite3-0 libpq5 libcurl4 libjsoncpp25 libuuid1 zlib1g libssl3 libbrotli1 \
    && rm -rf /var/lib/apt/lists/*

FROM runtime-common AS runtime-cpu

FROM runtime-common AS runtime-gpu
ARG TENSORRT_VERSION=10.16.1.11-1+cuda12.9
RUN apt-get update && apt-get install -y --no-install-recommends \
        libnvinfer10="${TENSORRT_VERSION}" \
        libnvinfer-plugin10="${TENSORRT_VERSION}" \
        libnvonnxparsers10="${TENSORRT_VERSION}" \
    && rm -rf /var/lib/apt/lists/*

FROM runtime-${TARGET_RUNTIME} AS runtime
COPY --from=build /opt/onnxruntime/lib /opt/onnxruntime/lib
COPY --from=build /opt/vqec /opt/vqec
RUN useradd --system --no-create-home --uid 10001 vqec \
    && mkdir -p /var/lib/vqec /var/log/vqec /data \
    && chown vqec:vqec /var/lib/vqec /var/log/vqec /data
ENV LD_LIBRARY_PATH=/opt/onnxruntime/lib
USER vqec
EXPOSE 8080
ENTRYPOINT ["/opt/vqec/bin/vqec_simulator"]
CMD ["--config", "/etc/vqec/simulator.yaml"]

# CPU live-view worker: adapters use the existing ORT/FFmpeg/OpenCV runtime libraries.
FROM runtime AS live-ai-runtime
ENTRYPOINT ["/opt/vqec/bin/vqec_live_ai"]
CMD ["--config", "/etc/vqec/live_ai.yaml"]

# Independent WebUI API and frontend toolchain (Ubuntu 22.04, linux/amd64).
FROM ${UBUNTU_IMAGE} AS node-dev
ARG DEBIAN_FRONTEND=noninteractive
ARG NODE_VERSION=22.23.3
ARG NODE_SHA256=df450af89261115ef9f9e3830c3eeb2cc9213b63c720b1af623cb5dcbe2e02de
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl xz-utils \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz" \
        -o /tmp/node.tar.xz \
    && echo "${NODE_SHA256}  /tmp/node.tar.xz" | sha256sum --check \
    && tar -xJf /tmp/node.tar.xz -C /usr/local --strip-components=1 \
    && rm /tmp/node.tar.xz
ENV NODE_OPTIONS=--enable-source-maps
WORKDIR /workspace

FROM node-dev AS backend-build
WORKDIR /workspace/backend
COPY backend/package*.json ./
RUN npm ci
COPY backend/ ./
RUN npm run build

FROM node-dev AS backend-runtime
ENV NODE_ENV=production
WORKDIR /app
COPY backend/package*.json ./
RUN npm ci --omit=dev && useradd --system --uid 10002 backend
COPY --from=backend-build /workspace/backend/dist/src ./dist
USER backend
EXPOSE 3000
CMD ["node", "dist/main.js"]

# Local live-view development server; Vite proxies /api to NestJS.
FROM node-dev AS web-dev
WORKDIR /workspace/web
COPY web/package*.json ./
RUN npm ci
COPY web/ ./
EXPOSE 5173
CMD ["npm", "run", "dev", "--", "--host", "0.0.0.0"]
