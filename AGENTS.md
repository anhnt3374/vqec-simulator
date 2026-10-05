# AGENTS.md — VQEC Simulator

Mandatory instructions for AI coding agents and human contributors. Scope: the whole repository.
`CLAUDE.md` imports this file; put project rules here, not there.

## 1. What this project is

VQEC Simulator is a C++ video-analytics service. It reads video inputs (RTSP, MP4), runs AI
inference (detection, tracking, embeddings), applies per-use-case business logic with
configurable zones and gates, and produces outputs: annotated video (RTSP/MP4), events,
snapshots and counters. It exposes a REST + WebSocket backend and a static HTML WebUI, stores
data in a SQL database and a vector database, and runs inference on CPU or GPU today and on NPU
later. Everything is built and run inside Docker (Ubuntu 22.04).

| Term | Meaning |
| --- | --- |
| Stream | One video input with its pipeline and outputs. |
| Solution (bài toán) | A pluggable business use case (zone intrusion, line crossing, loitering, face recognition…). Turns tracks + scene into events. |
| Scene | The zones and gates configured for one stream. |
| Zone | Polygon area in normalized `[0,1]` frame coordinates. |
| Gate | Directed line segment in normalized coordinates; crossing is `forward`/`backward`. |
| Runtime | Hardware class that runs inference: `cpu`, `gpu`, `npu`. |
| Backend | Inference library for a runtime: `onnxruntime`, `tensorrt`, `openvino`, vendor NPU SDK. |
| Port | Interface owned by the core (`src/ports/`). |
| Adapter | Implementation of a port with a concrete technology (`src/adapters/`). |

## 2. Build, test, run — always inside Docker

Never build, test or run on the host; the host toolchain is not the target environment.
`scripts/dev.sh` builds the dev image on first use and runs any command inside it with the repo
mounted at `/workspace`.

```bash
scripts/dev.sh                                   # interactive shell (cpu image)
scripts/dev.sh cmake --preset cpu-debug          # configure
scripts/dev.sh cmake --build --preset cpu-debug  # build
scripts/dev.sh ctest --preset cpu-debug          # unit tests
scripts/dev.sh scripts/check.sh                  # layer rules + formatting (required before commit)
scripts/dev.sh scripts/check.sh --tidy           # + clang-tidy (needs a configured cpu-debug)
scripts/dev.sh scripts/format.sh                 # apply clang-format
scripts/dev.sh build/cpu-debug/src/bootstrap/vqec_simulator --config config/simulator.example.yaml --check-config
VQEC_RUNTIME=gpu scripts/dev.sh ...              # GPU image (needs NVIDIA Container Toolkit)
docker compose -f docker/compose.yaml up --build # full stack: simulator + MediaMTX + Qdrant
```

Presets: `cpu-debug`, `cpu-release`, `cpu-asan`, `gpu-debug`, `gpu-release`. Build output goes to
`build/<preset>/`. Dependency versions are pinned in `docker/Dockerfile`; change them only there.

## 3. Repository map

```text
src/
  common/        Result/Status, BoundedQueue, logging facade. No project dependencies.
  domain/        Pure business model: geometry, frame/detection/track, zone/gate/scene, event.
  ports/
    inbound/     Use-case interfaces called by driving adapters (HTTP).
    outbound/    Interfaces the core needs: frame source/sink, detector, tracker, repositories,
                 vector store, event publisher.
  application/   Use cases: stream pipeline, solutions (bài toán), services implementing inbound ports.
  adapters/
    inbound/http/                    REST + WebSocket + static WebUI (Drogon).
    outbound/media/ffmpeg/           RTSP/MP4 sources and sinks, overlay rendering.
    outbound/inference/engine/       IInferenceEngine + Tensor (internal to the inference family).
    outbound/inference/runtimes/     onnxruntime/ tensorrt/ openvino/ npu/ — one dir per backend.
    outbound/inference/models/       Model codecs (pre/post-processing) implementing detector/extractor ports.
    outbound/tracking/               Trackers (ByteTrack…).
    outbound/persistence/            SQL repositories: sqlite/ (default), postgres/.
    outbound/vectordb/               qdrant/ (default), memory/ (dev/test).
  bootstrap/     Composition root: main, config loading, solution factory, adapter wiring.
tests/           unit/ (mirrors src/), integration/ (real adapters), fakes/, support/, data/.
web/             Static WebUI: plain HTML + CSS + ES modules, no build step.
config/          Example runtime configuration (`*.example.yaml` tracked, local copies ignored).
db/migrations/   Forward-only SQL migrations `NNNN_description.sql`.
models/          `manifest.yaml` + docs. Model binaries are never committed.
docker/          Dockerfile (dev/build/runtime stages per runtime), compose files.
scripts/         dev.sh, check.sh, check_layers.sh, format.sh.
tools/           Offline utilities (model export/conversion, data preparation).
docs/            architecture.md, coding_style.md, recipes.md, rest_api.md, adr/.
```

