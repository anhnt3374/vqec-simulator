# Architecture

Status: initial design (v0.1). Decisions are recorded in `docs/adr/`.

## 1. Goals

1. Feed video (RTSP, MP4) through AI inference and per-use-case business logic ("solutions")
   with zones and gates, producing annotated video, events, snapshots and counters.
2. Run the same core on different inference runtimes — CPU and GPU now, NPU later — by swapping
   adapters, never by changing business code.
3. Be operable: WebUI for configuration and live view, REST/WebSocket API, SQL + vector storage,
   and logs complete enough to diagnose problems without a debugger.

## 2. Hexagonal layout

```text
                          driving side                                   driven side
                  ┌──────────────────────────┐                   ┌──────────────────────────────┐
 Browser (web/) ──► adapters/inbound/http    │                   │ adapters/outbound/media      │◄── RTSP / MP4 in
 REST + WS        │  (Drogon)                │                   │  (FFmpeg sources & sinks)    │──► RTSP (MediaMTX) / MP4 out
                  └────────────┬─────────────┘                   │ adapters/outbound/inference  │
                               │ ports/inbound                   │  models/ × runtimes/         │──► CPU / GPU / NPU
                  ┌────────────▼──────────────────────────┐      │ adapters/outbound/tracking   │
                  │ application                           │      │ adapters/outbound/persistence│──► SQLite / PostgreSQL
                  │  services  ·  StreamPipeline          │      │ adapters/outbound/vectordb   │──► Qdrant / in-memory
                  │  solutions/<bài toán>                 ├──────► (ports/outbound interfaces)   │
                  │ domain: geometry, frame, track,       │      └──────────────────────────────┘
                  │         zone, gate, scene, event      │
                  └───────────────────────────────────────┘
                     bootstrap/ wires everything from config (the only place that knows adapters)
```

| Layer | Contains | Depends on |
| --- | --- | --- |
| `common` | `Result`/`Status`, `BoundedQueue`, logging facade, `redact_uri` | std (+ spdlog for logging) |
| `domain` | Value types and pure rules: `Polygon`, `Gate` + `detect_crossing`, `Frame`, `Track`, `Event` | `common` |
| `ports` | Interfaces + DTOs: inbound (use cases) and outbound (what the core needs) | `domain` |
| `application` | `StreamPipeline`, `ISolution` + solutions, services implementing inbound ports | `ports` |
| `adapters` | Technology implementations of ports | `ports` + third-party |
| `bootstrap` | `main`, config, factories, wiring | everything |

The include rules are enforced by `scripts/check_layers.sh`; see `AGENTS.md` section 4.

## 3. Frame flow (one stream)

```text
IFrameSource.read()            decoded Frame (pixels shared, immutable)
  → IObjectDetector.detect()   Detections in source-frame pixels
  → IObjectTracker.update()    Tracks (Tentative / Confirmed / Lost)
  → ISolution.process() × N    Events "<solution>.<event>" (zones/gates in normalized coords)
  → IEventRepository.save()    async, never blocks the pipeline
  → IEventPublisher.publish()  WebSocket fan-out, drops when clients are slow
  → IFrameSink.write() × M     overlay + encode → RTSP push / MP4 file
```

Version 1 runs these stages sequentially on one thread per stream (`StreamPipeline::run`).
When profiling shows a bottleneck, stages are split onto threads connected by `BoundedQueue`:

- live sources use `OverflowPolicy::DropOldest` (latency over completeness);
- file sources use `OverflowPolicy::Block` (every frame is processed, results reproducible).

The detector is shared by all streams and is thread-safe; a GPU/NPU implementation batches
frames from several streams internally. Trackers, solutions and sinks are per stream and
single-threaded.

Scene changes from the WebUI go through `ISceneManagement` → persisted → handed to the pipeline
with `request_scene_update()`, applied between two frames (no locks in solutions).

## 4. Solutions (bài toán)

A solution implements `app::ISolution` and lives in `application/solutions/<type>/`:

- `type()` — config name and event prefix, e.g. `zone_intrusion`.
- `set_scene()` — receives zones/gates; called before the first frame and on every change.
- `process(FrameContext)` — returns events for this frame; keeps per-track state.

