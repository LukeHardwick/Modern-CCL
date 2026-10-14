#!/usr/bin/env bash
# Fidelity check of a model against the real aircraft (task T22e): gates F1-F5 of
# tools/models/fidelity_audit.py (outline against the reference three-view, feature checklist,
# cross-sections, UVs / textures / file budget, edge crispness). Method:
# docs/research/05-cessna-fidelity.md; gate values: tools/models/audit/cessna_fidelity.toml.
#   scripts/fidelity_audit.sh [options]
#
# Writes <name>_fidelity.json and <name>_fidelity.md (and, with --images, F1 overlay PNGs:
# grey = both, red = only the reference, blue = only the model) to the report directory.
#
# Options:
#   --name NAME      model to check (default: cessna)
#   --model PATH     check this .glb instead of databases/sample/models/<name>.glb
#   --report NAME    prefix of the report files (default: the model name)
#   --config PATH    gate file (default: tools/models/audit/<name>_fidelity.toml)
#   --out DIR        report directory (default: build/fidelity/<name>, git-ignored)
#   --family F       textured (default) | vertex_colour (the cessna_vc*.glb family)
#   --images         also write the F1 overlay PNGs
#   --blender        run inside headless Blender (its Python decodes JPEG textures);
#                    the default uses python3 with numpy, which gives the same report bytes
# Environment: BLENDER (default: blender on the PATH), PYTHON (default: python3).
# Exit status: the checker's (0 = no hard gate failed, 1 = a hard gate failed or could not be
# measured, 2 = bad input or an internal error); 64 on a usage error.
#
# Example: scripts/fidelity_audit.sh --images --out /tmp/cessna_fidelity
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repo="$(cd "$(dirname "$0")/.." && pwd)"
name="cessna"
report=""
model=""
config=""
out=""
use_blender=0
args=()

usage() { sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; }
need() { [ $# -ge 2 ] && [ -n "$2" ] || { echo "fidelity_audit: $1 needs a value" >&2; exit 64; }; }

while [ $# -gt 0 ]; do
  case "$1" in
    --name) need "$@"; name="$2"; shift 2 ;;
    --report) need "$@"; report="$2"; shift 2 ;;
    --model) need "$@"; model="$2"; shift 2 ;;
    --config) need "$@"; config="$2"; shift 2 ;;
    --out) need "$@"; out="$2"; shift 2 ;;
    --family) need "$@"; args+=(--family "$2"); shift 2 ;;
    --images) args+=(--images); shift ;;
    --blender) use_blender=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "fidelity_audit: unknown option $1 (see --help)" >&2; exit 64 ;;
  esac
done

model="${model:-$repo/databases/sample/models/$name.glb}"
config="${config:-$repo/tools/models/audit/${name}_fidelity.toml}"
out="${out:-$repo/build/fidelity/$name}"
report="${report:-$name}"
[ -f "$model" ] || { echo "fidelity_audit: no model at $model" >&2; exit 2; }
[ -f "$config" ] || { echo "fidelity_audit: no config at $config" >&2; exit 2; }

status=0
if [ "$use_blender" = 1 ]; then
  blender="${BLENDER:-blender}"
  command -v "$blender" >/dev/null || { echo "fidelity_audit: Blender not found ($blender)" >&2; exit 2; }
  "$blender" --background --factory-startup --python-exit-code 2 \
    --python-expr "import sys; sys.dont_write_bytecode = True" \
    --python "$repo/tools/models/fidelity_audit.py" -- \
    --model "$model" --config "$config" --out "$out" --name "$report" \
    ${args[@]+"${args[@]}"} || status=$?
else
  py="${PYTHON:-python3}"
  command -v "$py" >/dev/null || { echo "fidelity_audit: $py not found" >&2; exit 2; }
  "$py" -c 'import numpy' 2>/dev/null || {
    echo "fidelity_audit: $py has no numpy; use --blender" >&2; exit 2; }
  "$py" "$repo/tools/models/fidelity_audit.py" \
    --model "$model" --config "$config" --out "$out" --name "$report" \
    ${args[@]+"${args[@]}"} || status=$?
fi
echo "[fidelity_audit] exit $status: $out/${report}_fidelity.md and $out/${report}_fidelity.json"
exit "$status"
