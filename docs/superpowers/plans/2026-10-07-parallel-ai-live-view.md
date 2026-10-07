# Parallel AI Live View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task after user review and selection of inline execution. Steps use checkbox syntax for tracking. Delegation requires explicit authorization.

**Goal:** Run NanoDet-Plus-m 320 and YuNet concurrently on one RTSP source and show their combined annotated RTSP output in the existing AI tab.

**Architecture:** A C++ worker decodes once, dispatches the same immutable frame to two model workers, merges their results, and queues annotated H.264 publication to MediaMTX `/ai`. Application orchestration uses existing ports; bootstrap wires FFmpeg media adapters and runtime-agnostic model codecs backed by ONNX Runtime CPU. NestJS returns mode-specific player metadata to React.

**Tech Stack:** C++17, GCC 11, CMake/Ninja, FFmpeg, OpenCV, ONNX Runtime, yaml-cpp, spdlog, GoogleTest/GMock, NestJS, React/Vite, MediaMTX; Ubuntu 22.04 Docker.

**Spec:** [Approved design](../specs/2026-10-07-parallel-ai-live-view-design.md).

## Global Constraints

- Build, test and run everything inside Docker.
- Preserve every existing port signature and layer dependency rule.
- Model binaries remain ignored and are mounted into the worker read-only.
- Do not add a Python inference service or new runtime dependencies.
- No embedding, recognition, tracking, persistent identities, events or database work.
- Positive processing FPS limits admission; zero processes as fast as available input permits.
- Source/output pixel buffers are not mutated after publication to another thread.
- No detached threads, unbounded queues, locks across ports or sleep-based synchronization.
- Raw remains the initial selection; host ports stay 8000/8001 and existing MediaMTX ports.
- GPU execution is not a completion requirement on this CPU-only machine.
- Git commits are possible only if `.git` becomes writable; do not repeat failed staging attempts.

## Review Focus

- Non-square input must map both detectors' boxes back correctly; test in Task 2.
- Unequal worker durations must not mix frame sequences or deadlock; test in Task 4.
- Source reconnect and PTS reset must start a fresh session without mixed results; test in Tasks 3 and 4.
- Model/encoder startup failure must be actionable and release partially acquired resources; test in Tasks 1 and 3.
- Rapid Raw/AI switching must discard stale metadata and preserve reverse-proxy paths; test in Task 5.

## Files and responsibilities

- `src/bootstrap/live_ai_config.{hpp,cpp}`: validated worker config and model metadata; `live_ai_main.cpp`: composition root and process lifecycle.
- `src/adapters/outbound/inference/runtimes/onnxruntime/onnx_runtime_engine.{hpp,cpp}`: existing IInferenceEngine implementation.
- `src/adapters/outbound/inference/models/nanodet_detector.{hpp,cpp}` and `yunet_detector.{hpp,cpp}`: independent codecs implementing IObjectDetector.
- `src/adapters/outbound/inference/models/detection_utils.{hpp,cpp}`: shared box mapping/NMS helpers within the inference family.
- `src/adapters/outbound/media/ffmpeg/ffmpeg_frame_source.{hpp,cpp}`, `ffmpeg_frame_sink.{hpp,cpp}` and `overlay_renderer.{hpp,cpp}`: capture, publication and overlay.
- `src/application/pipeline/live_ai_pipeline.{hpp,cpp}`: same-frame fan-out/merge, rate admission and statistics.
- Matching `CMakeLists.txt` files and unit/integration tests: target registration and behavioral validation.
- `config/live_ai.example.yaml`, `models/manifest.yaml`, `scripts/prepare_live_ai_models.sh`: reproducible model/config setup.
- Existing NestJS live-view service/controller and React App/LiveView/API client: mode selection.
- `docker/Dockerfile`, `docker/compose.live_view.yaml`, `config/mediamtx.live_view.example.yaml`: worker build and `/ai` publishing.
- `README.md`, `docs/architecture.md`, `docs/rest_api.md`, new `docs/adr/0003_parallel_ai_live_view.md` if that number is free: operation and architecture decisions.

### Task 1: Typed configuration and real model preparation

