# SD-card driver cross-OS comparison — BCM2711 EMMC2 single-block WRITE failure

Read-only comparative analysis. No source modified. Scope: why single-block
**writes** (CMD24) fail at 50 MHz on our BCM2711 EMMC2 driver while single-block
**reads** mostly work (occasional Data-CRC that recovers on retry), and why a
failed write can wedge the controller.

References fetched as primary source (raw GitHub / kernel tree), 2026-06-07:
- Linux `drivers/mmc/host/sdhci.c` (core PIO + error/reset + command-inhibit)
- Linux `drivers/mmc/host/sdhci-iproc.c` (BCM2711 EMMC2 variant + quirks)
- U-Boot `drivers/mmc/sdhci.c` (SDHCI core) + `drivers/mmc/bcm2835_sdhci.c`
- Circle `addon/SDCard/emmc.cpp` (rsta2, bare-metal Pi4 EMMC2)

Our driver: `sources/phoenix-rtos-devices/storage/bcm2711-emmc/`
(`sdcard.c`, `sdhost_defs.h`, `bcm2711-sdio.c/.h`, `sdstorage_dev.c`).

---

## Symptom split (the lens used to rank)

Every candidate is tagged with which half of the symptom it explains, and a
discrepancy that cannot explain the **read-works / write-fails asymmetry** is
down-ranked even if it is a real divergence:

- **(a)** the CRC/timeout on the transfer itself
- **(b)** the controller/card "wedge" after a failed transfer

Reads leave the card in TRAN/DATA and never enter the PRG (programming-busy)
state; writes put the card into RCV then PRG and hold DAT0 busy. That single
asymmetry is the discriminator.

---

## Executive summary — top 5 ranked discrepancies

### 1. MISSING — no card-side recovery after a write data error (→ wedge, b)
On a data CRC/timeout our error path resets only the **host** CMD/DAT circuits
(`sdcard.c:535-537`, `_sdio_cmdExecutionWait` `sdcard.c:270-276`). A host reset
does **not** reset the *card*: after a write CRC the card is left in RCV/PRG. The
glue then re-issues the identical CMD24 (`sdstorage_dev.c:195-201`) with no CMD12
abort and no CMD13 poll-to-TRAN, so the retry meets a card that is not in TRAN /
still busy on DAT0 → it wedges or NAKs. Circle handles exactly this by
invalidating the RCA and forcing a full `CardReset()` on the next access
(`emmc.cpp` `DoDataCommand`); Linux issues a stop/CMD12 for open-ended transfers
and on error, then resets (`sdhci.c:2136-2149`, `__sdhci_finish_data_common`).
Reads never enter PRG, so the same controller-only reset is enough for them —
which is precisely why reads recover and writes don't.
**Fix (Pi4/driver-specific):** after any write data error, before retrying,
issue CMD13 (SEND_STATUS) and poll the card current-state back to TRAN (and
CMD12 if a multiblock was in flight); if it won't return to TRAN, re-init the
card. Keep this inside the bcm2711-emmc driver, not the libstorage retry loop.

### 2. ORDER — next-command busy-wait returns `-EBUSY` instead of waiting through programming (→ cascade, b) — coupled with #1
**This is largely the mechanism by which #1 manifests, not a fully independent
cause.** After a write data error #1 leaves the card in PRG holding DAT0; the
very next command then hits the bounded poll below and bails `-EBUSY` — that is
the "one failed write → failed MBR read" cascade. #2 also bites *independently*
of any error on legitimate long-programming back-to-back writes (slow card,
programming > ~100 ms), so it is worth fixing on its own, but a reader should not
treat "lengthen the poll" as also fixing #1's missing card recovery.

Before issuing any command, `_sdio_cmdSend` polls `PRES_STATE_BUSY_FLAGS`
(= DAT_BUSY|CMD_BUSY) for only ~100 ms (1000×100 µs) then returns `-EBUSY`
(`sdcard.c:377-390`). `PRES_STATE_DAT_BUSY` is the SDHCI **DATA_INHIBIT** bit =
card still programming. The reference pattern is to *wait on* that inhibit as a
normal precondition, not to bail: U-Boot's core waits while
`PRESENT_STATE & (CMD_INHIBIT|DATA_INHIBIT)` and only strips DATA_INHIBIT for
stop/no-data commands; Linux sets `mask = SDHCI_CMD_INHIBIT; if (data_line_cmd)
mask |= SDHCI_DATA_INHIBIT;` and clears it only for the stop command
(`sdhci.c:1778-1785`). NB per the SDHCI spec, on the **success** path Transfer
Complete already spans DAT0 busy-release, so this is an **error-path /
back-to-back-write** phenomenon: after a failed or slow write the card holds
DAT0 and our short bounded poll trips `-EBUSY`, turning one bad write into a
failure of the *next* unrelated command (e.g. the MBR re-read).
**Fix:** lengthen/condition the pre-command DAT0 wait to the card-programming
timeout (CMD13-driven), and never return `-EBUSY` while DAT0 is legitimately
busy from a prior write — wait it out.

