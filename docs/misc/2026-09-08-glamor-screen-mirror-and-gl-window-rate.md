# Three findings from the `x-action2` capture (2026-09-08)

Source: `artifacts/hdmi-video/20260908-185522-x-action2.mp4` (240 s, 1920×1080@30,
`startx_gpu action`), UART `artifacts/rpi4b-uart/rpi4b-uart-20260908-205511-x-action2.log`
(**0 faults**, all stage gates green). The owner spotted two of these by eye; the third fell out
of measuring the first properly.

Reproduction of every number below: `.venv/bin/python` with `numpy`/`PIL`, frames extracted with
`ffmpeg -ss <t> -i <mp4> -frames:v 1`. **All measurements are on unfiltered native-resolution
frames** — no crop-and-brighten, which is what made the first pass on this misleading.

---

## 1. ★★ Glamor paints screen content at its exact vertical MIRROR (a fifth bug)

Two visually different artefacts, one mechanism.

**Instance A — the Game-of-Life xterm shows the `top` window, upside down.** At t≈112 s a band
inside the `python3` xterm (client area, screen rows ≈192–232) contains legible but Y-flipped
text from the *other* xterm: `1.1M rpi4-vcmbox`, `1.1M dummyfs-tmp -m /ramtmp -D`,
`1.1M rpi4-thermal`, `17.1M pl011-tty`. Those lines really are on screen — in the `top` window,
around rows 861–901.

**Instance B — a mirrored Window Maker Clip icon in the bottom-left corner.** The "Main" clip
icon that belongs at top-left (rows ~6–74) also appears at bottom-left (rows ~1008–1076),
vertically mirrored, in a region that should be empty desktop.

### The measurement

For a destination band `A`, search every source offset `b` for the one minimising
`mean|A − vflip(frame[b:b+h])|`. A true mirror shows up as a sharp minimum:

| instance | destination rows | best vflip source | mean&#124;diff&#124; | runner-up | `dst_y + src_y` |
|---|---|---|---|---|---|
| A (GoL band, x 700–1260) | 192–232 | **861–901** | **4.62** | 20.97 | 1092 |
| B (clip icon, x 0–70) | 1008–1076 | **6–74** | **5.03** | 16.94 | 1081 |

A residual of ~5 grey levels against a ~17–21 runner-up is a pixel-exact match plus MJPEG
capture noise. The non-flipped search finds nothing but the trivial self-match (0.00 at the same
offset) — so the relationship really is a *flip*, not a translation.

### What that pins down

- **The mirror axis is the full screen height, not a window.** `dst_y + src_y` ≈ 1080 for both
  instances (1092 and 1081; the small per-instance excess is the copy's own offset — 13 px is
  one xterm text row, i.e. a one-line scroll, and 2 px is a frame inset). If the wrong
  coordinate were window-local, instance A could not pull pixels out of a *different window*
  200 px away, and instance B could not reach across 1000 px of desktop.
- **So the faulty coordinate lives in the screen pixmap's texture space**, which is exactly
  where this stack has a known convention mismatch: the screen pixmap is a GL texture whose
  scanout readback is Y-flipped (`PHX_READBACK_FLIP_Y 1`,
  `tools/x11-port/glamor-shim/glamor_phoenix_ctx.c:236`). The signature is a blit that touched
  the screen pixmap at row `y` where the rest of the stack means row `H-1-y`.
- **Instance B is persistent, instance A is transient.** The clip mirror is byte-identical in
  every frame sampled from t=72 to t=235 (`mean|diff|` 5.03 at src y=6 in all of them) and
  first appears between t=60 and t=66 — i.e. it is painted once, when Window Maker draws the
  Clip, and never repaired. Instance A is a spike: the GoL canvas' bright-pixel share is
  4–8 % on either side of t=112 and **26.03 %** at t=112.

### The concrete suspect

The fork's screen-pixmap Y-flip was hand-rolled into **`glamor/glamor_transfer.c`** only
(`tools/x11-port/patches/xorg-server-21.1.24-glamor-screen-upload-yflip.patch`, which gates on
`pixmap == GetScreenPixmap(screen)` in both `glamor_upload_boxes` and `glamor_download_boxes`).
An audit of every pixel-transfer call site in glamor 21.1.24 finds **two more that the patch
never touched**, in a different file:

| site | call | flip applied? |
|---|---|---|
| `glamor_transfer.c:121,128,135` | `glTexSubImage2D` (upload) | yes — patched |
| `glamor_transfer.c:240` | `glReadPixels` (download) | yes — patched |
| **`glamor_spans.c:234`** | `glReadPixels(x1 - box->x1, **y - box->y1**, …)` — `glamor_get_spans` | **no** |
| **`glamor_spans.c:345`** | `glTexSubImage2D(…, **y1 - box->y1**, …)` — `glamor_set_spans` | **no** |

