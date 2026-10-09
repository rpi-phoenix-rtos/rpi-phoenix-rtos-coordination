# F1 WiFi throughput: what bounds it, and the A/B to run (2026-10-08)

**Status:** analysis + knobs on devices branch `wifi-f1-agg` (`59121a4`, pushed to `publish`,
contains master `961544f`), compiled under the real flags; **not yet built into an image, no Pi
run.** `scripts/radio-ap-up.sh` takes `RADIO_AP_BAND` / `RADIO_AP_WIDTH` (defaults unchanged).

## What bounds throughput today (TX 4.45 / RX 4.47 MiB/s, 2.4 GHz HT20, 72 Mbit/s PHY)

Source: `artifacts/rpi4b-uart/rpi4b-uart-20261001-194729-g18-wifi.log` and the earlier F1 docs.

- **TX is at the HT20 air limit, with the Pi's own path right under it.** Going from a 25 MHz
  to a 41.67 MHz bus cut 66 µs of bus time per frame but saved only 36 µs; 512 B blocks cut
  18 µs more and saved nothing. One model fits all three runs: air ≈ 335 µs/frame
  (4.45 MiB/s), and the Pi path ≈ 205 µs + bus time ≈ 290–305 µs/frame at 41.67 MHz.
- **RX is bound by the Pi.** It rose 19 % with the clock alone. The RX thread almost never
  slept: 245 interrupt wakes for 13 570 frames. The firmware packed ~6.2 frames per glom
  superframe, so frames were queueing for the Pi.
- **The AP** runs HT20, because NetworkManager's `channel-width=auto` picks 20 MHz (`iw dev`:
  `width: 20 MHz`), and its txpower is **3 dBm**. The host's mt7925e can run the AP at 5 GHz
  VHT80: `iw list` shows Band 2 with VHT80 SGI, and ch36–48 have no DFS and no no-IR limits
  in regdomain PL. The Pi's NVRAM has `aa5g=1`.
- **Not the bound:**
  - **SDIO TX credits:** `avail=40`, `blocked=0`.
  - **RX glom:** `bad=0`.
  - **lwip:** `TCP_WND` is 64 240 with no window scaling. That is fine at 4.5 MiB/s with
    2–8 ms round trips, and becomes the ceiling around 8–12 MiB/s.
- **AMPDU and power save:** unknown, now readable.
- **Measurement:** both ends agree, and "MB/s" means MiB/s. Use 16 MiB transfers; 4 MiB is
  about a 1 s run.

**Expected:** 5 GHz VHT80 alone gives TX ~4.7–5.0 MiB/s (+5–15 %), with RX about flat. If
both directions jump by more than 50 %, the model is wrong and the air was the bound. The next
real lever is **several frames per IPC** (devices + lwip), on both sides.

## What the branch adds

| commit | change |
|---|---|
| `d8e8e6c` | `wifi stats` bustime gains `glom=<n> avg_us= avg_bytes=`: bus time of RX superframe reads |
| `2cf2406` | Each join runs: radio down → country + queued settings → up. New probes `wifi iovar get\|set`, `wifi ioctl get\|set`, `wifi chanspecs`, `wifi dump ampdu`. New line `WIFISTATS radio chanspec= ch= bw= band= pm=` |
| `1c85b51` | Every data request is timed. New line `WIFISTATS daemon busy=% of ms`. `WIFI_STATS_TIMING` is removed, so the 09-02 doc's mention of it is stale |
| `59121a4` | `bw_cap` is queued per band; `iovar set` takes up to 16 words |

User commands:

- `wifi country [PL|-]`
- `wifi bw 2g|5g 20|40|80`
- `wifi atjoin [clear|<iovar> <words>]`

Daemon arguments: `country=XX`, `bw2g=`, `bw5g=`.

Queued settings apply on the next join; `wifi leave` makes the netif rejoin within ~3 s. With
nothing queued, the join sends exactly what it sent before.

## The A/B (one image, one boot)

**Preparation:**

1. Merge `wifi-f1-agg` and build with `--scope core`. Gate with `strings -a`: `rpi4-wifi`
   must contain `JOIN-SETTINGS` and `daemon busy=`, and `wifi` must contain `chanspecs`.
