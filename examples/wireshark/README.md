# Viewing CIGI frames in Wireshark

`cigi_frames` runs a real `cigi::Host` and `cigi::Ig` back to back over the in-memory transport
(no network) and writes every datagram they send to `cigi_frames.pcap` (through
`cigi::tools::PcapWriter`), so you can see the start-up exchange the library actually produces,
decoded by an independent tool. A small recording `DatagramTransport` decorator sits between each
facade and the wire; the clock is stepped by hand at 60 Hz, so the capture is identical on every run.
Exception: the recorder deliberately rewrites the Host's 4th datagram into the opposite byte order
to simulate a big-endian Host and show the Byte Swap field; the library itself always sends native
order.
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

Nineteen datagrams, 16.67 ms apart (60 Hz), the Host's reply carrying the same timestamp as the
IG frame it answers (the clock does not move between them): IG Start of Frame (SOF), then the
Host's IG Control.

| Frames | What |
|---|---|
| 1 | IG SOF frame 0 (Reset/Standby, database 0); shown as Unknown by the dissector, see the caveats |
| 2 | The Host's probe IG Control (requests Operate and database 1); the Host repeats database 1 in frames 4 and 6 |
| 3, 5 | SOF IG frames 1-2 in Reset/Standby (the IG has accepted the request but is not ready) |
| 7-17 odd | SOF in Operate from IG frame 3; database -1 (loading) in frames 7, 9, 11, then 1 (loaded) from frame 13 |
| 8 | IG Control host frame 3 in the **foreign** byte order, rewritten by this example (not the library): Byte Swap shows `0x8000` (Big-Endian) |
| 19 | The Host's closing IG Control: Reset/Standby (ICD 2.8) |

Every IG Control's *Last IG Frame Number* equals the SOF just received; every SOF's *Last Host Frame
Number* equals the previous IG Control's *Host Frame Number*. Minor Version is 3 throughout.
`examples/wireshark/check_pcap.sh` asserts the counts (19 datagrams, 18 named, 1 foreign, 0 malformed).

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
