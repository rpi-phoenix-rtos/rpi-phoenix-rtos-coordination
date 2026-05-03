# Current Implementation Step

## Step: Cleanup after first real Pi 4 psh prompt (TD-14)

**Status**: ACTIVE — Pi 4 reaches the UART shell prompt. Current work is
TD-16 cache bring-up: remove unsafe bootstrap aliases first, then retry a
single early MMU/cache transition.

**Date**: 2026-05-02 night

**Manifest**: `manifests/2026-05-02-td14-uart-shell-prompt.md`

## What just changed

Sibling commits:
- kernel `60703368` — relative `proc_portLookup("devfs")` fix, direct
  stored OID for the `devfs` namespace, bounded TD-14 `proc_send("devfs")`
  timing probe.
- devices `63f1d438` — PL011 minimal stat/attr support plus direct
  `/dev/console` alias.
- devices `3ee4702` — `TIOCSPGRP` now stores the requested foreground
  process-group ID directly.
- libphoenix `3c76bba` — temporary `/dev/console` open trace plus a narrow
  fast path that skips the second `resolve_path()` walk for the direct console
  alias.
- utils `da2f541` — psh early probes use `debug()` and bracket tty open,
  `isatty`, `tcsetpgrp`, and first `readcmd`.

Validation:
- QEMU Pi 4 smoke reaches `(psh)% help`.
- Real Pi image SHA256:
  `d219efa27dd617ea171465f601742427ca1c96f3d505fb3979a1c7a27d0c520e`.
- Real Pi log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260502-220314-netboot-td14-readcmd-long.log`.

## New known boundary

The first real Pi 4 UART prompt is reached:

```text
psh: tty ready
psh: tcsetpgrp
psh: tcsetpgrp done
psh: readcmd
(psh)%
```

## Next action

Run a cleanup-focused iteration:
- Strip or gate the highest-volume TD-04/TD-14 boot probes that are no longer
  needed for the prompt boundary.
- Keep the functional fixes: `devfs` direct OID, PL011 stat/attr support,
  `TIOCSPGRP` semantics, and the temporary direct console alias/fast path.
- Rebuild and run QEMU smoke.
- Run real Pi netboot long enough to verify `(psh)%`, then run an interactive
  UART smoke if the current helper supports sending commands.

Then run:

```bash
./scripts/rebuild-rpi4b-fast.sh
./scripts/qemu-shell-smoke.sh rpi4b
./scripts/test-cycle-netboot.sh --label td14-clean-prompt --capture-secs 240 --dhcp-wait-secs 90
python3 scripts/summarize-rpi4b-uart-log.py artifacts/rpi4b-uart/<latest>.log
```

## 2026-05-03 update — TD-16-1 landed; cache-enable attempted, reverted

After landing TD-16-1 measurement probes (kernel `843e6c61` and plo
`61927ba`), we now have hard data on the Pi 4 slowdown:

- `td16: arm_freq Hz = 0x59682f00` = **1.5 GHz** — firmware confirms
  the ARM core is at full turbo. CPU is NOT throttled.
- `td16:cf=0337f980 dt=0000000000872d51` →
  - cntfrq = **54 MHz** (correct for BCM2711),
  - dt = **8,858,961 ticks for 1M nops** (~62× slower than physics
    says it should be at 1.5 GHz with caches enabled).

**The slowdown is caches being disabled in the kernel** — confirmed.
Not CPU throttling, not timer rate, not power management.

Attempts to enable I-cache + D-cache in `_init.S` were investigated:

1. I+D enable right after `_core_0_virtual:` → **recursive
   exception loop** on real Pi 4 (`E E E E ...`).
2. I+D enable just before `b main` → similar fault inside
   `syspage_init`'s first `hal_syspageAddr()` call.
3. I-cache only just before `b main` → no fault, per-nop loop is
   117× faster, but **overall boot doesn't progress meaningfully
   faster** (480 s capture stalls at the same `kllmnP` marker).
4. D-cache later in `_hal_init` (after syspage_init completes) →
   hung QEMU smoke inside `bl hal_cpuInvalDataCacheAll`. Not
   tested on real Pi 4.

All cache-enable code has been reverted to baseline. The TD-15
phase 1 + TD-16-1 probes remain in place as documented diagnostic
infrastructure. The Pi 4 boot reaches `(psh)%` reliably in ~420 s
capture per the 2026-05-02 manifest.

The detailed findings + hypothesis space are in
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` under TD-16-cache-enable.

