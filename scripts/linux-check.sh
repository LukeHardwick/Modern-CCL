#!/usr/bin/env bash
# Build and test with GCC 14 inside Docker (Colima). Uses a separate build dir.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
image=modern-ccl-gcc14

docker build -t "$image" -f "$root/docker/gcc14.Dockerfile" "$root/docker"
# Build dirs live in a named volume so they never clash with macOS build/ trees. Each checkout
# (main or a worktree) gets its own subdirectory; the dependency cache is shared. Parallel jobs
# default to 2 (CIGI_LINUX_JOBS) because GCC + ASan needs ~1 GB per job and Colima defaults to 2 GB.
tree="$(printf '%s' "$root" | shasum | cut -c1-12)"
docker run --rm -v "$root":/src:ro -v modern-ccl-linux-build:/work -e TREE="$tree" -e JOBS="${CIGI_LINUX_JOBS:-2}" "$image" \
  bash -euo pipefail -c '
  for p in linux-gcc-debug linux-gcc-asan; do
    echo "=== $p ==="
    cmake -S /src -B "/work/$TREE/$p" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=g++ -DCIGI_DEPS_CACHE=/work/deps-cache \
      $([ "$p" = linux-gcc-asan ] && echo -DCIGI_ENABLE_SANITIZERS=ON)
    cmake --build "/work/$TREE/$p" -j "$JOBS"
    ctest --test-dir "/work/$TREE/$p" --output-on-failure
  done'
