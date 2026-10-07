# Live Face Recognition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for native execution, or superpowers:subagent-driven-development if the user chooses delegated execution. Execute task-by-task with checkbox tracking.

**Goal:** Build and run an EdgeFace employee gallery, recognize employee IDs on person tracks, and log recognition without drawing faces or employee labels.

**Architecture:** YuNet exposes pixel-space landmarks; an inference codec aligns crops and executes a genuine tensor batch. Deterministic matching and person-owned recognition state remain inward of ports. Bootstrap loads validated configuration and gallery once; a joined bounded embedding worker is wired into LiveAiPipeline.

**Tech Stack:** C++17, GCC 11, CMake/Ninja, GoogleTest, existing OpenCV/ONNX Runtime, YAML/JSON parsing in bootstrap, Docker-only build/test/run. Pinned export tooling is separate from the production image.

**Spec:** `docs/superpowers/specs/2026-10-07-live-face-recognition-design.md`

## Global Constraints

- All build/test/run commands execute inside Docker; no production dependency/version change without approval.
- Keep domain pure; only bootstrap wires adapters and reads config/environment; no technology types cross ports.
- Face source gate: width 20 px, height 25 px, area 500 px²; fresh batch budget 4.
- RGB NCHW 112×112, normalization (pixel / 255 - 0.5) / 0.5; finite nonzero L2-normalized 512-vectors.
- Model ID edgeface_s_gamma_05_v1; same model/alignment in gallery and live; ignore generated binaries/data.
- Match score 0.41 and distinct-identity gap 0.08 are configurable initial values, not measured EdgeFace accuracy.
- Refresh 2 s PTS; max identities 512; max pending faces 4; worker queue capacity 2.
- Log face_recognition.recognized once per newly confirmed identity/track; no embeddings/images in logs.
- Existing line/zone behavior remains covered; remove face visualization and employee bbox labels.
- Do not commit automatically or restart the user's running camera stack as part of verification.

## Review Focus

- Several gallery photos of one employee must not masquerade as the top-two different identities (Task 2).
- An ambiguous overlapping person assignment must not attribute attendance to either person (Task 2).
- A missing/unknown face between two weak matches must reset pending confirmation (Tasks 2, 4).
- One invalid crop in a batch must not silently shift embedding-to-person indices (Tasks 1, 4).
- Shutdown/reconnect during embedding must release frame ownership, join workers and discard stale results (Task 4).

## Task 1: Landmark contract, EdgeFace codec and real batch artifact

**Files:** modify `src/domain/vision/detection.hpp`, `src/ports/outbound/feature_extractor.hpp`, `src/adapters/outbound/inference/models/yunet_detector.cpp`, model CMake, `tests/CMakeLists.txt`, `models/manifest.yaml`; create `src/adapters/outbound/inference/models/edgeface_extractor.{hpp,cpp}`, `tools/prepare_edgeface.py`, `tests/unit/adapters/outbound/inference/models/edgeface_extractor_test.cpp`, `tests/integration/inference/edgeface_model_test.cpp`, `tests/fakes/fake_feature_extractor.hpp`.

**Interfaces:** optional `std::array<domain::Point2f,5>` landmarks appended to Detection; `ports::FeatureRegion {BoxF box; optional<array<Point2f,5>> landmarks;}`; `using EmbeddingBatch = vector<Result<domain::Embedding>>`; `IFeatureExtractor::extract_batch(const Frame&, const vector<FeatureRegion>&) -> Result<EmbeddingBatch>`. Preserve single-region extract signature; EdgeFace requires landmarks and returns InvalidArgument without them. `EdgeFaceExtractor(unique_ptr<IInferenceEngine>)` implements both methods and serializes engine access.

- [x] Write tests for default missing landmarks, YuNet source scaling/order, synthetic canonical alignment, malformed/crossed landmarks, crop bounds, RGB normalization, nonfinite/zero/output-dimension errors and ordered per-region failures.
- [x] Register and run failing tests in Docker; missing codec/contract is the expected initial failure.
- [x] Implement contract/fake, preserve YuNet landmarks, and implement codec with source-space similarity alignment and validation. Canonical points: (38.2946,51.6963), (73.5318,51.5014), (56.0252,71.7366), (41.5493,92.3655), (70.7299,92.2041); reject residual >12 px, invalid scale or incomplete source crop.
- [x] Prepare original FP32 EdgeFace from pinned source/checkpoint in Docker. Prefer fixed tensor batch 4: pad unused rows for 1..3 valid faces and discard their outputs, so no engine dynamic-shape change is necessary. Keep batch-1 original as validation reference. The codec accepts 1..4 faces without waiting to fill four; this realizes the spec's 1..4-face contract.
- [x] Record preparation versions/hashes and model metadata; run real batches 1,2,3,4 through the codec and compare ordered normalized outputs to independent batch-1 reference, tolerance 2e-4. If export tooling is absent, use a separate pinned Docker export target, not host execution or production library upgrades.
- [x] Run codec/YuNet unit tests and real model integration tests in Docker; require finite nonzero 512 outputs and actual batch-4 tensor execution.

