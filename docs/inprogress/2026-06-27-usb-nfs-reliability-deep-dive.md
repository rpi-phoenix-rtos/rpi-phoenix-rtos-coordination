# 2026-06-27 — USB + NFS reliability deep-dive (the two foundational bugs)

User mandate: NFS must ALWAYS support direct exec; USB must ALWAYS fully enumerate on boot.
No tricks (no RAM-copy, no retries) — proper root-cause fixes. Linux + the Pi firmware both do
USB reliably every boot, so it is a Phoenix software bug, not hardware.

## ✅ NFS direct-exec — FIXED + committed + HW-validated (kernel f145658f)

Root cause: the demand-paged ELF loader's `object_fetch` (vm/object.c) issued ONE `proc_read` per
page and treated only a negative return as failure. NFS legitimately returns SHORT positive reads,
and `vm_pageAlloc` does not zero pages → a mid-file short read left a page with a correct head +
**garbage tail** → corrupt ELF → the vfork'd child died **silently** (no output, no fault — exactly
"startx returns immediately to psh"). copy-to-RAM worked only because its copy path loops short
reads; `object_fetch` was the lone non-looping reader.

Fix (the proper, no-trick one): `object_fetch` loops `proc_read` to end-of-page (bounded by file
size), then zero-fills the tail. HW-validated: micropython, busybox, and the 665KB startx launcher
(→ Xphoenix + twm + xterm) all exec reliably from NFS where they failed intermittently before.

Note: `busybox sh -c '<long quoted script>'` still returns ENOMEM — a **psh argv-parsing artifact**
of complex quoted commands (busybox/applets exec fine directly; no `object_fetch` failure even with
USB disabled). Separate low-priority psh issue, NOT NFS.

## ✅ USB enumeration — BOTH bugs FIXED + HW-validated (11/11 cold boots, kbd0+mouse0)

Update (end of session): both USB bugs below are FIXED and committed. USB now fully enumerates
every device on every boot (11/11: keyboard + mouse), where it was ~1/4 before.
- **Bug A (#129) FIXED — two-step BSR Address Device** (devices `53383d1`): BSR=1 (setup context
  only) then BSR=0 (assign), matching Linux xhci_setup_device. The single-step BSR=0 wedged the
  VL805 command processor (no completion event). + TRSTRCY 50ms reset-recovery delay (usb
  `47eede9`). HW: 0 AddressDevice timeouts across ~15 boots.
