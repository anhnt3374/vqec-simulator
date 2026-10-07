# ADR 0006: Temporary live AI line control

Date: 2026-10-07. Status: accepted.

## Context

The live-view UI needs an editable outward-crossing line, per-track box highlighting and
crossing logs before database/event persistence is introduced. NestJS serves configuration
while a separate C++ worker owns tracking and encoded annotations. Browser reload must clear
both the line and crossing state. The AI video is shared across viewers.

## Decision

Use one active page session per board43 stream. POST starts an idempotent session; PUT
replaces the line or clears it with null. A newer page supersedes old sessions; stale writes
are rejected. Page load and backend startup clear configuration. No browser storage or DB.

The backend serializes commands and atomically renames a bounded JSON snapshot in a shared
Docker tmpfs volume. The worker mounts it read-only. A joined bootstrap watcher polls every
100 ms, validates coordinates using make_gate, and sends a latest-update mailbox to the
coordinator. There is no file/network I/O on the per-frame pipeline thread. A missing/invalid
snapshot clears the line. The optional LIVE_LINE_FILE enables this bridge without changing
existing standalone worker configuration.

The coordinator uses existing domain gate geometry and the line_crossing event constants.
Confirmed person bottom-center anchors determine forward crossings. Lost tracks retain state;
absent tracks are forgotten. An anchor exactly on the line retains its previous side.
Forward crossings log one line with stream, gate, track, frame and UTC milliseconds. An
owned FrameAnnotations outward_crossed_tracks snapshot carries persistent color state to
the async FFmpeg sink. Faces retain their color and lost prediction boxes stay hidden.
Line edits/deletes and source reconnects reset history. Events are not persisted/published.

The editor compensates for the player's contain/letterbox layout. Default AI aspect ratio
is 16:9, matching the example output. VITE_AI_ASPECT_RATIO must match output.width / output.height
if deploying an AI output with another ratio.

## Consequences

This introduces an explicit additive annotation DTO field and temporary REST contracts.
Existing sinks may ignore the highlight field; the recording fake copies the complete DTO.
Backend/worker restart and page reload never restore a durable line. Multiple tabs share the
same encoded AI stream; the newest page owns its configuration. Acceptance precedes worker
application by up to one poll interval plus in-flight inference/encoding. A future persistent
scene/event service can replace this IPC transport while retaining normalized gate geometry.
