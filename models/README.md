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