## 2026-05-03 follow-up — I-cache-only late placements rejected; D-cache maintenance helper fixed

Two additional I-cache-only placements were tested after reviewing the
current logs and cache-enable history:

- End of `_hal_init_c()`: QEMU reached `(psh)% help`; real Pi showed
  the synthetic TD-16 speedup (`td16b:dt=0x126ee`) but then hung after
  marker `h`, before `_usrv_init()` returned. Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-202432-netboot-td16-late-icache-long.log`.
- `_hal_start()`: QEMU reached `(psh)% help`; real Pi progressed through
  VM/proc/syscall init but then hung immediately after
  `main_initthr: enter`, before `_hal_start()` returned. Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-203723-netboot-td16-icache-hal-start.log`.

Conclusion: I-cache-only is not a safe functional fix. It can make a
nop-loop fast, but on real Pi 4 it exposes another cache/coherency issue
as soon as normal kernel initialization continues. The live boot path has
no SCTLR cache enable.

The useful source change kept from this investigation is kernel
`1a4eb297`, the `hal_cpuInvalDataCacheAll()` rewrite: the old set/way
loop selected only L1, missed the required `isb` after `csselr_el1`, used
invalidate-only instead of clean+invalidate, and had no final barriers.
The new helper is CLIDR-driven and uses `dc cisw` across every
data/unified cache level. That is a prerequisite for the next D-cache
attempt, not a complete cache enable fix.

Next action for TD-16: remove the unsafe mixed cacheable/Normal-NC aliases
from the bootstrap mappings before trying SCTLR.C again. In particular,
do not enable D-cache while `_hal_syspageCopied` and `PMAP_COMMON_STACK`
are reachable through both TTBR0 cacheable identity entries and TTBR1 NC
entries.

External OS comparison changes the cache strategy:

- Linux arm64 and FreeBSD arm64 both enable `SCTLR_EL1.M`, `C`, and `I` as
  part of the early MMU transition after MAIR/TCR/TTBR setup and page-table
  cache/TLB maintenance.
- Circle's Pi-oriented bare-metal path likewise treats cache enable as early
  memory-management infrastructure tied to a consistent map, not as a late
  C-level performance optimization.
- Therefore the Phoenix Pi 4 fix should not be another late I-cache-only
  placement. It should make the early bootstrap maps alias-safe, restore
  correct page-table cache maintenance, and then enable MMU + I-cache +
  D-cache together in a Linux/FreeBSD-shaped transition.
- Re-check upstream references before implementation:
  `arch/arm64/kernel/head.S` and `arch/arm64/mm/proc.S` in Linux, FreeBSD
  `sys/arm64/arm64/locore.S`, and Circle `startup64.S` / memory code.

## 2026-05-03 TD-16 step 1 — TTBR0 RAM identity blocks made Normal-NC

Implemented the first alias-reduction step toward the Linux/FreeBSD cache
plan: the temporary TTBR0 level-1 RAM identity block descriptors now use
Normal Non-Cacheable attributes (`NC_BLOCK_ATTRS`) instead of Normal
cacheable. This keeps the low identity aliases consistent with the existing
TD-04 NC mappings for `_hal_syspageCopied` and `PMAP_COMMON_STACK` while
the live path still has `SCTLR.C` disabled.

Validation:
- `./scripts/rebuild-rpi4b-fast.sh` completed and exported image SHA256
  `f6e77484512867c68f880923687342ec510469b61b59d09d4fb22be935a9795c`.
