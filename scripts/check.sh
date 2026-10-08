#!/usr/bin/env bash
# Local verification (no CI): format, then clang-debug / clang-asan / clang-tidy.
# Options: --coverage  also run the coverage preset + target
#          --linux     also run the Docker GCC 14 check
set -euo pipefail
cd "$(dirname "$0")/.."

coverage=0 linux=0
for a in "$@"; do
  case "$a" in
    --coverage) coverage=1 ;;
    --linux) linux=1 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

cmake --preset clang-debug >/dev/null
cmake --build --preset clang-debug --target format-check

for p in clang-debug clang-asan clang-tidy; do
  echo "=== $p ==="
  cmake --preset "$p"
  cmake --build --preset "$p"
  ctest --preset "$p"
done

if [ "$coverage" = 1 ]; then
  echo "=== clang-coverage ==="
  cmake --preset clang-coverage
  cmake --build --preset clang-coverage
  cmake --build --preset clang-coverage --target coverage
fi

if [ "$linux" = 1 ]; then
  scripts/linux-check.sh
fi
echo "ALL CHECKS PASSED"
