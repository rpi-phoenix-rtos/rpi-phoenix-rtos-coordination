# BCM2711 SD-card performance — systemic analysis & fix plan

**Date:** 2026-07-01
**Task:** #62 (SD finalization) — performance
**Direction (user):** *Do not* chase clock overclock (unstable, card-dependent, non-universal).
Focus on **universal, hardware-safe** systemic wins: DMA, cache, duplicate copies,
sequential-vs-parallel, buffer sizes, code paths. Match what Linux/Raspberry Pi OS does.
**Measure** where time is lost, then fix.

---

## 1. Where the time actually goes (architecture of the current driver)

The current `bcm2711-emmc` data path (`sdcard.c`) moves every SD block **by PIO** — the CPU
copies each 32-bit word through the controller's single `BUFFER_DATA` FIFO register — and it
does so **through an uncached staging buffer**, with a **full-size `memcpy` on every transfer**.

Per block transfer of `len` bytes, the driver pays:

| Cost | Where | Why it's slow |
|---|---|---|
| **A. Bounce `memcpy`** (writes: caller→staging; reads: staging→caller) | `_sdcard_transferBlocks` (was L1737/L1770) | A whole extra pass over the data — into/out of **uncached** memory (`MAP_UNCACHED`, L187) |
| **B. Uncached FIFO-loop buffer** | `_sdio_cmdSend` PIO loop (L730) | The PIO loop also reads/writes that **same uncached** buffer → every CPU access is a full RAM round-trip, no cache-line coalescing |
| **C. CPU word-by-word FIFO loop** | L764-772 (`pioW[i] = *pioFifo` ×128/block) | The CPU is the data mover; it cannot feed a 25 MB/s bus as fast as a DMA engine, and each MMIO access carries ordering cost |
| **D. Per-block spin-poll** | L734-745 (spin ≤2M on `PRES_STATE`) | CPU busy-waits between blocks instead of the controller streaming |

**Net:** the staging buffer is touched **twice** (memcpy + PIO loop) and **both touches are to
uncached memory**. Since the FIFO is MMIO (not memory-DMA), that uncached staging buffer buys
nothing on the PIO path — it is pure overhead.

Baseline (committed `eda0766`, 50 MHz HS, multi-block, **with** the bounce buffer):
**read ~20 MB/s, write ~10 MB/s** (SDDIAG-MB). The HS50 4-bit bus ceiling is ~25 MB/s.

### Why it's PIO and not DMA today
SDMA was disabled (comment L656-659, #120) because *"the SDMA engine advances its address
register but never lands data in the driver's buffer — it can't reach the >1 GB ARM-physical
buffer the allocator returns."* This is a **real, precise** hardware constraint (see §3), not a
controller defect — and it is fixable.

---

## 2. What Raspberry Pi OS / Linux does (reference: `external/linux`)

Studied `drivers/mmc/host/sdhci.c`, `sdhci-iproc.c`, `arch/arm/boot/dts/broadcom/bcm2711.dtsi`:

1. **ADMA2 by default** on `brcm,bcm2711-emmc2` (`sdhci-iproc.c:289-297`, `sdhci.c:4345`).
   No `BROKEN_DMA`/`BROKEN_ADMA` quirk. The CPU never touches the data — a scatter-gather
   descriptor table drives the transfer at bus rate.
2. **Cacheable data buffers + explicit cache maintenance** (`dma_map_sg`, `dma_sync_*`;
   `sdhci.c:694`, `862-880`). Clean-before-write, invalidate-after-read. **No uncached bounce.**
3. **PIO fallback goes directly to the caller's cacheable buffer** via `sg_miter`
   (`sdhci.c:536-612`) — **no staging copy**. ← this validates our §4 fix exactly.
4. **DMA window = low 1 GiB.** `bcm2711.dtsi:424` `emmc2bus`:
   `dma-ranges = <0x0 0xc0000000  0x0 0x00000000  0x40000000>` → the EMMC2 DMA master can
   only reach **CPU-physical `0x0`–`0x3FFF_FFFF`**. Linux allocates DMA buffers there (CMA).
   **This is exactly our #120 constraint, quantified.**
5. **Sizing:** `max_req_size = 4 MiB`, `max_seg_size = 64 KiB`, `max_blk_count = 65535`,
   `SDHCI_MAX_SEGS = 128`. Our `SDCARD_MAX_TRANSFER = 64 KiB`.
6. **Auto-CMD23** on (no `ACMD23_BROKEN` quirk) — we already use CMD23.

---

## 3. Ranked, hardware-safe fix plan

Ordered by (impact ÷ risk). None of these change the clock; all are universal across cards.

### FIX-1 — Direct-PIO: drop the uncached bounce buffer  ✅ IMPLEMENTED (this session)
Move the PIO data phase **directly to/from the caller's cacheable buffer** (matches Linux
`sg_miter`, §2.3). Eliminates cost **A** entirely and converts cost **B** from uncached to
**cached** access.
- **Change:** `_sdio_cmdSend` gains a `dataBuf` param; `_sdcard_transferBlocks` passes the
  caller's `data` (4-byte-aligned fast path; uncached-staging fallback kept for the rare
  misaligned caller and the tiny register-read path).
