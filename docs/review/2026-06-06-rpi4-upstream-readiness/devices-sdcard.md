# devices-sdcard — upstream-readiness review

- **Area:** `devices-sdcard`
- **Repo:** `phoenix-rtos-devices` (base `origin/master` `d511e0f` → head `master` `ebac8e4`)
- **Files reviewed (RPi4 net diff only):**
  - `storage/bcm2711-emmc/sdcard.c`
  - `storage/bcm2711-emmc/sdcard.h`
  - `storage/bcm2711-emmc/sdhost_defs.h`
  - `storage/bcm2711-emmc/bcm2711-sdio.c`
  - `storage/bcm2711-emmc/bcm2711-sdio.h`

## Method / scope note

These files are presented as "entirely new", but the new-vs-upstream net contribution is
narrower than the file count suggests:

- `sdhost_defs.h` is **byte-identical** to `storage/zynq7000-sdcard/sdhost_defs.h`
  (verified with `diff -u`: no output). It is a verbatim copy of pre-existing upstream
  code authored by Jacek Maksymowicz (2023).
- `sdcard.c` is a **fork** of `storage/zynq7000-sdcard/sdcard.c`; the RPi4-authored delta
  is only the hunks shown by `diff -u zynq7000-sdcard/sdcard.c bcm2711-emmc/sdcard.c`
  (the include swap, the lost-wakeup guard, the R1b poll path, the PIO data path, the
  ACMD41 `-ENODEV` mapping, the error-class split, and the STATUS_ENABLE change).
- `sdcard.h` is a near-identical fork of the zynq header.
- `bcm2711-sdio.c` / `bcm2711-sdio.h` are genuinely new (BCM2711 EMMC2 platform layer).

Per the rubric ("review only the changed hunks — never flag pre-existing upstream code"),
I audited the register constants/masks in `sdhost_defs.h` **for RPi4-introduced
divergence** as the task asked, and found there is none: the file is unchanged from zynq.
The reviewable issue there is therefore the *duplication* (ARCH-1), not any individual
constant. All `sdhost_defs.h` constants/masks remain as-shipped upstream and are out of
scope to re-litigate.

---

## Findings (ordered by severity)

### ARCH-1 · `storage/bcm2711-emmc/sdcard.c:28` (+ `sdhost_defs.h`, `sdcard.h`, `sdstorage_*`) · ARCH · sev=high
- **WHAT:** The generic SDHCI core (`sdcard.c`, `sdhost_defs.h`, `sdstorage_*.c`) is
  forked verbatim from `storage/zynq7000-sdcard/`. `sdhost_defs.h` is byte-identical;
  `sdcard.c` differs only in the platform include and the BCM2711 deltas. A
  `TODO(rpi4b-emmc)` at line 28 acknowledges this and asks to de-duplicate into a shared
  lib before upstreaming.
- **WHY:** Upstreaming a second full copy of an SDHCI core is the single biggest
  presentation problem in this area: two divergent copies of the same standard-controller
  logic will drift, and any future fix (e.g. the zynq `off_t`/msg-API churn already in
  zynq history) must be applied twice. SDHCI is a *standard* — the Arasan/EMMC2 block and
  the Zynq block are both SD Host Controller Simplified-Spec parts; the only real
  differences are the platform layer (`*-sdio.c`) and a handful of controller quirks.
- **REFERENT:** `storage/zynq7000-sdcard/sdcard.c` + `sdhost_defs.h` (identical core);
  the platform-layer split (`sdio_platformConfigure()` contract in `*-sdio.h`) is already
  the intended seam — both drivers consume the same `sdio_platformInfo_t`.
- **REC:** Before upstreaming, hoist `sdcard.c`/`sdhost_defs.h`/`sdstorage_*` into a shared
  `storage/libsdhci` (or similar) parameterised by `sdio_platformInfo_t` plus a small quirk
  flag for the BCM2711 PIO/R1b-poll behaviour; leave only `bcm2711-sdio.{c,h}` +
  board glue per-platform. This is a structural refactor — **NEEDS-HW** (must re-validate
  both zynq and rpi4b boots after the merge). Keep the `TODO(rpi4b-emmc)` marker until done.

