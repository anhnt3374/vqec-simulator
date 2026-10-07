# ADR 0004: Parallel CPU person and face live view

Status: accepted (2026-10-07).

## Context

One RTSP camera and a CPU-only workstation need a runnable prototype for testing
application logic. The user approved NanoDet-Plus-m 320 for person detection,
YuNet for face detection, and MobileFaceNet as the later embedding choice.
This increment implements the two detection branches and combined playback.

## Decision

Add a dedicated C++ `vqec_live_ai` process in the existing Docker stack. Decode
once, send the same immutable frame to two independent ONNX Runtime CPU sessions,
merge only matching frame sequences, and publish one H.264 RTSP stream at `/ai`.
FFmpeg publishing uses its own bounded DropOldest queue and joined thread.
NanoDet returns only COCO person; YuNet returns face. Runtime-agnostic model
codecs map detections to source pixels. Bootstrap pins and verifies SHA256 for
exact upstream artifacts; model binaries remain ignored, read-only mounts.

The worker uses existing ports unchanged. Frame-local Tentative Track DTOs with
ID zero satisfy the existing annotation sink contract, without persistent
tracking. NestJS adds the approved `mode=raw|ai` query and React switches players,
aborting stale metadata requests. Raw is the initial selection.

`processing_fps=0` admits all available source frames. A positive rate enforces
minimum source-PTS spacing between admissions. Output preserves timestamp gaps;
encoder nominal FPS does not duplicate frames. Each source failure reconnects
with bounded backoff. Publisher failure exits for Compose to restart the worker.

No new product dependency, dependency version bump, DB schema or port change is
needed. GPU execution, embedding/recognition, tracking and business events are
deferred. A GPU backend can later replace the engine inside bootstrap.

## Verification and measured limits

Measured inside Ubuntu 22.04 Docker on AMD Ryzen 5 7530U (6 cores / 12 threads),
CPU execution, one intra-op thread per model. Camera source: 2592×1944, 25 FPS;
annotated output: 1280×720. Ten-second log windows, not a long-duration benchmark:

| processing_fps | Processed FPS | Output FPS | Person / face / pair milliseconds | Queue drops |
| --- | --- | --- | --- | --- |
| 0 | 24.99–25.10 | 24.92–25.10 | 25.25–25.75 / 15.74–15.87 / 25.32–25.81 | 0 |
| 5 | 4.96–4.99 | 4.96–4.98 | 25.41–26.44 / 15.76–16.42 / 25.50–26.50 | 0 |
| 10 | 8.28–8.33 | 8.27–8.33 | 26.06–26.24 / 16.50–16.71 / 26.12–26.29 | 0 |

The 10 FPS limit admits every third 40 ms source frame, hence approximately
8.33 FPS. Increasing a configured limit above 25 cannot manufacture source
frames. Pair latency covers detector dispatch/preprocessing/inference/postprocess;
it is not camera-to-browser latency. Source decode, encode, network and WebRTC
playback buffering are additional costs. Output resizing may change aspect ratio;
tune output dimensions for your source. Accuracy is uncalibrated; the observed
scene includes false positives, consistent with this logic-testing prototype.

Verification: cpu-debug build with warnings as errors; 52 unit tests; 5 real-model/media/RTSP
integration tests; required layer/format checks; 30 backend tests; 11 frontend
API tests and Vite production build. Docker Chromium decoded raw/AI/raw video,
verified stale mode cancellation, API retry and abort on unmount. Inspected both
the annotated RTSP snapshot and AI browser screenshot. Local MediaMTX recreation
required remounting its edited config; final `/ai` publication is active.

GPU path and sustained load/disconnect soak testing were not performed. Git
metadata is read-only, so changes are left in the workspace without a commit.
