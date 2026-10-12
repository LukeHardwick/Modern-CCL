# Trying the library without a real IG

Two small programs let you watch a complete CIGI 3.3 exchange on one machine:

- **`demo_ig`** - a mock Image Generator without graphics. It sends Start of Frame, follows the
  start-up sequence of ICD 2.8, keeps a model of everything the Host commands (the same
  `cigi_mock_ig` logic as the 3-D viewer) and answers HAT/HOT requests from a synthetic terrain.
- **`demo_host`** - a Host built on the `cigi::Host` facade (a longer worked example than
  [examples/host_minimal.cpp](../../examples/host_minimal.cpp)).

Both log readably, print a summary when they stop, and can record every datagram to a pcap file.

## Build and run

    cmake --preset clang-debug
    cmake --build --preset clang-debug --target demo_ig demo_host
    scripts/demo.sh 10                      # both on loopback for 10 s, both summaries printed

Or by hand, in two terminals (`build/clang-debug/apps/demo_ig/demo_ig`, `.../demo_host/demo_host`):

    demo_ig   --listen-port 8004
    demo_host --ig-addr 127.0.0.1 --ig-port 8004 --listen-port 8005

Press Ctrl-C to stop either one. `demo_host` exits 0 only if it reached Operate, received HAT/HOT
responses and saw no protocol error; `demo_ig` exits 0 unless its socket failed.

`scripts/demo.sh [seconds] [demo_ig flags...]` passes extra flags to the IG, e.g.
`scripts/demo.sh 10 --drop 0.1 --latency 20`. Set `PCAP_DIR=/some/dir` to record both sides.

## What `demo_host` does

1. Waits for the first Start of Frame and requests Operate (the facade sends IG Control).
2. In Operate, every frame: Entity Control for an ownship (entity 0) flying a clockwise circle of
   1.5 km radius at 80 m/s over the IG's database, and Articulated Part Control spinning part 1.
3. At the start of Operate: View Definition, and View Control attaching view 0 to the ownship;
   Celestial Sphere Control with a clock that runs one minute per second (repeated every 30 frames).
4. HAT/HOT: a one-shot HAT (id 1, frame 1), a periodic *extended* request relative to the ownship
   (id 3, every 30th frame, from frame 2) and a one-shot HOT (id 2, frame 30).
5. Prints a status line each second and a summary at exit.

If the link is lost or the IG restarts, the whole scenario is sent again when Operate returns.

Options: `--ig-addr`, `--ig-port`, `--listen-port`, `--sync` / `--async` (with `--rate`),
`--peer-timeout <ms>`, `--duration <s>`, `--pcap <file>`, `--log-level`.

## What `demo_ig` does

- Sends Start of Frame at `--rate` Hz (default 60). With no `--host-addr` it learns its Host from
  the first valid Host message; until then nothing is sent.
- Stays in Reset/Standby for `--load-frames` frames after the Host requests Operate, then reports
  Operate. Entities, views, celestial time, articulated parts and HAT/HOT requests only take
  effect in Operate (ICD 2.8).
- Logs entity creation/destruction/attachment. `--table` prints the entity table every second.
- `--log-level debug` prints every packet sent (`->`) and received (`<-`), one line each.

Options: `--listen-addr`, `--listen-port`, `--host-addr`, `--host-port`, `--rate`, `--load-frames`,
`--duration <s>`, `--pcap <file>`, `--log-level`, `--table`.

### Fault injection

| Flag | Effect | What you should see in `demo_host` |
|---|---|---|
| `--refuse-operate` | every request for Operate is refused | stays in Standby; summary says `reached Operate: NO`, exit code 1 |
| `--pause-sof-after S --resume-sof-after T` | no Start of Frame between S and T seconds | `Operate -> Lost` after `--peer-timeout`, then back to Operate |
| `--restart-after S` | the IG forgets everything, as after a crash | link drops to Standby, Operate again, IG restart counted |
| `--drop P` / `--duplicate P` | each Start of Frame is lost / sent twice with probability P (0..1) | `Start of Frame lost` / `duplicate` counters rise, run still succeeds |
| `--latency MS` | every Start of Frame is delayed by MS milliseconds | slower responses |
| `--seed N` | seed of the dice above (runs repeat) | |

Faults act on what the IG *sends*. The IG learns its Host from the first valid Host message and
then ignores datagrams from any other sender (counted as "ignored" in its summary); the Host
likewise only accepts datagrams from `--ig-addr`:`--ig-port`. If the Host goes quiet for a second
the IG forgets it, so a Host restarted on another port is picked up again. The Host's `--peer-timeout` (default 1000 ms) decides how long a
silent IG takes to count as Lost, so with `--pause-sof-after 1 --resume-sof-after 2` use
`--peer-timeout 400` on the Host to see the Lost state.

## Reading the logs

    18:59:01.870 [ig] info IG listening on UDP port 8004 (60 Hz), Host learnt from its first message
    18:59:02.459 [ig] info Host requests mode Operate
    18:59:02.470 [host] info link WaitingForIg -> Operate
    18:59:02.470 [ig] info scene: entity 0 created (type 101)
    18:59:02.504 [host] info HAT id=1 valid=true height=900.0 m (LSN 0)
    18:59:02.520 [host] info HAT/HOT extended id=3 valid=true hat=435.5 m hot=64.5 m material=0 (LSN 3)
    18:59:02.882 [ig] info t=1s mode=Operate host=yes sof=60 (+60) rx=26 entities=1 hat/hot=2
    18:59:03.474 [host] info t=1s link=Operate sent=62 (+62) ig_frame=95 lost=0 responses=4 (+4) errors=0

- Host status line: `sent` messages to the IG (`+n` this second), `ig_frame` the last IG frame
  number received, `lost` Start of Frame messages missing from the sequence, `responses` HAT/HOT
  answers, `errors` protocol errors reported by the facade.
- IG status line: `mode` the mode it reports, `host` whether a Host message arrived within the last
  second, `sof` Start of Frame sent, `rx` datagrams received.
- HAT/HOT lines: `LSN` is the Host Frame Number LSN the IG echoed (ICD 2.3, 4.2.2; 0 for
  one-shots). HAT is height above terrain, HOT height of the terrain.
- Lines from the facade and sessions (`link ... ->`, `Host requests mode ...`, `IG mode ...`) are
  the protocol state changes.

## Recording a pcap and opening it in Wireshark

    demo_ig   --pcap ig.pcap
    demo_host --pcap host.pcap

Each file holds every datagram that program sent or received (raw-IPv4 frames, link type 101, with
valid IP and UDP headers), so it needs no capture privileges. On loopback the addresses are
127.0.0.1; for a peer elsewhere, a program bound to all interfaces shows its own address as
0.0.0.0 (use `--listen-addr` on the IG to get the real one). Open it in Wireshark; if the CIGI
dissector does not pick the traffic up automatically, use *Analyze > Decode As...* and map UDP port
8004 and 8005 to CIGI. See [docs/interop/wireshark.md](../interop/wireshark.md) for the dissector's
known quirks (for example the first datagram of a capture is shown as `Unknown`).

### Live capture on the loopback interface

Without `--pcap` you can capture while the demos run: choose `lo0` (macOS) or `lo` (Linux) in
Wireshark with the capture filter `udp port 8004 or udp port 8005`. On macOS capturing needs
Wireshark's **ChmodBPF** helper (installed by the Wireshark package; it gives your user access to
`/dev/bpf*`). Loopback frames have a different link-layer header than the files `--pcap` writes,
which Wireshark handles by itself.