Both operate on any drawable, the screen pixmap included, and both index the FBO row directly
from the X row. `glamor_get_spans` reading the screen unflipped delivers precisely "the content
of screen row `H-1-y`", which is the observed defect; `glamor_set_spans` is the matching
write-side hazard. This is a code fact, not yet a proven causal chain — the remaining step is to
apply the same screen-pixmap gate to those two sites and see whether both artefacts disappear.

### Why it matters beyond the cosmetics

This is the **same root cause family as the owner's bug #2** (the Quake II underwater Y-mirror),
whose entry in the weekly log already records the real fix: *declare the scanout FBO `FlipY` and
delete the `st_atom_framebuffer.c:137` size gate, which also removes glamor's three hand-rolled
flips.* Bug #2 currently carries a documented stopgap — a conditional correction on a conditional
bug. This finding is a second, independent, user-visible symptom of the same unresolved
convention split, and it argues for doing the structural fix rather than adding a fourth
hand-rolled flip. That work is owner-gated: it invalidates the six-application hardware gate and
needs a re-verification pass.

---

## 2. ★ The GL-accelerated X11 window really does update at ~1 fps — not a capture artefact

The owner asked whether the slow-looking GL window was the grabber or the stack. It is the
stack.

Method: crop the GL window region (`crop=384:340:305:200`) from **120 consecutive captured
frames** (4.00 s at 30 fps) and count consecutive pairs that differ on >1 % of pixels.

```
120 captured frames = 4.00 s at 30 fps
consecutive-pair changes >1% of pixels: 4  -> ~1.0 distinct GL updates/s
```

The change pattern is one update roughly every 22 captured frames (~0.73 s). The grabber is
demonstrably capable of resolving this: in the *same* frames the `top` window and `xbill` change
far more often, and the full-screen games recorded on the same card the same evening show 35 and
73 fps counters (§3 below). So:

- **windowed GL through `/dev/v3d-srv` + glamor + X: ~1 fps**
- **full-screen GL in-process (QuakeSpasm): 35 fps**

i.e. the windowed path is ~30× slower than the in-process winsys on the same GPU. A supporting
observation from the same frame (t=112, the `top` window is legible in the capture): `/sbin/rpi4-v3d`
is at **76.9 % CPU** and **CPU0 is at 100 %**, with `Xphoenix-glamor-daemon` at 17.9 %. The
windowed path is CPU-bound in the V3D daemon, not GPU-bound — consistent with a per-frame CPU
readback/copy through the daemon rather than a page flip. Not yet root-caused; recorded here so
the next session starts from a number instead of an impression.

---

## 3. Quake FPS overlays — what the counters actually read

Delivered on the owner's request for "Quake screen captures with FPS". Both figures are read off
**unfiltered native-resolution frames**:

| engine | path | on-screen counter | capture |
|---|---|---|---|
| QuakeSpasm (GLQuake) | OpenGL, in-process winsys | **35 FPS** | `20260908-191446-qs-fps2.mp4` @ t=120 |
| vkQuake | Vulkan / V3DV, in-process winsys | **73 FPS** | `20260908-192454-vkq-fps.mp4` @ t=170 |

Enabling them needed a legibility fix, not just the cvar. `scr_showfps 1` alone draws through
`CANVAS_BOTTOMRIGHT`, whose scale is `glwidth / vid.conwidth` (`gl_draw.c:739`) — at 1920 wide
with the default `scr_conscale 1` that is 1.0, i.e. **8-pixel-tall text**, invisible in any
downscaled recording. `scr_conscale 4` sets `vid.conwidth = 480` (`gl_screen.c:392`), giving a
scale of 4 and ~32 px glyphs. Both engines read the same
`/usr/share/quake/id1/autoexec.cfg`, so one file covers them:

```
scr_showfps 1
scr_conscale 4
```

Confirmed executed on target — the capture shows `Execing autoexec.cfg` in Quake's console.

Note for cutting the reel: `+playdemo demo1` plays **one** demo and then drops to the console
(`cls.demonum` is cleared), so the usable gameplay window in `qs-fps2` is roughly t=75–125 s;
after that the frame is the console backlog. Use `+startdemos demo1 demo2 demo3` if a longer
continuous take is needed.

---

## 4. Side finding: `life.py` stops after ~80 s while the rest of the desktop keeps running

Between t≈140 and t≈160 the Game-of-Life canvas freezes and is then byte-identical to the end of
the recording. Everything else is demonstrably still live over the same interval (t=160 vs
t=235): GL window 49.9 % of pixels changed, `top` window 6.7 %, `xbill` 5.1 %, xclock 1.3 %,
**GoL canvas 0.09 %**. The UART log carries no fault and no Python traceback — but `life.py`'s
stderr goes to its xterm, not the UART, so a Python-level exception would be invisible there.
At the last good frame the status line reads `gen 536 … 12.2 gen/s`, so it had run ~44 s at that
point and stopped some 30–50 s later. Unresolved; the fix for a demo take is to capture the
first ~90 s of the desktop, and the diagnosis is to run `life.py` with its output redirected to
a file on the NFS root.
