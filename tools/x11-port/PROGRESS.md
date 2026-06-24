# X11 (tinyx/kdrive) library port — progress

## UPDATE 2026-06-24 (assist): `startx desktop` (twm + managed xeyes) + kbd0-ordering verified

Host-side + NFS-staging only (no Pi boot this session — UART reserved for the orchestrator).

### Task 1 — twm + a managed client in one session (`startx desktop`)
`pl_phoenix_xlaunch.c` now drives a **list of clients** instead of one. The
single-foreground-binary model is unchanged (psh has no job control): bring the
server up first, wait for the listening socket, then vfork+execve each client
with `DISPLAY=:0`, then supervise. Design chosen = a reserved client name in the
existing "startx" convenience branch (argc < 4), so every prior form is byte-for-byte
unchanged and the new mode lands without touching the explicit `<X> <fontdir> <client>`
path or adding a flag that would collide with the `argc >= 4` parse:

```
startx              -> <prefix>/bin/xeyes                (unchanged)
startx twm          -> <prefix>/bin/twm   (WM only, bare root — unchanged)
startx /bin/2048    -> /bin/2048                          (unchanged)
startx desktop      -> twm (WM)  +  xeyes (managed, decorated, draggable)   <-- NEW
```

`desktop` expands to the list `[twm, xeyes]`: twm is launched first and given a
250 ms settle so it is in its dispatch loop and ready to reparent/decorate xeyes'
window when it maps. xeyes is launched with `-geometry 300x200+360+240` so twm
**auto-places** it: the compiled-in twm config has no `RandomPlacement`, so a window
carrying no position hint triggers twm's *interactive* placement (a rubber-band
outline that follows the pointer until the user clicks to drop it) — the geometry
supplies a `USPosition` hint that twm honours immediately, so the user sees a
decorated xeyes at once rather than an outline. All argvs + the shared DISPLAY=:0 env
are built in the parent before any vfork (vfork-safe). Supervisor semantics
generalized: **server death kills all clients and exits; any single client exit
(incl. twm) leaves the server + other clients running** so the painted root persists.
New vfork-live locals marked `volatile` (same false-positive `-Wclobbered` rationale
as before).

**The command the user runs (netboot, NFS at /nfstest):**
```
mkdir /tmp                 # if not already
/nfstest/bin/startx desktop
```
(`/nfstest/bin/startx` is the launcher under its convenience name; `argv[0]`==startx
+ argc<4 triggers convenience mode.) Equivalent fully-explicit invocation is NOT
available for the multi-client case — use `startx desktop`. Single-client still works
either way (`startx`, `startx twm`, or the explicit 3-arg form).

Rebuilt static aarch64-phoenix ELF, **0 undefined symbols, 0 warnings**:
`artifacts/x11/pl_phoenix_xlaunch` (664736 bytes), staged to BOTH
`/srv/phoenix-rpi4-nfs/bin/pl_phoenix_xlaunch` AND `.../bin/startx` (startx is a
plain copy, not a symlink — `build-xlaunch.sh` now refreshes both automatically).

**Expect on UART:** `xlaunch: startx mode — prefix=/nfstest client=desktop (2 clients)`,
the `[fbdev] /dev/fb0: ...` line, `xlaunch: server socket present...`,
`xlaunch: starting client[0]: /nfstest/bin/twm`, `xlaunch: starting client[1]: /nfstest/bin/xeyes`.
twm will also log a few `unable to open font "-adobe-helvetica-bold-..."` warnings —
the `misc`-only font path has no helvetica, so twm falls back to `fixed` (which IS in
that path, since the server itself started). Those warnings are EXPECTED and harmless,
not a failure. **Expect on HDMI:** xeyes at ~360,240 inside a twm titlebar/border on
the slategrey twm root (auto-placed via the `-geometry` hint — NOT an outline waiting
for a click); the mouse drags the window by its titlebar (mouse is HW-proven) and the
eyes track the pointer. Left-click on the bare root pops twm's "TWM" (defops) menu.

### Task 2 — minimal twmrc: SKIPPED (per task guidance)
twm's compiled-in default config (`src/twm-1.0.12/src/system.twmrc` → deftwmrc) already
binds **Button1-on-root → the "defops" menu** and paints an icon manager, so a root menu
exists with no config file. The only thing a custom `.twmrc` would add is `f.exec`
launching apps — `f.exec` forks a shell from twm, whose viability on Phoenix (no job
control, uncertain shell-fork) can't be verified host-side. So per the task's own
guidance we rely on the launcher running the client directly (`startx desktop` maps
xeyes for twm to manage) rather than shipping a twmrc. The default menu's `Xterm` entry
is a harmless dead link (no xterm on the export).

