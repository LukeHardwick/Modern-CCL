# T02c — Example: view IG Control / Start of Frame traffic in Wireshark

Foundation (extra) · Depends on T02b · Branch `task/t02c-wireshark-frame-example`
Follow the shared rules in [README.md](README.md). Rule 2 applies: treat Wireshark's CIGI dissector as
a black box (don't read `packet-cigi.c`).

## Goal
Something a person can run straight after cloning the repo to see our CIGI bytes decoded by
Wireshark — before networking (T08) or the pcap writer (T10) exist. Uses only the two implemented
packets, so it demonstrates framing, byte order, versions and frame numbering (ICD 3.3 §2.3, §2.5,
§2.8, §4.1.1, §4.2.1).

## Environment
Wireshark 4.6.9 installed: `text2pcap`, `mergecap`, `tshark` in `/opt/homebrew/bin`, Wireshark.app.
CIGI dissector prefs: `cigi.version`, `cigi.byte_order`, `cigi.host`, `cigi.ig`
(`tshark -G defaultprefs | grep cigi`).

## Scope
1. `examples/wireshark/cigi_frames.cpp` (built by the existing examples target; < 150 lines, readable —
   it is teaching material): simulate a scripted start-up exchange **in memory** using
   `MessageBuilder`/`MessageView` and the public API only:
   - IG sends Start of Frame in Reset/Standby for a few frames; Host replies with IG Control
     requesting Operate; IG switches to Operate; a few more frames in Operate.
   - Frame numbers follow ICD §2.3 Figure 6 (Host Frame Number / Last IG Frame Number and the
     reverse), timestamps advance at 60 Hz in 10 µs ticks with Timestamp Valid set.
   - Include one Host→IG message encoded in the **foreign** byte order (fabricated with
     `wire::store(..., ByteOrder::Swapped)` or an equivalent helper) so Wireshark's byte-order
     handling is visible; label it in the output.
   - Output: hex dumps in a format `text2pcap` accepts (offset-prefixed lines, one dump per datagram,
     with timestamps so `mergecap` can interleave directions), written to an output directory given
     on the command line: one file for Host→IG and one for IG→Host datagrams. Also print a short
     human-readable summary of each message to stdout.
2. `examples/wireshark/make_pcap.sh`: runs the example, converts each direction with `text2pcap`
   (UDP; Host 10.0.0.1:8005 → IG 10.0.0.2:8004 and the reverse), merges with `mergecap` in time order
   into `cigi_frames.pcap`, and prints the `tshark` command to inspect it. Fails with a clear message
   if Wireshark tools are missing. `set -euo pipefail`; no hard-coded absolute paths.
3. `examples/wireshark/README.md`: prerequisites (Wireshark; macOS `brew install --cask wireshark`
   or the website; Linux packages), build + run steps, the Wireshark prefs to set (Preferences →
   Protocols → CIGI: host/IG addresses, version "From Packet") or Decode As on the ports, what each
   frame should show, and known dissector caveats you observe.
4. Verify with `tshark` yourself (`tshark -r cigi_frames.pcap -o cigi.host:10.0.0.1 -o cigi.ig:10.0.0.2
   -V` and/or `-T fields`): every datagram decodes as CIGI; record per field whether Wireshark's values
   match what we encoded — especially Major/Minor Version (we send minor 3), Byte Swap Magic, frame
   numbers, timestamp, IG mode, database number, and the foreign-order message.
5. Create `docs/interop/discrepancies.md` (T10/T11 will extend it) with a table: field, ICD section,
   our value, Wireshark's value, verdict (ICD wins). Do not change library code to match Wireshark;
   if our library is actually wrong per the ICD, stop and report instead of fixing it here.
6. Add a ctest (label `wireshark`) that runs `make_pcap.sh` into the build dir and checks with
   `tshark` that the expected number of CIGI datagrams decode without "Malformed" — only registered
   when `find_program(tshark text2pcap mergecap)` all succeed.
7. Commit the generated `cigi_frames.pcap`? **No** — generate it; add `*.pcap` to `.gitignore`
   except nothing. Mention in the README how to regenerate.

## Acceptance
`scripts/check.sh` green (clang-tidy applies to the example). `make_pcap.sh` works from a fresh
build. Report: the tshark verification table, any discrepancies with your ICD-based verdicts, and
screenshots are not needed — paste trimmed `tshark` output instead.
