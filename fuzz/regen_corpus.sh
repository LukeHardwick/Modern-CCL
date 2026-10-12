#!/usr/bin/env bash
# Regenerate the committed seed corpora in fuzz/corpus/: the hand-built and golden-vector seeds
# of cigi_gen_corpus plus every datagram of the T10 Wireshark verification corpus (corpus.pcap).
# Usage: fuzz/regen_corpus.sh      (uses the clang-debug preset, so macOS only)
# Nothing checks that the committed corpus matches the generator: rerun this script after changing
# gen_corpus.cpp or the T10 Wireshark corpus, and commit the result.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
cmake --preset clang-debug >/dev/null
cmake --build --preset clang-debug --target cigi_gen_corpus make_wireshark_corpus
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
build/clang-debug/tests/make_wireshark_corpus "$work"
rm -rf fuzz/corpus/message_view fuzz/corpus/roundtrip fuzz/corpus/sessions
build/clang-debug/fuzz/cigi_gen_corpus fuzz/corpus --import-pcap "$work/corpus.pcap"