## Task 2: Gallery ranking and person-owned recognition policy

**Files:** create `src/domain/vision/face_gallery.{hpp,cpp}`, `src/domain/vision/face_association.{hpp,cpp}`, `src/application/recognition/face_recognition.{hpp,cpp}`, matching unit tests under domain/application; modify domain/application/tests CMake.

**Interfaces:** `domain::FaceGallerySample {string employee_id; Embedding embedding;}`; `domain::FaceMatch {string employee_id; float score; optional<float> second_score;}`; `match_face(const vector<FaceGallerySample>&, const Embedding&) -> Result<FaceMatch>`; `associate_faces(const vector<Track>&, const vector<Detection>&) -> vector<optional<TrackId>>` in detection order. `app::FaceRecognitionOptions` owns spec defaults; `FaceRecognition(options, vector<FaceGallerySample>)`, `select(const Frame&, const vector<Track>&, const vector<Detection>&) -> vector<FaceRecognitionRequest>`, `apply(const Frame&, const vector<FaceRecognitionRequest>&, const ports::EmbeddingBatch&) -> vector<domain::Event>`, `reset()`. Request contains person TrackId and FeatureRegion; state is owned by this class, not tracker/renderer.

- [x] Write and run failing tests for distinct-employee top-two ranking, model/dimension mismatches, source-size boundaries, overlap ambiguity, one-face/one-person assignment, gap >=0.08 immediate confirmation and below-gap two-observation confirmation.
- [x] Implement geometry ownership: face center in visible Confirmed person's head region (upper half), reject competing plausible people and multiple competing faces for one person. Tentative/Lost tracks cannot own new evidence.
- [x] Implement finite normalized gallery matching with each employee's maximum sample score and deterministic ties. Missing runner-up permits one qualifying observation; near tie needs same candidate twice.
- [x] Implement bounded state and deferred priority before area×confidence ordering; refresh confirmed tracks after 2 s PTS, require two consistent observations to switch identity, and emit only on newly confirmed employee/track. Reset verification on unknown, ownership gaps or different candidates; defer is not a hit.
- [x] Test starvation with >4 eligible tracks, interrupted votes, identity changes, repeated refresh without duplicate logs, Lost retention, expiry, capacity and reset. Require all focused tests pass before wiring live.

## Task 3: Gallery build and validated bootstrap configuration

**Files:** create `src/bootstrap/face_gallery_io.{hpp,cpp}`, `src/bootstrap/face_gallery_main.cpp`, gallery I/O and enrollment tests; modify `src/bootstrap/live_ai_config.{hpp,cpp}`, bootstrap/tests CMake and `config/live_ai.example.yaml`.

**Interfaces:** `load_face_gallery(const filesystem::path&, string_view expected_model_id) -> Result<vector<FaceGallerySample>>`; `write_face_gallery(...) -> Status` with atomic replacement. `LiveAiConfig` gains disabled-by-default recognition settings/model/gallery paths and FaceRecognitionOptions. New `vqec_face_gallery` executable reads `--images`, `--output`, `--models-dir`; uses YuNet/EdgeFace codec from Task 1.

- [x] Write failing tests for wrong model/version/dimension, nonfinite/non-normalized vectors, invalid employee IDs, duplicate metadata, missing input and corrupt gallery; config tests assert all defaults/bounds from the spec.
- [x] Implement bootstrap-only schema-versioned gallery serialization including model/preprocessing/hash metadata, employee ID and up to 12 samples per employee. Load immutable gallery once, never per frame.
- [x] Implement stable employee/image traversal, read images with existing OpenCV imgcodecs, require exactly one usable face, reject invalid alignment, and save only a validated nonempty gallery atomically. Preserve an existing output on failure.
- [x] Validate widths/heights/area >0, budget 1..4, score/gap [0,1], refresh 0.1..60 s, capacities 1..4096; enabled model/gallery errors name offending keys without dumping data.
- [x] Test small generated image fixtures and serialization in Docker, including skip summaries and employee-with-no-usable-image reporting. Run the actual builder on a read-only mount of `/home/anhnt5/Pictures/employees`; output to ignored `data/`, report coverage and skipped employees.

## Task 4: Live embedding worker, event logs and person-only video