### BUG-1 · `storage/bcm2711-emmc/sdcard.c:460-491` · BUG · sev=med
- **WHAT:** Multi-block PIO data path (CMD18 read-multi / CMD25 write-multi) is unproven.
  Per project tracking (`project_pi4_sdroot_120`), only **CMD17 single-block lba0** has
  been validated on HW; CMD18 multi-block has never moved correct data. The loop clears
  `RW_READ_READY`/`RW_WRITE_READY` (w1c) and *then* drains the 512-byte FIFO for each of
  `blockCount` blocks, relying on the controller to re-assert the ready bit per block.
- **WHY:** The clear-then-drain order itself is the standard SDHCI PIO pattern (cf. Linux
  `sdhci_read_block_pio()` / `sdhci_irq`), so it is **not** wrong from the code alone — I
  am explicitly *not* calling it a defect. The risk is the per-block re-assert/refill
  interaction: (a) does the controller correctly re-raise the ready bit after each block,
  and (b) on multi-block, can clearing the ready bit before the drain completes race the
  controller's next-block FIFO refill / Transfer-Complete? With `autoCmd12Enable=1` on
  multi-block, the auto-CMD12 + Transfer-Complete ordering at the tail is also untested.
- **REC:** Do not present multi-block reads/writes as working. On HW, validate CMD18 over
  ≥2 blocks and CMD25, instrumenting `INTR_STATUS` per block to confirm ready re-assert and
  that no `DATA_CRC`/`DATA_ENDBIT` fires. If multi-block proves unreliable, gate the read/
  write entry points to single-block (loop CMD17/CMD24) until fixed. **NEEDS-HW** (document
  only; cannot validate overnight without a card-swap to SD-boot).

### BUG-2 · `storage/bcm2711-emmc/sdcard.c:463` · BUG · sev=low
- **WHAT:** The per-block FIFO-ready wait is a fixed iteration-count busy-spin
  (`for (spin = 0; spin < 2000000; spin++)`) with no `usleep`/yield, used as a timeout.
- **WHY:** A loop-count "timeout" is wall-clock-nondeterministic (depends on CPU/cache/
  contention), unlike every other timeout in this file which is microsecond-based
  (`_sdio_cmdExecutionWait(..., wait_us)`, `usleep(cmdPollUs)` in `_sdio_pollBusyCmd`).
  It also hard-spins a core with no scheduler yield for up to the full bound. The choice
  is defensible for a PIO inner loop (the ready bit can come fast and `usleep` granularity
  would dominate), but the magic `2000000` is undocumented.
- **REFERENT:** Same-file `_sdio_pollBusyCmd()` (lines 282-330) uses named bounds
  (`cmdPollMax`/`busyPollMax`) + `usleep(cmdPollUs)` and documents the wall-clock budget in
  a comment.
- **REC:** Replace the bare `2000000` with a named constant and a one-line comment stating
  the intended wall-clock budget; consider a coarse `usleep` fallback if the tight spin
  exceeds a threshold. **NEEDS-HW** (it is inside the live data path; document only).

### COMMENT-1 · `storage/bcm2711-emmc/sdcard.c:394-399` · COMMENT · sev=med
- **WHAT:** With `cmdFrame.dmaEnable = 0` now hard-set (line 407), the SDMA setup
  immediately above is dead: `*(... SDHOST_REG_SDMA_ADDRESS) = host->dmaBufferPhys;` and
  the `TRANSFER_BLOCK_SDMA_BOUNDARY_4K` field OR'd into `TRANSFER_BLOCK` only matter when
  SDMA is enabled. They are now a misleading no-op write to a device register on every
  data command.
- **WHY:** A reader (and a maintainer of the future shared lib) will assume SDMA is in use.
  Writing `SDMA_ADDRESS`/`SDMA_BOUNDARY` when DMA is disabled is at best confusing and at
  worst (on some controllers) primes a path that is never taken. The comment block at
  403-406 explains *why DMA is off* but does not flag that the surrounding register writes
  are now vestigial.
