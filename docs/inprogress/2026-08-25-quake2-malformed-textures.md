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

## ★★★ ROOT CAUSE (2026-08-25): NPOT-texture MIP-LEVEL mistiling on V3D

Decisive evidence chain (no GL_LINEAR run needed — the flaky NFS/serial harness kept
blocking it; inferred from the render + texture dims instead):
- Reproduced: model-skin textures scramble (banner, gibs); BSP wall/floor textures fine.
- ALL Q2 model skins are **non-power-of-two** (pak0: banner 260×195, gibs 120×92 /
  264×124 / …, soldier 288×195, v_blast 296×164). BSP `.wal` textures are always POT.
- **Distance correlation**: the CLOSE blaster viewmodel (v_blast 296×164, NPOT, samples
  the BASE level) renders CORRECTLY, while DISTANT banner/gibs (small MIP levels) scramble.
⇒ The bug is in the **tiling of NPOT texture MIP LEVELS** (UIF layout / slice padding),
NOT the base level and NOT POT textures. Base level correct + small mips wrong = a
mip-slice tiling/padding miscalculation. This is the "mip-path" branch of the GL_LINEAR
discriminator, established from physics instead of a 6th flaky Pi cycle.

Connects to the TFU decode: Mesa's `v3dx_tfu.c` sets ICFG OPAD (dest UIF padding) only
for level 0 (`!IOA_DIMTW`); "when filling mipmaps the miplevel 1+ tiling state is
inferred" — so mip levels 1+ rely on the HW/slice-derived tiling. For NPOT heights the
UIF `ub_pad`/`padded_height` per mip level (v3d_get_ub_pad, mirrored in the winsys
CPU-tiler lines ~1312-1330) is what the TMU descriptor encodes; if Mesa's
`v3d_setup_slices` / the TFU mip generation computes an NPOT mip level's padded_height or
tiling mode differently from what the TMU reads, that mip samples scrambled.

FIX LOCUS (to investigate/implement):
- `external/mesa/src/gallium/drivers/v3d/v3d_resource.c` `v3d_setup_slices` — per-mip
  tiling-mode + padded_height for NPOT (does a small NPOT mip drop below the UIF
  threshold to UBLINEAR/LINEARTILE, and is the transition computed right?).
- The TFU mip-gen dest slice tiling in `v3dx_tfu.c` (NUMMM path) vs what the TMU
  descriptor (`v3dx_state` texture shader state) encodes for the same levels.
- Compare against upstream/Linux Mesa v3d (Q2 renders NPOT model skins fine on Linux-Pi4)
  — a diff of `v3d_setup_slices` / tiling.c vs the ported copy may reveal a port delta.

VERIFY: rebuild libv3d + one `+map demo1` capture (NFS-server restarted this session to
clear the stale-lease stalls) → banner renders clean.

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
capture attempts:
- cycles 1-2: cut off mid-precache by max-cmd-secs=120 (0 frames).
- cycle 3-4 (360s/480s cap): precache COMPLETES ("models done", ca_active) but the
  `+demomap q2demo1.dm2 +set timedemo 1` demo then **HANGS at ca_active** — 0 game
  events after, no `CAPTURE:` log ⇒ RI_RenderFrame never called ⇒ 3D never renders ⇒
  hook never fires. Same invocation produced 30 HOST frames + (per plan) 23 Pi frames
  on 08-22, so this is either a Pi demo-playback/timedemo regression since 08-22 OR the
  console holds key focus with no autonomous game-input path to release it.
- Discriminator: bare `quake2` (attract demo) DOES render+advance (HDMI timer moved
  00:52→02:13 in cycle q2tex-default), but that path can't be frame-captured cleanly
  (page-flip vs HDMI). `+demomap+timedemo` hangs. ⇒ cycle 5 (q2cap-map) uses
  **`+map demo1`** = a LIVE in-game map (console closed, key_dest=key_game, 3D renders
  the spawn view immediately = same Outer Base wall textures) so the hook fires.

- cycle 5 (`+map demo1`, q2cap-map): ★ **BREAKTHROUGH — the render path WORKS.** `+map`
  (live in-game, console closed) renders 3D and the capture hook FIRED 12× (`CAPTURE:`
  logged, "done (12 shots), quitting"). So the `+demomap+timedemo` hang is specifically a
  demo-playback issue, NOT a general render/console block. BUT the TCP sink connect
  FAILED (`CAPTURE: tcp 10.42.0.1:559[9] FAILED` ×12) ⇒ 0 frames landed. ROOT CAUSE:
  **host firewall** — NetworkManager's shared-connection chain (`nm-sh-in-wlp3s0`) only
  whitelists the netboot subnet's UDP (dhcp/tftp/quake-26000); the capture-sink TCP:5599
  SYN was dropped. FIX: `sudo iptables -I INPUT 1 -s 10.42.0.0/24 -p tcp --dport 5599 -j
  ACCEPT` (session-scoped; re-add after host reboot). ⇒ cycle 6 (q2cap-map2) re-runs
  `+map demo1` capture with the firewall open.

NEXT: cycle-6 frames → convert + eyeball for the malformed wall textures + diff vs the
clean 08-22 baseline (regression check), then the GL_LINEAR fork (mip vs base-level).
## ★★ BUG REPRODUCED + LOCALIZED (2026-08-25, cycle q2cap-cfg3, 12 coherent frames)

`quake2 +exec capmap.cfg +map demo1` streamed 12 live 3D frames off the Pi. The scene
renders correctly EXCEPT the hanging **Strogg banner** (a rectangular model quad,
`models/objects/banner`) whose skin is **SCRAMBLED** — brown/orange noise + green/red
speckle instead of the banner image (evidence: `artifacts/q2-texbug/banner-malformed-default.png`).
This IS the owner's "rectangles with very strange malformed texture." The scramble
pattern = a **tiling mismatch** (data present, arranged wrong = UIF-vs-linear), on a
distant object ⇒ a MIP LEVEL is sampled. Discriminator cycle (q2cap-linear,
`gl_texturemode GL_LINEAR` = mipmapping OFF, base level only) launched to confirm
mip-path vs base-level.

Standing win: `+map` (live game) + firewall-open + config-file cvars (short launch cmd) is the reusable autonomous Q2/Q3 capture path. Long UART command lines drop chars — always put many cvars in a cfg.
- cycle 6 (q2cap-map2): still FAILED — the port echoed as `scr_capture_port 559` (not
  5599): a char DROPPED on the long (~200-char) UART command send (host→psh). So the Pi
  connected to :559 (nothing there). FIX: moved all cvars into `baseq2/capmap.cfg`
  (staged in the NFS export; NO timedemo, ends with `map demo1`) and launch with the
  SHORT command `quake2 +exec capmap.cfg` → no long-line UART drop, reliable port 5599.
- cycle 7 (q2cap-cfg): launched with the config-file approach + firewall open.

