# ADR 0009: Track identity labels and line attendance

Status: accepted (user requested extension on 2026-10-07).

## Context

Recognition already retains confirmed employee IDs in per-person state. Attendance should
be tied to a physical outward line crossing, and video should identify recognized employees
without displaying detection scores.

## Decision

Keep cached identity in FaceRecognition, retaining it through missing face observations and
short Lost states, clearing it at track expiry or source reset. Snapshot visible confirmed
person identities in the technology-free, owned `FrameAnnotations.employee_ids` map. Renderer
shows the employee code, or the existing person track label for unknown people. Never render
scores on any object label; classification labels and detector scores remain intact for logic.

After the existing live line monitor, attribute each forward line_crossing.cross to the cached
employee and append attendance.line_crossed. Preserve crossing gate, track, frame and UTC time;
add employee_id and source PTS. Log it at info as a business event. Unknown crossings produce
no attendance and are not replayed later. Keep the existing recognition and crossing events.

## Consequences

Recognition is evidence; outward crossing triggers attendance. Repeated outward crossings
produce repeated events. No per-day deduplication, database or external event publisher is
introduced. Zone/line highlight priorities remain independent of employee labels.