### 3. EXTRA — dead SDMA programming on the PIO path (cleanliness, neither a nor b)
`_sdio_cmdSend` programs `SDHOST_REG_SDMA_ADDRESS` and a `dmb` barrier
(`sdcard.c:420-421`) and sets `TRANSFER_BLOCK_SDMA_BOUNDARY_4K`
(`sdcard.c:422-425`) even though `dmaEnable = 0` (`sdcard.c:433`) — pure PIO.
The SDMA address register is ignored by the controller in PIO mode. Harmless but
misleading; not a cause of the write failure. Flag for removal/clarification on
upstreaming. (Per advisor: do **not** tag this MEMORY/barrier as the bug.)

### 4. MISSING — no Auto-CMD12 error / multiblock-read-ACMD12 handling parity (multiblock only, a)
For BCM2711 Linux sets `SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12` (bcm2711 quirks =
*only* that). Our multiblock path enables `autoCmd12Enable`
(`sdcard.c:439-442`) but the Auto-CMD12 error-status register
(`SDHOST_REG_AUTOCMD12_ERROR_STATUS`, defined `sdhost_defs.h:104`) is never
read, and `SDHOST_INTR_AUTO_CMD12_ERROR` recovery just resets the host like any
other error. This is consistent with multiblock CMD18/CMD25 being sidestepped
today; it is **not** the single-block write cause, but must be addressed before
re-enabling multiblock. Down-ranked: does not affect single-block CMD24.

### 5. EXTRA/ORDER — full-system `dmb sy` per MMIO vs reference register-write fences (memory, neither)
We emit `dmb sy` around the command launch and clock writes
(`sdcard.c:142,421,446,1222`; `bcm2711-sdio.h:42-48`). The references rely on
`writel`/`readl` ordering plus a *device-specific timing* delay, not a full
barrier: U-Boot bcm2835 inserts a **"twoticks" consecutive-write delay** on every
register **except `SDHCI_BUFFER`** (`bcm2835_sdhci.c:67-82`) and Linux iproc adds
a post-write `udelay` only when `host->clock <= 400000` (`sdhci-iproc.c`,
`sdhci_iproc_writel`). Our `dmb sy` is correct for ordering and is **not** a
plausible write-data-corruption cause (uncached DMA buffer + Device-MMIO FIFO
carry a data dependency; consecutive Device stores are ordered). NB the
twoticks/iproc delay quirks apply to the **legacy bcm2835/Cygnus** controllers,
not the bcm2711 EMMC2 (Linux bcm2711 does not set them) — listed only to record
that we ruled it out.

### NON-finding (explicitly retracted): HIGH_SPEED control bit
Earlier hypothesis was that we wrongly set `HOST_CONTROL_HIGH_SPEED` at 50 MHz.
**Verified false.** Linux `sdhci-iproc.c` `sdhci_bcm2711_pltfm_data.quirks =
SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12` — it does **NOT** set
`SDHCI_QUIRK_NO_HISPD_BIT` (that flag is on `sdhci_iproc_pltfm_data`, cygnus, and
bcm2835 — *different* controllers). So Linux **does** drive the HS bit on the
Pi4 EMMC2; our `HOST_CONTROL_HIGH_SPEED` at 50 MHz (`sdcard.c:1213-1215`) matches
Linux and is correct. (Circle also reaches HS purely via the clock divider and
sets no CONTROL0 HS bit, but that is a valid alternative, not a contradiction.)
This is **not** a discrepancy and is not in the ranked list.

---

## Detailed step-by-step comparison — single-block WRITE (CMD24)

