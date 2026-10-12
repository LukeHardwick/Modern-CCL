# Interoperability discrepancies

Where an independent implementation (Wireshark's CIGI dissector, later the original CCL) disagrees
with the CIGI 3.3 ICD. The ICD (with the Appendix C errata) wins; the library is not changed to
match. T10 and T11 extend this file.

## Wireshark 4.6.9 CIGI dissector (tshark, found in T02c)

Capture: `examples/wireshark/make_pcap.sh` (IG Control and Start of Frame only, version "From
Packet", `cigi.host`/`cigi.ig` set). "Match" = the value Wireshark shows equals what we encoded.

| Field | ICD section | Our value | Wireshark's value | Verdict |
|---|---|---|---|---|
| Major Version (both packets) | 4.1.1, 4.2.1 | 3 | 3 | match |
| Minor Version (both packets) | 4.1.1, 4.2.1 | 3 | 3 | match |
| Byte Swap Magic, native order | 2.5 | 0x8000 written natively (bytes 00 80 on LE host) | Little-Endian (0x0080) | match (receiver-makes-right) |
| Byte Swap Magic, foreign-order IG Control | 2.5 | bytes 80 00 | Big-Endian (0x8000); all other fields correct | match |
| Host Frame Number (IG Control) | 4.1.1 | 1..6 | 1..6, also in the foreign-order frame | match |
| Last IG Frame Number (IG Control) | 4.1.1 | 1..6 | 1..6, but labelled "IG Frame Number" | match; label is imprecise |
| IG Frame Number (SOF) | 4.2.1 | 1..6 | 1..6 | match |
| Last Host Frame Number (SOF) | 4.2.1 | 0..5 | frame 1 not decoded (below); 1..5 for the rest | match |
| Timestamp (both packets) | 4.1.1, 4.2.1 | ticks of 10 us, e.g. 1666 for 16.66 ms | 1666, labelled "(microseconds)" | Wireshark label wrong; ICD wins |
| Timestamp Valid | 4.1.1, 4.2.1 | 1 | Valid | match |
| IG Mode (both packets) | 4.1.1, 4.2.1 | 0 for frames 1-3, 1 afterwards (IG Control: Operate requested at host frame 3) | Reset/Standby, Operate as encoded | match |
| Database Number | 4.1.1, 4.2.1 | IG Control 1; SOF -128, -1, 1 | same | match |
| IG Status Code (SOF) | 4.2.1 | 0 | 0 | match |
| Earth Reference Model (SOF) | 4.2.1 | 0 | WGS 84 | match |
| Extrapolation/Interpolation Enable (IG Control) | 4.1.1 | 0 | Disabled | match |
| Packet Size | 4.1.1, 4.2.1 | 24 | 24 | match |
| First datagram of a capture, version "From Packet" | 2.5 | valid 3.3 SOF (T02c) or IG Control (T10) | "Unknown (24 bytes)", whichever of the two it is; the next datagram decodes (a SOF teaches the version as well as an IG Control; corrected in T10) | Wireshark limitation; ICD wins |
| Version forced to "CIGI 3" | 4.1.1, 4.2.1 | 24-byte packets | assumes the 3.0/3.1 16-byte layout; trailing 8 bytes parsed as a bogus packet, "Malformed: Packet offset does not match packet length" | Wireshark limitation (3.2/3.3 size not supported in this mode); ICD wins |
| `cigi.version` on non-frame packets (T10 checker) | 4.1.1, 4.2.1 | major version 3 in the frame packet only | tshark 4.6.9 shows `cigi.version` only inside the IG Control / SOF tree, so the checker compares it on packet 1 of each datagram | Wireshark presentation; no conflict |

No library discrepancy was found: every field Wireshark decodes agrees with the ICD-derived layout.

## CIGI 3.3 ICD internal inconsistencies (found in T03)

| Item | ICD section | Issue | Our choice |
|---|---|---|---|
| Component Data: word order of a 64-bit value | 4.1.4, Figs. 44-46 | The "m (MSW)" / "m (LSW)" labels in Figs. 44 and 46 (first word = MSW) contradict the ICD's own arithmetic: the procedure takes beta = m truncated after bit 32 (the low bits) and gamma = m >> 32 (the high bits), and Fig. 46 places beta = 0 in word 3 and gamma = 0x40590000 in word 4. Unchanged in 3.0-3.2; no erratum. | Low 32 bits in word i, high 32 bits in word i + 1 (procedure and Fig. 46 placement agree). To be verified against the original CCL in T11. |

## T10: full MVP corpus through Wireshark 4.6.9 (`ctest -L wireshark`)

`tests/interop/wireshark/` decodes 33 datagrams (plus one priming datagram; 12 packet types, both
directions, native and fabricated opposite byte order) with `tshark -T json`: 1404 field checks,
**no value differs from what we encoded**, and no library change was needed. The ICD was consulted for
every observation below; none is an error on our side.

| Topic | ICD section | Our value / behaviour | Wireshark's value / behaviour | Verdict |
|---|---|---|---|---|
| Minor Version of our packets | 4.1.1, 4.2.1 | 3 | "Minor Version: 3" in IG Control and Start of Frame; "CIGI Version: 3" | match |
| Component Control, 64-bit values | 4.1.4, Figs. 44-46 | double 100.0 (0x4059000000000000) as word 1 = 0x00000000, word 2 = 0x40590000 (low 32 bits first, see the T03 section above) | Component Data 1-6 are opaque byte strings in wire order (`00:00:00:00`, `00:00:59:40` on a little-endian wire); no 16/32/64-bit or float interpretation, in either byte order | Wireshark cannot adjudicate the word order; our choice stands pending T11 against the CCL. Per-word bytes match in both byte orders |
| Component Class 14 and 15 (new in 3.3) | 4.1.4, Table 10 | 6-bit field, 14 Symbol Surface, 15 Symbol | decoded and labelled "Symbol Surface" / "Symbol" | match |
| Yaw 0..360 | 4.1.2, 4.1.6, 4.1.16 | 360.0 accepted and sent; range is 0..360, not -180..180 | shows 360 exactly (Entity, Articulated Part, View Control) | match |
| HAT/HOT Update Period | 4.1.24 | 0, 1, 100, 200, 201 | same values | match |
| Host Frame Number LSN | 4.2.2, 4.2.3 | 0..15 in bits 7-4 of byte 4 | same values (shown inside the flags sub-tree) | match |
| HAT/HOT Response Type, Valid | 4.2.2 | bit 1 / bit 0 | same | match |
| HAT/HOT Extended Response | 4.2.3 | no Response Type field (the ICD has none) | none shown | match |
| Celestial Sphere Date | 4.1.9 | MMDDYYYY decimal, e.g. 10082026, 1012000 (1 Jan 2000) | raw number, labelled "Date (MMDDYYYY)" | match |
| Foreign byte order, every packet type | 2.5, 2.7 | every multi-byte field reversed, magic bytes 80 00 | all 12 packet types decode to the same values; Byte Swap "Big-Endian (0x8000)" (native: "Little-Endian (0x0080)") | match |
| Host / IG preferences (`cigi.host`, `cigi.ig`) | 2.3 | n/a | change only the summary label ("Host => IG", the opposite when swapped, or `10.0.0.1 => 10.0.0.2` when unset); packets are decoded by opcode and identically in all three settings | Wireshark behaviour, no impact |
| IG Mode 3 (Offline Maintenance), Start of Frame | 4.2.1 | 3 | "Offline Maintenance (3)" | match |
| Pixel Replication 4..7, View Type 0..7 | 4.1.21 | all eight values sent | 4..7 labelled "Defined by IG"; View Type shown as the number | match |
| Timestamp label | 4.1.1, 4.2.1 | 10 us ticks | the same number labelled "Timestamp (microseconds)" | Wireshark label wrong (as in T02c) |
| IG Control, Last IG Frame Number label | 4.1.1 | field at bytes 16-19 | labelled "IG Frame Number" (as in T02c) | label imprecise |
| IPv4 / UDP checksums written by `PcapWriter` | RFC 791, 768 | computed | `ip.check_checksum` / `udp.check_checksum` enabled: status "Good" (a unit test, `TsharkRoundTrip`) | match |

Every MVP field has a dissector counterpart. Wireshark does not show reserved bytes or bits, so
reserved-field errors cannot be seen through it; the unit tests cover them (0xFF-filled buffers).
