#!/usr/bin/env python3
"""Decode the Wireshark corpus with tshark and compare every field with what was encoded.

    check_tshark.py --generator <make_wireshark_corpus> --out <dir> [--tshark <path>] [--quiet]
    check_tshark.py --corpus <dir-with-corpus.pcap-and-expected.json> [--tshark <path>]

Python 3 standard library only. Exit status 0 when every datagram decodes to the encoded values
(known dissector issues must still show the documented wrong value, see dissector_map.py).
"""

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dissector_map  # noqa: E402

HOST = "10.0.0.1"
IG = "10.0.0.2"


DEFAULT_PREFS = ["-o", "cigi.host:" + HOST, "-o", "cigi.ig:" + IG]


def run_tshark(tshark, pcap, extra, prefs=None):
    """Run tshark (by default with the Host/IG preferences) and return stdout; raise on failure."""
    command = [tshark, "-r", pcap] + (DEFAULT_PREFS if prefs is None else prefs) + extra
    done = subprocess.run(command, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise RuntimeError("tshark failed: %s\n%s" % (" ".join(command), done.stderr))
    return done.stdout


def keep_pairs(pairs_list):
    """json.loads object_pairs_hook that keeps the (key, value) list as is, so repeated keys
    (one 'cigi.entity_control' per packet) are not collapsed into a dict."""
    return pairs_list


def flatten(tree):
    """All leaf fields of a packet tree (sub-trees such as flags_tree merged), name -> string."""
    leaves = {}
    for key, value in tree:
        if isinstance(value, list):
            leaves.update(flatten(value))
        else:
            leaves[key] = value
    return leaves


def lookup(pairs_list, key):
    for name, value in pairs_list:
        if name == key:
            return value
    return None


def decode_frames(tshark, pcap, prefs=None):
    """frame number -> {'packets': [(tree_name, leaves)], 'info': str, 'text': str}."""
    raw = run_tshark(tshark, pcap, ["-T", "json"], prefs)
    frames = {}
    for entry in json.loads(raw, object_pairs_hook=keep_pairs):
        layers = lookup(lookup(entry, "_source"), "layers")
        number = int(lookup(lookup(layers, "frame"), "frame.number"))
        cigi = lookup(layers, "cigi") or []
        packets = [(k, flatten(v)) for k, v in cigi if isinstance(v, list)]
        frames[number] = {"packets": packets, "text": json.dumps(entry)}
    infos = run_tshark(tshark, pcap, ["-T", "fields", "-e", "frame.number", "-e", "_ws.col.info",
                                      "-E", "separator=|"], prefs)
    for line in infos.splitlines():
        number, _, info = line.partition("|")
        frames[int(number)]["info"] = info
    return frames


def f32(value):
    """`value` rounded to the nearest IEEE single."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def ulps_apart(a, b):
    """Distance in units in the last place between two finite doubles (as ordered integers)."""
    def ordered(x):
        bits = struct.unpack("<q", struct.pack("<d", x))[0]
        return bits if bits >= 0 else -(bits & 0x7FFFFFFFFFFFFFFF)
    return abs(ordered(a) - ordered(b))


def close(kind, expected, shown):
    """f32: tshark's text, rounded to single, equals the encoded single exactly. f64: within 1 ulp
    (tshark prints doubles with %g-style rounding, so the last digit may differ)."""
    if math.isnan(expected) or math.isinf(expected):
        return False
    got = float(shown)
    if kind == "f32":
        return f32(got) == expected
    return ulps_apart(got, expected) <= 1


def word_bytes(value, endian):
    return ":".join("%02x" % b for b in struct.pack("<I" if endian == "little" else ">I", value))


def compare(kind, expected, shown, endian):
    """True if the dissector's string `shown` agrees with the encoded value."""
    if kind == "int":
        return int(shown) == expected
    if kind == "bool":
        return int(shown) == (1 if expected else 0)
    if kind in ("f32", "f64"):
        return close(kind, expected, shown)
    if kind == "word":
        return shown == word_bytes(expected, endian)
    raise ValueError("unknown kind " + kind)


class Report:
    def __init__(self):
        self.per_type = {}
        self.errors = []
        self.priming = []

    def row(self, ptype):
        return self.per_type.setdefault(ptype, {"packets": 0, "fields": 0, "known": 0, "bad": 0})

    def fail(self, message):
        self.errors.append(message)


def check_packet(report, frame, index, ptype, expected, leaves, endian):
    row = report.row(ptype)
    row["packets"] += 1
    where = "frame %d packet %d (%s)" % (frame, index + 1, ptype)
    mapped = [spec[0] for spec in dissector_map.PACKETS[ptype] if spec[0] is not None]
    missing = sorted(set(expected) - set(mapped))
    extra = sorted(set(mapped) - set(expected))
    if missing or extra:
        report.fail("%s: field coverage differs: encoded but not in dissector_map.py: %s; "
                    "in dissector_map.py but not encoded: %s" % (where, missing, extra))
        row["bad"] += 1
        return
    for spec in dissector_map.PACKETS[ptype]:
        our, theirs, kind = spec[0], spec[1], spec[2]
        known = spec[3] if len(spec) > 3 else None
        shown = leaves.get(theirs)
        if shown is None:
            report.fail("%s: dissector shows no %s" % (where, theirs))
            row["bad"] += 1
            continue
        row["fields"] += 1
        if kind == "magic":
            want = "0x0080" if endian == "little" else "0x8000"
            if shown != want:
                report.fail("%s: byte swap magic %s, expected %s" % (where, shown, want))
                row["bad"] += 1
            continue
        value = expected[our]
        if known is not None:
            row["known"] += 1
            if shown != str(known["wireshark"]):
                report.fail("%s: %s known issue expected Wireshark value %r, got %r (%s) - "
                            "update dissector_map.py and the discrepancy log if it was fixed" %
                            (where, our, known["wireshark"], shown, known["note"]))
                row["bad"] += 1
            continue
        if not compare(kind, value, shown, endian):
            report.fail("%s: %s encoded %r, dissector shows %r (%s)" %
                        (where, our, value, shown, theirs))
            row["bad"] += 1


def check(tshark, corpus_dir):
    pcap = os.path.join(corpus_dir, "corpus.pcap")
    with open(os.path.join(corpus_dir, "expected.json"), encoding="utf-8") as handle:
        expected = json.load(handle)["messages"]
    frames = decode_frames(tshark, pcap)
    report = Report()
    sizes = {name: (opcode, size) for name, opcode, size in dissector_map.PACKET_TREES.values()}
    trees = {tree: name for tree, (name, _, _) in dissector_map.PACKET_TREES.items()}

    if sorted(frames) != [m["frame"] for m in expected]:
        report.fail("tshark saw frames %s, expected %s" %
                    (sorted(frames), [m["frame"] for m in expected]))
        return report
    for message in expected:
        number = message["frame"]
        frame = frames[number]
        if message.get("priming"):
            report.priming = [name for name, _ in frame["packets"]]
            continue
        if "Malformed" in frame["text"] or "_ws.expert" in frame["text"]:
            report.fail("frame %d: tshark reports an expert/malformed note" % number)
        total = sum(sizes[p["type"]][1] for p in message["packets"])
        arrow = "Host => IG" if message["direction"] == "host_to_ig" else "IG => Host"
        want_info = "%s (%d bytes)" % (arrow, total)
        if frame.get("info") != want_info:
            report.fail("frame %d: summary %r, expected %r" % (number, frame.get("info"), want_info))
        got = [trees.get(name, name) for name, _ in frame["packets"]]
        want = [p["type"] for p in message["packets"]]
        if got != want:
            report.fail("frame %d: dissector packets %s, expected %s" % (number, got, want))
            continue
        for index, (packet, (_, leaves)) in enumerate(zip(message["packets"], frame["packets"])):
            opcode, size = sizes[packet["type"]]
            # Wireshark prints the CIGI major version only in the first packet of a datagram (the
            # IG Control or Start of Frame, which is where the ICD puts it), never on the others.
            if index == 0 and leaves.get("cigi.version") != "3":
                report.fail("frame %d packet %d: cigi.version %s, expected 3" %
                            (number, index + 1, leaves.get("cigi.version")))
            if leaves.get("cigi.packet_id") != str(opcode) or \
                    leaves.get("cigi.packet_size") != str(size):
                report.fail("frame %d packet %d: id/size %s/%s, expected %d/%d" %
                            (number, index + 1, leaves.get("cigi.packet_id"),
                             leaves.get("cigi.packet_size"), opcode, size))
            check_packet(report, number, index, packet["type"], packet["fields"], leaves,
                         message["wire_endian"])
    return report


def observe_direction_preferences(tshark, pcap):
    """What the cigi.host / cigi.ig preferences change, as a sentence (informational only)."""
    trees = {n: [name for name, _ in f["packets"]] for n, f in decode_frames(tshark, pcap).items()}
    swapped = ["-o", "cigi.host:" + IG, "-o", "cigi.ig:" + HOST]
    notes = []
    for label, prefs in (("no preferences", []), ("swapped preferences", swapped)):
        frames = decode_frames(tshark, pcap, prefs)
        same = all([name for name, _ in f["packets"]] == trees[n] for n, f in frames.items())
        notes.append("%s: frame 2 summary %r, same packets decoded: %s" %
                     (label, frames[2]["info"], same))
    return "; ".join(notes)


def print_table(report, frames):
    print("%-28s %8s %8s %6s %5s" % ("packet", "packets", "fields", "known", "bad"))
    for ptype, row in sorted(report.per_type.items()):
        print("%-28s %8d %8d %6d %5d" % (ptype, row["packets"], row["fields"], row["known"],
                                        row["bad"]))
    print("datagrams: %d (the priming datagram is not compared; Wireshark showed it as %s)" %
          (frames, ", ".join(report.priming) or "nothing"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generator", help="path to make_wireshark_corpus")
    parser.add_argument("--out", help="directory for the generated corpus")
    parser.add_argument("--corpus", help="existing corpus directory (skips generation)")
    parser.add_argument("--tshark", default=shutil.which("tshark"))
    args = parser.parse_args()
    if not args.tshark:
        print("error: tshark not found; install Wireshark or pass --tshark", file=sys.stderr)
        return 2
    corpus = args.corpus
    if corpus is None:
        if not args.generator or not args.out:
            parser.error("give --corpus, or --generator and --out")
        subprocess.run([args.generator, args.out], check=True, stdout=subprocess.DEVNULL)
        corpus = args.out
    report = check(args.tshark, corpus)
    with open(os.path.join(corpus, "expected.json"), encoding="utf-8") as handle:
        datagrams = len(json.load(handle)["messages"])
    print_table(report, datagrams)
    print("direction preferences -> " +
          observe_direction_preferences(args.tshark, os.path.join(corpus, "corpus.pcap")))
    for error in report.errors:
        print("MISMATCH:", error, file=sys.stderr)
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