### Task 3 — keyboard-in-X readiness: ORDERING VERIFIED CORRECT, no code change
Read the vendored kdrive/dix source to confirm (not guess) the lifecycle:
- `dix/main.c`: `InitOutput()` (line 193) runs BEFORE `InitInput()` (line 250).
- `hw/kdrive/src/kdrive.c` `KdInitOutput()` calls `(*card->cfuncs->cardinit)(card)`
  (line 955–956) = our `fbdevCardInit`, which calls `fbdevConsoleSetMode(FBCON_DISABLED)`
  (fbdev.c:144) — this is what frees `/dev/kbd0` from the pl011-tty console bridge.
- The keyboard is opened later in `fbdevKeyboardEnable` (fbdev.c:591), reached via
  `InitInput`. So **fbcon-disable strictly precedes the kbd0 open.** A 40-try / 1 s retry
  loop in Enable additionally absorbs the asynchronous console release. No fix needed.

**HW test the orchestrator should run:**
1. Boot netboot; at psh: `mkdir /tmp` (if needed) then `/nfstest/bin/startx desktop`.
2. On UART, confirm the keyboard line is the SUCCESS form, not EBUSY:
   - GOOD: `[fbdev] /dev/kbd0 opened (fd=N), RAW HID mode — keyboard active`
   - BAD:  `[fbdev] /dev/kbd0 open failed (Device or resource busy) — keyboard input disabled`
   Also confirm `[fbdev] HDMI console fbcon mode -> 0` appears BEFORE the kbd0 line.
3. On HDMI: xeyes inside a twm-decorated frame on the slategrey root; move the mouse →
   eyes track; drag the titlebar → window moves.
4. Keyboard: focus matters under twm (default is click/pointer-to-focus). With xeyes
   focused, type — xeyes ignores keys, so for a visible keyboard proof prefer launching
   a key-echoing client; alternatively `startx 2048` then use arrow keys. The kbd0
   "keyboard active" UART line is the primary readiness proof; on-screen key effect needs
   a client that consumes keys.

---

## UPDATE 2026-06-23 (assist): xinit-style launcher `pl_phoenix_xlaunch` built

psh has no job control (`&` is a redirect), so a server + client cannot be
started from one psh session. New foreground launcher solves this:
`tools/x11-port/launcher/pl_phoenix_xlaunch.c` + `tools/x11-port/build-xlaunch.sh`.
It vfork+execve's the server (`:0 -ac -nolisten tcp -fp <fontdir>`), polls for the
listening socket `/tmp/.X11-unix/X0` (~10 ms, ≤10 s, abort-on-server-death via
`waitpid(WNOHANG)`), then vfork+execve's the client with `DISPLAY=:0` (env built in
the parent — vfork-safe, no setenv in the child), then blocks in `waitpid` on both
(server dies → kill client + exit). Spawn idiom mirrors psh/pshapp.c + psh/runfile.c
(vfork + execv + waitpid). `/tmp` and `/tmp/.X11-unix` are mkdir'd up front.

Built static aarch64-phoenix ELF, **0 undefined symbols** (libc only):
`artifacts/x11/pl_phoenix_xlaunch` (also staged to NFS export `/bin/pl_phoenix_xlaunch`).
(The vfork-live locals are marked `volatile` to suppress GCC's `-Wclobbered`, which
fires because at -O2 the env/argv builders inline into main() alongside the vfork
calls; it is a false positive — the children only execve/_exit, never writing parent
memory — but volatile documents the constraint cleanly for upstreaming.)

**HW recipe (main agent, netboot, NFS export at /nfstest):**
```
mkdir /tmp                         # if not already
/bin/pl_phoenix_xlaunch /nfstest/bin/Xphoenix /nfstest/usr/share/fonts/X11/misc /nfstest/bin/xeyes
```
(`/bin/pl_phoenix_xlaunch` is on the NFS root; pass absolute NFS paths for the three
args.) Expect on UART: `xlaunch: starting server...`, the `[fbdev] /dev/fb0: ...` line,
`xlaunch: server socket present...`, `xlaunch: starting client: .../xeyes (DISPLAY=:0)`.
Expect on **HDMI**: xeyes' two eyes painted on the black X root (eyes won't track —
pointer input is still a stub, that's fine; the paint is the proof). For twm later:
stage a `twm` ELF to `/nfstest/bin/` and swap the last arg.

