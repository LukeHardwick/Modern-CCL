#!/usr/bin/env bash
# Run a libFuzzer target for a fixed time.
#   scripts/fuzz.sh [--linux] <message_view|roundtrip|sessions> <seconds> [extra libFuzzer flags...]
# Seeds come from fuzz/corpus/<target>; the grown corpus lives in build/fuzz-corpus/<target> (or the
# modern-ccl-fuzz-corpus Docker volume with --linux), and crashing inputs in build/fuzz-artifacts/.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"

linux=0
if [ "${1:-}" = "--linux" ]; then linux=1; shift; fi
if [ $# -lt 2 ]; then
  echo "usage: $0 [--linux] <message_view|roundtrip|sessions> <seconds> [libFuzzer flags...]" >&2
  exit 2
fi
target="$1" seconds="$2"; shift 2
case "$target" in
  message_view|roundtrip|sessions) ;;
  *) echo "unknown target: $target" >&2; exit 2 ;;
esac
flags=(-max_total_time="$seconds" -max_len=1500 -use_value_profile=1 -print_final_stats=1
       -timeout=10 -rss_limit_mb=2048 "$@")

if [ "$linux" = 0 ]; then
  cd "$root"
  cmake --preset clang-fuzz >/dev/null
  cmake --build --preset clang-fuzz --target "fuzz_$target"
  mkdir -p "build/fuzz-corpus/$target" build/fuzz-artifacts
  exec "build/clang-fuzz/fuzz/fuzz_$target" \
    -dict="$root/fuzz/cigi.dict" -artifact_prefix="$root/build/fuzz-artifacts/${target}-" \
    "${flags[@]}" "build/fuzz-corpus/$target" "fuzz/corpus/$target"
fi

image=modern-ccl-clang
docker build -t "$image" -f "$root/docker/clang.Dockerfile" "$root/docker"
mkdir -p "$root/build/fuzz-artifacts"
docker run --rm \
  -v "$root":/src:ro -v modern-ccl-linux-fuzz-build:/work \
  -v modern-ccl-fuzz-corpus:/corpus -v "$root/build/fuzz-artifacts":/artifacts \
  "$image" bash -euo pipefail -c '
    target="$1"; shift
    # --fresh: never reuse a stale cache left in the build volume by an older image/preset.
    cmake --fresh -S /src --preset linux-clang-fuzz -B /work/linux-clang-fuzz
    cmake --build /work/linux-clang-fuzz --target "fuzz_$target"
    mkdir -p "/corpus/$target"
    "/work/linux-clang-fuzz/fuzz/fuzz_$target" \
      -dict=/src/fuzz/cigi.dict -artifact_prefix="/artifacts/$target-" \
      "$@" "/corpus/$target" "/src/fuzz/corpus/$target"' _ "$target" "${flags[@]}"
