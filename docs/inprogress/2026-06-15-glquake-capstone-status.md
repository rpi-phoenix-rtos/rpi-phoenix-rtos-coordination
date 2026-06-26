# GLQuake capstone — status + path to a fully stable, usable build (2026-06-15)

> **STATUS (2026-06-26): most "usable" blockers below are now RESOLVED — GLQuake is the
> working flagship.** Since this doc: keyboard input (§1, kbd0 ownership), mouse-look (§3,
> #24 — usbmouse 3-byte boot-report fix), QUIT/fbcon restore (#25), LAN multiplayer net
> drivers (#26), NFS-root + manual launch (#27) and the torch-flame alias-model fix (#28)
> all landed and are HW-validated (see `2026-06-25-hw-validation-results.md` and
> `2026-06-26-overnight-results.md`). FPS (§2) was already solved (render-to-scanout, ~40 fps).
> **Genuinely still open:** §4 audio (SNDDMA — DMA mechanism proven, audible sign-off is an
> attended bench item) and §5/§6 minor visual-polish + a formal multi-boot soak. Kept as the
> remaining-polish reference; the interactivity items are done.

Evaluation of the open topics after the textured-world / live-map / water /
input-decoder / NFS-perf work. Goal: a **stable, high-performance, usable**
GLQuake build on Pi4 Phoenix + V3D.

## Done (committed, HW-validated this cycle)

- **Textured 3D world** renders on the V3D — root cause of the long gray-world
  bug was back-face culling (missing `glFrontFace(GL_CW)` from the excluded
  `gl_vidsdl.c`). coord `e3c43ca`, external/quakespasm `5f02adb`.
- **Live single-player `map`** — registered the loopback net driver
  (`pl_phoenix_stubs.c`); `map start` spawns the server + QuakeC VM + connects +
  renders the live "start" hub. coord `4d9919f`. (The documented QuakeC pointer
  crash was already fixed by prior work.)
- **Water rendering** — `r_oldwater 1` (classic per-vertex warp); the modern
  warpimage path needs `glCopyTexSubImage2D` (unimplemented on V3D) → was RGB
  noise. external/quakespasm `ff17470`, coord `f5e1d4e`.
- **Keyboard input decoder** — `pl_phoenix_in.c` parses `/dev/kbd0`'s cooked
  ASCII / ANSI-arrow stream into `Key_Event`+`Char_Event`; boot self-test 11/11.
  coord `b921fd1`. **Blocked at runtime** — see below.
- **NFS fast + reliable** — see `2026-06-15-nfs-network-perf-results.md`
  (~8.5 MB/s, reliable rapid reboots) → faster pak0/map loads.

## Open topics → path to "usable"

### 1. Keyboard input — `/dev/kbd0` ownership (HIGHEST for "usable")
The decoder is ready but `open("/dev/kbd0")` returns **EBUSY**: `pl011-tty`'s
`pl011_kbdthr` holds it exclusively to bridge USB keys into the serial/psh
console, and `usbkbd` is single-opener. Options:
- (a) **Gate the pl011-tty kbd→console bridge** off for the Quake netboot variant
  (it boots straight into Quake, so the console bridge is unused there). Smallest
  change; needs a per-variant gate (build flag or runtime condition) since a
  global gate would regress psh-USB-kbd (#122) for other boots.
- (b) **Make `usbkbd` multi-reader** (per-client FIFOs) so both the bridge AND
  Quake can read. Proper fix; touches the USB daemon (attended-flagged) + needs
  keypress validation.
- **Unattended-verifiable** either way: the boot log flips from
  `/dev/kbd0 open failed (EBUSY)` to `keyboard input active`. Actual menu/play
  navigation is **attended** (physical keypresses). → Do (a) next.

### 2. Performance / FPS — SOLVED (CORRECTED: caches were never the issue)
> **CORRECTION (2026-06-17):** "caches are globally off (TD-16)" is **stale/false** —
> TD-16 was RESOLVED 2026-05-17; caches are ON. The ~12 fps was the present path
> (uncached tiled V3D readback + CPU blit), not a global cache switch.

FPS is now ~40-42 @1080p: render-to-scanout + linear RT + cacheable readback +
cross-core pipelined present (see `2026-06-15-td16-cache-enable-plan.md` "WIN" notes).
The only residual cache lever is the *uncached GENET RX DMA pool* for NFS bandwidth
(Policy B, attended, cable-gated) — narrow and separate from FPS.

### 3. Mouse look — after keyboard
`usbmouse` → `/dev/mouse0`, same EBUSY-class ownership + attended keypress/move
validation. `IN_Move`/`IN_MouseMotion` shims exist as stubs.

### 4. Audio — deferred
`SNDDMA_Init` returns false (silent). Needs the PWM/I²S audio driver (attended
bench rig). Lower priority for "usable"; Quake plays fine silent.

### 5. Visual polish — minor
Gamma `pow(x,0.5)` brighten needs a matched host frame to retune (attended);
image y-flip is cosmetic; water fixed. Low priority.

### 6. Stability — looks good; soak to confirm
0 faults across many boots, demos cycle demo1→2→3, `map start` stable. Worth a
formal multi-boot soak once input lands.

## Recommended order
1. **Free `/dev/kbd0` for Quake** (option 1a) + verify the open succeeds
   (unattended) → hand off keypress validation. This is the gate to interactivity
   = the biggest "usable" step.
2. **Mouse** (same unblock pattern) once keyboard is confirmed.
3. ~~TD-16 cache spike for FPS/load~~ **DONE** — FPS solved via render-to-scanout
   (see §2 correction); the only residual cache lever is the uncached GENET RX pool
   (Policy B, attended, NFS-bandwidth only, cable-gated).
4. Audio (DMA mechanism proven 2026-06-17, devices 7bdb1c4; Quakespasm SNDDMA wiring
   next), gamma retune, soak — polish.

The remaining "usable" blockers are dominated by **attended** steps (keypress
validation, audible audio sign-off). Unattended progress = unblock `/dev/kbd0`
+ verify, then prep mouse + audio SNDDMA wiring.
