# How Linux does high-speed SD on BCM2711 EMMC2 — and why our Transfer-Complete IRQ never latches for CMD18/CMD25

**Date:** 2026-07-01. **Type:** research + design (READ-ONLY). No source modified, no HW
booted/flashed. Supersedes nothing; **complements** `2026-06-29-sd-bcm2711-research.md`
(which solved the *single-block write* completion via CMD13-poll and explicitly sidestepped
multi-block). This doc takes up the part that doc deferred: the **multi-block CMD18/CMD25 +
Auto-CMD12 Transfer-Complete-never-latches** symptom, and the path off PIO entirely (ADMA2).

**Provenance discipline (the task demands it).** Everything cited as `file:line` was read
directly from the offline clones under `external/`:
- `external/linux/` — real, read line-by-line (sdhci core, sdhci-iproc, bcm2711 dts).
- **FreeBSD / U-Boot / Circle are NOT present locally** (`external/` holds only
  linux, mesa, quakespasm, rpi-eeprom, vkquake). Every FreeBSD/U-Boot/Circle statement
  below is **training-knowledge or a prior-web citation** (the 2026-06-29 doc fetched those
  URLs on that date) and is **explicitly marked `[TRAINING-KNOWLEDGE]`**. No `file:line` is
  fabricated for them.

---

## 0. Executive summary (the 10-line version)

