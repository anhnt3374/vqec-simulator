# Parallel person and face AI live view

Status: approved written specification. The user approved the design direction,
additive REST contract change and this written spec on 2026-10-07.

## Purpose and scope

Provide a working temporary detection flow for testing simulator logic with one
RTSP camera on a CPU-only machine. Run NanoDet-Plus-m 320 for person detection and
YuNet for face detection concurrently, draw both results on the same frame, and
publish one separate RTSP stream that the existing React AI tab can display.
Correct detection accuracy is secondary to a functioning, observable media flow.
Detections must still come from real pretrained models, not fabricated boxes.

The selected embedding model remains MobileFaceNet from InsightFace buffalo_sc.
Embedding, recognition, persistent identities, tracking, events, databases and
vector search are outside this increment. No embedding model download is needed.
GPU execution is a future backend extension, not a completion requirement on the
current CPU-only machine. Keep model codecs independent of the runtime backend.

## Existing system and selected approach

The current Docker live-view stack comprises NestJS, React and MediaMTX. MediaMTX
relays the configured camera to path `relay`. NestJS supplies raw player metadata;
React embeds its WebRTC player. The AI tab is disabled. The C++ processing service
contains contracts and a sequential pipeline, but no wired media or inference
adapters.

Add a C++ AI worker service to the existing live-view Compose stack. Use the
already provisioned FFmpeg, OpenCV, ONNX Runtime, yaml-cpp and spdlog dependencies.
Do not add a Python inference service or new runtime dependencies. This makes
the new flow usable without implementing unrelated persistence or business
solutions. Leave the existing sequential StreamPipeline available for its
current use cases.

Use application orchestration under `src/application/`, model codecs and engine
implementation under their inference adapter directories, media decoding,
overlay and encoding under the FFmpeg media family, and configuration and wiring
only under `src/bootstrap/`. Preserve every existing port signature and layer
dependency rule. Update build targets and a Docker AI runtime target accordingly.

## Media and concurrency flow

```text
camera RTSP -> MediaMTX /relay -> FFmpeg source -> bounded input queue
                                                  |
                                         frame coordinator
                                           /           \
                                  NanoDet worker    YuNet worker
                                           \           /
                                    same-frame result merge
                                                  |
                                      bounded output queue
                                                  |
                               overlay + H.264 RTSP publisher
                                                  |
                                         MediaMTX /ai
                                                  |
                                       WebRTC -> React AI tab
```

Decode once and publish immutable shared image buffers. The coordinator admits
one frame at a time to both inference workers, with at most one active pair.
Workers own separate engines and reusable preprocessing/output buffers. Limit
CPU intra-op threads per model through configuration to control oversubscription.
Pass jobs and result messages exclusively through BoundedQueue; do not use sleeps
as synchronization or hold a lock across inference or another port call.

Every result identifies the admitted frame sequence. Merge only the two results
for that frame; never draw boxes from a previous frame on a newer image. Complete
the pair before admitting another frame. Pending live capture frames use
DropOldest so slow inference does not create an increasing input backlog.
The asynchronous publisher also uses a bounded DropOldest queue. Independent
worker queues have explicit capacities and overflow policies; because only one
pair is active, they cannot silently discard an awaited inference result.

Shutdown closes queues, interrupts blocking media I/O with finite deadlines,
wakes waiting workers and joins threads in reverse creation order. Exceptions
must not escape public adapter APIs or worker threads. Expected failures return
Status/Result and are logged once by the component deciding recovery or stop.

The initial output uses bbox labels `person` and `face`, confidence scores, and
distinct colors. Overlay rendering writes into a separate mutable image and
never changes shared source pixels. Detection annotations have no persistent
track identity. Reuse FrameAnnotations using explicitly documented frame-local
annotation records if required by the current sink contract; do not claim these
are tracked identities.

Encode video-only H.264 with low-latency settings and no B frames; publish via
RTSP/TCP to `rtsp://mediamtx:8554/ai`. Preserve increasing presentation times for
retained frames, including gaps from dropped frames. Audio passthrough is outside
this increment. Browser playback remains MediaMTX WebRTC.

## Models and artifacts

Use pretrained NanoDet-Plus-m at 320x320, retaining only the person class, and
pretrained YuNet with an ONNX input shape supported by the selected artifact.
Record input dimensions, color order, normalization, dtype, output decoding,
class mapping, version, upstream provenance and SHA256 in model metadata.
Model binaries remain ignored and are mounted into the worker read-only.

