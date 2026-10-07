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
`GET /api/v1/streams/board43/live-view`. This first endpoint supplies raw playback
metadata; camera availability and AI overlays are not included.

Build and run this independent stack in Docker (Ubuntu 22.04, linux/amd64):

```bash
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
docker compose -f config/relay.board43.yaml up -d --build
docker compose -f config/relay.board43.yaml logs --tail 50
docker compose -f config/relay.board43.yaml down
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
The independent NestJS API and React raw live view use MediaMTX. C++ adapters
(FFmpeg, ONNX Runtime, TensorRT, Drogon, SQLite, Qdrant, ByteTrack) are not
implemented yet; their directories and build options are in place.
