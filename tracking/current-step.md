# Current Implementation Step

## Active step (2026-05-16 evening): C-3 I-cache reaches spawn loop entry; boundary at `n`

After the morning's `43635eca` (`dc civac` to PoC instead of `dc cvau` to
PoU for kernel text before I-cache enable) reached `…ef` inside
`posix_init()` and a same-source rebuild shifted the boundary to `…ab`
inside the first `lib_printf()`, a brief diagnostic round explored
extending the cache maintenance to cover the post-`_etext` tail of
the 2 MiB `KERNEL_TTL3` mapping:

* **c3-civac-full-2mib** (extended `dc civac` to cover the entire
  2 MiB kernel mapping including `.rodata` + `.bss`): regressed the
  boundary to immediately after the "icache enabled" print, before
  the next `main_uartMark('a')`. Hypothesis: `dc civac` over `.bss`
  pages writes any stale firmware-era SLC dirty lines back to DDR,
  corrupting the kernel-data values that the M-only kernel placed
  there via NC stores. Reverted.

* **c3-civac-text+ivac-tail** (kept text `dc civac`, added `dc ivac`
  for the post-`_etext` tail): hard-faulted with Exception #37 Data
  Abort EL1, ESR=`0x9600014f` = Permission fault level 3 at
  PA=0xc0024588 (.rodata range). Cause: `_pmap_preinit()` maps
  `.rodata` with `DESCR_AP2` (RO) and `dc ivac` requires write
  permission. Reverted.

After reverting both tail-maintenance variants and committing the
text-only `dc civac` with the documented tail-range constraints as
`2ca5df17`, the rebuild's natural layout shift moved the boundary
**much further forward**:

```
iurstxy2z0Imain_initthr: icache enabled
abcdmain_initthr: syspage listed
efgmain_initthr: posix init done
hjkmain_initthr: posix clone done
lmn
```

The boundary is now at the 'n' marker — between `prog = syspage_progList()`
and the first iteration of the spawn loop body. **The kernel now
completes `posix_init()` and `posix_clone()` under I-cache** (the
previous best boundary `…ef` stopped inside `posix_init()`). Real-Pi
UART log:
`artifacts/rpi4b-uart/rpi4b-uart-20260516-192222-netboot-c3-revert-to-text-civac-only.log`.

This is a layout effect, not a code change — removing the `'L'`/`'M'`
markers from the failed tail-maintenance variant shifted subsequent
code by ~8 bytes, moving the function that lands at the stale-SLC
PA range to one that the I-fetch happens not to read in this run.
The layout sensitivity itself is unchanged; a future rebuild may
shift it back.

Image SHA256 for the `lmn` boundary build:
`23b304202f60f0169dfdcd4786b799cea5396bee20010bacf5a5b27adebc4c6e`.

### 2026-05-16 22:00 — reproducibility test: boundary is NON-DETERMINISTIC

Same image (SHA256 23b3042…), two consecutive power-on cycles:
* Run 1: `…lmn` (spawn loop entry)
* Run 2: `…ef` (inside posix_init)

UART logs:
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-192222-netboot-c3-revert-to-text-civac-only.log` (lmn)
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-194241-netboot-c3-reproducibility-same-image.log` (ef)

**This is decisive evidence for the BCM2711 SLC hypothesis.** The kernel
binary is byte-identical across the two boots; the only thing that
changes is the SLC stale state left by firmware (VC4/bootcode.bin/start4.elf/
armstub run differently each cold boot due to GPU memory layout
non-determinism). Whichever cache line in the kernel's PA range
holds firmware residue at boot determines which kernel function the
I-fetch reads wrong via L2 → SLC → DDR.

ARM cache-maintenance ops (`dc isw`, `dc civac`, `ic ialluis`, etc.)
only reach the A72 cluster's caches. They cannot invalidate the
BCM2711 SLC. **No software-only fix using ARM ops alone will be
deterministic on this SoC.** The only remaining paths:

