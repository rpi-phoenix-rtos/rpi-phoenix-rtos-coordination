# WiFi (F1): the SDIO clock at `core_freq=500`, the firmware-download failure, and throughput (2026-09-27)

Scope: [KNOWN-ISSUES F1](../KNOWN-ISSUES.md) /
[details](../known-issues-details.md#f1). Two open items: (a) the watch item "both natural
firmware-download failures came after `core_freq=500`", and (b) throughput of about 3 MB/s each way.
Everything here comes from reading the code and the log archive. **No Pi cycle was run.** Every
effect on hardware below is a prediction, and each has a pre-registered cycle at the end.

## Verdicts

| # | Question | Answer | Branch |
|---|---|---|---|
| a1 | Does the SDIO bus run at 2× at `core_freq=500` because the driver assumes a 250 MHz base? | **No, refuted.** The driver has queried the base clock since `27db916`. The firmware reports 250 MHz in all 12 boots that logged it, 11 of them at core 500. | — |
| a2 | What are the download failures, then? | All three natural failures end the same way: `rc_w=-5`, the latched BUF_WR_READY never came back within 100000 register reads, with no error bit set. That PIO loop has a lost-edge ordering hazard, and its budget is counted in bus reads, not in time. Neither mechanism is proven. Fix and discriminating instruments are on the branch. | `wifi/sdio-clock` `c2ff5a8` |
| b | What bounds throughput? What is the lowest-risk lever? | The SDIO transfer is wire-bound per frame (25 MHz, 4-bit). About 70 % of each frame's time is spent off the wire, and the air (2.4 GHz, 1×1, 65 Mbit/s PHY) may itself be near the ceiling. Lowest-risk change: stop rewriting the backplane window before every transfer. Small expected gain. The PHY rate is now reported. | `wifi/throughput` `679ee59` |

Branches (phoenix-rtos-devices, pushed to `publish`, not merged, both based on master `1924a42`):

| branch | commits | worktree |
|---|---|---|
| `wifi/sdio-clock` | `9f90572` level-bit PIO wait + wall-clock bound + `SDIO-CLK`/`SDHCI-POLL`/`SDHCI-TIMEOUT`/`SDHCI-PIO` lines + `fwloadbench`; `248a01f` download timing + `legacypio`; `c2ff5a8` `legacypio fwloadbench` starts with legacy | `/home/houp/.claude/jobs/c8f1289c/tmp/wt-devices-wifi` |
| `wifi/throughput` | `560d8a4` backplane-window cache + `WIFISTATS sbwin`; `679ee59` `WIFISTATS phy rate` in `wifi stats` | `/home/houp/.claude/jobs/c8f1289c/tmp/wt-devices-wifithr` |

The two branches are independent. They touch different functions of `wifi/rpi4-wifi/rpi4-wifi.c`,
except `wifi_stats()`, where each adds one line. Expect a trivial conflict there when both are merged.

---

## (a1) Clock hypothesis: refuted

**The code.** `diag_sdhciSetClockKHz()` divides `diag_sdhciBaseHz()`, and since devices `27db916`
(2026-09-26 22:58) that value is `GET_CLOCK_RATE(EMMC = clock id 1)` through `/dev/vcmbox`. The
fixed 250 MHz is only the fallback, and a fallback prints `GET_CLOCK_RATE(EMMC) failed`. `27db916`
came 1 h 47 min *before* `core_freq=500` was adopted (00:45). The arithmetic is SDHCI 3.0,
`sd = base / (2·N)` with `N = ceil(base / (2·target))`:

| target | base 250 MHz | resulting clock |
|---|---|---|
| 400 kHz (enumeration) | N = 313 | 399 361 Hz |
| 25 MHz (data, after CCCR high-speed) | N = 5 | 25.000 MHz |

A hard-coded 250 with a real 500 MHz base would have given 2× both. That is the scenario the
hypothesis needs, and the query rules it out.

**The archive.** `grep -a 'SDHCI base clock' artifacts/rpi4b-uart/*.log` finds 12 boots, all
`= 250000000 Hz`: `core-g0` (core 250), then `core-g1`, `wake-1`…`wake-9`, `fwretry-1` (core 500).
Core 500 is proven in `core-g1` (`rpi4-hci: core_clk=500000000 Hz -> AUX_MU_BAUD=541`). For the
`wake-*`/`fwretry-1` boots it is shown by the image banner, which carries project `e70e124`, the
`core_freq=500` commit (e.g. `rpi4b-uart-20260927-040826-wake-9.log:117`). So the EMMC clock does not
follow `core_freq`. The gate in [core-clock-500.md](../gpu-new-lane/core-clock-500.md) (P4)
reached the same conclusion.

**The hardware description.** Linux gives the Arasan (`mmc@7e300000` / `mmcnr`)
`clocks = <&clocks BCM2835_CLOCK_EMMC>` (`external/linux/arch/arm/boot/dts/broadcom/bcm283x.dtsi:423-428`,
`bcm270x.dtsi:50-60`). That is a CPRMAN peripheral clock, not the VPU/core clock that the AUX, SPI,
BSC and SDHOST blocks run on.

**Residual gap, now closed by instrument.** `GET_CLOCK_RATE` returns the rate the firmware *set*.
`wifi/sdio-clock` also asks for `GET_CLOCK_RATE_MEASURED` (0x00030047) and prints the whole
derivation on every clock change:

```
rpi4-wifi: SDIO-CLK target=25000 kHz base=250000000 Hz measured=<M> Hz div=5 sd=25000000 Hz
```

A `measured` far from `base` would reopen a1.

## (a2) The firmware-download failures

### What they look like

| log | core | stopped at | `rc_w` | `rc_nvram_w` | afterwards |
|---|---|---|---|---|---|
| `rpi4b-uart-20260923-035345-wifiscan.log` | 250 | 24 576 B (window 0, 7th 4 KB CMD53) | −5 | −5 | CR4 never released, `fw_alive=0` |
| `rpi4b-uart-20260927-010406-wake-1.log` | 500 | 90 112 B (window 2) | −5 | −5 | same; no retry existed yet |
| `rpi4b-uart-20260927-040826-wake-9.log` | 500 | 102 400 B (window 3) | −5 | −5 | same; retry `9c4267e` then loaded 643 648 B, `fw_alive=1`, joined |

`-5` from `diag_sdioCmd53Write` has exactly one source: the per-word wait for the **latched**
`INT_STATUS.BUF_WR_READY` (bit 4) ran out its 100000 reads, and no error bit (`-4`) was ever set.
The CMD53 is then left open mid-data-phase, so every later command on the bus fails or reads zero
(`IoCtrl pre=0x00`, `rstvec … 00 00 00 00`, `CHIPCLKCSR 00…`). It stays that way until the next
WL_REG_ON power cycle, which is why the retry works.

Two patterns stand out:

- **All three stopped early**, within the first 102 KB of the 643 KB image (the first 16 %). If the
  hazard were the same for every block, the chance of that is about 0.16³ ≈ 0.004. So the failures
  are tied to something early in the load, not to a constant per-block risk.
- **All three were the first load of a process.** That is true of almost every load, so on its own
  it is not evidence. But the first load is the only one in which the compiled-in firmware image
  (`wifi_fw_43455[]`, `.rodata` of a static binary on the NFS root) is **paged in on first touch, from
  inside the PIO loop**. Loads that run later in the same process (the two retries in the archive,
  `fwretry-1` and `wake-9`) find it resident, and both succeeded.

### The counts behind the watch item

The denominator is every natural load since the firmware first ran, 2026-08-09 13:44. That is
111 `fw_alive=1` + 3 `fw_alive=0` = **114 loads**. Excluded: the 7 `fw_alive=0` lines in the five
bring-up development logs of 2026-08-09 (`wifiprobe`, `wifitrivial`, `wifitrivial2`, `wifierom`,
`wifiprecond`), which predate a working firmware load. At core 500: **13 loads in 11 boots**
(`core-g1` 1, `wake-1` 1, `wake-2…6` 5, `fwretry-1` 2, `wake-7/8` 2, `wake-9` 2), **2 failed**. At
core 250: 101 loads, 1 failed. Fisher one-sided p = 0.034. **Enriched, n = 2, not a cause.** The
test was chosen after the pattern was noticed, and three things changed in the same window:

1. `core_freq=500` (project `e70e124`). It speeds up every uncached read of the controller.
2. devices `27db916`. It adds a `/dev/vcmbox` round trip before the first clock set. That is before
   enumeration, so it is not on the download path.
3. devices `d73803e` (chip wake at join, after the download) and a rebuild against the tree's
   libphoenix (`eeee8ac`, 2026-09-26). The rebuild could change scheduling and paging behaviour
   around the load.

### Two mechanisms, both core-clock sensitive, neither proven

**M1: the lost edge.** The old loop waited for the latched bit before every word and cleared it
(W1C) *after* the block. Per SDHCI 3.0, BUF_WR_READY is set when PRESENT_STATE.BUF_WRITE_ENABLE
(bit 10) goes 0→1, i.e. when the next block can be written. Suppose that edge fires before our clear
lands: the other half of the controller's buffer drained while we were still writing this block,
because the thread was interrupted, preempted or page-faulted mid-block. Then our clear erases it,
bit 10 stays 1, no new edge ever comes, and the wait runs out: `-5`, with no error. A page fault on
first touch of the image is a multi-millisecond host stall inside the loop. That fits "first load
only" and "early in the image". Linux's driver for this exact controller does the opposite:
`bcm2835-mmc.c` acknowledges INT_STATUS first (`bcm2835_mmc_irq`, line ~1006) and then loops on the
**level** bit (`bcm2835_mmc_transfer_pio`, lines 455-485:
`while (readl(PRESENT_STATE) & SDHCI_SPACE_AVAILABLE)`). Upstream `sdhci.c:615-651` does the same.
*Discriminator:* at a timeout, `space=1 wr_rdy=0` (M1) versus `space=0` (M2).

**M2: a budget in bus cycles.** Every wait is `for (100000 reads)`. An uncached read of the Arasan
crosses the VideoCore peripheral bus, so the same count may last about half as long at core 500. A
card-side stall that 250 MHz tolerated can then trip it. *Discriminator:* `space=0` at the timeout.
With the fix, a `slow_waits>0` and `slow_max_us` in the range of the old budget. `SDHCI-POLL`
measures the budget directly.

**M3 (named, not pursued).** The downstream driver delays after *every* register write by
`BCM2835_SDHCI_WRITE_DELAY(f) = 2 SD clocks + 1 µs` (`bcm2835-mmc.c:65,134-144`), because the
controller can lose back-to-back register writes. Our helpers write `BLOCK_SIZE_CNT`, `ARGUMENT` and
`TRANS_CMD` back to back, and a faster bus makes them closer. A lost register write would more
likely show as `-2`/`-3`/`-4` than as `-5`, so it ranks third. It is the next thing to try if
M1/M2 are refuted.

### The fix on `wifi/sdio-clock` (`9f90572`, `248a01f`, `c2ff5a8`)

- **Block-mode read and write helpers (`diag_sdioCmd53Read`/`Write`)**: per block, W1C first, then
  wait on the PRESENT_STATE level bit (`SPACE_AVAILABLE` 0x400 / `DATA_AVAILABLE` 0x800), then move
  one block. This is the Linux order. It also takes fewer MMIO reads than the old loop, which did one
  status read per word.
- **Every wait in those two helpers** (`diag_sdhciWaitSet`) keeps the old 100000-read spin as the fast
  path. When the spin runs out, it keeps polling against `CLOCK_MONOTONIC` for up to 100 ms, checks
  once more after the deadline (so preemption cannot fake a timeout), and counts the waits that
  needed the extra time. The byte-mode helpers (the RX hot path) and `diag_sdhciCmd` (CMD52) are
  unchanged.
- **The old loop is kept, selectable at run time** (`g_pio_legacy`), only for the A/B below.
  `fwloadbench` alternates it, starting with level, or with legacy when `legacypio` is also given.
  `rpi4-wifi legacypio &` runs the daemon with it. Remove it once graded.
- **Scope, and a risk to know about:** `diag_f2Write` sends every frame over 512 B through
  `diag_sdioCmd53Write`. So full-MTU **TX at run time** also uses the new loop, not only the firmware
  download. On a wedged F2 write, level mode now holds the daemon's single message thread for up to
  100 ms per wait before returning `-5`, and the lwip RX thread's `read()` queues behind it. The path
  is rare, but a later "RX hiccup" right after a TX error should be read with this in mind.
- **Tagged lines for grading:**

```
rpi4-wifi: SDIO-CLK target=<kHz> kHz base=<Hz> Hz measured=<Hz> Hz div=<N> sd=<Hz> Hz
rpi4-wifi: SDHCI-POLL spin=100000 reads took <us> us (<ns> ns/read)
rpi4-wifi: SDHCI-TIMEOUT dir=wr|rd mode=level|legacy blk=<b>/<n> pres=0x.. int=0x.. space=<0|1> data=<0|1> wr_rdy=<0|1> rd_rdy=<0|1>
rpi4-wifi: SDHCI-PIO mode=<m> fw_bytes=<B> rc_w=<rc> rc_nvram=<rc> slow_waits=<n> slow_max_us=<us> timeouts=<n> dl_ms=<ms> cmd53_max_us=<us>@0x<fw offset> cmd53_ge2ms=<n>
rpi4-wifi: FWLOAD-BENCH load=<i>/<N> mode=<m> rc=<rc> fw_alive=<0|1> ... dl_ms=<ms> cmd53_max_us=<us>
rpi4-wifi: FWLOAD-BENCH-SUMMARY level=<ok>/<n> legacy=<ok>/<n> (loads whose firmware started)
WIFISTATS pio mode=<m> slow_waits=<n> slow_max_us=<us> timeouts=<n>
```

  `cmd53_max_us@offset` detects the page-fault stall: a slow 4 KB CMD53 at the offset where a legacy
  load failed would mean the host stalled there.

**Compile evidence** (both branches, same flags as `build-standalone.sh`, the real build; `-Werror`
added):

```
R=/home/houp/phoenix-rpi; SR=$R/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
W=<worktree>
$R/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -std=gnu11 \
  --sysroot=$SR/ -B$SR/lib/ -I$R/sources/phoenix-rtos-lwip/port -I$R/tools/wifi-probe \
  -I$W/misc/rpi4-vcmbox -Werror -fsyntax-only $W/wifi/rpi4-wifi/rpi4-wifi.c      # -> clean
GCC=… NM=… LWIP_PORT=$R/sources/phoenix-rtos-lwip/port WIFI_PROBE=$R/tools/wifi-probe \
  SYSROOT=$SR NFSROOT=/nonexistent-skip bash $W/wifi/rpi4-wifi/build-standalone.sh
  # -> 0 warnings, "rpi4-wifi: 0 undefined symbols", "wifi: 0 undefined symbols", staging skipped
```

`strings -a` of both built binaries shows every new tag once. Nothing was staged to the export. The
EULA blobs are read through `-I` from their existing locations and were not copied. (The file does
not go through the project build, so `scripts/syntax-check.sh` does not apply to it.)

---

## (b) Throughput

### Where the time goes

Measured 2026-09-02 at core 250, before the glom fix. These are averages over thousands of frames
in one run, so they hold up despite the run-to-run noise:
**TX 132 µs/frame, RX 178 µs/frame, empty RX probe 22 µs**
([2026-09-02 doc](2026-09-02-wifi-throughput-first-numbers.md)). After glom (`4e8a7e9`) the medians
were TX 3.27 / RX 3.00 MB/s, 3 runs each.

| quantity | value | source |
|---|---|---|
| bus | 4-bit, **25 MHz** (CCCR high-speed and HCTL HS are set, but the clock is programmed to 25) | `diag_sdioGoHighSpeed` |
| raw bus rate | 12.5 MB/s | 4 bit × 25 MHz |
| wire time of one 1536 B frame (1514 + 16 hdr, padded to 64) | ≈ 123 µs | 1536 × 2 clocks / 25 MHz |
| measured daemon cost per TX frame | 132 µs, i.e. **wire-bound** | 2026-09-02 |
| end-to-end per frame at 3.3 MB/s (1460 B payload) | ≈ 440 µs | 1460 / 3.3e6 |
| ⇒ off the wire, per frame | ≈ 300 µs (~70 %) | message round trip on the single daemon thread, RX probes sharing it, lwip/tcpip, ACKs |
| Linux on the same controller and base clock | 250 MHz / 6 = **41.67 MHz** (divisors must be even; `brcm,overclock-50 = <0>`) | `bcm2835-mmc.c:1063-1097`, `bcm270x.dtsi:57` |
| PHY rate, Pi → AP | **65.0 Mbit/s, MCS 7** (346 samples); AP → Pi 6.0 Mbit/s at idle (idle rate control, not meaningful) | AP-side `iw station dump`, `$CLAUDE_JOB_DIR/tmp/wifi-station-dump.log`, 2026-09-01 |

**The ceiling is probably the air, not the SDIO bus.** Channel 6 is 2.4 GHz, 20 MHz, and the 43455
is 1×1. MCS 7 at long GI is 65 Mbit/s of PHY rate, which typically yields 30–45 Mbit/s TCP
(3.7–5.6 MB/s), depending on A-MPDU aggregation. 3.3 MB/s is then 60–90 % of the air, against
~26 % of raw SDIO. The PHY rate was sampled once, on an idle link. `wifi/throughput` now reports it
(`WIFISTATS phy rate=`, `BRCMF_C_GET_RATE` = 12 in `fwil.h:18`, in 500 kbit/s units), so the next
throughput run measures the ceiling instead of assuming it. It is deliberately **not** in
`status`, which the netif polls every 3 s: a control command drains the shared F2 FIFO and drops
the data frames it meets.

Not re-measured since `core_freq=500`: faster uncached reads could by themselves have moved the
off-wire part. A fresh baseline is part of cycle T below.

### Levers, by expected value

| # | change | expected gain | risk | status |
|---|---|---|---|---|
| 1 | **Measure the air first** (PHY rate next to every throughput result) | makes 2–5 falsifiable | none | **done**, `679ee59` |
| 2 | **Cache the backplane window.** `diag_setWindow18` sent 3 CMD52s before every TX frame, control frame and RX probe, nearly always rewriting the value already there. brcmfmac shadows it (`sdiodev->sbwad`, `bcmsdh.c:223-242`). | per empty probe ≈ −14 of 22 µs (3 CMD52 ≈ 4–5 µs each at 25 MHz); per TX frame ≈ −14 of 132 µs (~−10 %); end to end **+3 to +10 %**, mostly by freeing the single message thread | low: the shadow is updated by every CMD52 write to SBADDR, so every hand-written window site keeps it right; forgotten on WL_REG_ON power cycle or a failed write | **done**, `560d8a4`, `WIFISTATS sbwin writes=N skips=M` |
| 3 | Data clock 25 → 41.67 MHz (request 50 MHz; `N = 3`, same as Linux) | wire per frame 123 → 74 µs, i.e. ≤ −49 of ~440 µs, **≤ +11 %** | medium: moves the clock of the bus F1 is watching; do it only after (a2) is graded | design |
| 4 | Batch frames per `/dev/wifidata` message (length-prefixed, both directions; daemon + `lwip/drivers/wifi43455.c`) | attacks the ~300 µs off-wire; plausibly the largest host-side lever, but bounded by the air | medium, two repos | design |
| 5 | CARD_INTR-driven RX (SDHCI IRQ, GIC SPI 126 per `bcm2711-rpi-ds.dtsi:188`) instead of the lwip RX thread's 200 µs poll | removes empty probes and poll latency | medium–high (userspace IRQ attach, new wake path) | design |
| 6 | DMA for F2 (Linux uses `dmas = <&dma 11>`) | frees the CPU, but the wire and the air stay the bound | high | design |

Only 2 met the brief ("small and self-contained"). It is honest to say its gain is small; the
reason to take it is that it is free of protocol risk. 3 is the next cheapest, but it waits for (a2).

---

## Pre-registered Pi cycles (not run)

Common preparation, following the queue16 pattern. Run one lane at a time: one Pi, one UART.

```
J=/home/houp/.claude/jobs/c8f1289c/tmp; R=/home/houp/phoenix-rpi
build() {   # $1 = worktree
  GCC=$R/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc NM=$R/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm \
  LWIP_PORT=$R/sources/phoenix-rtos-lwip/port WIFI_PROBE=$R/tools/wifi-probe \
  SYSROOT=$R/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot NFSROOT=/srv/phoenix-rpi4-nfs-gcc16 \
  bash $1/wifi/rpi4-wifi/build-standalone.sh
}
./scripts/check-netboot-blob.sh | tail -1
./scripts/radio-ap-up.sh
```

⚠ Each `fwloadbench` load power-cycles WL_REG_ON through the **direct** mailbox FIFO
(`diag_mboxPower`, pre-existing, not via `/dev/vcmbox`). 40 loads means ~120 unarbitrated mailbox
transactions. Run the bench on a boot with no GPU/X client, and count
`mailbox tag … timed out AFTER the doorbell` lines (expect 0).

### Cycle F: `fwloadbench` (a2), 3 boots, `wifi/sdio-clock`

```
build $J/wt-devices-wifi
strings -a /srv/phoenix-rpi4-nfs-gcc16/bin/rpi4-wifi | grep -c 'FWLOAD-BENCH load='     # gate: 1
for n in 1 2 3; do
  ./scripts/test-cycle-psh-interact.sh --label fwbench-$n --inter-cmd-secs 8 --idle-secs 60 --max-cmd-secs 300 -- \
      "rpi4-wifi fwloadbench 40"
done          # Bash timeout 600000 per boot
grep -aE 'SDIO-CLK|SDHCI-POLL|SDHCI-TIMEOUT|FWLOAD-BENCH|timed out AFTER|Exception #' <log>
```

About 1–2 s per load is an estimate (power cycle 0.2 s, the fixed 300 + 50 + 240 ms of settle,
plus the download). If `--max-cmd-secs 300` cuts the bench, lower N rather than raising the timer
past 600 s. Per boot: 20 level + 20 legacy loads; over 3 boots, 60 + 60. Load 1 is the only cold load (the
image is paged in), and it goes to level unless `legacypio` is given. So run boot 2 as
`"rpi4-wifi legacypio fwloadbench 40"`: the cold slot is then legacy in one boot and level in two.
Grade load 1 separately from loads 2…40.

| outcome | reading |
|---|---|
| any **legacy** `SDHCI-TIMEOUT … space=1 wr_rdy=0` | **M1 confirmed** (lost edge), regardless of counts |
| legacy timeouts with `space=0`, and level loads with `slow_waits>0` whose `slow_max_us` is ≲ the `SDHCI-POLL` budget | **M2 confirmed** (card stall longer than the spin) |
| level 0/60 failures **and** legacy ≥ 3/60 | the fix works on warm loads (Fisher p ≈ 0.12 at 3, ≈ 0.03 at 5; the SDHCI-TIMEOUT bits carry the mechanism) |
| legacy 0/60 | the warm bench does not reproduce it. Consistent with a first-load trigger (page-in); grade on cycle N |
| any level failure | the fix is incomplete; its SDHCI-TIMEOUT line and `cmd53_max_us@off` say which way |
| `SDIO-CLK … measured` ≠ `base` by > 1 % | reopen a1 |
| `sd=` ≠ 399361 / 25000000 | the divider changed; void the run |

Record `SDHCI-POLL` ns/read: it is the first direct measurement of the M2 budget at core 500. An
optional comparison boot at `core_freq=250` (project `config.txt`) is the coordinator's call. It is
not needed to grade the fix.

### Cycle N: natural first loads, rolling (a2)

Every WiFi boot from now on prints `SDHCI-PIO`. Alternate boots between `rpi4-wifi &` (level, the
default) and `rpi4-wifi legacypio &`, in the `wake-N` form:

```
./scripts/test-cycle-psh-interact.sh --label wifinat-<k> --inter-cmd-secs 8 --idle-secs 60 --max-cmd-secs 160 -- \
    "rpi4-wifi &" "wifi status" "wifi status"                  # odd k
    "rpi4-wifi legacypio &" "wifi status" "wifi status"        # even k
```

Grade on the **first** load of each boot (`SDHCI-PIO` before any "did not start" line).
**The level-default boots must also carry bulk TX**, because this is the only cycle that exercises
the new loop on the run-time TX path (full-MTU frames go through `diag_sdioCmd53Write`; cycle T runs
on the other branch). On at least 3 level boots, append the cycle-T perf command and a `wifi stats`
to the list. Require `tx_ok` ≥ 8 600, `tx_err=0`, `resyncs=0`, `WIFISTATS pio … timeouts=0`, and TX
medians within the master range from cycle T's A arm. The
prediction under the page-in story: legacy first loads keep failing at the core-500 rate (≈ 2/13);
level first loads show 0, and on the would-be failures a `cmd53_ge2ms>0` with `slow_waits` small. At
that rate, 20 boots per arm are needed for 0/20 against the legacy rate to mean anything
(0.85²⁰ ≈ 0.04). Meanwhile `fw_alive=0` on the default build is the plain count to keep: **0 in the
next 20 default boots** is the pass line.

**Merge gate for `wifi/sdio-clock`:** cycle F shows no level failure, **and** the bulk-TX level
boots of cycle N pass. Cycle F alone covers only the download path.

### Cycle T: throughput A/B (b), `master` vs `wifi/throughput`, 4 boots ABAB

Stage the Pi side once: `sudo cp scripts/wifi-perf-pi.py /srv/phoenix-rpi4-nfs-gcc16/root/wifi-perf.py`
(it is not in the live export today; `/bin/python3` is). `/etc/wifi.conf` stays in place, so the
netif auto-joins. On the host, per boot, start `python3 scripts/wifi-perf-host.py 7777 4194304 3`
in the background before the cycle.

```
# A = master (same wifi code as 9c4267e), B = wifi/throughput
build $R/sources/phoenix-rtos-devices     # A
./scripts/test-cycle-psh-interact.sh --label wifithr-A1 --inter-cmd-secs 8 --idle-secs 60 --max-cmd-secs 200 -- \
    "rpi4-wifi &" "wifi status" "wifi status" \
    "python3 /root/wifi-perf.py 10.43.0.1 7777 10.43.0.89 4194304 3" "wifi stats"
build $J/wt-devices-wifithr               # B; gate: strings -a …/rpi4-wifi | grep -c 'WIFISTATS sbwin' = 1
# … same commands, label wifithr-B1; then A2, B2
```

The second `wifi status` must show `address: 10.43.0.89 (wl2)` before the perf command. If the join
is slow, add a third `wifi status`. **Validity gates, per boot:** the host log shows the peer
`10.43.0.89`; `WIFISTATS frames tx_ok` ≥ 8 600 (3 × 4 MiB / 1460), so the traffic really crossed
wl2; `WIFISTATS phy rate=` is recorded (B only).

| outcome | reading |
|---|---|
| B: `sbwin skips / (writes + skips)` ≥ 0.9 | the cache works as designed (mechanism check, independent of throughput) |
| B median (6 runs) > A **max** (6 runs), each direction | a gain; report medians and ranges, n = 6 per arm |
| otherwise | no measurable end-to-end change. Keep it only if skips ≥ 0.9 and no errors (it is cheaper per frame) |
| B `rx_err`/`tx_err`/`resyncs` above A | a stale window reached a transfer: revert |
| `phy rate` ≤ 72 Mbit/s and A ≥ 3 MB/s | the link is within ~2× of the air: re-rank levers 3–6 against the air, not the bus |

KNOWN-ISSUES F1 keeps "≈ 3 MB/s each way" until cycle T produces n ≥ 3 medians per arm.

---

## Notes for the coordinator

- The first-join fix and the download retry are unaffected; the retry stays as a backstop whatever
  cycle F shows.
- `docs/gpu-new-lane/core-clock-500.md` (G1 command list) and several other coord files contain
  the lab PSK verbatim. None of the cycles above needs it: the netif joins from the export's
  `/etc/wifi.conf`. Any `wifi connect` line added later should read
  `wifi connect PhoenixNet <psk from /srv/phoenix-rpi4-nfs-gcc16/etc/wifi.conf>`. Nothing outside
  this file was edited.
