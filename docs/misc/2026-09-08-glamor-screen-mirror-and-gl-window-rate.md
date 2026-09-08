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
