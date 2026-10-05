#!/usr/bin/env bash
# Formats all C++ sources with clang-format. Run inside the dev container:
#   scripts/dev.sh scripts/format.sh           # rewrite files
#   scripts/dev.sh scripts/format.sh --check   # fail if anything would change
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

mode=(-i)
if [[ "${1:-}" == "--check" ]]; then
  mode=(--dry-run --Werror)
fi

find src tests -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.cu' \) -print0 |
  xargs -0 --no-run-if-empty clang-format "${mode[@]}"
echo "format: OK"
