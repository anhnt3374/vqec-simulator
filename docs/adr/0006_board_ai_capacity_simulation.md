# ADR 0006: Simulated board AI service capacity

Status: accepted (2026-10-07), short design approved in chat.

## Decision

Add optional `simulation.ai_fps` to live AI YAML, default zero (disabled), positive
range 1–1000. A rate of 18 makes the detector pair and person tracking occupy at
least 1/18 second per frame. Actual elapsed inference/tracking consumes this
budget; only the remainder is waited. Work slower than the budget receives no
additional delay. A pure `AiSimulationBudget` helper is covered without sleeps
or real clocks in unit tests.

The coordinator waits using a deadline and condition variable, with bounded
20 ms stop-flag polling so SIGTERM can interrupt artificial delays. Capture stays
independent and continues to push a bounded DropOldest queue for live sources.
Artificial delay ends before publication; encoding/network costs remain real.
The existing PTS admission filter remains separate: use `processing_fps: 0` for
board capacity tests. Output PTS remain source PTS; encoder nominal output FPS
does not duplicate frames. No port/API/dependency changes.

Log actual detector/tracking costs separately from `simulation_wait_ms`; pair
latency includes the delay. Simulation emulates service throughput/latency and
queue pressure, not CPU/GPU/NPU resource utilization. Physical input, inference,
encoding and scheduling can lower achieved FPS below the configured rate.

## Verification

Docker cpu-debug build, unit suite and layer/format checks pass. Timed integration
source at 25 FPS produced 75 frames: 56 processed, 19 capture drops, 17.953 FPS
over 3.119 seconds with simulation set to18. Test detectors isolate pacing from
model performance. Another integration test interrupts a one-second budget at
100 ms and verifies no output after stop. Pure budget tests prove a 30 ms actual
job waits only the remaining ~25.6 ms, a 60 ms job adds no wait, and zero disables.

The local ignored config uses `processing_fps: 0`, `simulation.ai_fps: 18`;
tracked example defaults to simulation disabled. Runtime image rebuilt, original
camera/live-view services remain stopped at the user's request. Live RTSP
verification is skipped while stopped; no GPU/board hardware equivalence claim.
Git metadata remains read-only and no commit is made.