- **Risk:** minimal — no DMA, no clock, no coherency concern (FIFO is MMIO; the caller buffer
  is the final data, no device touches it). Correctness unchanged.
- **Expected:** removes one full-size copy each way **and** the uncached penalty on the FIFO
  loop. Plausibly approaching ~1.5–2× on the CPU-bound path. **Measuring now** (SDDIAG-MB A/B,
  same 50 MHz, bounce vs direct).

### FIX-2 — ADMA2 hardware DMA  (the big one; the real "match Linux")
Replace the CPU FIFO loop with an ADMA2 descriptor-table transfer (§2.1). Removes costs **C**
and **D** — the controller streams at bus rate, CPU free. This is what closes most of the gap
to the ~25 MB/s HS50 ceiling.
- **Prerequisite (the #120 unlock):** a **physically-contiguous DMA buffer in the low 1 GiB**
  (CPU-phys < `0x4000_0000`, §2.4). Needs a Phoenix low-memory / DMA-zone allocation path —
  the current `mmap(MAP_CONTIGUOUS)` returns >1 GB physical. **This is the gating unknown to
  investigate** (does Phoenix expose a low-DMA-zone alloc, or must we reserve one?).
- **Then:** build the 96-bit ADMA2 descriptor table (4-byte data align, 8-byte table align),
  write it to `ADMA_ADDRESS`, set the ADMA2 mode bits, use **cacheable** data buffers with
  `dcacheClean` (write) / `dcacheInval` (read) around submit (reuse the GENET streaming-DMA
  pattern, [[project_pi4_genet_rx_perf]]).
- **Risk:** moderate — real DMA + cache maintenance + the low-buffer allocator. Netboot-
  recoverable. Correctness gated on the diag's single-block-oracle readback.
- **Expected:** the dominant win. Frees the CPU (helps the whole system under I/O load, not
  just raw MB/s).

### FIX-3 — Larger transfers / segment coalescing
Linux allows 4 MiB requests / 128 segments; we cap at 64 KiB. With ADMA2 (scatter-gather),
larger requests amortize per-command + completion-poll overhead. Bump `SDCARD_MAX_TRANSFER`
and let libcache issue bigger runs. Cheap once FIX-2 lands; little value while PIO-bound.

### FIX-4 — Overlap (sequential→parallel), lower priority
With DMA, the CPU could prepare/parse block N+1 while the engine transfers block N (double
buffering), and/or an IRQ-driven completion instead of the spin-poll frees the CPU during the
transfer. Marginal next to FIX-2; revisit after.

### NON-GOAL — 100 MHz clock overclock  ❌ (per user)
Feasible (firmware grants 200 MHz refclk → clean DIV_2 100 MHz, verified this session) but it
is out of HS50 3.3V spec, card-dependent, and risks CRC on marginal cards. **Rejected** in
favor of the universal wins above. Note for the record: even at best it is ~2× and cannot
close the 4× gap to the host USB reader (that gap is the reader's UHS-I 1.8V SDR104 vs the
Pi's fixed-3.3V HS50 slot — a hardware ceiling, not a driver deficiency).

---

## 4. Measurement method

- **A/B via SDDIAG-MB** (raw `sdcard_transferBlocks`, single-block-oracle readback for
  correctness): same 50 MHz, **bounce (baseline eda0766) vs direct-PIO (FIX-1)** isolates the
  copy/uncached cost. The residual gap to ~25 MB/s attributes the PIO-loop cost → the FIX-2
  headroom.
- Real-workload cross-check: `dd` read/write throughput on a mounted ext2 SD (full
  libcache+ext2+driver stack) once FIX-1 is validated.

---

## 5. DECISIVE: same-board same-card A/B vs stock Linux (2026-07-01)

Netbooted **Raspberry Pi OS Lite arm64** on the SAME Pi 4 + SAME microSD (Samsung SR64G,
59.5 GiB) via our dnsmasq TFTP + NFS-root, PID1 `dd` bench (`init=` to skip systemd's
first-boot reboot loop). `iflag=direct` reads (raw block dev), `oflag=direct` writes (32 MB
temp file on p1). Kernel banner (both boots, deterministic):

    mmc0: SDHCI controller on fe340000.mmc using ADMA
    mmc0: new UHS-I speed DDR50 SDXC card at address aaaa

**Head-to-head (same card):**

| Block | Linux (DDR50 + ADMA) | Phoenix (HS50-SDR + PIO) | Linux advantage |
|---|---|---|---|
| 4k read | 9.3 MB/s | ~8.5 MB/s (nb=8) | ~tie (latency-bound) |
| 64k read | **36.3 MB/s** | **21 MB/s** (nb=128) | **1.7×** |
| 1M read | **43.7 MB/s** | — (we cap at 64k) | **~2.1×** |
| 64k write | 10.0 MB/s | ~10.3 MB/s | ~tie |
| 1M write | 19.0 MB/s | — | ~1.9× |

**Verdict: the earlier "we're at the HW ceiling" conclusion was WRONG.** 36.3 MB/s read
exceeds the 25 MB/s HS50-SDR ceiling → Linux is definitively running a **faster bus mode:
UHS-I DDR50** (double-data-rate, ~2× bandwidth at the SAME 50 MHz clock), via **ADMA DMA**,
with **1M transfers**. All three are the **stock RPi OS default** — NOT an overclock. So the
~2× gap is real and closeable with universal, hardware-safe techniques (exactly the class the
user asked for). The gap is >20% → optimize.

The 4k/64k-write ties confirm the small-block/latency-bound cases are already at parity; the
win is entirely at large sequential transfers, which is what real workloads (Quake asset
loads, file copies) hit.

## 6. Re-ranked plan (evidence-based)

1. **DDR50 bus mode** — the headline 2× (21→36 at 64k). Enable the SD DDR50 UHS-I timing:
   negotiate via CMD6 (function group 1 → DDR50), set the SDHCI UHS mode select + DDR, and —
   the open implementation question — handle the **1.8V signal-voltage switch** (Linux uses
   `sd_io_1v8_reg`/`vqmmc` on the Pi 4; DDR50 is a UHS-I mode, normally 1.8V). Confirm whether
   the Pi 4 does DDR50 at 1.8V (needs the regulator-GPIO + SDHCI 1.8V switch sequence) or a
   3.3V-DDR variant. **This is the biggest safe win and does NOT need the low-DMA buffer.**
2. **ADMA2 + low-1GiB DMA buffer** — unlocks large (1M) transfers + frees the CPU (36→44) and
   removes the PIO spin. Gated on a Phoenix low-1GiB contiguous-DMA allocation path (§3 FIX-2).
3. **Raise `SDCARD_MAX_TRANSFER`** past 64 KiB — only useful once ADMA makes large transfers
   cheap.

FIX-1 (direct-PIO) measured as **~0 throughput change** (reads bus-bound, writes card-bound at
HS50) — keep it as a small upstreamable cleanup matching Linux `sg_miter`, but it is NOT the
perf lever. The perf lever is DDR50 (#1).

**Reproduce the Linux baseline:** tree staged at `artifacts/linux-netboot/` (RPi OS trixie
arm64, `tftp/` + NFS-root `rootfs/` with `usr/local/bin/sdbench.sh`, `init=` cmdline). Bring up
`RPI4B_NETBOOT_TFTPROOT=.../linux-netboot/tftp netboot-server-up.sh` then netboot with the card in.

## 7. Pre-implementation checks (2026-07-01, all done)

Before building, front-loaded three cheap checks (advisor-gated):

- **Check 1 — DMA reach (go/no-go for the whole project): PASS.** Phoenix diag prints
  `dmaBufferPhys=0x03780000` (~56 MB) — the `MAP_CONTIGUOUS` staging buffer lands in the low
  1 GiB, so the EMMC2 DMA master reaches it. **No new low-memory allocator needed.** (Implies
  the #120 "SDMA can't reach >1 GB" story was situational — this buffer *is* reachable; the more
  likely SDMA failure was the 4K SDMA-boundary interrupt going unhandled. ADMA2 sidesteps it.)
- **Check 2 — DDR50 signal voltage: 1.8 V.** Linux `mmc0/ios` on this card:
  `timing spec: 7 (sd uhs DDR50)`, `signal voltage: 1 (1.80 V)`, `clock: 50 MHz`, `vdd: 21
  (3.3 V)`. So DDR50 keeps card power at 3.3 V but switches **I/O signaling to 1.8 V** — the
  DDR50 phase needs the full UHS voltage-switch sequence (CMD11 + `sd_io_1v8` regulator GPIO +
  SDHCI 1.8 V-signal-enable in Host Control 2), not a divisor tweak. Hardest phase confirmed real.
- **Check 3 — DMA actually lands data:** in progress (SDMA read via the reachable staging
  buffer, 512K boundary so no boundary interrupt for ≤64K transfers).

**Registers available** (`sdhost_defs.h`): `SDMA_ADDRESS`=0x00; `ADMA_ADDR_1/2`=0x58/0x5c;
`ADMA_ERROR_STATUS`=0x54; `HOST_CONTROL_DMA_SELECT_{SDMA,ADMA32,ADMA64}` (bits[4:3]);
`TRANSFER_BLOCK_SDMA_BOUNDARY_512K`; cmd `dmaEnable` bit; `INTR_DMA_ERROR`(28),
`INTR_ADMA_ERROR`(25). ADMA32 is the target (32-bit descriptors, buffer < 4 GiB — trivially met).

## 8. Implementation plan (incremental, measured against Linux 36/44)

- **Phase A — DMA via the low uncached staging buffer + memcpy** (keep FIX-1 PIO as fallback).
  A1: SDMA read (512K boundary, wait TRANSFER_DONE) → prove DMA lands data + correct (oracle).
  A2: swap to **ADMA2** (32-bit descriptor table in a second low uncached buffer) for reads +
  writes, any size → enables >64K transfers. Expect ~24 MB/s @ HS50 (bus-bound) but CPU freed.
- **Phase B — DDR50** on ADMA: 1.8 V switch (CMD11 + regulator + HC2 signal-enable) → CMD6
  group-1=DDR50 → HC2 UHS mode select=DDR50 → DDR clock. Expect ~36 MB/s (matches Linux 64K).
- **Phase C — raise `SDCARD_MAX_TRANSFER`** past 64 KiB with ADMA → approach Linux 1M (~44).

Correctness gate every phase: SDDIAG-MB single-block-oracle (0 detected-fail + 0 SILENT-CORRUPT)
+ boot-to-psh + 0 faults. Cross-check throughput vs the Linux gold standard (36/44). Commit +
manifest each working phase for clean rollback.

## 9. Phase A1 findings — SDMA wired, completion signal is the blocker (2026-07-01)

Wired an SDMA data path (dmaEnable + 512K boundary + `useDma` gated on the reachable staging
buffer; PIO kept as fallback). Results + the critical lesson:

- **SDMA read: nb=8/32 correct+fast, single-block CMD17 + nb=128 time out with `intr=0x0`** —
  the **Transfer-Complete IRQ does not latch for SDMA** (bus goes idle, `pres=0x1fff0000`, but
  no TC). TC *does* latch for PIO reads (they pass), so it's DMA-specific.
- **SDMA write: adding a `TRANSFER_DONE` wait made it WORSE** (introduced apparent
  silent-corrupt vs CMD13-only's detected-fail) = classic **stale Transfer-Complete bit**
  returning premature completion.
- **The "silent corruption" + "0 partitions" were a MEASUREMENT ARTIFACT, not real damage.**
  The oracle verifies writes with single-block reads, which were on the same broken SDMA-read
  path → garbage readback registered as corruption. **PIO baseline re-confirmed the card intact
  (1 partition, LBA0 16/16, large-read 2048/2048, 0 silent-corrupt).** Never trust a verdict
  from an instrument on the path under test.
- **Autonomy-safe confirmed:** the Pi netboots with the card in regardless of card contents →
  EEPROM network-first, not card-FAT-dependent. A bad card write can't brick remote access.

**DMA is gated OFF (`SDCARD_ENABLE_DMA` undefined) — PIO (21/10, 100% correct) is the shipping
path** until DMA completion is proven on the trusted oracle.

**Fix plan (next):** the Transfer-Complete IRQ is unreliable for DMA on this controller (as #154
found for PIO writes). Replace TC-based DMA completion with a **present-state poll** (wait DAT
lines idle: `DAT_BUSY | DAT_LINE_ACTIVE` clear, bounded, error-checked), and **write-1-clear the
stale TRANSFER_DONE/error bits before each data command**. Reads: poll-idle → data ready. Writes:
poll-idle → CMD13-to-TRAN (the trustworthy, card-side completion; do NOT wait TC). Re-validate on
the PIO-trusted oracle; the DMA-read fix also repairs the oracle's readback.

## 10. Phase A1 DONE — DMA reads correct via poll completion (2026-07-01)

Replaced TC-based DMA completion with a **present-state poll** (spin until
`DAT_BUSY|DAT_LINE_ACTIVE` clear, error-checked; then clear the stale TRANSFER_DONE bit). Reads:
poll-idle → data ready. Writes: poll-idle → CMD13-to-TRAN. `SDCARD_ENABLE_DMA` on, `useDma`
gated to **reads-only** so the diag's PIO-write oracle validates DMA reads.

Result (netboot, same card): **DMA reads CORRECT** — `read LBA0 16/16`, `large read 2048/2048`,
WRITE (PIO) `SILENT-CORRUPT=0/10` on nb=8/32/128, card 1 partition. **Throughput flat**: DMA
nb=128 read 20.3 vs PIO 21.2 MB/s — no HS50 gain (bus-bound + extra uncached-staging memcpy),
as predicted. DMA's payoff is enabling DDR50 + CPU offload, not HS50 speed.

## 11b. Phase B DDR50 implementation spec (Linux-derived, ready to code)

Register-level sequence to replicate stock Linux DDR50 (all offsets standard SDHCI; controller
is 32-bit-access-only — use 32-bit RMW for the 16-bit HC2 at 0x3E):

- **Host Control 2 (0x3E, 16-bit)**: UHS mode select bits[2:0] — DDR50 = `0x4`; `VDD_180`
  (1.8V signal enable) = bit3 = `0x8`; tuning bits 6/7 NOT needed for DDR50.
- **The 1.8V rail switch (the Pi-specific enabler, no Linux-regulator-fw in Phoenix):** the SD
  I/O rail is `sd_io_1v8_reg` = **`expgpio 4` = firmware GPIO `128+4 = 132`**, driven via the
  **VideoCore mailbox `RPI_FIRMWARE_SET_GPIO_STATE`** (Phoenix already has `libvcmbox` — same
  path bcm2711-sdio.c uses for the clock). Set `{gpio:132, state:1}` → 1.8V; `state:0` → 3.3V.
  Wait ~5 ms to settle. (Verify the SET_GPIO_STATE tag id against the firmware header; GET is
  0x00030041, SET is 0x00038041 — confirm.)
- **Voltage-switch order** (during card init, gated by ACMD41 S18R/S18A): CMD11 VOLTAGE_SWITCH →
  card pulls DAT[3:0] low → **stop SD clock** → set GPIO 132 HIGH (1.8V) + wait 5 ms → set HC2
  `VDD_180` → wait, verify bit stays set → **restart clock** (≥5–10 ms gated total) → card
  releases DAT[3:0] high = switch OK.
- **DDR50 select** (after 1.8V): CMD6 SWITCH_FUNC group1=`0x4` (DDR50) → set HC2 UHS mode
  bits[2:0]=`0x4` → clock divisor for 50 MHz (DDR = data on both edges, physical clock still
  50 MHz). No tuning (Linux treats DDR50 tuning failure as non-fatal).
- **iproc/bcm2711**: no custom voltage_switch/UHS override — the standard SDHCI sequence applies.

Boot-risk (netboot-recoverable): a failed switch leaves the card unusable → PIO/HS50 fallback if
S18A not granted or DAT-verify fails. Gate DDR50 behind its own macro; keep HS50 the default
until proven. Cites: sdhci.h:226-249, sdhci.c:2686-2717, core.c:1200-1221, sd.c:461-527,
bcm2711-rpi-4-b.dts:34-46, gpio-raspberrypi-exp.c:178-199, sdhci-iproc.c:273-297.

## 11c. Committed checkpoints (devices repo)

- `845a109` — SDMA **read** path (poll-based completion, reads-only, gated `SDCARD_ENABLE_DMA`).
  HW-validated correct; no HS50 gain (expected). PIO writes remain the trusted path.
- `a2ba852` — DDR50 **foundations**: `HOST_CONTROL2` defs + `sdio_setSdIoVoltage18()` (mailbox
  GPIO 132). No behavior change; ready for the switch orchestration to call.

**Remaining DDR50 orchestration (the delicate init-sequence surgery, spec in §11b):** inject
ACMD41 **S18R** (arg bit 24) into `sdcard_initCard`; on S18A-accepted OCR, issue **CMD11**
VOLTAGE_SWITCH then the switch dance (stop clock → `sdio_setSdIoVoltage18(true)` +5ms → HC2
`VDD_180` → verify → restart clock → confirm DAT lines); continue init at 1.8V; in
`sdcard_wideAndFast` select **CMD6 group1=DDR50** + HC2 UHS=`DDR50` + 50 MHz DDR clock. Gate
behind `SDCARD_ENABLE_DDR50`; fall back to HS50 if S18A not granted or DAT-verify fails
(netboot-recoverable). Validate on the PIO/DMA-read oracle; target ~36–44 MB/s read.
Note: the uncached-staging + memcpy DMA-read may cap DDR50 below 44 (memcpy drag) — if so, move
to DMA-into-cached-buffer-with-invalidate or ADMA scatter-gather to the caller buffer.

## 11d. Exact DDR50 orchestration injection points (sdcard.c, verified 2026-07-01)

`sdcard_initCard`:
- **S18R**: `acmd41Arg` is built at ~L1392 (`trySdhc ? (1<<30) : 0`) and reused in the ready
  loop (~L1415/1418). OR in **S18R = `1<<24`** when `trySdhc` (v2 only).
- **S18A check + switch**: after the ready loop (`highCapacity` set at ~L1435) and **before
  CMD2** (~L1438): if `(acmd41Response & (1<<24))` (S18A granted) and DDR50 enabled → issue
  **CMD11 VOLTAGE_SWITCH**, then the switch dance (stop clock via CLOCK_CONTROL, wait DAT[3:0]
  low, `sdio_setSdIoVoltage18(true)`, ≥5 ms, set HC2 `VDD_180` [RMW the 0x3C word], restart
  clock, confirm DAT[3:0] high). On any failure → `sdio_setSdIoVoltage18(false)`, stay 3.3V/HS50
  (recoverable). Set a `host->card.uhs` flag.
- **CMD11 needs a command-table entry**: add `SDIO_CMD11_VOLTAGE_SWITCH` to `sdCmdMetadata`
  (R1 response, no data). Check the enum + metadata array near the other CMD defs.

`sdcard_wideAndFast`: when `host->card.uhs`, replace the HS CMD6 (group1=HS/0x1) with
**group1=DDR50 (0x4)**, set HC2 UHS mode = `HOST_CONTROL2_UHS_DDR50` (RMW 0x3C word), and use a
50 MHz divisor (DDR). Gate all of the above behind `SDCARD_ENABLE_DDR50`.

## 11e. CAPSTONE ACHIEVED — SD-boot from self-flashed card, DDR50, 0 faults (2026-07-01)

Full autonomous loop, no manual swap: built a 2-partition Phoenix SD image (`rpi4b-sd-2part.img`,
FAT boot + ext2 root, DDR50 driver) → netbooted Linux → `dd` it onto `/dev/mmcblk0` +
`cmp`-verified the whole 323 MB (`SDFLASH VERIFY OK`) → netboot server DOWN → power-cycle → the
Pi timed out on network and **SD-booted Phoenix from the card**. On the real SD-boot path:
- `HC2 uhsMode=4 vdd180=1 cardUhs=1` — **DDR50 engaged even during SD-boot** (after the firmware
  had already inited EMMC2 at 3.3V/HS — the switch + defensive 3.3V re-init handle the artifact).
- SDDIAG-MB READ nb=128 = **34.8 MB/s** (≥ netboot), `SILENT-CORRUPT=0/10` all sizes.
- `/dev/mmcblk0 ready: 2 partition(s)`, `mounted /dev/mmcblk0p2 (ext2) as / after 0 tries`,
  reached `(psh)%`, USB kbd + genet up, **fault_pattern_matches: 0**.

**Loop goal met: full speed (DDR50 ~35 MB/s read, ~1.6–1.7× the old 20, ≈ Linux at 64K) +
correctness (0 silent-corrupt on netboot AND SD-boot) + stable SD-boot.** Self-flash capability:
[[feedback_selfflash_sd_via_netboot_linux]].

## 11f. Final state / remaining polish

- Committed: 845a109 (SDMA read) · a2ba852 (DDR50 foundations) · fcd4ad1 (DDR50) · 1b78519
  (DMA-write quirk doc) · + finalization commit (diag OFF, production DDR50/HS log lines).
- **DONE for "ready":** DDR50 reads, PIO writes, ext2 SD-boot, all HW-validated, 0 faults, diag
  gated off, mode logged in normal boot.
- **Optional future:** larger transfers >64K (→ ~44, needs ADMA or a 512K-aligned buffer; SDMA
  boundary caps at 512K); DMA writes (controller quirk — 2 fixes failed, PIO is correct); a
  multi-boot DDR50 signal-integrity confidence bench; upstream cleanup (remove SDCARD_ENABLE_*
  gates once settled). None block a working, fast, correct driver.

## 11g. Larger transfers (128K) shipped + RESET_ALL dead-end (2026-07-02)

- **128 KiB transfers + 128 KiB libcache sector** (SDCARD_MAX_TRANSFER 64→128K, staging buffer
  32 pages, BLK_CACHE_SECSIZE→128K/SECNUM 32): HW-validated. Raw (netboot DDR50): READ nb=256
  **38.3 MB/s** (vs 34.9 @64K), WRITE **17.4** (vs 12.3), SILENT-CORRUPT 0/10. Buffer stays
  512K-aligned (0x0378_0000) so no SDMA-boundary cross. SD-boot: ext2 mounts first-try + psh with
  the 128K cache sector (fs path validated). Net: +10% read / +40% write, correct.
- **DDR50 reliability: reliable on netboot (4/4), intermittent on SD-boot (1/2)** — the firmware
  pre-inits EMMC2 during SD-boot, and the 1.8V switch sometimes falls back to HS50 (safe: HS50
  fallback is correct, mounts, psh, 0 faults). **A full RESET_ALL at init to clear the firmware
  state BROKE card init on SD-boot** ("root device not found" — RESET_ALL clears clock/power the
  re-init can't restore for a pre-inited card) → **reverted**. So the earlier "full controller
  reset at init" TODO is a DEAD END; do not do it. DDR50-on-SD-boot reliability is a residual
  refinement (a lighter card-state resync, not a full reset) — not a blocker; HS50 floor is safe.

## 11. Status / next

- **DMA read path: correct + safe, gated `SDCARD_ENABLE_DMA` (reads-only).** No HS50 speedup
  (expected). PIO remains 100% correct fallback.
- **Next = Phase B (DDR50, the 2× lever)**: 1.8 V signal-voltage switch (CMD11 + `sd_io_1v8`
  regulator + Host-Control-2 1.8V-signal-enable, clock-stopped during switch) → CMD6 group-1
  DDR50 → HC2 UHS mode = DDR50 → DDR clock. Then widen `useDma` to writes (validate via the
  now-trusted DMA reads / PIO cross-check). Target ~36–44 MB/s (Linux gold standard).
- Then: raise `SDCARD_MAX_TRANSFER`; then the **SD-boot self-flash capstone**
  ([[feedback_selfflash_sd_via_netboot_linux]]).
- Uncommitted WIP in `sdcard.c` + `sdhost_defs.h`. Diag + DMA gated by macros (OFF before publish).