- **REC:** Drop the `SDMA_ADDRESS` write and the `SDMA_BOUNDARY_4K` term from
  `TRANSFER_BLOCK` (keep `blockCount` + `blockLength`), or add a comment that they are
  retained only as a no-op for a future DMA re-enable. Because it removes a live MMIO
  write to the controller, treat as **NEEDS-HW** (re-run a single-block read boot to
  confirm no regression) — do not blind-apply despite being "dead".

### ROLLBACK-1 · `storage/bcm2711-emmc/sdcard.c:312-314` · ROLLBACK · sev=low
- **WHAT:** A diagnostic `TRACE("R1b cmd %u response done: intr=0x%08x", ...)` explicitly
  labelled `/* Diagnostic (#119): ... */` sits in `_sdio_pollBusyCmd`. Its hypothesis
  (whether the controller raises CMD_DONE/TRANSFER_DONE for R1b) has already been answered
  by the surrounding code's existence (it does not, hence the poll path).
- **WHY:** Per the project rule "remove diagnostic-only code whose hypothesis was disproved
  before closing a step", this is a leftover probe. `TRACE` compiles out under
  `if (0)` so it is harmless at runtime, but it is presentation noise and an investigation
  artifact.
- **REC:** Delete the diagnostic `TRACE` at 312-314. **APPLY-SAFE** (it is a no-op `TRACE`;
  removal needs only a build+boot smoke). [Not applied — read-only phase.]

### COMMENT-2 · `storage/bcm2711-emmc/sdcard.c:28-30`, `200`, `277`, `406`, `429`, `1026` · COMMENT/ROLLBACK · sev=low
- **WHAT:** Markers use GitHub-issue style (`#119`, `#120`) and a bespoke
  `TODO(rpi4b-emmc)` tag rather than the project-mandated `TODO(TD-xx)` convention, and
  none of these has a matching entry in `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`
  (grep for `emmc`/`sdcard`/`PIO`/`SDMA`/`#119`/`#120` returns nothing).
- **WHY:** CLAUDE.md / rubric instruction 4 require transitional code to carry a
  `TODO(TD-xx):` marker with a matching debt-doc entry so it can be tracked and removed.
  The `#nnn` references are opaque outside the issue tracker and the SDMA-disable / PIO /
  R1b-poll / verbatim-fork shortcuts are exactly the kind of debt that doc is meant to hold.
- **REC:** Allocate `TD-xx` IDs for: (a) the verbatim SDHCI fork / de-dup (ARCH-1),
  (b) SDMA disabled → PIO fallback, (c) R1b IRQ-not-raised poll workaround, and add entries
  to `TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`; convert the in-code `#nnn` comments to
  `TODO(TD-xx):` while keeping the issue reference. **APPLY-SAFE** (comment + doc only).
  [Not applied — read-only phase.]

### STYLE-1 · `storage/bcm2711-emmc/bcm2711-sdio.c:189` · STYLE · sev=low
- **WHAT:** `sdio_platformConfigure()` prints an init banner to stdout via
  `printf("bcm2711-emmc: EMMC2 @0x%08x irq=%d refclk=%u Hz\n", ...)`.
- **WHY:** The driver's own logging idiom (`sdcard.c:35`) is `LOG_ERROR(...)` to *stderr*
  with the `LOG_TAG` prefix, and the zynq platform layer's `sdio_platformConfigure()`
  prints nothing at all on success. An unconditional stdout banner diverges from both and
  is upstream noise for a routine success path.
- **REFERENT:** `storage/zynq7000-sdcard/zynq7000-sdio.c:213-248` (`sdio_platformConfigure`
  is silent on success); `storage/bcm2711-emmc/sdcard.c:35` (`LOG_ERROR` → stderr idiom).
- **REC:** Drop the banner, or demote it behind the same `TRACE`/debug gate the core uses
  (and route via stderr if kept). **APPLY-SAFE** (logging only; build+boot smoke).
  [Not applied — read-only phase.]

### ARCH-2 · `storage/bcm2711-emmc/sdcard.c:457`, `1109`, `1142`, `166` · ARCH/efficiency · sev=low
- **WHAT:** Now that the data path is PIO (CPU FIFO loads/stores), the
  `MAP_UNCACHED | MAP_CONTIGUOUS` bounce buffer (`host->dmaBuffer`, allocated at line 166)
  plus the `memcpy(host->dmaBuffer, data, len)` / `memcpy(data, host->dmaBuffer, len)`
  round-trips (1109 / 1142) are pure overhead — there is no DMA engine reading/writing that
  buffer anymore. The PIO loop could read/write the caller's buffer directly.
