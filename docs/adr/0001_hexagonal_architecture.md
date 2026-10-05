# ADR 0001: Hexagonal architecture

Status: accepted (2026-10-02)

## Context

The simulator must combine many technologies that change independently — video I/O (RTSP, MP4),
inference runtimes (CPU, GPU, future NPU), SQL and vector databases, an HTTP/WebSocket API — with
business logic (zones, gates, per-use-case solutions) that should stay stable and testable.

## Decision

Use ports and adapters:

- `domain` holds pure business types and rules; `ports` defines interfaces the core offers
  (inbound) and needs (outbound); `application` implements use cases against ports only.
- Every technology lives in an adapter that implements a port; only `bootstrap` instantiates
  adapters, based on configuration and compile-time `VQEC_WITH_*` options.
- Inference is split into model codecs (pre/post-processing) and runtime engines behind
  `IInferenceEngine`, so models and runtimes combine freely.
- Dependency rules are enforced mechanically by `scripts/check_layers.sh` and CMake targets.

## Consequences

- Business logic is unit-tested with fakes, without GPUs, cameras or databases.
- New runtimes, codecs and databases are additive: a new adapter, an option, a factory branch.
- More interfaces and DTO conversions at the boundaries; port changes ripple to all adapters
  and fakes, so ports must be designed carefully and changed deliberately.