| Step | Ours (`bcm2711-emmc`) | Linux sdhci.c | U-Boot sdhci.c | Circle emmc.cpp |
|---|---|---|---|---|
| Wait prev busy before cmd | poll `PRES_STATE_BUSY_FLAGS` ~100 ms then **`-EBUSY`** (`sdcard.c:377-390`) | wait while `PRESENT_STATE & (CMD_INHIBIT\|DATA_INHIBIT)`, strip DATA_INHIBIT only for stop (`sdhci.c:1778-1785`) | same inhibit wait, exponential backoff `cmd_timeout` | `TimeoutWait` on inhibit |
| Program block size/count | `TRANSFER_BLOCK = (blkCount<<16)\|boundary\|len` (`sdcard.c:422-425`) | shadowed BLKSIZE/BLKCNT, combined 32-bit write at cmd issue | `BLKSIZE`/`BLKCNT` writes | `BLKSIZECNT = size\|(count<<16)` |
| Program ARG then CMD | ARG (`sdcard.c:445`), `dmb`, then `CMD` last (`sdcard.c:449`) | ARG then CMD | ARG then CMD | `ARG1` then `CMDTM` |
| Push data to FIFO, gating | poll **level** `PRES_STATE_BUFFER_WRITE_ENABLE` per block, then store words (`sdcard.c:499-531`) | `while (PRESENT_STATE & SDHCI_SPACE_AVAILABLE)` (`sdhci.c:1037-1050`) | check `PRESENT_STATE & SPACE_AVAIL` before `transfer_pio` | `TimeoutWait(INTERRUPT, WRITE_RDY)` then store |
| Wait transfer complete | wait `TRANSFER_DONE` IRQ, 1 s (`sdcard.c:541`) | wait `SDHCI_INT_DATA_END` | `while(!(stat & INT_DATA_END))` | `TimeoutWait(INTERRUPT, 0x8002)` |
| Wait card-not-busy (DAT0/PRG) after write | **none here**; deferred to next cmd's bounded `-EBUSY` poll | next cmd blocks on DATA_INHIBIT; R1b busy via INT_DATA_END | next cmd blocks on DATA_INHIBIT | delegated to `EnsureDataMode()` → **CMD13** |
| Read response | `RESPONSE_0` (`sdcard.c:552-556`) | response regs | response regs | RESP0 |
| On data error | reset host **CMD+DAT only** (`sdcard.c:535-537`); glue re-issues same CMD24 (`sdstorage_dev.c:195`) | reset CMD+DATA (`sdhci.c:2136-2149`) **+ MMC core re-queues / stops card** | reset + return error to mmc core | **invalidate RCA + full CardReset** next access |

The two write-specific gaps are the last two rows: **no card-side
busy/recovery handling** after the transfer and on error. Reads have neither
need (no PRG state), which is the asymmetry.

## Detailed comparison — controller reset / init / clock

| Step | Ours | References |
|---|---|---|
| Full reset at init | `RESET_ALL`, poll self-clear (`sdcard.c:1142`, `sdhost_reset` `:139-151`) | same (`SDHCI_RESET_ALL`, poll) |
| Internal-clock-stable → SD-clock-enable | set div + `START_INTERNAL_CLOCK`, poll `INTERNAL_CLOCK_STABLE`, then `START_SD_CLOCK` (`sdcard.c:1221-1228`) | identical ordering in all refs |
| Register-write delay quirk | none (full `dmb sy` only) | bcm2835 twoticks delay on all regs except BUFFER (`bcm2835_sdhci.c:67-82`); iproc `udelay` when clock≤400 kHz — **both for legacy controllers, not bcm2711** |
| 16/8-bit register access | all 32-bit `volatile uint32_t*` (matches Arasan 32-bit-only requirement) — **OK** | iproc shadows TRANSFER_MODE/BLOCK_SIZE/BLOCK_COUNT and writes 32-bit (same reason) |
| HS bit at 50 MHz | `HOST_CONTROL_HIGH_SPEED` set (`sdcard.c:1213-1215`) | Linux bcm2711 **does** drive HS bit (no NO_HISPD quirk) — **matches** |
| STATUS_ENABLE for PIO bits | sets `RW_READ_READY\|RW_WRITE_READY` in STATUS_ENABLE (`sdcard.c:1161-1162`) — **OK / required** | refs enable buffer-ready status |

## Detailed comparison — error recovery / re-arm

