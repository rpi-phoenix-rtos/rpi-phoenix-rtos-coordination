# WiFi throughput — first real numbers (2026-09-02)

The lwip `wifi43455` netif holds a DHCP lease over the air, so throughput is
finally measurable. These are the first numbers, the method, and where the time
actually goes.

## Method

`scripts/wifi-perf-host.py` on the host, `/root/wifi-perf.py` on the Pi (staged
into the netboot root, not committed — it is a two-screen test script). A plain
TCP transfer each way over the WiFi subnet:

- The Pi **binds its source address to the WiFi netif's own IP** before
  connecting. Both interfaces are up, and without that bind a wrong source
  address would quietly measure the gigabit link instead.
- Rates are computed from the first byte moved, so Python startup on the Pi does
  not pollute the result, and **both ends report independently** — they agreed to
  three decimal places in every run, which is what makes these numbers credible.
- The air monitor's broadcast probe sender must be **off**: 5 × 1472 B/s of
  background broadcast is not much, but it is not nothing.

## Numbers, and why most of them prove less than they look

| Config | Pi → host (TX) | host → Pi (RX) | rx_err |
|---|---|---|---|
| RX idle poll 1500 µs | 0.58 MB/s | — | 1 |
| RX idle poll 200 µs | 1.73 MB/s | 0.14 MB/s | 1 |
| one block-mode command per RX frame | 1.07 MB/s | 0.03 MB/s | **433** |
| daemon waits for a frame (3 threads + bus mutex) | 0.92 MB/s | 0.29 MB/s | — |
| … plus a 2 ms spin before sleeping | 1.17 MB/s | 0.19 MB/s | — |
| reverted — **byte-for-byte the same code as row 2** | **0.66 MB/s** | 0.27 MB/s | — |

Wired genet, for scale: ~30 MB/s read / ~20 MB/s write.

**The last row is the important one.** It runs the same code as row 2 and
measured 0.66 against 1.73 — a 2.6× spread. Single-run throughput A/B on this
link is therefore not a valid method, and the middle rows cannot support the
conclusions they appear to. They were collected one run per config; they need
n ≥ 3 (or a much longer transfer) before any of them means anything.

Two results *do* survive, because they average thousands of samples inside a
single run rather than comparing runs:

- **Per-frame cost in the daemon: 121–132 µs to transmit, 178–183 µs to
  receive, 22 µs for an empty probe.** The radio and the SDIO transfers are not
  the bottleneck.
- **1.1 million empty probes in one run, burning 24.9 s of bus time.** That is
  the actual waste, and it is a count, not a rate.

The block-mode row is also decided by a counter rather than a rate: `rx_err`
went 1 → 433, which is stream desynchronisation from over-reading past the end
of a frame (block mode must move whole blocks). That is why the chunked byte
mode stays.

What was tried and reverted, for adding complexity with no benefit that survives
the noise: a bounded waiting read in the daemon, a spin before sleeping, and a
multi-threaded message loop with a bus mutex. The mutex alone tripled the cost
of an empty probe (22 → 72 µs), and the timing instrumentation itself is now
opt-in (`-DWIFI_STATS_TIMING=1`) because two `clock_gettime` calls per probe at
~5000 probes/s are not free.

## Next levers, in expected-value order

0. **Fix the measurement first.** Repeat each config n ≥ 3 times, or transfer
   long enough to average the link's variance out, and prefer the daemon's own
   per-frame counters over end-to-end MB/s. Everything below is unfalsifiable
   without this.
1. **Interrupt-driven RX** — SDIO `CARD_INTR` so a frame wakes the daemon
   instead of being polled for. A *bounded waiting read* was tried as a cheap
   stand-in and is not the same thing: it still polls, just on the other side of
   the message boundary.
2. **Batch frames per message** — return several frames from one `read()`,
   length-prefixed, amortising the message round trip and the window setup.
3. **DMA for F2** — the transfers are PIO today, word at a time with status
   polling. This is the floor under everything else.

