# RPi4 bring-up — upstream-readiness code review (2026-06-06)

Goal: prepare all RPi4 bring-up changes for public presentation to the Phoenix-RTOS
maintainers. **We are NOT pushing upstream yet** — this is a readiness audit. Review
covers ONLY code created/changed by the RPi4 bring-up project (authors: Witold Bołt,
Codex, Claude, Gemini, Mistral), i.e. the net diff of each sibling's `master` vs its
upstream `origin/master`. Do NOT review unchanged upstream code.

## What we diff against (baseline = upstream `origin/master`, recorded 2026-06-06)

| repo | base (origin/master) | head (master) | net delta |
|---|---|---|---|
| libphoenix | `250383d` | `5674368` | 4 files, +51/-16 |
| phoenix-rtos-build | `556394d` | `30f6867` | 5 files, +95/-1 |
| phoenix-rtos-devices | `d511e0f` | `ebac8e4` | 34 files, +13189/-201 |
| phoenix-rtos-filesystems | `fc027f3` | `c7a1401` | 4 files, +77/-27 |
| phoenix-rtos-kernel | `57b30411` | `6cdf217e` | 46 files, +3807/-284 |
| phoenix-rtos-lwip | `fc152cb` | `a078a5c` | 11 files, +7337/-2 |
| phoenix-rtos-project | `537ef83` | `cb4b216` | 28 files, +2680 |
| phoenix-rtos-usb | `3ffbe3c` | `b3e97dc` | 8 files, +342/-20 |
| phoenix-rtos-utils | `34b00a1` | `34f87c4` | 7 files, +94/-8 |
| plo | `ce4eab9` | `ae05823` | 24 files, +2385/-35 |

(`behind=0` for every repo: `master` is a strict superset of `origin/master`, so the
two-dot tree diff is exactly the RPi4 net contribution. No `git fetch` was run — we diff
the base we are on, reproducibly.)

## Finding categories

- **BUG** — correctness, race, leak, UB, missing error handling, resource lifetime.
- **ROLLBACK** — temporary / diagnostic / dead / debug-only code that should be removed
  before presentation (and any such code *lacking* a `TODO(TD-xx)` marker).
- **COMMENT** — misleading, stale, excessive, or noise comments; TODO hygiene.
- **STYLE** — divergence from Phoenix conventions (clang-format, naming, header guards,
  license header, error-return idioms, `LOG_*`/`debug()`/`lib_printf` logging idiom).
- **ARCH** — does it differently from how other arches / other drivers of the same class
  do it (cite the concrete Phoenix referent file/function).

Every finding: `file:line` · category · severity (high/med/low) · what · why · recommendation.
STYLE/ARCH findings **must cite the concrete Phoenix referent**. Review the **changed
hunks** (full file for context only) — never flag pre-existing upstream code.

## Apply policy (overnight, autonomous — conservative)

Per `feedback_unattended_scoping`: additive, cannot-silently-regress. The user is asleep
and left the SD card in the host so netboot smoke tests run without them.

- **APPLY overnight** only verifiably-safe edits: dead/diagnostic-code removal, comment
  fixes, formatting/style alignment, obvious local cleanups. Gate every batch on a
  `--scope core` rebuild (green) + a **netboot boot-to-psh smoke** (regression tripwire).
- **DOCUMENT, do not blind-apply** semantic / control-flow / driver-logic / BUG fixes —
  they cannot be HW-validated overnight. Record as recommendation + patch sketch for the
  user to approve. (We are not pushing upstream, so there is no urgency to justify risk.)

## Reviewer instructions (read fully before reviewing)

You are a senior Phoenix-RTOS reviewer auditing one area of the RPi4 bring-up for
upstream readiness. Method:

1. Get your area's net diff (runs without prompts):
   `git -C /home/houp/phoenix-rpi/sources/<repo> diff origin/master master -- <files>`
   Read the diff AND the current file(s) for context. **Review only the changed hunks**
   — never flag pre-existing upstream code.
2. To establish a Phoenix convention referent, read sibling code under
   `/home/houp/phoenix-rpi/sources/` (other arches, other drivers of the same class).
   Allowed read tools: Read, Grep, Glob, Bash (`git -C ... diff/show/log`, `grep`, `rg`).
   **Do NOT edit any source file** — this is a read-only review phase.
