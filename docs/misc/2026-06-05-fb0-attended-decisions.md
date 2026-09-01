# /dev/fb0 (GPU Tier-1) — proven groundwork + the two attended decisions

> **STATUS (2026-06-26): partly overtaken, residual attended items still ACTIVE.** The
> `/dev/fb0` device exists + is HW-validated (#148, see the 2026-06-07 UPDATE below). Display
> ownership (Decision 1) is now solved IN PRACTICE for the graphics path — both GLQuake and the
> X11 DDX take the framebuffer via `FBCONSETMODE(FBCON_DISABLED)` and render full-screen on HDMI.
> Still open + attended (#149): the fb0 device's own Linux-fbdev `FBIOGET_*` veneer (Decision 2)
> and a true zero-copy `mmap(fd,0)` kernel backing (clients still MAP_PHYSMEM the pa today). NOTE:
> the §47 "caches are off globally" parenthetical is STALE — caches are ON (TD-16 resolved).
> Kept as the reference for those remaining attended decisions.

**Date:** 2026-06-05 (overnight, netboot-only)
**Status:** userspace framebuffer access PROVEN on HW; device bring-up blocked on
two architecture decisions that want a human + a screen, deliberately deferred.

> **UPDATE 2026-06-07 (#148):** the scope-safe `/dev/fb0` DEVICE now exists and is
> HW-validated on netboot — see `phoenix-rtos-devices/video/rpi4-fb/` (manifest
> `manifests/2026-06-06-fb0-framebuffer-device.md`). It ships read()/write() byte
> access + an `RPI4FB_GETMODE` devctl + getattr(atSize/atMode), and does a
> READ-ONLY startup self-test (`rpi4-fb: pa=0x3e87c000 1024x768 bpp=32 pitch=4096
> size=3145728 first_px=0x003060a0; registered /dev/fb0`); `fbcon: ok` still prints
> after, boot→psh, 0 faults. The THREE items below are exactly what was left out
> and remain attended (task #149): (1) drawing/display-ownership vs the pl011-tty
> fbcon — fb0 draws nothing yet; (2) a Linux-fbdev `FBIOGET_*` veneer for Tiny-X;
> (3) a true `mmap(fd,0)` zero-copy backing (kernel VM work — the device-fd mmap
> path demand-pages a private COPY today; clients must MAP_PHYSMEM at the pa from
> `RPI4FB_GETMODE` instead). Quake (MAP_PHYSMEM, no veneer) is the lowest-friction
> first real client.

This note exists so the attended `/dev/fb0` session starts from facts, not a
blank page. It is the GPU Tier-1 entry in `docs/todo/gpu-vc6-impl.md` (Phases
1–2): adopt the plo-allocated framebuffer and expose it as a device.

## What is already PROVEN (diag-udp 'V', lwip `fdbec01`)

A non-destructive probe (`diag_format_fb`) ran on real Pi 4 hardware via netboot:

```
fb: pa=0x3e87c000 w=1024 h=768 bpp=32 pitch=4096
fb mmap rw verified (testoff_word=786416 saved[0]=0xff000000 wrote=0xdeadbeef)
```

Established facts:
1. **`platformctl(pctl_get, pctl_graphmode)` works from an arbitrary userspace
   process** — not tty-special. Returns `{framebuffer PA, width, height, bpp,
   pitch}`. plo/firmware sets up 1024×768×32, pitch 4096 (== width·4, so **no
   per-line off-screen pad**). FB physical base 0x3e87c000 (low RAM, < 1 GB —
   reachable, unlike the high-DRAM SDHCI-DMA hazard).
2. **mmap of the FB PA works** from userspace:
   `MAP_SHARED | MAP_UNCACHED | MAP_ANONYMOUS | MAP_PHYSMEM`, len =
   round_up(pitch·height, page). Same flags pl011-tty's fbcon uses.
3. **Uncached write→read coherence is verified.** Wrote a pattern to the
   bottom-right corner, read it back identical, restored the original
   (0xff000000 = opaque black ARGB). So a `/dev/fb0` mmap-backed surface is a
   straight pass-through of this mapping; no bounce buffer, no cache ops needed
   for the framebuffer itself (caches are still off globally — see TD-16).

So the *mechanism* a Tier-1 `/dev/fb0` needs is done. What's left is interface
and ownership, both of which produce ambiguous/destructive results unattended.

## Decision 1 — display ownership / fbcon coexistence  (needs a screen)

pl011-tty's fbcon is a **live concurrent writer** to this exact framebuffer (it
renders the kernel klog + psh console). A `/dev/fb0` client that fills the
surface fights fbcon: a gradient gets partially overwritten by the next klog
line, and "did pixels land?" can only be judged by a human looking at HDMI —
exactly the unattended-validation trap we avoid.

Options to decide (attended, watching the screen):
- **(a) Handé off:** `/dev/fb0` open → tell pl011-tty to stop drawing (a message
  or a shared "console suspended" flag); console output keeps going to UART only.
  Cleanest for a real graphics app (Tiny-X / Quake), matches Linux VT switch.
- **(b) Coexist by region:** console owns the top N text rows, fb0 owns the rest.
  Hacky; only useful for demos.
- **(c) fb0 wins, console → UART-only permanently** once fb0 is registered.
  Simplest; acceptable if HDMI is "the app's screen" by policy.

Recommendation to evaluate first: **(a)**. The handoff protocol is the reusable
primitive (a future VT/display-manager needs it anyway).

## Decision 2 — device interface ABI  (Phoenix has NO fbdev)

The plan (`gpu-vc6-impl.md`) assumed Linux `FBIOGET_VSCREENINFO` /
`fb_var_screeninfo`. **Those do not exist in this tree** — Phoenix has no fbdev
framework. So the `/dev/fb0` interface is a *new ABI* we'd be defining. Choose:
- **mmap-only + info via the existing syscall:** `/dev/fb0` supports `mmap` (the
  driver returns the FB physical page); clients get geometry by calling
  `platformctl(pctl_graphmode)` themselves (it's general). Minimal, zero new ABI.
- **mmap + a small Phoenix-native devctl** returning a `{w,h,bpp,pitch}` struct,
  so clients don't depend on the platform syscall. One struct, one devctl id.
- **A minimal Linux-fbdev-compatible subset** (define `FBIOGET_*` +
  `fb_var/fix_screeninfo` in libphoenix) — most portable for porting apps, most
  surface to get right; an upstream-coordination question.

Defining an ABI is a one-way door; that is why it waits for review, not for lack
of mechanism.

## Concrete next steps (attended)

1. Pick Decision 1 + 2 (above).
2. New driver `phoenix-rtos-devices/video/rpi4-fb/` (binary.mk), modeled on the
   `rpi4-thermal` userspace pattern: `platformctl(pctl_graphmode)` → mmap →
   `create_dev("fb0")` → msg loop handling `mtMmap` (return the FB page) +
   chosen info devctl. Self-log `rpi4-fb: registered /dev/fb0 w=.. h=.. pitch=..`.
3. Wire fbcon handoff per Decision 1.
4. Validate: gradient test program → HDMI screenshot (this is the step that
   needs the human; that's why it's attended).
5. Remove the diag-udp 'V' probe once `/dev/fb0` covers it (diag-cleanup policy).

## References
- Proven probe: `sources/phoenix-rtos-lwip/port/diag-udp.c` `diag_format_fb` ('V').
- fbcon mapping: `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c` (the
  `platformctl(pctl_graphmode)` query + mmap at ~line 473–510).
- graphmode struct: `phoenix/arch/aarch64/generic/generic.h` (`platformctl_t`).
- Plan: `docs/todo/gpu-vc6-impl.md` (Tier-1, Phases 1–2).
