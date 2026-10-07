# ADR 0005: ByteTrack person IDs in live AI output

Status: accepted (2026-10-07), bounded design approved in chat.

## Decision

Implement ByteTrack in the existing tracking adapter directory behind unchanged
`IObjectTracker`, and wire one instance into `vqec_live_ai`. Person detections use
XYAH constant-velocity Kalman prediction, global Hungarian assignment with a
cost limit inside the optimization, IoU/score fusion for high detections, a
second IoU pass for low detections, tentative confirmation, lost-track
reactivation/expiry and duplicate suppression. This is a native C++ algorithm
implementation using the already installed OpenCV core matrix types; it does
not import a tracking library or add dependencies.

Reference algorithm: [official ByteTrack tracker](https://github.com/FoundationVision/ByteTrack/blob/main/yolox/tracker/byte_tracker.py)
and [Kalman filter](https://github.com/FoundationVision/ByteTrack/blob/main/yolox/tracker/kalman_filter.py).
This port adapts prediction dt to source PTS (30 Hz reference time units), uses
elapsed PTS for expiry, and caps retained tracks/detections for bounded work.
These adaptations are intended for configurable processing rates and dropped
live input frames; benchmark parity with the upstream implementation is not claimed.

The coordinator alone owns/calls the tracker. Captured frames carry a private
source session counter, incremented after reconnect; the coordinator resets
tracking and admission when the session changes or source PTS rewinds. Reset
preserves the ID counter within the worker lifetime. IDs restart when the
process restarts, and they do not provide face identity or re-identification.

Overlay displays `person #ID` for observed tracks, including Tentative ones,
and hides Lost prediction boxes. Face detection remains frame-local. The
`tracking` YAML block controls confidence split, matching costs, lost timeout
and maximum tracks. NanoDet's effective confidence floor is the minimum of
person confidence and the tracking low threshold, preserving weak candidates
needed by ByteTrack. No port, REST contract, database or business solution changes.

## Verification and limitations

Tests cover moving-person IDs, confidence dips, temporary loss/reacquisition,
PTS-based expiration at low processing FPS, multiple detections reordered,
weak/face rejection, tentative confirmation, reset without ID reuse, pipeline
IDs and face isolation, PTS rewind reset and overlay IDs/Lost suppression.
Integration uses real NanoDet predictions on the downloaded bus fixture and
checks IDs across frames; it also decodes deployed `/ai` with increasing PTS.
Docker Chromium verifies actual raw/AI/raw playback and request cancellation.

Live camera run on Ryzen 5 7530U, CPU, input 2592×1944 at 25 FPS and output
1280×720: initial processed rate about 24.49–25.00 FPS; tracking 0.03–0.16 ms/frame
in early windows. With more weak detections, square dummy-row assignment reached
about 4 ms/frame. The final rectangular assignment removes these dummy rows.
The final deployed ten-second window measured 24.50 processed FPS, 24.47 output
FPS and 0.04 ms tracking/frame, with one capture drop during integration checks. These are short operational measurements, not a sustained
load or tracking accuracy benchmark. A snapshot may contain no person box when
NanoDet scores do not meet initiation thresholds. Occlusion/crossing/detection
errors can still cause ID changes; face tracking/embedding remain deferred.

Build cpu-debug, all 62 unit tests, all 6 integration tests (no skips with live
RTSP enabled), and layer/format checks pass inside Docker.
No GPU path test and no long-duration disconnect/occlusion soak. Git metadata
remains read-only, so workspace changes are left uncommitted.
