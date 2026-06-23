# Overnight progress — 2026-06-23 (autonomous)

## CORRECTION (later 2026-06-23): multiplayer ATTEMPT broke single-player → REVERTED

The multiplayer work below was **reverted** — it regressed the single-player flagship (Quake
hung at the demo / never rendered). **Committed end state = working single-player flagship**
(Loopback-only, FIONREAD default, EZ on, colour/alpha/QUIT fixed). Reverts: coord `4c65180`,
lib-lwip `8da7fc2`, phoenix-rtos-lwip `e247acb`. HW-validated demo renders (QSFPS ~60, no error).

**What happened (task #26):** registering the UDP LAN driver (Phase 0) made Quake's per-frame
net poll service incoming UDP; a stray BCM2711-LAN packet triggers `UDP_CheckNewConnections` →
`ioctl(FIONREAD)` → fatal `ENOSYS` Quake Error (intermittent — an early boot got lucky). The
FIONREAD enablement (`LWIP_FIONREAD_LINUXMODE` + value→int) made the *server* start but ALSO
regressed the NFS-backed Quake load (demo never rendered, QSFPS=0). So multiplayer needs BOTH a
working FIONREAD AND one that doesn't break NFS, plus a non-fatal UDP poll — a careful combined
re-work, deferred. Details + plan in task #26.

---
### (HISTORICAL, reverted) Quake LAN multiplayer attempt
1. **Driver registration** (coord `13702d1`, REVERTED 4c65180): added Datagram + UDP LAN drivers
   in `pl_phoenix_stubs.c`. → "UDP Initialized", menu worked — but destabilized single-player.
2. **FIONREAD fix** (lib-lwip `4d24465` + phoenix-rtos-lwip `67380fa`, REVERTED 8da7fc2/e247acb):
   lwIP FIONREAD was `_IOR('f',127,unsigned long)` vs libphoenix's `int` (different command
   numbers); fixed value→int + opt.h LINUXMODE default→1 → server started, but regressed NFS.

**Remaining for full multiplayer:** re-do the above without breaking NFS + non-fatal FIONREAD,
then the actual 2-machine connect handshake (attended; the dev host
NIC is cabled to the Pi).

## Investigated + documented (queued for fresh context / you)

- **#28 Torch flame black-triangles** (you reported): the flame is a GLSL alias model
  (`gl_glsl_alias_able=true`), so it's NOT my recent GL_FLAT/EZ/LEQUAL/alpha changes (those don't
  apply to the GLSL alias path). Pre-existing V3D GLSL-alias-shader lighting bug — leading
  suspect: pose-lerped vertex normal / `anorm_dots` approximation / `GL_BYTE` normal decode
  producing ~0 light for some verts/frames. Deep V3D-shader investigation; details in task #28.
- **#24 Mouse** (needs you): root-caused — the mouse's xHCI interrupt URB is armed (`slot=2
  pend=1`) and EP config is identical to the working keyboard, but the controller posts no
  transfer-completion events for the mouse's interrupt endpoint. The build has an improved
  diagnostic (`XFER-EVT-INTR slot=2 ep=3`) that captures interrupt events — **move the mouse on
  the next boot and check the UART** to confirm delivery vs scheduling. Deep xHCI + attended.
- **#11 cacheable-RX DMA, #12 dir-reorg**: still deferred (coherency/brick risk; scoped in tasks).
- **Bigger items** (Vulkan Tier-1, X11 server, WiFi #91, Bluetooth): large multi-session efforts;
  not started overnight — better with fresh context than half-implemented while deep in this run.

## Current image
`artifacts/rpi4b/rpi4b-sd.img` (last build SHA `9aebc274…`, netboot variant): EZ on, correct
colours/alpha, QUIT→fbcon, multiplayer working, + the uncommitted xHCI mouse diagnostics (debug,
throttled; kept for the attended mouse-move test). The validated GLQuake render fixes + Early-Z +
NFS-true-root work from earlier are all committed with manifest `2026-06-22-glquake-render-fixes-ez-reenable`.

## Why I wound down rather than push bigger items
This has been an extremely long single session; the remaining work is either attended (mouse,
2-machine multiplayer), deep V3D-shader (torch), or large multi-session (Vulkan/X11/WiFi). Pushing
those this deep into one context risks a fatigue regression to the now-working flagship, which is
the opposite of valuable. Everything delivered is committed; everything queued is documented with
a concrete next step.
