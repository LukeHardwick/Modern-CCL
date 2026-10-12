#!/usr/bin/env bash
# Generate cigi_frames.pcap and check that tshark decodes every datagram as CIGI without errors.
# Usage: check_pcap.sh <path-to-cigi_frames> <output-dir>
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
out=$2
"$here/make_pcap.sh" "$1" "$out" >/dev/null
command -v tshark >/dev/null 2>&1 || { echo "error: 'tshark' not found on PATH" >&2; exit 1; }

opts=(-o cigi.host:10.0.0.1 -o cigi.ig:10.0.0.2)
total=$(tshark -r "$out/cigi_frames.pcap" "${opts[@]}" -Y cigi | wc -l)
malformed=$(tshark -r "$out/cigi_frames.pcap" "${opts[@]}" -Y '_ws.expert.group == "Malformed"' | wc -l)
# The first Start of Frame precedes any IG Control, so "From Packet" cannot yet pick a CIGI version
# (see README); every other datagram must decode as a named packet.
known=$(tshark -r "$out/cigi_frames.pcap" "${opts[@]}" -Y 'cigi.sof || cigi.ig_control' | wc -l)

# The Host's fourth datagram is sent in the other byte order (Byte Swap 0x8000).
foreign=$(tshark -r "$out/cigi_frames.pcap" "${opts[@]}" -Y 'cigi.byte_swap == 0x8000' | wc -l)

echo "datagrams: $total, decoded: $known, malformed: $malformed, foreign byte order: $foreign"
[ "$total" -eq 19 ] && [ "$known" -eq 18 ] && [ "$malformed" -eq 0 ] && [ "$foreign" -eq 1 ]