1. **BCM2711-specific SLC invalidate.** Locate the SLC controller
   in the BCM2711 datasheet (likely in the AXI/SoC peripheral
   address range), find the invalidate-all or invalidate-by-range
   register, write the invalidate sequence. Linux's
   `arch/arm/mach-bcm/bcm2711_priv.c` or equivalent driver code
   would be the reference. Requires datasheet study.
2. **Disable the SLC entirely** via the BCM2711 SLC enable register.
   Cuts ~30% of expected performance gain but might restore
   determinism. Same datasheet hunt.
3. **Boot through U-Boot or another loader** that already
   invalidates the SLC as part of its kernel handoff. Linux's
   regular boot path on Pi 4 goes through U-Boot or rpi firmware
   which does this maintenance.
4. **Accept the non-determinism and don't enable I-cache or
   D-cache.** M-only baseline boots correctly every time. The Pi 4
   M-only kernel reaches `(psh)%` prompt and all 8 expected user
   processes spawn.

**Decision (2026-05-16 22:00): pivot to non-cache features.**

The cache work is parked at the c3 deferred-I-cache state (kernel
commit `2ca5df17`, image `23b3042…`) — boot-correct *some* of the
time but non-deterministic. To return to fully boot-correct, either
revert `f7fe6b39` (and successors) or comment out the
`hal_cpuEnableICache()` call site in `main.c` between markers `i`
and `I`. The cache helpers themselves stay in `_init.S` for future
SLC work.

Next focus: **USB+keyboard, SMP integration, HDMI text mode** — all
M-only compatible and independent of the cache enable path.

### 2026-05-16 22:30 — PCIe fix lands; xhci capProbe still ENODEV

After disabling the non-deterministic I-cache call site in `main.c`,
ran the M-only baseline with 480 s capture and the user's HDMI photo
confirmed that the HDMI text console works end-to-end:

  Phoenix-RTOS HDMI console
  fbcon: ok
  st-side: usbkbd
  threads: create proc=… path=usb …

The boot reaches `(psh)%` prompt cleanly. PCIe driver detects
VL805 (vendor 1106 / dev 3483 / class 0c0330) and programs BAR0
to `0xf8000004`. **Then** the driver was previously entering an
infinite probe loop on empty slots `01:01.0`, `01:02.0`, `01:03.0`
— each returned `ven=0000 dev=0000` (BCM2711 root complex returns
all-zeros instead of the spec-mandated `0xffff` for empty slots).

Fixed in `phoenix-rtos-devices 5b20bcb` by treating `vendor_id ==
0x0000` as an empty-slot indicator alongside `0xffff`:

  pcie/server: treat all-zero vendor as no-device on BCM2711

Real-Pi UART after the fix:

  pcie: 01:00.0 ven=1106 dev=3483 cls=0c0330 hdr=00   ← VL805
  pcie: VL805 cmd 0000->0006 rb=0006
  pcie: VL805 BAR0 programmed lo=f8000004 hi=00000000
  pcie: post-scanBus                                  ← clean exit
  pcie: exit main
  …
  (psh)% xhci: capProbe iter pre / ENODEV (looping)

xhci is still stuck — it can't read valid `CAPLENGTH` /
`HCIVERSION` from the VL805 BAR0 space. The pcie driver reads
VL805 capability registers fine through the CONFIG-space ECAM
path (`pcie: caps caplen=20 ver=0100`), but xhci reads through
the CPU outbound window (PA `0x600000000` → PCIe bus `0xf8000000`
per `bcm2711SetOutboundWindow0`). The two access paths are
independent.

Next-session task: verify the BCM2711 outbound window programming
in `pcie/server/pcie.c`'s `bcm2711SetOutboundWindow0`. Possible
issues to check:

* Window enable bit (does the BCM2711 PCIe controller require a
  separate "enable" write after programming the base/limit?)
* CPU→PCIe address translation field encoding (BAR0 PCIe-bus
  address `0xf8000000` may need to be a multiple of window-size
  granularity)
