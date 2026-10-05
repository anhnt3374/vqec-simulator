#!/usr/bin/env bash
# Static checks required before every commit. Run inside the dev container:
#   scripts/dev.sh scripts/check.sh          # layer rules + formatting
#   scripts/dev.sh scripts/check.sh --tidy   # + clang-tidy on build/cpu-debug
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

scripts/check_layers.sh
scripts/format.sh --check

if [[ "${1:-}" == "--tidy" ]]; then
  build_dir="build/cpu-debug"
  if [[ ! -f "${build_dir}/compile_commands.json" ]]; then
    echo "check: configure first: cmake --preset cpu-debug" >&2
    exit 1
  fi
  run-clang-tidy -quiet -p "${build_dir}" "$(pwd)/(src|tests)/.*\.cpp$"
  echo "clang-tidy: done"
fi
