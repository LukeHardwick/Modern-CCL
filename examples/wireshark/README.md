# Viewing CIGI frames in Wireshark

`cigi_frames` simulates a short Host/IG start-up exchange in memory (no network), using only the
public `MessageBuilder` / `MessageView` API, and writes the datagrams to `cigi_frames.pcap`
(through `cigi::tools::PcapWriter`) so you can see our bytes decoded by an independent tool.
The larger verification of every MVP packet is in `docs/interop/wireshark.md`.

## Prerequisites

Wireshark (or just `tshark`) to read the pcap; writing it needs nothing.

- macOS: `brew install --cask wireshark` (tools land in `/opt/homebrew/bin`) or wireshark.org.
- Debian/Ubuntu: `sudo apt install wireshark tshark`.
- Fedora: `sudo dnf install wireshark-cli wireshark`.

## Build and run

```sh
cmake --preset clang-debug && cmake --build --preset clang-debug --target cigi_frames
examples/wireshark/make_pcap.sh build/clang-debug/examples/cigi_frames build/wireshark
```

This prints a one-line summary per message, writes `cigi_frames.pcap` into the output directory
(running `cigi_frames <dir>` directly does the same), and prints the `tshark` command to inspect
it. The pcap is generated, never committed; rerun the script to regenerate it.
`ctest -L wireshark` runs the same pipeline and checks it with tshark.

## Wireshark settings

Open `cigi_frames.pcap`, then Preferences, Protocols, CIGI: set **Host** to `10.0.0.1` and **IG** to
`10.0.0.2` (the dissector uses them to know the direction), leave Version on "From Packet". Or use
Analyze, Decode As, UDP ports 8004 and 8005, CIGI. From the CLI:

```sh
tshark -r cigi_frames.pcap -o cigi.host:10.0.0.1 -o cigi.ig:10.0.0.2 -V
```

## What you should see

Twelve datagrams, 16.667 ms apart (60 Hz): IG Start of Frame (SOF), 2 ms later the Host's
IG Control.

| Frames | What |
|---|---|
| 1, 3, 5 | SOF IG frames 1-3 in Reset/Standby; database -128 (unavailable), -1 (loading), 1 (loaded) |
| 2, 4, 6 | IG Control host frames 1-3 (Standby, Standby, then Operate requested) |
| 7-12 | IG frames 4-6 in Operate, host frames 4-6; frame numbers keep incrementing (ICD 2.3, Figure 6) |
| 10 | IG Control host frame 5 in the **foreign** byte order: Byte Swap shows `0x8000` (Big-Endian) |

Every IG Control's *Last IG Frame Number* equals the SOF just received; every SOF's *Last Host Frame
Number* equals the previous IG Control's *Host Frame Number*. Minor Version is 3 throughout.

## Dissector caveats observed (Wireshark 4.6.9)

See `docs/interop/discrepancies.md` for the full table. In short:

- With Version "From Packet" the very first datagram of a capture shows as "Unknown (24 bytes)",
  whether it is a Start of Frame or an IG Control; later ones decode. Real captures start with one
  of them, so this happens once.
- With Version forced to "CIGI 3" it uses the 3.0/3.1 16-byte layout: 24-byte packets then raise
  "Packet offset does not match packet length" (Malformed) and the 3.2/3.3 fields are not shown.
  Keep "From Packet".
- "Timestamp (microseconds)" shows the raw 10 us tick count; multiply by 10 for microseconds.
- The Last IG Frame Number in IG Control is labelled "IG Frame Number".
