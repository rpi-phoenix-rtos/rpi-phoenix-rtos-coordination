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

## BUT — why the prior session called the vcheck a false positive (must verify, not re-assert)

The mismatching jobs have **`icfg` src FORMAT `(icfg>>18)&0xf = 0xE` (NON-raster)**,
whereas the CPU-tiler/vcheck raster-source assumption (`src[y*w+x] = pixel(x,y)`) only
holds when src FORMAT == 0 (RASTER). A tiled→tiled (in-place **mip**) TFU job with a
tiled source makes BOTH the "LINEAR!" verdict and the vcheck compare against the wrong
bytes ⇒ a **plausible false positive** for those specific jobs. The 32×32 UIF-VERIFIED
job is the base-level raster→UIF upload (assumption holds) and passes.

⇒ Source analysis is INCONCLUSIVE on its own: the mismatch is either (a) a real
mip-level tiling bug, or (b) a diagnostic artifact on tiled-source mip jobs. The
owner's REAL visible corruption means *something* is wrong regardless.

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

1. Host: `python3 scripts/quake-capture-sink.py --out /tmp/quake-pi-q2-default --port 5599`
2. Pi (netboot, card out): launch yquake2 on **q2demo1** with the TCP-sink capture
   cvars + `timedemo 1` + `fixedtime` (mirror `scripts/quake2-host-capture.sh` lines
   72-76), DENSER sampling than the 08-22 run so the buggy view is captured. **TODO:**
   confirm the Pi-side sink cvar spelling (`scr_capture_host`/`scr_capture_port`?) —
   grep the yquake2 capture patch (not found under a plain name; likely a build define
   or in the SDL/refresh glue). `quake2-launcher.c` is the ram-stage `quake2` wrapper —
   check whether it forwards argv to yquake2 (so `quake2 +set gl_texturemode GL_LINEAR`
   works) or run `/usr/bin/yquake2` directly.
3. Repeat with `+set gl_texturemode GL_LINEAR` → /tmp/quake-pi-q2-linear.
4. Convert + eyeball both sets (PIL, as in this session). **Malformation gone with
   GL_LINEAR ⇒ mip-level path** (fix the tiled-source mip TFU tiling; the vcheck
   false-positive is then masking a real mip bug). **Persists ⇒ base-level** raster→UIF
   upload (the 32×32-verified path fails at 64-wide; look at TFU icfg/ioa for w=64).
5. Also compare the CURRENT default-run Pi frames vs the 08-22 clean set → confirms/denies
   an 08-22→08-25 regression.

## Fix locus (once discriminated)

`tools/v3d-driver-port/v3d_phoenix_winsys.c` TFU path (or `external/mesa` v3d resource
tiling in `v3d_resource_setup`/`v3d_tiling.c` if Mesa chooses the wrong tiling for
64-wide levels). A `VKQ_CPU_TILE` CPU-tiler fallback already exists (compiled out) for
the raster→UIF case — but it explicitly does NOT handle tiled-source mip jobs, so it is
NOT a fix if the bug is mip-path.

## Status
Source analysis done + banked. NEXT: execute the discriminator Pi cycle above.
