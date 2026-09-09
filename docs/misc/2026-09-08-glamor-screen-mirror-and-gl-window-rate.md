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

---

## 5. The spans hypothesis was WRONG — and relinking the X server onto current Mesa breaks it

Recorded because both halves are results, and the second one matters more than the first.

### 5a. `glamor_spans.c` is not the path (hypothesis refuted)

§1 named `glamor_get_spans_gl` / `glamor_set_spans_gl` as the two unpatched pixel-transfer sites
and predicted they were reading the screen pixmap unflipped. The patch that fixed them carried a
one-shot `ErrorF` per direction precisely so the hypothesis could fail loudly:

> if these paths turn out never to run on the screen pixmap, the absence of the message is the
> evidence, and this patch is then a no-op that should be reverted rather than kept.

The diagnostic channel was validated first — `ErrorF` from the DDX demonstrably reaches the UART
capture (`[fbdev] glamor initialised` arrives that way, and so does the shim's own
`glamor-phx: screen-readback FBO status`). In the cycle
`artifacts/rpi4b-uart/rpi4b-uart-20260908-215755-x-spansfix.log`, with a daemon whose binary
contains both markers (`strings … | grep -c 'on the SCREEN pixmap'` = 2):

```
spans diag hits: 0
```

Neither span path ran on the screen pixmap at all, so the flip added there could not have
changed a pixel. **Patch reverted**, and the build-script patch list is back to one entry. The
candidate set narrows to the remaining screen-pixmap consumer the geometry already pointed at:
`glamor_copy.c`. Both measured offsets are copy-shaped (+13 px is one xterm text row, i.e. a
one-line scroll; +2 px is a frame inset), and `glamor_copy_fbo_fbo_draw`'s shader samples
`fill_pos = (fill_offset + primitive.xy) * fill_size_inv` — texel row = pixmap row, with no
screen-pixmap flip, while the destination gets its flip from the rasterizer. That asymmetry is
the signature. The open objection is frequency: a systematically broken CopyArea should corrupt
every scroll, and it does not, so something must gate which copies take that route. Next step is
to instrument `glamor_copy.c`'s four routes the same way, not to patch first.

### 5b. ⚠️ The shipped X server is linked against a Mesa that predates our own `u_vbuf` fix, and current Mesa crashes it

This fell out of the same cycle and is the more consequential finding.

| | Mesa in the binary | result |
|---|---|---|
| the daemon that has been shipping (built 2026-09-08 02:12) | `git-e4be116324` | X desktop works, 0 faults |
| relinked from the current tree (21:38) | `git-aa916f2f06` | **X server dies with SIGILL** |

`aa916f2f060` is *our* commit `u_vbuf: do not silently drop draws on the index-unrolling path
(Phoenix RPi4)` — the fix for the owner's bug #3 (Quake III glitches). It is in the shipped image
for the *games*, which link Mesa in-process; the X server was simply never relinked after it
landed, so glamor has been running on the previous Mesa the whole time.

Failure shape, in order, from the log: server starts, glamor initialises, all six clients launch,
then `v3d-winsys: RENDER MMU-VIO`, `v3d-winsys: BIN MMU-VIO vio_addr=0x00470808
fault_va=0x47080800`, then `libphoenix: NULL handler for signal 4` (SIGILL) and
`xlaunch: server exited (status=0x300) — killing clients`. Screen goes flat.

Attribution: the spans patch is excluded as a cause by §5a (its paths never ran, and for every
other pixmap `PHX_SPAN_FLIP_ROW` is the identity), so the delta that remains is the Mesa link.
Not airtight — the relink also refreshed the xorg core archives — but the Mesa version is the one
visible change and the fault is in the V3D winsys.

**Recovery, done:** the known-good daemon was never overwritten (`build-xfbdev.sh` writes
`Xphoenix-glamor`, and staging renames it to `Xphoenix-glamor-daemon`), so
`tools/x11-port/src/xorg-server-21.1.24/hw/kdrive/fbdev/Xphoenix-glamor-daemon` still held the
02:12 build. Restored to both `.buildroot/_fs/.../bin` and the live fsid=0 export
(sha256 `8e003ab45ef41009…`, 27 948 336 bytes, `git-e4be116324`, 0 spans markers) and re-verified
on hardware.

**What this means going forward:** any future change to the glamor X server requires a relink,
and a relink now moves it onto a Mesa that crashes it. So the next X-server change has to fix
that first — either by finding what in `aa916f2f060` upsets glamor's draw path (the same
index-unrolling code glamor's copy and composite paths lean on), or by pinning the X server's
Mesa link. Do not start an X-server change without budgeting for it.

---

## 6. Open defect in `hevc-play`: a collocated TMVP reference is evicted from the DPB

Found while cutting the video segment of the showcase reel, on a 750-frame
1280×720 clip produced by `tools/hevc-decode/transcode-for-phoenix.sh` from real
gameplay footage (`artifacts/rpi4b-uart/rpi4b-uart-20260908-224139-hevcwin4.log`):

```
hevc-play: 1280x720  240 CTBs (20x12)  750 frames [tmvp] [wpp]
hevc-play: buffers bs=217088 pu=393216 coeff=1572864 luma_stride=92160 cols=10 pool=7 reorder=2 tmvp=1 colmv=61440
hevc-play: frame 12 POC 12 — collocated POC 0 not in DPB
hevc-play: decoded+displayed 10 frame-instances (750 unique frames) on HDMI
```

Playback stops after 10 frames. The retention rule (`hevc-m2.c`, the MARK+REMOVE
block) keeps a DPB entry only while **its POC appears in the current slice's
RPS**, or while it is still pending display:

```c
for (uint32_t i = 0; i < pool_n; i++) if (dpb[i].used && !dpb[i].pending) {
        int keep = 0;
        for (uint32_t k = 0; k < s.rps_n; k++) if (dpb[i].poc == s.rps_poc[k]) { keep = 1; break; }
        if (!keep) dpb[i].used = 0;
}
```

Per H.265 the collocated picture is drawn from the current slice's reference
picture lists, which are built from the RPS — so on a conforming stream this rule
*should* be sufficient and POC 0 should still be present at POC 12. It is not, so
one of these is wrong and the next step is to find out which, on this exact
bitstream:

1. the RPS parse drops a long-term / IDR entry that x265 really did signal, or
2. `collocated_poc` is resolved from the wrong `collocated_ref_idx` / wrong list
   (`collocated_from_l0` handling is at `hevc-m2.c:1097-1098`), or
3. the eviction runs *before* something the collocated lookup still needs.

Not chased in this turn — it is a player-side defect, the hardware decode itself
is bit-exact on the committed conformance vectors, and the reel needed a working
segment. Two things follow from it:

- **The reel's clips are encoded `--no-temporal-mvp`.** That is a *player*
  workaround, not a codec-subset restriction: `hevc-m2` decodes TMVP bit-exact
  and TMVP stays in the verified subset. Do **not** "simplify" the committed
  conformance vectors the same way — they are what proves the subset.
- **`hevc-play` gained periodic progress output** (`presented N/M frames`, every
  25). It previously printed nothing between its banner and its final line, so
  the test harness's idle timer — no UART output for N seconds means finished —
  powered the Pi off mid-playback on a 297-frame clip, and the run produced no
  completion line at all. That is the same class of trap as a silent truncation:
  the absence of output was read as the absence of work.

## 7. The HDMI text console runs at UART speed

