# ADR 0008: Person-owned EdgeFace recognition

Status: accepted (user approved design and implementation plan on 2026-10-07).

## Context

Live attendance needs employee IDs associated with existing person tracks, without face boxes
or employee labels in video. The q5 reference offers size gating, batch budget, top-two gap
and temporal confirmation. Its DSP ArcFace artifact/gallery cannot serve CPU EdgeFace inference.

## Decision

Use original EdgeFace-S gamma 0.5 FP32, pinned checkpoint/export and model-specific gallery.
Export a fixed batch-4 tensor; pad unused rows and preserve ordered per-region errors. Keep
5-point source landmarks in Detection and extend IFeatureExtractor with a technology-free
batch contract. The model codec owns image conversion/alignment; application owns person
association, distinct-employee ranking and confirmation. A joined bounded worker runs embedding;
bootstrap loads gallery once. Employee directory names become log-only attendance identifiers.

Initial policy follows reference: 20×25 px/500 px² minimum, four fresh faces, score 0.41,
gap 0.08, two observations for near ties. These values are configurable, not measured EdgeFace
accuracy. Ambiguous ownership is rejected. Track expiry/reconnect clears recognition history;
confirmed identities refresh and changes require two observations. Existing scene controls remain.

## Consequences

Gallery must be rebuilt for model/preprocessing changes. CPU service time includes a tensor batch
of four even with fewer active faces; no performance claim is made. Recognition logs may recur
on a new track and do not represent daily attendance persistence. Gallery/model binaries are ignored.