- **WHY:** Reading FIFO words into an *uncached* staging buffer and then `memcpy`'ing them
  to the caller doubles the memory traffic and pays the uncached penalty for no coherence
  benefit (the producer is the CPU, not a device). Inherited from the zynq DMA design but
  no longer justified on this controller.
- **REFERENT:** The bounce-buffer + `memcpy` pattern comes from zynq's SDMA path
  (`storage/zynq7000-sdcard/sdcard.c` same structure); it is correct *there* because SDMA
  fills the buffer. Here the rationale is gone.
- **REC:** When de-duping (ARCH-1), have the BCM2711 PIO path read/write the caller buffer
  directly and drop the bounce + the uncached mapping for the PIO case. **NEEDS-HW**
  (changes the live data path; document only).

---

## Items explicitly considered and NOT flagged

- **Lost-wakeup guard (`sdcard.c:199-218`).** Correct. `INTR_STATUS` is read under
  `eventLock`; `sdhost_isr` (181-191) only zeroes `SIGNAL_ENABLE` and never touches
  `INTR_STATUS`, and the cond is signalled under the same lock path — no missed-wakeup
  window. Good fix for the real #119 hazard.
- **ISR-vs-PIO race.** During the PIO loop `SIGNAL_ENABLE` is still `AWAITABLE_INTRS`, so
  `sdhost_isr` can fire on an error concurrently — but the ISR only disables signalling and
  leaves `INTR_STATUS` intact, so the PIO loop still observes the error bit. Benign; not a
  finding.
- **`sdhost_reset` after `mutexUnlock` in the PIO error path (492-495).** Not a race:
  `cmdLock` serialises whole commands above `eventLock` (`sdcard_transferBlocks` 1169-1171),
  so no second command can interleave the reset.
- **PIO read coherence.** Non-issue: the FIFO drain is CPU loads in program order into the
  buffer, then a `memcpy` — no device-side producer, so no barrier needed between drain and
  copy. (`sdio_dataBarrier()` is correctly used around the command-issue MMIO at 395/420.)
- **`sdhost_defs.h` register constants/masks.** Verbatim-identical to zynq upstream;
  audited for RPi4 divergence, found none. Out of scope to re-review (see Method note).
- **Clock-divider / voltage init (`sdcard.c:1086-1093`).** Inherited zynq code, not in the
  RPi4 diff — not flagged. (The RPi4 contribution to clocking is the mailbox refclk query
  in `bcm2711-sdio.c`, reviewed below as part of the platform layer.)
- **`bcm2711-sdio.c` mailbox sequence.** Mirrors the proven diag-udp mailbox path; uncached
  MAP for both the mbox MMIO and the message buffer, `va2pa` + 16-byte-aligned request,
  full error-path `munmap` on every exit. No leak or width bug found in the changed code.

---

## Summary

Counts: **8 findings** — ARCH ×3 (1 high, 2 low), BUG ×2 (1 med, 1 low),
COMMENT ×2 (1 med, 1 low), ROLLBACK ×1 (low), STYLE ×1 (low). (COMMENT-2 is jointly
COMMENT/ROLLBACK.) By severity: 1 high, 2 med, 5 low. APPLY-SAFE: 3 (ROLLBACK-1 diagnostic
TRACE, COMMENT-2 TODO/doc hygiene, STYLE-1 init banner). NEEDS-HW / document-only: the
rest.

**Most important issue:** ARCH-1 — the SDHCI core (`sdcard.c` + the byte-identical
`sdhost_defs.h` + `sdstorage_*`) is a verbatim fork of `zynq7000-sdcard`. For upstream
presentation this duplication, not any single register or barrier, is the headline: it
must be de-duplicated into a shared SDHCI library parameterised by the platform layer
before this can go upstream. The runtime risk to flag for the user is BUG-1: only
single-block CMD17 is HW-validated; the multi-block PIO path (CMD18/CMD25) is untested and
should not be presented as working.