* `bcm2711SetRcBar2(bcm, 0u, 0x100000000ull)` — sets the
  PCIe→CPU inbound window for DMA, but the OUTBOUND CPU→PCIe
  window is the relevant one here
* VL805 BAR0 readback validation: program BAR0, then read it
  back via config space to confirm `0xf8000004` actually sticks
* Direct CPU read from `0x600000000` via a kernel-side test
  before xhci runs — distinguishes "outbound window broken"
  from "VL805 device not responding"

Real-Pi UART evidence:
`artifacts/rpi4b-uart/rpi4b-uart-20260516-201321-netboot-pcie-stop-scanning-empty-slots.log`

Image SHA256 with PCIe fix:
`cc6427c63eef8f7b30a824d420b013aa68ece7319eba1b4da55c7b18f147adde`.

### 2026-05-17 00:00 — xhci capProbe non-determinism + CRCR spec fix

Iterated on the xhci init path through several diagnostic + fix
commits (phoenix-rtos-devices `7bfcff7` then `f25e9a3`):

* **CRCR readback fix**: corrected xhci_programCommandSpace to not
  validate Command Ring Pointer readback against the written value —
  per xHCI 1.0 §5.4.5 the CR Pointer (bits 63:6) and write-only
  control bits (RCS / CS / CA, bits 0:2) read back as zero; only the
  Command Ring Running bit (bit 3) is readable. The previous check
  was a spec-required mismatch.

* **Extended capProbe retry window**: 50 × 100 ms = 5 s → 600 × 100 ms
  = 60 s. On the M-only Pi 4 kernel pcie takes 10-20 s to complete
  the scan + BAR0 program + xhci-reset mailbox, so the previous
  window was racing pcie. Per-attempt diag now prints the attempt
  counter every 50 iters.

* **VL805 firmware settle**: `usleep(200000)` between
  `bcm2711NotifyXhciReset()` (mailbox call that resets VL805 and
  reloads its firmware) and BAR0 programming. The mailbox returns
  early; VL805 firmware reload is async. Without the settle, MMIO
  reads race the boot ROM → firmware handoff.

* **USBSTS preWrite diag** in xhci_programCommandSpace: reads
  USBSTS + USBCMD and prints HSE / HCE / CNR — distinguishes
  "controller in error state" from "bridge returning 0xdead from
  unreachable MMIO".

**Result**: xhci progression is non-deterministic across boots
(same image, different stop points). In the best observed run
(`outbound-window-diag`, image `8a0c362b`) xhci went all the way
through capProbe → reset → validateRuntime → allocCommandSpace →
initCommandRing → allocScratchpads → programCommandSpace; the
mismatch failure was a misread of the spec (the CRCR readback
"failure" is in fact correct per §5.4.5). In subsequent boots
with the CRCR fix, xhci consistently gets stuck at `capProbe iter
ENODEV attempt=0` — the bridge stops returning valid MMIO reads.

**Working hypothesis**: BCM2711 PCIe bridge returns valid MMIO
reads only when VL805 has recently completed a host-initiated
transaction. The pcie diag-outbound mmap+read+munmap happens to
land at the right moment in some runs (probabilistic ordering)
but on most runs xhci fires its first read before that window
and the bridge enters a 0xdead-returning state for subsequent
reads.

**Robust fix (next session)**: implement a config-space ECAM
polling loop in pcie that waits for VL805 vendor-ID readback to
return the expected `0x1106` AND then publishes a "USB bus ready"
sentinel (file in /dev or a posix shared mem flag) that xhci's
capProbe retry loop checks before attempting MMIO reads. This
turns the timing problem from a probabilistic race into a
positive handshake.

Image SHA256 with all xhci/pcie diagnostics:
`c1bf0dd4e6ec908eb3f01576b726b03749f9afc7423998acce2e662945fe6650`

