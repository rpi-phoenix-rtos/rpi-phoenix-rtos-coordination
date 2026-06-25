# HW validation — what actually renders on HDMI (2026-06-25, grab-proven)

Each result below was verified by an HDMI screen grab I captured + read (not just a log line).
Honest status, after the user correctly pushed back on over-optimistic earlier claims.

## ✅ GL Quake — WORKS (the solid, testable flagship)
Real textured Quake level on HDMI: stone walls, lit torch, shotgun viewmodel, HUD (health/ammo),
**~32–42 fps**, multiple changing frames. Grab: `artifacts/hdmi/…-glquake-validate-tick.png`.
**Default**: GL Quake autostart is re-enabled (commit 64ead29) — netboot the current image and it
plays. This is the one to demo.

## ✅ X11 via `startx` (xeyes alone) — WORKS
Black X root + the xeyes widget, rendered on HDMI (matches the 2026-06-23 evidence
`artifacts/x11/xeyes-on-hdmi.png`). Grab: `artifacts/hdmi/…-x11-xeyes-alone-…161015…png`.
**How to test** (boot-to-psh build — comment the rpi4-quake launch + rebuild, OR nfsroot variant):
```
ls /nfstest/bin            # REQUIRED first — warms the NFS dircache (see #156 below)
/nfstest/bin/startx        # xeyes on a black root
```

## ✅ X11 `startx desktop` (twm + xeyes) — NOW RENDERS (FIXED + grab-proven)
**Fixed 2026-06-25.** HDMI shows a black X root (console text fully overdrawn) with a **twm-decorated
xeyes window** — a green titlebar reading "xeyes" + window-manager iconify/resize buttons, the two
eyes inside the managed window. Grab: `artifacts/hdmi/20260625-163446-twm-verbose-final.png`
(brightness trace: console 98% bright → 0.0% fully black at 163428 → black + 1.7% window). This was
the user's "startx desktop did nothing" report. Two fixes, both committed:
1. **DDX periodic full-screen flush** (`tools/x11-port/ddx/fbdev.c`, commit `2b224a2`): the kdrive
   fbdev DDX blitted only DAMAGED scanlines to /dev/fb0, so twm's static root never overdrew the
   console (xeyes-alone worked only because its animation kept producing damage). Added an OsTimer
   (`TimerSet`, 300 ms) that re-blits the ENTIRE shadow to fb0 regardless of damage — static WM
   content now reaches the display, the first tick overdraws the console immediately.
2. **Launcher output visibility** (`tools/x11-port/launcher/pl_phoenix_xlaunch.c`, commit `99bc144`):
   psh wires a program's stdout to the console but NOT its stderr, so the launcher's + X server's +
   clients' diagnostics all vanished ("no debug output, no logs"). `dup2(STDOUT_FILENO,
   STDERR_FILENO)` in the parent before forking makes the whole session self-report on the UART
   (server pid, fb0 geometry, socket-up, mouse-active, the periodic-flush banner, per-client launch).
**How to test** (boot-to-psh build — comment the rpi4-quake launch in user.plo.yaml + rebuild):
`ls /nfstest/bin` (#156 warmup) then `/nfstest/bin/startx desktop`.

## ❌ vkQuake — runs crash-free but renders NO recognizable content (earlier claim CORRECTED)
**Correction:** my earlier "first visible vkQuake frame" was over-stated. vkQuake now runs crash-free
through full init (all 41 shaders, pipelines, render resources — a real achievement, 9 blockers
fixed), and presents at most a single transient render-pass CLEAR (solid color), but it does NOT
render recognizable Quake content (no console/menu/3D) and does not sustain the frame loop. The HDMI
shows the boot console, not vkQuake. Honest status: **not a visually-testable result yet.** The
remaining work is the per-frame loop + 2D draw recording + the no-WSI present actually landing
sustained frames on fb0 (the same fb0-present theme as the twm case).

## NFS first-access ENOENT (#156) — affects every /nfstest test
The FIRST access to any `/nfstest/...` path returns "not found" (libnfs dircache / mount-settle
race), even though the file is present. This is why `startx desktop` reported nothing — `psh:
/nfstest/bin/startx not found`. **Workaround:** `ls /nfstest/bin` (or the dir you need) once before
running anything from it. Real fix tracked separately (the NFS boot-order/dircache, #156).

## Summary for testing
- **Demo now, works (bundled default):** GL Quake (boots straight into it) — recognizable, stable, ~40 fps.
- **Works (grab-proven), with #156 warmup, on a boot-to-psh build:**
  - `ls /nfstest/bin; /nfstest/bin/startx`          → xeyes on a black root.
  - `ls /nfstest/bin; /nfstest/bin/startx desktop`  → **twm-decorated xeyes window on a black root.**
- **Not yet:** vkQuake (crash-free init, no sustained recognizable content) — the fb0-present/frame-loop
  theme; clear next steps above.
</content>
