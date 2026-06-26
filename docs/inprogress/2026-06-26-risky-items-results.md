# Risky / deferred-item push — results (2026-06-26)

Per the user's "continue on the items you considered risky + the pending tasks." Several real bugs
were found and fixed by actually exercising the risky paths. Final state: flag-off GL Quake flagship
bundled, all repos clean.

## ✅ Closed
- **xterm (#36) — DONE, HW-grab-proven.** A working terminal emulator under Xphoenix running a live
  BusyBox `ash` shell (`/ #` prompt on HDMI). Root-cause arc: (1) `ioctl(FIONBIO)` EINVAL (posixsrv
  ptys reject unimplemented ioctls) → route non-blocking via `fcntl(O_NDELAY)`; (2) the real one —
  `get_pty()` opened the pty MASTER (`/dev/ptmx`) but `USE_USG_PTYS` was undefined, so the child's
  slave-open (`unlockpt`+`ptsname`+`open`) was COMPILED OUT → child exec'd the shell on a dead fd →
  Instruction-Abort storm. Fix: `#define USE_USG_PTYS` for `__phoenix__`. Commits: coord 6e16129,
  ports ffa4214 (+ libphoenix be93342 wcslen, 397b4c6 grp stubs). Also satisfies #30's keyboard path
  end-to-end (HID→kbd0→X→xterm→pty→shell); physical keystroke = the remaining user eyeball.
- **GENET cacheable RX (#11) — DONE, HW-validated (default-OFF).** Exercising the risky path found
  TWO real bugs: (a) the per-frame invalidate used `dc ivac` (invalidate-only) which is **EL1-only**
  (NOT in the `SCTLR_EL1.UCI` EL0-enabled set) → trapped at EL0 → killed RX → no DHCP lease
  (deterministic). Fixed to `dc civac` (clean+invalidate, EL0-legal) — RX restored, integrity=PASS,
  zero corruption. (b) the integrity bench thread RETURNED off the end of its entry function; Phoenix
  `beginthreadex` installs no return trampoline, so the closing `ret` jumped to the stack-poison fill
  (`0x1e1e…`) → PC-alignment fault in lwip. Fixed with `endthread()` at all exits. Final HW run:
  DHCP OK, integrity=PASS, NO Exception #34. Throughput 6–9.6 MB/s (≈ the ~8 MB/s uncached baseline —
  civac's double-`dsb` per frame makes Policy B roughly a wash here, not a clear win; correctness +
  crash-free is the deliverable). Commits: lwip 476825b/89cc6e7/f0b75a2/1eee839; kernel `_init.S:589`
  comment corrected (UCI does NOT enable `dc ivac`).
- **NFS-as-`/` (#32) — verified working** (nfsroot variant; `nfs-fs: registered / (takeover)`, no
  `/nfstest`).

## ◑ Advanced / in flight
- **USB #121 free-list corruption (#33) — advanced.** Turned a long-"unconfirmed, non-reproducing"
  bug into a DETERMINISTIC every-boot repro, localized to `usb_allocFrom` (usb/mem.c) with the
  "overflow upstream" guard; non-fatal (guard-caught). Deep fix (the overflowing consumer) deferred —
  3 subagent attempts hit API stalls; well-documented for a focused session.
- **wmaker (#35) — localized, fix in progress.** Builds (full fontconfig/Xft/WINGs stack); hang pinned
  to inside `XftFontOpenName("sans serif")` — the fontconfig match or freetype TTF open (XftDrawCreate/
  FcInit returns fine; not a NULL return). Agent instrumenting match-vs-open + trying a direct-TTF-file
  bypass.
- **vkQuake (#29) — prior "hang" was a STALE-BUNDLE artifact.** The "present 1 then hang" was tested
  against a days-old bundled binary lacking the instrumentation (the documented stale-core hazard).
  Needs a clean rebuild of rpi4-vkquake from the fresh libvkquake.a + a pre-boot marker gate
  (`strings <bundle> | grep "loop %lu enter"` non-empty) before the next grab. Heartbeat markers
  committed (255fcb9). Real verdict still pending a clean run.

## Reusable gotchas (saved to memory)
1. **EL0 cache maintenance:** `SCTLR_EL1.UCI` enables `dc cvau/cvac/cvap/cvadp/civac` + `ic ivau` at
   EL0 — but NOT `dc ivac` (invalidate-only is EL1-only). Userspace streaming-DMA must use `dc civac`.
2. **Phoenix threads must not RETURN:** `beginthreadex` has no return trampoline; a thread function
   that returns pops the stack-poison fill and jumps to `0x1e1e…` → PC-alignment fault. End with
   `endthread()` or loop forever.
3. **EL0 exception → identify the process before addr2line:** ASLR-free statics share virtual
   addresses; addr2line on the wrong `prog/<x>` gives plausible-but-false symbols (cost a misattributed
   "rpi4-audio" finding that was actually USB #121).

## ⚠ CORRECTION (2026-06-26, user-caught) — GENET cacheable RX is NOT safe; #11 RE-OPENED
My "GENET cacheable RX — DONE/validated" above was an OVER-CLAIM. The integrity bench ran in
ISOLATION (no GPU) → integrity=PASS proved only RX-data correctness. With the GPU active (the
genet-final boot had Quake autostart + flag-ON), the cacheable-RX path CORRUPTS THE GPU FRAMEBUFFER:
artifacts/hdmi/20260626-151622-genet-final-final.png is full-screen GREEN NOISE. Progression: Quake
rendered cleanly through grab 151546, then corrupted into green noise by 151612 as the bench's 32MB
RX stream flowed. The flag-OFF flagship (shipped default) renders Quake fine — so the SHIPPED build
is safe — but the feature is HARMFUL, not "a perf wash." The dc ivac→civac (f0b75a2) + endthread
(1eee839) fixes are still correct; the cacheable-RX POLICY itself corrupts the framebuffer when the
GPU is active (likely the dmammap_cached RX pool aliasing the GPU scanout high-memory region — scanout
PAs 0x3d3b2000/0x3db9b000/0x3e384000 — or a civac-range/coherency interaction). #11 re-opened; keep
DEFAULT-OFF + UNSAFE-with-GPU until root-caused. LESSON: validate risky features under realistic
CONCURRENT load (GPU running), never just the isolated micro-bench.

## FINAL outcomes (2026-06-26, end of campaign)
- **xterm (#36) — DONE**: terminal + live BusyBox shell, HW-proven. Production binary staged.
- **Window Maker (#35) — DONE**: renders its desktop (mauve root + Dock + Clip) + STABLE. Fixed a
  chain incl. two general wins: the libphoenix NULL/SIG_DFL signal-handler bug (da69de7) and the
  Phoenix main-stack-default=32KB lesson (1MB PT_GNU_STACK via -z stack-size). Plus the X RECORD
  malloc(0) assert guard, the Xft font-load bypass, and the fonts.conf XML fix. The Pi4 X11 stack now
  runs TWO WMs (JWM + WMaker) + a terminal.
- **GENET cacheable RX (#11) — CONCLUDED UNVIABLE** (default-off, shipped-safe): corrupts the GPU
  framebuffer under load (user-caught); root-caused to a coherency interaction (not overlap/range);
  kept the valuable dc ivac→civac + endthread + kernel-UCI-doc fixes.
- **vkQuake (#29) — PAUSED, 2D-raster-PROVEN**: renders GPU 2D geometry via Vulkan on the V3D (quad);
  frame loop / present / projection / cull all fixed. Texture upload is a deep no-WSI-winsys gap
  (DRM_V3D_SUBMIT_TFU no-op + CL meta-copy fallback also doesn't land textures) → needs a proper
  buffer→image copy impl in v3d_phoenix_winsys.c (focused session). Console gated on that.
- **USB #121 (#33) — ADVANCED**: deterministic repro + localized (usb/mem.c); deep fix deferred.
- Final bundled default: flag-off GL Quake flagship. All repos clean.
- DURABILITY: the mesa blake3 NEON-stub fix is uncommitted in the mesa clone but captured in the
  tracked mesa-phoenix-port.patch (the clone is rebuilt from the patch).
