#!/usr/bin/env bash
# Optional real-model integration fixtures; run in Docker, images remain ignored.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p tests/data/media
curl --fail --location --retry 3 https://raw.githubusercontent.com/opencv/opencv/master/samples/data/lena.jpg -o tests/data/media/lena.jpg
curl --fail --location --retry 3 https://raw.githubusercontent.com/ultralytics/assets/main/im/bus.jpg -o tests/data/media/bus.jpg