**Files:** modify `src/application/pipeline/live_ai_pipeline.{hpp,cpp}`, `src/bootstrap/live_ai_main.cpp`, `src/adapters/outbound/media/ffmpeg/overlay_renderer.cpp`, pipeline/renderer/config tests and `web/src/App.jsx`; create a small worker helper under `src/application/recognition/` if necessary to keep lifecycle separate.

**Interfaces:** append optional recognition options to LiveAiOptions and optional extractor/gallery inputs to LiveAiPipeline construction, preserving existing callers' defaults. Embedding jobs/results carry source session, frame sequence, immutable Frame, ordered request list and batch results. Bootstrap wires model codec/config/loaded gallery.

- [x] Write failing pipeline tests using fake extractor for once-per-track employee events, per-region failure ordering, missing face interrupting weak votes, reconnect reset, model failure degradation and bounded/joined shutdown. Adjust prior face-overlay expectations to person-only output.
- [x] Implement joined bounded embedding worker with exception-to-Status conversion; run inference off coordinator, match ordered results to exact frame/session and reject stale results. Never hold locks across calls or I/O.
- [x] Apply recognition alongside line/zone monitors; emit info event log with employee_id, person_track_id, score/gap, confirmation path, frame/PTS and UTC ms. Aggregate recognition metrics into stats and rate-limit repeated failures.
- [x] Skip face annotations in live output and explicitly suppress face drawing in renderer; retain person IDs and existing red/orange/green priorities. Update WebUI labels from Person + Face to Person + Recognition, without employee bbox labels.
- [x] Test recognition disabled compatibility, startup check validation and recognition failures not stopping line/zone analytics. Run pipeline/renderer/unit tests and web tests/build in Docker.

## Task 5: Operable commands, documentation and whole-feature verification

**Files:** update `README.md`, `models/README.md`, `docs/architecture.md`, `docs/recipes.md`, `docs/rest_api.md` event documentation, Docker/run instructions; add `docs/adr/0008_person_owned_edgeface_recognition.md`.

- [x] Document exact Docker model preparation, gallery build, enabled local config, startup check and log inspection commands, required mounts and model/gallery incompatibility rules.
- [x] Explain two-observation meaning, near-tie behavior, track-scoped recognition log versus daily attendance persistence and absence of face/employee bbox labels.
- [x] Run `scripts/dev.sh cmake --preset cpu-debug`, `scripts/dev.sh cmake --build --preset cpu-debug`, both CTest presets and `scripts/dev.sh scripts/check.sh`; run web tests/build in node Docker. Failures trigger focused fixes and relevant regression reruns.
- [x] Verify actual gallery loads and run recorded/media integration if available; separate enrollment self-matching from measured recognition accuracy. Report exact gallery totals, skips, tests and unverified GPU/NPU/live camera behavior.
- [x] Request independent review through requesting-code-review, resolve concrete findings and rerun affected checks. Leave changes reviewable and uncommitted; give rebuild command for user's stack.

## Execution evidence — 2026-10-07

Implemented in the shared IDE checkout on `feat/live-edgeface-recognition`, with no automatic
commit or stack restart. Generated artifacts and enrollment data remain ignored.

- Docker exporter rebuilt the same original-GELU batch-4 artifact hash; counts 1–4 compared
  with independent PyTorch batch-1 results. Target ONNX Runtime codec batches also passed.
- Actual gallery: 74 employee directories, 72 usable employees, 207 samples, 277 images read,
  70 skipped (43 alignment/crop rejection, 21 multiple faces, 6 no face). TECH0003 and TECH3618
  had no usable images. Startup loaded all 207 samples; enrollment self-match was checked.
- Synthetic BGR fixtures test RGB/NCHW alignment and invalid crops. Builder skip summaries
  were verified against actual enrollment images instead of adding synthetic face photographs.
- Unit coverage includes ownership ambiguity, source limits, distinct employee ranking,
  weak/unknown/error interruptions, identity refresh/change, deferred budget, Lost retention,
  expiry/capacity/reset, config bounds, gallery validation and person-only overlays.
- Independent review identified reconnect invalidation and missing error diagnostics. Both
  have regression tests that failed before their fixes; stop-during-embedding also discards
  attendance. Separate region/batch error counters preserve each initial diagnostic.
- Atomic gallery replacement preserves prior data on rejected replacement; corrupt/missing
  gallery reads and actual local gallery loading are covered by integration tests.
- CPU configure/build, 111 unit tests, 10 integration tests, layer/format checks and
  13 WebUI tests/build passed in Docker.
  Live RTSP integration was skipped because its dedicated test fixture was not configured.
  GPU/NPU and live camera recognition accuracy were not tested. No speed/accuracy claim.