- **Bug B (#121) FIXED — dc civac on uncached DMA pages** (usb `12c4fe8`): evict the recycled
  pages' stale dirty cache lines right after mmap in usb_allocUncached. HW: 0 corruption across
  ~15 boots; "Fail to init hcds" gone.
- Decisive enabler: the `external/linux` xhci oracle (xHCI §4.6.1 — a dequeued command ALWAYS
  completes, so "no completion" = never-dequeued, not device-not-ready) reordered the whole
  hypothesis space and pointed straight at the BSR scheme.

The original diagnosis (kept below for the record) reached "two bugs, key theories eliminated"
before the fixes landed.

## ◑→✅ USB enumeration — root-caused into TWO independent intermittent bugs (now both fixed above)

The ~3/4 cold-boot enum failure is NOT one bug. Decisive instrumentation (xhci.c event-ring dump +
AddressDevice slot-context/PORTSC dump; usb/mem.c PA-provenance + live-writer test) showed two:

### Bug A — #129: controller does not complete AddressDevice (silicon-boundary, intermittent)
- The VIA HS hub on the root port enumerates fine (ep0 control transfers complete; ~22 events
  posted, general DMA works). The controller completes EnableSlot.
- It then **intermittently never posts the AddressDevice command completion** while staying
  running/healthy (USBSTS: HCH=0, HSE=0, HCE=0; EINT=0, IP=0). Full-ring scan: the completion is
  genuinely ABSENT (not a Phoenix poll-miss — the advisor's lost-wakeup hypothesis is DISPROVEN).
- **TT context is CORRECT** (verified: mouse slot `ttHubSlot=1 ttPort=3`, kbd `ttPort=4`; hub
  speed=3/route=0/no-TT). So the advisor's TT hypothesis is also disproven.
- At a SUCCEEDING AddressDevice the root PORTSC = `0x40000e03` (CCS=1 PED=1 PR=0 PLS=0/U0 HS) —
  port healthy/enabled. (Need a FAILING-AddressDevice PORTSC to compare — not yet captured; the
  fail boots mostly hit Bug B or didn't reach the slot-1 AddressDevice diag.)
- **The recovery is also broken (Phoenix-side, FIXABLE):** after the first AddressDevice times out,
  `xhci_cmdRingRecover` resets the command ring to base, so the re-issued command ALIASES the prior
  command's physical address; the event-ring consumer (`eventDeq`) has meanwhile OVERSHOT (consuming
  port-status-change events, type 34), and `cmdRingRecover` reports "CRR did not clear after abort".
  On retry the controller DOES post the completion (`ourEvent=PRESENT idx=1`) but eventDeq is past
  it → missed. **A correct recovery (re-sync event ring + fresh command addresses + working CRR
  abort) would let a dropped first attempt succeed on retry — plausibly "always enumerates" even if
  the silicon occasionally drops attempt #1.** This is the most tractable USB fix.

### Bug B — #121: usb DMA pool corrupted by stale data (intermittent → "Fail to init hcds")
- The uncached `usb_mem` pool's buffer header (`buf->next`@0, `buf->head`@16) is intermittently
  smashed with FOREIGN data — decoded ASCII from other processes' banners: `" jack @ "` (rpi4-audio
  PWM banner), `"rpi4-hwr"`, `"Raspb"`, `" Phoenix"`. The usb daemon never writes those.
- **PROVEN not double-allocation:** at the corruption, two back-to-back reads of the smashed field
  are STABLE (r1==r2, e.g. both 0x5b) → no live writer owns the page → it is a one-time stale
  snapshot, NOT a concurrent cached-alias. (Refutes the refcount/allocator-bug branch.)
- Corrupt pool buffer PA = `0x036bd000` (the first pool buffer, allocated early at usb_memInit).
  Boot order: rpi4-sysinfo/hwrng/audio launch BEFORE usb and could free dirty cached pages recycled
  into usb's uncached pool.
- **TENSION to resolve before any fix:** `_pmap_remove` (unmap) DOES flush cached-RW pages via
  `_pmap_cacheOpBeforeChange` (pmap.c:590) — so a page unmapped on process exit should be clean
  before reuse. Yet stale data appears. So either some free/teardown path BYPASSES the flush, or the
  flush condition (`oldCachedRW`) misses this case, or the mechanism is subtler. **DO NOT patch pmap
  blind** (advisor): the decisive next experiment is to instrument `vm_pageAlloc`/`vm_pageFree` for
  the usb pool PA and confirm freed-then-reused + whether it was flushed. Then the fix is likely
  flush-on-free (or flush-when-remapped-uncached via the existing scratch-page mechanism). The #121
  guard already committed (usb 53b3db2) makes the head-corrupt case non-fatal (bounded leak), but
  the `buf->next`-corrupt-during-init case still hits "Fail to init hcds".

## Honest status
- NFS: DONE.
- USB: both bugs root-caused; neither fixed yet. "Always enumerates" needs BOTH (A's recovery +
  B's cache-coherency). These are the hardest problems in the port; the fixes are a complex xhci
  recovery rewrite (A) and a careful boot-critical kernel cache change (B) — to be done correctly,
  not rushed. Next concrete steps: (1) Bug A — implement a correct command-ring + event-ring
  recovery so retries succeed; (2) Bug B — the page-provenance experiment, then flush-on-free/uncached.

## Diagnostic state (uncommitted WIP in the siblings)
- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`: event-ring timeout dump (xhci-diag#129) +
  AddressDevice slot-ctx/PORTSC dump. Re-gated to enum-critical waits.
- `sources/phoenix-rtos-usb/usb/mem.c`: pool-buffer PA log + corruption PA/live-writer test.
- Left uncommitted (like the parked SD/WiFi WIP) for the continued USB fight; strip/gate before any
  production commit (advisor).
