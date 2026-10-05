#!/usr/bin/env bash
# Enforces the hexagonal dependency rules (AGENTS.md section 4) on #include directives:
#   - project includes are written from src/ and never use "../"
#   - each layer includes only the layers it may depend on
#   - domain, ports and application include no third-party headers
#   - #if VQEC_WITH_* appears only in bootstrap/
# Exit code 1 lists every violation.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

violations=0
report() {
  echo "layer violation: $1" >&2
  violations=$((violations + 1))
}

std_header='^[a-z_0-9]+$'

while IFS= read -r -d '' file; do
  rel="${file#src/}"
  layer="${rel%%/*}"
  deny=''
  pure=1
  case "${layer}" in
    common)
      allow='^common/'
      [[ "${rel}" == common/log.* ]] && pure=0
      ;;
    domain)
      allow='^(common|domain)/'
      deny='^common/log\.hpp$'
      ;;
    ports)
      allow='^(common|domain|ports)/'
      deny='^common/log\.hpp$'
      ;;
    application)
      allow='^(common|domain|ports|application)/'
      ;;
    adapters)
      family="$(echo "${rel}" | cut -d/ -f1-3)"
      allow="^(common|domain|ports|${family})/"
      pure=0
      ;;
    bootstrap)
      allow='.*'
      pure=0
      ;;
    *)
      report "${file}: unknown layer '${layer}'"
      continue
      ;;
  esac

  while IFS= read -r line; do
    number="${line%%:*}"
    text="${line#*:}"
    if [[ "${text}" =~ \#[[:space:]]*include[[:space:]]*\"([^\"]+)\" ]]; then
      target="${BASH_REMATCH[1]}"
      if [[ "${target}" == *../* || "${target}" == ./* ]]; then
        report "${file}:${number}: relative include \"${target}\" (write it from src/)"
      elif [[ ! "${target}" =~ ${allow} ]]; then
        report "${file}:${number}: ${layer} must not include \"${target}\""
      elif [[ -n "${deny}" && "${target}" =~ ${deny} ]]; then
        report "${file}:${number}: ${layer} must not include \"${target}\""
      fi
    elif [[ "${text}" =~ \#[[:space:]]*include[[:space:]]*\<([^\>]+)\> ]]; then
      target="${BASH_REMATCH[1]}"
      if [[ "${pure}" -eq 1 && ! "${target}" =~ ${std_header} ]]; then
        report "${file}:${number}: ${layer} must not include third-party <${target}>"
      fi
    fi
  done < <(grep -nE '^[[:space:]]*#[[:space:]]*include' "${file}" || true)

  if [[ "${layer}" != bootstrap ]] && grep -nE '^[[:space:]]*#[[:space:]]*if.*VQEC_WITH_' "${file}" >/dev/null; then
    report "${file}: #if VQEC_WITH_* is only allowed in bootstrap/"
  fi
done < <(find src -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.cu' -o -name '*.in' \) -print0)

if [[ "${violations}" -gt 0 ]]; then
  echo "check_layers: ${violations} violation(s)" >&2
  exit 1
fi
echo "check_layers: OK"
