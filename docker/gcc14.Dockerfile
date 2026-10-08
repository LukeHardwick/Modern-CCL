FROM gcc:14

RUN apt-get update \
    && apt-get install -y --no-install-recommends ninja-build python3-pip ca-certificates \
    && pip3 install --no-cache-dir --break-system-packages "cmake>=3.28,<5" \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
