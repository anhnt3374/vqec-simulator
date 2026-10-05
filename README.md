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
Adapters (FFmpeg, ONNX Runtime, TensorRT, Drogon, SQLite, Qdrant, ByteTrack) are not
implemented yet; their directories and build options are in place.
