#!/usr/bin/env bash
# Run demo_ig and demo_host against each other on loopback and print both summaries.
#   scripts/demo.sh [seconds] [extra demo_ig flags...]      (default 5 seconds)
# Environment: PRESET (default clang-debug), IG_PORT (8004), HOST_PORT (8005),
#              PCAP_DIR (write ig.pcap and host.pcap there; open them in Wireshark).
# (seconds must be a whole number.) Example: scripts/demo.sh 10 --drop 0.1 --latency 20
set -euo pipefail
cd "$(dirname "$0")/.."

seconds="${1:-5}"
[ $# -gt 0 ] && shift
case "$seconds" in
  ''|*[!0-9]*) echo "usage: $0 [whole seconds] [demo_ig flags...]" >&2; exit 2 ;;
esac
preset="${PRESET:-clang-debug}"
ig_port="${IG_PORT:-8004}"
host_port="${HOST_PORT:-8005}"

cmake --preset "$preset" >/dev/null
cmake --build --preset "$preset" --target demo_ig demo_host >/dev/null
bin="build/$preset/apps"

pcap_ig=() pcap_host=()
if [ -n "${PCAP_DIR:-}" ]; then
  mkdir -p "$PCAP_DIR"
  pcap_ig=(--pcap "$PCAP_DIR/ig.pcap")
  pcap_host=(--pcap "$PCAP_DIR/host.pcap")
fi

"$bin/demo_ig/demo_ig" --listen-addr 127.0.0.1 --listen-port "$ig_port" \
  --duration "$((seconds + 2))" ${pcap_ig[@]+"${pcap_ig[@]}"} "$@" &
ig_pid=$!
cleanup() { kill "$ig_pid" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.5

status=0
"$bin/demo_host/demo_host" --ig-addr 127.0.0.1 --ig-port "$ig_port" --listen-port "$host_port" \
  --duration "$seconds" ${pcap_host[@]+"${pcap_host[@]}"} || status=$?

wait "$ig_pid" || true
trap - EXIT
echo "demo_host exit code: $status"
[ -n "${PCAP_DIR:-}" ] && echo "pcap files: $PCAP_DIR/ig.pcap $PCAP_DIR/host.pcap"
exit "$status"
