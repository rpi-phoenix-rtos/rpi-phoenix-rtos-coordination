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