Shared conventions: Tentative tracks are ignored, Lost tracks keep their state, tracks absent
from a frame are forgotten. Position tests use a box anchor (`bottom_center` by default)
converted to normalized coordinates.

Shipped examples: `zone_intrusion` (enter events), `line_crossing` (forward/backward crossings).
Planned: loitering (dwell time in zone), crowd counting (occupancy per zone), face recognition
(embedding + vector search), PPE/attribute checks. Solutions needing more data (embeddings,
attributes) get it through new outbound ports, never by calling adapters directly.

## 5. Inference: multiple runtimes

```text
adapters/outbound/inference/
  engine/      IInferenceEngine, Tensor, TensorSpec (dtype, layout, shape, quantization)
  models/      model codecs: YOLO detector, ArcFace embedder… implement IObjectDetector /
               IFeatureExtractor; do pre/post-processing; talk only to IInferenceEngine
  runtimes/
    onnxruntime/   cpu (optionally CUDA EP)       VQEC_WITH_ONNXRUNTIME
    tensorrt/      gpu, builds/caches engines     VQEC_WITH_TENSORRT
    openvino/      cpu / Intel iGPU               VQEC_WITH_OPENVINO
    npu/           vendor SDK (future)            VQEC_WITH_<VENDOR>
```

Any model codec runs on any backend: `detector = YoloDetector(engine = TensorRtEngine(...))`.
Bootstrap chooses the backend from `inference.backend` and the artifact from
`models/manifest.yaml` (one artifact per backend, e.g. ONNX for ORT/TensorRT, a compiled/quantized
graph for an NPU).

Rules for runtimes:
- Vendor types (CUDA streams, `nvinfer1::*`, NPU handles) stay inside `runtimes/<backend>/`.
- Quantization parameters and tensor layouts are explicit in `TensorSpec`; a backend that needs
  NHWC/int8 converts inside the codec↔engine boundary, not in solutions.
- Zero-copy device paths (NVDEC → CUDA preprocessing → TensorRT) are optimizations inside the
  media/inference adapters behind an opaque handle; they must not change port signatures.
- Adding an NPU: new `runtimes/npu_<vendor>/` target + CMake option + Dockerfile stages
  `devbase-npu`/`runbase-npu` + manifest artifacts. No change in domain/ports/application.

Build variants: one Docker image per runtime class (`TARGET_RUNTIME=cpu|gpu`), CMake presets
`cpu-*` / `gpu-*`. The binary logs which backends it contains and refuses to start with a
backend it was not built with.

## 6. Storage

| Data | Store | Notes |
| --- | --- | --- |
| Streams, zones, gates, solution params | SQL | Config file seeds an empty DB; afterwards the WebUI edits the DB. |
| Events | SQL | Indexed by stream/type + time; written asynchronously. |
| Snapshots, recordings | Files under `storage.media_dir` | DB stores relative paths. |
| Embeddings (faces, re-id) | Vector DB (Qdrant) | One collection per purpose + model version: `faces_arcface_r100_v1`. |

SQL dialect: the portable subset of SQLite and PostgreSQL (TEXT ids generated by the app,
BIGINT epoch-ms UTC timestamps, JSON as TEXT). Migrations in `db/migrations/NNNN_*.sql` are
forward-only and applied at startup by the persistence adapter. SQLite is the default (single
node, zero ops); PostgreSQL is for multi-instance deployments.

## 7. Deployment (docker compose)

| Service | Image | Role |
| --- | --- | --- |
| `simulator` | built from `docker/Dockerfile` (`runtime` target) | pipelines, REST/WS, WebUI |
| `mediamtx` | `bluenviron/mediamtx` | RTSP ingest of annotated streams, WebRTC to browsers |
| `qdrant` | `qdrant/qdrant` | vector database |

GPU: add `docker/compose.gpu.yaml`. Config is mounted read-only from `config/`, models from
`models/`, data and logs on volumes. Credentials come from `.env` via `${NAME}` references.

## 8. Observability

Logging rules are mandatory (`AGENTS.md` section 6). Every component logs lifecycle transitions,
failures with error codes and context tags, and each stream emits a periodic stats line
(fps, per-stage latency, events, persistence failures). Logs go to console (docker logs) and to
a rotating file (`logging.directory`), asynchronously so the pipeline never blocks on I/O.
Planned: `/api/v1/metrics` exposing the same counters for dashboards.