Worth recording because it bounds every console-based demo. `life.py` under
curses on the framebuffer console sizes itself to **239×66** and reports **1.0
gen/s** — where the same program in an xterm under X reported 12–13 gen/s. The
arithmetic says why: a full redraw of that field is ~16 KB, the console is driven
by `pl011-tty` which mirrors every byte to the 115200-baud UART (~11.5 KB/s), and
16 KB / 11.5 KB/s ≈ 1.4 s ≈ the observed rate. So the HDMI console's throughput
is the serial port's, not the framebuffer's. Stated as a hypothesis consistent
with the numbers, not as a measured isolation — the test that would settle it is
one run with the UART console detached.


---

## 8. CORRECTION to §5b: current Mesa is NOT what crashed the X server

§5b attributed the X server's SIGILL to being relinked onto Mesa `aa916f2f060`, with the honest
caveat that the relink had also refreshed the xorg core archives. **The caveat was the load-bearing
part: the attribution was wrong.** Isolated by experiment
(`artifacts/rpi4b-uart/rpi4b-uart-20260908-232845-x-mesa-isolate.log`):

| build | Mesa | spans patch | result |
|---|---|---|---|
| shipping (02:12) | `git-e4be116324` | no | works, 0 faults |
| §5b's crashing build (21:38) | `git-aa916f2f06` | **yes** | SIGILL |
| **this isolation (23:28)** | `git-aa916f2f06` | **no** | **no SIGILL, all six clients up** |

Two variables were changed together and I named the wrong one. Reading the Mesa commit afterwards
agrees with the experiment rather than with §5b: `aa916f2f060` only adds a `false &&` guard that
declines index-unrolling plus a diagnostic `fprintf`, and the two predicates it short-circuits
(`util_is_vbo_upload_ratio_too_large`, `u_vbuf_mapping_vertex_buffer_blocks`) are pure — the latter
takes a `const struct u_vbuf *` and only does mask arithmetic. There is no mechanism there for a
binner fault.

**So what did crash it?** The spans-patch build, and most likely not the patch's *logic* — which is
the identity transform for every non-screen pixmap and provably never ran on the screen pixmap
(§5a). The remaining suspect is *how* it was built: `apply_glamor_chain_patch` rebuilds **only**
`libglamor.a` after applying a patch, and links it against xorg core archives left from an earlier
configure/build. A partial rebuild of one archive against stale siblings is a classic way to get
inconsistent inlines and execute garbage, and SIGILL is what that looks like. The isolation build
did a full pass (the patch was already applied, so `patch --dry-run` failed, `applied=0`, and no
partial libglamor rebuild happened).

**Consequences, all of which are better than §5b's:**

- The X server **can** be relinked onto current Mesa. The hazard §5b warned about does not exist,
  and glamor work is not blocked.
- The real hazard is the **partial libglamor rebuild** in `build-xserver-core.sh`. Anything that
  applies a glamor core patch must rebuild the whole server, not one archive.
- The new binary is not fault-free: 4 fault-pattern matches, all `RENDER MMU-VIO
  vio_addr=0x00000000 fault_va=0x00000000` followed by `GPU wedged — true reset + drop this frame`,
  survived by the existing mitigation. The log itself notes the zero VA is the scratch-cfg echo and
  "NOT the fault". The 02:12 binary showed 0. That difference is unexplained and is the reason the
  **demo keeps the 0-fault binary** (`artifacts/x11/known-good/Xphoenix-glamor-daemon.mesa-e4be116324`,
  sha256 `8e003ab45ef41009…`) until it is understood.

Method note for next time: two changes, one experiment. The spans patch and the Mesa bump went to
hardware together, and one run could not separate them — exactly the trap
`docs/misc/2026-09-08-*` keeps recording in other forms.

---

## 9. The mirror is NOT in `glamor_copy` either — the scanout readback is what is left

A second probe, built the same falsifiable way as §5a's, instrumented **all five**
`glamor_copy` routes (`bail`, `cpu_fbo`, `fbo_cpu`, `fbo_fbo_draw`,
`fbo_fbo_temp`), reporting the route and whether the screen pixmap was source,
destination or both, once at each power of two. Result over a whole `startx_gpu
action` session (`artifacts/rpi4b-uart/rpi4b-uart-20260908-233901-x-copyprobe.log`):

```
fbo_fbo_draw  n=1  src_screen=0  dst_screen=0
```

**One copy in the entire session, and neither side was the screen pixmap.** So
`glamor_copy` never touches it, exactly as `glamor_spans` never did. Both
copy-shaped hypotheses are dead, and it is worth being clear that the geometric
argument that pointed at them (the +13 px = one text row, +2 px = frame inset
offsets) was suggestive but not evidence — two probes have now refuted it.

### What that leaves

If nothing *reads* the screen pixmap, the mirrored content cannot be arriving by
a copy. The remaining path that touches whole bands of screen rows is the one the
DDX uses to present: `glamor_phx_screen_readback()`
(`tools/x11-port/glamor-shim/glamor_phoenix_ctx.c`), which for an fb band
`[y0, y0+rows)` reads the GL band `[H-(y0+rows), H-y0)` and reverses the rows into
the shadow buffer the DDX write()s to `/dev/fb0`. Both artefacts are band-shaped,
which fits.

Two specifics make it the strongest remaining candidate:

1. **`H` is a queried TEXTURE height, not the screen height** — `glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &H)`. glamor recycles FBOs from a size-bucketed cache, so the texture backing the screen pixmap need not be exactly 1080 rows. Any H other than the screen height puts the whole flip about the wrong axis.
2. **The measured mirror constants are not equal.** `dst_y + src_y` is **1092** for the xterm band and **1081** for the clip icon. A single fixed axis would give one constant for both, so whatever computes the axis is varying per flush — which is what a per-band `y0`/`rows`/`H` computation does, and what a fixed screen height would not.

### Next step, stated so it is not re-guessed

Instrument the readback itself: log `(y0, rows, H, width)` per call plus the fb row
the DDX writes that band to, then check it against the two measured pairs
(dst 192–232 ← src 861–901, and dst 1008–1076 ← src 6–74). That is a direct
comparison of the suspect arithmetic against the observed offsets, not another
plausibility argument. Do **not** patch first: two hypotheses have now been
refuted by their own probes, which is cheaper than two wrong fixes.

The diagnostic patch has been removed from the tree and from
`glamor_core_patches` now that it has answered its question.

---

## 10. Bug #5 ROOT-CAUSED: render orientation is decided by a SIZE heuristic

The third probe closes the search. Instrumenting `glamor_phx_screen_readback()` over a
whole `startx_gpu action` session
(`artifacts/rpi4b-uart/rpi4b-uart-20260908-234853-x-readback.log`) gives, on **every**
call:

```
glamor-phx: readback #N y0=0 rows=1080 H=1080 width=1920 gl_y=0
```

`H` is exactly the screen height, the GL origin is never negative, and there are **no
partial-damage flushes at all** — the readback always takes the whole screen and flips it
uniformly. It therefore cannot produce a per-region mirror, and §9's leading suspect is
eliminated along with the other two:

| path | verdict | evidence |
|---|---|---|
| `glamor_spans.c` | ❌ not the path | 0 probe hits on the screen pixmap |
| `glamor_copy.c` (5 routes) | ❌ not the path | 1 copy in a whole session, neither side the screen pixmap |
| presentation readback | ❌ not the path | always `y0=0 rows=1080 H=1080`, uniform flip |

