# WebUI API

NestJS serves playback metadata; MediaMTX streams video directly to the browser.
Build, test and run this service with the Docker commands in the root README.
No C++ processing adapters are needed for raw relay playback.

| Environment | Meaning |
| --- | --- |
| `PORT` | Listen port, default `3000`; integer 1–65535. |
| `MEDIAMTX_PUBLIC_URL` | Required public HTTP(S) player base, e.g. `http://192.168.138.217:8889`. Optional reverse-proxy path; no credentials, query or fragment. |
| `WEB_ORIGINS` | Comma-separated frontend origins allowed by CORS. Default empty (no cross-origin browser access); same-origin Vite proxy needs no CORS. |

The tracked Compose uses `LIVE_VIEW_HOST` to configure the public player base and
MediaMTX ICE candidate, `LIVE_VIEW_SOURCE` for camera input and `WEB_PORT` for the
frontend host port (default 8000). `API_PORT` publishes NestJS on host port 8001
by default; inside Docker it listens on 3000. Vite proxies `/api` to the backend container;
standalone Docker development can set `API_PROXY_TARGET` in the web container.
For a separate API origin, set `VITE_API_BASE_URL` when starting/building Vite and
add the frontend origin to `WEB_ORIGINS`. A production frontend can alternatively
use a same-origin `/api` reverse proxy to NestJS.

`GET /api/v1/health` returns process liveness. It does not check the camera.
`GET /api/v1/streams/board43/live-view` returns the configured raw WebRTC player
URL; `?mode=ai` selects the annotated `/ai` player. Invalid or repeated mode
values return 400; unknown stream IDs return 404. See `docs/rest_api.md` for the JSON contract.

The backend supports graceful shutdown. Logs contain startup/listen/shutdown and
access status/duration with `[backend]`/`[http]` context. Access logging omits query
strings; configuration errors name keys without exposing their values.

Node is centrally pinned in `docker/Dockerfile`; npm lockfiles are committed.
The backend Docker runtime runs as a non-root user. Local live-view deployment
uses a Vite development server and is intended for a trusted development LAN.

Browser smoke verification is in `tests/integration/web/live_view_smoke.cjs`.
It requires a running stack with live video and Playwright/Chromium installed in
an Ubuntu Docker test container. Set `PLAYWRIGHT_MODULE` to its module path and
`LIVE_VIEW_WEB_URL` to the frontend URL; run the script with Node in that
container. It checks decoded Gốc/AI/Gốc video frames, stale mode cancellation, API retry and component fetch
cancellation on unmount. It is separate from network-free unit checks.
