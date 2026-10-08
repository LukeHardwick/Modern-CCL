#!/usr/bin/env bash
# Build and test with GCC 14 inside Docker (Colima). Uses a separate build dir.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
image=modern-ccl-gcc14

docker build -t "$image" -f "$root/docker/gcc14.Dockerfile" "$root/docker"
# Build dir lives in a named volume so it never clashes with macOS build/ trees.
docker run --rm -v "$root":/src:ro -v modern-ccl-linux-build:/work "$image" bash -euo pipefail -c '
  for p in linux-gcc-debug linux-gcc-asan; do
    echo "=== $p ==="
    cmake -S /src -B "/work/$p" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=g++ -DCIGI_DEPS_CACHE=/work/deps-cache \
      $([ "$p" = linux-gcc-asan ] && echo -DCIGI_ENABLE_SANITIZERS=ON)
    cmake --build "/work/$p"
    ctest --test-dir "/work/$p" --output-on-failure
  done'
