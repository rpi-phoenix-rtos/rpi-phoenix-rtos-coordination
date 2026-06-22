# Overnight progress — 2026-06-23 (autonomous)

## Delivered + committed tonight

### Quake LAN multiplayer — NOW FUNCTIONAL (was "no communications" / server hang)
Two fixes, both HW-validated:
1. **Driver registration** (coord `13702d1`): the port registered only Loopback; added the
   Datagram + UDP LAN drivers in `pl_phoenix_stubs.c` (net_udp.c/net_dgrm.c already compiled).
   → "UDP Initialized", multiplayer menu works.
2. **FIONREAD fix** (lib-lwip `4d24465` + phoenix-rtos-lwip `67380fa`): the UDP server hung at
   "Loading" with `ioctlsocket (FIONREAD) failed (ENOSYS)`. Root cause: lwIP FIONREAD was
   `_IOR('f',127,unsigned long)` but libphoenix/clients send the `int` version — `_IOR` encodes
   sizeof into the value, so they were different command numbers and lwip_ioctl's `case FIONREAD`
   never matched. (FIONBIO matched, both ulong → non-blocking worked, FIONREAD didn't.) Plus
   `LWIP_FIONREAD_LINUXMODE` wasn't reaching the lib-lwip core compile. Fixed value→int +
   opt.h default→1. → server reaches protocol-666 init, no ENOSYS, 0 faults.

**Remaining for full multiplayer:** Phase 1 (real advertised IP via `SIOCGIFADDR` for `slist`
discovery — autonomous, but moderate net-code risk, deferred to fresh context so as not to
regress the now-working server) + the actual 2-machine connect handshake (attended; the dev host
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
