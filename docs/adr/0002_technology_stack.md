# ADR 0002: Technology stack

Status: accepted (2026-10-02). Versions are pinned in `docker/Dockerfile` and `docker/compose.yaml`.

## Decision

| Concern | Choice | Why |
| --- | --- | --- |
| Language | C++17, GCC 11 | Default toolchain of Ubuntu 22.04; vendor NPU SDKs commonly lag behind C++20. Moving to C++20 needs a new ADR. |
| Build | CMake ≥ 3.22 + Ninja + presets, ccache | Ubuntu 22.04 packages; presets give reproducible commands for humans and agents. |
| Environment | Docker, Ubuntu 22.04 base; `nvidia/cuda:12.9.1-cudnn-*-ubuntu22.04` for GPU | One Dockerfile, stages per runtime class. |
| Video I/O | FFmpeg (libav* 4.4 from apt) | RTSP and MP4 in/out with one library; hardware decode available later. |
| RTSP serving / browser live view | MediaMTX sidecar | Simulator pushes RTSP; browsers get WebRTC without code in the simulator. |
| Image ops / overlay | OpenCV 4.5 (adapters only) | Drawing and resize; never in domain/application. |
| Inference CPU | ONNX Runtime 1.30 | Broad operator coverage, same ONNX artifact as TensorRT input. |
| Inference GPU | TensorRT 10.16 (CUDA 12.9) | Best NVIDIA performance; engines built from ONNX and cached. |
| Inference NPU | Vendor SDK adapter, chosen per target | Isolated under `runtimes/npu_<vendor>/`. |
| HTTP / WebSocket | Drogon 1.9.13 (source build) | Async, WebSocket and static files in one library. |
| SQL | SQLite (default), PostgreSQL (optional) | Zero-ops single node; Postgres for multi-instance. |
| Vector DB | Qdrant 1.19 (REST), in-memory for tests | Simple to operate, payload filtering, no C++ client needed. |
| Config | YAML (yaml-cpp 0.7) | Human-edited zones and streams; validated at startup. |
| JSON | nlohmann/json | API and DB JSON columns. |
| Logging | spdlog 1.9 (async, console + rotating file) | Mandatory comprehensive logging without blocking pipelines. |
| Tests | GoogleTest/GMock 1.11 | Standard, available from apt. |
| WebUI | Plain HTML + CSS + ES modules | No build toolchain to maintain; served by the backend. |

## Consequences

- Upgrading any version is a single edit in `docker/Dockerfile` (or compose) plus a rebuild and
  full test run; record notable upgrades here.
- GPU images are large (CUDA/cuDNN/TensorRT); CPU images stay small for development and CI.