Not yet measured: the negotiated PHY rate (the firmware knows it; `wifi mtu`
should report it), and NFS-over-WiFi, which is only interesting once the above
lands.


---

## Follow-up: what the RX errors actually were (2026-09-02, later)

Splitting `rx_err` by cause settled it in one run. Every single RX error was
`-32`: **this driver rejecting frames the firmware had announced**, up to
**9248 bytes** against a 2048-byte cap — 184 of 1016 receives, so roughly **18%
of all inbound frames were being dropped** and left for TCP to retransmit. Zero
parse errors, zero transport failures.

That reads like a textbook undersized buffer, and the fix looks obvious. It is
wrong, which is why it was worth measuring rather than assuming:

| cap | rx_err | rx_garbage | RX | max announced |
|---|---|---|---|---|
| 2048 | 184 | 876 | 0.10–0.29 MB/s | 9248 |
| 16384 | **0** | **0** | **0.03 MB/s** (3 runs, identical) | **0** |

Raising the cap eliminated every error — and made RX five to ten times *worse*,
consistently. The decisive detail is the last column: with the bigger buffer, no
oversize frame ever arrived again. Those 9248-byte headers were never real
aggregation. They are what a receive stream that has lost frame alignment looks
like, and the controller reset on the `-32` rejection path was quietly
resynchronising it. Remove the rejection and the recovery goes with it.

So the cap stays at 2048 and the real fix is the stream itself — SDPCM glom
handling, or a resync strategy that does not depend on a rejection happening to
fire. Also disproved along the way: the firmware's credit window is never the
limit (`blocked=0`, `avail=39` across a full transfer), and the 64-consecutive
bad-header resync fires almost never (`resyncs=0–1`), so neither is the cause.

One more datum worth keeping: a short single transfer measured **3.56–3.61 MB/s
TX**, twice the previous best. The link is capable of far more than the medians
suggest; what degrades is sustained transfer, which is consistent with a stream
that loses alignment and recovers by accident.


---

## Root cause: the firmware is glomming, and we drop every aggregate

Recording the SDPCM channel of the oversize frames settled it in one run:

```
rxxfer hdr=0 oversize=198 body=0 max_announced_len=9248 chan=3 (cap 2048)
```

**Channel 3 is `SDPCM_GLOM_CHANNEL`.** The firmware bundles several received
frames into one superframe — measured at 9248 bytes, and later up to **23072** —
and this driver has no de-aggregation, so every one is dropped. That is the
~18% inbound loss, and it also explains the 16 KiB experiment above: with a
bigger buffer the superframes were read successfully and then discarded anyway
for not being channel 2, which is why RX got *worse*, not better.

**It cannot be turned off.** `bus:rxglom = 0` is accepted by the firmware
(`rc 0`, after being moved past the first command of the join — as the very
first command it fails with a transport error) and simply ignored: superframes
keep arriving. brcmfmac only ever sets this iovar to 1, to *enable* glom, and
treats failure as acceptable; the chip's default is evidently on.

So the frames must be split. A superframe is its own SDPCM header followed, from
its `data_offset`, by back-to-back subframes each carrying a full SDPCM header.
Splitting them is worth more than just recovering the lost 18%: one bus transfer
would feed several frames, amortising exactly the per-frame overhead that limits
this driver.

**Status: attempted and reverted.** The first implementation does not pass
traffic (TCP resets immediately, `rx_ok 7`, `glom supers 0`), so master keeps the
known-good driver — re-verified after the revert at **TX 3.51 MB/s**, traffic
normal. The work is preserved and published on branch `wip/rpi4-wifi-glom` in
phoenix-rtos-devices, with the counters (`glom supers/subframes/bad`) already in
place to debug it. One bug of this class was already found and fixed during the
attempt: the frame copy-out still read the old buffer after the receive buffer
changed, which delivered stale bytes as ethernet frames.


---

## The glom wire format, captured from the hardware

Two one-shot dumps settled what the aggregation actually looks like, replacing
several rounds of guessing. Both frames are SDPCM channel 3; the SW header's
`0x80` bit (brcmfmac `SDPCM_GLOMDESC`) tells them apart.

