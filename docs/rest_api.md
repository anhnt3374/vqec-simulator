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

## Temporary live AI line (NestJS)

`POST /api/v1/streams/board43/live-line/session` with `{"session_id":"page_unique_id"}`
starts a page session, clears the previous line and returns HTTP 201. Retrying the same
session is idempotent. Every page load starts a new session, including loads in raw mode.
Only the newest page controls the single shared AI output for this stream.

`PUT /api/v1/streams/board43/live-line` with
`{"session_id":"page_unique_id","line":[[0.1,0.5],[0.9,0.5]]}` saves/replaces the line;
`line: null` deletes it. Returns HTTP 200 with
`{"session_id":"page_unique_id","revision":"opaque_uuid","line":[[0.1,0.5],[0.9,0.5]]}`.
Coordinates must be finite numbers in `[0,1]`; endpoints must differ. Invalid bodies
return 400, unknown streams 404, and writes from superseded sessions 409 (`already_exists`).
Saving confirms acceptance; the worker applies the revision asynchronously between frames.

Forward means moving onto the right side of start → end (top → bottom for a horizontal
line drawn left → right). The UI displays a perpendicular outward arrow and reverses the
endpoints to flip direction. Only confirmed person tracks use bottom-center anchors.
Outward crossings produce `line_crossing.cross` in logs with `direction=forward`, UTC
milliseconds, frame sequence, gate and track ID. They are not persisted/published as events yet.
The box stays red until the track expires. Editing/deleting the line or reconnecting resets
crossing history and box color. Touching the line alone does not count.

`LIVE_LINE_FILE` on backend and worker selects an atomic IPC snapshot on a shared Docker
tmpfs volume. Backend resets it on startup. No SQL, browser storage or durable configuration
is used. Without this optional bridge, the standalone AI worker has no live line configured.

## Temporary live AI zone (NestJS)

The same page session owns one line and one zone. `POST .../live-line/session`
now clears both on a new page load. Line and zone saves are independent.

`PUT /api/v1/streams/board43/live-zone` with
`{"session_id":"page_unique_id","zone":[[0.2,0.2],[0.8,0.2],[0.8,0.8],[0.2,0.8]]}`
saves a polygon; `zone: null` deletes it. The polygon closes implicitly from last
vertex to first. Requires 3..32 distinct finite normalized points forming a simple,
nonzero-area polygon. Concave zones are accepted; self intersections and collinear
polygons return 400. Unknown streams return 404; superseded sessions return 409.

Both save endpoints return `{session_id, revision, line_revision, zone_revision, line, zone}`.
Each feature revision changes only when that feature is saved/cleared; session reset changes
both. This keeps zone edits from resetting line-crossing history and vice versa.
Old line-only IPC snapshots remain readable; missing `zone` means no zone.

Confirmed person bottom-center anchors determine membership. A visible person turns
orange after 5 seconds inside; outside is green.
The worker setting `zone_dwell_seconds` defaults to 5 (validated range 1..3600).
Dwell uses frame PTS; leaving restarts the timer. Red from a prior outward line crossing
takes precedence, and face colors are unchanged. The zone outline is orange. Each new membership logs
`zone_intrusion.enter` with stream, zone, track, frame and UTC milliseconds; staying
inside does not repeat the log. A first confirmed observation inside counts as entry.
Lost tracks retain membership; absent tracks are forgotten. Zone edits/deletes and
source reconnect reset membership and dwell timers. Entry logs remain immediate.
No database/event publication is used yet.

### Recognition and attendance logs (prototype)

`face_recognition.recognized` is currently an AI-worker log, not a REST response or a
persisted event. Fields include employee_id, person_track_id, stream, frame_sequence, pts_ms,
occurred_at_utc_ms, score, optional second_score/gap and confirmation (clear_gap/two_observations).
It occurs on new confirmed employee identity per person track. Cached frames and same-identity
refreshes do not emit again. New track IDs may emit again; this is not daily attendance deduplication.

`attendance.line_crossed` is also a frame event and AI-worker log, with no new REST endpoint,
DB write or external publisher. It is emitted on each `forward` (inside-to-outside) live-line
crossing with an already confirmed employee identity. Fields: `employee_id`, `person_track_id`,
`line_id` (the event's `gate_id`), `direction`, `frame_sequence`, `pts_ms`,
`occurred_at_utc_ms` and stream. The source crossing's gate, track, frame and wall-clock timestamp
are preserved. It accompanies `line_crossing.cross`; merely recognizing a face does not emit
attendance. Unknown crossings are not queued for later recognition; no daily deduplication.

`FrameAnnotations.employee_ids` is an internal owned track-ID-to-employee-code snapshot for
video sinks, not a REST field. Recognized boxes show only the employee code; unknown boxes keep
`person #<track_id>`. Scores are never rendered on any bounding box, irrespective of highlights.
