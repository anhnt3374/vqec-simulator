# ADR 0007: Temporary live polygon zone

Date: 2026-10-07. Status: accepted. Extends ADR 0006.

## Context

The object filter needs an editable polygon zone alongside the outward-crossing line.
A person inside the zone should be highlighted, with entry logs ready for eventual
persistence. Neither feature should reset the other's tracking state during edits.

## Decision

Keep one zone and one line in the existing active page session and RAM-backed IPC snapshot.
Add PUT /api/v1/streams/board43/live-zone with normalized vertices or null. New page sessions,
page exit and backend startup clear both. Saving/deleting one preserves the other. Independent
line_revision and zone_revision values let the bootstrap watcher queue only changed features.
Old line-only snapshots remain readable; a missing zone means no zone.

Allow 3..32 distinct vertices of a simple nonzero-area polygon, including concave polygons.
Validate numeric coordinates, duplicate vertices, overlapping edges and self intersections in
NestJS before publishing. The worker applies domain make_zone validation to the bounded
snapshot. The UI reuses the video-coordinate editor with vertex dragging, undo-last-point,
implicit polygon closure, save and delete. Drawing is a draft until saved.

Reuse ZoneIntrusionSolution with a person label filter and bottom-center anchors. Each visible
confirmed person inside for at least zone_dwell_seconds (default 5, frame PTS) appears in the owned FrameAnnotations.inside_zone_tracks
snapshot. The FFmpeg renderer draws the zone outline and its people orange. Red outward-line
highlight takes precedence; faces stay pink. Leaving restores green unless red was latched.
Logs record zone_intrusion.enter with stream, zone, track, frame and UTC milliseconds. The
first confirmed observation inside is an entry. Lost tracks retain membership; absent tracks
are forgotten. Leaving resets dwell. Zone edits/deletes and source reconnect clear membership and dwell. Gate edits leave zone
membership unchanged, and zone edits leave gate history unchanged.

## Consequences

No DB migration, dependency or inference runtime change. FrameAnnotations has an additive
membership field and the temporary API/snapshot has additive revision/zone fields. Existing
sinks may ignore membership; the recording fake copies it. Event persistence/publication
remain deferred. One page owns the shared AI stream and opening a newer page resets both
features. The edge convention of the existing polygon contains test is retained; observations
exactly on boundaries are not guaranteed to stay classified on a particular side.
