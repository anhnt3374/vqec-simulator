# NestJS Live View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for native execution, or superpowers:subagent-driven-development if the user selects delegation. Steps use checkbox syntax for tracking.

**Goal:** Run a Docker-hosted NestJS API and connect the existing React live view to board 43 through MediaMTX WebRTC.

**Architecture:** NestJS returns playback metadata. MediaMTX pulls board 43 HD and serves its built-in WebRTC player. React obtains the player URL from NestJS and embeds it in its existing iframe.

**Tech Stack:** TypeScript, NestJS, Node.js, React 18, Vite, MediaMTX 1.21.1, Ubuntu 22.04 Docker containers.

**Spec:** `docs/superpowers/specs/2026-10-07-nestjs-live-view-design.md` (approved 2026-10-07).

## Global Constraints

- Build, test and run only in Ubuntu 22.04 Docker containers.
- Preserve user changes to `AGENTS.md` and the React migration; do not delete their files.
- API prefix `/api/v1`, JSON fields `snake_case`, repository error envelope.
- Source `rtsp://192.168.138.43:18554/live/hd`; MediaMTX path `relay`.
- Initial public player base `http://192.168.138.217:8889`; NestJS listen `0.0.0.0:3000`.
- Validate configuration once at startup; do not derive public URLs from request headers.
- Preserve C++ ports and existing dependency versions; pin Node installation in `docker/Dockerfile`, npm dependencies in lockfiles.
- Commit only task changes if committing; exclude local configs, build output and dependencies.

## Review Focus

- A LAN browser must receive a public URL and reachable ICE candidate, not a container hostname.
- A stopped board must not make a metadata response falsely claim that video is playing.
- Invalid configuration must fail startup and name the offending key.
- Unknown stream IDs must return 404 using the established error envelope.
- Unmounts/retries must not let stale API results overwrite a newer player or leave requests pending.

## File Map

- `backend/package.json`, `package-lock.json`, `tsconfig.json`, `.gitignore`: reproducible TypeScript/Nest project and build/test scripts.
- `backend/src/config.ts`: environment parsing and public URL validation.
- `backend/src/live_view.service.ts`: configured stream lookup and DTO.
- `backend/src/live_view.controller.ts`, `health.controller.ts`, `app.module.ts`: HTTP routes and wiring.
- `backend/src/http_error.filter.ts`, `http_logging.interceptor.ts`: uniform errors and access logs.
- `backend/src/main.ts`: startup configuration, listen and shutdown.
- `backend/test/config.test.ts`, `live_view.test.ts`, `http.test.ts`: built-in Node test runner, compiled with TypeScript.
- `docker/Dockerfile`: separate Ubuntu Node/backend/frontend stages with central Node version/checksum pins.
- `docker/compose.live_view.yaml`: NestJS, React dev server and MediaMTX live-view stack.
- `config/mediamtx.live_view.example.yaml`: tracked generic relay configuration using environment substitutions.
- `config/mediamtx.board43.yaml`, `config/relay.board43.yaml`: ignored working deployment configuration, extend the running relay.
- `web/src/api_client.js`, `api_client.test.js`: API request, response validation and abort support.
- `web/src/LiveView.jsx`: API loading, retry/error state and iframe.
- `web/src/App.jsx`, `index.css`, `web/vite.config.js`, `web/package.json`: connect raw tab and development proxy, add scoped styling and test script.
- `README.md`, `docs/rest_api.md`, `docs/architecture.md`, `docs/adr/0003_nestjs_web_api.md`: API, deployment and architecture decision.

### Task 1: NestJS playback metadata API

**Interfaces:** `load_config(env: NodeJS.ProcessEnv): AppConfig`; `LiveViewService.get_live_view(stream_id: string): LiveViewDto`. DTO fields are `stream_id`, `mode`, `protocol`, `player_url`. `AppConfig` contains `port`, `player_base_url`, `cors_origins`. A creation helper `create_app(config: AppConfig)` creates a Nest application without listening, for HTTP tests and bootstrap.

- [x] Add Ubuntu Node Docker stage, pin a supported Node release and verified checksum, and add minimal package/TypeScript scaffold. Generate the npm lockfile in Docker. Dependencies are Nest common/core/platform-express, reflect-metadata and rxjs; build dependencies are TypeScript and Node typings.
- [x] Write config/service tests: default port is 3000; valid public base returns `http://192.168.138.217:8889/relay`; HTTPS and trailing slash work; zero/invalid ports, malformed/non-HTTP URLs, URL credentials/query/fragment and empty required public base are rejected with the key in the message. Unknown IDs throw a not-found error. Metadata has no availability assertion or source URI.
- [x] Run `npm test` in the Node development container and verify failures are caused by missing implementation.
- [x] Implement `load_config` and service. Use Nest built-in exceptions and injection; startup reads environment once. Restrict CORS to configured origins.
- [x] Write HTTP tests using `node:test`, built-in fetch and an ephemeral application listener: successful board43 DTO; health status; unknown stream 404 envelope; unsupported route envelope; configured CORS behavior; hostile Host header does not change the public URL.
- [x] Verify those HTTP tests fail before routes/filter are implemented.
- [x] Implement module, controllers, app creation, error filter, access logging and bootstrap. Debug access lines carry duration; input rejection warns; unexpected failures log once. Enable graceful shutdown.
- [x] Run backend tests and TypeScript build inside Docker; review the complete API diff.

