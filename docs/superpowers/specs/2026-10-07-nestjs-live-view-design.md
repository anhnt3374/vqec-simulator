# NestJS live view service

Status: approved written specification (2026-10-07).

Deployment update approved by the user during implementation: publish frontend
on host port 8000 and backend on host port 8001. Internal container ports remain
5173 and 3000 respectively; MediaMTX remains on 8889/8554/8189.

## Purpose and success criteria

Provide the existing React WebUI with a backend API that supplies a browser-playable
live view for board 43. The source is `rtsp://192.168.138.43:18554/live/hd` and the
existing local MediaMTX relay publishes it at the RTSP path `relay`.

Success means the NestJS service runs inside Docker, React obtains its player URL
from the API, and a browser on the host or LAN can view the relayed stream. API
tests, frontend build and live streaming verification are required. Verification
must distinguish receiving RTP, completing WebRTC negotiation and actually
rendering video; unavailable browser verification must be reported explicitly.

## Architecture

Add a TypeScript NestJS project in `backend/`. It owns the live-view HTTP endpoint;
MediaMTX owns video ingestion, WebRTC signaling and media delivery. React consumes
the endpoint and embeds MediaMTX's built-in WebRTC player in its existing iframe.
The C++ core and its port signatures are outside this change.

Data flow:

```text
board43 /live/hd -> MediaMTX relay -> WebRTC -> React iframe
React -> GET /api/v1/streams/board43/live-view -> NestJS -> player metadata
```

An iframe is the selected first implementation because the UI already has one.
Direct WHEP integration with a video element would add connection management.
HLS would require a different playback setup and is deferred.

## API contract

NestJS listens on `0.0.0.0:3000`. The new endpoint is
`GET /api/v1/streams/:stream_id/live-view`; `board43` is the only configured stream.

Successful response (HTTP 200):

```json
{
  "stream_id": "board43",
  "mode": "raw",
  "protocol": "webrtc",
  "player_url": "http://192.168.138.217:8889/relay"
}
```

The response supplies configured playback metadata, not proof that the camera is
online. It must not contain source credentials or a fabricated availability
status. Unknown stream IDs return HTTP 404 with the repository error envelope:

```json
{"error":{"code":"not_found","message":"stream 'unknown' does not exist"}}
```

Also implement the existing documented `GET /api/v1/health` contract returning
`{"status":"ok"}` for process liveness. Unexpected errors use the same error
envelope and HTTP 500, without stack traces in responses.

## Configuration and deployment

Configuration validates once during NestJS startup. Use environment variables
for the listen port, public MediaMTX HTTP base URL and allowed WebUI origins.
The player URL is built from a configured public HTTP(S) base URL and the fixed
relay path, never from arbitrary request headers. Docker-internal hostnames must
not appear in the browser-facing URL. The initial LAN base URL is
`http://192.168.138.217:8889`; changing host address requires configuration updates.

Enable MediaMTX WebRTC, publish HTTP port `8889` and ICE ports required by its
configuration, and advertise `192.168.138.217` as an ICE candidate. Keep the
existing RTSP output on `8554` and input from board 43. Apply deployment changes
to the existing local relay composition so two MediaMTX instances do not contend
for the same ports. Provide tracked example configuration and documented Docker
commands for reproducibility; local configuration remains ignored.

Use Ubuntu 22.04 containers for backend and frontend build/test/run. Add the
necessary Node build/runtime Docker stages; existing C++ dependency pins are
preserved. Node dependencies use committed lockfiles and `npm ci`. Pin Node
installation metadata centrally in `docker/Dockerfile`, in accordance with
repository rules. No database or C++ port changes are required.

## React integration

Add an API client under `web/src/`, use Vite's `/api` development proxy to the
NestJS service and call the endpoint on mount with cancellation on unmount.
The API base can be configured for other deployments. Show an API loading state,
handle failed HTTP requests and invalid playback metadata, and provide retry.
The iframe loads only after a valid player URL is received. The frame uses
autoplay permissions and the MediaMTX player can mute autoplay through its
documented query parameters.

Start on the raw tab and identify the camera as board 43. Until an AI output is
implemented, mark the AI tab unavailable instead of showing raw video as AI.
Do not infer FPS, resolution, latency or inference activity from this endpoint.
Report API errors to the browser console; the documented backend client-error
report endpoint is outside this first API scope.

## Logging, documentation and verification

Use NestJS logging with bracketed backend/HTTP context, startup/listen/shutdown
messages and one access line per request with method, path, status and duration.
Expected rejected IDs log at warn; unexpected failures log once at error. Do not
log credentials, request query secrets or SDP bodies. C++ logging macros apply
to C++ components; no C++ code changes are needed for the service.

Add backend tests for the successful DTO, unknown stream, liveness, invalid
configuration and the public URL configuration. Run tests and compilation in
Docker, build React in Docker, validate Compose and confirm the live endpoint
and MediaMTX player/WHEP routes. Verify actual WebRTC media in a browser when
browser tooling is available. Run the existing required C++ checks in Docker
and report any environment blockers without claiming they passed.

Update `README.md`, `docs/rest_api.md`, relevant architecture documentation and
an ADR describing NestJS as the WebUI API service alongside the C++ processing
core. Preserve the user's existing React migration and other working-tree edits.

## Review and execution

This written specification is ready for user review. After written-spec approval,
create an implementation plan and execute it in this session once the user selects
execution. The original request authorizes deploying the reviewed local service;
no further publication approval is required for that deployment.