## 4. Architecture rules (hexagonal) — non-negotiable

Dependencies point inward. `scripts/check_layers.sh` enforces the include rules below.

| Layer | May include | Must never include |
| --- | --- | --- |
| `common` | std | any other layer, third-party (except `common/log.*` → spdlog) |
| `domain` | std, `common` (not `common/log.hpp`) | `ports`, `application`, `adapters`, `bootstrap`, any third-party |
| `ports` | std, `common`, `domain` | `application`, `adapters`, `bootstrap`, any third-party |
| `application` | std, `common`, `domain`, `ports` | `adapters`, `bootstrap`, any third-party (logging only through `common/log.hpp`) |
| `adapters/<dir>/<family>` | std, `common`, `domain`, `ports`, own family, its third-party libs | `application`, `bootstrap`, other adapter families |
| `bootstrap` | everything | — |

1. **Domain is pure.** Deterministic logic and data, no I/O, no threads, no logging, no clocks.
2. **Ports are contracts.** Interfaces and plain DTO structs only. Each port documents ownership,
   thread-safety, blocking behavior and error codes. Changing a port signature is an architecture
   change: update every adapter, fake and doc in the same change.
3. **No technology types cross a port.** `cv::Mat`, `AVFrame`, `Ort::Value`, `nvinfer1::*`,
   CUDA pointers, Drogon, sqlite3, YAML or JSON types never appear in `domain/`, `ports/` or
   `application/`. Adapters convert at the boundary.
4. **Only `bootstrap/` wires.** It is the only place that instantiates adapters, reads config
   files and environment variables, and uses `#if VQEC_WITH_*`.
5. **One solution, one directory.** A new solution lives in `application/solutions/<name>/`, is
   registered in `bootstrap/solution_factory.cpp`, and must not modify the pipeline or other
   solutions. If a solution needs new data (e.g. embeddings), extend the port, not the solution.
6. **Runtime-specific code is quarantined.** CUDA, TensorRT, OpenVINO and NPU SDK code lives only
   under `adapters/outbound/inference/runtimes/<backend>/` (hardware codecs under
   `adapters/outbound/media/`). Model codecs stay runtime-agnostic and talk to `IInferenceEngine`.
7. **Adding a runtime/backend must not touch `domain/`, `ports/` or `application/`.** If it seems
   to, stop and propose a port change first.

## 5. Coding rules