- `./scripts/qemu-shell-smoke.sh rpi4b` reached `(psh)% help`.
- `./scripts/qemu-shell-smoke.sh generic` reached `(psh)% help`.
- `./scripts/test-cycle-netboot.sh --label td16-ttbr0-nc-blocks
  --capture-secs 600 --dhcp-wait-secs 90` reached `(psh)%` on real Pi 4.
  Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-213203-netboot-td16-ttbr0-nc-blocks.log`.

Warnings observed:
- Build/export: no compiler, linker, DTB, packaging, or image verification
  warnings; helper reported `Verification: OK`.
- Real Pi firmware: expected netboot-path messages only (`sdcard` open
  failures before network fallback, missing `cmdline.txt`, HDMI1 EDID/DSI
  messages while HDMI0 is active). No new Phoenix runtime fault.

Next TD-16 step: shrink the TTBR0 identity map further or disable TTBR0
earlier after the higher-half branch so the same PA is not reachable through
low and high aliases when `SCTLR.C` is eventually enabled. Do not enable
caches until that alias boundary is handled.

## 2026-05-03 TD-16 step 2 — TTBR0 dropped after syspage copy

Implemented the next alias-boundary cleanup in kernel commit `d52f6c3a`:
after the boot code copies the syspage and runs the post-copy
`_clean_inval_dcache_range`, it immediately switches `TTBR0_EL1` to the
scratch translation table. That prevents later bootstrap and C code from
accidentally touching the same physical syspage region through both the low
identity map and the higher-half TTBR1 mapping. The obsolete E2 syspage
source/destination byte-dump block was removed in the same commit.

Validation:
- `./scripts/rebuild-rpi4b-fast.sh` completed and exported image SHA256
  `c82fa3be79c9a13f35c72a8717e97adfb6d5d7cb719ea31ebb1c7586bdae15b9`.
- `./scripts/qemu-shell-smoke.sh rpi4b` reached `(psh)% help`.
- `./scripts/qemu-shell-smoke.sh generic` reached `(psh)% help` on rerun.
  The first generic run timed out once after the VM log had reached
  `psh: tty open`; record this as a warning to watch for, but not as a
  reproduced regression.
- `./scripts/test-cycle-netboot.sh --label td16-early-ttbr0-drop
  --capture-secs 600 --dhcp-wait-secs 90` reached `(psh)%` on real Pi 4.
  Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-214816-netboot-td16-early-ttbr0-drop.log`.

Warnings observed:
- Build/export: no compiler, linker, DTB, packaging, or image verification
  warnings; helper reported `Verification: OK`.
- Real Pi firmware: expected netboot-path messages only (`sdcard` open
  failures before network fallback, missing `cmdline.txt`, HDMI1 EDID/DSI
  messages while HDMI0 is active). No new Phoenix runtime fault.

Next TD-16 step: inspect remaining early page-table/cache-maintenance
differences against Linux/FreeBSD. Do not enable caches until the remaining
bootstrap aliases and page-table visibility path are explicitly accounted for.

## 2026-05-04 TD-16 step 3 plan — restore early page-table invalidation

Objective: restore the Linux-shaped page-table visibility step that Phoenix
currently comments out before the first `SCTLR_EL1.M` write. This is still
cache-disabled execution; it does not enable I-cache or D-cache yet.

In scope:
- Kernel `hal/aarch64/_init.S` only.
- Restore `_inval_dcache_range` over the contiguous early page-table/scratch
  region populated with the MMU off:
  `PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK`.
- Keep the existing TTBR0 NC block descriptors and early TTBR0 drop.

Out of scope:
- Enabling `SCTLR_EL1.C` or `SCTLR_EL1.I`.
- Changing normal runtime `pmap` attributes.
- Removing TD-04/TD-14 runtime workarounds.

Acceptance criteria:
- Pi 4 fast rebuild/export completes with no compiler, linker, DTB,
  packaging, or image-verification warnings.
- QEMU Pi 4 shell smoke reaches `(psh)% help`.
- Generic QEMU shell smoke reaches `(psh)% help`.
- Real Pi 4 netboot reaches `(psh)%` or, if it regresses, the step is
  reverted and documented as still unsafe on BCM2711.