**Descriptor** — `len=22 doff=12`, `buf[5]=0x83` (channel 3 **+** descriptor bit):

```
16 00 e9 ff | 3d 83 00 0c | 00 67 00 00 | 60 00 60 00 60 00 60 00 60 00
^HW len 22   ^seq, chan|desc, doff 12    ^credits      ^five u16 strides = 96 each
```

Its payload is a list of subframe **slot sizes**, not data. Walking it as data
was the first bug.

**Superframe** — `len=480 doff=12`, `buf[5]=0x03` (channel 3, bit clear):

```
hdr  e0 01 1f fe | 35 03 00 0c | 00 4b 00 00
sub  48 00 b7 ff | 35 02 00 0e | 00 4b 00 00 | 00 00 | 20 00 00 00 | <ethernet>
     ^HW len 72    ^chan 2, data_offset 14    ^credits  ^2b pad     ^BDC
```

So each subframe carries a **full SDPCM header of its own**, then **two bytes of
pad** (its `data_offset` is 14, not 12), then the BDC header and the frame. The
first subframe parses exactly: 72 − 14 − 4 = 54 bytes, precisely a TCP ACK.

## Where it still fails

The walker extracts **one good subframe per superframe and then hits a bad
header, every time** (`subframes N, bad N`). That is what landing mid-header
looks like, so the spacing is still wrong. Tried, none of them fixed it:

- stride = the subframe's own HW length (72)
- stride = that length rounded up to 4
- stride = the preceding descriptor's slot size (96)

And one that actively broke reception, reverted: padding the superframe **read**
up to the SDIO block size. `roundup(len, blocksize)` is what
`brcmf_sdio_hdparse` expects, but that belongs to block-mode transfers — this
driver reads the FIFO in byte mode, which pops exactly what is asked for, so
rounding up reads past the frame and desynchronises the stream (receive died
within a handful of frames).

Note `480 − 12 = 468`, which divides evenly by neither 72 nor 96, so the real
spacing is still unaccounted for. **Next step: dump the bytes at the failing
offset instead of reasoning about the stride** — the answer is in those 468
bytes. The dumps and counters to do it are already on branch
`wip/rpi4-wifi-glom`; master keeps the known-good driver.


---

## RESOLVED: glom de-aggregation — RX 0.14 → 3.0 MB/s

The stride was in the bytes, not in the reasoning. Dumping a whole superframe
and decoding it offline gave the answer immediately:

```
valid subframe headers at: 12, 96, 192, 288   (len=72, chan=2, doff=14 each)
gaps:                      84, 96, 96
superframe len 384; its descriptor listed 96-byte slots summing to exactly 384
```

**Subframes live in the descriptor's SLOTS, measured from the start of the
superframe** — slot k spans `[sum(lens[0..k-1]), +lens[k])`. Slot 0 also carries
the superframe's own 12-byte header, which is why the first gap is 84 and the
rest are 96. Stepping by each subframe's own length (72) lands mid-header after
the first one — exactly the "one good subframe per superframe, then a bad one"
signature that three earlier guesses produced.

### Result, three runs each

| | TX | RX |
|---|---|---|
| before | 0.22–0.66 MB/s median (0.14–3.6 spread) | 0.13–0.29 MB/s |
| **after** | **3.27 MB/s** (3.02–3.27) | **3.00 MB/s** (3.00–3.02) |

RX is roughly **20× faster**, TX ~5×, and — the part worth noting — **the wild
run-to-run variance is gone**. That variance was never the radio: it was frame
loss driving TCP retransmit storms. Every superframe now de-aggregates cleanly
(740 superframes → 3595 subframes, `bad=0`, `rx_err=0`).

Wired genet remains ~30/20 MB/s, so WiFi is now within ~10× of the wired link
rather than ~200×, and the per-frame overhead identified earlier is the next
ceiling — though glom already amortises much of it, since one bus transfer now
feeds several frames.
