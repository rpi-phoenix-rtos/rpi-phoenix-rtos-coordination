# BCM2711 EMMC2 SD-driver bugs — known-good solutions + transcribe-not-rediscover port plan

> **2026-06-30 OUTCOME (HW-validated, booted from SD):**
> - **WRITE bug (#154): FIXED.** CMD13-poll-to-TRAN write completion (devices `3d92b9e`,
>   raw eventLock-held CMD13 + exp-backoff poll). Self-test `writeRc=16/16 dataMatch=16/16`
>   (was 0/16); under load `stress-fs` seq/rand/many all verified, 0 faults.
> - **READ EIO: CARD-SPECIFIC, not a driver bug.** The §4.1 measurement (large consecutive
>   read diag, devices `dea4e7b`) on a *good* card: `readOk=2048/2048 firstErrBlk=-1` (1 MiB
>   at 50 MHz, no error, no SDREADDIAG). The 2026-06-13 EIO card was marginal media. **No read
>   fix needed for good cards.** The 25 MHz fallback (§4 P3) stays DEFERRED/optional — and note
>   the driver inherits firmware 50 MHz (no clock-set today; runtime divisor change wedges), so
>   it means adding careful init-time clock-setting, attended.
> - Full 8 GB ext2 rootfs (entire NFS volume) boots from SD; NFS probes gated to netboot-only
>   (project `158281a`); stress suite (proc/ipc/mem/thread/sched/syscall/fs) runs from SD, 0 faults.
> - Remaining SD item = I/O **throughput** (PIO single-block ~0.21 MB/s W / 1.25 MB/s R; DMA +
>   multi-block CMD18/CMD25 is the lever) — separate enhancement, deferred.



**Date:** 2026-06-29. **Type:** research + design (read-only). No source modified; no
HW booted; the parked #154 WIP in `storage/bcm2711-emmc/` was read but **not** touched.

**Purpose.** Consolidate everything we know about our two open BCM2711 EMMC2 SD-driver
bugs and pair each with the *proven* implementation that Linux / U-Boot / Circle already
ship, so the implementation phase transcribes a shipped pattern instead of trial-and-error.