Rollback baseline:
- Kernel `d52f6c3a`.
- Coordination repo `9222cec`.

Result: PASSED 2026-05-04 in kernel commit `5e727dcc`.

Validation:
- `./scripts/rebuild-rpi4b-fast.sh` completed with image SHA256
  `0f6dc1a9e8254d9c42f41d6ee308eff074a9a6a2e0810cc1fa25044d9c260115`.
- `./scripts/qemu-shell-smoke.sh rpi4b` reached `(psh)% help`.
- `./scripts/qemu-shell-smoke.sh generic` reached `(psh)% help`.
- `./scripts/test-cycle-netboot.sh --label td16-early-pt-inval
  --capture-secs 600 --dhcp-wait-secs 90` reached `(psh)%` on real Pi 4.
  Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-221342-netboot-td16-early-pt-inval.log`.

Warnings observed:
- Build/export: no compiler, linker, DTB, packaging, or image verification
  warnings; helper reported `Verification: OK`.
- Real Pi firmware/netboot: expected SD/USB boot misses before network
  fallback, expected missing per-MAC TFTP files before root `config.txt`,
  expected missing `cmdline.txt`, and expected HDMI1 EDID/DSI messages while
  HDMI0 is active.
- UART helper used `picocom` and printed `STDIN is not a TTY`; capture was
  still valid and completed cleanly.

Key finding: the restored pre-MMU page-table invalidation is safe after the
TD-16 alias cleanup, but it does not change performance. The TD-16 loops still
report `dt≈0x883e**`, proving caches remain disabled and the next step must be
the actual early `M|C|I` transition or better early fault capture around it.

## 2026-05-04 TD-16 step 4 plan — harden early exception dump

Objective: make the temporary early exception vector print ESR/ELR/FAR without
using stack setup or `bl` calls. Previous cache-enable experiments sometimes
degenerated into repeated `E` output, which suggests the handler itself can
refault before completing the diagnostic dump.

In scope:
- Kernel `hal/aarch64/_init.S` only.
- Replace `_early_exception_common` with a terminal, no-call UART dump that
  emits vector slot, `ESR_EL1`, `ELR_EL1`, and `FAR_EL1`.
- Do not enable caches in this step.

Out of scope:
- Changing the normal post-bootstrap exception vectors.
- Interpreting cache-enable faults.
- Changing runtime scheduling, pmap, or driver code.

Acceptance criteria:
- Pi 4 fast rebuild/export completes with no compiler, linker, DTB,
  packaging, or image-verification warnings.
- QEMU Pi 4 shell smoke reaches `(psh)% help`.
- Generic QEMU shell smoke reaches `(psh)% help`.
- Real Pi 4 netboot reaches `(psh)%`, proving the diagnostic path is inert on
  the non-faulting path.

Rollback baseline:
- Kernel `5e727dcc`.
- Coordination repo `4fbb341`.

Result: FAILED and reverted locally; no kernel commit made.

Evidence:
- First rebuild failed in assembly because numeric macro-local labels expanded
  into invalid branch targets such as `99145`. The macro labels were corrected
  to named local labels and the rebuild then passed.
- Rebuild/export after the label fix produced image SHA256
  `1559c85756df97bb4d18e4c6fc9702c606a55a7c1e95c3a86d15ecf585c018c1`.
- QEMU Pi 4 and generic QEMU shell smokes both reached `(psh)% help`.
- Real Pi 4 netboot with label `td16-early-exdump` did not reach `(psh)%`
  in 600 s. It reached `psh: readcmd`, then timed out amid heavy interleaved
  process-spawn/debug output. Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-222821-netboot-td16-early-exdump.log`.

Warnings observed:
- Build attempt 1: assembler errors from bad macro label expansion. Fixed in
  the working tree before validation, then ultimately reverted because the
  hardware acceptance criterion failed.