2. Rename `/srv/phoenix-rpi4-nfs-gcc16/etc/wifi.conf.off` to `wifi.conf` for the run, then
   back afterwards. The PSK stays export-only.
3. `cp scripts/wifi-perf-pi.py /srv/phoenix-rpi4-nfs-gcc16/root/wifi-perf.py`.
4. On the host: `python3 scripts/wifi-perf-host.py 7777 16777216 9` (3 runs × 3 arms).

**Arm A: 2.4 GHz HT20.** psh, one command per line:

    wifi status
    wifi chanspecs
    wifi country
    wifi iovar get ampdu_tx
    wifi iovar get ampdu_rx
    wifi iovar get ampdu_ba_wsize
    wifi iovar get ampdu_mpdu
    wifi iovar get ampdu_rx_factor
    wifi iovar get bw_cap 2
    wifi iovar get bw_cap 1
    wifi ioctl get 85
    wifi dump ampdu
    wifi stats
    python3 /root/wifi-perf.py 10.43.0.1 7777 10.43.0.89 16777216 3
    wifi stats
    wifi dump ampdu

**Arm B: 5 GHz VHT80.**

1. On the Pi: `wifi country PL`, `wifi chanspecs` (it must list `5g/80`), `wifi bw 5g 80`.
2. On the host: `RADIO_AP_BAND=a RADIO_AP_CHAN=36 RADIO_AP_WIDTH=80mhz sudo -E ./scripts/radio-ap-up.sh`.
3. Repeat `wifi status` until it shows an address (1–2 min). The lease can change, so bind to
   the address it shows.
4. Run the same `stats` / perf / `stats` sequence as arm A.
5. **The arm counts only if** the Pi shows `radio … band=5g bw=80` with a phy rate above 72,
   `iw dev wlp3s0 info` shows 80 MHz, and `iw dev wlp3s0 station dump` shows VHT rates.

**Arm A′:** back to 2.4 GHz, for drift.

**Optional arm:** 2.4 GHz HT40 (`wifi bw 2g 40` on the Pi, `RADIO_AP_WIDTH=40mhz` on the
host). Coexistence with the networks on ch 4 and 8 may narrow it back to 20 MHz.

**Reading the results:**

- The per-request counters accumulate, so subtract "before" from "after".
- `busy=` includes Python startup, so treat it as a lower bound.
- iovar return codes:

| rc | meaning |
|---|---|
| -100 | not sent |
| ≤ -1000 | transport error (-1042 = no reply) |
| -2 | BADARG |
| -3 | BADOPTION |
| -4 | NOTUP |
| -5 | NOTDOWN |
| -14 | BUFTOOSHORT |
| -23 | UNSUPPORTED |