UART evidence for the session's experiments:
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-194030-netboot-m-only-stable-baseline-for-usb-work.log` (pre-fix baseline, ENODEV loop)
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-201321-netboot-pcie-stop-scanning-empty-slots.log` (PCIe empty-slot fix)
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-203737-netboot-outbound-window-diag.log` (best run: full xhci init path)
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-211019-netboot-xhci-30s-retry-window.log` (extended retry)
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-212005-netboot-xhci-usbsts-diag.log` (preWrite diag added)
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-214321-netboot-xhci-attempt-count-diag.log` (attempt counter)
* `artifacts/rpi4b-uart/rpi4b-uart-20260516-220213-netboot-xhci-vl805-200ms-settle.log` (200 ms VL805 settle)

## Prior active step (2026-05-16 morning-afternoon): cache enable C-3 continuation after upstream sync

2026-05-16 upstream-sync checkpoint:

* `sources/phoenix-rtos-kernel` dirty cache/MMU diagnostic WIP was committed as
  `ef3a0fda` before merging upstream.
* Upstream `origin/master` was merged into `agent/rpi4-program-reloc` as
  `2193fc4b`. The only conflict was `proc/name.c`; resolution preserved the
  local TD-14/devfs lookup diagnostics while accepting upstream port
  register/unregister and process-destroy API changes.
* Post-merge `./scripts/rebuild-rpi4b-fast.sh` passed and exported verified
  image SHA256
  `242d495bd67079b8e566735c506839e68a9d39d5f112afa5426d31449c883ffa`.
* Warning handled: the first post-merge fast rebuild exposed stale VM-local
  `libphoenix` syscall objects (`portRegister` multiple definition). A targeted
  `make -C libphoenix clean` in the disposable Pi buildroot fixed the build.
  This should be automated in the fast-rebuild helper for future upstream
  syscall ABI changes.

2026-05-16 C-3 I-cache continuation after the merge:

* Kernel commit `bfde5f63` added post-I-cache `main_initthr()` markers.
  Real Pi log
  `artifacts/rpi4b-uart/rpi4b-uart-20260516-175604-netboot-c3-icache-post-enable-markers.log`
  stopped at `...I...ab`, before returning from the first
  post-I-cache `lib_printf()`.
* Kernel commit `ddccee27` traced `log_write()`; log markers appeared before
  I-cache but not after the stop, proving the first failure was before
  `log_write()` entry. That instrumentation was removed in the next fix.
* Post-enable `ic iallu` (`3b0f298b`) was a negative result: the helper stopped
  before returning. Pre-enable invalidate-all (`ac66dd49`) was positive: real
  Pi reached `iurstxy2z0I`, printed `main_initthr: icache enabled`, reached
  `abcd`, printed `main_initthr: syspage listed`, then stopped before `e`.
* IRQ/FIQ masking around the I-cache boundary (`3e21a146`) regressed the stop
  to `abc`; the hypothesis that an immediate timer interrupt caused the
  post-print stop is rejected.
* Switching the helper's pre-enable invalidate-all to `ic ialluis`
  (`b678636f`) did not move the boundary.
* A console-exit marker (`624b7433`) proved the failure is layout-sensitive:
  simply adding a marker inside `hal_consolePrint()` moved the stop earlier,
  inside the first post-I-cache `main_initthr: icache enabled` string. The
  marker was removed.
* Stronger high-VA text maintenance (`43635eca`) changed the helper from
  `dc cvau` clean-to-PoU to `dc civac` clean+invalidate-to-PoC before
  `ic ivau`/`ic ialluis`. This moved the boundary forward to
  `...abcd...syspage listed...ef`, i.e. the first confirmed post-I-cache stop
  is now inside `posix_init()`.
* A `posix_init()` marker probe (`1a286449`) regressed the boundary back to
  `ab` due layout sensitivity, so it was reverted as `9aec8c60`.

Latest rebuilt image after reverting the regressing `posix_init()` probe:
`artifacts/rpi4b/rpi4b-sd.img` SHA256
`710b10f76654ea65cd9ba5224fa0c798a8042e16ae09f630c37459dfdad88aee`.
This exact reverted image still needs a clean re-test; the best observed
boundary for the same code shape before the temporary `posix_init()` probe was
`...syspage listed...ef`.

