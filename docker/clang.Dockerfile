# Clang 21 + libFuzzer + libstdc++ 15 (Ubuntu 26.04), for scripts/fuzz.sh --linux.
# (Ubuntu 24.04's Clang 18 hides std::expected even with libstdc++ 13/14, and its libFuzzer is not libc++-ABI.)
FROM ubuntu:26.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-21 clang-tools-21 libclang-rt-21-dev libstdc++-15-dev llvm-21 cmake ninja-build ca-certificates \
    && ln -s /usr/bin/clang++-21 /usr/local/bin/clang++ \
    && ln -s /usr/bin/clang-21 /usr/local/bin/clang \
    && ln -s /usr/bin/clang-scan-deps-21 /usr/local/bin/clang-scan-deps \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