Risks: if Xphoenix names its socket differently than `/tmp/.X11-unix/X0`, the readiness
poll times out but the launcher proceeds best-effort (server-reaches-dispatch is
HW-proven) so the client still launches — confirm the socket name on the run.


**Goal:** a software X server (kdrive `Xfbdev`) on the Pi 4 `/dev/fb0`, per
`docs/todo/tinyx-x11-demo.md`. The OS prereqs are done (AF_UNIX HW-validated, fb0,
USB HID kbd+mouse). The remaining cost is porting the X11 **library stack** (it is
NOT in `phoenix-rtos-ports` — PR #82 never landed). This dir cross-compiles that
stack for aarch64-phoenix, bottom-up.

**Isolation (hard rule):** builds into `/tmp/x11-phoenix` only. X11 is **never** added
to the rpi4b default components / any `ports.yaml` that feeds the flagship netboot
image. Host-side build work only — no Pi boots, no flagship rebuilds.

Build: `tools/x11-port/build-x11-phoenix.sh` (fetches x.org release tarballs;
needs host internet; idempotent).

## Ladder + status (2026-06-17)

| Brick | Status | Notes |
|---|---|---|
| xorgproto 2023.2 | ✅ builds | headers-only; 129 headers + 29 `.pc` installed |
| libXau 1.0.11 | ✅ builds | `libXau.a` aarch64-phoenix; config.sub knows `phoenix` |
| xtrans 1.5.0 | ✅ builds | transport headers (AF_UNIX path is the Phoenix-relevant one) |
| libXdmcp 1.1.5 | ✅ builds | `libXdmcp.a` |
| xcb-proto 1.16.0 | ✅ builds | host python codegen; `.pc` in `share/pkgconfig`, `xcbgen` under `local/lib/python3.x` |
| libpthread-stubs 0.5 | ✅ builds | provides `pthread-stubs.pc` (pthread is in Phoenix libc → stubs are no-ops) |
| libxcb 1.16 | ✅ builds | `libxcb.a` + ~24 extension libs (randr/render/shm/shape/xfixes/xkb/xinput/…); needed 2 Phoenix-gap patches (below) + `--disable-mitshm` |
| **libX11 1.8.7** | ✅ builds | **the core Xlib** (`libX11.a`, 2.2 MB). Needed: `xorg_cv_malloc0_returns_null=no` cache, `-DMAXHOSTNAMELEN=256`, `-DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS` (POSIX getpw*_r path) + libphoenix getpw*_r/sys/poll.h (below) |
| libXext 1.3.5 | ✅ builds | `libXext.a` |
| libXrender 0.9.11 | ✅ builds | `libXrender.a` |
| pixman 0.42.2 | ✅ builds (lib) | `libpixman-1.a` (3.1 MB) + `.pc`; only pixman's *test* `utils.c` fails (its `gettime` clashes with Phoenix's non-standard `sys/time.h` `gettime`), so build `make -C pixman install` |
| zlib 1.3.1 | ✅ builds | `libz.a` (libfontenc needs zlib.h) |
| freetype 2.13.2 | ✅ builds | `libfreetype.a` (minimal: no zlib/png/harfbuzz/bzip2/brotli) |
| libfontenc 1.1.8 | ✅ builds | `libfontenc.a` |
| libXfont2 2.0.6 | ✅ compiles | `libXfont2.a` built + headers installed. Needed `-DO_NOFOLLOW=0`, `-DNOFILES_MAX=256`, `ac_cv_lib_m_hypot=yes` + libphoenix `hypot` (6e2b929). A font *tool* link needs the hypot symbol → resolves after the libphoenix rebuild. |
| **kdrive fbdev server (Xphoenix)** | ✅ **COMPILES + LINKS** (2026-06-23) | fresh fbdev DDX (`hw/kdrive/fbdev/fbdev.c`) → static aarch64-phoenix `Xphoenix` ELF (7.1 MB, 0 undefined syms). Opens `/dev/fb0`, `RPI4FB_GETMODE`, shadow-FB, `write()`-blit on damage. NOT yet run on HW (needs fonts staged + libphoenix on-device). See "SERVER — Xphoenix LINKS" below. |

