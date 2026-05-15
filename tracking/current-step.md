# Current Implementation Step

## Active step (2026-05-15 late): cache enable parked again — C-3 deferred-enable round

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
