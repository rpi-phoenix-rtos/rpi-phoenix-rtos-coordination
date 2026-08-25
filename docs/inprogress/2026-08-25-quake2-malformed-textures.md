# Quake2 malformed-texture bug (owner #1) — analysis + next experiment

Owner report (2026-08-25, log `artifacts/rpi4b-uart/20260825-135418-live-test.log`):
launched `quake2` → **demo1 (Outer Base, maps/demo1.bsp)** attract demo; used
`]viewpos` → **position `-141 974 -81`, angles `67 133 0`**; saw "rectangles with
very strange malformed texture" (some wall textures scrambled; most of the scene fine).

## Evidence already in the owner's log (winsys TFU diagnostics)

`v3d_phoenix_winsys.c ioc_submit_tfu` prints a produced-tiling verdict + a `vcheck`
probe per TFU job. In the owner's demo1 precache (lines 546-559):

| tex     | icfg        | ioa FORMAT | verdict       | vcheck                    |
|---------|-------------|------------|---------------|---------------------------|
| 64×128  | 0x003808e0  | 6=UIF_NO_XOR | **LINEAR!**  | VERTICAL-MISMATCH 2/6     |
| 64×64   | 0x003808c0  | 6=UIF_NO_XOR | **LINEAR!**  | VERTICAL-MISMATCH 2/6     |
| 32×32   | 0x003808a0  | 6=UIF_NO_XOR | UIF-VERIFIED | 5/5                       |
| 16×16   | 0x00340880  | n/a (small)  | n/a          | (too small)               |

Decode: `iofmt=(ioa>>3)&7=6` ⇒ **Mesa REQUESTED UIF_NO_XOR** for the 64×128, but the
verdict says the TFU **produced LINEAR** (dst[16]==src(16,0), != src(4,0)). Per the
code's own 3-way comment that is the "Mesa-asked-UIF + produced-LINEAR ⇒ TFU ignored
IOA (winsys/HW)" case — i.e. a real tiling bug that would scramble the wall texture.

## CONFIRMED (2026-08-25, source decode): the VERTICAL-MISMATCH is a false positive on MIP jobs

Mesa builds TFU ICFG in `external/mesa/src/gallium/drivers/v3d/v3dx_tfu.c` (V3D42):
src FORMAT = `RASTER(0)` if the source slice is raster, else `LINEARTILE + (tiling -
LINEARTILE)`. The FORMAT field is **6 bits (23:18)** — but the winsys decodes only
`(icfg>>18)&0xf` (4 bits, `v3d_phoenix_winsys.c:1306`). Decoding the owner's 64×128 job
`icfg=0x003808e0` with the full 6-bit mask: `(0x003808e0>>18)&0x3f = 14 = UIF_NO_XOR`.
⇒ the **source is UIF-tiled**, i.e. these 64×128/64×64 jobs are **mipmap-generation**
TFU jobs (base UIF level → mip levels), NOT raster→UIF base uploads. The vcheck's
`src[y*w+x] = raster pixel(x,y)` assumption is therefore invalid for them ⇒ the
`VERTICAL-MISMATCH` + `TILING=LINEAR!` verdicts are **confirmed false positives on mip
jobs** (32×32 "UIF-VERIFIED" is a coincidental pass — small single-UIF-block image).
This rigorously validates the prior session's claim. The real corruption is NOT what
this probe flags.

⇒ The bug is in the **mip-level content** (wrong data sampled at distance) OR the
base-level upload — exactly the `gl_texturemode GL_LINEAR` (mipmapping OFF) fork.

## HDMI is NOT a usable capture path here (2026-08-25 cycle q2tex-default)

Ran `quake2` (demo1) with dense HDMI ticks: the grabs show the Q2 **console overlay**
(`]` prompt + logo) with a pure-black lower half — the page-flipped 3D scanout
(log: "3 buffer(s) TRIPLE-BUFFER+page-flip") does not reach the HDMI grab reliably
(same class as the O2/O3 HDMI-vs-page-flip issue). Use the coherent frame-dump instead.

