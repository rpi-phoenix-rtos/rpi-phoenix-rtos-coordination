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

## ❌ X11 `startx desktop` (twm + xeyes) — does NOT render (twm issue)
The desktop mode launches cleanly (Xphoenix + fbcon-disable + mouse0 + twm + xeyes all start, per
the UART log) but the HDMI keeps showing the **stale boot-console text** — the X root-clear never
reaches fb0. Isolated by comparison: **xeyes-alone clears the root to black + blits (works); adding
twm suppresses that full-screen blit.** The kdrive fbdev DDX blits only DAMAGED scanlines via
lseek()+write() to /dev/fb0 (mmap(fb0) is unbacked, #149); xeyes animates → continuous damage →
blits, but twm's static root produces no full-screen damage, so the console text underneath is never
overdrawn. **Fix idea:** add a periodic full-screen shadow→fb0 blit in the DDX (like GL Quake's
continuous full-frame blit), or force a full root repaint when the WM starts. This was the user's
"startx desktop did nothing" report — the launcher is fine; this DDX/twm blit gap is the cause.

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
- **Demo now, works:** GL Quake (boots into it) — recognizable, stable, ~40 fps.
- **Works with the #156 warmup:** `ls /nfstest/bin; /nfstest/bin/startx` → xeyes on a black root.
- **Not yet:** `startx desktop` (twm blit gap) and vkQuake (no sustained content) — both are the
  fb0-present/full-blit theme; clear next steps above.
</content>