3. Hunt for findings in the 5 categories above. For BUG, look hard at: MMIO access
   width + ordering (missing `dsb`/barriers on device/DMA memory), DMA-buffer cache
   coherence, resource leaks / error-path cleanup, integer width + truncation, off-by-one,
   wrong register constants/masks, race/locking, lifetime of `oid`/handles.
4. Reconcile `TODO(TD-xx)` markers vs the diff: don't recommend naively deleting code
   with a valid marker, but DO surface (a) temporary/diagnostic code with NO marker, and
   (b) any TD item the diff shows is already resolved. (`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.)

Per-finding format (one entry each):
`path:line` · **CATEGORY** · sev=high|med|low · WHAT · WHY · REC: <concrete; sketch the
change> · **APPLY-SAFE** (dead code / comment / pure format — safe to apply with only a
build+boot smoke) or **NEEDS-HW** (driver logic / control flow / semantics — cannot be
validated without hardware; document only).

Every **STYLE/ARCH** finding MUST cite the concrete Phoenix referent (file/function that
shows the convention). Unfalsifiable "differs from Phoenix style" with no referent is not
acceptable — drop it or find the referent.

Output: write `<AREA>.md` in this directory with a header (area, files, repo base→head
SHA), findings ordered by severity, and a 3–5 line summary (counts by category + the
single most important issue). Then return ONLY a concise summary: area, counts by
category/severity, and the top 3 issues (one line each).

## Phase 1 review areas (one findings file each)

| AREA file | repo | files |
|---|---|---|
| `devices-sdcard` | devices | storage/bcm2711-emmc/{sdcard.c,sdcard.h,sdhost_defs.h,bcm2711-sdio.c,bcm2711-sdio.h} |
| `devices-sdstorage` | devices | storage/bcm2711-emmc/{sdstorage_dev.c,sdstorage_dev.h,sdstorage_srv.c,Makefile} |
| `devices-xhci` | devices | usb/xhci/xhci.c |
| `devices-pcie-bcm2711` | devices | usb/xhci/{bcm2711-pcie.c,bcm2711-pcie.h,phy-aarch64a72-generic.c,Makefile} |
| `devices-pcie-server` | devices | pcie/server/{pcie.c,Makefile} |
| `devices-tty-pl011` | devices | tty/pl011-tty/{pl011-tty.c,Makefile} |
| `devices-tty-usbhid` | devices | tty/usbkbd/*, tty/usbmouse/* |
| `devices-misc` | devices | libklog/libklog.c, tty/libtty/libtty.c, sensors/rpi4-thermal/*, misc/rpi4-hwrng/*, _targets/Makefile.aarch64a*, README.md |
| `lwip-genet` | lwip | drivers/{bcm-genet.c,bcm-genet-regs.h,ephy.c,ephy.h,netif-driver.c}, include/netif-driver.h, _targets/Makefile.aarch64a72-generic |
| `lwip-port-diagudp` | lwip | port/{diag-udp.c,main.c,mbox.c}, .gitignore |
| `kernel-init-asm` | kernel | hal/aarch64/{_init.S,_exceptions.S,_memset.S,Makefile} |
| `kernel-hal-c` | kernel | hal/aarch64/*.c+*.h (pmap,spinlock,cpu,hal,interrupts_gicv2,pl011,dtb,exceptions,aarch64.h,arch/*,gtimer*), hal/aarch64/generic/*, include/arch/aarch64/generic/*, hal/{cpu.h,timer.h} |
| `kernel-core` | kernel | main.c, proc/{name.c,threads.c,msg.c}, syscalls.c, syspage.c, log/log.c, vm/{map.c,vm.c}, posix/posix.c, usrv.c, lib/lib.h, README.md |
| `plo` | plo | all changed files (see `git diff --stat`) |
| `usb-framework` | usb | usb/{hub.c,usb.c,mem.c,dev.c,dev.h,hcd.c,usbhost.h}, README.md |
| `project-config` | project | all changed files (board_config, build.project, *.plo.yaml, armstub .S, reloc .S/.lds, busybox_config, lwipopts.h, config.txt, scripts) |
| `small-repos` | libphoenix, build, filesystems, utils | all changed files in each |

Synthesis + cross-cutting themes: `_SYNTHESIS.md`.