**Supersedes / consolidates** (do not re-derive these; this doc is their superset):
- `docs/research/2026-06-07-sd-driver-cross-os-comparison.md` (write-fail cross-OS ranking)
- `docs/research/2026-06-07-bcm2711-emmc2-clock-base.md` (clock base = 100 MHz, N=1 ⇒ 50 MHz, CONFIRMED correct)
- `docs/inprogress/2026-06-07-sd-write-completion-rootcause.md` (#154 root cause + designed fix — **still PARKED**)
- `docs/done/2026-06-07-sd-clock-base-hypothesis.md` (the 2× overclock hypothesis — **REFUTED**, base is 100 MHz)

**Our driver under study:** `sources/phoenix-rtos-devices/storage/bcm2711-emmc/`
(`sdcard.c` 1570 ln, `sdhost_defs.h`, `sdstorage_dev.c`, `bcm2711-sdio.[ch]`).
Controller: BCM2711 **EMMC2** SDHCI block @ `0xfe340000`, IRQ 158, divider-input base
clock **100 MHz** (VideoCore mailbox `GET_CLOCK_RATE(id 12)`), microSD slot run at HS
3.3V / 50 MHz / 4-bit. SDHCI-style register map, accessed **32-bit only** (Arasan).

**Linux reference (offline clone):** `external/linux/drivers/mmc/`
- host: `host/sdhci-iproc.c` (bcm2711 platform data), `host/sdhci.c` (SDHCI core)
- **core (the linchpin for the write bug):** `core/mmc_ops.c`, `core/block.c`, `include/linux/mmc/mmc.h`

---

## 0. The unifying thesis (read this first)

Both bugs share a root: **this controller's host-side completion + present-state signalling is
unreliable, and we have no MMC-core layer above us to compensate** — so we must stop trusting
the controller's Transfer-Complete / present-state for the card-busy decision and ask the
**card** via **CMD13 SEND_STATUS**, exactly what the Linux MMC **core** does in
`__mmc_poll_for_busy()`/`mmc_busy_cb()` (`core/mmc_ops.c`). That fully fixes the **write** bug.
For the **read** bug, CMD13 is the diagnostic/escalation primitive, but the *likely* fix is the
core's other known-good move for a persistent data error — renegotiate the link **down**
(drop the clock). One shared CMD13 primitive serves both; the read clock-drop is gated by a
single HW measurement (§4.1).

- **Write bug (#154):** Transfer-Complete IRQ never fires for a write → false `-ETIME`,
  even though the data landed and the card is back in TRAN. Linux's host layer would catch
  this on the busy-end interrupt, but the **authoritative** completion the core relies on is
  the CMD13 busy poll (`mmc_blk_card_busy` → CMD13 for *writes only*). We must transcribe
  that poll.
- **Large-read EIO:** sustained single-block CMD17 reads start failing after ~256 KB. The
  field log (`transfer error 00400900`) is the **card R1** reporting an error *while the card
  is back in TRAN + READY_FOR_DATA* (decode below) — so for *reads* the dominant suspect is
  **host-side HS50 sampling margin**, whose Linux known-good recovery is to renegotiate the
  link **down** (drop the clock). The missing-CMD13-resync angle is weaker for single-block
  reads than for writes (no open-ended stream to abort, card not wedged); see §2.2. A single
  HW measurement (§4.1) discriminates sampling-margin (⇒ 25 MHz) from a genuine wedge (⇒ CMD13
  re-init).

So the **one** building block to add — a *raw, register-level CMD13 poll-to-TRAN* — directly
fixes the write bug and provides the read path's escalation/diagnostic; the read path's
*probable* fix is the clock drop (P3), gated by that measurement.

---

## 1. The two bugs, with exact register/IRQ symptoms

### Symptom decode — the `00400900` flood (large-read EIO)
**Corrected (verified in source):** the logged `sdcard error: transfer error 00400900` is
**NOT** `INTR_STATUS` — it is emitted by `sdcard.c:1312` (`LOG_ERROR("transfer error %08x",
resp)`) and `resp` is the **card's R1 status response** (`_sdio_cmdSend` out-param), checked
against `CARD_STATUS_ERRORS` (`sdhost_defs.h:399`). So `0x00400900` decodes as an SD **R1
card-status word** (`sdhost_defs.h:365-398`):

| bits | value | field (`sdhost_defs.h`) | meaning |
|------|-------|-------------------------|---------|
| 22 | `0x00400000` | `CARD_ERROR_ILLEGAL_COMMAND` (`:375`) | card flags an error in its R1 reply (bit 22 in this driver's enum) |
| 12:9 | `0x900 → (>>9)&0xf = 4` | `CARD_STATUS_CURRENT_STATE` = **TRAN(4)** (`:391,398`) | card is back in the transfer state |
|  8 | `0x00000100` | `CARD_STATUS_READY_FOR_DATA` (`:367`) | card is ready for data |

So the field log says: **the card returned an R1 error bit but is itself in TRAN +
READY_FOR_DATA** — i.e. the *card* is not wedged. This is the single most useful field
datapoint and it bears directly on §4.1 (it leans away from a card-side wedge). NB the *host*
`INTR_STATUS` data-end-bit path is separate: `SDHOST_INTR_DATA_ENDBIT` = bit 22 of INTR_STATUS
(`sdhost_defs.h:202`), surfaced by the PIO loop as `pio xfer error ... intr=0x...`
(`sdcard.c:519`) / the TRACE at `:555` — **those** emit INTR_STATUS, and in Linux the
equivalent `SDHCI_INT_DATA_END_BIT` maps to `-EILSEQ` (`sdhci.c:3494`), a CRC-class data
error. The validation build must log **both** words distinctly (see §4.1) so we stop
conflating the host data-end-bit interrupt with the card R1 reply.

### Bug A — write completion (#154): Transfer-Complete IRQ never fires for writes
HW-proven (`docs/inprogress/2026-06-07-sd-write-completion-rootcause.md`), SDDIAG @ 50 MHz HS/4-bit, 16 trials:
```
read  LBA0   x16: readOk=16/16
write LBA100 x16: writeRc=0/16  dataMatch=16/16   <-- data lands every time; "write" never returns OK
  per attempt: transfer_done-timeout cmd=24 ret=-37 pres=0x1fef0000 intr=0x00000000
               DAT0release=0ms cmd13rc=0 cardState=4(TRAN)
```
- CMD24 + CMD_DONE OK, PIO push OK, but `SDHOST_INTR_TRANSFER_DONE` (bit 1) **never latches**
  (`intr=0x00000000`) for the full 1 s wait at `sdcard.c:553` → `-ETIME` (`-37`).
- Yet read-back MATCHes 16/16 and the card is in **TRAN(4)**, `READY_FOR_DATA` — the write
  *worked*; only completion *detection* is broken.
- `pres=0x1fef0000`: DATA_INHIBIT(bit1)=0 while DAT0 reads busy — **the controller's
  present-state bits are internally inconsistent on writes. Do not trust DAT0/DATA_INHIBIT;
  use the card's CMD13 status.**
- Reads use the identical `TRANSFER_DONE` wait and complete 16/16 — the asymmetry is that
  reads never enter the PRG/busy phase, so `TRANSFER_DONE` latches for them.

### Bug B — large-read EIO (sustained reads die after ~256 KB)
HW-proven (memory `project_pi4_sd_quake_largeread`, 2026-06-13): `dd if=/id1/pak0.pak
of=/dev/null bs=65536` → `EIO after 262144 bytes`; log floods `transfer error 00400900`.
- **Reframe (important):** reads are *already single-block CMD17 in a loop*
  (`sdstorage_dev.c:136-163`, `sdcard_readCb`) with a 5× per-block retry
  (`SDCARD_READ_RETRIES`, `sdstorage_dev.c:147`). So this is **not** a multiblock/CMD18 bug —
  it is a *sequence of ~512 independent single-block reads* that starts throwing
  `DATA_ENDBIT` partway in, and the retry does not recover.
- 256 KB / 512 B = **512 blocks** complete before the first unrecovered error → consistent
  with **marginal HS50 sampling** (errors accumulate, not a one-shot init artifact) *and/or*
  **incomplete recovery** (once one block errors, the retry can't resync the card).
- Our PIO error path (`sdcard.c:518-522, 545-549`) sets `-EIO`, write-1-clears the error
  bits, then `RESET_CMD`+`RESET_DAT` (host-only). `sdcard_readCb` then re-issues the *same*
  CMD17 with **no CMD13 / no card resync**.

---

## 2. Known-good solutions (concrete, with code refs + why they work)

### 2.1 Does Linux poll CMD13 for write completion? — YES, in the MMC **core** (the linchpin)

The host driver (`sdhci.c`) relies on the Transfer-Complete interrupt
(`SDHCI_INT_DATA_END`), which for an R1b/write is *supposed* to span the DAT0 busy-end
(`sdhci.c:3460-3475`, the `SDHCI_INT_DATA_END` busy-end branch; and the R1b busy path that
arms `SDHCI_INT_DATA_END` when `cmd->flags & MMC_RSP_BUSY`, `sdhci.c:1842-1871`). **But** the
*authoritative* write-completion in Linux is one layer up, in the MMC core, which is exactly
why it isn't in `sdhci.c` and why our host-only driver is missing it:

**`drivers/mmc/core/block.c::mmc_blk_card_busy()` (`block.c:2082`)** — after every data
request, the core decides busy-completion **by direction**:
```c
if (rq_data_dir(req) == READ)
        return 0;                         /* reads: no busy poll */
...
err = __mmc_poll_for_busy(card->host, 0, MMC_BLK_TIMEOUT_MS,
                          &mmc_blk_busy_cb, &cb_data);   /* writes: poll the card */
```
That is the **read/write asymmetry our driver needs**, shipped verbatim.

**`block.c::mmc_blk_busy_cb()` (`block.c:2065`)** and the generic
**`core/mmc_ops.c::mmc_busy_cb()` (`mmc_ops.c:468`)** both do:
```c
err = mmc_send_status(data->card, &status);   /* CMD13 */
...
*busy = !mmc_ready_for_data(status);
```
**`core/mmc_ops.c::__mmc_send_status()` (`mmc_ops.c:69`)** issues **CMD13**:
`cmd.opcode = MMC_SEND_STATUS; cmd.arg = card->rca << 16; cmd.flags = MMC_RSP_R1`.

**`include/linux/mmc/mmc.h::mmc_ready_for_data()` (`mmc.h:170`)** is the completion predicate:
```c
return status & R1_READY_FOR_DATA &&                 /* R1 bit 8  (mmc.h:155) */
       R1_CURRENT_STATE(status) == R1_STATE_TRAN;     /* bits 12:9 == 4 (mmc.h:154,166) */
```

**`core/mmc_ops.c::__mmc_poll_for_busy()` (`mmc_ops.c:510`)** is the loop: call the cb; if
`*busy`, `usleep_range` with **exponential backoff** (32 µs → 32768 µs cap, `mmc_ops.c:517,541-545`),
bounded by `timeout_ms`; return `-ETIMEDOUT` only if still busy at expiry.

Crucial nuance: `mmc_busy_cb` prefers `host->ops->card_busy()` (DAT0 present-state) when the
*controller* exposes a trustworthy busy line (`mmc_ops.c:475-478`); it falls back to CMD13
otherwise. **On BCM2711 EMMC2 the present-state busy bits are NOT trustworthy on writes**
(our `pres=0x1fef0000` finding) — so CMD13 is exactly the path Linux would take here.

**Why it works:** CMD13 is the *card's* authoritative state, immune to this controller's
flaky Transfer-Complete + present-state. CMD13 is **CMD-line-only** (valid while DAT0 is
busy) and **actively clocks the bus**, which lets the card finish its programming (the passive
TC-wait at `sdcard.c:553` generates no clocks). Completion = `READY_FOR_DATA` (bit 8) **AND**
current-state == TRAN(4) — bit-identical to our designed #154 fix.

**Independent corroboration (transcribe-ready snippets):**
- **U-Boot** `drivers/mmc/mmc.c::mmc_send_status()` / `mmc_poll_for_busy()`: same loop —
  CMD13 (`MMC_CMD_SEND_STATUS`, arg `rca<<16`), check `status & MMC_STATUS_RDY_FOR_DATA` and
  `(status & MMC_STATUS_CURR_STATE) != MMC_STATE_PRG`, bounded retry.
  https://github.com/u-boot/u-boot/blob/master/drivers/mmc/mmc.c
- **Circle (bare-metal Pi4 EMMC2)** `addon/SDCard/emmc.cpp::EnsureDataMode()` issues **CMD13**
  before/after data ops and polls `CURRENT_STATE`; `DoDataCommand` invalidates RCA + full
  `CardReset()` on persistent error.
  https://github.com/rsta2/circle/blob/master/addon/SDCard/emmc.cpp

### 2.2 The `DATA_END_BIT` error and how Linux recovers (large-read)

`sdhci.c::sdhci_data_irq` (`sdhci.c:3494`): `SDHCI_INT_DATA_END_BIT → host->data->error =
-EILSEQ` (CRC-class), then `sdhci_finish_data()` → `__sdhci_finish_data_common()`
(`sdhci.c:1566`) resets the **DATA** circuit (`SDHCI_RESET_DATA`) and returns the error to the
MMC core. The recovery **ladder** above the host is:
1. **Reset the DAT line** (`SDHCI_RESET_DATA`) — per SDHCI spec this drains the data FIFO and
   resets the DAT state machine. (Our `RESET_DAT` does the equivalent, `sdcard.c:547-548`.)
2. **Retry the request** (MMC core re-queues).
3. On *persistent* errors the core **renegotiates down**: it retunes (UHS only) and/or the
   error-recovery path can drop bus speed/width. For HS50 (no tuning), the practical lever is
   the lower clock.

There is **no** BCM2711-specific input-sample/output-drive delay register that Linux programs
for HS50 (confirmed in `docs/research/2026-06-07-bcm2711-emmc2-clock-base.md` §(c): no
`platform_execute_tuning`, stock `sdhci_set_clock`; tuning is HS200/SDR104-only, which the
3.3V microSD path never enters). So "force 25 MHz" is the **blunt fallback Linux *effectively*
falls back to on a marginal link — it is NOT a Linux-specific fix.**

**Caveat — for *single-block* reads the recovery-cascade story is weaker than for writes.**
A single-block CMD17 has no open-ended stream to abort (unlike CMD18), and the field-log R1
(`00400900`) shows the **card already back in TRAN + READY_FOR_DATA** at the error. So the
card most likely self-returns to TRAN on its own; an end-bit error on CMD17 is most plausibly
a *host-side sampling failure on an otherwise-complete card transmission*. If that holds, a
"CMD13 resync before retry" (P2) is **largely a no-op for reads** — CMD13 will just report
TRAN immediately (exactly as it already does at the write-timeout: `cmd13rc=0 cardState=4`).
Its value on the read path is therefore the **escalation-to-reinit branch + the diagnostic**,
not the recovery itself.

So the genuinely missing piece differs by direction:
- **Writes:** the missing CMD13 *completion* (Bug A / §2.1) — high value.
- **Reads:** the Linux known-good for a persistent `DATA_END_BIT` is *reset DATA → retry → on
  persistence, renegotiate the link **down***. We do reset+retry; we do **not** drop the
  clock. So **P3 (25 MHz) is the *probable real read fix*, not merely a fallback**, and P2 for
  reads is the cheap escalation/diagnostic that *also* tells us whether P3 is even needed. The
  P2-vs-P3 ownership for reads is a HW question with a single discriminating measurement (§4.1).

### 2.3 The bcm2711 quirks Linux sets, mapped to our driver

From `external/linux/drivers/mmc/host/sdhci-iproc.c` for `brcm,bcm2711-emmc2`
(`bcm2711_data` @ `:294`, `sdhci_bcm2711_pltfm_data` @ `:289`, ops `sdhci_iproc_bcm2711_ops`
@ `:273`):

| Linux quirk / setting | what it does | our driver |
|---|---|---|
| `SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12` (`:290`) — **the only** quirk for bcm2711 | force Auto-CMD12 on multiblock **reads** | We sidestep multiblock entirely (single-block CMD17/CMD24 loop). Honor only when multiblock is re-enabled. |
| base clock via `sdhci_iproc_get_max_clock`→clk framework (`:157-165,282`), **not** the CAPABILITIES base-clock field | take the SDCLK divider-input rate from the firmware-managed `emmc2` clk (=100 MHz), ignoring the unreliable CAPS base field | ✅ we read 100 MHz from mailbox, ignore CAPS base (`sdcard_configClockAndPower` comment) — **matches**. (NB: `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN` is **not present in this `external/linux` tree's `sdhci-iproc.c`**; the clock-base-correct conclusion rests on `get_max_clock`/clk-framework, not that quirk — provenance corrected vs. the older clock-base doc.) |
| `sdhci_iproc_get_max_clock` → 100 MHz; `sdhci_calc_clk(100M,50M)` ⇒ field N=1 | 50 MHz SDCLK | ✅ `CLOCK_CONTROL_DIV_2` (N=1) ⇒ 100/(2·1)=50 — **matches**; do NOT change to N=2 |
| `sdhci_iproc_bcm2711_get_min_clock` → **200 kHz floor** (`:179`) — known hang when core-clk ≫ bus-clk | avoid a controller hang at very low clock | we don't probe at 100 kHz, low risk; note it |
| **`write_l = sdhci_iproc_writel`** with post-write `udelay` **only when `host->clock <= 400000`** (`:78-84`) | the Arasan clock-domain write delay applies **only below 400 kHz** | At 50 MHz this delay is **OFF** — our `dmb sy`-only writes are correct at HS50. Not a cause. |
| `is_cmd_shadowed`/`is_blk_shadowed` 32-bit shadow of BLKSIZE/BLKCNT/TRANSFER_MODE/COMMAND (`:106-155`) | Arasan loses back-to-back 16-bit writes to adjacent regs | We are 32-bit-only already; equivalent. Relevant only if we add 16-bit accessors. |
| **NOT set:** `SDHCI_QUIRK_NO_HISPD_BIT` (that's on the *legacy* iproc/cygnus/bcm2835 data, `:209,233,256,340` — NOT bcm2711) | — | ✅ Linux **does** drive the HS bit on bcm2711; our `HOST_CONTROL_HIGH_SPEED` at 50 MHz (`sdcard.c:1213-1215`) is correct (prior "HS bit wrong" hypothesis **REFUTED**) |
| `bcm2711_data.mmc_caps = MMC_CAP_3_3V_DDR` (`:296`) | allows DDR50 3.3V | we run plain HS50 SDR; not required |

**Net:** the only behavioral quirk we don't yet honor is Auto-CMD12 on multiblock reads — and
we don't do multiblock yet, so it's deferred. No missing write-path or HS50 quirk explains our
bugs; the gap is the **card-side CMD13 completion/recovery the MMC core provides and we lack.**

---

## 3. Port plan — minimal, priority-ordered, fewest HW cycles

All changes live in `bcm2711-emmc/sdcard.c` (+ the read-retry caller in `sdstorage_dev.c`).
Build **one** shared primitive, then wire it into write-completion and read-recovery.

### P0 — Build the shared primitive: raw register-level CMD13 poll-to-TRAN
Transcribe Linux `mmc_busy_cb`+`mmc_ready_for_data`+`__mmc_poll_for_busy` as a host-local
helper. Constraints from `2026-06-07-sd-write-completion-rootcause.md` §"Implementation
constraints" (still valid):
- Runs **inside `_sdio_cmdSend` with `host->eventLock` HELD** → must be **raw**: poke
  `SDHOST_REG_ARG` + `SDHOST_REG_CMD` directly and wait via
  `_sdio_cmdExecutionWait(CMD_DONE)` (already called eventLock-held). Do **not** recurse
  through `_sdio_cmdSend`/`sdio_cmdSend` (deadlock + hits the pre-command busy poll).
- Must **bypass** the pre-command `PRES_STATE_BUSY_FLAGS` poll (`sdcard.c:377-390`, bails
  `-EBUSY` after ~100 ms on DAT_BUSY) — CMD13 is CMD-line-only and *must* issue while DAT0 is
  busy.
- Building blocks already in tree: `SDIO_CMD13_SEND_STATUS=13`, R1/no-data metadata;
  `CARD_STATUS_READY_FOR_DATA=(1<<8)`, `CARD_STATUS_CURRENT_STATE(x)=((x>>9)&0xf)`,
  `..._TRAN=4`; arg = `host->card.rca` (already in `[31:16]` form, see `sdcard.c` ~789).

Transcribe-ready pseudocode (mirrors `__mmc_poll_for_busy` + `mmc_ready_for_data`):
```c
/* eventLock HELD; returns 0 when card is TRAN+READY_FOR_DATA, <0 on error/timeout */
static int _sdio_pollCardReady(sdcard_hostData_t *host, unsigned timeoutMs)
{
    unsigned us = 32, total = 0;           /* exp backoff, like mmc_ops.c */
    for (;;) {
        /* raw CMD13, bypassing the pre-command DAT_BUSY poll */
        *(host->base + SDHOST_REG_INTR_STATUS) = SDHOST_INTR_CMD_DONE;   /* clear stale */
        *(host->base + SDHOST_REG_ARG)  = host->card.rca;               /* rca<<16 form */
        sdio_dataBarrier();
        *(host->base + SDHOST_REG_CMD)  = <CMD13 frame: idx=13, R1, no-data, no TRANSFER_BLOCK>;
        int rc = _sdio_cmdExecutionWait(host, SDHOST_INTR_CMD_DONE, 50*1000);
        if (rc < 0) return rc;                                          /* CMD13 itself failed */
        uint32_t st = *(host->base + SDHOST_REG_RESPONSE_0);            /* card R1 */
        bool ready = (st & CARD_STATUS_READY_FOR_DATA) &&
                     (CARD_STATUS_CURRENT_STATE(st) == CARD_STATUS_CURRENT_STATE_TRAN);
        if (ready) return 0;
        if (total >= timeoutMs * 1000u) return -ETIME;
        usleep(us); total += us; if (us < 32768u) us *= 2u;            /* mmc_ops.c backoff */
    }
}
```

### P1 — Bug A fix: use the CMD13 poll for **write** completion (highest value, #154)
At the post-PIO completion in `_sdio_cmdSend` (`sdcard.c:552-568`), branch on direction
(`pioRead` already computed at `:500`), mirroring `mmc_blk_card_busy`:
- `if (pioRead)`: keep the existing `_sdio_cmdExecutionWait(TRANSFER_DONE, 1 s)` (works 16/16).
- `else` (write): **skip** the TRANSFER_DONE wait; call `_sdio_pollCardReady(host, ~500 ms)`.
  On success, **clear the never-raised stale `TRANSFER_DONE`** bit so it can't confuse the
  next command; return 0. On error during poll → `-EIO` + `RESET_CMD/RESET_DAT`.
- After this lands, the `sdcard_writeCb` 5× retry (`sdstorage_dev.c:195`) becomes
  belt-and-suspenders (harmless; keep until validated, then reconsider).

### P2 — Bug B fix: CMD13 resync on **read** error before retry
The read-retry caller (`sdstorage_dev.c:147-153`) currently re-issues CMD17 with no card
resync. Add card resync to the recovery so the retry meets a card in a known state:
- On a CMD17 `DATA_ENDBIT`/`-EIO` (already `RESET_CMD/RESET_DAT`-recovered host-side at
  `sdcard.c:545-549`), before the next attempt call `_sdio_pollCardReady` (CMD13 → TRAN). If
  the card won't return to TRAN, escalate to a full card re-init (Circle's `CardReset`
  pattern) rather than spinning CMD17.
- Keep the bounded retry count; log retries-needed vs outright-fail (the existing
  `sdstorage diag(#120)` prints already do this — keep them for the validation build).

This is **zero-extra-HW-cost** and is the escalation/diagnostic half. For reads it most likely
does **not** by itself resolve the EIO (the card is already in TRAN — see §2.2 caveat); its job
is to (a) cleanly escalate to a full card re-init if the card *is* ever wedged, and (b) feed
the §4.1 discriminating measurement. Do **not** assume P2 alone fixes the read EIO.

### P3 — reduce HS50 → 25 MHz (the *probable real* read fix, gated by the §4.1 measurement)
For reads, the field R1 (card in TRAN) points at host-side **sampling margin** at 50 MHz, whose
Linux known-good recovery is to renegotiate the link **down**. If the §4.1 measurement shows
the card in TRAN at the first unrecovered EIO (host sampling, not a wedge), **P3 is the fix, not
a fallback**: skip the CMD6 high-speed switch (`sdcard_hasHighSpeedFunction`, `sdcard.c` ~968)
/ select `CLOCK_CONTROL_DIV_4` (N=2 ⇒ 100/(2·2)=25 MHz). (The earlier "25 MHz fails MBR" run was
a *botched* config — HS bit left set with the card not in the HS function — **not** evidence that
25 MHz is bad; see clock-base doc.) Only if §4.1 instead shows the card stuck in a non-TRAN
state does read ownership shift to P2's escalation/re-init.

### P4 — cleanups to fold in while touching this code (no behavior risk)
- Remove the **dead SDMA programming on the PIO path** (`sdcard.c:420-425`,
  `SDHOST_REG_SDMA_ADDRESS` + `TRANSFER_BLOCK_SDMA_BOUNDARY_4K` while `dmaEnable=0`) — ignored
  in PIO; misleading on upstreaming (cross-OS doc discrepancy #3).
- When multiblock CMD18/CMD25 is eventually re-enabled: honor
  `SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12` and **read** `SDHOST_REG_AUTOCMD12_ERROR_STATUS`
  (`sdhost_defs.h:104`, currently never read) in the Auto-CMD12 error path (discrepancy #4).
  Out of scope for the single-block fix.

### Validation bar (writes were flaky — one pass ≠ done)
- Writes: ≥16–32 raw single-block writes via SDDIAG, all succeed + readback MATCH.
- Reads: a **>1 MB** sustained read (`dd … bs=65536` of pak0) completes with **0** unrecovered
  EIO, and the retry log shows transient errors *recovered* (not climbing toward the cap).
- A real file write→read through the mounted ext2 rootfs from psh.
- Boot-to-psh, **0 faults, ≥2 boots**. Reads stay 16/16 (don't regress the read path).
- Tooling: `rebuild --scope core --variant sd` → `build-rpi4b-rootfs-ext2.sh` → `dd` to
  `/dev/sda` (verify first-4MB sha) → user shuttles card → `test-cycle-netboot.sh --sd-boot`
  → `grep -a SDDIAG`. (SD test requires SD-boot: card-in breaks netboot.)

---

## 4. Open questions that genuinely need HW (not answerable from references)

1. **P2-vs-P3 ownership of the read EIO — settle with ONE observation before writing the read
   fix.** At the moment of the *first unrecovered* read EIO, log **both** words distinctly: the
   card's **CMD13 R1 state** *and* the controller `INTR_STATUS` + `PRES_STATE`.
   - **Card in TRAN(4) + READY_FOR_DATA** (which the field `00400900` already hints) ⇒ host
     **sampling margin** at HS50 ⇒ **P3 (25 MHz)** is the fix; P2 resync is a no-op for reads.
   - **Card stuck in DATA/RCV/PRG / not returning to TRAN** ⇒ controller/card **wedge** ⇒ P2
     escalation + full re-init owns it.
   This single datapoint decides P2-vs-P3 *before any read-path code is written* — that is the
   whole point of this doc. (The HS50-marginal vs recovery-incomplete hypotheses are **not**
   resolved here; this measurement resolves them.)
2. **Does the raw CMD13 actively clocking the bus by itself let the write busy clear** (as the
   rootcause doc hypothesizes), i.e. is `READY_FOR_DATA+TRAN` reached on the *first* CMD13, or
   after several? Log the CMD13 count / elapsed-ms in the SDDIAG build ("TRAN after N ms").
3. **Is the `00400900` passenger bit 11 (and the spurious CARD_IRQ bit 8) benign**, or does
   bit 11 indicate a retune/tuning-window event we should act on? On HS50 (no tuning) it should
   be benign; confirm it doesn't co-occur with successful blocks.
4. **One-time CMD6 leftover-FIFO contamination?** A known *legacy-SDHOST* failure mode is an
   undrained DATA response from the double CMD6 high-speed switch contaminating the first
   reads (RPi forum t=377652). Our symptom (clean for ~512 blocks *then* errors) argues against
   a one-time init leftover, but the SDDIAG build should confirm the FIFO is empty
   (`PRES_STATE` buffer-enable bits clear) at the start of each CMD17 to rule it out cheaply.

---

## Citations

### Our source (exact line reads)
- `storage/bcm2711-emmc/sdhost_defs.h`: INTR bits `:185-206`; `DATA_ENDBIT`=bit22 `:202`;
  `DAT_ERRORS` mask `:221-224`; `DATA_TIMEOUT_VALUE` `:181`; AUTOCMD12_ERROR_STATUS reg `:104`;
  **`CARD_STATUS` enum (R1 bits) `:365-384`** (`READY_FOR_DATA`=bit8 `:367`,
  `CARD_ERROR_ILLEGAL_COMMAND`=bit22 `:375`), `CARD_STATUS_CURRENT_STATE(x)` `:398`,
  `..._TRAN`=4 `:391`, `CARD_STATUS_ERRORS` mask `:399`.
- `storage/bcm2711-emmc/sdcard.c` (cont.): **`transfer error %08x` emitter prints the card R1
  `resp`, not INTR_STATUS** `:1311-1312` (and `:1449`); `resp` is `_sdio_cmdSend` out-param `:1301`.
- `storage/bcm2711-emmc/sdcard.c`: `SDHOST_ERROR_REASONS` `:49-55`; `sdhost_reset` `:142-154`;
  pre-command busy poll → `-EBUSY` `:377-390`; dead SDMA-on-PIO `:420-425`, `dmaEnable=0` `:433`;
  PIO loop level-gated `:498-544`; PIO error → reset `:545-549`; **write/read TRANSFER_DONE wait
  `:552-568`**; HS bit `:1213-1215`; STATUS_ENABLE incl RW_*_READY `:1189-1194`; data-timeout
  value `0b1110` `:1258`; clock-stable→SD-clock `:1221-1228`.
- `storage/bcm2711-emmc/sdstorage_dev.c`: `sdcard_readCb` single-block loop + 5× retry
  `:116-165`; `sdcard_writeCb` single-block loop + 5× retry `:168-213`; retry counts `:48-49`.

### Linux (external/linux clone — direct reads)
- `drivers/mmc/host/sdhci-iproc.c`: `sdhci_iproc_writel` (udelay only ≤400 kHz) `:71-85`;
  shadow writew `:106-155`; `get_min_clock` 200 kHz + known-hang comment `:167-182`;
  `sdhci_iproc_bcm2711_ops` `:273-285`; `sdhci_bcm2711_pltfm_data` (quirks =
  `MULTIBLOCK_READ_ACMD12` only) `:289-292`; `bcm2711_data` `:294-297`; OF match
  `brcm,bcm2711-emmc2` `:324`.
- `drivers/mmc/host/sdhci.c`: data-IRQ error decode (`DATA_END_BIT → -EILSEQ`) `:3491-3518`;
  PIO transfer/read/write `:536-650`; `sdhci_calc_timeout` `:969-1025`; `__sdhci_finish_data_common`
  (reset DATA) `:1566-1603`; R1b busy arms DATA_END `:1842-1871`; busy-end DATA_END branch
  `:3449-3475`.
- `drivers/mmc/core/mmc_ops.c`: `__mmc_send_status` (CMD13, arg rca<<16, R1) `:69-91`;
  `mmc_busy_cb` (card_busy-or-CMD13, `!mmc_ready_for_data`) `:468-508`; `__mmc_poll_for_busy`
  (exp-backoff loop) `:510-550`; `mmc_poll_for_busy` `:552-563`.
- `drivers/mmc/core/block.c`: `mmc_blk_busy_cb` (CMD13) `:2065-2080`; **`mmc_blk_card_busy`
  (read⇒0, write⇒poll)** `:2082-2101`.
- `include/linux/mmc/mmc.h`: `R1_CURRENT_STATE` `:154`, `R1_READY_FOR_DATA` (bit8) `:155`,
  `R1_STATE_PRG`=7 `:167`, `R1_STATE_TRAN`=4, `mmc_ready_for_data()` `:170-178`.

### External references (web, fetched 2026-06-29)
- U-Boot `drivers/mmc/mmc.c` `mmc_send_status`/`mmc_poll_for_busy` (CMD13 busy poll):
  https://github.com/u-boot/u-boot/blob/master/drivers/mmc/mmc.c
- U-Boot `drivers/mmc/bcm2835_sdhci.c` (legacy controller; twoticks delay — NOT bcm2711):
  https://github.com/u-boot/u-boot/blob/master/drivers/mmc/bcm2835_sdhci.c
- Circle `addon/SDCard/emmc.cpp` (`EnsureDataMode` CMD13; `DoDataCommand` RCA-invalidate+reset):
  https://github.com/rsta2/circle/blob/master/addon/SDCard/emmc.cpp
- LKML — `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN` on BCM2711 ("controller doesn't pick up clock changes"):
  https://www.spinics.net/lists/arm-kernel/msg915451.html
- LKML — bcm2711 min-clock 200 kHz floor (core-clk≫bus-clk hang):
  https://lore.kernel.org/all/1628334401-6577-5-git-send-email-stefan.wahren@i2se.com/
- RPi forum — SDHOST block read returns CRC: undrained CMD6/data FIFO contaminates later reads
  (legacy SDHOST; listed for the §4.4 leftover-FIFO rule-out):
  https://forums.raspberrypi.com/viewtopic.php?t=377652
