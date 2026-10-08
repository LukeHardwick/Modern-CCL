# Verifying our bytes with Wireshark / tshark

Wireshark's CIGI dissector is an independent implementation (treated as a black box: its source is
never consulted). `ctest -L wireshark` checks that it decodes every MVP packet we produce to the
values we put in. Tested with Wireshark 4.6.9; the CIGI 3.3 packets are decoded by its "CIGI 3"
code path.

## What is checked

| Piece | Where |
|---|---|
| `cigi::tools::PcapWriter` (classic pcap, raw IPv4 + UDP, correct checksums) | `tools/` |
| Corpus generator: `corpus.pcap` + `expected.json` | `tests/interop/wireshark/make_wireshark_corpus.cpp` |
| Our field name to dissector field name table | `tests/interop/wireshark/dissector_map.py` |
| Checker (`tshark -T json`, Python 3 standard library) | `tests/interop/wireshark/check_tshark.py` |
| ctest tests (label `wireshark`) | `wireshark_corpus`, `wireshark_cigi_frames`, `TsharkRoundTrip.*` |

The corpus has 34 datagrams: for each of the 12 MVP packets (IG Control, Start of Frame, Entity
Control, Articulated Part Control, Rate Control, Component Control, Celestial Sphere Control,
View Control, View Definition, HAT/HOT Request, Response, Extended Response) at least two packets
with distinctive values (negative numbers, maxima, every enum value, all flag combinations), each
packet type once in the sender's native byte order and once as a fabricated opposite-endian peer
(ICD 2.5). Host messages go 10.0.0.1:8005 to 10.0.0.2:8004, IG messages the reverse. The first
datagram is a priming message (see "Dissector behaviour").

The checker compares, per packet: packet ID and size, the packet sequence of each datagram, the
"Host => IG" / "IG => Host" summary, no malformed/expert notes, and every mapped field (integers
and bits exactly; floats with relative tolerance 1e-6, doubles 1e-9; Component Data words as raw
bytes in the message's wire order; the Byte Swap value). It prints a per-packet table, for example
`entity_control   8 packets  152 fields  0 known  0 bad`.

## Running it

```sh
cmake --preset clang-debug && cmake --build --preset clang-debug
ctest --preset clang-debug -L wireshark                  # needs tshark on PATH (or found by CMake)

# by hand, keeping the files:
build/clang-debug/tests/make_wireshark_corpus /tmp/corpus
python3 tests/interop/wireshark/check_tshark.py --corpus /tmp/corpus
tshark -r /tmp/corpus/corpus.pcap -o cigi.host:10.0.0.1 -o cigi.ig:10.0.0.2 -V    # full decode
```

`expected.json` lists, per datagram, the frame number, direction, wire endianness and the encoded
value of every field under our struct member names. To inspect it graphically open `corpus.pcap` in
Wireshark and set Preferences, Protocols, CIGI: Host `10.0.0.1`, IG `10.0.0.2`, Version "From
Packet", Byte Order "From Packet".

macOS: `brew install --cask wireshark` puts the tools in `/opt/homebrew/bin`; Debian/Ubuntu:
`apt install tshark`. Without tshark the tests are not registered (CMake simply skips them).

## Adding a packet (Phase B)

1. In `make_wireshark_corpus.cpp` add a `describe(const NewPacket&)` overload (field names and
   values) and a few messages that use it in `add_*_packets`.
2. In `dissector_map.py` add the dissector tree name to `PACKET_TREES` and one row per field to
   `PACKETS` (`our_name, dissector_name, kind`). Find the dissector's names with
   `tshark -G fields | grep cigi.` or with `tshark -r corpus.pcap -T json`.
3. Where the dissector is wrong according to the ICD, add the 4th tuple element
   (`{"note": ..., "wireshark": <its value>}`): the check then requires the dissector's wrong value,
   so a fixed Wireshark makes the test fail and the entry can be removed. Log it in
   `discrepancies.md` (ICD section, our value, Wireshark's value, reasoning). No such entry was
   needed for the MVP packets.

## Viewing live traffic

`PcapWriter` is reusable: the demo apps write a capture with `--pcap <file>` (T09), and any code can
do

```cpp
cigi::tools::PcapWriter pcap{"run.pcap"};
pcap.write_udp(now, host_endpoint, ig_endpoint, message.bytes());
```

To watch a real exchange between `demo_host` and `demo_ig` on one machine, capture the loopback
interface (macOS `lo0`, Linux `lo`; capturing needs the usual privileges, e.g. the ChmodBPF helper
installed with Wireshark on macOS) with the filter `udp port 8004 or udp port 8005` (or whatever
ports you passed). The Host/IG preferences only label the direction (see below), so with both apps on `127.0.0.1`
set them to the same address or leave them empty: the packets decode the same either way.
Writing a pcap with `--pcap` avoids the capture privileges.

## Dissector behaviour worth knowing

Full details and ICD adjudication in [discrepancies.md](discrepancies.md). In short:

- The first CIGI datagram of a capture is displayed as `Unknown` whatever it is (IG Control or Start
  of Frame); later datagrams decode. Hence the corpus's priming message.
- Keep Version on "From Packet": forcing "CIGI 3" assumes 16-byte frame packets.
- `cigi.host` / `cigi.ig` change only the summary label; packets are decoded by their opcode and
  the same way with the preferences unset or swapped.
- Component Data words are shown as opaque bytes in wire order; Wireshark applies no 16/64-bit or
  floating-point interpretation, so it cannot confirm the word order of 64-bit values.
- "Timestamp (microseconds)" is really our 10 us ticks.
