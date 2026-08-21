> **2026-08-21 update:** the 5 still-real items (B2/B5/B7b/B8/B14) have been
> re-verified against the current tree (line numbers moved) and turned into a
> turnkey attended-pass work order with exact diffs + per-item HW tests:
> [`2026-08-21-b-items-attended-work-order.md`](2026-08-21-b-items-attended-work-order.md).
> Use that for the apply pass; the table below remains the relevance snapshot.

# B1–B14 re-verification vs current code — 2026-08-10

The 2026-06-06 upstream-readiness review (`_SYNTHESIS.md`) listed 14 "NEEDS-HW" bugs
B1–B14. That list is now 64 days old; substantial driver/kernel work has landed since.
Every item was re-checked against **current** sibling-repo code (read-only) so the
public-release / upstreaming picture is accurate. Line numbers below are current.

Reachability baseline: `PCI_EXPRESS_BCM2711_INDEXED_CFG=y` is set only in the
`aarch64a72-generic-rpi4b` project; the standalone `pcie/server` daemon is built only for
`aarch64a53-zynqmp` (which does not define that macro). The active/default Pi build is a72.

| ID | Status | Current location | Evidence |
|----|--------|------------------|----------|
| B1 | **FIXED (live)** + INERT (dead copy) | `phoenix-rtos-devices/usb/xhci/bcm2711-pcie.c:563`; dead copy `.../pcie/server/pcie.c:464` | Live copy (compiled into libusbxhci for a72) seeds `shift=0` with correct `log2(size)-15`. The `pcie/server` copy still has `shift=20` but sits under `#ifdef PCI_EXPRESS_BCM2711_INDEXED_CFG`, never compiled (pcie-server builds only on a53-zynqmp where the macro is undefined). |
| B2 | **STILL-REAL** | `phoenix-rtos-usb/usb/xhci/xhci.c:1902` | `xhci->inputCtx = usb_allocAligned(...)` re-allocs the *shared* input context on every `xhci_allocSlotSpace` (per behind-hub device) with no free/`if(NULL)` guard → leak; free only in teardown (646). Fix small; **verify needs HW** (hub + multiple devices). |
| B3 | STILL-REAL but by-design guard | `phoenix-rtos-usb/usb/usb.c:192-196` | On malformed ring (`finished->prev==NULL`) resets the pending list; now non-silent (fprintf) + commented as a deliberate crash-guard for the un-root-caused #121 corruption. Not a delete candidate. |
| B4 | **FIXED** | `phoenix-rtos-kernel/main.c:161` | `#if (NUM_CPUS != 1) && defined(__aarch64__)` gates the whole SMP block; the only aarch64-only externs (`hal_smpPrimaryReady`/`hal_smpFirstIntervalUs`) are inside it; no other ungated `hal_smp*` in main.c. Was the headline cross-target link-break; now closed. |
| B5 | **STILL-REAL** | `phoenix-rtos-kernel/hal/aarch64/generic/console.c:88→32` | `hal_consolePrint` → `_hal_consoleEarlyPrint` → `_hal_consoleEarlyPutch` writes the hardcoded `0xffffffffffe00000` alias; only `hal_consolePutch` (klog mirror) uses the DTB-discovered `console_common.uart`. **The alias is load-bearing** — `hal_consolePrint` must work pre-`_hal_consoleInit` (comment lines 66-68), so a naive redirect drops early boot output. Fix **medium** (needs early-vs-discovered conditionalization); **weak verify** on rpi4 (alias == discovered base, so a boot can't distinguish the paths). |
| B6 | **FIXED** | (removed) | Kernel-wide grep for `0xffffffffffe00000` returns only `console.c` (arch HAL); the raw-VA UART probes are gone from `usrv.c`/`log/log.c` (removed in kernel 08a09d28). |
| B7a | **FIXED** | `phoenix-rtos-lwip/drivers/bcm-genet.c:128,657-667` | RX pool is one contiguous `dmammap` of `GENET_RX_POOL_SLOTS` (=`TOTAL_DESC+256`=512) UNIQUE buffers with a free-list; the 256 BDs are armed with unique buffers (matches 6b01087). (The stale file-top "16 unique buffers" comment was corrected 2026-08-10.) |
| B7b | **STILL-REAL / effect-UNCERTAIN** | `phoenix-rtos-lwip/drivers/bcm-genet.c:212-215 (TX doorbell), payload edges` | `genet_write` is a bare `volatile` store, no barrier; explicit `dsb` exists only inside the disabled `#if GENET_RX_CACHEABLE` block. TX: Normal-NC payload copy is not fenced before the Device-MMIO `PROD_INDEX` doorbell. RX: the Normal-NC payload read after DMA-done is the unbarriered edge. Empirically netboot NFS-root is reliable (buffers are Normal-NC, so ordering is looser-but-adequate on this SoC in practice), so this is a latent/defensive correctness item. Fix small (add `dsb` before doorbell / after RX-done); **verify needs HW stress** to prove a corruption exists. |
| B8 | **STILL-REAL** | `phoenix-rtos-devices/tty/libtty/libtty_disc.c:204` | `libtty_putchar_helper` does `*wake_reader = 0` at entry every call, so pl011-tty's unlocked batch path (pl011-tty.c:1187-1194, `libtty_putchar_unlocked` in a loop then one wake check) collapses a multi-char burst to the last char's wake decision → a completed line followed by more chars in the same burst can lose its reader wakeup. (Locked path is saved by per-char `condSignal`.) Fix small but **shared libtty** (all arches); verify needs HW, hard to repro. |
| B9 | **FIXED** | `phoenix-rtos-usb/usbkbd/usbkbd.c:197`, `usbmouse/usbmouse.c:167` | Proper `usbkbd_free`/`usbmouse_free` destructors free fifo+cond+lock+dev; `_devAlloc` error paths do incremental teardown. No bare `free(dev)` leak remains. |
| B10 | **FIXED 2026-08-10** (non-built a53 target) | `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/board_config.h:16-17` | Was `PLO_GICD/GICC_BASE = 0x40041000/0x40042000` vs the correct Pi4 GIC-400 `0xff841000/0xff842000`. The target's UART base was already the BCM2711 `0xfe201000`, confirming it's a genuine Pi4b variant → GIC bases corrected to match a72. Inert w.r.t. the default a72 build. Kept-status of the a53 target is still an owner decision. |
| B11 | **FIXED/GONE** | `phoenix-rtos-filesystems/dummyfs/srv.c:189-266` | The two `write(1,"",0)` sync loops are gone; parent-readiness is now a `fork()` + `kill(getppid(),SIGUSR1)` handshake. A readiness sync IS present, so the "silently removed" concern doesn't stand. |
| B12 | Divergence, not a defect | `phoenix-rtos-devices/tty/libtty/libtty.c:507` | `tty->pgrp = *pid;` (was `getpgid(*pid)`), from port commit 3ee4702; the more POSIX-correct form (TIOCSPGRP's arg is already a pgid). An upstream-review discussion item, not a bug to fix. |
| B13 | STILL-PRESENT, now justified | `phoenix-rtos-usb/usb/mem.c:197` | Unconditional `MAP_...|MAP_CONTIGUOUS`; now carries a rationale comment (matches kernel `dmammap`; ports ignore/honor without cost). Residual `-ENOMEM`-under-fragmentation risk, documented. Not an actionable defect. |
| B14 | **STILL-REAL** | `phoenix-rtos-devices/usb/xhci/xhci.c:3191/3195/3199/3203` | The four `C_*` clear cases write `(portsc & ~PED) | <one C-bit>` without masking sibling RW1C change bits → any other change bit reading 1 gets re-written as 1 (over-cleared). The `ENABLE`/`POWER` cases (3207/3211) correctly `& ~(... | RW1C)`. Fix small (mask `~RW1C` in the four writes); **verify needs HW** (simultaneous-change-bit race, low value). |

## Summary

- **FIXED since the review:** B1(live), B4, B6, B7a, B9, B11 — and B10 landed 2026-08-10.
- **STILL-REAL defects, all HW/load-verification-gated:** B2 (leak), B5 (console early-path), B7b (missing DMA barriers), B8 (tty wake race), B14 (xHCI RW1C over-clear). Each is a small, self-contained edit, but none is observable/confirmable on an unattended netboot smoke (a passing boot-to-psh would not prove the fix), so they are **left for an attended / HW-validation session** with the precise current locations above.
- **Not actionable as fixes:** B3 (deliberate crash-guard), B12 (POSIX-correct divergence), B13 (justified). Keep as upstream-discussion notes.

## Landed this pass (2026-08-10)
- **B10** — a53 `board_config.h` GIC-400 bases corrected (inert w.r.t. a72; can't regress it).
- **B7a comment** — `bcm-genet.c` file-header RX description corrected (was the old 16-buffer design).
- This re-verification doc + a pointer at the top of `_SYNTHESIS.md`.

The honest takeaway: the review's real remaining bugs are few and all HW-gated; the biggest
prior headline (B4 link-break) and the RX-aliasing corruption (B7a) are already closed.
