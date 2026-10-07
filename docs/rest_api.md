# REST and WebSocket API

Status: NestJS implements health and raw/AI live-view metadata. Other endpoints below
remain draft contracts for the C++ processing service. Change this document before changing code.

## Conventions

- Base path `/api/v1`; WebSocket under `/ws/v1`. Breaking changes require `/api/v2`.
- JSON bodies, `snake_case` fields, UTF-8. Timestamps are RFC 3339 UTC strings
  (`2026-10-02T09:30:00.123Z`). Coordinates are normalized `[0, 1]`.
- Ids are lowercase snake_case chosen by the user (`cam_entrance`, `restricted_area`).
- Lists return `{ "items": [...], "next_cursor": "..." }` when paginated; `limit` ≤ 1000.
- Source URIs are always returned redacted (`rtsp://***@host/path`).
- Errors use one shape and map from `ErrorCode`:

```json
{ "error": { "code": "not_found", "message": "stream 'cam_x' does not exist" } }
```

| ErrorCode | HTTP |
| --- | --- |
| `invalid_argument` | 400 |
| `not_found` | 404 |
| `already_exists` | 409 |
| `unavailable` | 503 |
| `timeout` | 504 |
| `unsupported` | 501 |
| `internal` | 500 |

## Endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/api/v1/health` | Liveness: `{ "status": "ok" }` |
| GET | `/api/v1/system/info` | `version`, `runtime`, `backend`, compiled backends, solution types |
| GET | `/api/v1/streams` | `{ "streams": [ { "id", "state", "fps", "frames_processed", "last_error" } ] }` |
| GET | `/api/v1/streams/{stream_id}/live-view` | NestJS: configured browser player metadata; currently only `board43` |
| POST | `/api/v1/streams/{id}/start` | Start a configured stream |
| POST | `/api/v1/streams/{id}/stop` | Stop a stream |
| GET | `/api/v1/streams/{id}/scene` | `{ "zones": [ { "id", "name", "polygon": [[x,y],...] } ], "gates": [ { "id", "name", "line": [[x,y],[x,y]] } ] }` |
| PUT | `/api/v1/streams/{id}/scene` | Replace zones and gates (validated, persisted, applied live) |
| GET | `/api/v1/events` | Query: `stream_id`, `type`, `from`, `to`, `limit`, `cursor` |
| GET | `/api/v1/events/{id}/snapshot` | JPEG snapshot of the event frame |
| GET | `/api/v1/metrics` | Per-stream counters (fps, stage latency, drops) |
| POST | `/api/v1/client-errors` | WebUI reports JavaScript errors (logged at `warn`) |
| WS | `/ws/v1/events` | Live events, one JSON object per message |

## Live-view metadata (implemented by NestJS)

`GET /api/v1/streams/board43/live-view` returns HTTP 200:

```json
{
  "stream_id": "board43",
  "mode": "raw",
  "protocol": "webrtc",
  "player_url": "http://192.168.138.217:8889/relay"
}
```

`player_url` uses the configured public MediaMTX HTTP(S) base and can be embedded
as an iframe with autoplay permission. It is independent of request Host headers.
It does not return a source URI, credentials, FPS or camera availability. A 200
response confirms the configuration exists; it does not confirm media delivery.
Player connection errors are displayed by MediaMTX's built-in player.

The optional query `mode=raw|ai` selects the output. Omitted mode defaults to
`raw` and preserves the response above. `?mode=ai` returns `mode: "ai"` and
`player_url: "http://192.168.138.217:8889/ai"`; protocol remains `webrtc`.
Unknown or repeated mode values return HTTP 400 with `invalid_argument`.
AI metadata does not guarantee that the AI worker is publishing video.

An unknown stream returns HTTP 404:

```json
{"error":{"code":"not_found","message":"stream 'unknown' does not exist"}}
```

`GET /api/v1/health` on NestJS returns `{"status":"ok"}` for process liveness.
Unexpected errors return HTTP 500 using `internal` and a generic message.

## Event object

```json
{
  "id": "01J9Z...",
  "type": "line_crossing.cross",
  "stream_id": "cam_entrance",
  "occurred_at": "2026-10-02T09:30:00.123Z",
  "frame_sequence": 1234,
  "track_id": 42,
  "zone_id": null,
  "gate_id": "main_door",
  "attributes": { "label": "person", "direction": "forward" },
  "snapshot_url": "/api/v1/events/01J9Z.../snapshot"
}
```

## Event types

| Type | Raised when | Attributes |
| --- | --- | --- |
| `zone_intrusion.enter` | A confirmed track's anchor enters a zone | `label` |
| `line_crossing.cross` | A confirmed track crosses a gate | `label`, `direction` (`forward` = onto the right-hand side of the gate arrow) |