## Also: the 08-22 Pi timedemo was CLEAN

`artifacts/quake2-compare/` = the **Pi** side (compare script takes `--pi`/`--host`;
host frames are `/tmp/quake2-host`, 30 frames). All 23 archived Pi frames render clean
(SSIM 0.993 vs host). So either the deterministic q2demo1 timedemo doesn't sample the
buggy view, OR a regression landed 08-22→08-25 (the 08-22 capture predates the O2
scanout-RASTER fix coord 571d57f / mesa 34a448d6a29 — check if that touched tiling).

## DECISIVE NEXT EXPERIMENT (autonomous, 1-2 Pi cycles) — the advisor's discriminator

`gl_texturemode GL_LINEAR` is a **launch-time cvar** (no in-game input): GL_LINEAR
disables mipmapping ⇒ removes all the mip-level (tiled-source) TFU jobs, leaving only
base-level raster→UIF uploads.

**Capture path RESOLVED** — the coherent hook is `external/yquake2/src/client/refresh/gl1/gl1_sdl.c`;
it dumps every Nth **in-game 3D frame** and (on the Pi) streams `[u32 idx][u32 len][TGA]`
over TCP to `scripts/quake-capture-sink.py`. Cvars: `scr_capture` (every Nth),
`scr_capture_max` (N shots then EXIT), `scr_capture_host` (host IP; empty=file mode),
`scr_capture_port` (5599). The `quake2` ram-stage wrapper (`tools/yquake2-port/quake2-launcher.c`)
FORWARDS argv, so `quake2 +set ...` works.

1. Host: `python3 scripts/quake-capture-sink.py --out /tmp/quake-pi-q2-default --port 5599 &`
2. Pi (netboot, card out), via test-cycle-psh-interact, one command (mirror
   `quake2-host-capture.sh` 72-76 so frames pair 1:1 with the host ref + the 08-22 Pi set):
   `quake2 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 +set r_vsync 0 +set cl_particles 0 +set fixedtime 50000 +set timedemo 1 +set scr_capture 5 +set scr_capture_max 40 +set scr_capture_host 10.42.0.1 +set scr_capture_port 5599 +demomap q2demo1.dm2`
3. Repeat with `+set gl_texturemode GL_LINEAR` added → sink `--out /tmp/quake-pi-q2-linear`.
4. Convert + eyeball both sets (PIL, as this session). **Malformation gone with
   GL_LINEAR ⇒ mip-level path**; **persists ⇒ base-level** raster→UIF upload.
5. Also diff the current default Pi frames vs the 08-22 clean set → confirms/denies an
   08-22→08-25 regression.

## Fix locus (once discriminated)

`tools/v3d-driver-port/v3d_phoenix_winsys.c` TFU path (or `external/mesa` v3d resource
tiling in `v3d_resource_setup`/`v3d_tiling.c` if Mesa chooses the wrong tiling for
64-wide levels). A `VKQ_CPU_TILE` CPU-tiler fallback already exists (compiled out) for
the raster→UIF case — but it explicitly does NOT handle tiled-source mip jobs, so it is
NOT a fix if the bug is mip-path.

## GOTCHA (2026-08-25): capture cycle needs a LONG --max-cmd-secs

Q2 precache is slow on Pi (~66 models, each with TFU texture uploads doing heavy
per-submit cache flushes → ~2s/model → full precache >200s). The psh-interact harness
`--max-cmd-secs` (default 120) powers off the Pi mid-precache (dies ~model 55/66, 0
frames streamed). Use `--max-cmd-secs 360 --idle-secs 360` (Bash timeout 600000) so
precache + the timedemo + all 40 captures complete before power-off.

## Status
Source analysis done (TFU mismatch = confirmed false-positive on mip jobs). Coherent
capture in progress: 1st/2nd cycles cut off mid-precache by max-cmd-secs=120 (0 frames);
3rd cycle (q2cap-d3) launched with 360s cap. NEXT: inspect streamed frames → GL_LINEAR fork.
