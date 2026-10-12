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
    cmake -S /src -B /work/linux-clang-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g" -DCIGI_ENABLE_FUZZING=ON -DCIGI_BUILD_TESTS=OFF \
      -DCIGI_BUILD_EXAMPLES=OFF
    cmake --build /work/linux-clang-fuzz --target "fuzz_'"$target"'"
    mkdir -p "/corpus/'"$target"'"
    "/work/linux-clang-fuzz/fuzz/fuzz_'"$target"'" \
      -dict=/src/fuzz/cigi.dict -artifact_prefix=/artifacts/'"$target"'- \
      '"${flags[*]}"' "/corpus/'"$target"'" "/src/fuzz/corpus/'"$target"'"'
