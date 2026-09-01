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

## Numbers

| Config | Pi → host (TX) | host → Pi (RX) | rx_err |
|---|---|---|---|
| RX idle poll 1500 µs | 0.58 MB/s | — | 1 |
| RX idle poll 200 µs | **1.73 MB/s** | 0.14 MB/s | 1 |
| …plus one block-mode command per RX frame | 1.07 MB/s | 0.03 MB/s | 433 |

Wired genet, for scale: ~30 MB/s read / ~20 MB/s write.

## What the numbers say

**TX was ACK-latency bound, not radio bound.** TCP cannot advance its window
faster than ACKs are picked up, so an idle RX poll of N µs puts a ceiling near
one frame per N µs. At 1500 µs we measured 402 frames/s = 2.49 ms per frame —
almost exactly the poll interval plus processing. Dropping the poll to 200 µs
tripled TX for a one-constant change. The honest reading is that this is a
workaround: the fix is an event-driven read on the daemon side so the RX thread
sleeps until a frame exists instead of polling.

**RX is per-frame cost, not latency.** 0.14 MB/s is ~97 frames/s, ~10 ms per
frame — far more than the 200 µs poll explains. Each received frame costs a
message round trip, a backplane window setup (three CMD52s), and four PIO SDIO
transfers (a 12-byte header read plus three 512-byte chunks), every one of them
spinning on SDHCI status registers.

**One block-mode command per frame is slower, not faster** — measured, against
expectation. It moves one command instead of four, but block mode must transfer
whole blocks, so it pops `ceil(len/64)*64` bytes, reads past the end of the
frame and desynchronises the stream. `rx_err` jumping 1 → 433 is that
desynchronisation. The toggle (`F2_RX_ONE_CMD`) is kept so the comparison can be
re-run, defaulting to the byte-mode chunks that won.

## Next levers, in expected-value order

1. **Event-driven RX** — a read that blocks in the daemon until a frame arrives
   (or a short bounded wait), removing both the poll latency and the wasted
   empty-poll SDIO traffic. Addresses TX ceiling and RX cost together.
2. **Batch frames per message** — return several frames from one `read()`,
   length-prefixed, amortising the message round trip and the window setup.
3. **DMA for F2** — the transfers are PIO today, word at a time with status
   polling. This is the floor under everything else.

Not yet measured: the negotiated PHY rate (the firmware knows it; `wifi mtu`
should report it), and NFS-over-WiFi, which is only interesting once the above
lands.
