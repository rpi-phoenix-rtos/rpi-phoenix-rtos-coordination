# SD #154 — ROOT CAUSE FOUND: PIO write completion (Transfer-Complete IRQ never fires)

> **STATUS (2026-06-26): still ACTIVE / PARKED — no change.** Root cause stands (writes
> land but the Transfer-Complete IRQ never fires for writes → false ETIME); the CMD13-poll
> completion fix is still designed-not-implemented. Validation needs an attended SD-card
> swap, so it stays parked. The SD-write WIP in `storage/bcm2711-emmc/` is flagged by the
> cleanup plan (Phase A) as "finalize behind a branch/flag."

**Date:** 2026-06-07. **Task:** #154. **Status:** root cause HW-proven; **fix designed, not yet
implemented**. Paused for an overnight NFS session (SD needs card-swaps; card is in the host).

## The finding (decisive, HW-proven)

On the BCM2711 EMMC2 controller, single-block **writes physically succeed but are reported as
failures.** Instrumented self-test (`SDDIAG`) at the operational 50 MHz config, 16 trials, card
in HS + 4-bit, reset between every attempt:

```
read  LBA0   x16: readOk=16/16
write LBA100 x16: writeRc=0/16  dataMatch=16/16     <-- data lands every time, "write" never returns OK
  per attempt: transfer_done-timeout cmd=24 ret=-37  pres=0x1fef0000 intr=0x00000000
               DAT0release=0ms  cmd13rc=0  cardState=4 (TRAN)
```

Interpretation:
- **Reads** complete via the `TRANSFER_DONE` (Transfer Complete) interrupt → 16/16, perfect.
- **Writes**: CMD24 + CMD_DONE OK, PIO data pushed OK (no `pio-timeout`), **but
  `TRANSFER_DONE` never latches** (`intr=0x00000000`) for the full 1 s wait → driver returns
  `-ETIME` (`-37`). Yet the **data is correct on read-back (16/16 MATCH)** and the card is back
  in **TRAN (state 4)**, READY_FOR_DATA, with DAT0 released. So the write *worked*; only the
  *completion detection* is broken.
- `pres=0x1fef0000` at the timeout = DAT0 line low (card was signalling busy) with the
  controller's own `DATA_INHIBIT`(bit1)=0 — present-state bits are internally inconsistent on
  this controller, so **do not trust DAT0/DATA_INHIBIT**; use the card's CMD13 status.

This is **not** signal integrity, **not** the clock, **not** a bad block, **not** ext2:
- Clock base = **100 MHz** confirmed on-chip (`CAPS baseClk[15:8]=100MHz`) + by Linux-source
  research → our `N=1` divider = 50 MHz, correct (NOT overclocking). [refuted: 2× overclock]
- Host (proper timing) writes the same card + LBA 137786 perfectly → media healthy. [refuted: bad block]
- 25 MHz "failure" earlier was a botched experiment (HS bit + HS-function mismatch). [refuted]

## The fix (designed — implement next)

For **writes only**, replace the `_sdio_cmdExecutionWait(TRANSFER_DONE, 1s)` with a **CMD13
SEND_STATUS poll** until the card reports `CURRENT_STATE == TRAN(4)` **and** `READY_FOR_DATA`
(bit 8). Rationale (advisor + Circle `EnsureDataMode` + cross-OS doc
`docs/research/2026-06-07-sd-driver-cross-os-comparison.md`):
- CMD13 is the card's authoritative state — immune to this controller's flaky present-state bits.
- CMD13 is **CMD-line-only** (valid while DAT0 busy) and **actively clocks the bus**, which itself
  likely lets the card finish its busy (the passive TC-wait generates no clocks).
- Reads keep `TRANSFER_DONE` (works).

