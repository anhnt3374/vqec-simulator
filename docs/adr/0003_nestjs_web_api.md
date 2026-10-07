# ADR 0003: NestJS WebUI API and raw live view

Status: accepted (2026-10-07).

## Context

The WebUI has migrated to React/Vite. The user requested a NestJS backend with a
first API that connects live view. The C++ processing adapters are scaffolded;
board 43 already serves H.264 at `/live/hd` and MediaMTX relays it locally.

## Decision

Add `backend/` as a separate NestJS service. It exposes `/api/v1/health` and
`/api/v1/streams/:stream_id/live-view` with repository snake_case JSON conventions
and the established error envelope. The configured stream ID is `board43`.

NestJS returns an HTTP WebRTC player URL configured independently of request
headers. MediaMTX pulls RTSP and handles signaling/media delivery. React embeds
MediaMTX's built-in player; no video passes through NestJS. Initial playback is
raw only, with AI view disabled until a processed stream exists.

Build/test/run use Ubuntu 22.04 Docker containers. Node version/checksum are
pinned in `docker/Dockerfile`; npm dependencies are locked. The API service runs
as non-root. A standalone Compose and example MediaMTX config reproduce local
development without needing the incomplete C++ runtime.

## Consequences

NestJS owns the implemented WebUI HTTP routes. The C++ core continues to own
business processing and its inward dependency rules; no C++ ports change.
ADR 0002's Drogon/static-HTML choice is superseded for this WebUI API deployment,
while the existing C++ HTTP adapter directories remain available for future work.

The public player host and ICE candidates must match the machine's reachable LAN
address. Live metadata does not assert camera availability. Local deployment uses
the Vite development server; HTTPS/public deployment requires a production web
server, HTTPS media signaling and appropriate network configuration.
