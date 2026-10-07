#!/usr/bin/env bash
# Run inside Docker; exact upstream artifacts, no Python/export dependencies.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p models/live_ai
fetch_model() {
  local filename="$1" checksum="$2" url="$3"
  local destination="models/live_ai/${filename}"
  if [[ -f "$destination" ]] && echo "${checksum}  ${destination}" | sha256sum --check --status; then
    echo "[models] verified ${filename}"
    return
  fi
  curl --fail --location --retry 3 "$url" -o "${destination}.download"
  echo "${checksum}  ${destination}.download" | sha256sum --check
  mv "${destination}.download" "$destination"
}
fetch_model nanodet_plus_m_320.onnx 4f12723cce3d48e47ca92cb925ba74d97a965c069208edca660bbb9f7ce2c610 \
  https://github.com/RangiLyu/nanodet/releases/download/v1.0.0-alpha-1/nanodet-plus-m_320.onnx
fetch_model yunet_2023mar.onnx 8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4 \
  https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