D-cache remains parked. Do not enable it until the I-cache-only path is stable
and no longer layout-sensitive.

## Prior active step (2026-05-15 late): cache enable parked again — C-3 deferred-enable round

The 2026-05-14 "M|C|I enabled" milestone was retracted on
2026-05-15 morning after real-Pi bisect showed it broke user-process
execution (every spawned process silently failed before any user
code ran). Three kernel commits were reverted (3d9c5574, 3b63677f,
f2b7c62f) and the kernel returned to the M-only baseline.

2026-05-15 morning: C-1 (PT pre-MMU) and C-2 (TTBR0 cacheable
low-PA aliases) landed cleanly and verified M-only-correct.

2026-05-15 afternoon-evening: tried 12 numbered C-3 variants
(c3a–c3l) and 8 more iterations (c3m–c3t), spanning single-shot
M|C|I, M|C cacheable walks, M|C NC walks, staged M-then-C, plo
`dc civac → dc ivac`, kernel `dc cisw → dc isw`, `_hal_init` asm
probe-store cleanup, deferred enable from `main.c`, post-enable
VA-range invalidate, double set/way invalidate, split D-only vs
I-only enable. **Every variant fails** either at the first
post-MMU walker read (cacheable walks) or hangs at the first
post-SCTLR.C=1 / SCTLR.I=1 cacheable data access (deferred enable).

Cache enable is parked. Re-engaging requires either a hardware
debugger (JTAG/SWD or U-Boot debug stub) to single-step the
post-SCTLR-flip behavior, OR a BCM2711-specific SLC (system L2
cache) invalidate that ARM cache ops don't reach — the leading
hypothesis for the persistent stale-read pattern.

**Full c3a–c3t attempt matrix + planned next strategies** are in
[docs/research/2026-05-15-cache-enable-c-approach-design.md](../docs/research/2026-05-15-cache-enable-c-approach-design.md)
(see "2026-05-15 night — C-3 second pass" section).

2026-05-15 late update: c3u/c3v/c3w/c3z were tested on real Pi 4:

* c3u-style L1D prefetch disable in the armstub let deferred D-cache enable
  return (`Cc`) but hung immediately afterward before `td16b`.
* Corrected / stronger A72 prefetch sweep (`CPUACTLR_EL1[56]`,
  `CPUACTLR_EL1[55]`, `CPUECTLR_EL1[38]`, L2 prefetch distances cleared)
  also stopped at exactly `cdegCc`.
* Outer-NC kernel mapping experiment (MAIR slot 4 = Normal Inner-WB /
  Outer-NC for normal kernel pages and TTBR0 low-PA aliases) also stopped
  at exactly `cdegCc`.

Real-Pi UART evidence:

* `artifacts/rpi4b-uart/rpi4b-uart-20260515-184812-netboot-c3u-l1d-prefetch-disable.log`
* `artifacts/rpi4b-uart/rpi4b-uart-20260515-185112-netboot-c3vw-a72-prefetch-disable-sweep.log`
* `artifacts/rpi4b-uart/rpi4b-uart-20260515-185538-netboot-c3z-outer-nc-kernel-map.log`

Conclusion: simple A72 prefetch disabling and MAIR-level Outer-NC bypass do
not fix the post-`SCTLR.C=1` stale-read/hang. The next high-information step
is no longer more CPU prefetch tweaking; it is either a true BCM2711 SLC
maintenance path or a cache-enable ritual that stays in assembly until it can
prove multiple post-enable data reads without returning to C.

## Locked-in shipping configuration

Boot-correct on real Pi 4 in this state:

* armstub: A72 erratum 859971 + **1319367** (CPUACTLR2_EL1[0]=1) +
  SMPEN, applied at EL3 reset.
