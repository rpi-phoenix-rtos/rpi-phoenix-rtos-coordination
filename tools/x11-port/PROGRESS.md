# X11 (tinyx/kdrive) library port — progress

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
| **kdrive Xfbdev server** | ⬜ next (big, scouted) | see "Server frontier" below — needs an Xfbdev source + libphoenix rebuild + OS-integration. Multi-session. |

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