## Toolkit base — BUILDS (2026-06-18)

The toolkit-base libraries now cross-compile: **libICE, libSM, libXt, libXmu, libXpm** (`.a` each).
- **libICE** needed `patches/libICE-1.1.1-phoenix.patch` (drop old-K&R `long time();` vs `<time.h>`).
- **libXt/libXmu** needed the same MT-safe-pwd + `-DMAXHOSTNAMELEN` defines as libX11; **libXt** also
  needed the libphoenix `alloca.h` fix (`#include <stddef.h>`, committed).
- **libXpm** lib builds (its sxpm/cxpm tools link the deferred symbols → lib-only install).
- **libXaw** (Athena widgets) — ✅ **BUILDS** (`libXaw7.a` + `libXaw6.a`, 2026-06-19). Unblocked by
  adding the standard wide-char functions to libphoenix (`wchar.c` had only `wcscmp`, 2026-06-18): committed
  `wcsncpy`, `wcscpy`, `wcscat`, `wcschr`, `wcsrchr`, `wcsncmp`, `wmemcpy`, `wmemmove`, `wmemset`,
  `wmemcmp`, plus `mbtowc` (libphoenix `0cb9f72`). So the **entire** X11 client/render/font/toolkit
  library stack now cross-compiles — **44 archives** in `/tmp/x11-phoenix/lib`.

### THE EXECUTABLE BOUNDARY — CROSSED (2026-06-18) ✅
Every X **executable** links libc, so it needs the libphoenix additions present as SYMBOLS in
`libc.a`/`libm.a`. **All the libc gaps are now committed** (getpwnam_r/getpwuid_r/sys-poll `89d1543`,
hypot `6e2b929`, wide-char `0cb9f72`, full C-locale multibyte set mblen/mbtowc/wctomb/mbstowcs/wcstombs
`e29c840`) and a `--scope core` libphoenix rebuild puts them in the on-device `libc.a`. **Two non-obvious
gotchas were resolved:**
1. **The toolchain bundles its OWN libphoenix/libc/libm** under
   `.toolchain/aarch64-phoenix/aarch64-phoenix/lib/`, and the auto-linked libc comes from THERE, not the
   build sysroot. After a libphoenix change you must sync that bundle (build script's
   `sync_toolchain_libc()` does it: `cp <sysroot>/lib/lib{phoenix,c,m}.a <toolchain>/lib/`).
2. **App configure needs `PKG_CONFIG="pkg-config --static"`** so the static private deps (xcb/Xau/Xdmcp/
   Xrender) land on the link line, plus `LDFLAGS -L$SYSROOT/lib`.

**FIRST X11 EXECUTABLES BUILT for aarch64-phoenix (2026-06-18):**
- **`xprobe`** — a minimal Xlib client (XOpenDisplay). **RUN-VERIFIED ON HW (2026-06-18):** staged on the
  NFS export, exec'd via scripted psh on the Pi 4, it printed
  `xprobe: XOpenDisplay returned NULL (no server) — but Xlib linked + ran` — i.e. the X client stack +
  libc **execute** on real hardware (not just link). Catches any runtime libc bug (mbstowcs/wctomb/…)
  that linking can't. 0 faults.
- **`twm` 1.0.12** — a complete X11 **window manager**, the first real X11 *application* ported to
  Phoenix. 3.1 MB static aarch64-phoenix ELF (`main`/`XOpenDisplay`/`XtToolkitInitialize` present),
  exercises the full toolkit (libXmu/libXt/libXext/libX11/libxcb/libSM/libICE) + libXrandr. Binaries
  archived in `artifacts/x11/`. They cannot RUN until the X **server** exists, but the entire
  client+toolkit+app+libc link closure is now proven. Build with `build-x11-phoenix.sh --with-apps`.

## ★★★ SERVER — `Xphoenix` (kdrive fbdev DDX) COMPILES + LINKS for aarch64-phoenix (2026-06-23)

**A complete, static `Xphoenix` X-server ELF now links** (7.1 MB aarch64-phoenix, **0 undefined
symbols**). This is the sole remaining new-code piece — the kdrive **fbdev DDX backend** — written
fresh because xorg-server 1.20 ships no Xfbdev (removed in 1.17). Host-side; no Pi boot.