- Real Pi firmware emitted many `xHC-CMD err` diagnostics during SD/USB-MSD
  probing before network fallback. Netboot and Phoenix startup still
  proceeded, so this is classified as firmware-stage noise for this run, but
  it should be watched if USB-MSD probing starts delaying or disrupting future
  netboot cycles.
- UART helper used `picocom` and printed `STDIN is not a TTY`; capture was
  still valid.

Conclusion: do not commit the no-call early exception dump as written. The
next cache-enable diagnostic should either use QEMU gdbstub first or add a
smaller fault path that is explicitly tested by forcing a controlled exception
under QEMU before running on hardware.

## Sequencing decision for the next session

The user's stated goal is **fully unlocking 4 GiB DRAM and
correctly controlling VC6 memory access** (TD-15). The slowdown
investigation (TD-16) is important for quality-of-life, but it's
not strictly on the critical path for 4 GiB unlock.

Two viable directions:

**Option A: Continue TD-16-cache-enable.** Read Linux's
`arch/arm64/kernel/head.S` cache-enable sequence for A72 / Pi 4,
replicate precisely. Add a more-isolated early-exception handler
that prints ESR_EL1 / ELR_EL1 / FAR_EL1 via direct PL011 MMIO
(no `bl` calls) so we can see the actual fault. Likely 1-2 more
Pi cycles to converge.

**Option B: Pivot to TD-15 phases 2-6 for 4 GiB unlock.**
- Phase 2: move PLO_RPI_MAILBOX_BUFFER_ADDRESS out of ARM-usable RAM.
- Phase 3: VC4 quiesce mailbox sequence before plo `eret`.
- Phase 4: DTB `/reserved-memory` + `/soc/dma-ranges` parsing.
- Phase 5: `total_mem=4096` in `config.txt` + 4 GiB validation.
- Phase 6: DMA correctness audit across `pcie/xhci`.
This addresses the user's stated near-term goal directly. Pi 4 is
slow during validation cycles but each Phase produces visible
progress on the memory layout work regardless of cache state.

## 2026-05-03 reframe — TD-15 (VC6 hygiene + 4 GiB) is the next investment

The TD-15 phase 1 mailbox-buffer drift probe ran on real Pi 4. Result:
**`td15:OK`** — the 64-byte pattern plo wrote at PA `0x02000000` was
intact when the kernel read it through the NC alias. So **VC4 is
NOT writing to the mailbox-buffer page during the plo→kernel
handoff window.** That eliminates one of the top suspects for
TD-04-class corruption.

In the same cycle the user provided HDMI photographs taken ~1 minute
apart showing only ~12 visible characters of new kernel output across
that whole minute. Combined with the TD-14 timing probe data (a
single `proc_send("devfs")` round trip took anywhere from 1 ms to
43 s on the same hardware), this confirms the system is running
**~1000–60 000× slower than expected**, which is timer-driven and
silicon-specific.

This finding is now tracked as **TD-16** with a planned **TD-16-1**
probe (read CNTFRQ_EL0 + CNTPCT_EL0 deltas at boot to confirm whether
the architectural timer ticks at the rate `cntfrq_el0` advertises).
The Phoenix armstub at
`_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S:153`
sets `CNTFRQ_EL0 = 54000000` (`OSC_FREQ`) which is correct for
BCM2711, so either the cntfrq value is being silently overwritten
later, or the actual hardware tick rate doesn't match it on real
silicon, or the timer IRQ is delivered late due to long DAIF-masked
sections.

**Sequencing implication:** TD-16 jumps the queue ahead of the rest
of TD-15. If the actual root cause of every "slow user-space" symptom
on Pi 4 is timer ticks running at the wrong rate, then fixing TD-16
likely makes Gate 2 (HDMI text console) and Gate 3 (PCIe + USB +
keyboard) trivially observable because they'll run at full speed.
TD-15 phases 2-6 still need to land for correctness and the 4 GiB
unlock, but the immediate next investment is TD-16-1.

## 2026-05-03 reframe — TD-15 (VC6 hygiene + 4 GiB) is the next investment