**Interfaces:** Produce `vqec::bootstrap::LiveAiConfig` with typed media, model, queue and logging options; `Result<LiveAiConfig> load_live_ai_config(const std::filesystem::path&)`. Define per-model artifact path, checksum, input specification, confidence/NMS thresholds and thread count. Read YAML and env vars only in bootstrap.

- [ ] Add `tests/unit/bootstrap/live_ai_config_test.cpp`: zero/positive processing FPS accepted; negative/nonfinite rates rejected; threshold outside [0,1], zero queue capacity, invalid dimensions, missing artifacts and malformed model metadata rejected with the offending key. Test `${ENV_VAR}` expansion without secret logging.
- [ ] Run new tests in Docker and confirm failure before adding the loader.
- [ ] Implement loader and example YAML: input `rtsp://mediamtx:8554/relay`, output `rtsp://mediamtx:8554/ai`, unlimited processing FPS, encoder nominal 15 FPS, 1280x720 output, queue capacities 2, one intra-op thread per model, confidence 0.35, NMS IoU 0.5, ten-second stats interval, ten-second media I/O deadline. These are initial tunable defaults, not performance promises.
- [ ] Add metadata for exact NanoDet-Plus-m 320 and YuNet ONNX variants. Inspect upstream release assets to acquire NanoDet ONNX directly, avoiding export dependencies. Pin verified download URLs/revisions and SHA256 in `prepare_live_ai_models.sh` and manifest. For YuNet, use an upstream fixed-shape artifact whose shape is read and respected by the codec.
- [ ] Download and inspect artifacts inside Docker; validate checksum failure is detected. If an official NanoDet ONNX artifact cannot be obtained, pause only its dependent preparation and request approval for export dependencies; continue independent tasks. Never substitute another NanoDet scale.
- [ ] Run loader tests successfully and record actual model I/O shapes before Task 2.

### Task 2: ONNX Runtime engine and two detector codecs

**Interfaces:** `OnnxRuntimeEngine final : IInferenceEngine` implements existing `load`, `input_specs`, `output_specs`, `run`. `NanoDetDetector` and `YuNetDetector` implement existing IObjectDetector, own an engine and expose fixed labels `person` and `face` respectively; constructors take an engine and explicit model options defined in their headers. Each codec serializes external calls; one pipeline worker owns each instance.

- [ ] Add fake inference engine under `tests/fakes/fake_inference_engine.hpp`; add codec tests under `tests/unit/adapters/outbound/inference/models/`. Assert person-only filtering, confidence rejection, expected DFL/stride decoding for NanoDet, YuNet output decoding, clipping/NMS, empty results and non-square coordinate mapping. Assert malformed tensor shapes/bytes and NaN predictions are rejected without out-of-bounds access.
- [ ] Run targeted tests and confirm failure before implementing codecs.
- [ ] Implement engine with ONNX Runtime C++ API, CPU intra-op limits and error translation. Resolve dynamic output shapes after execution; do not expose Ort types beyond this runtime. Add CMake discovery for the existing `/opt/onnxruntime` installation.
- [ ] Implement codec preprocessing/decoding from the exact upstream graph contracts confirmed in Task 1. Preserve NanoDet upstream normalization and warp semantics. Reuse buffers; map all boxes to original frame pixel coordinates. Decode YuNet landmarks internally without changing domain DTOs.
- [ ] Run codec tests successfully. Add real-model integration tests under `tests/integration/inference/` with CTest integration label and explicit unavailable-model skip messages. Confirm both actual pretrained artifacts load and infer in Docker.

### Task 3: RTSP source, overlay and asynchronous publisher

**Interfaces:** `FfmpegFrameSource final : IFrameSource` implements existing source contract with RTSP/TCP and interruptible I/O. `FfmpegFrameSink final : IFrameSink` queues publication internally and implements existing open/write/close. `OverlayRenderer` consumes a Frame and FrameAnnotations inside the media family, creating a mutable output copy. Sink options specify output dimensions, nominal encoder FPS, queue capacity and I/O deadline.