- If 5 GHz comes in low, the AP's 3 dBm txpower may be the cap; `iw dev wlp3s0 set txpower
  auto` would lift it, but that is a host radio change to note in the log.

## Second lever: several frames per IPC (branches `wifi-f1-batch`)

- **Branches** (pushed to `publish`):
  - devices `2d0354e`, on top of `wifi-f1-agg`;
  - lwip `6cbcc8f`, on top of master `6ae263b`.
- **Host test:** `tools/wifi-batch-hosttest` (`make run DEVICES=<wt> LWIP=<wt>`) passes.

**Data path today:**

- **TX** is one `write()` per frame, from a buffer that is not page-aligned, so the kernel
  copies a page every time. The tcpip thread blocks for the whole round trip.
- **RX** is ~1.3 IPCs per frame: one `read()` per frame, an empty read at the end of each
  drain, and one `/dev/wifiirq` read per wake.

**Batch path:**

- A new device `/dev/wifibatch`; `/dev/wifidata` stays as the control arm.
- **TX:** a netif TX thread sends up to 16 frames or 24 KB per message. The daemon applies
  the same per-frame credit check, takes what it can and returns the count; the netif
  re-sends the rest every 200 µs for up to 100 ms. `linkoutput` waits at most 20 ms on a
  full queue, then returns `ERR_IF`, so a shut credit window cannot stall genet/NFS.
- **RX:** one read returns everything queued (≤32 frames or 24 KB), with a `DRAINED` flag
  that saves the confirming empty read.
- **Buffers:** page-aligned `mmap` buffers.
- **Knob:** daemon `batch=1` or `wifi batch 0|1`, **default 0**. The netif follows it from
  its 3 s status poll and prints `lwip: wifi43455: frames now go several|one per message`.
- **Counters:**
  - `WIFISTATS batch` (batch-path messages and frames each way, plus `tx_partial` and
    `rx_capped`);
  - `WIFISTATS ipc`: frames per request each way, **reset at every `wifi stats`**, so
    stats → transfer → stats needs no subtraction.

**Build gate:** `--scope core` with both branches merged, then `strings -a`:

- `rpi4-wifi` contains `WIFISTATS ipc` and `wifibatch`;
- `wifi` contains `batch [0|1]`;
- the lwip binary contains `frames now go`.

**Arms**, same boot, psh: A = `batch 0`; B = `wifi batch 1`, wait ≥3 s for the UART line;
A′ = `wifi batch 0`. Each arm runs `wifi status`, `wifi stats`,
`python3 /root/wifi-perf.py 10.43.0.1 7777 <ip> 16777216 3`, `wifi stats`.

**Expected:**

- RX +20–40 % (Pi-bound).
- TX about flat at HT20 (air-bound); a TX gain needs the 5 GHz arm as well.
- If frames per request rises but throughput does not, the next bound is lwip `TCP_WND`
  (8–12 MiB/s).

**Risks:**

- a batch read holds the message thread for up to 32 frames;
- more `blocked` and `tx_partial` when credits run out mid-batch;
- bus errors become real drops instead of `ERR_IF` retries;
- one reorder at each switch;
- empty polls cost more in poll mode;
- no SDPCM TX glom yet.

## Result 1 — frames per IPC, 2.4 GHz HT20 (2026-10-09, `wifi-batch-ab`, build of 10-09 02:21)

| arm | frames per request tx / rx | daemon busy | TX MB/s (median) | RX MB/s (median) |
|---|---|---|---|---|
| batch 0 | 1.00 / 0.91 | 31.9 % | 4.96 | 4.57 |
| **batch 1** | **4.15 / 5.66** | 31.7 % | 5.12 | 4.73 |
| batch 0 (drift) | 1.00 / 0.91 | 33.3 % | 4.97 | 4.69 |

Batching works as designed (requests 110 k → 22 k for the same data, `tx_max=16`, `rx_max=24`,
1 partial TX write), but throughput moves only ~+3 %. The daemon is ~32 % busy in every arm, so
the per-frame IPC path is **not** the bound. The model in §1 was wrong on this point. What is left:
the air (HT20, AP txpower 3 dBm) or lwip's 64 KB TCP window. Next: the 5 GHz VHT80 arm.
`wifi batch` stays default 0 until there is a measured reason to change it.

## Result 2 — 5 GHz (2026-10-09, `wifi-5g`, `wifi-5g20`; the AP restored to 2.4 GHz ch6 afterwards)

| AP | batch | TX MB/s | RX MB/s | frames/request tx / rx |
|---|---|---|---|---|
| 2.4 GHz ch6 HT20 | 0 | 4.96 | 4.57 | 1.00 / 0.91 |
| 2.4 GHz ch6 HT20 | 1 | 5.12 | 4.73 | 4.15 / 5.66 |
| 5 GHz ch36 20 MHz | 0 | 6.10 | 5.53 | 1.00 / 0.95 |
| **5 GHz ch36 20 MHz** | **1** | **6.47** | **7.10** | 4.59 / 6.03 |
| 5 GHz ch36 **80 MHz** | — | **no traffic** | | |

- **5 GHz at 20 MHz** is +23 % TX and +21 % RX on its own: the 2.4 GHz channel is shared with
  the neighbours on ch 4/8.
- On the cleaner channel, **batching adds +28 % RX**, against +3 % at 2.4 GHz. Best case against
  the baseline: TX +30 %, RX +55 %.
- **80 MHz is broken:** the radio associates (`WIFISTATS radio chanspec=0xe02a ch=42 bw=80
  band=5g`), but DHCP never completes (the Pi falls back to 169.254.x) and it receives 0–4 data
  frames. A new defect, not yet investigated: the key handshake or the data path at VHT80 (the AP
  transmits at 3 dBm).
- **Owner decisions:**
  - run the lab AP at 5 GHz / 20 MHz;
  - make `wifi batch 1` the default (no errors in two runs: `tx_partial` 1, `tx_bad` 0).