**New code:**
- `tools/x11-port/src/xorg-server-1.20.14/hw/kdrive/fbdev/fbdev.c` — the DDX. Provides the full
  `KdCardFuncs fbdevFuncs` lifecycle (cardinit/scrinit/initScreen/finishInitScreen/createRes/
  scrfini/cardfini/closeScreen/get+putColors) + the DDX entry hooks dix/os require
  (`InitCard`/`InitOutput`/`InitInput`/`OsVendorInit`/`ddxProcessArgument`/`ddxUseMsg`/
  `ddxInputThreadInit`) + **no-op stub keyboard/pointer drivers**. Modelled structurally on
  `hw/kdrive/ephyr/ephyr*.c` but the "host" is the bare framebuffer device.
  - `fbdevCardInit`: `open("/dev/fb0")` + `RPI4FB_GETMODE` → geometry (1024x768x32 pitch 4096).
  - `fbdevScreenInit`: depth 24 / bpp 32 / TrueColor masks (BGRX) from the mode; **always shadow**
    (`KdShadowFbAlloc` — /dev/fb0 has no live `mmap(fd,0)`, issue #149). Shadow byteStride
    (width*bpp/8 = 4096) == device pitch, so the blit is a straight copy.
  - `fbdevShadowUpdate` (the custom `ShadowUpdateProc`): on each damage, `lseek()`+`write()` the
    damaged scanline band of the shadow buffer out to `/dev/fb0` (one syscall when strides match;
    falls back to row-by-row for rotation). Wired via `createRes`→`KdShadowSet`.
- `tools/x11-port/build-xfbdev.sh` — compile+link helper (idempotent). Mirrors the kdrive
  `KDRIVE_CFLAGS`/`KDRIVE_LIBS` from `hw/kdrive/ephyr/Makefile`; wraps the 25 core archives in
  `-Wl,--start-group … --end-group` (circular dix/os/mi/fb refs); links the X11 lib stack from
  `/tmp/x11-phoenix/lib` (+ `-lmd` for SHA1, `-lxkbfile`). `main()`/`dix_main` come from
  `dix/libmain.a` (we do NOT define our own main). `build-x11-phoenix.sh --with-server` calls it.
  A `--stub` mode (fbdev_stub.c, empty hooks) was used first to de-risk the archive link-closure.

**Verified:** `nm Xphoenix` → `main`/`dix_main`/`InitOutput`/`KdInitOutput`/`InitCard`/`InitInput`/
`fbScreenInit`/`shadowAdd`/`fbdevShadowUpdate` all `T`; `grep " U "` empty (fully resolved static
exe); compiles `-Wall`-clean. Binary archived at `artifacts/x11/Xphoenix`.

**Screen-add correctness (important, not just a link detail):** `OsVendorInit` must NOT pre-create a
card. `KdInitOutput` (kdrive.c) only runs its default-screen fallback — `InitCard(0)` +
`KdScreenInfoAdd` + `KdParseScreen(screen, 0)` — when `kdCardInfo` is still NULL. If `OsVendorInit`
called `InitCard(0)` itself, that fallback is skipped → a card with **no screen** is registered →
`cardinit`/`scrinit` (the /dev/fb0 open + GETMODE + shadow alloc) never run and dix bails. So
`OsVendorInit` is intentionally empty; the no-arg run gets a native-mode screen from KdInitOutput,
and an explicit `-screen WxH` gets card+screen via `KdProcessArgument`. `KdParseScreen(.,0)` leaves
width/height 0, which `fbdevScreenInitialize` then fills from the /dev/fb0 native mode.
`screen->softCursor = TRUE` is forced in scrinit (no HW-cursor backend).

**Honest ceiling:** *links*, NOT yet *runs*. Before it can paint on HW the main agent must (see the
report's launch recipe): (1) rebuild libphoenix `--scope core` so the on-device libc carries the X
gaps (getpw*_r/hypot/wide-char/sys-poll) — already committed, just needs to ship; (2) stage X font
files (`fixed`, `cursor`) + a `font-dirs`/`fonts.dir` or pass `-fp` — dix `InitFonts` FatalErrors
without a default font; (3) stage `Xphoenix` + run it on the Pi. Input is stubbed (no real
`/dev/kbd0`/`/dev/mouse0` yet). xkb: relies on built-in defaults (no xkbcomp on-device).

## ★★ SERVER CORE — ENTIRE xorg-server core + kdrive DDX core COMPILE for aarch64-phoenix, 0 errors (2026-06-18)

**The full server-side core now cross-compiles cleanly to static aarch64-phoenix archives.**
Host-side, no Pi boots. After the "371 objects" milestone below, the remaining finite os-layer gap
list was cleared and `make` now produces **28 `.a` archives with zero compile/link errors** — the
complete X server core plus the **kdrive DDX core** (`libkdrive.a`, exports `KdInitOutput`/
`KdScreenInit`/`KdAddScreen`):

```
dix(+libmain) os render mi fb(+wfb) Xext(+hashtable+Xvidmode) Xi(+Xistubs) xkb(+xkbstubs)
xfixes composite damageext dbe exa present randr record config
miext/{damage,rootless,shadow,sync}  hw/kdrive/src(libkdrive.a)
```

`miext/shadow` (`libshadow.a`) is built — that is exactly the shadow-framebuffer layer the future
fbdev DDX blits from. **The only thing NOT produced is a server *binary*** — correct and expected:
xorg-server 1.20 ships no Xfbdev DDX, so the `main()`/`KdCardFuncs`/`KdScreenFuncs` backend that
`write()`s a shadow FB to `/dev/fb0` is the sole remaining **new-code** step. Everything it links
against now compiles.

**How the gap list (below) was cleared — two clean, upstreamable fixes + one configure decision:**
- `setlinebuf` → added as a **static inline** in libphoenix `<stdio.h>` (commit libphoenix `91cdbfd`).
  NOTE: a `-Dsetlinebuf(f)=...` *CFLAGS macro* does NOT work — the parens/commas break the libtool
  shell command line (`syntax error near unexpected token '('`), silently failing every compile.
  Function-like-macro gaps must be real header declarations, not `-D` hacks.
- `openssl/sha.h` → built **`libmd.a`** (public-domain Steve-Reid SHA1 with the BSD libmd API:
  `SHA1_CTX`/`SHA1Init`/`SHA1Update`/`SHA1Final`; `tools/x11-port/libmd-phoenix/`, verified against the
  `SHA1("abc")` test vector) and configured `--with-sha1=libmd` (configure: `SHA1 implementation...
  libmd`). No fake stub.
- `O_NOFOLLOW`/`SI_USER` → `-DO_NOFOLLOW=0 -DSI_USER=0` (both are guard values, not behaviour).
- `sys/ipc.h` (Xephyr `hostx.c` SysV-SHM) → **`--disable-xephyr`**. Xephyr is a *nested* server (needs
  a host X to connect to) and can never run standalone on Phoenix; the fbdev DDX is the real target,
  so dropping Xephyr is correct, not a workaround. This also removed the `htons`/xdmcp gaps (already
  `--disable-xdmcp`). `ifa_broadaddr` did not recur (the access.c path it sat in compiles as-is).

**Exact working configure** (host-side; `PKG_CONFIG="pkg-config --static"`,
`PKG_CONFIG_PATH=$PREFIX/{lib,share}/pkgconfig`):
`--enable-kdrive --disable-xephyr --with-sha1=libmd --disable-{xorg,xwayland,xnest,xvfb,dmx,glamor,
dri,dri2,dri3,glx,int10-module,vgahw,vbe,xdmcp,xinerama,systemd-logind,secure-rpc,config-udev,
config-hal,unit-tests} --without-{dtrace,systemd-daemon}`,
`CFLAGS=--sysroot=$SYSROOT -I$PREFIX/include -DMAXHOSTNAMELEN=256 -DXOS_USE_MTSAFE_PWDAPI
-D_POSIX_THREAD_SAFE_FUNCTIONS=200809L -DO_NOFOLLOW=0 -DSI_USER=0`,
`LDFLAGS=--sysroot=$SYSROOT -L$PREFIX/lib -L$SYSROOT/lib`.

**Honest ceiling:** this is *compiles + archives*, NOT *runs*. No server binary exists yet (needs the
fbdev DDX = new code) and it is unvalidated on HW (the Pi is off). The next step is purely additive
new code: a minimal kdrive fbdev DDX on `hw/kdrive/src` (a `main()` + `KdCardFuncs`/`KdScreenFuncs`
using `miext/shadow` + `write()` to `/dev/fb0`) + a kdrive input driver (`/dev/kbd0`+`/dev/mouse0`,
`tools/quakespasm-port/platform/pl_phoenix_in.c` is the HID→event reference) + xkb data.

---

## ★ SERVER — kdrive xorg-server CONFIGURES + COMPILES 371 objects for aarch64-phoenix (2026-06-18)

Real progress on the server (the last X11 gate), host-side (no Pi boots). Chose **xorg-server 1.20.14**
(autotools; kdrive core + Xephyr — note Xfbdev was removed in 1.17, so a fbdev backend must be written on
the kdrive core, see below). Built the remaining server-side X libs into `/tmp/x11-phoenix`: **libxkbfile
1.1.3**, and the **xcb-util family** (xcb-util 0.4.1, xcb-util-image 0.4.1, xcb-util-renderutil 0.3.10,
xcb-util-keysyms 0.4.1, xcb-util-wm 0.4.2) — Xephyr's deps. With those, **`./configure` SUCCEEDS** (kdrive
+ Xephyr enabled; Makefiles generated for `hw/kdrive/{src,ephyr}`). Configure flags that worked:
`--enable-kdrive --enable-xephyr --disable-{xorg,xwayland,xnest,xvfb,dmx,glamor,dri,dri2,dri3,glx,
int10-module,vgahw,vbe,systemd-logind,secure-rpc,config-udev,config-hal,unit-tests} --without-{dtrace,
systemd-daemon}`, `CC=aarch64-phoenix-gcc`, `CFLAGS=--sysroot=$SYSROOT -I$PREFIX/include
-DMAXHOSTNAMELEN=256 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L`,
`PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig`.

**`make` compiles 371 objects** (the dix/os/Xi/render/... server core largely builds for aarch64-phoenix!)
then hits a clean, finite set of **Phoenix os-layer gaps** — the exact remaining porting work:
- `os/access.c`: `struct ifaddrs` lacks `ifa_broadaddr` → add the union member to libphoenix `ifaddrs`, or
  `#if __phoenix__` skip the broadcast-addr code (server interface enumeration).
- `os/osinit.c`: `SI_USER` undeclared (libphoenix `signal.h`/siginfo), `setlinebuf` implicit (stdio) →
  add `SI_USER` + `setlinebuf` to libphoenix, or guard.
- `os/utils.c`: `O_NOFOLLOW` undeclared → `-DO_NOFOLLOW=0` (same fix as libXfont2).
- `os/xsha1.c`: `openssl/sha.h` missing → configure `--with-sha1=libsha1`/CesCH built-in, or provide sha1.
- `os/xdmcp.c`: `htons`/`IN_MULTICAST` implicit → `<arpa/inet.h>`/`<netinet/in.h>` gaps; or `--disable-xdmcp`.
- `hw/kdrive/ephyr/hostx.c`: `sys/ipc.h` missing (SysV IPC/shm) → `--disable-mitshm` / drop the shm path.

**Remaining server road (multi-session):** (1) clear the os-layer gaps above (mostly small libphoenix
additions + CFLAGS/configure flags) → finish the compile + link a kdrive binary; (2) **write a minimal
fbdev DDX backend** on `hw/kdrive/src` (KdCardFuncs/KdScreenFuncs: shadow-FB + `write()`-blit to
`/dev/fb0`, no `mmap(fd,0)`) since 1.20 has no Xfbdev; (3) a kdrive input driver reading `/dev/kbd0`+
`/dev/mouse0` (the `pl_phoenix_in.c` HID→event logic is the reference) + xkb data; (4) run on HW. The
foundation (50 X archives + a configuring/compiling server) is the delivered, de-risked milestone.

## Server frontier — scout (2026-06-18)

The hard remaining piece is the X **server** (the libraries above are the client/render/font side).
Scout finding: **modern xorg-server (master) no longer ships the kdrive `fbdev` backend** — `hw/kdrive/`
has only `ephyr` (Xephyr, runs on another X) + `src` (the kdrive core) + `meson.build`; `hw/kdrive/fbdev`
(Xfbdev) was removed years ago. So a stock recent xorg-server can't build Xfbdev.

Paths to a software X on `/dev/fb0` (for the next, attended-or-multi-session effort), best-first:
1. **Restore/write a minimal kdrive fbdev backend** on the modern kdrive core (`hw/kdrive/src` +
   a small `fbdev` card/screen driver) using the shadow-FB + `write()`-to-`/dev/fb0` model
   (no `mmap(fd,0)`; per `docs/inprogress/2026-06-05-fb0-attended-decisions.md`). Cleanest long-term.
2. Use the **PR #82 tinyx tree** (`phoenix-rtos-ports#82`) if it carries a restored Xfbdev — cherry-pick.
3. An **old xorg-server (~1.13)** that still has `hw/kdrive/fbdev` — but decade-old autotools/deps.

Prerequisites before any server build: (a) **rebuild libphoenix** so the on-device libc carries the new
symbols (getpwnam_r/getpwuid_r/hypot/sys/poll.h); (b) the server needs xkb data (xkeyboard-config +
xkbcomp) or `-DXKB_DFLT_*` + a built-in keymap; (c) a kdrive input driver reading `/dev/kbd0` +
`/dev/mouse0` (the `pl_phoenix_in.c` HID→event logic is a proven reference). Expect a further stream of
Phoenix libc gaps (the server is the most OS-integrated component). **This is the multi-session frontier;
the 36-archive client/render/font foundation above is the delivered, de-risked milestone.**

## Findings / cross-compile recipe (proven)

- Toolchain `aarch64-phoenix-gcc 14.2.0` + sysroot
  `.buildroot/_build/aarch64a72-generic-rpi4b/sysroot`. `config.sub` accepts
  `--host=aarch64-phoenix` (no triple-faking needed).
- Static libs (`--disable-shared --enable-static`) — matches the Phoenix static-link
  model; `dlopen` is absent so a static kdrive is the only viable server anyway.
- `CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include"`, `LDFLAGS` likewise; `PKG_CONFIG_PATH`
  points at the isolated prefix so each lib finds its already-built deps.

## Phoenix-gap patches applied (in `patches/`)

- **libxcb-1.16-phoenix.patch** — (1) `xcb_conn.c`: add `#include <arpa/inet.h>` (Phoenix
  defines `htonl` as a macro there and doesn't pull it transitively). (2) `xcb_in.c`: no-op
  guards for `MSG_TRUNC`/`MSG_CTRUNC` (Phoenix `sys/socket.h` lacks them; recvmsg doesn't
  report them; SCM_RIGHTS fd-passing is unused by a software X server).

### libphoenix fixes made for the port (committed, libphoenix 89d1543)

- **getpwuid_r** added + **getpwnam_r** implemented (was a `return -1` stub) — proper
  POSIX-reentrant wrappers. libX11's `Xos_r.h` needs them on the MT-safe path.
- **`sys/poll.h`** compat alias added (→ `<poll.h>`) — libX11's `Xpoll.h` includes it.
- **hypot()/hypotf()** added to libm (`6e2b929`) — Phoenix's libm lacked them; libXfont2's
  freetype font-matrix code calls hypot.
- These ship in the on-device libc on the next libphoenix/image rebuild (additive; needed when
  the X server eventually links/runs). The frozen flagship image is unaffected until then.

> **STATUS 2026-06-18:** the ENTIRE X11 client + render + font + toolkit library stack cross-compiles
> for aarch64-phoenix — **45 archives** in `/tmp/x11-phoenix/lib` (libX11, libxcb +24 exts, libXext,
> libXrender, libXrandr, libXfont2, libfontenc, libfreetype, libpixman-1, libICE, libSM, libXt, libXmu,
> libXpm, libXaw, libXau, libXdmcp, libz). The **executable boundary is CROSSED**: the libphoenix libc
> gaps are all committed + rebuilt, and the first X executables — `xprobe` (minimal Xlib client) and
> **`twm`** (a full window manager) — LINK as aarch64-phoenix ELFs (`artifacts/x11/`). Remaining: the
> kdrive Xfbdev **server** (the big multi-session piece) — then twm can actually run. Foundation +
> client + toolkit + first app are DONE + de-risked.

### libphoenix gaps noted (upstream-worthy, not yet fixed)

- `sys/socket.h` lacks `MSG_TRUNC`/`MSG_CTRUNC` (patched locally in libxcb instead — see above).

## Known Phoenix walls to expect (from tinyx-x11-demo.md)

- `shm_open` missing → build XCB/X with `--disable-mitshm`.
- `dlopen`/`dlfcn.h` absent → static-link kdrive only (no loadable DDX/modules).
- `mmap(fd,0)` of `/dev/fb0` gives a private copy, not the live FB → the server must
  use a shadow framebuffer + `write()`-blit to `/dev/fb0` (kdrive shadow-FB layer).
