#!/usr/bin/env bash
# Build cigi_frames.pcap from the in-memory CIGI exchange simulated by cigi_frames.
# Usage: make_pcap.sh <path-to-cigi_frames> [output-dir]   (output-dir defaults to the current dir)
# cigi_frames writes the pcap itself (cigi::tools::PcapWriter); this script runs it and prints how
# to inspect the result.
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
  echo "usage: $0 <path-to-cigi_frames> [output-dir]" >&2
  exit 2
fi
example=$1
out=${2:-.}

if [ ! -x "$example" ]; then
  echo "error: '$example' is not an executable; build the 'cigi_frames' target first." >&2
  exit 1
fi

mkdir -p "$out"
"$example" "$out"

echo "inspect it with (needs Wireshark's tshark; macOS: brew install --cask wireshark):"
echo "  tshark -r $out/cigi_frames.pcap -o cigi.host:10.0.0.1 -o cigi.ig:10.0.0.2 -V"
