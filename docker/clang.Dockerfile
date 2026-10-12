# Clang + libFuzzer on Linux, for scripts/fuzz.sh --linux and the linux-clang-fuzz preset.
FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-18 clang-tools-18 libclang-rt-18-dev llvm-18 cmake ninja-build ca-certificates \
    && ln -s /usr/bin/clang++-18 /usr/local/bin/clang++ \
    && ln -s /usr/bin/clang-18 /usr/local/bin/clang \
    && ln -s /usr/bin/clang-scan-deps-18 /usr/local/bin/clang-scan-deps \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