Also worth recording, because it kills a tempting hypothesis: a **wrong `H` cannot produce
a mirror**. Screen row `y` is read from texel row `H-1-y`, so with the invariant "texel row
`t` holds screen row `fbHeight-1-t`" the delivered content is screen row `y + (fbHeight - H)`
— a pure vertical **translation**. Only the *presence or absence* of a flip produces a
mirror.

### So the mirrored content is already in the texture, and the writer is the rasterizer

Which lands on the orientation decision — and it is a **size test**, in two independent
places, both in the same Mesa tree:

`src/mesa/state_tracker/st_atom_framebuffer.c:137`
```c
if (st->state.fb_orientation == Y_0_BOTTOM &&
    fb->Width >= 1024 && fb->Height >= 768)
   st->state.fb_orientation = Y_0_TOP;
```

`src/gallium/drivers/v3d/v3d_resource.c:143`
```c
... rsc->base.width0 >= 1024 && rsc->base.height0 >= 768) ? V3D_CREATE_BO_SCANOUT : 0;
```

So "is this the scanout?" is answered by "is it at least 1024×768?". For the 1920×1080
screen pixmap that is right, and the desktop renders upright. For **any other** FBO that
happens to be ≥1024×768 it is wrong, and the consequence is asymmetric:

- the **BO** side is safe — the winsys refuses a second claim
  (`else if (!W.scanout_claimed)` in `v3d_phoenix_winsys.c`, and a bounded
  `scanout_claim_idx` in double-buffer mode), so a second large resource gets fresh DRAM
  rather than aliasing the framebuffer's pages. A tempting "two pixmaps share the fb"
  explanation is therefore **wrong**, and was discarded on reading that guard.
- the **orientation** side is *not* guarded. Nothing ties the `Y_0_TOP` forcing to whether
  the FBO actually claimed scanout, so a large non-scanout FBO is rendered with the
  viewport inverted — content written upside down relative to what glamor expects of an
  ordinary offscreen pixmap. Read back through a uniform whole-screen flip, that is
  exactly a mirror.

