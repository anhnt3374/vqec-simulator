# VQEC Simulator

C++ video-analytics service: RTSP/MP4 in → AI inference (CPU / GPU / NPU) → business solutions
with zones and gates → annotated video, events, REST/WebSocket API, WebUI, SQL + vector storage.

- Rules for contributors and AI agents: [AGENTS.md](AGENTS.md) (also loaded by `CLAUDE.md`)
- Architecture: [docs/architecture.md](docs/architecture.md)
- Coding style: [docs/coding_style.md](docs/coding_style.md)
- How-to recipes: [docs/recipes.md](docs/recipes.md)
- API contract: [docs/rest_api.md](docs/rest_api.md)
- Decisions: [docs/adr/](docs/adr/)

## Quick start

### React live view + NestJS API

The WebUI API lives in `backend/` (NestJS). MediaMTX receives an RTSP source and
serves its WebRTC player, which React loads using
`GET /api/v1/streams/board43/live-view`. Use the Gốc/AI buttons to switch between raw `/relay` and the annotated `/ai`
stream. The C++ CPU worker runs NanoDet person and YuNet face detection in parallel.
Playback metadata does not assert camera availability.

Build and run this independent stack in Docker (Ubuntu 22.04, linux/amd64):

```bash
scripts/dev.sh scripts/prepare_live_ai_models.sh
cp config/live_ai.example.yaml config/live_ai.yaml
export LIVE_AI_CONFIG=../config/live_ai.yaml
export LIVE_VIEW_HOST=192.168.138.217  # LAN IP of the machine running Docker
export LIVE_VIEW_SOURCE=rtsp://192.168.138.43:18554/live/hd
export WEB_PORT=8000
export API_PORT=8001
docker compose -f docker/compose.live_view.yaml up -d --build
```

Open `http://192.168.138.217:8000` (or `http://localhost:8000`). The API is at
`http://192.168.138.217:8001/api/v1/streams/board43/live-view` and its player is at
`http://192.168.138.217:8889/relay`. RTSP remains available at
`rtsp://192.168.138.217:8554/relay`.

If the existing board43 local relay is running, use its extended composition
instead of starting a second MediaMTX on the same ports:

```bash
export LIVE_AI_CONFIG=../config/live_ai.yaml
# Ensure the local MediaMTX config includes: paths.ai.source: publisher
docker compose -f config/relay.board43.yaml -f docker/compose.live_ai.yaml up -d --build
docker compose -f config/relay.board43.yaml -f docker/compose.live_ai.yaml logs --tail 50 ai-worker
docker compose -f config/relay.board43.yaml -f docker/compose.live_ai.yaml down
```

The local composition publishes the Docker UI on **8000** and API on **8001**.
Local configs are ignored by git. The tracked
composition above reproduces the setup on another machine using its LAN IP.
For sources requiring credentials, provide `LIVE_VIEW_SOURCE` from a local
environment file and do not commit it.

Allow TCP 8000/8001/8889 and TCP+UDP 8189 on the Docker host for LAN access.
`LIVE_VIEW_HOST` is used both in the player URL and advertised ICE candidates;
update it when the host IP changes. If video does not appear, open the player
URL directly and check MediaMTX logs/source connectivity. Do not embed an HTTP
player in an HTTPS frontend; that deployment requires an HTTPS player/reverse
proxy. This composition uses the Vite development server for local use.

Run Node checks inside Docker:

```bash
docker build -f docker/Dockerfile --target node-dev -t vqec-node-dev:local .
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$PWD:/workspace" -w /workspace/backend vqec-node-dev:local \
  bash -lc 'npm ci && npm test'
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$PWD:/workspace" -w /workspace/web vqec-node-dev:local \
  bash -lc 'npm ci && npm test && npm run build'
```

See [backend/README.md](backend/README.md) for configuration.

### Draw a temporary outward-crossing line

Open **Bộ lọc đối tượng → Vẽ line**. The UI switches to AI playback. Click two points
on the video and drag A/B to edit. The perpendicular **Ra ngoài** arrow indicates the
counted direction; **Đảo chiều** reverses it. **Lưu line** applies the current line;
**Xóa line** removes it. **Vẽ lại** only clears the draft until you save a replacement.
Confirmed people crossing outward get red boxes until their tracks expire; every outward
crossing writes `line_crossing.cross` to `ai-worker` logs. Faces keep their pink boxes.

Reload clears the line and all crossing highlights. This prototype uses one active page
session for the shared stream, so opening another page takes control and clears the old
line. Both Compose recipes above wire a RAM-backed snapshot between backend and worker;
rebuild/recreate the stack after updating. No database or browser storage is used.
For a custom output aspect ratio, pass `VITE_AI_ASPECT_RATIO=width/height` as a numeric
ratio to the web service (default 1.7777777778 for 1280×720).

### Draw a temporary polygon zone

