# T10 — Wireshark / tshark verification

Phase A (MVP) · Depends on T06 · Can run beside T07–T09 · Branch `task/t10-wireshark`
Follow the shared rules in [README.md](README.md). Note rule 2: do **not** read Wireshark's
`packet-cigi.c`; treat the dissector as a black box.

## Goal
Independent evidence that our bytes are CIGI 3.3 as a third-party implementation understands them:
Wireshark's CIGI dissector must decode every MVP packet we produce to the values we put in.

## Environment
Wireshark 4.6.9 is installed (`/opt/homebrew/bin/tshark`, `dumpcap`, `/Applications/Wireshark.app`).
The dissector `cigi` supports CIGI 2/3/4; relevant prefs (`tshark -G defaultprefs | grep cigi`):
`cigi.version` (From Packet / cigi2 / cigi3 / cigi4), `cigi.byte_order` (From Packet / …),
`cigi.host` and `cigi.ig` (IP addresses used to tell direction). Field names: `tshark -G fields | grep '\tcigi\.'`.

## Design (planner decision)
- `tools/` target `cigi_tools` with `cigi::tools::PcapWriter`: writes classic libpcap files
  (`LINKTYPE_RAW`/IPv4) with synthesized IPv4 + UDP headers (correct lengths and checksums),
  configurable addresses/ports and timestamps. No capture privileges needed. (Reused by T09.)
- `tests/interop/wireshark/`:
  - `make_wireshark_corpus` (C++): writes a pcap containing, for every MVP packet, at least two
    messages with distinctive values (incl. negative numbers, max values, every enum value), in
    native byte order and a fabricated foreign-endian peer; Host messages from 10.0.0.1→10.0.0.2,
    IG messages the reverse; plus a JSON/CSV "expected values" file.
  - `check_tshark.py` (Python 3 standard library only): runs `tshark -r corpus.pcap -o
    cigi.host:10.0.0.1 -o cigi.ig:10.0.0.2 -T json` (or `-T fields`), maps our field names to
    dissector field names through an explicit table, and compares (floats with tolerance).
  - ctest test (label `wireshark`) registered only if `find_program(tshark)` succeeds.
- **Discrepancies**: whenever tshark disagrees, decide from the ICD who is right. If we are wrong,
  fix the library with a regression test. If Wireshark is wrong (it had historical CIGI 3.2/3.3
  bugs), mark that field as a known dissector issue in the mapping table (test expects the dissector's
  value or skips it) and record it in `docs/interop/discrepancies.md` with ICD section, our value,
  Wireshark's value and reasoning. Record what Wireshark reports for our Minor Version 3.
- `docs/interop/wireshark.md`: how to regenerate and inspect the corpus, how to view live demo
  traffic, the prefs needed, and the discrepancy summary.
- The mapping table is data-driven so Phase B tasks add packets by appending rows.

## Acceptance
README rules 7–9 (PcapWriter has unit tests incl. a byte-exact header check and a tshark round-trip
when available). Report: packets/fields covered, every discrepancy with your adjudication.