**Language and toolchain**
- C++17, GCC 11 (Ubuntu 22.04), CMake ≥ 3.22, Ninja. Do not use compiler extensions.
- Warnings are errors (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion …`).
  Fix the cause; never silence a warning without a `// NOLINT(check): reason` or pragma comment.

**Errors**
- Expected failures (I/O, network, decode, bad input) return `vqec::Status` / `vqec::Result<T>`
  from `common/result.hpp`. Check every result; both types are `[[nodiscard]]`.
- Map third-party errors to `ErrorCode` inside the adapter. Exceptions may be used inside an
  adapter or bootstrap translation unit but must never cross a port, a public header API or a
  thread boundary.
- Programmer errors (violated preconditions) use `assert`. Never use `exit()`/`abort()` outside `main`.

**Ownership and memory**
- No raw `new`/`delete`. Single owner → `std::unique_ptr`; shared lifetime (frames between
  stages, services shared by pipelines) → `std::shared_ptr<const T>` or `std::shared_ptr<T>`.
- Non-owning access is a reference or a raw pointer documented as non-owning; never store a
  reference/`string_view` whose lifetime you do not control.
- Frame pixel buffers are immutable once published (`std::shared_ptr<const ImageBuffer>`).
- Never release or reuse a buffer while a device (GPU/NPU/codec) may still access it.

**Concurrency**
- Each stream pipeline runs on its own thread. Threads talk only through `BoundedQueue` with an
  explicit `OverflowPolicy` (live sources: `DropOldest`; files: `Block`). No unbounded queues.
- Solutions and trackers are single-threaded by contract; no locks inside them.
- Never hold a lock while calling a port, a callback or doing I/O. No detached threads; every
  thread is joined on shutdown in reverse creation order. No `sleep` for synchronization.
- Shared counters/flags use `std::atomic`. Document the thread-safety of every class that is
  shared between threads.

**Performance (per-frame hot path)**
- No blocking I/O on the pipeline thread: repositories and publishers queue internally.
- No per-frame logging above `debug`. Avoid per-frame heap allocations in adapters; reuse buffers.
- Do not claim speedups, zero-copy or real-time performance without a measurement.

**Configuration and constants**
- No magic numbers or strings. Fixed contract values → named `constexpr` (`kMaxBatchSize`);
  tunable policy → validated config field. Defaults live next to the config struct.
- Config is YAML, parsed and validated once at startup into typed structs; fail fast with a
  message naming the offending key. Secrets come from environment variables (`${RTSP_PASSWORD}`),
  never from committed files. Never log URIs that may contain credentials.

**Coordinates, units, time**
- Zones, gates and anchors used by solutions are normalized `[0,1]` frame coordinates.
- Detections and tracks are in pixel coordinates of the source frame (letterbox already undone).
- Durations and timestamps use `std::chrono` types, never raw integers. Frame `pts` is the
  stream clock; `captured_at` is wall clock. Store times in the DB as UTC epoch milliseconds.
- Tensors always carry dtype, layout (NCHW/NHWC), shape and quantization parameters.

**Events, API, data**
- Event type is `<solution_type>.<event_name>` (e.g. `zone_intrusion.enter`). Constants live in
  the solution class.
- REST is versioned (`/api/v1`), JSON fields are `snake_case`. Contract in `docs/rest_api.md`.
- Schema changes go through a new migration in `db/migrations/`; never edit an applied migration.
- Vector collections encode the embedding model and version (`faces_arcface_r100_v1`); never mix
  embeddings of different models in one collection.

## 6. Logging — every component must be observable

Every subsystem (pipeline, each adapter, HTTP, DB, vector DB, inference, bootstrap) logs enough
that an operator can reconstruct what happened from the log alone, without a debugger.

- Use only the `VQEC_LOG_*` macros from `common/log.hpp` (spdlog, async, console + rotating file
  configured by `logging:` in the config). Never `std::cout`/`printf` outside `main` usage text.
- Every message starts with its context in brackets: `[stream=cam_entrance]`,
  `[adapter=ffmpeg_source]`, `[http]`, `[db=sqlite]`, `[engine=tensorrt]`. Include ids, counts,
  durations with units, and the `ErrorCode` + message of any failure.
- **Must log**, at the level shown:

  | Level | What |
  | --- | --- |
  | `info` | Startup (version, runtime, compiled backends, config path), effective config summary (no secrets), every lifecycle transition: stream open/close/reconnect, model load (name, version, backend, device, load time), DB/vector DB connect, HTTP listen address, shutdown. Business events emitted (one line per event). Periodic stats per stream every `logging.stats_interval` (fps, latency per stage, queue depth, dropped frames). |
  | `warn` | Recoverable problems: reconnect attempts with backoff, dropped frames/events, slow stage over budget, retries, degraded mode, rejected API input. |
  | `error` | Failed operations that lose data or stop a component: stream stopped, persistence failure, model load failure, unhandled HTTP 5xx. |
  | `critical` | The process cannot continue; logged right before exit. |
  | `debug` | Decisions useful when diagnosing: per-request HTTP lines, scene updates, solution state changes. |
  | `trace` | Per-frame / per-detection details. Off in production. |

- Every error path logs exactly once, at the layer that decides what to do with it (usually the
  caller that drops, retries or stops) — not at every layer it passes through.
- The domain layer does not log (it stays pure); it returns `Result`/`Status` and the caller logs.
- Hot path: no per-frame logging above `debug`; aggregate into periodic stats instead. Rate-limit
  repeated warnings (log the first, then a count).
- Never log secrets, credentials, full RTSP URIs with passwords, tokens, biometric data or raw
  embeddings. Redact URIs with the shared helper before logging.
- HTTP adapter logs one access line per request (method, path, status, duration) at `debug`,
  failures at `warn`/`error`. The WebUI reports JS errors to the console and to the backend.

## 7. Style — quick reference (details and examples: `docs/coding_style.md`)

| Kind | Style | Example |
| --- | --- | --- |
| Files, directories | `snake_case` | `line_crossing_solution.cpp` |
| Namespaces | `snake_case`, one per layer | `vqec::domain`, `vqec::app`, `vqec::adapters::ffmpeg` |
| Types (class/struct/enum/alias) | `PascalCase` | `StreamPipeline`, `BoxF` |
| Interfaces (abstract classes) | `I` + `PascalCase` | `IFrameSource` |
| Functions, methods | `snake_case`, verb first | `detect_crossing()`, `process_next_frame()` |
| Variables, parameters, public struct fields | `snake_case` | `frame_width` |
| Private/protected data members | `snake_case_` | `last_anchors_` |
| Constants (`constexpr`, `static const`) | `k` + `PascalCase` | `kMinVertices` |
| Enumerators (`enum class` only) | `PascalCase` | `CrossingDirection::Forward` |
| Macros (avoid) | `VQEC_UPPER_CASE` | `VQEC_LOG_INFO` |

- Formatting is whatever `clang-format` produces (`.clang-format`: Google base, 4-space indent,
  100 columns). Do not hand-format; do not reformat files you did not otherwise change.
- `.hpp`/`.cpp` (`.cu` for CUDA); `#pragma once`; one primary type per header; file name =
  primary type in `snake_case` (port headers drop the `I`: `IFrameSource` → `frame_source.hpp`).
- Project includes use the full path from `src/`: `#include "domain/scene/gate.hpp"`. Never `../`.
- Include order (clang-format enforces): own header, C++ std, third-party, project.
- Mark single-argument constructors `explicit`; mark leaf classes `final`; use `override`.
- Prefer return values over out-parameters; out-parameters only for hot-path buffer reuse.
- Comments explain *why* and contracts (ownership, threading, units), not *what* the code does.
- `struct` for passive data with public fields; `class` when there are invariants.

## 8. Testing

- GoogleTest/GMock. Unit tests mirror `src/` under `tests/unit/` and are named `<file>_test.cpp`;
  test names read as sentences: `TEST(GateCrossing, ReportsForwardWhenMovingDownAcrossLeftToRightGate)`.
- Domain and application code must be covered by unit tests using fakes from `tests/fakes/`
  (one fake per port). No real network, GPU, files or sleeps in unit tests.
- Adapter tests that need real dependencies (video files, DB, Qdrant, GPU) go in
  `tests/integration/` with CTest label `integration`; they may be skipped when the dependency is
  unavailable, and must say so.
- Every bug fix comes with a regression test. Small fixtures go in `tests/data/`; large media are
  downloaded by a script, never committed.

## 9. Workflow and definition of done

1. Read the relevant section of `docs/architecture.md` and the port headers you touch.
   Follow the recipes in `docs/recipes.md` for new solutions, runtimes, adapters, endpoints.
2. Keep changes scoped to the task. Do not refactor or reformat unrelated code.
3. Ask before: adding a third-party dependency, changing a port or REST contract, changing the
   DB schema, bumping the C++ standard or a pinned dependency version, or deleting files you did
   not create.
4. Done means: builds with `cpu-debug` without warnings, `ctest` passes, `scripts/check.sh`
   passes, new behavior has tests, and docs/config examples/migrations are updated in the same
   change. Report what was verified and what was not (e.g. "GPU path not built: no GPU here").
5. Never commit build outputs, model binaries, videos, database files, local configs or secrets.
6. Record significant decisions as an ADR in `docs/adr/NNNN_title.md`.