### Implementation constraints (important)
1. The completion runs **inside `_sdio_cmdSend` while `host->eventLock` is HELD** → cannot recurse
   through `_sdio_cmdSend`/`sdio_cmdSend` (would deadlock + hit the pre-command busy poll). Write a
   **raw CMD13 issue** that operates directly on registers and uses `_sdio_cmdExecutionWait(CMD_DONE)`
   (which is called with eventLock held in the normal path, so that's fine).
2. The raw CMD13 must **bypass the pre-command `PRES_STATE_BUSY_FLAGS` poll** (sdcard.c ~377-390,
   bails `-EBUSY` after ~100 ms on `DAT_BUSY`) — else it never issues while DAT0 is busy.
3. Branch on direction at the post-PIO completion: `if (pioRead)` keep TRANSFER_DONE wait; `else`
   CMD13-poll. Bound the poll (~500 ms). On error interrupt during poll → `-EIO` + reset.
4. After success, clear the never-raised stale `TRANSFER_DONE` bit so it can't confuse the next cmd.

### Building blocks (verified in tree)
- `SDIO_CMD13_SEND_STATUS = 13`; metadata `[13] = RESPONSE_METADATA_R1(CMD_NO_DATA)` (R1, no data).
- `CARD_STATUS_READY_FOR_DATA = (1<<8)`; `CARD_STATUS_CURRENT_STATE(x) = ((x>>9)&0xf)`;
  `CARD_STATUS_CURRENT_STATE_TRAN = 4`.
- CMD13 arg = `host->card.rca` (already stored in the [31:16] arg form; see sdcard.c usage ~789).
- `sdhost_command_reg_t { commandIdx:6; ...; commandMeta:8; ... .raw }` — mirror the no-data path
  in `_sdio_cmdSend` (set TRANSFER_BLOCK=0, no dataPresent).
- The dual-purpose validation build should re-enable the `SDDIAG` self-test and log the CMD13
  state sequence + elapsed time ("TRAN after Nms" vs "stuck PRG → timeout").

### Validation bar (writes were flaky — one pass ≠ done)
- Writes: ≥16–32 raw single-block writes via SDDIAG, all succeed + readback MATCH.
- A real file write→read through the mounted ext2 rootfs from psh.
- Boot-to-psh, 0 faults, across **≥2 boots**.
- Reads stay 16/16 (don't regress the read path).

## Uncommitted source state (sources/phoenix-rtos-devices/storage/bcm2711-emmc/sdcard.c)
- **KEEP (real fix):** reset CMD+DAT on the CMD_DONE-timeout and TRANSFER_DONE-timeout paths in
  `_sdio_cmdSend` (a timed-out command previously wedged every later command → "one bad write →
  failed MBR read" cascade). Not gated.
- **Gated OFF (`#ifdef SDCARD_DIAG_CLOCKSWEEP`, macro left undefined):** the `sdcard_diagReset` /
  `sdcard_diagClockSweep` self-test (read x16 + write+readback+CMD13 x16) and three `*-timeout`
  localization prints in `_sdio_cmdSend`, plus the call at the tail of `sdcard_initCard`.
- **NOT yet done:** the CMD13-poll write-completion fix itself.
- `sdstorage_dev.c`: write-retry in `sdcard_writeCb` (added earlier) — keep; revisit once the
  completion fix lands (retry may become unnecessary but is harmless).
- Nothing committed yet — commit the whole #154 fix together after validation.

## Resume tomorrow
1. Implement the raw-CMD13 write-completion in `_sdio_cmdSend` (constraints above).
2. Re-enable `#define SDCARD_DIAG_CLOCKSWEEP 1`, build SD variant, flash (card→host→Pi), boot,
   confirm `writeRc=16/16` + state-sequence log.
3. Real file-write through ext2 from psh; ≥2 boots, 0 faults.
4. Remove the diagnostic, commit (devices) + manifest. Update the hardware matrix row.

Tooling recap: `rebuild-rpi4b-fast.sh --scope core --variant sd` → `build-rpi4b-rootfs-ext2.sh`
→ `dd` the 2-part image to `/dev/sda` (verify first-4MB sha) → user shuttles card → Pi →
`test-cycle-netboot.sh --sd-boot --label … --capture-secs 150` → `grep -a SDDIAG`.