User direction 2026-05-03: handle Pi 4 VideoCore VI memory access
correctly even if it doesn't end up explaining the residual TD-14
IPC slowness. Reasoning: VC6 memory hygiene is on the critical path
to **(a)** unlocking the full 4 GiB DRAM and **(b)** ensuring kernel
and user-space allocations are safe from VC6 / firmware DMA
interference. It's also the single most plausible TD-04 root-cause
candidate we have not yet eliminated.

TD-15 has a complete phased plan in
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`:

  1. VC4 / firmware audit + cheap probes (no source changes; just
     read back `PLO_RPI_MAILBOX_BUFFER_ADDRESS` post-handoff and
     confirm whether VC4 keeps writing).
  2. Move VC4 mailbox buffer out of ARM-usable RAM (`0x02000000`
     is currently inside plo's `map ddr 0x00400000 0x3b400000`).
  3. Quiesce VC4 background tasks via mailbox before plo's `eret`.
     Keep HDMI scanout alive. If TD-04-class corruption disappears,
     we have a causal answer.
  4. DTB-driven memory layout (`/memory@0` consistency check,
     `/reserved-memory` parsing, `/soc/dma-ranges` parsing). Drop
     hardcoded `SIZE_DDR`. Provide `arm_to_bus_addr()` helper.
  5. Unlock 4 GiB: `total_mem=4096` + `gpu_mem=64` in `config.txt`,
     validate end-to-end. Watch for TTBR1 map sizing regressions.
  6. Tighten DMA correctness across drivers (pcie, xhci, future).

The rest of this document — Gates 1-5 toward HDMI text console +
USB keyboard — is still valid and will resume after TD-15 phases
1-3 land. In particular Gate 2 (HDMI text console) becomes much
easier once we know the framebuffer region is the only place VC4
writes.

## 2026-05-03 forward plan — toward HDMI text console + USB keyboard

The user's near-term milestone is **fully booted Phoenix-RTOS on Pi 4
with HDMI0 text console and USB keyboard input**. UART (psh)% is
verified. Remaining gates, in order:

### Gate 1 — clean reproducible boot

- Re-run real Pi netboot at the current main checkpoint
  (manifest `manifests/2026-05-02-td14-uart-shell-prompt.md`,
  image SHA `d219efa27dd6...`) twice in a row, ~20 min apart, to
  confirm `(psh)%` is reliably reached (not a one-off).
- If not reliable, investigate residual TD-14 races before going
  further.
- Strip the high-volume probes the previous agent noted:
  TD-13 spawn-cap log lines, TD-04 hack debug markers, TD-14
  per-syscall timing prints. Keep just enough to identify the
  pl011-tty boundary and the `(psh)%` event.

### Gate 2 — HDMI text console (HDMI0)

The wiring already exists end-to-end:

```
plo video.c (mailbox firmware framebuffer fetch)
  -> syspage_graphmodeSet(width, height, bpp, pitch, framebuffer)
  -> kernel pctl_graphmode handler in hal/aarch64/generic/generic.c
     reads hal_syspage->hs.graphmode and returns it
  -> pl011-tty/pl011-tty.c::pl011_fbcon_init() does platformctl(),
     mmap(framebuffer, MAP_DEVICE|MAP_UNCACHED|MAP_PHYSMEM),
     clears rows, prints "Phoenix-RTOS HDMI console\r\n".
  -> pl011_thr's TX path calls pl011_fbcon_write() per character
     (line 633), so kernel klog mirrored to UART also appears on HDMI.