1. **Root cause (two co-primary triggers): this controller fails to latch the completion IRQ
   whenever a transfer uses Auto-CMD12 OR ends in a DAT0/R1b-busy "stop" phase — either alone is
   sufficient.** Our four data points separate cleanly (truth table in §4): CMD17 (no Auto-CMD12,
   no busy) latches TC fine; CMD24 write (no Auto-CMD12, **busy** PRG) does not (#154); CMD18 read
   (**Auto-CMD12**, **no** busy — card sits idle in TRAN, `PRES_STATE=0x1fff0000`, DAT_BUSY=0)
   does not; CMD25 write (both) does not; CMD7 R1b (busy) loses Command-Complete (#119). So CMD24
   isolates the **busy** trigger and CMD18 isolates the **Auto-CMD12** trigger — **two related
   completion-signalling gaps, not one mechanism** (the earlier "one terminal-busy mechanism"
   framing is wrong: the CMD18 read has no busy phase). The defensible unification is at the level
   we need it: the *same workaround* (CMD13-poll) and the *same escape* (ADMA2) cover both. The
   card always reaches TRAN with **no** error bits (AUTO_CMD12_ERROR is in our error mask, so a
   *real* Auto-CMD12 failure would surface as -EIO, not `intr=0x0`) → the controller executed the
   transfer (and any internal CMD12) on the bus and silently completed without latching TC.
2. **Refuted by our own evidence: it is NOT a missing interrupt-status-enable bit.**
   `STATUS_ENABLE` (0x34) is written **once** at init (`sdcard.c:1574`) with a mask that
   includes Transfer-Complete, and single-block CMD17 latches TC **16/16** under that identical
   mask. The bit is enabled; the controller just doesn't set it for the multi-block/busy case.
3. **Not a CMD23-vs-CMD12 *mistake* — but CMD23 is the clean discriminator for the read half.**
   We use Auto-CMD12, which is exactly what Linux's `brcm,bcm2711-emmc2` data forces
   (`SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12`, the *only* quirk), so our *choice* is not a bug. But
   because the CMD18 row isolates the **Auto-CMD12** trigger, switching multi-block to Auto-CMD23
   (SET_BLOCK_COUNT, which removes the Auto-CMD12 arm entirely) directly tests that trigger.
   `SDHCI_QUIRK2_ACMD23_BROKEN` is **not** set for bcm2711, i.e. Linux deems CMD23 usable on this
   core. Promoted to a first-class experiment (§7-S3).
4. **The real unlock is ADMA2, not a PIO patch.** Linux runs this silicon at full speed and TC
   demonstrably works *in the mode Linux uses* — and `bcm2711_data` sets **no** `BROKEN_DMA`/
   `BROKEN_ADMA` quirks (contrast `bcm7211a0_data`, which sets both), so **Linux uses ADMA2 and
   never PIO** on this controller. Linux source therefore *cannot* tell us whether a PIO software
   change restores TC — it never exercises that path. The proven escape that gives clean
   completion **and** ~25 MB/s is ADMA2.
5. **The thing that actually blocks ADMA2 today is DMA addressability.** Our buffer is
   `mmap(MAP_CONTIGUOUS|MAP_UNCACHED)` whose `va2pa` returns a **>1 GB** ARM-physical address
   (`sdcard.c:188,198`). The bcm2711 dts pins emmc2 DMA to a **1 GB window**
   (`dma-ranges = <0x0 0xc0000000  0x0 0x00000000  0x40000000>`, bcm2711.dtsi:424) — the legacy
   peripheral can only reach phys `[0, 0x4000_0000)` via the `0xc0000000` alias. A >1 GB phys
   buffer is unreachable: SDMA "advances the address register but lands no data" is the textbook
   out-of-window signature.
6. **Recommended top 3 steps (detail in §7):**
   - **S1 (keep, it's correct):** leave the CMD13-poll completion for write *and* multi-block
     read. It is the MMC-core-equivalent (`mmc_blk_card_busy`) and is the safety net regardless
     of what follows. Reframe it in code comments as "correct, not a hack."
   - **S2 (the proper fix + throughput, one move):** implement **ADMA2 with a buffer allocated
     in the low-1 GB DMA window**, transcribing `sdhci_adma_write_desc` + `sdhci_config_dma` +
     `sdhci_adma_table_pre`. If TC then latches in DMA mode, we have *both* clean completion and
     the throughput unlock, and we've *proven* the suppression is PIO/busy-specific. If TC still
     doesn't latch, CMD13-poll (S1) remains and we've learned the controller never raises TC.
     Either branch wins.
   - **S3 (cheap experiment, gated):** before/independent of ADMA2, try **Auto-CMD23
     (SET_BLOCK_COUNT) instead of Auto-CMD12** for multi-block — `SDHCI_QUIRK2_ACMD23_BROKEN` is
     **NOT** set for bcm2711, i.e. Linux considers Auto-CMD23 usable on this core. CMD23 has no
     trailing busy STOP, so if the suppression is specifically the R1b-busy CMD12, CMD23 would
     let TC latch. Low confidence, but a few lines and one HW boot to test.

**Cross-OS confirmation (§2.7, web-verified 2026-07-01):** Linux, U-Boot, and FreeBSD all use the
*same* model (TC is the multi-block completion signal; fires after Auto-CMD12; Auto-CMD12 surfaces
separately only on error) and **none** carries a "broken-TC / broken-auto-cmd12" Broadcom quirk.
Decisively, **U-Boot** is *polled* (reads the `SDHCI_INT_DATA_END` status bit, just like we would)
yet boots the Pi from SD — because it uses **SDMA with a bus-translated (in-window) DMA address**
(`dev_phys_to_bus`), never PIO. So humanity's working approach on this exact silicon is "DMA +
correctly-translated low-1 GB address," which is precisely S2.

The single open question that needs HW: **does TC latch once the data moves by ADMA2/SDMA
(DMA-mode completion, in-window address) rather than PIO?** That observation decides whether the
controller's TC is usable at all, and it is the same boot that delivers the throughput win.

---

## 1. The concrete symptom, re-stated precisely

Multi-block CMD18/CMD25 set, in our frame, `multiBlock=1` and `autoCmd12Enable=1`
(`sdcard.c:596-599`). On HW:
- Data moves correctly on the bus (PIO loop drains/fills every block, `pioErr==0`).
- The card returns to **state=4 (TRAN), R1=...0x900 (READY_FOR_DATA)** — verified by the CMD13
  poll we already run.
- Yet the host `INTR_STATUS` (0x30) stays **0x00000000**: Transfer-Complete (bit 1) **never
  latches**, so an IRQ/level wait on it times out (-ETIME). `PRES_STATE` at timeout =
  `0x1fff0000` (DAT/CMD lines idle, buffers empty).

We already worked this around (the code at `sdcard.c:715-727` routes CMD18 through
`_sdio_pollCardReady` just like writes). The question is **why TC doesn't fire** and whether a
proper fix restores it (clean completion → DMA).

---

## 2. How Linux's SDHCI core handles this (exact citations)

All from `external/linux/drivers/mmc/host/sdhci.c` (5066 ln) and `sdhci.h` (934 ln).

### 2.1 Interrupt-enable model — Transfer-Complete is **always** enabled

Offsets (`sdhci.h:163-165`): `SDHCI_INT_STATUS 0x30`, `SDHCI_INT_ENABLE 0x34`,
`SDHCI_SIGNAL_ENABLE 0x38`.

`sdhci_set_default_irqs()` (`sdhci.c:300-314`) — written **once**, both 0x34 and 0x38 get the
same mask:
```c
host->ier = SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT |
            SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT |
            SDHCI_INT_INDEX | SDHCI_INT_END_BIT | SDHCI_INT_CRC |
            SDHCI_INT_TIMEOUT | SDHCI_INT_DATA_END |   /* <-- Transfer Complete, bit 1 */
            SDHCI_INT_RESPONSE;                        /* <-- Command Complete, bit 0 */
sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
```
- `SDHCI_INT_DATA_END` = Transfer Complete = bit 1 (`sdhci.h:167` `0x00000002`). **In the
  always-on set.** Never removed.
- `SDHCI_INT_RESPONSE` = Command Complete = bit 0 (`sdhci.h:166` `0x00000001`). Always on.

`sdhci_set_transfer_irqs()` (`sdhci.c:1027-1044`) — the only per-command adjustment — swaps
**PIO** bits (`SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL`) vs **DMA** bits
(`SDHCI_INT_DMA_END | SDHCI_INT_ADMA_ERROR`), and toggles `SDHCI_INT_AUTO_CMD_ERR`:
```c
if (host->flags & SDHCI_REQ_USE_DMA)
        host->ier = (host->ier & ~pio_irqs) | dma_irqs;
else
        host->ier = (host->ier & ~dma_irqs) | pio_irqs;
if (host->flags & (SDHCI_AUTO_CMD23 | SDHCI_AUTO_CMD12))
        host->ier |= SDHCI_INT_AUTO_CMD_ERR;
```
It **never touches `DATA_END` or `RESPONSE`.** So in Linux, Transfer-Complete is enabled for
*every* transfer, PIO or DMA, single or multi.

> **Bearing on our symptom:** our `STATUS_ENABLE` (`sdcard.c:1574`) sets
> `SDHOST_STATUS_MASK | RW_READ_READY | RW_WRITE_READY`, and `SDHOST_STATUS_MASK` →
> `SDHOST_INTR_CMD_STATUS` → includes `SDHOST_INTR_TRANSFER_DONE` (`sdcard.c:66-70`,
> `sdhost_defs.h:209-213`). **Transfer-Complete IS status-enabled, identically to Linux, and
> single-block CMD17 latches it 16/16 under that exact mask.** The "missing enable bit"
> hypothesis is therefore *refuted by our own negative control.*

### 2.2 Transfer-Mode bits and Auto-CMD12 vs Auto-CMD23

TRANSFER_MODE (0x0c) bits (`sdhci.h:38-44`): `TRNS_DMA 0x01`, `TRNS_BLK_CNT_EN 0x02`,
`TRNS_AUTO_CMD12 0x04`, `TRNS_AUTO_CMD23 0x08`, `TRNS_AUTO_SEL 0x0C`, `TRNS_READ 0x10`,
`TRNS_MULTI 0x20`.

`sdhci_set_transfer_mode()` (`sdhci.c:1460-1499`):
```c
if (mmc_op_multi(cmd->opcode) || data->blocks > 1) {
        mode = SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_MULTI;
        sdhci_auto_cmd_select(host, cmd, &mode);
        if (sdhci_auto_cmd23(host, cmd->mrq))
                sdhci_writel(host, cmd->mrq->sbc->arg, SDHCI_ARGUMENT2);
}
if (data->flags & MMC_DATA_READ)  mode |= SDHCI_TRNS_READ;
if (host->flags & SDHCI_REQ_USE_DMA)  mode |= SDHCI_TRNS_DMA;
```
`sdhci_auto_cmd_select()` (`sdhci.c:1421-1458`) picks CMD12 (0x04) or CMD23 (0x08) (or
`AUTO_SEL` on v4.10+ v4_mode). The host flags come from caps setup (`sdhci.c:4532-4547`):
```c
if (host->quirks & SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12)
        host->flags |= SDHCI_AUTO_CMD12;       /* enables Auto-CMD12 at all */
...
if ((host->version >= SDHCI_SPEC_300) && (... ) &&
    !(host->quirks2 & SDHCI_QUIRK2_ACMD23_BROKEN)) {
        host->flags |= SDHCI_AUTO_CMD23;
}
```

> **Bearing on us:** `brcm,bcm2711-emmc2` sets `SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12` (so
> Auto-CMD12 is enabled) and does **NOT** set `SDHCI_QUIRK2_ACMD23_BROKEN` (so Auto-CMD23 is
> *also* considered usable). We use Auto-CMD12 (`sdcard.c:598`), matching Linux's default for
> multi-block reads. CMD23 (S3) is the open lever, see §6.

### 2.3 The completion path — and the Command-Complete / Transfer-Complete **ordering interlock**

`sdhci_irq()` (`sdhci.c:3593-3676`): reads `INT_STATUS`, **write-1-clears** the handled bits in a
single masked `writel` to `INT_STATUS` at the *top* of the loop, then dispatches:
```c
mask = intmask & (SDHCI_INT_CMD_MASK | SDHCI_INT_DATA_MASK | SDHCI_INT_BUS_POWER);
sdhci_writel(host, mask, SDHCI_INT_STATUS);   /* W1C, before handlers */
if (intmask & SDHCI_INT_CMD_MASK)   sdhci_cmd_irq(host, ...);
if (intmask & SDHCI_INT_DATA_MASK)  sdhci_data_irq(host, ...);
```
`sdhci_data_irq()` DATA_END branch (`sdhci.c:3520-3563`):
```c
if (intmask & SDHCI_INT_DATA_END) {
        if (host->cmd == host->data_cmd) {
                /* Data finished before the command completed.
                 * Make sure we do things in the proper order. */
                host->data_early = 1;
        } else {
                sdhci_finish_data(host);
        }
}
```
And Auto-CMD12 error (`sdhci_cmd_irq`, `sdhci.c:3314-3329`) — note **only on error**:
```c
if (intmask & SDHCI_INT_AUTO_CMD_ERR && host->data_cmd) {
        u16 auto_cmd_status = sdhci_readw(host, SDHCI_AUTO_CMD_STATUS);  /* 0x3C */
        ...
        if (!mrq->sbc && (host->flags & SDHCI_AUTO_CMD12)) {
                *intmask_p |= data_err_bit;   /* re-inject as a data error */
                return;
        }
}
```

> **The load-bearing finding:** Linux has an explicit interlock for the case where
> **Transfer-Complete arrives while Command-Complete is still unprocessed** (`data_early`).
> Linux processes both in ONE handler. **There is no separate "Auto-CMD12 done" interrupt** — on
> *success* the only completion signal for a multi-block transfer is `SDHCI_INT_DATA_END`
> (Transfer Complete), raised *after* the internal CMD12 completes. The AUTO_CMD error path is
> reached only on failure.
>
> **Our driver does the opposite order, serially:** it waits for `CMD_DONE` *first*
> (`sdcard.c:622`), which **consumes/clears it** (`*(host->base + INTR_STATUS) = flags`,
> `sdcard.c:275`, with `flags == CMD_DONE` only), *then* runs the PIO loop, *then* waits for
> `TRANSFER_DONE`. Two consequences worth noting (neither is the *primary* cause, but both are
> real divergences): (a) we serialize what Linux interlocks; (b) if this controller ever raised
> CC+TC **together** in one latch (as #119 showed for R1b: `INTR_STATUS == 0x3`), our line-275
> clear of *only* CMD_DONE would leave TC latched — so a *co-raised* TC would still be seen.
> Since `intr=0x0` after the full wait, TC is genuinely **never raised** for the multi-block
> case, which points the finger at the controller's busy-terminal-phase completion, not at our
> clear sequence.

### 2.4 DMA: ADMA2 descriptor + config + SDMA boundary re-arm

ADMA2 descriptor (`sdhci.h:360-399`): `{ __le16 cmd; __le16 len; __le32 addr; }` (32-bit) or
`{ ... __le32 addr_lo; __le32 addr_hi; }` (64-bit). Attr byte: `ADMA2_TRAN_VALID 0x21`
(=ACT_TRAN 0x20 | VALID 0x01), `ADMA2_NOP_END_VALID 0x3`, `ADMA2_END 0x2`.

`sdhci_adma_write_desc()` (`sdhci.c:718-732`):
```c
dma_desc->cmd = cpu_to_le16(cmd);
dma_desc->len = cpu_to_le16(len);
dma_desc->addr_lo = cpu_to_le32(lower_32_bits(addr));
if (host->flags & SDHCI_USE_64_BIT_DMA)
        dma_desc->addr_hi = cpu_to_le32(upper_32_bits(addr));
*desc += host->desc_sz;
```
`sdhci_config_dma()` (`sdhci.c:316-360`) picks the mode into HOST_CONTROL (0x28):
`SDHCI_CTRL_SDMA 0x00`, `SDHCI_CTRL_ADMA32 0x10`, `SDHCI_CTRL_ADMA64 0x18` (`sdhci.h:114-119`).
Address programmed by `sdhci_set_adma_addr` → `SDHCI_ADMA_ADDRESS 0x58` (+ `_HI 0x5C` for 64-bit)
(`sdhci.c:884-905`).

SDMA boundary: `SDHCI_DEFAULT_BOUNDARY_SIZE = 512*1024` (`sdhci.h:353`). On each
`SDHCI_INT_DMA_END` the IRQ re-writes the address rounded up to the next 512K boundary
(`sdhci.c:3535-3550`) — and explicitly does **not** trust the controller's DMA_ADDRESS readback.

DMA addressability at the sdhci layer (`sdhci.c:4134-4153`): `dma_set_mask_and_coherent(64)` if
`USE_64_BIT_DMA` else 32. The **bounce buffer** (`sdhci.c:4210-4263`, `SZ_64K`) exists only for
the single-segment SDMA case to pack scattered sg — it is *not* an out-of-window bounce. The
window itself is enforced by the DT `dma-ranges` + the DMA-API mask, upstream of sdhci.

> **Bearing on us:** Linux moves data with ADMA2 (the descriptor table lives in DMA-coherent
> memory, by definition allocated inside the device's `dma-ranges` window). The addressability
> problem we hit is *the DMA mask / dma-ranges window*, which the Linux DMA API handles before
> sdhci ever sees the address. We have no DMA API; **we must allocate our buffer in the window
> ourselves** (§5, §7-S2).

### 2.5 Clock / HS50 — no tuning, divider N for 50 MHz

`sdhci_calc_clk()` (`sdhci.c:1976-2008`) for 50 MHz from 100 MHz base: loop finds `div=2`
(`100/2=50<=50`), then `real_div=2; div>>=1 → div=1`; encoded into CLOCK_CONTROL bits 8-15
(low 8) + bits 6-7 (high 2) (`sdhci.h:145-150`). `sdhci_set_uhs_signaling()` (`sdhci.c:2303-2326`)
programs the speed mode in HOST_CONTROL2; **tuning** (`sdhci_execute_tuning`) is invoked by the
MMC core only for HS200/SDR104 — **HS50/SDR25/DDR50 never tune**.

> **Bearing on us:** confirms our N=1 (`CLOCK_CONTROL_DIV_2`, 100/(2·1)=50) and "no HS50 tuning"
> stance (already settled in `2026-06-07-bcm2711-emmc2-clock-base.md`). No change.

### 2.6 PIO path — per-block on the PRESENT_STATE level bit

`sdhci_transfer_pio()` (`sdhci.c:615-651`):
```c
mask = (host->data->flags & MMC_DATA_READ) ? SDHCI_DATA_AVAILABLE : SDHCI_SPACE_AVAILABLE;
while (sdhci_readl(host, SDHCI_PRESENT_STATE) & mask) {   /* LEVEL bit, not the IRQ */
        if (host->data->flags & MMC_DATA_READ) sdhci_read_block_pio(host);
        else                                    sdhci_write_block_pio(host);
        if (--host->blocks == 0) break;
}
```
`SDHCI_DATA_AVAILABLE 0x00000800`, `SDHCI_SPACE_AVAILABLE 0x00000400` (`sdhci.h:88-89`). The
data-ready *interrupt* (`DATA_AVAIL`/`SPACE_AVAIL`) only *kicks off* the routine; per-block
readiness is the present-state level bit.

> **Bearing on us:** our PIO loop (`sdcard.c:661-700`) gates on
> `PRES_STATE_BUFFER_READ_ENABLE/_WRITE_ENABLE` — **identical to Linux** (and the prior
> latched-bit approach that raced at 50 MHz, #120, was correctly abandoned). No change.

### 2.7 Cross-OS check — is the Linux approach what *other* systems do? (U-Boot + FreeBSD)

Per the user's request, verified against two other independent code-bases (fetched from primary
GitHub sources 2026-07-01; **web-fetched, not local clones** — quotes are abbreviated as the fetch
returned them, line numbers not available, so treat as corroborating, not citable file:line).

**U-Boot — `drivers/mmc/sdhci.c` (the *generic* SDHCI driver bcm2711 emmc2 uses; NOT the legacy
`bcm2835_sdhost.c`).** This is the most directly relevant comparator because **U-Boot is a
bootloader running on this exact silicon at HS50, and it is POLLED, not interrupt-driven** — so it
reads the Transfer-Complete *status bit* the same way we would:
- Data completion is a **busy-poll of `SDHCI_INT_STATUS` until `SDHCI_INT_DATA_END` is set**:
  ```c
  do { stat = sdhci_readl(host, SDHCI_INT_STATUS);
       if (stat & SDHCI_INT_ERROR) return -EIO;
       ...
  } while (!(stat & SDHCI_INT_DATA_END));
  ```
- Command completion is a poll for `SDHCI_INT_RESPONSE` (`mask = SDHCI_INT_RESPONSE`), *then*
  `sdhci_transfer_data()` waits for `SDHCI_INT_DATA_END`. Multi-block sets
  `SDHCI_TRNS_MULTI | SDHCI_TRNS_BLK_CNT_EN`.
- It **defaults to SDMA** when `SDHCI_CAN_DO_SDMA` is set, and programs `SDHCI_DMA_ADDRESS` with a
  **bus address** translated from CPU-phys: `dma_addr = dev_phys_to_bus(dev, host->start_addr)`.

> **This is the load-bearing cross-OS datapoint.** U-Boot is *not* interrupt-driven, yet it relies
> on `SDHCI_INT_DATA_END` latching in the status register for multi-block. If U-Boot boots a Pi 4
> from SD (it does), then **TC *does* latch on this controller in U-Boot's configuration** — which
> uses **SDMA with a properly bus-translated (in-window) DMA address**, not PIO. That is exactly
> the H2 picture and exactly what our ADMA2-with-in-window-buffer plan (S2) sets up. It also shows
> the `dev_phys_to_bus` translation (= apply the `dma-ranges` `0xc0000000` offset) is the standard
> way the DMA address is formed — our missing piece.

**FreeBSD — `sys/dev/sdhci/sdhci.c` (core).** Confirms the Linux interrupt model verbatim:
- `sdhci_init()` enables `... | SDHCI_INT_DATA_END | SDHCI_INT_RESPONSE | SDHCI_INT_ACMD12ERR`
  (Transfer-Complete and Command-Complete **always enabled**, plus the Auto-CMD12 *error* IRQ).
- Completion via the `SDHCI_INT_DATA_END` interrupt in `sdhci_data_irq()` → `sdhci_finish_data()`.
- Multi-block sets `SDHCI_TRNS_MULTI`, and Auto-CMD12 (`SDHCI_TRNS_ACMD12`) is added unless
  `SDHCI_QUIRK_BROKEN_AUTO_STOP`. Auto-CMD12 error is handled only in `sdhci_acmd_irq()` (the
  `SDHCI_INT_ACMD12ERR` path); **Transfer-Complete fires *after* the Auto-CMD12 completes.**
- **No BCM2835/2711 "broken auto-cmd12 / broken multiblock / broken TC" quirk** exists in the core
  (only a BCM577xx clock-source quirk, unrelated).

> **Conclusion of the cross-OS check:** all three independent code-bases (Linux, U-Boot, FreeBSD)
> agree on the *same* model — Transfer-Complete is the canonical multi-block completion signal,
> fires *after* Auto-CMD12, and Auto-CMD12 only surfaces separately *on error*. **None** carries a
> "broken Transfer-Complete / broken Auto-CMD12" quirk for Broadcom. And the one comparator that
> shares our constraint (U-Boot: polled, reads the status bit) makes TC work by using **DMA with
> an in-window/bus-translated address** — never PIO. This is strong, convergent evidence that our
> symptom is a property of the **PIO + Auto-CMD12/busy** path we are on, not of the silicon, and
> that the reuse-the-proven-approach fix is **ADMA2/SDMA with an in-window buffer** (S2). The
> CMD13-poll completion (S1) remains the correct MMC-core-equivalent fallback. "This is solved by
> humanity" → humanity solved it with **DMA + a correctly-translated low-1 GB address**.

---

## 3. The sdhci-iproc quirks list for `brcm,bcm2711-emmc2`, with what each works around

From `external/linux/drivers/mmc/host/sdhci-iproc.c` (`bcm2711_data` `:294-297`,
`sdhci_bcm2711_pltfm_data` `:289-292`, ops `sdhci_iproc_bcm2711_ops` `:273-287`):

| Linux flag / setting (file:line) | What it works around | Relevant to TC-never-latches? |
|---|---|---|
| **`SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12`** (`:290`) — the **only** `quirks` flag | Forces Auto-CMD12 on multi-block (and *enables* `SDHCI_AUTO_CMD12` at all, `sdhci.c:4532`). The Arasan/iproc core needs the host-issued CMD12 STOP on multi-block reads. | **YES, directly.** This is the flag that arms Auto-CMD12 on every CMD18/CMD25 — the Auto-CMD12 trigger H2 blames for the read-side TC suppression. |
| `quirks2 = 0` — **`SDHCI_QUIRK2_ACMD23_BROKEN` NOT set** | (legacy `iproc`/`cygnus` data *do* set it, `:210,234`) → bcm2711 considers **Auto-CMD23 usable**. | **YES (lever).** Auto-CMD23 (SET_BLOCK_COUNT) has no trailing busy STOP → candidate to make TC latch (§6, S3). |
| **No `SDHCI_QUIRK_BROKEN_DMA`, no `SDHCI_QUIRK_BROKEN_ADMA`** | (contrast `bcm7211a0_data` `:300-302`, which sets both → PIO-only). bcm2711 has working DMA. | **YES (decisive).** Linux uses **ADMA2** on bcm2711, never PIO. So Linux's working TC is a *DMA-mode* fact; it says nothing about PIO-mode TC. ADMA2 is the proven escape. |
| `write_l = sdhci_iproc_writel` w/ post-write `udelay` **only when `host->clock <= 400000`** (`:78-84`) | Arasan clock-domain write-loss, **only below 400 kHz**. | No — at HS50 this delay is OFF; our `dmb`-only writes are correct. Not a cause. |
| `is_cmd/blk_shadowed` 16→32-bit shadow of BLKSIZE/BLKCNT/TRNSMODE/COMMAND (`:106-155`) | Arasan loses back-to-back 16-bit writes to adjacent regs. | No — we are 32-bit-only already (we write `TRANSFER_BLOCK` and `CMD` as single 32-bit words). Equivalent. |
| `get_min_clock = 200000` (`:179`) + known-hang comment (`:167-182`) | Controller hangs when core-clk ≫ bus-clk at very low probe clock. | No — we don't probe at 100 kHz. Note it. |
| `get_max_clock` via clk framework, not CAPS base field (`:157-165,282`) | The CAPS base-clock field is unreliable on this SoC; take 100 MHz from the firmware clk. | No (already settled): we read 100 MHz from mailbox, matches. |
| `mmc_caps = MMC_CAP_3_3V_DDR` (`:296`) | Allows DDR50 at 3.3 V. | No — we run plain HS50 SDR. Not required. |

**Net for the new symptom:** the quirk that *creates* the trailing R1b-busy CMD12
(`MULTIBLOCK_READ_ACMD12`), the absence of `ACMD23_BROKEN` (CMD23 is a usable alternative), and
the absence of any `BROKEN_DMA/ADMA` (Linux is on ADMA2, not PIO) are the three load-bearing
quirk facts. None of them is a "broken Transfer-Complete" quirk — **there is no such quirk** —
which is consistent with TC working fine in the DMA mode Linux actually uses.

---

## 4. Root-cause hypotheses, ranked by evidence

### The discriminating truth table (this is the evidence ranking)

| Command | Auto-CMD12? | Busy terminal phase? | Completion IRQ latches? | source |
|---|---|---|---|---|
| CMD17 read single | no | no | **✓ TC latches** (16/16) | `sdcard.c:731` |
| CMD24 write single (#154) | no | **yes** (PRG) | ✗ TC never | `sdcard.c:756-768` |
| CMD18 read multi | **yes** | **no** (idle, TRAN, `PRES=0x1fff0000`) | ✗ TC never | `sdcard.c:716-727` |
| CMD25 write multi | **yes** | **yes** | ✗ TC never | `sdcard.c:756-768` |
| CMD7 R1b (#119) | n/a | **yes** | ✗ CC never | `sdcard.c:448-508` |

**Read off the table:** the completion IRQ latches **only when BOTH (no Auto-CMD12) AND (no
busy)**. Either condition alone suppresses it. CMD24 isolates the **busy** trigger (no
Auto-CMD12); CMD18 isolates the **Auto-CMD12** trigger (no busy — the card is idle in TRAN). These
are **two related but distinct completion-signalling gaps**, which is why H1 and H2 below are
*co-primary*, not ranked. The unification that matters operationally is the shared fix
(CMD13-poll) and the shared escape (ADMA2).

### H1 (CO-PRIMARY) — busy-completion gap: completion IRQ suppressed when a transfer ends in a DAT0/R1b-busy stop
- **Covers the *write* rows** (CMD24/CMD25) and the CMD7 R1b case — transfers whose terminal phase
  is the card holding DAT0 busy (PRG). On this Arasan/iproc core, the completion IRQ across a
  terminal busy phase is unreliable:
  - **#119:** CMD7 (R1b) → Command-Complete never delivered → we poll PRES_STATE
    (`_sdio_pollBusyCmd`, `sdcard.c:448-508`).
  - **#154:** CMD24 write single (no Auto-CMD12) → card enters PRG (busy) → Transfer-Complete never
    delivered → we poll CMD13 (`_sdio_pollCardReady`, `sdcard.c:353-385`).
- **Evidence FOR (strong):** CMD24 is a *clean isolation* of the busy trigger — it has **no**
  Auto-CMD12 yet TC still never latches, while CMD17 (no busy, no Auto-CMD12) latches TC 16/16
  under the identical enable mask. Card reaches TRAN with no error bits; `intr=0x0` after a full
  1 s wait *and* the pre-wait level check (`sdcard.c:231`) rules out a lost wakeup.
- **Does NOT explain the CMD18 read** (Auto-CMD12 but no busy) — that is H2.
- **Rank: CO-PRIMARY (owns the write half).**

### H2 (CO-PRIMARY) — Auto-CMD12 / non-PIO completion gap: TC suppressed for an Auto-CMD12 transfer in PIO mode
- **Covers the CMD18 read row** (and adds to CMD25): an Auto-CMD12 transfer does not raise TC even
  with **no** busy phase. Two non-exclusive mechanisms: (a) the controller gates Transfer-Complete
  on the *internal CMD12 completion* in a way that only fires in DMA mode, or (b) TC for the
  Auto-CMD12 sequence is wired to the DMA/ADMA-end event and is simply not generated when no DMA
  engine drives the data port (PIO).
- **Evidence FOR:** CMD18 is a *clean isolation* of the Auto-CMD12 trigger — `PRES=0x1fff0000`
  (DAT_BUSY=0, idle) at the timeout, card in TRAN, no error bits, yet `intr=0x0` (our code comment
  `sdcard.c:716-727`). Linux uses **ADMA2 exclusively** on bcm2711 (no BROKEN_DMA/ADMA quirks) and
  its TC works — so Linux's working TC is a *DMA-mode* fact and says nothing about PIO-mode
  Auto-CMD12 TC. There is **no** "broken Transfer-Complete" quirk in `sdhci-iproc.c`, consistent
  with TC being fine in the DMA mode Linux runs.
- **Evidence AGAINST / uncertainty:** we cannot tell from Linux source whether the gap is
  "Auto-CMD12 in any mode" vs "Auto-CMD12 in PIO only" — Linux never runs PIO here. S2 (ADMA2) and
  S3 (CMD23) each resolve it on HW.
- **Rank: CO-PRIMARY (owns the multi-block-read half / the stated crux).**

### H3 (REFUTED) — Missing Transfer-Complete *status-enable* bit (the task's lead hypothesis)
- **Refuted by our negative control:** `STATUS_ENABLE` is written once (`sdcard.c:1574`) with a
  mask that *includes* Transfer-Complete; single-block CMD17 latches TC 16/16 under that exact
  mask. If the bit weren't status-enabled, CMD17 wouldn't latch either. **Rank: REFUTED.**

### H4 (REFUTED) — A CMD23-vs-CMD12 selection mistake / wrong Transfer-Mode bits
- We set the correct multi-block + Auto-CMD12 bits (`sdcard.c:591,596-599`), which is exactly
  what `brcm,bcm2711-emmc2` mandates. The *choice* of CMD12 isn't a "mistake"; switching to CMD23
  is a deliberate experiment (S3), not a bug fix. **Rank: REFUTED as a bug; retained as a lever.**

### H5 (REFUTED) — A write-1-clear / clear-sequence ordering error eats TC
- We clear only `CMD_DONE|TRANSFER_DONE` on entry (`sdcard.c:552`) and the CMD_DONE wait clears
  only `CMD_DONE` (`flags`, `sdcard.c:275`). A co-raised TC would survive. `intr=0x0` means TC was
  never set, not cleared. **Rank: REFUTED.** (Our serialized CC-then-TC wait *does* diverge from
  Linux's `data_early` interlock — §2.3 — but that divergence would only matter if TC were
  raised at all.)

---

## 5. DMA addressability — why SDMA "advances the address but lands no data", and the window fix

From `external/linux/arch/arm/boot/dts/broadcom/bcm2711.dtsi`:
- The emmc2 sits on `emmc2bus` (`:418-433`) with
  **`dma-ranges = <0x0 0xc0000000  0x0 0x00000000  0x40000000>;`** (`:424`):
  - DMA-bus addr `0xc000_0000`, CPU-phys addr `0x0`, size `0x4000_0000` (**1 GB**).
  - i.e. the controller's DMA address space maps **only** CPU-physical `[0, 0x4000_0000)`, via
    the VC `0xc0000000` alias.
- The soc node carries the same constraint (`:45`, "Emulate a contiguous 30-bit address range for
  DMA"). emmc2 node itself: `reg = <0x0 0x7e340000 0x100>`, `interrupts = <GIC_SPI 126 ...>`,
  `clocks = <&clocks BCM2711_CLOCK_EMMC2>` (`:427-430`); board file adds `broken-cd`,
  `vqmmc-supply`, `vmmc-supply` (`bcm2711-rpi-4-b.dts:198-204`). **No `sd-uhs-*`, no explicit
  64-bit-DMA property** in the base node.

**Our buffer** (`sdcard.c:188`): `mmap(..., MAP_PRIVATE|MAP_ANONYMOUS|MAP_UNCACHED|MAP_CONTIGUOUS, ...)`
then `host->dmaBufferPhys = va2pa(...)` (`:198`). On a Pi 4 (4 GB) the allocator routinely returns
a physical address **above 1 GB**, which is **outside the emmc2 DMA window**. Feeding that to
`SDHOST_REG_SDMA_ADDRESS` (`sdcard.c:577`) is precisely why SDMA "advances its address register
but never lands data" — the controller drives a bus address that the fabric does not route back to
our buffer. This is the textbook out-of-DMA-window signature, and it is **the** blocker for ADMA2.

**Linux solves it upstream of sdhci** via the DMA API: DMA-coherent allocations for a device
honour its `dma-ranges`, so the descriptor table and (for SDMA) the bounce buffer are always
in-window. **We have no DMA API**, so we must do the equivalent by hand: allocate the staging
buffer (and any ADMA2 descriptor table) in **low physical memory `< 0x4000_0000`**.

> **Phoenix mechanism check (needs confirming in-tree, flagged):** we need a way to force a
> low-1 GB physical allocation. Candidates: a `MAP_*` flag/zone that bounds physical address, a
> reserved low-memory pool, or allocating from a known low region and `va2pa`-verifying
> `phys + size <= 0x4000_0000`. The minimal correctness gate before any ADMA2 work is: **assert
> `va2pa(buffer) + SDCARD_MAX_TRANSFER <= 0x40000000`** and fail init loudly otherwise. (Today the
> code silently uses an out-of-window buffer and relies on PIO not caring.)

---

## 6. Side-by-side: Linux (bcm2711, ADMA2) vs our driver (PIO)

| Mechanism | Linux `sdhci` on `brcm,bcm2711-emmc2` | Our `bcm2711-emmc` driver |
|---|---|---|
| **Data movement** | **ADMA2** (no BROKEN_DMA/ADMA quirks); PIO is fallback only | **PIO** over BUFFER_DATA FIFO (`sdcard.c:661-700`); SDMA disabled (`dmaEnable=0`, `:590`) because buffer is out-of-window |
| **Completion (single-block)** | `SDHCI_INT_DATA_END` (TC), always-enabled IRQ (`sdhci.c:305`) | CMD17: TC IRQ wait, works 16/16 (`sdcard.c:731`). CMD24 write: **CMD13-poll** (TC never fires) |
| **Completion (multi-block + Auto-CMD12)** | `SDHCI_INT_DATA_END` raised after internal CMD12 (`sdhci.c:3543`); AUTO_CMD_ERR only on failure | **CMD13-poll to TRAN** (`_sdio_pollCardReady`, `sdcard.c:716-727`) — TC `intr=0x0` |
| **Multi-block stop** | Auto-CMD12 (forced by `MULTIBLOCK_READ_ACMD12`); Auto-CMD23 available (ACMD23 **not** broken) | Auto-CMD12 (`autoCmd12Enable=1`, `:598`); CMD23 unused |
| **Interrupt enables** | 0x34 & 0x38 written once w/ TC+CC always on (`sdhci.c:307-313`); per-cmd PIO↔DMA + AUTO_CMD_ERR swap (`:1027-1044`) | 0x34 written once: `STATUS_MASK \| RW_READ/WRITE_READY` (`sdcard.c:1574`); 0x38 armed per-wait to `AWAITABLE_INTRS`, masked in ISR (`:211,221`) |
| **CC/TC ordering** | One IRQ handler, `data_early` interlock (`sdhci.c:3543-3550`) | **Serial**: wait CC (`:622`) → PIO → wait TC/CMD13 (`:715-781`) |
| **PIO per-block gate** | PRESENT_STATE level bit (`sdhci.c:625`) | PRES_STATE level bit (`sdcard.c:669`) — **matches** |
| **DMA addressability** | DMA API honours `dma-ranges` 1 GB window | **Broken**: `va2pa` returns >1 GB phys, outside window (`sdcard.c:188,198`) → SDMA lands no data |
| **Clock / HS50** | `calc_clk` div→N=1 for 50 MHz; no HS50 tuning (`sdhci.c:1976-2008`) | `DIV_2` (N=1) → 50 MHz (`sdcard.c:1634`); no tuning — **matches** |
| **Error mask incl. AUTO_CMD12** | AUTO_CMD_ERR routed to cmd_irq, re-injected as data error (`sdhci.c:3314`) | `SDHOST_INTR_AUTO_CMD12_ERROR` in `SDHOST_ERROR_REASONS` (`sdcard.c:62`) — surfaces as -EIO |

---

## 7. Concrete, prioritized, reuse-the-proven-approach plan (Phoenix style)

Ordered lowest-risk-highest-value first. Each step: what to transcribe, and how to validate.

### S1 — KEEP the CMD13-poll completion; reframe it as correct, not a workaround (zero risk)
- **Do:** leave `_sdio_pollCardReady` driving both write completion (`sdcard.c:768`) and
  multi-block-read completion (`:726`). Update the code comments to state it is the
  **MMC-core-equivalent** completion (`mmc_blk_card_busy` → `__mmc_poll_for_busy` →
  `mmc_ready_for_data`, `core/block.c`/`core/mmc_ops.c`) — the *authoritative* completion Linux
  itself relies on for writes — not a hack to be removed.
- **Why:** it is the safety net regardless of S2's outcome. If the controller never raises TC for
  busy-terminal transfers, this is the permanent answer.
- **Validate:** already HW-proven (writeRc 16/16; multi-block read drains correctly). No new HW.

### S2 — THE PROPER FIX + THROUGHPUT (one move): ADMA2 with an in-window buffer
This is where "restore clean completion" and "escape PIO for throughput" **converge** — do them
together, because the addressability fix is the prerequisite for *either*.

- **S2a — allocate the staging buffer (and ADMA2 descriptor table) in low-1 GB physical.**
  Transcribe the *constraint*, not Linux code (we have no DMA API): force the buffer phys into
  `[0, 0x4000_0000)` and **assert** `va2pa(buf) + SDCARD_MAX_TRANSFER <= 0x40000000` at init
  (fail loudly otherwise). Resolve the Phoenix low-memory allocation mechanism first (see §5
  flag). This alone is the gate; without it, neither SDMA nor ADMA2 can land data.
- **S2b — build the ADMA2 path**, transcribing:
  - descriptor format + writer: `sdhci_adma_write_desc` (`sdhci.c:718-732`), attr
    `ADMA2_TRAN_VALID 0x21`, end via `ADMA2_END 0x2` (`sdhci.h:360-399`). We already have
    `HOST_CONTROL_DMA_SELECT_ADMA32/64` and `SDHOST_REG_ADMA_ADDR_1/_2` in `sdhost_defs.h:111-112,146-147`.
  - mode select into HOST_CONTROL: `sdhci_config_dma` (`sdhci.c:316-360`) → write `ADMA32` (0x10)
    (start 32-bit; the buffer is in-window so 32-bit addressing suffices — **do NOT** need 64-bit
    ADMA — **assumption**: the base caps likely don't advertise 64-bit DMA, but we read the DT,
    not the HW Capabilities register where 64-bit-DMA support actually lives; not load-bearing
    since an in-window buffer makes 32-bit ADMA sufficient regardless).
  - set `cmdFrame.dmaEnable = 1` (replacing the `dmaEnable=0` at `sdcard.c:590`) and
    `TRNS_DMA`-equivalent; program `SDHOST_REG_ADMA_ADDR_1` with the descriptor-table phys.
  - completion: wait on `SDHOST_INTR_TRANSFER_DONE` again for the DMA path.
- **The decisive HW observation (this is the open question):** with ADMA2 + in-window buffer, does
  **Transfer-Complete latch** for CMD18/CMD25?
  - **If YES:** we have clean completion *and* throughput, and we've **proven** the TC suppression
    is PIO/busy-specific (H2). Multi-block DMA becomes the fast path; CMD13-poll stays only for the
    single-block write tail (or is replaced by TC there too — retest).
  - **If NO:** the controller never raises TC across the busy stop even in DMA mode (H1 in its
    strongest form). Keep CMD13-poll (S1) as completion for DMA too. We *still* win the throughput
    (data moved by ADMA2, not the FIFO).
- **Validate:** `rebuild --scope core --variant sd` → ext2 rootfs → `dd` to `/dev/sda` (verify
  first-4MB sha) → user shuttles card → `test-cycle-netboot.sh --sd-boot`. Bar: a **>1 MB**
  sustained `dd` read AND a multi-block write+readback both verify byte-exact, 0 faults, ≥2 boots,
  and the SDDIAG build logs whether TC latched. **Needs the HW card swap to validate** (SD test
  requires `--sd-boot`).

### S3 — CHEAP EXPERIMENT (gated, low confidence): Auto-CMD23 instead of Auto-CMD12 for multi-block
- **Do:** for CMD18/CMD25, issue CMD23 SET_BLOCK_COUNT first and set `TRNS_AUTO_CMD23`-equivalent
  instead of `autoCmd12Enable`. `SDHCI_QUIRK2_ACMD23_BROKEN` is **not** set for bcm2711
  (`sdhci-iproc.c:289-292`), so Linux deems CMD23 usable on this core.
- **Why it might restore TC:** CMD23 bounds the transfer up-front; there is **no trailing R1b
  CMD12 busy STOP**. If H1 is "TC suppressed specifically by the terminal R1b busy", removing that
  busy could let TC latch. Low confidence (the data still ends; the controller may still gate TC
  on a DMA event), but it is a few lines and one boot.
- **Order:** can be tried before S2b (still needs S2a if combined with DMA; works as-is on the
  current PIO path with no DMA). If it makes TC latch on PIO, that is a strong, cheap result.
- **Validate:** same bar as S2; compare `intr` at completion with/without CMD23.

### S4 — cleanups to fold in while here (no behavior risk)
- Remove the **dead SDMA programming on the PIO path** (`sdcard.c:577-582`: writing
  `SDHOST_REG_SDMA_ADDRESS` + `TRANSFER_BLOCK_SDMA_BOUNDARY_4K` while `dmaEnable=0`) once S2
  supersedes it — misleading for upstreaming.
- When multi-block DMA lands, **read `SDHOST_REG_AUTOCMD12_ERROR_STATUS`** (`sdhost_defs.h:104`,
  currently never read) in the Auto-CMD12 error path, mirroring `sdhci_cmd_irq`'s
  `SDHCI_AUTO_CMD_STATUS` read (`sdhci.c:3317`).
- Consider adopting Linux's CC/TC `data_early` interlock idea (§2.3) only if S2 keeps the TC IRQ —
  otherwise N/A (CMD13-poll has no ordering hazard).

**Distinguish the two deliverables explicitly:**
- *Restore clean Transfer-Complete (the "proper fix")* = **S2 with the YES branch**, or **S3** —
  both are HW-gated and may or may not succeed; CMD13-poll (S1) is the guaranteed fallback.
- *Escape PIO (the real throughput unlock)* = **S2 (ADMA2)** regardless of branch — the data
  moves by DMA either way; only the completion signal's source differs.

---

## 8. What needs HW to settle (cannot be answered from sources)

1. **Does TC latch for CMD18/CMD25 in ADMA2 mode (in-window buffer)?** Decides H1 vs H2 and
   whether the TC IRQ is ever usable on this controller. Same boot delivers throughput. (S2.)
2. **Does Auto-CMD23 (no trailing busy) let TC latch on the current PIO path?** (S3.)
3. **What is the lowest-risk Phoenix mechanism for a guaranteed-low-1 GB physical allocation?**
   In-tree investigation, then the §5 assert. (S2a prerequisite.)

---

## Citations

### Read directly from `external/linux/` (real file:line)
- **sdhci.c:** default IRQ set incl. `DATA_END`+`RESPONSE` `300-314`; per-cmd PIO↔DMA+AUTO_CMD_ERR
  swap `1027-1044`; `config_dma` mode select `316-360`; PIO transfer (PRESENT_STATE level)
  `615-651`, read/write block `536-613`; ADMA write desc `718-732`, mark_end `745-751`, table_pre
  `774-841`; `set_adma_addr`/`sdma_address` `884-905`; `calc_clk` `1976-2008`, `enable_clk`
  `2014-2065`, `set_clock` `2068-2082`; `set_uhs_signaling` `2303-2326`; auto-cmd helpers
  `1402-1419`, `auto_cmd_select` `1421-1458`, `set_transfer_mode` `1460-1499`; `prepare_dma`
  `1182-1203`; `send_command` ordering `1688-1697`; cmd_irq auto-CMD12 `3314-3329` / auto-CMD23
  `3374-3392`; data_irq DATA_END + `data_early` `3520-3563`, DMA_END boundary re-arm `3535-3550`;
  top-level irq W1C `3593-3676`; AUTO_CMD12 host-flag enable + ACMD23 gate `4532-4547`; dma_mask
  `4134-4153`; bounce buffer `4210-4263`.
- **sdhci.h:** INT offsets `163-165`; INT bits `166-195` (`RESPONSE 0x1`, `DATA_END 0x2`,
  `DMA_END 0x8`, `SPACE_AVAIL 0x10`, `DATA_AVAIL 0x20`, `AUTO_CMD_ERR 0x1000000`,
  `DATA_END_BIT 0x400000`); CMD/DATA masks `200-207`; TRNS bits `38-44`; PRESENT_STATE
  `DATA_AVAILABLE 0x800`/`SPACE_AVAILABLE 0x400` `88-89`; HOST_CONTROL DMA mode `114-119`; CLOCK
  divider fields `144-154`; ADMA2 desc + attrs `357-399`; boundary `353-354`; ADMA addr regs
  `308-309`.
- **sdhci-iproc.c:** `bcm2711_data` `294-297`; `sdhci_bcm2711_pltfm_data` (quirks =
  `MULTIBLOCK_READ_ACMD12` only) `289-292`; bcm2711 ops `273-287`; legacy `iproc`/`cygnus`
  ACMD23_BROKEN+NO_HISPD `207-236`; `bcm7211a0` BROKEN_DMA+BROKEN_ADMA `299-302`; writel udelay
  ≤400 kHz `71-85`; shadow writew `106-155`; get_min_clock 200 kHz + hang comment `167-182`;
  get_max_clock `157-165`; OF match `brcm,bcm2711-emmc2` `324`.
- **bcm2711.dtsi:** `emmc2bus` + `dma-ranges <0x0 0xc0000000 0x0 0x0 0x40000000>` `418-433`
  (dma-ranges `424`); soc dma-ranges `45`; emmc2 reg/irq/clocks `427-430`.
- **bcm2711-rpi-4-b.dts:** emmc2 `broken-cd`/supplies/okay `198-204`.

### Read directly from our source (`sources/phoenix-rtos-devices/storage/bcm2711-emmc/`)
- **sdcard.c:** error/status masks `58-77`; ISR masks SIGNAL_ENABLE `203-213`; `_sdio_cmdExecutionWait`
  (arm SIGNAL_ENABLE, lost-wakeup guard, W1C `flags`) `216-299`; `_sdio_rawSendStatus` (raw CMD13)
  `314-339`; `_sdio_pollCardReady` `353-385`; `_sdio_pollBusyCmd` (R1b PRES_STATE poll) `448-508`;
  `_sdio_cmdSend` (clear DONE `552`, multi-block+autoCmd12 `591-599`, SDMA addr write `577-582`,
  `dmaEnable=0` `590`, CMD_DONE wait `622`, PIO loop level-gated `661-700`, CMD18 CMD13-poll
  `715-727`, write CMD13-poll `768`) `511-794`; DMA buffer mmap+va2pa `184-200`; STATUS_ENABLE
  init `1574-1575`; clock config N=1/HS bit `1609-1648`.
- **sdhost_defs.h:** register enum `88-114`; PRES_STATE bits `116-127`; HOST_CONTROL incl. ADMA
  select `140-157`; CLOCK_CONTROL `159-179`; INTR bits `184-207`; status/error masks `209-224`;
  command-data types `226-236`; CARD_STATUS R1 `365-411`.
- **sdcard.h:** `SDCARD_MAX_TRANSFER 65536` `21`.

### Prior internal docs (consolidated, not re-derived)
- `docs/inprogress/2026-06-29-sd-bcm2711-research.md` — single-block write CMD13-poll fix +
  iproc quirks (superset of the 2026-06-07 docs). This doc extends it to multi-block/Auto-CMD12.

### Web-fetched 2026-07-01 (primary GitHub sources; NOT local clones — quotes verified, no file:line)
- **U-Boot** `drivers/mmc/sdhci.c` (master) —
  `https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/mmc/sdhci.c`. Verified: polled
  `sdhci_transfer_data` busy-loops `SDHCI_INT_STATUS` until `SDHCI_INT_DATA_END`; command poll on
  `SDHCI_INT_RESPONSE`; defaults to SDMA when `SDHCI_CAN_DO_SDMA`; programs `SDHCI_DMA_ADDRESS`
  with `dev_phys_to_bus(...)` (bus-translated CPU-phys). This is the generic SDHCI path bcm2711
  emmc2 uses — *not* the legacy `bcm2835_sdhost.c`. (See §2.7.)
- **FreeBSD** `sys/dev/sdhci/sdhci.c` (main) —
  `https://raw.githubusercontent.com/freebsd/freebsd-src/main/sys/dev/sdhci/sdhci.c`. Verified:
  `sdhci_init` enables `SDHCI_INT_DATA_END | SDHCI_INT_RESPONSE | SDHCI_INT_ACMD12ERR`; completion
  via `SDHCI_INT_DATA_END` in `sdhci_data_irq`→`sdhci_finish_data`; Auto-CMD12 via
  `SDHCI_TRNS_ACMD12` unless `SDHCI_QUIRK_BROKEN_AUTO_STOP`; error-only `sdhci_acmd_irq`; no
  BCM2835/2711 broken-auto-cmd12/TC quirk. (See §2.7.)

### `[TRAINING-KNOWLEDGE]` — NOT read from any source this session
- **Circle** (bare-metal Pi4) `addon/SDCard/emmc.cpp::EnsureDataMode` issues CMD13 around data ops
  and polls CURRENT_STATE; `DoDataCommand` does RCA-invalidate + full reset on persistent error.
  Prior-web citation (2026-06-29 doc); not in `external/` and not re-fetched here.
- **U-Boot** `drivers/mmc/mmc.c::mmc_poll_for_busy` (CMD13 busy poll) — prior-web citation
  (2026-06-29 doc); the §2.7 fetch covered `sdhci.c`, not `mmc.c`.