| Step | Ours | References |
|---|---|---|
| What is reset on data error | host CMD + DAT (`sdcard.c:270-276,535-537`) | Linux CMD+DATA (`sdhci.c` REQUEST_ERROR) |
| Re-wait clock-stable after reset | no (RESET_CMD/DAT don't touch clock) — OK | Linux re-applies only on RESET_ALL |
| Card-side recovery | **none** — re-issue same CMD24 (`sdstorage_dev.c:195-201`) | Circle: invalidate RCA + CardReset; Linux MMC core: CMD12/CMD13 + re-queue |
| Multiblock Auto-CMD12 error | `AUTOCMD12_ERROR_STATUS` never read (`sdhost_defs.h:104`) | Linux reads Auto-CMD12 error status |

---

## Conclusion — single most likely cause of write-fail-at-50MHz, ranked

1. **(b) wedge:** missing card-side recovery after a write data error
   (discrepancy #1). Best explains "a failed transfer can wedge the controller"
   and the read/write asymmetry.
2. **(b) cascade:** the bounded pre-command DAT0 poll returning `-EBUSY` during
   legitimate card programming (discrepancy #2). Best explains "one failed write
   turns into a failed MBR read."
3. **(a) the transient per-block CRC is most likely NOT write-specific.** We
   found **no write-specific data-path divergence in the code** and the
   references reveal no extra write-data fence or delay that the bcm2711 EMMC2
   requires (the twoticks/iproc delays are legacy-controller quirks). The honest
   reading: the per-block transient CRC rate is plausibly the *same* for reads
   and writes (same clock, same lines, direction-agnostic CRC); what differs is
   the **consequence**. On a read a single transient CRC is recovered by a plain
   re-issue; on a write the same transient CRC is converted into a *terminal*
   failure + controller wedge by the missing card recovery (#1) and the `-EBUSY`
   bail (#2). So the real answer to "write-fail-at-50MHz" is **the broken
   recovery cascade (#1+#2), not a write-specific signal-margin defect.** Any
   residual transient CRC floor is a board/signal issue that the existing retry
   will absorb *once #1/#2 make a write retry actually recover the card.*

The actionable, Pi4/driver-local fixes are #1 and #2 (#2 as part of #1's
recovery); both live entirely in `bcm2711-emmc/sdcard.c` (+ the driver's own
recovery), not in ext2/libstorage.

---

## Citations

### Our source
- `sources/phoenix-rtos-devices/storage/bcm2711-emmc/sdcard.c`
  - `sdhost_reset` :139-151
  - `_sdio_cmdExecutionWait` reset-on-error :270-276
  - pre-command busy poll → `-EBUSY` :377-390
  - SDMA addr + barrier on PIO path (dead) :420-425, dmaEnable=0 :433
  - ARG/CMD launch + dmb :445-449
  - PIO write loop gated on BUFFER_WRITE_ENABLE level :499-531
  - wait TRANSFER_DONE :541
  - PIO error → reset CMD+DAT :535-537
  - response read :552-556
  - HS bit at 50 MHz :1213-1215
  - STATUS_ENABLE incl RW_*_READY :1161-1162
  - clock-stable→SD-clock-enable :1221-1228
  - autoCmd12 enable :439-442
- `.../sdhost_defs.h`: PRES_STATE bits :116-127; AUTOCMD12_ERROR_STATUS reg :104
- `.../bcm2711-sdio.h`: `sdio_dataBarrier` (`dmb sy`) :42-48
- `.../sdstorage_dev.c`: write retry re-issuing same CMD24 :195-201

### References (raw primary source, fetched 2026-06-07)
Reference line numbers below are **approximate** (obtained via a summarizing
fetch of the raw files, not direct line-by-line inspection); function names +
URLs make them locatable. Our-side line numbers are exact (direct file read).

- Linux sdhci.c — https://raw.githubusercontent.com/torvalds/linux/master/drivers/mmc/host/sdhci.c
  - `sdhci_transfer_pio` mask/loop :1029-1050
  - reset-reason enum + `sdhci_reset_for_reason` :282-301
  - `__sdhci_finish_data_common` reset on data->error :2136-2149
  - `sdhci_send_command` inhibit mask :1778-1785
- Linux sdhci-iproc.c — https://raw.githubusercontent.com/torvalds/linux/master/drivers/mmc/host/sdhci-iproc.c
  - `sdhci_bcm2711_pltfm_data.quirks = SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12` (no NO_HISPD_BIT)
  - `sdhci_iproc_pltfm_data.quirks = …|SDHCI_QUIRK_NO_HISPD_BIT` (generic iproc, NOT bcm2711)
  - `sdhci_iproc_writel` post-write udelay only when clock≤400 kHz
- U-Boot bcm2835_sdhci.c — https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/mmc/bcm2835_sdhci.c
  - twoticks consecutive-write delay, all regs except SDHCI_BUFFER :67-82
  - quirks BROKEN_VOLTAGE|BROKEN_R1B|WAIT_SEND_CMD|NO_HISPD_BIT :224-227
- U-Boot sdhci.c (core) — https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/mmc/sdhci.c
  - `sdhci_transfer_data` gating; command-inhibit (CMD_INHIBIT|DATA_INHIBIT) wait
- Circle emmc.cpp — https://github.com/rsta2/circle/blob/master/addon/SDCard/emmc.cpp
  - `IssueCommandInt` FIFO push gated on write-ready; TRANSFER_COMPLETE wait
  - `EnsureDataMode` issues CMD13; `DoDataCommand` invalidates RCA + CardReset on error
  - HS via clock divider only, no CONTROL0 HS bit

Note: BCM2835/Cygnus quirks (twoticks delay, NO_HISPD_BIT) are for the *legacy*
SDHOST/EMMC and Cygnus iProc controllers, explicitly NOT the bcm2711 EMMC2;
listed to document what was ruled out for the Pi4.