Provide reproducible artifact preparation/download instructions or a script
with checksum verification. Prefer an upstream ONNX artifact; if NanoDet needs
export, perform it in a separately documented Docker preparation workflow. Adding
any dependency needed for export requires a separate explicit approval before
installation. Missing or incompatible model artifacts fail startup with the
model ID and actionable instructions. Never silently replace a selected model.

NanoDet preprocessing and box decoding must follow its upstream implementation,
including resize/aspect-ratio behavior and inverse coordinate mapping. Decode
YuNet face boxes and landmarks, although landmarks are not consumed by embedding
in this increment. Apply confidence filtering, clipping and model-appropriate
NMS. A frame with no detections is still a valid output frame.

## Configuration and FPS behavior

Add a dedicated typed YAML configuration and tracked example for the AI worker.
Validate at startup: input/output URI, model metadata/artifact paths, detector
thresholds, NMS thresholds, per-model thread counts, input/output queue capacities,
processing FPS limit, output dimensions, encoder FPS and statistics interval.
Defaults live beside typed config fields. Credentials are expanded from env vars
in bootstrap and all logged URIs are redacted.

Define processing FPS as a maximum admission rate: positive values limit work;
zero processes as fast as available frames and CPU permit. This setting does not
increase the camera's source FPS or duplicate frames to simulate additional
inference. Use source PTS or monotonic deadlines for admission decisions, without
sleep-based synchronization. Expose examples for 5, 10, 15, 20, 30 and unlimited
processing FPS. Changing worker YAML and restarting the service is sufficient;
a UI FPS control or a new configuration REST endpoint is outside this increment.

Log startup/model load, source/publisher open, reconnect, shutdown and periodic
input/admitted/output FPS, queue depths, dropped-frame counts and per-model/total
stage latency. Inference failure stops the worker with an error; Docker restarts
it. Media reconnects use bounded backoff and fresh session state so frame sequence
resets cannot mix results. Rate-limit repeated recovery warnings. Do not claim
parallel inference improves throughput before measurement.

## API and React changes

Extend the approved endpoint with an optional mode query:

`GET /api/v1/streams/board43/live-view?mode=raw|ai`

Omitting mode preserves the existing raw response. Raw resolves to public player
path `/relay`; AI resolves to `/ai`. The response shape remains stream_id, mode,
protocol and player_url, with protocol `webrtc`. Reject unsupported modes with
HTTP 400 using the existing error envelope. Unknown streams remain HTTP 404.
Metadata availability is not proof that inference or video delivery is healthy.
Update `docs/rest_api.md` before implementing the approved additive contract.

Enable the AI tab, propagate selected mode to the API client and LiveView,
cancel pending requests on switching, and discard stale responses. Replace the
iframe player on mode changes and keep existing loading/error/retry behavior.
Update tab, footer, iframe title and topbar to reflect the actual selected mode.
Raw remains the initial selection. Browser URLs use the configured public host,
never Docker service names or source credentials.

## Deployment and verification

Extend `docker/compose.live_view.yaml` with the AI worker, read-only models/config
mounts and the MediaMTX publisher path `ai`. Preserve host ports 8000/8001 and
MediaMTX ports. Reuse the existing relay camera configuration. Provide startup,
model preparation, output RTSP viewing and log inspection commands in README.
Record the architectural decision in a new ADR.

Build, test and run everything inside Docker. Required checks are cpu-debug
configure/build, ctest and scripts/check.sh, plus NestJS tests and React tests/build.
Use fakes for application tests covering concurrent dispatch, same-frame merge,
empty results, branch failure, dropped inputs, FPS admission and shutdown without
sleeps or real media. Model codec tests cover deterministic tensor decoding,
person-class filtering, thresholds and mapping boxes back to source coordinates.
Use integration tests for real model loads, RTSP capture/publish and encoding.

Verify the camera relay is reachable, both pretrained models execute, output RTSP
can be decoded, and an AI output frame contains both model annotations when the
source has corresponding detections. Inspect an output image, then verify actual
browser video playback in both tabs when browser tooling is available. API 200,
RTSP negotiation or WebRTC negotiation alone does not establish rendered video.
Report CPU measurements, unavailable camera/browser checks and GPU not tested
accurately. Do not mark unavailable integration checks as passed.

## Review and execution

The design direction, REST extension and written spec are approved. Write the
implementation plan for user review. The recommended execution method is inline in this session;
do not spawn agents unless explicitly authorized. All implementation and local
Docker deployment described here are within the original task scope, subject to
the written-spec and plan review gates.