### Task 2: Browser-capable MediaMTX deployment

**Interfaces:** configured HTTP player `http://192.168.138.217:8889/relay`, WHEP endpoint `/relay/whep`; existing RTSP `rtsp://192.168.138.217:8554/relay` remains available.

- [x] Add backend/frontend Docker stages and standalone `docker/compose.live_view.yaml`, with tracked example relay config and explicit deployment environment variables. Use the backend dependency lockfile and frontend's existing lockfile. No C++ runtime dependency changes.
- [x] Enable WebRTC in local board43 configuration, publish port 8889 and configured UDP/TCP ICE listeners, advertise the host LAN IP. Disable unneeded protocols. Define equivalent tracked example settings with deployment-specific values passed through environment.
- [x] Validate tracked Compose with supplied environment using `docker compose ... config`. Validate the local stack composition and check image build targets and port allocation before replacing the relay container.
- [x] Extend the existing local relay composition with backend and React services, avoiding a second MediaMTX competing for port 8554. Build and start the local stack; report any temporary relay restart.
- [x] Check container logs, API health and DTO, HTTP player response and WHEP negotiation. Recheck RTSP packets. Repeat with a LAN public URL to ensure no localhost/container-only address leaks into the API response.

### Task 3: React API integration

**Interfaces:** `fetch_live_view({signal}: {signal?: AbortSignal}): Promise<LiveViewDto>` requests `/api/v1/streams/board43/live-view`. `LiveView` renders loading, API failure/retry or iframe. API metadata success must not be labeled media-playing success.

- [x] Add Node built-in tests for the API client using injected/mock fetch: valid DTO, non-200 response, invalid protocol/URL/stream/mode, aborted request and a successful retry. These tests run inside the frontend Ubuntu container.
- [x] Run tests and confirm expected failures before implementation.
- [x] Implement the API client with HTTP error handling and strict DTO validation; forward AbortSignal. Do not log response secrets.
- [x] Implement `LiveView` with an AbortController per request and effect cleanup that ignores stale results. Show a retry button after API errors. Embed the configured HTTP(S) player and request muted autoplay with MediaMTX-supported query parameters.
- [x] Replace only the existing live-view placeholder with `LiveView`. Make raw the default tab, disable unavailable AI view and label the camera board43. Remove misleading inference/FPS/latency claims in touched live-view status labels. Preserve the user's layout and unrelated styling.
- [x] Configure Vite proxy target using deployment configuration and bind the Docker dev server on port 5173. Add the frontend test script without replacing the existing package setup.
- [x] Run API-client tests and `npm run build` in Docker. Verify browser loading, retry and raw/AI behavior with browser tooling if available; explicitly report if browser rendering cannot be verified.

### Task 4: Documentation and final verification

**Interfaces:** documented commands must start the reviewed local stack and reproduce the response consumed by React.

- [x] Update REST documentation with the new endpoint, DTO, 404 and metadata semantics. Document health as implemented by NestJS.
- [x] Add ADR 0003 and architecture notes defining NestJS as the WebUI HTTP service alongside the C++ processing core. Explain Docker build stages and MediaMTX media flow.
- [x] Update README/backend README with configure, start, test, stop and logs commands, URLs 3000/5173/8889/8554, public host/ICE configuration, raw-only scope and browser troubleshooting.
- [x] Re-run backend tests/build and frontend tests/build after final changes, check Compose validation and `git diff --check` for task files. Do not modify unrelated pre-existing whitespace issues.
- [x] Run `scripts/dev.sh cmake --preset cpu-debug`, build, `ctest` and `scripts/check.sh`. Report environment/dependency failures explicitly; do not claim blocked checks passed.
- [x] Verify fresh local API, player/WHEP and RTSP media. Check working-tree diff for unrelated changes and missing ignored runtime configuration documentation.
- [x] Review the final implementation against the spec, especially LAN ICE addresses, errors and abort cleanup. Leave the requested stack running and report the actual WebUI URL and verification limits.

## Execution Selection

This plan is ready for user review. Native execution in this session is recommended:
the four tasks share configuration and API interfaces, so sequential implementation
avoids coordination cost. Delegation is optional and requires explicit selection.

## Verified outcome (2026-10-07)

Frontend runs at `http://192.168.138.217:8000`, backend is published on `8001`,
and MediaMTX serves `/relay` on `8889` (RTSP on `8554`). Backend27/27,
frontend10/10, C++36/36 tests passed in Docker; CPU build, layer/format checks
and React production build passed. Chromium confirmed2592x1944 video,
API error/retry and component unmount cancellation. GPU was not built.
The user-approved8000/8001 publications supersede the initial plan ports.
Code remains in the working tree; no merge/push/commit was requested.
