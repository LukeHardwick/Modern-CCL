#!/usr/bin/env bash
# Build cigi_frames.pcap from the in-memory CIGI exchange simulated by cigi_frames.
# Usage: make_pcap.sh <path-to-cigi_frames> [output-dir]   (output-dir defaults to the current dir)
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
  echo "usage: $0 <path-to-cigi_frames> [output-dir]" >&2
  exit 2
fi
example=$1
out=${2:-.}

for tool in text2pcap mergecap tshark; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: '$tool' not found. Install Wireshark (macOS: brew install --cask wireshark;" >&2
    echo "       Debian/Ubuntu: apt install tshark wireshark-common) and make sure it is on PATH." >&2
    exit 1
  fi
done
if [ ! -x "$example" ]; then
  echo "error: '$example' is not an executable; build the 'cigi_frames' target first." >&2
  exit 1
fi

mkdir -p "$out"
"$example" "$out"

# Host 10.0.0.1:8005 -> IG 10.0.0.2:8004 and the reverse; the dump timestamps drive packet times.
text2pcap -q -t '%Y-%m-%d %H:%M:%S.%f' -4 10.0.0.1,10.0.0.2 -u 8005,8004 \
  "$out/host_to_ig.txt" "$out/host_to_ig.pcap"
text2pcap -q -t '%Y-%m-%d %H:%M:%S.%f' -4 10.0.0.2,10.0.0.1 -u 8004,8005 \
  "$out/ig_to_host.txt" "$out/ig_to_host.pcap"
mergecap -w "$out/cigi_frames.pcap" "$out/host_to_ig.pcap" "$out/ig_to_host.pcap"

echo "wrote $out/cigi_frames.pcap; inspect it with:"
echo "  tshark -r $out/cigi_frames.pcap -o cigi.host:10.0.0.1 -o cigi.ig:10.0.0.2 -V"
