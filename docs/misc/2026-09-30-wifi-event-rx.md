# WiFi RX without the idle poll cost (F1 follow-up, 2026-09-30)

Scope: the joined-link CPU cost found in [MIGRATION §7s](../gpu-new-lane/MIGRATION.md): with the AP
up, vkQuake (the one CPU-bound game) ran **38.65 fps**, and **42.75** with the AP down on the same
image (−9.6 %). Goal: a joined but idle link costs ~0 CPU; throughput (TX 3.6 / RX 3.3 MB/s) and
latency stay where they are. **No Pi cycle and no image build were run.** Everything about hardware
below is a prediction, checked by the pre-registered cycles at the end.

## Branches (`wifi/event-rx`, pushed to `publish`, not merged)

| repo | commit | what |
|---|---|---|
| phoenix-rtos-lwip | `049647f` | `wifi43455`: RX poll interval paced by traffic (200 µs → 10 ms when idle) + a TX→RX wakeup |
| phoenix-rtos-devices | `a6bca6a` | `rpi4-wifi`: comments only, plus 3 dead `WIFI_RX_*` defines removed. Records why RX is polled and what an interrupt path would need |
| coordination | this commit | this doc |

Based on lwip `9065afe` and devices `6a3bf49` (today's masters). The lwip commit works on its own. The
devices commit changes no code, so the two can merge in either order. Both files compile under the
real flags with `-Werror`: `syntax-check.sh`, pointed at the worktree copies, with a negative control
that failed as it should.

## Where the 200 µs loop was

The loop was in **lwip, not in the daemon**. The daemon has no loop of its own:

```
lwip wifi_rxThread                       (drivers/wifi43455.c)
  read("/dev/wifidata")  ── IPC ──►  rpi4-wifi wifi_thread → wifi_frameRead
                                        → diag_wifiFrameRx → diag_f2RecvFrame
                                        = ONE 12-byte CMD53 header read of the F2 FIFO,
                                          PIO busy-spin on SDHCI status (~22 µs, the daemon's
                                          own measurement: 1.1 M empty probes = 24.9 s of bus time)
  ◄── 0 ("nothing queued") ──
  usleep(200)  → next read
```

`/dev/wifidata` reads never block, because the daemon has one message thread. So lwip decided the
rate, and ran it as fast as usleep(200) plus two IPC hops allow: about **4 000–5 000 probes a
second**, joined or not, until `c7e9d14` stopped it while unassociated. Each probe costs about 22 µs
of spinning in the daemon plus the IPC round trip and two context switches. At an estimated 30–40 µs
of CPU per probe, that comes to **12–20 % of one core**. This is consistent with the 9.6 % vkQuake
loss, but it is an estimate, not a measurement. Whether the loss comes from core time, SDIO/bus
contention or IPC scheduling is not separated here. Check (1) tests the whole effect whatever the
mechanism.

The comments did not match the code. lwip said "the daemon now WAITS for a frame inside read()",
which was false because that wait had been reverted. The daemon still defined
`WIFI_RX_WAIT/PROBE/SPIN_US`, which nothing referenced. Both are corrected.

## Why not SDIO CARD_INTR (the "real" event-driven RX)

An interrupt-driven path is the right end state, since the chip raises the SDIO card interrupt when
it queues a frame. It cannot be shipped without a Pi, for two reasons found in the code:

1. **The interrupt line is shared with the SD card controller.** The Arasan controller (WiFi,
   `0xfe300000`) and emmc2 (SD card, `0xfe340000`) both use GIC SPI 126 = IRQ 158
   (`bcm2711-rpi-ds.dtsi:184-189`, `bcm2711.dtsi:429`, `bcm2711-sdio.c:38`). The kernel calls every
   handler on the line (`interrupts_dispatch`), and `userintr_dispatch` broadcasts the handler's cond
   whenever it returns ≥ 0. `bcm2711-emmc/sdcard.c:sdhost_isr` **always** returns 0 and zeroes its
   SIGNAL_ENABLE. Its `_sdio_cmdExecutionWait` does a single `condWait`. After an early wakeup it
   finds its flags absent, keeps `ret = -ETIME`, and **resets CMD and DAT under an in-flight SD
   command**. So every WiFi frame interrupt during SD I/O would abort that I/O. On SD boot the SD
   card holds the root file system. This must be fixed first: `sdhost_isr` returns -1 unless
   `INTR_STATUS & SIGNAL_ENABLE` is non-zero. That is a storage-path change, and it needs an SD-boot
   validation with I/O running.
2. **Nothing enables the interrupt on the chip side.** The SDIO core's `hostintmask` (core + 0x24)
   and CCCR IEN (0x04) are never written. The Arasan's INT_STATUS_EN/SIGNAL_EN (0x34/0x38) are left
   as the boot firmware set them.

The full recipe is in the daemon's comment above `WIFI_DEV_TEXT_ID`, and it is listed under
follow-ups below. Until it lands, the daemon has to poll the chip, and the only open question is
**how often**. That rate is lwip's to choose.

## Design: poll at the rate the traffic needs

`wifi_rxThread` now paces its reads:

| state | next read |
|---|---|
| a frame was just received | immediately (more may be queued behind it, e.g. the rest of a glom superframe). Then **8** empty reads at 200 µs (≈ 1.6 ms) |
| a frame was just **transmitted** | **100** empty reads at 200 µs (≈ 20 ms): a TCP segment, ACK, ping or DHCP/ARP request usually draws a reply, and the measured RTT to the AP is 1.7–5.5 ms |
| hold used up | the interval doubles per empty read: 400, 800, 1600, 3200, 6400, then **10 000 µs**. Idle is reached ~22 ms after the hold ends |
| not associated | 100 ms, unchanged (`c7e9d14`) |
| read error | 20 ms, unchanged |

**TX→RX wakeup.** A reply must not wait out a 10 ms nap. So `wifi_linkOutput` calls `wifi_rxKick`
after every frame it transmits. The kick bumps `tx_kicks`, and if the RX thread is napping
(`rx_napping`) it signals `rx_cond`. The nap is a `condWait` with the interval as its timeout. The
flag and the counter form a store-then-load handshake on both sides, using seq_cst `__atomic`
builtins. The signal needs `rx_lock`, which `condWait` releases atomically, so no kick is lost. The
common TX path costs two atomics. The lock is taken only when the RX thread is actually asleep.

**Why throughput and latency should not move.** Every TCP flow transmits in both directions (data one
way, ACKs the other), and every ping and request is a TX. So any flow polls at 200 µs, exactly as
before. Only an **unsolicited** frame on an idle link waits, at most 10 ms (5 ms on average), and only
once, because that frame restores the fast rate. The TX path, `/dev/wifi`, the join thread,
DHCP and the link-loss rejoin are untouched. Loss events (WLC_E_LINK / DEAUTH / DISASSOC) reach the
daemon through the same reads. At idle they are seen within 10 ms instead of 0.2 ms, against a
supervisor that polls `status` every 3 s.

### The trade-off, in numbers

Probes on an idle link (no TX, no RX), assuming ~35 µs of CPU per probe (estimate, see above):

| idle interval | probes/s | CPU (one core) | worst extra latency, unsolicited frame |
|---|---|---|---|
| 200 µs (before) | ~4 000–5 000 | ~12–20 % | 0.2 ms |
| 1 ms | 1 000 | ~3.5 % | 1 ms |
| 5 ms | 200 | ~0.7 % | 5 ms |
| **10 ms (chosen)** | **100** | **~0.35 %** | **10 ms** |
| 20 ms | 50 | ~0.18 % | 20 ms |

10 ms is where CPU cost stops mattering: scaling the measured 9.6 % by 100/4000 predicts ≈ 0.25 %,
i.e. ~0.1 fps in vkQuake. It is also below any interactive threshold for the one case it delays. A
broadcast/multicast frame on the LAN costs 8 fast probes plus ~6 ramp naps (~14 probes). The lab
network (one host, one Pi) has almost none.

**Known weak spot:** an inbound stream with **no** return traffic (e.g. unacknowledged UDP from the
host) whose packets are 2–10 ms apart. It would ride the ramp and pick up to one interval of extra
delay per packet. Nothing on the Pi uses such a stream today, and TCP (`wifi-perf`, NFS, HTTP) is
not affected. If it ever matters, raise `WIFI_RX_HOLD_RX`.

## Pre-registered Pi check

Build: `./scripts/rebuild-rpi4b-fast.sh --scope core` with lwip on `wifi/event-rx`. It is a committed
core change, so `auto` would ship a stale lwip. Then prove the blob contains it. The new code has one
new string:
`strings -a .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep -c 'lock/cond creation failed'`
must print ≥ 1, where master prints 0. The same count for `'wifi43455: tx_lock mutexCreate failed'` (the old message) must print **0**. That proves the old lwip is gone, which is how the stale-core hazard actually shows up. Sync the export as for any image. Host AP up:
`./scripts/radio-ap-up.sh`. The export needs `/etc/wifi.conf` (lab file) and, for (3), `wifi-perf.py`
in `/root`, with `python3 scripts/wifi-perf-host.py 7777 4194304 3` running on the host.

**(1) vkQuake, AP up (joined).**
`./scripts/run-showcase-gate.sh --label erx-vkq --only vkq`, with the boot log showing
`joined "PhoenixNet"; link up`.
Predicted: **vkq median ≥ 41.75 fps** (within 1 fps of the AP-down 42.75), torches present, 0 faults.
To remove build-to-build noise, repeat on the same image with `./scripts/radio-ap-down.sh`
(`--label erx-vkq-apdown`). Predicted: the two differ by < 0.5 fps.

**(2) Idle cost while joined.**
```
./scripts/test-cycle-psh-interact.sh --label erx-idle --inter-cmd-secs 30 --idle-secs 30 --max-cmd-secs 120 -- \
    "wifi status" "wifi stats" "wifi stats" "top -n 1"
```
Predicted: `wifi status` shows joined with an address. Between the two `wifi stats`, `WIFISTATS
rx_misses` grows by **≤ 6 000** in the 30 s. That is ≤ 200 probes/s: 100/s steady plus headroom for
LAN chatter, since each host ARP/mDNS/IGMP frame costs ~14 probes and the Pi's reply arms the
100-probe TX hold. The old loop gives ~120 000–150 000, so the threshold is still 20× below master.
This is the discriminating number, because it counts empty probes in the daemon directly. Record
the `tx_calls` and `rx_hits` deltas from the same two lines, so that a near-miss can be attributed
to traffic rather than to the poll. In `top -n 1`, the graded line is **`rpi4-wifi` at 0–1 % CPU**.
`lwip` should be low too, but on netboot it also carries genet and the NFS root, so its figure is
recorded, not graded.

**(3) Latency and throughput.**
```
./scripts/test-cycle-psh-interact.sh --label erx-perf --inter-cmd-secs 8 --idle-secs 60 --max-cmd-secs 200 -- \
    "wifi status" "ping -c 5 10.43.0.1" "python3 /root/wifi-perf.py 10.43.0.1 7777 10.43.0.89 4194304 3" "wifi stats"
```
Predicted: ping 5/5. Replies 2–5 after ARP land in the W1 range, **1.7–5.5 ms**, because each ping is
a TX, so its reply meets the fast poll. `WIFIPERF-MEDIAN` lands in the cycle-T ranges, **TX
3.45–3.62, RX 3.23–3.31 MB/s**.

**(4) Link loss and rejoin.** The same outage as the 2026-09-26 cycle `wifill1`. Run a
psh-interact cycle (`--label erx-ll`, commands `"wifi status"` early and `"wifi status"`
`"ping -c 3 10.43.0.1"` last, with enough `--inter-cmd-secs` to span ~150 s). Meanwhile a host-side
driver waits for `joined "PhoenixNet"; link up` in the new log, sleeps 20 s, runs
`nmcli connection down phoenix-ap`, sleeps 45 s, then runs `./scripts/radio-ap-up.sh`. That driver
is step 3 of the 09-26 script. Skip its step 1: the daemon is in the image now, so nothing is staged.
Predicted, as on 09-26: the daemon prints `wifi: association LOST (event …)`, the netif prints
`association with "PhoenixNet" lost; rejoining`, and after the AP returns there is a new
`joined "PhoenixNet"; link up` + `dhcp_start: 0`. The final `wifi status` shows joined=1, and ping
is 3/3.

| outcome | reading |
|---|---|
| all four as predicted | the idle cost is gone; F1's "polling is the open part" closes; CARD_INTR stays a follow-up for latency, not CPU |
| (2) rx_misses still ~4 000/s (anything > 200/s) | the binary is stale (check the `strings` gate) or something transmits continuously and keeps the TX hold armed: compare `WIFISTATS tx_calls` across the window |
| (1) still ~38–39 fps with (2) passing | the cost was not the poll. Attribute it by killing `rpi4-wifi` in a re-run |
| (3) RX throughput low, TX fine | the RX hold is too short for the host's segment spacing: raise `WIFI_RX_HOLD_RX` (8 → 32) and re-measure with n ≥ 3 |
| (3) ping RTT +several ms | the TX kick is not waking the nap: look at `wifi_rxKick` / `rx_napping` |
| (4) no rejoin | not expected from this change (the loss path is untouched); run master's lwip to attribute |

## Follow-ups

- **CARD_INTR RX**, in this order: (a) make `bcm2711-emmc` `sdhost_isr` shared-safe (return -1 unless
  `INTR_STATUS & SIGNAL_ENABLE`), validated on SD boot under SD I/O; (b) in `rpi4-wifi`, enable
  `hostintmask = I_HMB_SW_MASK | I_CHIPACTIVE`, CCCR IEN master+F1+F2, and Arasan INT_STATUS_EN /
  SIGNAL_EN bit 8. Register a handler on IRQ 158 that returns -1 unless bit 8 is pending, else masks
  SIGNAL_EN bit 8. Add a waiter that drains under a bus lock, acks the core intstatus (W1C core +
  0x20, SMB_INT_ACK for a mailbox interrupt) and re-enables bit 8; (c) a blocking `/dev/wifidata`
  read served from that waiter, keeping this timed poll as a fallback. This removes the idle probes
  entirely and the 10 ms worst-case latency. After this change it is a latency improvement, not a
  CPU one.