- [ ] Add overlay tests under `tests/unit/adapters/outbound/media/ffmpeg/overlay_renderer_test.cpp`: shared pixels unchanged, box scaling/clipping correct, distinct label colors, empty annotations valid. Add testable time conversion checks for monotonic encoder PTS across dropped source frames.
- [ ] Run tests and confirm failure before implementing media components.
- [ ] Implement decode to immutable host BGR24 frames. Convert FFmpeg timestamps to chrono PTS, correctly handle stride, ownership, EOF, unsupported input and interrupt deadlines. Close resources idempotently on partial open failure.
- [ ] Implement overlay and H.264/YUV420P encoding, using a software encoder available in the existing FFmpeg build, no B frames and low-latency options. Publish RTSP/TCP; detect missing encoder at startup with an actionable error. Keep encoding/network work on the sink's joined worker behind DropOldest.
- [ ] Run unit tests. Add `tests/integration/media/ffmpeg/rtsp_round_trip_test.cpp`: capture/publish through a Docker MediaMTX test service, decode output and inspect dimensions and overlay. Exercise disconnect/reopen, interruption and partial startup failure; explicitly skip when its real dependency is absent.

### Task 4: Parallel application orchestration and worker bootstrap

**Interfaces:** `LiveAiPipeline` takes StreamId, unique IFrameSource, two shared IObjectDetector instances, unique IFrameSink and `LiveAiOptions`; exposes `Status run(const std::atomic<bool>& stop_requested)`. Application DTOs `DetectorJob` and `DetectorResult` carry frame sequence and result status; one active pair prevents discarding an awaited result. Define a pure PTS-based admission helper used by run and tested independently.

- [ ] Add `tests/unit/application/live_ai_pipeline_test.cpp`. Fakes use condition variables/queue handshakes to prove both detectors enter before either completes, without sleeps. Assert same-frame merge, two distinct labels, empty output, branch failure propagation, bounded input drops and joined shutdown. Assert reset PTS uses new session state. Admission test: at 10 FPS, frames at 0/50/100 ms admit first/third; zero admits all.
- [ ] Run tests and confirm failure before implementing orchestration.
- [ ] Implement capture queue, coordinator, worker job/result queues and joined lifecycle as specified. Convert detections to frame-local Track annotations solely for the existing sink contract, documenting that their IDs provide no persistent tracking. Keep business solutions out of this worker.
- [ ] Add per-stage stats, queue counters and lifecycle logs using VQEC macros. Handle source reconnect with bounded backoff through interruptible condition-variable deadlines, rebuild session state and avoid per-frame info logs.
- [ ] Implement `live_ai_main.cpp`: parse `--config` and `--check-config`, initialize logging, load model metadata and engines, wire ports, respond to SIGINT/SIGTERM and exit with a meaningful code on unrecoverable failure. No vendor construction or env access in application.
- [ ] Register `vqec_live_ai` and adapter targets; enable implemented media/ORT adapters for cpu-debug and cpu-release. Run targeted tests successfully; verify `--check-config` accepts example configuration with real models and rejects missing model files.

### Task 5: Approved Raw/AI REST contract and visualizer switch

**Interfaces:** NestJS `get_live_view(stream_id: string, mode: string = 'raw'): LiveViewDto`; DTO mode becomes `'raw' | 'ai'`. API client `fetch_live_view({ signal, base_url, mode = 'raw' })`; React `LiveView({ mode = 'raw' })`.

- [ ] Update `docs/rest_api.md` with the approved query before code changes. Add backend tests for omitted/raw mode -> `/relay`, ai -> `/ai`, invalid/repeated mode rejection, unknown stream 404 and reverse-proxy base-path preservation. Add API client tests for matching selected mode, invalid metadata, HTTP failure and abort.
- [ ] Run backend/frontend tests in Ubuntu Docker and confirm failure for new behavior.
- [ ] Add controller query validation and mode-specific service URLs. Keep response errors and credential protection consistent with existing middleware/filter.
- [ ] Implement mode state in App, enable AI tab, update topbar/footer/title and pass selected mode to LiveView. Include mode in effect dependencies, abort old requests and replace iframe on switch. Display only the selected mode's metadata, with loading/error/retry behavior.
- [ ] Run tests and frontend build successfully. Extend `tests/integration/web/live_view_smoke.cjs` for Raw/AI/Raw playback, rapid switch stale-response handling, retry and unmount cancellation; update interception patterns to accept mode queries.