* plo: M-only + teardown `dc ivac` (discard firmware-stale L2 lines,
  do NOT clean over plo's just-loaded kernel image).
* kernel: M-only + cache-hygiene fixes: `dc cisw → dc isw`,
  `_hal_init` asm probe-store cleanup. Reliably reaches `(psh)%`
  prompt and spawns all 8 user processes (bind / dummyfs /
  dummyfs-root / mkdir / pcie / pl011-tty / psh / usb).
* 4 GB DRAM unlocked via the `ddrh` map for chunk 2 in syspage.
* HDMI: framebuffer console up (fbcon spawned).
* SMP smoke: cores 1-3 wake from armstub spin-table, print alive
  marker, park in WFE.

Latest verified manifest:
`manifests/2026-05-15-cache-hygiene-fixes-m-only.md`.

## C-3 work-in-progress on the kernel branch

The kernel branch `agent/rpi4-program-reloc` currently has commit
`f7fe6b39` "deferred cache-enable helpers (C-3 work-in-progress)"
on top of the boot-correct state, plus a local late-2026-05-15 diagnostic
delta for c3v/c3w/c3z. This state:

* Adds `hal_cpuEnableDCache` and `hal_cpuEnableICache` in
  `hal/aarch64/_init.S` (asm helpers with the Linux set_sctlr
  ritual).
* Adds declarations in `hal/aarch64/aarch64.h`.
* Adds a late-enable call site in `main.c` after `_hal_init()`,
  currently invoking `hal_cpuEnableDCache()` for the c3v/c3w/c3z
  experiments.
* Adds armstub A72 prefetch-disable diagnostics and an Outer-NC MAIR slot 4
  mapping diagnostic. Both have been tested and are known not to move the
  stop point past `cdegCc`.

**This commit is intentionally non-boot-correct.** The
late cache-enable call site causes the kernel to hang silently after the
`Cc` UART marker. To return the kernel to boot-correct state, either:

1. Revert `f7fe6b39` with `git revert f7fe6b39`, or
2. Comment out the late `hal_cpuEnableDCache()` / `hal_cpuEnableICache()`
   call in `main.c`, leaving the asm helpers in
   place for future experiments.

The intent is to **preserve the C-3 work** so the next session
starts from the current experimental state without reconstructing the helper
functions.

## Next session — priority order

1. **c3y: BCM2711 SLC invalidate.** Most likely fix per BCM2711
   docs / Linux bcm2711 driver review. Adds a
   `_hal_bcm2711SlcInvalidate` function and calls it inside
   `hal_cpuEnableDCache`. Requires reading the BCM2711 datasheet
   sections on the system-cache controller.
2. **Assembly-only post-enable probe.** Keep execution in `_init.S` after
   `SCTLR.C=1` and perform direct literal-free reads/writes of several known
   kernel addresses before returning to C. This can distinguish "return to C
   frame/literal access" from "any cacheable read is poisoned."
3. **c3-jtag**: hardware debugger setup. Last resort if c3y and assembly
   probes cannot isolate the failing read.
4. **c3-accept**: stop cache enable work for v1; pursue
   USB+keyboard, HDMI text, SMP — they all work fine on M-only.

## What blocks USB+keyboard (still separate from cache)

The kernel's PCIe driver enumerates the VL805 device on bus 1 but
reads vendor/device IDs as `0000` and all six BARs as raw zero.
This is a firmware-to-kernel PCIe state handoff and/or VL805
mailbox reset issue — independent of cache enable. Worth its own
session once cache is either landed or definitively parked.

## Subordinate items

* TD-01 … TD-16, TD-plo-dcache, TD-plo-icache, TD-15-mboxprobe,
  TD-04 — see `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`. The
  cache-related TDs (TD-16, TD-plo-dcache, TD-plo-icache) remain
  open.
* TD-04-hack-2 (asm probe stores in `_hal_init`) — REMOVED in
  kernel `3d8bb81b`. Heisenbug protection turned out to be the
  hang point under M|C with NC walks; removing the probes made
  the kernel reach further in deferred-enable trials.