Open **Bộ lọc đối tượng → Vẽ zone**. Click 3–32 points in order; the polygon closes
back to its first vertex. Drag numbered vertices to edit, use **Bỏ điểm cuối** to undo,
and **Lưu zone** to apply. **Xóa zone** removes the saved zone. **Vẽ lại** clears only
the draft. The saved zone stays shaded in the AI view after closing the editor or switching
to line drawing; the shade does not intercept clicks. Self-intersecting/degenerate polygons are rejected; concave polygons are allowed.

Confirmed people's bottom-center anchors determine membership: bbox turns **orange after 5 seconds inside**,
**green outside**, with **red line-crossing color taking precedence**. The worker logs
`zone_intrusion.enter` once per entry, including a first observation inside. Line and zone
saves do not reset each other's tracking history. Both are temporary and clear on reload.
Rebuild/recreate the backend, web and AI worker using the same Compose commands above.

### Change AI processing FPS

Edit `processing_fps` in your ignored `config/live_ai.yaml`: `0` means unlimited;
try `5`, `10`, `15`, `20`, `30`. Restart the worker after editing:

```bash
export LIVE_AI_CONFIG=../config/live_ai.yaml
docker compose -f config/relay.board43.yaml -f docker/compose.live_ai.yaml up -d --force-recreate ai-worker
```

For the standalone stack, use `-f docker/compose.live_view.yaml` instead. Recreating
ensures a file replaced by an editor is mounted again. `processing_fps` limits
admission using source PTS; effective rates can be lower when source timing does
not divide evenly or the CPU cannot keep up. It does not generate extra source
frames. `output.fps` is the encoder nominal rate, not a second limiter. The output
is `rtsp://<host>:8554/ai`, 1280×720 by default, with green `person #ID` boxes from ByteTrack and pink
face boxes. ByteTrack preserves IDs across a short loss (default one second);
Lost prediction boxes stay hidden. Tune the `tracking` block in
`config/live_ai.yaml` and recreate the worker with the same command.
`low_threshold` admits weak detections for existing tracks, `high_threshold`
controls first-pass matching, and `new_threshold` starts new IDs. Effective
NanoDet confidence is `min(person.confidence, tracking.low_threshold)` so low
confidence associations remain available. IDs are local to a worker lifetime;
reconnect resets associations while keeping its ID counter increasing. Live queues drop old frames under load.

See [the measured CPU run and limitations](docs/adr/0004_parallel_ai_live_view.md).

### Simulate a board with 18 FPS AI capacity

In `config/live_ai.yaml`, set:

```yaml
processing_fps: 0
simulation:
  ai_fps: 18
```

Each person + face + tracking job occupies at least 55.6 ms of wall-clock time,
including actual processing. Faster execution waits out the remaining budget;
slower execution stays slower. Capture continues independently, and bounded live
queues discard old frames when AI cannot keep up. This models service time and
queue pressure, not the board's GPU/NPU utilization or memory bandwidth.

`simulation.ai_fps: 0` disables artificial delay; accepted positive rates are
1–1000. Keep `processing_fps: 0` when measuring simulated capacity; positive
`processing_fps` is an additional source-PTS admission filter. Output timestamps
remain those of selected source frames. `output.fps` stays nominal encoder FPS.
Logs include `simulation_ai_fps`, `simulation_wait_ms`, `pair_ms`, input/processed
FPS and queue drops. `pair_ms` includes the simulated wait; detector and tracking
times report the actual processing costs.

The Docker integration test with a timed 25 FPS source measured 17.953 processed
FPS: 56 of 75 frames processed, 19 discarded, over 3.119 seconds. This uses test
detectors to isolate the pacing behavior; real models or encode bottlenecks may
lower throughput. Change the YAML and recreate the worker as described above.

### C++ core

Everything runs in Docker (Ubuntu 22.04); the host only needs Docker (and the NVIDIA Container
Toolkit for GPU).

```bash
scripts/dev.sh cmake --preset cpu-debug
scripts/dev.sh cmake --build --preset cpu-debug
scripts/dev.sh ctest --preset cpu-debug
scripts/dev.sh scripts/check.sh

cp config/simulator.example.yaml config/simulator.yaml   # then edit streams/zones
scripts/dev.sh build/cpu-debug/src/bootstrap/vqec_simulator --config config/simulator.yaml --check-config

docker compose -f docker/compose.yaml up --build          # simulator + MediaMTX + Qdrant
```

## Status

v0.1 scaffold: domain (geometry, zones, gates, events), ports, stream pipeline, two example
solutions (`zone_intrusion`, `line_crossing`), config loading, logging, tests, Docker images.
The independent NestJS API and React Gốc/AI live view use MediaMTX. The CPU live
AI worker implements FFmpeg RTSP capture/publication, ONNX Runtime, NanoDet and
YuNet, plus ByteTrack person tracking. GPU execution, embeddings/recognition,
face tracking and the remaining production adapters are future work.
