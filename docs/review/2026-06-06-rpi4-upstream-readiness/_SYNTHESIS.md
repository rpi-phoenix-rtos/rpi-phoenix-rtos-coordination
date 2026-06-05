# RPi4 bring-up — upstream-readiness review: SYNTHESIS

17 parallel read-only reviews, one per area (see `README.md` for the map + baselines).
~150 findings. This file extracts the **cross-cutting themes** (the highest-value signal
for the maintainers) and a triaged action plan: **APPLY-SAFE** (mechanical, done overnight
behind a build + netboot smoke) vs **NEEDS-HW / DECISION** (documented for the user, not
blind-applied — they can't be HW-validated without the SD card / a real regression test).

## Headline verdict

The RPi4 functionality is real and largely correct — the reviewers hand-verified the
load-bearing logic (xHCI coherence/barriers, PCIe window arithmetic incl. the FIX-12
encoding, DTB parser bounds, the #119 SDIO lost-wakeup guard, the #120 mount-by-id fix).
**The upstream blockers are not correctness — they are presentation:** (1) wholesale
file-level *duplication* of existing Phoenix drivers, (2) pervasive *un-marked diagnostic
scaffolding* left in shipping files, and (3) *stale/inverted comments* that actively
mislead. A maintainer reading this today would bounce it on those three, not on bugs.

## Cross-cutting themes

### T1 — Wholesale duplication of existing Phoenix code (the #1 blocker)
- **SD/SDIO**: `storage/bcm2711-emmc/` is ~95% a verbatim fork of `storage/zynq7000-sdcard/`
  — `sdhost_defs.h` byte-identical; `sdcard.c`/`sdstorage_*` forked. Self-acknowledged in
  the Makefile but not as tracked debt. → factor a shared SDHCI core lib; per-SoC seam is
  the existing `sdio_platformInfo_t` + `*-sdio.c`.
- **PCIe**: `usb/xhci/bcm2711-pcie.c` is a near-verbatim fork of `pcie/server/pcie.c`. The
  two have **drifted** — and the drift hides a real bug (see B1): the FIX-12 BAR2-size fix
  exists only in the xhci copy. → retire one copy or hoist a shared lib.
- **VideoCore property mailbox**: the same round-trip is copy-pasted in **5** places —
  `diag-udp.c` ×3, `rpi4-thermal.c` (verbatim from diag-udp), `bcm2711-sdio.c`, plo
  `hal/aarch64/generic/video.c`. → one shared `mbox property` helper; any fix today must
  be made 5×.

### T2 — Un-marked diagnostic / dead scaffolding left in shipping files (pervasive ROLLBACK)
The project's debt convention is `TODO(TD-xx)` linked to `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`,
but most diagnostic code carries no marker (or ad-hoc `#119`/`#120`/`TD-USB`/`TD-15` that
don't reconcile). Inventory:
- **`lwip/port/diag-udp.c` (5534 lines)** — the headline: an unauthenticated UDP control
  surface (reboot/halt, live SDIO writes, physmem dumps). Not a build-flag candidate — a
  security non-starter; **remove**. `port/Makefile` is `wildcard *.c` so deleting the file
  + reverting the `main.c:153-160` hook + reverting `mbox.c` in-place diag edits + the
  `.gitignore` lines is the whole job.
- ungated `debug()`+`snprintf` register dumps: `xhci.c`, `bcm2711-pcie.c` (~12 sites),
  `pcie/server/pcie.c` (~9 sites + a 30×100ms warm-up loop), `usb/hcd.c` (6 sites).
  **Nuance:** several share a register *read* with a functional write — strip only the
  print, keep the write.
- **kernel raw-VA UART probes** in *shared* code: `usrv.c` + `log/log.c` write hardcoded VA
  `0xffffffffffe00000` ungated → fault/corrupt on every non-aarch64 boot. (BUG-grade; B7.)
- **dummyfs trace instrumentation** (`filesystems/dummyfs/srv.c`): `traceKind`/`lookupTrace`/
  `dummyfs_trace()` + 4 `debug()` sites, unconditional on *all* platforms incl. ia32, purpose
  already served.
- dead code: xhci `xhci_runStateSelftest`, the `USB_HCD_PCIE_DRIVE_ONLY` block in
  bcm2711-pcie.c (env var set nowhere), usb-framework `hub.c` `scanAll`/`hub_notifyScan`
  (kept alive only by `(void)`), `usbkbd_diag*` counters, plo `_init.S` halt-and-print
  vector table + boot markers, dead `tag_getclkrate`/`tag_clkid_arm`.

### T3 — Stale / inverted comments (several HIGH; actively misleading)
- `kernel _init.S:569-585`: "Caches stay OFF … M-only" sitting above code that
  unconditionally enables `SCTLR.M|C|I`.
- `kernel _init.S` (4 sites) + cross-refs: comments claim `_hal_syspageCopied` is Normal-NC
  (TD-04 override) but that override was deleted; the page is now cacheable. **This is the
  TD-13 lead** — the comments are inverted on the live syspage copy path.
- `kernel generic/config.h`: "NUM_CPUS stays at 1" above `#define NUM_CPUS 4U` (+ unmergeable
  WIP commit SHAs).
- `bcm2711-pcie.c`: disproven-hypothesis lab-journal ("chasing all day", rc=-19/-110 sweep
  table, "10-cycle experiment mode B", nGnRnE "NOT the fix").
- `pl011-tty.c`: contradictory cache-state rationale (caches operational vs D-cache disabled).

### T4 — TODO/TD marker hygiene + reconciliation
- Resolved-but-still-referenced TD items the diff proves done: **TD-04** (NC syspage override
  removed), **TD-Eth-DHCP** (genet does `dhcp_start`), **TD-16-1** (clkrate tags), the
  usbkbd path (#127 removal condition met).
- Bogus/ad-hoc markers: `TD-USB` (not a real ID), `TD-15 Stage 4 phase 2` misattributed to
  PCIe (TD-15 = VideoCore/4GiB), and many `#119`/`#120`/`TODO(#nnn)` where upstream uses none.
- Genuinely-temporary code lacking *any* marker: the dummyfs trace, the kernel raw-VA probes,
  several driver `debug()` blocks, the genet RX-aliasing limitation.

### T5 — License / header / style drift
- License headers: `lwip/drivers/bcm-genet.c` uses `SPDX` vs the repo-wide `%LICENSE%` block.
- Stale copyright: `bcm2711-pcie.c` ("Copyright 2025 / Dariusz Sabala", inconsistent with the
  .h + siblings), dummyfs `2023`.
- `extern void debug(...)` redeclared inline ~9× in pcie files vs the `<sys/debug.h>` idiom
  (referent: grspw2.c, imxrt-multi.c).
- Indentation: 8-space blocks inserted into tab-indented files (kernel `_init.S`, pcie munmap).
- README "fork warning / AI-generated" banners in devices + project — correct *today*, remove
  only at actual push time.

## Confirmed / suspected BUGs (mostly NEEDS-HW → documented for user)

| id | sev | where | bug | fix posture |
|---|---|---|---|---|
| B1 | high | `pcie/server/pcie.c:452` `bcm2711EncodeBar2Size` | `shift=20` seed → 4GiB inbound BAR2 encodes to field 37, masked to 5 = 1MiB window; VL805 DMA target outside it. The sibling `usb/xhci/bcm2711-pcie.c:594` already fixed this (FIX-12); copies drifted. | **NEEDS-HW** but unambiguous (in-tree fixed referent). Highest-value real bug. |
| B2 | high | `usb/xhci/xhci.c:2041` `xhci_allocSlotSpace` | `xhci->inputCtx` re-alloc'd per behind-hub device with no free → leak + contradicts "alloc once" contract. | NEEDS-HW |
| B3 | high | `usb/usb.c` finished-list reset | on malformed ring, silently drops/leaks entire pending URB-completion list (all targets). | NEEDS-HW (guards a live crash — gate/mark, don't delete) |
| B4 | high | kernel `main.c` `#if NUM_CPUS != 1` SMP blocks | reference `hal_smp*` externs defined only under aarch64 → **link break on gr740/zynq7000/gr712rc** (other SMP targets) which build shared main.c. | NEEDS-HW (regate to target macro) |
| B5 | high | kernel `generic/console.c` `hal_consolePrint` | writes every byte through hardcoded early alias `0xffffffffffe00000`, ignores the dtb-discovered/`hal_pl011Init` base → new DTB console discovery is dead for the primary print path. | NEEDS-HW |
| B6 | high | `usrv.c` + `log/log.c` raw-VA UART probes | ungated writes to hardcoded aarch64 VA in arch-shared init → fault on non-aarch64. | **APPLY-SAFE** (pure diagnostic delete) |
| B7 | med | `lwip/drivers/bcm-genet.c` | 16 RX buffers aliased across 256 BDs + no DMA-ordering barriers (TX+RX); burst >16 frames or stale-buffer fetch → corruption. | NEEDS-HW |
| B8 | med | `devices/tty/pl011-tty.c:1042/930` | `wake_reader` reset each `libtty_putchar` → multi-byte burst loses reader wakeup (console input stall). | NEEDS-HW |
| B9 | med | `usbkbd.c:775` / `usbmouse.c:587` | insertion error path `free(dev)` leaks fifo+lock+cond created by `_devAlloc`. | NEEDS-HW |
| B10 | med | `project a53-rpi4b/board_config.h` | `PLO_GICD/GICC_BASE` = `0x40041000/0x40042000`, not the Pi4 GIC-400 `0xff841000/0xff842000` → PLO can't init IRQs on HW. | APPLY-SAFE (copy a72 values) — but a53-rpi4b is secondary; verify it's a real target |
| B11 | med | `filesystems/dummyfs/srv.c` | the two `write(1,"",0)` parent-readiness sync loops silently removed, no rationale (cross-platform). | NEEDS-HW |
| B12 | med | `devices/tty/libtty.c:507` TIOCSPGRP | `getpgid(*pid)`→`*pid` in *shared* libtty changes job-control semantics for all arches. | NEEDS-HW |
| B13 | med | `usb/mem.c` | unconditional `MAP_CONTIGUOUS` for all USB targets → can `-ENOMEM` under fragmentation, for "hygiene" with no functional gain. | NEEDS-HW (revert or justify) |
| B14 | low | `usb/xhci/xhci.c:3338` clearPortFeature | RW1C over-clear of sibling `C_*` change bits (unlike ENABLE/POWER cases). | NEEDS-HW |

## APPLY-SAFE worklist (done overnight, batched; each batch → `--scope core` build + netboot boot-to-psh smoke)

Mechanical, regression-low. Group by repo to minimize rebuild churn. Order chosen so a
single netboot smoke after each repo batch covers it.

1. **Comments (all repos, zero functional risk):** fix the inverted/stale comments in T3 —
   `_init.S` cache + syspage-NC comments, `config.h` NUM_CPUS comment + strip WIP SHAs,
   bcm2711-pcie lab-journal → 1-line invariants, pl011 cache rationale. Reconcile T4 markers
   (mark resolved TDs, fix `TD-USB`/`TD-15` misattribution, convert `#nnn` to `TODO:` or a
   real TD entry).
2. **Pure diagnostic deletes (no shared register read):** B6 kernel raw-VA probes
   (usrv.c/log.c), dummyfs trace block, `usbkbd_diag*`, plo boot markers + dead
   `tag_getclkrate`, xhci `xhci_runStateSelftest`, `USB_HCD_PCIE_DRIVE_ONLY` block, usb
   `hub.c` `scanAll`/`hub_notifyScan` dead path, `hcd.c` 6 debug prints + include, the
   pl011 unmarked `fprintf`.
3. **Print-only strips where a register read is shared:** xhci/bcm2711-pcie/pcie-server
   register-dump blocks — delete the `snprintf`+`debug()` lines, KEEP the functional
   read+write. (More careful; do as its own batch with a diff re-read.)
4. **License/style:** genet SPDX→%LICENSE%, fix stale copyright headers, replace inline
   `extern void debug` with `#include <sys/debug.h>`, fix 8-space→tab indentation.
5. **Dead config:** remove the dead `_targets/*.plo.yaml` (no consumer) + the Pi-incompatible
   a72 preinit memory map; B10 a53 GIC base (if a53-rpi4b is a kept target).

NOTE the diag-udp.c removal (T2 headline) is APPLY-SAFE in effort but its blast radius
(main.c hook + mbox.c in-place edits + .gitignore) makes it a **dedicated batch** with its
own build+smoke — do it last among the safe set so a smoke failure is unambiguous.

## NEEDS-HW / DECISION worklist (documented for the user — do NOT blind-apply overnight)

- All B1–B14 marked NEEDS-HW above (driver logic / control flow / cross-arch semantics).
- **T1 de-duplication** (shared SDHCI lib, single PCIe impl, shared mbox helper): large
  refactors that must be HW-revalidated; the single biggest upstream win but also the
  riskiest — propose as a planned series, get the user's ack on approach.
- The `mbox.c` `mbox_tryfetch` corruption guard (masks unconfirmed #121/#129) — keep until
  root-caused; removing needs a soak.
- a53 QEMU/rpi4 targets: decide keep-vs-drop before presentation.

## Per-area findings index
`devices-sdcard.md`, `devices-sdstorage.md`, `devices-xhci.md`, `devices-pcie-bcm2711.md`,
`devices-pcie-server.md`, `devices-tty-pl011.md`, `devices-tty-usbhid.md`, `devices-misc.md`,
`lwip-genet.md`, `lwip-port-diagudp.md`, `kernel-init-asm.md`, `kernel-hal-c.md`,
`kernel-core.md`, `plo.md`, `usb-framework.md`, `project-config.md`, `small-repos.md`.
