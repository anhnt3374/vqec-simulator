# Models

This directory holds `manifest.yaml` (committed) and model binaries (git-ignored). It is mounted
read-only at `/opt/vqec/models` by `docker/compose.yaml`.

- Add or update a model: edit `manifest.yaml`, place the artifact next to it, fill `sha256`
  (`sha256sum <file>`). See `docs/recipes.md` → "Add a model".
- TensorRT engines are generated from the ONNX artifact on first load and cached in
  `models/cache/` (git-ignored); they are specific to the GPU model and TensorRT version.
- NPU artifacts (quantized/compiled graphs) are listed per backend under `artifacts:`.
- Export/conversion scripts live in `tools/`, never inside the C++ tree.
- Do not commit model weights, private SDK files or biometric data.

## CPU live-view pair

Run `scripts/dev.sh scripts/prepare_live_ai_models.sh` to download and SHA256-verify
`models/live_ai/nanodet_plus_m_320.onnx` and `yunet_2023mar.onnx`. The worker
verifies both hashes again before loading. URLs, hashes, shapes and licenses are
recorded in `manifest.yaml`; binaries stay ignored. No export toolchain is needed.

NanoDet uses fixed 320×320 BGR normalization and outputs `[1,2125,112]`
(COCO80 + four 8-bin distance distributions). Only class person is emitted. YuNet
uses fixed 640×640 BGR floats and stride-8/16/32 heads. Both warp inputs according
to these artifact contracts and map boxes to source pixels. MobileFaceNet face
embedding is the selected next model but is not loaded in this detection demo.

For real-model integration fixtures, run
`scripts/dev.sh scripts/prepare_live_ai_test_data.sh`; the downloaded images stay
ignored. Run `scripts/dev.sh ctest --preset cpu-debug-integration`. Set
`VQEC_TEST_RTSP_URL` inside the Docker container to additionally verify a live
RTSP stream; unavailable fixtures/streams are explicitly skipped.

EdgeFace-S gamma 0.5 uses the original pinned checkpoint and FP32 GELU graph. See README
for the Docker `edgeface-export` target and tools/prepare_edgeface.py. Artifact input is
[4,3,112,112] RGB normalized to [-1,1], output [4,512]; the codec L2-normalizes each result
and pads/discards unused rows. Gallery and live use identical 5-point similarity alignment.
The tracked export metadata records checkpoint/artifact hashes and verified batch counts.
