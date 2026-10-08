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
| First Start of Frame of a capture, version "From Packet" | 2.5 | valid 3.3 SOF | "Unknown (24 bytes)": the version is only learned from a later IG Control | Wireshark limitation; ICD wins |
| Version forced to "CIGI 3" | 4.1.1, 4.2.1 | 24-byte packets | assumes the 3.0/3.1 16-byte layout; trailing 8 bytes parsed as a bogus packet, "Malformed: Packet offset does not match packet length" | Wireshark limitation (3.2/3.3 size not supported in this mode); ICD wins |

No library discrepancy was found: every field Wireshark decodes agrees with the ICD-derived layout.

## CIGI 3.3 ICD internal inconsistencies (found in T03)

| Item | ICD section | Issue | Our choice |
|---|---|---|---|
| Component Data: word order of a 64-bit value | 4.1.4, Figs. 44-46 | Fig. 44 labels the first word "m (MSW)" and the prose calls the first word the MSW, but the worked example (Fig. 46, m = 100.0 = 0x4059000000000000) puts 0x00000000 in word 3 and 0x40590000 in word 4, i.e. the least significant half first. | Follow the worked example's wire bytes: low 32 bits in word i, high 32 bits in word i + 1. Verify against the original CCL in T11; flip in `component_data.hpp` if it disagrees. |
| Yaw range | 4.1.2, 4.1.6 | ICD gives Yaw 0..360 while Roll is -180..180 (the T03 brief said -180..180). | `validate()` enforces 0..360 as written, so a negative yaw is rejected; normalise before sending. |