```

Suspected current failure mode: in the latest psh-prompt log
(`rpi4b-uart-20260502-220314-netboot-td14-readcmd-long.log`),
there is no "Phoenix-RTOS HDMI console" string, so
`pl011_fbcon_init()` either returned `-ENOSYS` (graphmode not
populated by plo) or never ran. The previous agent's probe-strip
removed both branches' debug() prints, so we can't tell which.

Plan:
1. Add a single `pl011_writeRaw(uart, "fbcon: ok\r\n")` /
   `"fbcon: skip <err>\r\n"` after `pl011_fbcon_init()` returns —
   one cheap UART line, not a debug() syscall.
2. If "fbcon: skip" with an error, drop into `pctl_graphmode`'s
   syspage read and confirm width/height/bpp/pitch/framebuffer are
   sane. They might be zero (plo never set them) or corrupt
   (TD-04-class on the syspage handoff).
3. If plo never set them: probe plo's `video_init` (file
   `plo/hal/aarch64/generic/video.c`). The mailbox sequence at
   `tag_setphywh / tag_setdepth / tag_setpxlordr / tag_getfb /
   tag_getpitch` may be timing out on real Pi 4 mailbox.
4. If plo set them but kernel reads garbage, that's TD-04-class on
   the syspage `hs.graphmode` field — same fix pattern as
   `_hal_syspageCopied` (NC mapping or DCIVAC + DSB).

Once fbcon prints its banner, kernel klog should mirror to HDMI
automatically because pl011_thr's TX path is the same on both
devices.

### Gate 3 — PCIe + xHCI + HID for keyboard

The pieces are present but their first IPCs probably hang on Pi 4
(same TD-14 IPC slowness as pl011-tty had). Pipeline:

```
phoenix-rtos-devices/pcie/server/pcie.c  (pid 8)
  - probes BCM2711 PCIe host bridge at PCIE_BCM2711_HOST_BASE
  - enumerates VL805 USB host controller
phoenix-rtos-devices/usb/xhci/xhci.c  (pid 9, "usb")
  - mmaps XHCI_BCM2711_MMIO_BASE
  - resets, runs xhci_init, scans ports
phoenix-rtos-usb/usb/dev.c
  - enumerates USB devices, dispatches to class drivers
phoenix-rtos-usb/libusb/hid_client.c
  - HID class driver, parses report descriptors,
    publishes /dev/kbd0 (boot-protocol keyboard)
phoenix-rtos-devices/tty/pl011-tty pl011_kbdthr() (PL011_TTY_KBD_PATH=/dev/kbd0)
  - opens /dev/kbd0, read()s keystrokes, libtty_putchar() into pl011 tty
  - psh sees keystrokes as if typed on UART
```

In the latest log, `pcie` and `usb` are spawned but produce no
progress beyond `main: spawned`. We need to:
1. Add cheap UART markers in `pcie/server/pcie.c` and
   `usb/xhci/xhci.c` `main()` entry to confirm they reach init.
2. If they hang on first lookup() or namespace IPC, apply the
   same TD-14 mitigations the other agent applied to pl011-tty
   (devfs-direct OID lookup, fast path, retries).
3. Confirm HID server publishes `/dev/kbd0` on real Pi 4 with a
   physical USB keyboard plugged into HDMI0-side port.
4. Confirm pl011-tty kbdthr opens `/dev/kbd0` and feeds keys.

### Gate 4 — interactive UART smoke

Once Gates 1-3 are stable:

- `help`, `ps`, `ls /dev` via picocom send.
- Boot-time prompt + interactive command in <60 s end-to-end.

### Gate 5 — interactive HDMI + USB keyboard smoke

Combined Gate 2 + Gate 3 result. Visual confirmation: type on
USB keyboard, see characters echo on HDMI display.

## Risk register for the plan

- **TD-04-class slowness still active.** Each Pi 4 cycle is
  expensive (~5 min minimum). Aggressive testing budget: at most
  3 Pi cycles per session. Use QEMU + careful single-edit changes.
- **HDMI mailbox** is a separate TD-04-class candidate. If plo
  mailbox doesn't drain reliably, fbcon won't init.
- **PCIe + USB on Pi 4** has never been exercised by Phoenix-RTOS
  per the commit history; first-time bring-up may surface its
  own TD-NN class of issues.
- **HID server / /dev/kbd0** existence is assumed by pl011-tty
  but not verified to actually be created by the usb stack on
  Pi 4. May need additional wiring.
