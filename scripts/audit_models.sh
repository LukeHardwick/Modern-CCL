#!/usr/bin/env bash
# Audit a sample model against a denser reference build of the same generator (task T22c).
#   scripts/audit_models.sh [options]
# Builds the reference with the generator's make_models.py at --detail K into a temporary
# directory outside the repository (deleted afterwards), then runs the full audit
# (tools/models/audit_model.py) on the committed databases/sample/models/<name>.glb and writes
# <name>_audit.json and <name>_audit.md (and heat-map PNGs with --images) to the report
# directory. Method: docs/research/04-model-audit.md.
#
# Options:
#   --generator DIR   directory holding make_models.py (default: tools/models of this checkout)
#   --name NAME       model to audit (default: jet)
#   --detail K        tessellation multiplier for the reference build (default: 4)
#   --model PATH      audit this .glb instead of databases/sample/models/<name>.glb
#   --reference PATH  use this reference .glb instead of building one
#   --config PATH     audit config (default: tools/models/audit/<name>.toml)
#   --out DIR         report directory (default: build/audit/<name>, git-ignored)
#   --images          also render the heat-map PNGs
#   --fast            fewer samples and ray directions: for iteration, not for checkpoints
#   --strict          exit 1 when any gate fails, not only a hard one
#   --jobs N          worker processes for the visibility check (default: up to 4); the
#                     results do not depend on it
# Environment: BLENDER (default: blender on the PATH).
# Only the --out, --only and --detail flags of make_models.py are used.
# Exit status: the audit's (0 = no hard gate failed, 1 = a hard gate failed, 2 = bad input or
# an internal error); 3 when the reference build wrote no .glb; 64 on a usage error.
#
# Example: scripts/audit_models.sh --images --out /tmp/jet_audit
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1   # neither Blender run leaves __pycache__ in tools/models

repo="$(cd "$(dirname "$0")/.." && pwd)"
generator="$repo/tools/models"
name="jet"
detail="4"
model=""
reference=""
config=""
out=""
audit_flags=()

usage() { sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; }
need() { [ $# -ge 2 ] && [ -n "$2" ] || { echo "audit_models: $1 needs a value" >&2; exit 64; }; }

while [ $# -gt 0 ]; do
  case "$1" in
    --generator) need "$@"; generator="$2"; shift 2 ;;
    --name) need "$@"; name="$2"; shift 2 ;;
    --detail) need "$@"; detail="$2"; shift 2 ;;
    --model) need "$@"; model="$2"; shift 2 ;;
    --reference) need "$@"; reference="$2"; shift 2 ;;
    --config) need "$@"; config="$2"; shift 2 ;;
    --out) need "$@"; out="$2"; shift 2 ;;
    --images|--fast|--strict) audit_flags+=("$1"); shift ;;
    --jobs) need "$@"; audit_flags+=(--jobs "$2"); shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "audit_models: unknown option $1 (see --help)" >&2; exit 64 ;;
  esac
done

model="${model:-$repo/databases/sample/models/$name.glb}"
config="${config:-$repo/tools/models/audit/$name.toml}"
out="${out:-$repo/build/audit/$name}"
blender="${BLENDER:-blender}"

command -v "$blender" >/dev/null || {
  echo "audit_models: Blender not found ($blender)" >&2; exit 2; }
[ -f "$model" ] || { echo "audit_models: no model at $model" >&2; exit 2; }
[ -f "$config" ] || { echo "audit_models: no config at $config" >&2; exit 2; }
case "$detail" in
  ''|*[!0-9.]*) echo "audit_models: --detail must be a number" >&2; exit 64 ;;
esac

start=$(date +%s)
if [ -z "$reference" ]; then
  [ -f "$generator/make_models.py" ] || {
    echo "audit_models: no make_models.py in $generator" >&2; exit 2; }
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/audit_reference.XXXXXX")"
  trap 'rm -rf "$tmp"' EXIT   # set first, so nothing below can leave the directory behind
  tmp="$(cd "$tmp" && pwd -P)"
  case "$tmp/" in
    "$(cd "$repo" && pwd -P)"/*)
      echo "audit_models: the temporary directory $tmp is inside the repository; set TMPDIR" >&2
      exit 2 ;;
  esac
  echo "[audit_models] building the reference: $name at --detail $detail ($generator)"
  build_status=0
  "$blender" --background --factory-startup --python-exit-code 2 \
    --python "$generator/make_models.py" -- \
    --out "$tmp" --only "$name" --detail "$detail" > "$tmp/build.log" 2>&1 || build_status=$?
  reference="$tmp/$name.glb"
  if [ ! -f "$reference" ]; then
    echo "audit_models: the reference build wrote no $name.glb (exit $build_status):" >&2
    tail -n 30 "$tmp/build.log" >&2
    exit 3
  fi
  grep '^\[make_models\]' "$tmp/build.log" || true
  if [ "$build_status" -ne 0 ]; then
    # make_models.py exits 1 when its own checks find a problem (listed above); 2 is a Python
    # error after the export (its end is shown). The .glb was written either way.
    [ "$build_status" -eq 1 ] || tail -n 15 "$tmp/build.log"
    echo "[audit_models] note: make_models.py exited $build_status for the reference" \
         "after writing $name.glb; auditing against it anyway"
  fi
  echo "[audit_models] reference built in $(( $(date +%s) - start )) s"
fi

mkdir -p "$out"
status=0
"$blender" --background --factory-startup --python-exit-code 2 \
  --python "$repo/tools/models/audit_model.py" -- \
  --model "$model" --reference "$reference" --config "$config" --out "$out" --name "$name" \
  ${audit_flags[@]+"${audit_flags[@]}"} || status=$?
echo "[audit_models] done in $(( $(date +%s) - start )) s, exit $status:" \
     "$out/${name}_audit.md and ${name}_audit.json"
exit "$status"