### Task 6: Docker deployment and end-to-end verification

**Interfaces:** Compose `ai-worker` reads read-only YAML/models and publishes `/ai`; MediaMTX path `ai` accepts a publisher. Add Docker `live-ai-runtime` target without new third-party libraries or dependency pin changes.

- [ ] Add worker runtime/build target and required shared library copies, preserving existing runtime targets. Add worker service, model/config mounts and MediaMTX `/ai` path to live-view Compose; preserve existing ports and relay source env configuration.
- [ ] Validate Compose using the existing local env configuration without printing credentials. Build worker, backend and web with Docker.
- [ ] Run `scripts/dev.sh cmake --preset cpu-debug`, `scripts/dev.sh cmake --build --preset cpu-debug`, `scripts/dev.sh ctest --preset cpu-debug`, and `scripts/dev.sh scripts/check.sh`. Run relevant integration tests in Docker, backend tests and frontend tests/build. Check actual exit codes; unavailable checks are not passes.
- [ ] Start the reviewed local live-view stack. Confirm camera `/relay` decode, both detector invocation stats, output `/ai` decode and an inspected annotated frame. Run the browser smoke test when tooling is available, proving video frames actually render in both modes.
- [ ] Measure processing/output FPS and latency for selected admission limits; document hardware/input dimensions and drops. Unlimited consumes available source frames; do not manufacture a source FPS increase. Report GPU path not tested.
- [ ] Update README, architecture docs, model preparation docs and ADR with commands, limitations and actual verification. Perform inline whole-change review against spec, layer rules, memory lifetime, timestamp handling and shutdown.
- [ ] Commit scoped changes only if Git metadata is writable; otherwise leave reviewable workspace changes and explicitly report that no commit was made.

## Execution status

Authorized and executed inline (2026-10-07). Tasks 1–6 are delivered; the
unchecked granular bullets above retain the original planned checks rather than
claiming every planned fixture exists unchanged. Evidence and rulings follow.

- Typed config and exact pretrained artifacts validated; model contracts inspected.
- Both codecs and ORT CPU adapter implemented; decoder unit tests and real-model
  integration cover inference. Decoder helpers replace the planned fake-engine
  fixtures. Tensor buffers are reused; OpenCV resize and metadata allocations
  remain prototype optimization work, with measured performance reported.
- FFmpeg capture, immutable overlay, asynchronous publication and PTS conversion
  implemented. RTSP checks live with the other model integration tests, rather
  than a separate round-trip fixture/service. Actual `/relay` and `/ai` decoded;
  partial startup failures and checksum failures are tested. Source reconnect
  uses bounded backoff; repeated-disconnect soak testing remains unverified.
- Same-frame parallelism, failure propagation, empty detections/EOF, rate reset
  and joined EOF shutdown tested; bounded queue drop behavior covered by queue
  unit tests and zero drops observed in measured live runs. No dedicated
  repeated-reconnect application fake was added.
- Raw/AI contract, tests, UI switching, browser playback/retry/cancellation pass.
- Docker worker deployed on the existing camera stack. Measured 0/5/10 rate
  settings; restored local `processing_fps=0`. Docs and ADR 0004 updated.
- cpu-debug build, 52 unit tests, integration tests, layers/format, 30 backend
  tests, 11 frontend tests and Vite build pass. No GPU verification.
- Whole-change review performed inline under the approved no-agent execution.
  Review fixes: FPS numeric range validation and queue/thread exception handling.
  No changes to ports, vendor dependency pins or DB schema. No commit because
  Git metadata is read-only; unrelated existing files preserved.

Original handoff (superseded by approval and execution):

This plan is ready for user review. Recommend inline execution because adapter,
codec and orchestration tasks depend on shared interfaces and there is one camera
to integrate. Wait for the user to approve this plan and select inline execution
before implementation. Follow executing-plans, test-driven-development and
verification-before-completion; keep the user informed during long Docker builds.
