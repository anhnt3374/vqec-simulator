# Live person-owned face recognition with EdgeFace

Status: approved by the user; implemented and independently reviewed on 2026-10-07.

## Outcome

Recognize employees from the live AI stream using EdgeFace-S gamma 0.5, attach identity to
person track IDs, and log attendance recognition. Keep person boxes and line/zone colors;
remove face boxes and employee names from video overlays. Build a new local gallery from
`/home/anhnt5/Pictures/employees`; directory names such as TECH0000 are employee IDs.
No database, REST endpoint, attendance portal, or calendar/day deduplication is added.

## Sources and model evidence

- Official model: https://github.com/otroshi/edgeface, `edgeface_s_gamma_05`.
- Official checkpoint source already recorded locally:
  https://huggingface.co/Idiap/EdgeFace-S-GAMMA/resolve/a1c171036159e94bd4c766088370a37d0899d31f/edgeface_s_gamma_05.pt
- Checkpoint SHA256: dc59abda2e8580399fd115a1eeb07e1f21156196db604b884407bcf0f17efb07.
- Local export: `../qualcomm_quantize/edgeface_sdk228/artifacts/models/original_fp32.onnx`.
  Export metadata reports comparison to the original PyTorch variant on three inputs;
  the separate QuickGELU/QNN candidate changes the function and is excluded.
- Docker probe using the simulator's ONNX Runtime confirmed input [1,3,112,112], output
  [1,512], and finite output for batch 1. Batch 4 was rejected by the fixed input shape.
- Reference policy lives in `../vqec_camera_demo_q5/apps/edge-camera/backend/bootstrap/ai_worker/`:
  `runtime/FaceFrameProcessor.cpp` and `models/FaceIdentityCache.cpp`.

The reference's minimum source face is width 20 px, height 25 px, area 500 px²;
its fresh embedding budget is 4, match threshold 0.41, and top-two identity gap 0.08.
These are configurable initial policy values, not an accuracy claim for EdgeFace.

## Model preparation and preprocessing

Prepare an FP32 ONNX artifact from the pinned original checkpoint/source with batch support
for 1..4 faces. Verify all batch sizes on the target Docker ONNX Runtime and compare each
sample against batch-1 output. Do not label four sequential calls as a tensor batch.
If export cannot support the batch contract, resolve that failure before enabling recognition.
Keep preparation reproducible, record artifact SHA256 and model ID
`edgeface_s_gamma_05_v1` in the manifest. Model binaries remain ignored.

Use source-frame five-point landmarks for a 112×112 canonical face alignment. Reject malformed,
nonfinite, degenerate or badly aligned/incomplete crops. RGB NCHW float32 preprocessing is
(pixel / 255 - 0.5) / 0.5, followed by finite nonzero 512-vector validation and L2 normalization.
Use the exact same detector, alignment, model and normalization for gallery and live.
Do not compare to the reference project's ArcFace gallery.

## Contracts and layering

- Extend domain Detection with optional five-point pixel-coordinate landmarks; existing
  detectors/aggregates retain default behavior. YuNet currently validates landmark heads but
  does not expose landmarks, so decoding must preserve them in original-frame coordinates.
- Extend the feature extractor port with a plain region-plus-landmarks batch DTO and ordered
  embedding results. Document ownership, thread safety, synchronous blocking inference,
  result ordering, batch bounds and InvalidArgument/Internal failures. Update every adapter,
  fake and relevant contract documentation in the same change. No cv/Ort/JSON types cross it.
- Add an EdgeFace model codec in the inference adapter family using IInferenceEngine.
  Landmark crop/alignment and image conversion stay within that family.
- Keep matching, person ownership, admission and confirmation deterministic in domain/application.
  Store recognition state keyed by person TrackId, without changing tracker identity or adding
  employee labels to Track/video. No independently tracked face is required initially.
- Bootstrap loads config/gallery/model once and wires adapters. Gallery parsing and filesystem
  work never occur on the per-frame coordinator thread.

## Live flow and ownership

1. Retain parallel person detection and face detection on each admitted source frame.
2. Update the existing person tracker; associate eligible faces with visible Confirmed person
   boxes from the same frame. Use head-region geometry and reject ambiguous assignments.
   Enforce at most one selected face per person and one person per face. Unowned faces do not
   produce attendance logs.
3. Admit source faces only above the configured size gate and with valid alignment. Select
   at most four fresh embeddings using source area/confidence and deferred-track priority,
   so crowded scenes do not permanently starve one track. Do not wait for four faces to fill.
4. Execute the selected tensor batch on a joined worker through bounded queues, retaining
   immutable frame ownership and source session/sequence IDs. Coordinator results correspond
   to that frame/session; reconnect or stale results cannot attach identity to another track.
