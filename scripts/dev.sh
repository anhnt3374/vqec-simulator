#!/usr/bin/env bash
# Runs a command (or an interactive shell) inside the Ubuntu 22.04 dev container.
#
#   scripts/dev.sh                                  # shell
#   scripts/dev.sh cmake --build --preset cpu-debug # any command, non-interactive
#   VQEC_RUNTIME=gpu scripts/dev.sh ...             # GPU image (NVIDIA Container Toolkit required)
#
# The repo is mounted at /workspace, files are created with your uid/gid, ccache persists in
# ~/.cache/vqec-simulator. The image is (re)built from docker/Dockerfile when it changes.
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly runtime="${VQEC_RUNTIME:-cpu}"
readonly image="vqec-simulator-dev:${runtime}"
readonly ccache_dir="${HOME}/.cache/vqec-simulator/ccache-${runtime}"

case "${runtime}" in
  cpu | gpu) ;;
  *)
    echo "dev.sh: VQEC_RUNTIME must be cpu or gpu (got '${runtime}')" >&2
    exit 64
    ;;
esac

if ! docker image inspect "${image}" >/dev/null 2>&1; then
  echo "dev.sh: building ${image} (first run takes several minutes)..." >&2
fi
docker build --quiet --file "${repo_root}/docker/Dockerfile" --target dev \
  --build-arg "TARGET_RUNTIME=${runtime}" --tag "${image}" "${repo_root}" >/dev/null

mkdir -p "${ccache_dir}"
docker_args=(
  --rm --init
  --network host
  --user "$(id -u):$(id -g)"
  --env HOME=/tmp
  --env CCACHE_DIR=/ccache
  --volume "${repo_root}:/workspace"
  --volume "${ccache_dir}:/ccache"
  --workdir /workspace
)
if [[ "${runtime}" == "gpu" ]]; then
  docker_args+=(--gpus all)
fi
if [[ -t 0 && -t 1 ]]; then
  docker_args+=(--interactive --tty)
fi
if [[ $# -eq 0 ]]; then
  set -- bash
fi

exec docker run "${docker_args[@]}" "${image}" "$@"