**This is the same defect as the owner's bug #2**, whose entry already names the fix
("declare the scanout FBO `FlipY` and delete the `st_atom_framebuffer.c:137` size gate,
which also removes glamor's three hand-rolled flips"). Bug #5 is not a separate bug, and
fixing #2 properly should close both.

### The fix, and the one experiment still owed

The correct discriminator already exists and is already used in the same file:
`PIPE_BIND_SCANOUT` (`v3d_resource.c:907`, `:956`). Both size tests should key off the
resource's bind flags instead of its dimensions — an exact identity test rather than a
heuristic that a large pixmap can trip.

Still owed, and cheap to design though it needs a Mesa rebuild: log every resource that
takes the `>=1024x768` branch at `v3d_resource.c:143` during an X session, together with
whether it actually claimed scanout. **One** such resource would refute the mechanism above;
more than one confirms it. That measurement should come before the fix — three hypotheses
have now been refuted by their own probes, which is far cheaper than three wrong fixes.

⚠️ The fix itself is **owner-gated**: it invalidates the six-application hardware gate and
needs a re-verification pass over the five games plus the desktop.

---

## 11. Bug #5 mechanism CONFIRMED — and the fix proposed in §10 does not work

The measurement §10 asked for, instrumented at the branch itself and run over a full
`startx_gpu action` session
(`artifacts/rpi4b-uart/rpi4b-uart-20260909-001014-x-rscprobe.log`). Every resource of
≥1024 width or ≥768 height, with its bind flags and the resulting create flags:

```
rsc #0     4096x1     bind=0x40  rt=0  scanout_bind=0  -> create_flags=0x0
rsc #1  1920x1080     bind=0x0a  rt=1  scanout_bind=0  -> create_flags=0x2
rsc #2   524288x1     bind=0x10  rt=0  scanout_bind=0  -> create_flags=0x0
rsc #3   524288x1     bind=0x10  rt=0  scanout_bind=0  -> create_flags=0x0
rsc #4  1920x1080     bind=0x0a  rt=1  scanout_bind=0  -> create_flags=0x2
rsc #5   524288x1     bind=0x10  rt=0  scanout_bind=0  -> create_flags=0x0
rsc #6   524288x1     bind=0x10  rt=0  scanout_bind=0  -> create_flags=0x0
rsc #7  1048576x1     bind=0x70  rt=0  scanout_bind=0  -> create_flags=0x0
rsc #8     4096x1     bind=0x40  rt=0  scanout_bind=0  -> create_flags=0x0
```

**Two answers, and they point in opposite directions.**

### 1. The mechanism is confirmed: there are TWO "scanout" render targets

`rsc #1` and `rsc #4` are both 1920×1080 render targets and **both** get
`V3D_CREATE_BO_SCANOUT`. The winsys hands the framebuffer's physical pages to the first
claim only (`else if (!W.scanout_claimed)`), so the second gets fresh DRAM — but nothing
stops `st_atom_framebuffer.c` forcing `Y_0_TOP` on **both**, because it re-derives
"is this the scanout?" from the same size test. The second full-screen render target is
therefore rendered with an inverted viewport, and read back through the uniform whole-screen
flip that §10 measured, which is exactly a mirror. One such resource would have refuted
this; two confirm it.

(Correction to §10, which quoted only the size half of the test: the real condition is
`(bind & PIPE_BIND_RENDER_TARGET) && width0 >= 1024 && height0 >= 768` — narrower than
stated, and it is why only the two RTs and none of the seven buffers take the branch.)

### 2. The fix §10 proposed cannot work: `PIPE_BIND_SCANOUT` is never set

`scanout_bind=0` on all nine, including both render targets. Nothing in this stack sets
`PIPE_BIND_SCANOUT` — the two uses in `v3d_resource.c` (`:907`, `:956`) are on the
`screen->ro` renderonly path, which this port does not use. So "key both tests off the
resource's bind flags" is not available, and §10's fix direction is withdrawn. This is
precisely why the plan was measure-then-fix: the fix would have compiled, changed nothing,
and cost a Pi cycle plus a re-gate to discover.

`struct v3d_bo` does carry a `bool scanout` (`v3d_bufmgr.h:69`), but it is set from the
*requested* create flags, so both RTs would carry it too — also not a discriminator.

### The fix direction that is left

The only component that actually knows which BO got the scanout pages is the winsys, in
`sel_pa != 0`. So:

1. winsys reports the real outcome back from the BO allocation (out-param or query);
2. `v3d_bo_alloc_flags` sets `bo->scanout` from that outcome rather than from the request;
3. the `Y_0_TOP` forcing keys off the first colour attachment's BO actually having scanout
   pages, instead of off `fb->Width`/`fb->Height`.

Step 3 crosses the st↔driver boundary, which is the awkward part and the reason to look
first at the cheaper question this measurement raises: **why are there two full-screen
render targets at all?** If the second is avoidable — or if the two can be told apart by
something the driver already knows — the symptom goes away without new plumbing. That is
the next thing to establish, before any code.

⚠️ Still owner-gated: the fix expires the six-application hardware gate.

**Cleanup done:** the Mesa probe is reverted (`v3d_resource.c` clean against HEAD), the
shared `libv3d-phoenix.a` rebuilt without it (0 probe strings — the games link this archive,
so leaving a per-allocation `fprintf` in it would have been a real regression), the X daemon
relinked clean, and the 0-fault demo binary restored to both roots.

---

## 12. Why the X desktop is slow: presentation is a 300 ms full-screen timer, and the damage path never runs

§2 measured the GL-accelerated X window at ~1 update/s and left it "not yet root-caused". This is
the cause, and it bounds the **whole desktop**, not just that window.

The DDX has two presentation paths (`tools/x11-port/ddx/fbdev.c`):

1. **damage-driven** — `fbdevShadowUpdate()` flushes the damaged Y extent as content is drawn;
2. **a periodic timer** — `FBDEV_FLUSH_MS 300`, flushing the whole frame as an idle safety net.

Instrumenting the damage path (a print on entry, every 64 calls) gives **zero lines across two
separate X sessions**. `fbdevShadowUpdate` is never called with glamor active: the damage tracker is
wrapped *below* glamor (`shadowSetup()` at `fbdev.c:591`, before `glamor_init()` at `:603` — the same
layering that caused the DestroyPixmap bug), so glamor's GL rendering never marks the shadow
damaged. That matches the earlier readback measurement exactly: **every** flush was
`y0=0 rows=1080`, because the only path presenting anything is the full-screen timer.

So the desktop's presentation rate is capped at `1000/300 = 3.3 Hz` before any work is done — and
each flush is expensive twice over: `glReadPixels` pulls 1920×1080×4 = 8.3 MB out of the glamor
screen texture, then `write()` pushes 8.3 MB to `/dev/fb0`, with both the GL readback and the
framebuffer uncached. ~1 update/s is consistent with a 300 ms interval plus roughly 700 ms of
round trip.

### Two fix directions, neither cheap

- **Make damage work under glamor**: wrap the damage tracker *above* glamor rather than below. That
  is the same restructuring the DestroyPixmap fix wanted, and it would turn a full-screen flush into
  a per-window one.
- **Skip the round trip entirely.** The screen pixmap's BO *does* get `V3D_CREATE_BO_SCANOUT`
  (§11: `rsc #1 … create_flags=0x2`), and the whole point of that path per Mesa's own comment is
  that "the GPU raster-stores straight to the displayed surface — no per-frame
  glReadPixels/blit/fb0 CPU copies". If the screen texture really is backed by the framebuffer's
  pages, then the readback and the write are copying the framebuffer onto itself and can go. That
  needs confirming (§11 showed two RTs claim scanout and only the first gets the pages — it must be
  established that the *screen pixmap's* is the one that won).

An attempt at a third, cheaper option — flushing the damaged **rows** instead of the damage
bounding box, which on a desktop with scattered clients degenerates to the whole screen — was
written, and then **reverted**: with the damage path dead under glamor it is unreachable for the
glamor desktop, so it cannot help what this section is about. (It also had two defects worth
recording as a lesson, found only because the display went black: a missing `<stdlib.h>` left
`realloc` implicitly declared, which truncates its pointer on aarch64; and a `runs == 0` early
return that skipped presentation where the old code always flushed. A presentation path must have
no way to present nothing.)

## 13. CORRECTION to §8: current Mesa is not usable for the X server after all

§8 concluded "the X server **can** be relinked onto current Mesa" because the isolation build did
not SIGILL. That was right about the SIGILL — the crash was the partial libglamor rebuild — but I
generalised too far from one signal. With the display actually examined:

| daemon | Mesa | faults | display |
|---|---|---|---|
| known-good (02:12) | `git-e4be116324` | **0** | desktop renders and animates (mean 101, std 68) |
| current-Mesa builds | `git-aa916f2f06` | **4** `RENDER MMU-VIO` + wedge-reset, every run | black in one run, full-screen vertical stripes in another |

So it does not crash, but it does not render either. Glamor work **is** still blocked on this, and
§8's "not blocked" is withdrawn. What remains true from §8: the SIGILL was the partial rebuild, and
the build script now forces a full rebuild when a glamor patch lands.

Caveat kept explicit: both corrupt-display runs also had the (now reverted) damage-rows change
compiled in, so those two runs cannot separate the two changes on their own. The 4 MMU-VIO faults,
however, appear in the current-Mesa build **with and without** it, and the known-good build shows 0
— which is why the suspicion sits on Mesa. Settling it needs one run of the current-Mesa daemon,
with no DDX change, with the display checked rather than only the log.

**The Pi is left on the known-good binary, re-verified this turn: 0 faults, desktop rendering and
animating.**

---

## 14. The glamor-breaking Mesa delta is in the BUILD, not the source (three suspects cleared)

§13 established that a daemon linked against today's `libv3d-phoenix.a` does not render the desktop.
This turn isolated it properly, one variable at a time.

**1. The DDX is exonerated.** Current Mesa, `fbdev.c` clean against HEAD, display checked rather
than only the log (`rpi4b-uart-20260908-230837-x-mesa-display.log`): flat grey (mean 89.0, std
**0.8** — uniform, not a desktop) then black, 3 fault matches. The known-good build in the same
conditions gives mean 101 / std 68 and 0 faults. So §13's caveat resolves in the DDX's favour, and
the reverted damage-rows change had nothing to do with it.

**2. The `u_vbuf` guard is exonerated — by A/B, against my own hypothesis.** `aa916f2f060` is the
only Mesa commit between the working archive and the broken one, so it was the obvious suspect.
Neutralising just that hunk (`false &&` → `true &&`), rebuilding the archive and relinking gives
**still black** (`rpi4b-uart-...-x-ab-noguard`, 2 fault matches). Reading the code agrees: the guard
only suppresses an index-unrolling *optimisation* inside the indexed-draw branch, and the two
predicates it short-circuits are pure — there is no mechanism for a GPU fault. Both the reasoning
and the experiment now say the same thing.

**3. The other candidate source deltas are inert.** The winsys (`gpu/rpi4-v3d/mesa/`, compiled into
the same archive) has only add-then-revert pairs since 2026-09-07 — net zero. The two uncommitted
files in the Mesa tree (`src/broadcom/meson.build` modified, `src/broadcom/compiler/v3d_shader_dump.c`
untracked) are **not** in the archive and **not** referenced by `build-v3d-phoenix.py` — checked with
`ar t` and a grep, not assumed.

### So the source is fully accounted for, and the difference is in the build

Every source delta between the archive the working daemon was linked against and today's archive is
either exonerated by experiment or provably not compiled in. What is left is the build itself.

**And a sharp narrowing that came free:** today's archive renders **all four Quakes and SuperTuxKart
correctly** — every game capture in the current reel was taken *after* the archive was rebuilt at
12:47. So this is not a general v3d/Mesa regression. It breaks only **glamor's** use of the driver:
2D, many small draws, render-to-texture. That is a much smaller search space than "Mesa broke".

### The reason this is hard, and the lesson

**We never saved the archives the known-good daemon was linked against** — only the daemon binary
(`artifacts/x11/known-good/Xphoenix-glamor-daemon.mesa-e4be116324`). They were overwritten in place
by later rebuilds, so there is nothing to diff or bisect against, and the version string baked into
the binary (`git-e4be116324`) identifies the Mesa *commit* but not the build.

So the practice to adopt: whenever an X daemon is promoted to known-good, save
`tools/.gpu-libs/*.a` **beside it**. A known-good binary you cannot rebuild is a dead end the moment
it needs one change — which is exactly the position glamor work is in now.

Next step, in order: capture a known-good archive set the next time one is proven, then bisect the
build (not the source) against it. Do not spend more Pi cycles guessing at the source; three
suspects have now been cleared and the remaining space is the build.

**State left:** the `u_vbuf` guard is restored and the archive rebuilt with it (verified present in
the source the archive was built from), so a future game build keeps bug #3's fix; the Mesa tree
carries only the two pre-existing uncommitted files; and the demo runs the known-good daemon
(sha256 `8e003ab45ef41009…`), the identical file verified rendering correctly one cycle earlier.

---

## 15. ROOT CAUSE of every X failure in this session: the wrong X server target was built

§8, §13 and §14 chased a display failure through the DDX, a Mesa commit, the winsys and finally
"the build". The last one was right in the most literal sense: **the wrong binary was being built.**

There are two GPU X servers, differing only in how they reach V3D:

```
build-xfbdev.sh --glamor         -> Xphoenix-glamor          links the IN-PROCESS winsys
build-xfbdev.sh --glamor-daemon  -> Xphoenix-glamor-daemon   links libv3d-client (/dev/v3d-srv)
```

`startx_gpu` starts the `rpi4-v3d` daemon, so it requires the `-daemon` build. I ran `--glamor`
every time and copied the result into the `-daemon` slot — so an in-process-winsys X server ran
**alongside** `rpi4-v3d`, two processes driving the same GPU. That is what produced, on different
runs, MMU violations, GPU wedges, SIGILL, a black screen, full-screen vertical stripes and a flat
grey screen.

### How it was found — on the host, no Pi cycle

Both binaries are unstripped static ELFs, so they can simply be compared:

```
             text        data      bss
known-good   23779467   578996   395604
fresh build  23800683   579012   510916     <-- BSS +115,312
```

A +115 KB BSS jump is not a compiler difference, it is a large static object appearing. The symbol
diff named it immediately:

| known-good only | fresh build only |
|---|---|
| `v3d_cli_bo` (98 304 B), `v3d_cli` | `W` (213 112 B), `bo_hist`, `scanout_cpu`, `v3d_submit_lock`, `v3d_phoenix_render_timeouts` |

`v3d_cli_bo` is the daemon client's BO table; `W` is the in-process winsys' global state
(`v3d_phoenix_winsys.c`). Different backends, in a binary whose *name* said daemon.

### Verification

Built correctly as `--glamor-daemon` against **current Mesa** (`aa916f2f06`), staged, and run:
**0 faults, desktop renders and animates** (mean 102, std 68, frame-to-frame diffs 6.6–8.6) —
identical in character to the older known-good.

### What this withdraws

- §13's "current Mesa is not usable for the X server" — **wrong.** Current Mesa is fine.
- §14's "the difference is in the build, and there is nothing left to diff against" — the first half
  was right, the second was not: the *binaries* were always diffable, and that is what settled it.
- §8's "glamor work is not blocked" was right after all, for the wrong reason; §13's withdrawal of
  it is itself withdrawn. **Glamor work is not blocked.**
- The three exonerations stand and were never wrong: the DDX, the Mesa `u_vbuf` guard and the
  winsys deltas were all genuinely innocent. Every one of those A/Bs was measuring a run whose X
  server had the wrong backend.

Note also that §14's "partial libglamor rebuild" explanation for the original SIGILL is no longer
needed and should not be relied on — that run had the wrong backend too. The build-script fix it
prompted (a landed glamor patch forces a full rebuild) is still a real improvement, and the
staleness guard added alongside it caught a genuine stale build the same day; neither claim about
the SIGILL rests on them now.

### Why it took a whole session, and what stops it recurring

The two binaries are within **440 bytes** of each other, their filenames differ by one word, and
the failure looks exactly like a driver regression — so every plausible source suspect got
investigated before the artefact itself. Three defences are now in place:

1. `build-xfbdev.sh` asserts the linked backend matches the requested target (`v3d_cli_bo` present
   for `--glamor-daemon`, absent for `--glamor`) and **fails the build** otherwise.
2. The known-good directory now holds the **archives** beside the binary, so a known-good state can
   be rebuilt and diffed.
3. `artifacts/x11/known-good/README.md` states the two-servers distinction at the point of use.

**Method note worth keeping:** when an artefact misbehaves and every source suspect has been
cleared, diff the artefact. Two `size` invocations and an `nm` diff answered in minutes what four Pi
cycles could not.

---

## 16. Bug #5's mechanism RE-VERIFIED on the correct build — confirmed, and worse

§15 established that every X run this session used a mis-built server (in-process winsys staged into
the daemon slot). That invalidates the *runs* behind §§5a, 9, 10, 11 and 12, so the §11 mechanism —
"two full-screen render targets both classified as the scanout" — had to be re-measured. The
in-process winsys creates its own scanout FBO, so the second RT could simply have been the winsys'.

**It was not.** Re-run with a correctly built `--glamor-daemon` server, current Mesa, 0 faults
(`rpi4b-uart-20260909-014959-x-rsc-correct.log`), 171 large resources logged, of which the render
targets are:

```
rsc #1  1920x1080  rt=1  -> create_flags=0x2
rsc #4  1920x1080  rt=1  -> create_flags=0x2
rsc #9  1024x1024  rt=1  -> create_flags=0x2     <-- not the scanout by any reading
```

**Three** resources take the scanout branch, not two — and the third is **1024×1024**, which cannot
be the display under any interpretation. That is the cleanest possible demonstration that

```c
(bind & PIPE_BIND_RENDER_TARGET) && width0 >= 1024 && height0 >= 768
```

is answering "is this the scanout?" with something that a glamor internal pixmap trips. The
matching `Y_0_TOP` forcing in `st_atom_framebuffer.c:137` is applied on the same test and is
unguarded, so all three render with an inverted viewport while only one is the real display. The
mirror follows.

Also note the mirror **reproduces on the correct build** — the clip-icon artefact is present in the
verified `x-daemon-correct` capture at the same offset and the same 5.03 mean&#124;diff&#124; as in
the owner's original report. Bug #5 is real and is not an artefact of the mis-built server.

### An interim discriminator that is much tighter than the current one

The proper fix is still "ask the winsys which BO actually got the scanout pages" (§11). But this
measurement offers a far better *heuristic* for the meantime: the real scanout is exactly the
framebuffer's mode size. Testing `width0 == mode.width && height0 == mode.height` instead of
`>= 1024 && >= 768` immediately excludes the 1024×1024 case, and it uses information the winsys
already holds. It does not separate `rsc #1` from `rsc #4` — both are 1920×1080 — so it is not the
fix, but it shrinks the wrong set from three to two and is a one-line change.

### ⚠️ Three findings still owed a re-run

These were measured on the mis-built server and are **not** to be relied on until repeated on a
`--glamor-daemon` build:

| finding | why it matters | status |
|---|---|---|
| `glamor_spans` never runs on the screen pixmap (0 hits) | ruled out a fix location | **unverified** |
| `glamor_copy`: one copy per session, neither side the screen pixmap | ruled out a fix location | **unverified** |
| the presentation readback is always `y0=0 rows=1080`, and `fbdevShadowUpdate` is never called | the whole §12 account of the desktop's ~1 fps rests on it | **unverified** |

The third is the load-bearing one: if damage *does* fire on a correctly built server, then §12's
"presentation is a 300 ms full-screen timer" is wrong and the row-accurate flush that was written
and reverted becomes worth having after all. That is the next thing to measure, and it is one cycle.

---

## 17. §12 is WRONG and fully withdrawn: the DDX does not present at all

§16 owed a re-run of §12's account of the desktop's ~1 fps ("presentation is a 300 ms full-screen
timer; the damage path never fires"). Re-measured on a correctly built `--glamor-daemon` server, and
it does not survive.

`fbdevFlushRegion()` has exactly **three** call sites in `tools/x11-port/ddx/fbdev.c`: the damage
extent, the empty-damage full-frame fallback, and the periodic timer. All three were probed in one
build:

| probe | result |
|---|---|
| `fbdevShadowUpdate`, damage non-empty | **0 lines** |
| `fbdevShadowUpdate`, damage empty → full frame | **0 lines** |
| `fbdevFlushTimerCb` tick counter | **0 lines** |
| the timer's one-shot "flush armed" banner | **absent** |

…while the desktop **renders correctly** at 0 faults.

The measurement was verified rather than trusted, because a silent probe is exactly the failure mode
that has bitten this investigation twice: all four format strings are present in the binary that ran
(`strings | grep -c` = 1 each, including a control string known to appear in the log), and the staged
binary's sha256 matches the build tree's. So the silence is real.

**Conclusion: `fbdevShadowUpdate` never runs, the periodic timer never arms, and the DDX presents
nothing.** §12's mechanism — a 300 ms timer whose full-screen readback-plus-write round trip costs
~700 ms — is therefore withdrawn in full. Its `y0=0 rows=1080` observation came from the readback
probe on a mis-built server (§15) and is withdrawn with it.

### What that implies, and what is NOT yet known

If the DDX never calls `fbdevFlushRegion`, it never calls `glamor_phx_screen_readback` either (that
is the only caller, `fbdev.c:366`) — so the screen texture is never copied to the shadow, and the
~8 MB read + ~8 MB write per frame that §12 blamed for the frame rate **does not happen**. The most
likely remaining explanation is that render-to-scanout is genuinely working: the screen pixmap's BO
takes the scanout branch (§16 measured `create_flags=0x2` on it), so the GPU may be storing straight
into the framebuffer's pages with no CPU copy at all.

That is a hypothesis, not a finding. Note it sits awkwardly against `v3d_gpu.c:22`/`:638`, which say
the daemon deliberately leaves `W.scanout_pa` unset so `sel_pa` stays 0 and "the default DRAM path
runs" — i.e. the *daemon* does not hand out scanout pages. Both cannot be true as stated, and which
one is wrong is exactly the next thing to establish.

**So the desktop's ~1 fps currently has no established explanation.** §2c of the weekly log is
withdrawn, not merely flagged. The honest state is: the update rate is measured (~1 update/s, §2)
and the cause is unknown.

**Next step, named:** find what actually writes `/dev/fb0` in the glamor + daemon-client
configuration — grep the daemon client and the daemon for the writer, and check whether the screen
BO really is backed by scanout pages in *this* configuration (the §16 flag says it was requested;
`v3d_gpu.c` says the daemon does not grant it). Measure that before proposing any fix, and do not
reuse any number from §12.

---

## 18. The windowed-GL bottleneck is `XPutImage`, at ~3.6 MB/s over a local socket

§17 left the desktop's update rate unexplained; the per-row screen upload accounted for 1.0 → 2.5
updates/s and no more. Rather than guess at the rest, the demo client now times its four phases.
Measured on hardware, 0 faults, steady over hundreds of frames
(`rpi4b-uart-20260909-033751-x-glphase.log`):

```
gl-x11: frame 360/20000  376.2 ms/frame (draw 1.5  read 10.7  pack 6.3  put 337.2)  2.66 fps
```

| phase | ms/frame | share |
|---|---|---|
| `draw_scene` + `glFinish` | **1.5** | 0.4 % |
| `glReadPixels` (640×480×4 = 1.2 MB, uncached FBO) | 10.7 | 2.8 % |
| CPU repack to the XImage (307 k px, 3 channel scales each) | 6.3 | 1.7 % |
| **`XPutImage` + `XFlush`** | **337.2** | **90 %** |

**The GPU is not the problem and never was** — it renders the frame in 1.5 ms. Nor is the CPU
pixel work: readback and repack together are 17 ms, and both are the uncached-memory pattern that
was worth 10× in `hevc-play`, so there is little left there either.

The anomaly is the transfer: 1.2 MB per frame in 337 ms is **~3.6 MB/s**. A local AF_UNIX
round trip on this hardware should manage two orders of magnitude more; NFS over gigabit does
29.9 MB/s through a far longer path. `XPutImage` is nominally asynchronous, but a 1.2 MB request
does not fit the socket buffer, so the client blocks until the server drains it — which makes this
number the *combined* cost of the AF_UNIX copies and whatever the server does per chunk, and it is
what a re-measurement has to separate.

### Next step, stated precisely

Measure AF_UNIX bulk throughput **in isolation**, not through X: send a few MB between two local
processes and report MB/s. `tools/rpi4-ipcprobe` already exists for AF_UNIX validation and is the
natural place. Three outcomes, each pointing somewhere different:

- **~3–5 MB/s in isolation** → the socket path itself is the bug, and fixing it speeds up *every* X
  client, not this demo. Note the two AF_UNIX defects already found in this port (the EL1 user-copy
  `PROT_USER` COW storm, and `poll()` not being readiness-woken) — a third is plausible.
- **~100 MB/s in isolation** → the socket is fine and the 337 ms is server-side per-request work,
  so the next probe belongs in the server's PutImage path.
- anything between → both, and the split is the useful number.

Do not optimise before that measurement. The per-row upload fix was found by measuring and was
worth 2.5×; the two hypotheses before it were wrong and cost four Pi cycles.

The phase timing is kept in the client: one `fprintf` per 30 frames, and it is the readout that
says whether a future change helped.

---

## 19. AF_UNIX is exonerated (213 MB/s) — and §17's stated mechanism was wrong

§18's branch resolves cleanly. `rpi4-ipcprobe` now measures AF_UNIX bulk throughput in isolation
(8 MiB over a socketpair, writer/reader in separate processes). On hardware:

```
chunk= 65536 B   8.00 MB in 0.038 s ->  213.29 MB/s
chunk=  4096 B   8.00 MB in 0.038 s ->  211.80 MB/s
```

**The socket is not the bottleneck.** It is ~59× faster than the 3.6 MB/s the X path achieves, and
chunk-size-independent, so it is not per-write syscall overhead either. 1.2 MB at 213 MB/s is
5.6 ms; the X path spends 337 ms. So **~331 ms per frame is server-side work**, and the "socket path
is the bug" branch of §18 is closed.

### Correction to §17: the per-row upload was not "one RPC per row"

§17 explained the per-row `glTexSubImage2D` cost as "every GL call is an RPC to /dev/v3d-srv". **That
is wrong.** The daemon client (`libv3d-client.c`) forwards **`phoenix_v3d_ioctl`** — BO allocation,
CL submission — not GL calls. Mesa runs *in the X server's own process*, so `glTexSubImage2D` never
crosses to the daemon and the pixel data never went over a socket.

The measured improvement stands and was verified three ways (1.0 → 2.5 updates/s, Game of Life
12.2 → 17.0 gen/s, `rpi4-v3d` CPU 76.9 % → 14.1 %). What was wrong is the *reason*: the cost is
per-call overhead **inside Mesa** — state validation and, more likely, a map/sync of the destination
BO per call — not inter-process round trips. The `rpi4-v3d` CPU drop is still consistent with that,
since fewer ioctls follow from fewer upload calls.

Worth stating plainly because it is the same error twice in one investigation: a correct fix with a
wrong explanation is still a wrong explanation, and it will mislead the next change.

### Next target, now specific

`glamor_put_image_gl` does **not** bail — it goes through `glamor_upload_region` →
`glamor_upload_boxes`, i.e. the bulk path. So the 331 ms is inside the upload itself, and the
candidate is Mesa's **CPU tiling**: V3D textures are UIF/LT tiled, so a linear upload must be
re-laid-out per pixel by `v3d_store_tiled_image()` (`src/broadcom/common/v3d_tiling.c:481`, called
from `v3d_resource.c:174` and `:459`). That is 1.2 MB of per-pixel address arithmetic per frame,
into a BO that may well be uncached — precisely the pattern that cost 10× in `hevc-play`'s blit and
was fixed there by gathering rows and packing stores.

Measure it before touching it: time `v3d_store_tiled_image` (or the `v3d_resource` transfer that
calls it) per upload and compare against the 331 ms. If it accounts for most of it, the same
treatment that worked for the video blit applies. If it does not, the remaining suspect is the
BO map/sync around the upload.

---

## 20. Mesa's upload path is exonerated too — the 360 ms is WAITING, and the arithmetic names it

§19 predicted Mesa's CPU tiling. Measured both upload sites in one build, on a correct
`--glamor-daemon` server, 0 faults (`rpi4b-uart-20260909-041027`, `…-x-xfer`):

```
v3d-phx: texture_subdata  n=32    map 0.00 ms  store 0.02 ms    1349 px/call  cpp=1
v3d-phx: transfer_unmap   n=2784  0.15 ms/call               287147 px/call  cpp=4
```

Two things fall out.

**The tiling hypothesis is wrong.** `v3d_texture_subdata` runs **32 times in a whole session** at
1349 px with `cpp=1` — that is glamor's glyph cache, not the screen. The screen pixmap never reaches
it, because the full-screen RT is forced RASTER (`should_tile`), so it takes the `!rsc->tiled`
branch into the transfer path.

**And the transfer path is not the cost either.** `transfer_unmap` at 287 147 px/call and `cpp=4`
*is* the 640×480 window upload — and it costs **0.15 ms**. Even at the observed ~10 calls per frame
the entire Mesa upload path is ~1.5 ms/frame, against 360 ms in `put`. So **Mesa is exonerated**,
along with the GPU (1.5 ms), the CPU pixel work (17 ms) and AF_UNIX (213 MB/s).

### Everything that does work is accounted for, so the time is being spent waiting

Nothing in the chain is CPU-bound: the server sits at 37 % of one core of four, the socket is two
orders of magnitude faster than needed, and every stage that touches pixels is now measured in
single-digit milliseconds. A 1.2 MB `XPutImage` taking 360 ms with no component busy means the
request is **blocked**, not computed.

And there is an arithmetic fit worth testing before anything else. This port's `poll()` work records
that AF_UNIX fds are readiness-woken but "the timed loop survives only as the fallback … back at
20 ms". A 1.2 MB request does not fit a socket buffer, so it is drained in chunks:

```
1.2 MB / 64 KiB  = 18.75 chunks
18.75 x 20 ms    = 375 ms      vs measured put = 337-360 ms
```

That is close enough to be worth one targeted test and too close to be coincidence. **Hypothesis:
the X server's client fd is taking the 20 ms polling fallback rather than being readiness-woken, so
every chunk of a large request costs a full poll tick.** If true it is a Phoenix-level defect that
slows *every* X client in proportion to request size, and the desktop's sluggishness is one symptom.

### The one test that settles it

Instrument the server's request read loop: count `read()` calls per `XPutImage` and time the wait
before each. Three outcomes:

- **~19 reads, ~20 ms apart** → confirmed; fix the readiness wakeup for this fd class, and expect
  the whole desktop to get faster, not just this window.
- **~19 reads, back-to-back** → the socket buffer is the limit and the fix is a larger buffer or a
  chunked client; the 360 ms would then need another explanation.
- **1 read** → the request is not chunked at all and the wait is elsewhere in dispatch.

Note the count-per-frame anomaly to check at the same time: 2784 unmaps over ~275 frames is ~10
uploads per frame for a single 640×480 window. If that is real rather than an artefact of averaging,
the client is being asked to upload the same region ten times over, which is its own bug.

**Four hypotheses have now been refuted by measurement in this investigation** (spans, copy,
readback, CPU tiling) and one confirmed (the per-row upload, worth 2.5×). Measuring first has been
cheaper than patching first every single time.

## §21 — the residual 355 ms IS the AF_UNIX buffer: 4 kB, ~512 round-trips per frame

§20 predicted "~19 reads, ~20 ms apart" if the poll fallback were to blame. Both
halves are wrong, and the instrumented server says so precisely.

One `startx_gpu action` cycle, `Xphoenix-glamor-daemon` with probes in
`os/io.c` (before each `_XSERVTransRead`) and `os/WaitFor.c` (around
`ospoll_wait`), 0 faults:

```
[phx-io]   reads=187392  mean gap 0.785 ms  mean 118852 B/read   <- B = buffer OFFERED
[phx-wait] waits=186880  timeouts=85975 (46%)  mean blocked 0.384 ms
gl-x11: frame 360/20000  395.9 ms/frame (draw 2.8  read 10.7  pack 6.3  put 355.2)  2.53 fps
```

Differencing consecutive cumulative means gives the steady state directly:
**512 reads per 401.9 ms** = 0.785 ms/read. The frame is 395.9 ms. So the reads
are not *part* of the frame cost, they *are* the frame cost — 512 of them, and
nothing is left over to attribute elsewhere. The wait probe kills the 20 ms
theory outright: mean blocked is 0.384 ms, not 20 ms.

Why 512? `sources/phoenix-rtos-kernel/posix/unix.c:29`

```c
#define US_DEF_BUFFER_SIZE SIZE_PAGE     /* 4096 on aarch64 */
#define US_MAX_BUFFER_SIZE 65536U
```

Every AF_UNIX socket gets a **one-page circular buffer**, and the SOCK_STREAM
send path (`unix.c:1104`) does a plain `_cbuffer_write(&r->buffer, buf, len)` —
it copies only what currently fits and returns short, then blocks. A 640x480
XPutImage is 1.2 MB, so 1.2 MB / 4 kB = ~293 fill/drain round-trips; measured
512 including request headers and short fills. Right on the nose.

The 118852 B figure was a probe bug on my side: it recorded
`oci->size - oci->bufcnt`, the buffer the server *offers*, not what `read()`
returns. The offered buffer was never the constraint.

### The fix, and why it needs no kernel change

`SO_RCVBUF` is honoured (`unix_setsockopt`, `unix.c:1300`) and
`unix_bufferSetSize` resizes **this socket's own** `s->buffer` — which is exactly
the buffer the peer writes into (`unix.c:1104` writes into the *remote's*
buffer). So the receiving end is the correct end to set it on, and for
client→server request traffic that is the X server's accepted fd. No kernel
edit, no `--scope core`, no boot-regression risk.

`tools/x11-port/patches/xorg-server-21.1.24-os-client-rcvbuf.patch` sets
`SO_RCVBUF = 65536` in `EstablishNewConnections` immediately after
`_XSERVTransAccept`, before `AllocNewConnection`. That placement is load-bearing:
`unix_bufferSetSize` reinitialises the cbuffer and **discards its contents**, so
it must run before any client byte arrives. `SO_SNDBUF` is *not* implemented
(`-ENOPROTOOPT`), which is fine — this direction does not need it.

Also generalised `core_src_newer_than_archive()` in `build-xserver-core.sh` to
watch `os/`, `dix/` and `hw/kdrive/src/` and not only `glamor/`. The
already-built early return had let an unpatched binary ship twice before; an
`os/connection.c` change would have been the third.

### Prediction, recorded before the cycle

- 4 kB → 64 kB is 16x fewer round-trips: **reads/frame 512 → ~35**
  (1.2 MB / 64 kB = 18.75, plus headers).
- **put 355 ms → 25–40 ms**, **frame 396 ms → 50–70 ms** — draw 2.8 + read 10.7
  + pack 6.3 are untouched and become the dominant terms.
- If reads/frame drops 16x but frame time does **not**, the per-round-trip cost
  is the poll wakeup path rather than the buffer, and the in-read/outside split
  probe is the next instrument.
- If reads/frame does **not** drop, the setsockopt did not take effect (clamp,
  wrong end, or reinit-after-data) — read it off `[phx-io]`, do not assume.

Caveat worth stating up front: `rpi4-ipcprobe` measured 213 MB/s over a
socketpair on this *same* 4 kB buffer — ~19 µs per round-trip, not 0.785 ms. So
round-trips are not inherently expensive; the difference is almost certainly that
ipcprobe's reader spins while the X server goes through `ospoll_wait` every time
(mean blocked 0.384 ms). If that holds, the buffer fix is **necessary but not
sufficient**, and lands nearer 50 ms/frame than 25.

## §22 — the mirror artefacts: signature confirmed, then fixed by opting glamor OUT of the size gate

### The crop test that settled the mechanism

I had read the owner's bottom-left "mirrored clippy" two different ways in the
same session — *content copied to the mirrored screen position* versus *content
flipped in place* — and those are different bugs. The static HDMI frame
`artifacts/hdmi/20260909-025215-xclean-tick.png` settles it without a Pi cycle.

Scanning the left 70 px strip for non-background rows finds exactly two icon
bands: **y 0–63** and **y 1018–1079**. Window Maker draws its Clip once, at
top-left; nothing belongs at bottom-left. Cropping both 64×64 tiles:

| comparison | mean abs diff |
|---|---|
| bottom vs top, as-is | 51.4 |
| bottom vs **vflip(top)** | 30.5 → **6.85** at dy=+2 |
| bottom vs hflip(top) | 64.2 |
| bottom vs rot180(top) | 57.6 |

6.85 with a two-pixel offset is an exact match through HDMI capture noise. And
the tile sits at 1016–1079, which for H=1080 is precisely `y' = H-1-y` of
0–63. So it is **both**: the box was written to its Y-mirrored screen position
*and* with its rows reversed — one single `y' = H-1-y` transform applied to a box
on the screen pixmap, while everything around it was not.

(The GoL-xterm band could not be confirmed the same way — MAD 112 against the
mirror source — which is expected: `top` is live, so the mirrored band is a stale
snapshot of what it showed when the bad transfer happened. The static Clip is the
clean instance.)

### Why that transform existed at all

`st_atom_framebuffer.c` forces `Y_0_TOP` for any FBO `>= 1024x768`, on the theory
that a full-screen FBO is the scanout surface. glamor's screen pixmap is
1920x1080 and so was caught by it — but it is **not** scanout-backed: it is a
plain GL texture that the DDX presents by `glReadPixels` into a shadow it
`write()`s to `/dev/fb0`. Forcing `Y_0_TOP` therefore put every glamor GL path
into a flipped coordinate world, which needed **three** hand-rolled compensating
flips to come out upright: the `glamor_transfer.c` upload flip, the matching
download flip, and `PHX_READBACK_FLIP_Y` in the shim. Any path that missed one
emitted its box at `y' = H-1-y`. That is the artefact, and it is a *structural*
consequence of the arrangement, not a bug in one function — which is why hunting
for the specific caller was the wrong move.

### The fix (X path only; the games are untouched)

`phx_scanout_flip_gate`, a new `int` in `st_atom_framebuffer.c` defaulting to
**1**, now gates the heuristic. `glamor_phoenix_ctx.c` sets it to **0** before
`st_create_context`, so the X server's screen pixmap stays `Y_0_BOTTOM` — exactly
what upstream Mesa would give it — and all three compensators are deleted:

* `xorg-server-21.1.24-glamor-screen-upload-yflip.patch` → `reverted_core_patches`
* `xorg-server-21.1.24-glamor-screen-upload-bulk.patch` → `reverted_core_patches`
  (it only ever made the *mirrored* upload one `glTexSubImage2D` instead of one
  per row; upstream's unflipped path is a single bulk call already, so reverting
  it **keeps** the 2.5× and drops the scratch buffer)
* `PHX_READBACK_FLIP_Y` 1 → 0

`libv3d-phoenix.a` is shared with the five games, but the flag defaults to 1 and
only the X shim clears it, so the games' behaviour is bit-identical and their
binaries are not even relinked. This is deliberately **not** the full work order
(`docs/misc/2026-09-08-flipy-scanout-gate-work-order.md`): that replaces the size
test with a real scanout predicate for *everyone* and needs a 6-app soak. This
change needs one X cycle, and cannot regress a game.

### Prediction, recorded before the cycle

Two coherent end-states exist, and they differ by one `#define`:

* **S1 — what is built:** gate off, transfers unflipped, readback unflipped. If
  glamor lays pixmap row 0 into texture row 0 the way it does upstream, the
  desktop renders upright and **the mirrored Clip tile at y 1016–1079 is gone**.
* **S2 — if the whole desktop comes back upside down:** my readback convention is
  inverted; put `PHX_READBACK_FLIP_Y` back to 1 and nothing else. glamor is still
  internally consistent (all its paths agree, only presentation flips), so the
  artefact class is fixed in S2 as well.

Either way the fix holds; the cycle only picks which. The check is the same crop:
scan the left strip for icon bands and expect **one**, not two. A *partial* fix —
some artefacts gone, some not — would refute the structural account and mean
there is a second, independent bug.

### Result — confirmed, and it was S1

One `startx_gpu action` cycle, probe-free build, **0 faults**:

| | before | after |
|---|---|---|
| bottom-left tile vs vflip(top-left Clip), best MAD | **6.85** (exact mirror) | **74.98** (unrelated) |
| frame | 112.6 ms | **105.8 ms** |
| `XPutImage` | 77.6 ms | **70.1 ms** |
| fps | 8.88 | **9.45** |

S1 as predicted: the desktop renders upright with the readback flip off, so
glamor does lay pixmap row 0 into texture row 0 the way it does upstream. The
mirrored Clip is gone, the GoL xterm shows only Game of Life (no white blocks, no
mirrored `top` text), and all six clients render.

It also got *faster*, which was not predicted but follows: the retired bulk patch
still had to build a mirrored copy in a scratch buffer before uploading, and
upstream's unflipped path uploads the client's rows directly.

**My band-count check was a bad discriminator** and reported FAIL on a frame that
was actually correct: Window Maker legitimately keeps app icons at bottom-left, so
"one icon band" was never the right test. The content comparison is, and it is
unambiguous. Recording this because the wrong check nearly cost a good result.

**Cumulative for the X desktop this session: 395.9 → 105.8 ms/frame (3.7×), 355.2
→ 70.1 ms on the XPutImage path (5.1×), and the mirror artefacts fixed.**

### Follow-up this leaves open

* `tools/.gpu-libs/{libv3d,libGL}-phoenix.a` were rebuilt with `--force`. The five
  game binaries were **not** relinked, so nothing about them changed in this
  cycle — but the next full image build will relink them against these archives,
  and that build is where the games want re-verifying.
* The residual ~60 ms/frame of byte-proportional server-side work (1.2 MB at
  ~20 MB/s) is untouched and independent of both fixes.
* The full work order (a real scanout predicate replacing the size test for
  *everyone*, removing Quake II's compensating un-flip) is still open and still
  wants the 6-app soak. This change deliberately did not go there.