5. Match normalized embeddings to the startup-loaded gallery in memory. Rank distinct employee
   IDs, not individual photos, using each employee's best sample score. Multiple photos of
   the same employee must not become the runner-up identity.
6. Confirm a qualifying best employee immediately if top-two gap >= 0.08 (or no other employee).
   Otherwise require that same employee to win two successive eligible observations for that
   person, both meeting the score threshold and having unambiguous person ownership. An unknown
   result, different candidate, or observed gap in face ownership resets pending verification.
   A budget-deferred face does not count as a hit. In a two-frame path, retain fair retry priority.
7. Keep confirmed identities per person track and use a configurable refresh interval rather
   than embedding them on every frame. A proposed identity change requires two consistent
   fresh observations; never output a newly conflicting candidate after one frame.
   Suppress evidence that no longer meets ownership/recognition gates until reconfirmed.
8. Lost person tracks retain cached state but do not emit logs or render prediction boxes.
   Absent/expired tracks and reconnect clear state. New IDs cannot inherit an old employee.

## Attendance logs and overlays

Log `face_recognition.recognized` once when an employee is first confirmed on a person track,
with stream, employee_id, person_track_id, frame, source PTS and UTC timestamp. Include score,
runner-up score/gap and confirmation path when available; never print embedding values,
images or source credentials. Do not repeat for every cached frame or refresh. A confirmed
identity change may emit a new recognition for that employee; a new person track may emit
again. This is a recognition log suitable for later attendance events, not daily attendance
persistence. Counters aggregate attempted/skipped/deferred/failed/recognized work into stats.

Do not add face annotations to the FFmpeg overlay. Person IDs, existing line/zone colors,
zone dwell and web editor behavior remain intact. Update UI wording to remove the implication
that face boxes are displayed. Recognition failures should not stop person detection or line/zone
analytics; invalid enabled startup configuration/model/gallery fails with named-key diagnostics.

## Gallery build

Provide a Docker-run tool/command that reads the employees directory through a read-only mount,
uses the same YuNet/EdgeFace preprocessing, and writes an ignored local gallery under `data/`.
Gallery stores model ID, dimension, preprocessing version, source/model hashes, employee IDs,
and normalized samples (up to 12 per employee, matching the reference budget). Stable input
ordering makes rebuilds reproducible. Validate identifiers, dimensions, finite values and
normalization before accepting a gallery. Save atomically only after successful validation.

Skip unreadable/no-face/tiny-face/invalid-alignment images with actionable aggregate reporting;
reject images with multiple faces rather than enrolling a guessed identity. Report total
employees, usable samples, skipped images, and employees without usable images. Do not claim
complete coverage if employees are skipped, and do not silently reuse an incompatible gallery.
Actually run this builder against the supplied employee photos after implementation.

## Configuration and validation

Recognition defaults disabled for compatibility. Enabled config supplies pinned embedding model,
gallery path and validated policy: source minimum width/height/area, max fresh batch 4, score
threshold 0.41, distinct-identity gap 0.08, two-observation confirmation, refresh interval and
bounded tracked/pending capacity. Initial refresh interval is 2 seconds of source PTS,
maximum tracked identities 512, maximum pending faces 4 and worker queue capacity 2.
Validate source size bounds against positive source dimensions, batch budget in 1..4,
threshold/gap in [0,1], refresh interval in 0.1..60 seconds and capacities in 1..4096.
Defaults live beside typed structs; bootstrap validates once.
Preserve pinned dependency versions and CPU runtime. No new production dependency is assumed;
model export tooling runs in Docker with reproducible versions separate from runtime.

## Verification and acceptance

- Unit tests cover landmark scale/order and missing defaults; alignment rejection; embedding
  normalization/shape; person-face ambiguity and one-to-one ownership; source-size boundaries;
  batch budget/fairness; identity-vs-photo ranking; gap boundaries; two-observation interruption;
  refresh/conflict handling; once-per-track logging; expiry and reconnect.
- Adapter/integration tests run the real EdgeFace artifact in Docker for batches 1,2,3,4,
  compare to independent batch-1 outputs, and test finite 512-vector output. A recording fake
  verifies source frame ownership and embedding result ordering/session matching.
- Gallery build is actually executed on `~/Pictures/employees`; report coverage and skipped
  employees without committing gallery or photos. Validate gallery reload and deterministic
  matching, avoiding treating enrollment-image self-match as measured live accuracy.
- CPU debug build without warnings, all CTest suites, scripts/check.sh, and appropriate web
  tests/build pass. RTSP/live camera validation is reported separately from synthetic tests.
- Add an ADR and update model/config/architecture/recipe/run instructions with the feature.

## Explicit exclusions

No NPU/QNN clone, face-box visualization, employee bbox label, database changes, portal/API,
face attributes, gallery UI, or accuracy/real-time claims without measurement. Code from the
reference is adapted to simulator contracts and C++17, not copied across architectural layers.
