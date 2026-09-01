# tinyx X11 demo on Raspberry Pi 4

Implementation and integration plan for landing the Phoenix-RTOS tinyx
X11 port (PR `phoenix-rtos/phoenix-rtos-ports#82` — "x11: introduce X11
port", JIRA RTOS-826, branch `adamgreloch/RTOS-861`) on the
aarch64-rpi4b target and getting an X server running on HDMI with
keyboard input from a USB device.

This is a **stretch / "wow" milestone** — see
[`00-master-plan.md` M12](../knowledge/00-master-plan.md).

> ## ⚠️ STATUS RECONCILIATION — 2026-06-16
>
> **This plan predates the GPU and base-system work that has since landed.
> Most of the prerequisites it lists as future/blocked are now DONE.** The
> body below is kept for structure and historical context; read it through
> this lens. The headline deltas:
>
> | Old assumption in this doc | Reality as of 2026-06-16 |
> |---|---|
> | `/dev/fb0` is future work, owned by an unwritten "gpu-vc6 M8" server | **DONE.** `sources/phoenix-rtos-devices/video/rpi4-fb/` registers `/dev/fb0` (firmware framebuffer, `RPI4FB_GETMODE` devctl, `read()`/`write()` byte access, geometry runtime-queried via `platformctl(pctl_graphmode)` — validated `1024x768x32` on HW; the GPU/Quake path drives it at scanout resolution). HW-validated netboot, boot→psh, 0 faults. **NOT yet provided:** Linux-`FBIOGET_*` veneer and `mmap(fd,0)` FB backing — both flagged as attended one-way doors. So an X server reaches the framebuffer via `write()`-to-scanout (shadow→blit), not fbdev mmap. See `docs/inprogress/2026-06-05-fb0-attended-decisions.md`. |
> | Stage-1 caches off → "unaccelerated 1080p X is slideshow-slow" | **Caches are ON** (TD-16 resolved 2026-05-17; SCTLR.{M,C,I} set, Normal RAM WB-cacheable). The "caches off" premise is stale. |
> | USB HID keyboard "post-M3" / mouse driver doesn't exist | **Both work.** `usbkbd` (`/dev/kbd0`) ships a **raw 8-byte HID boot-report mode** (toggle by writing 1 byte) *in addition to* cooked ASCII; `usbmouse` (`/dev/mouse0`) ships raw 4-byte boot-mouse packets. Enumeration is reliable (11/11 benches). The "build an evdev shim + a usbmouse driver from scratch" lift in §4.2/§4.3 is **largely already paid** — see the revised note there. |
> | The whole X.org GPU-accel question is "deferred past v1.0" (Open Q2) | **The V3D 4.2 GPU now renders accelerated OpenGL on real hardware.** A ported Mesa gallium **v3d** driver + GL frontend (`st/mesa` + GLSL) + a **custom in-process userspace winsys** (`tools/v3d-driver-port/v3d_phoenix_winsys.c`) runs GL on the real V3D, with **render-to-scanout** straight into `/dev/fb0`. **GL Quake (quakespasm) runs and is playable** (`tools/quakespasm-port/`; render-to-scanout, reported ~42 fps at 1080p — the earlier port-plan note of ~10 fps was a fullscreen *cube* before caches/EZ work, not the world renderer). **BUT** this changes the X-accel calculus in a subtle, important way analysed in the companion doc — the winsys is **per-process, no-DRM, synchronous, single-client** (it reprograms the V3D's one MMU base register to its own page table on init), so it does **not** give X a multi-client accelerated GPU for free. |
> | "Tinyx ships in PR #82 today" — treats the X11 stack as arriving | PR #82 has **not** landed in `sources/phoenix-rtos-ports/` (verified: no `x11/`, `libX11`, `libxcb`, `pixman`, `freetype`, `fontconfig`, `Xau`, `Xdmcp`). The entire X11 library stack is **net-new unported work**, not a cherry-pick away. `shm_open` is still missing in libphoenix (MIT-SHM must be `--disable`d); `dlfcn.h`/`dlopen` is absent (rules out full Xorg loadable-module DDX — kdrive's static link is the only viable model). |
>
> **✅ AF_UNIX GATE PASSED — 2026-06-17.** Phase-1 dep #1 (the gate for *all*
> X-client connectivity) is confirmed on real aarch64-rpi4b hardware. The kernel
> implements AF_UNIX in `posix/unix.c` (socket/bind/listen/accept4/connect/
> send/recv for SOCK_STREAM/DGRAM/SEQPACKET, 1283 lines). A boot probe
> (`sources/phoenix-rtos-devices/misc/rpi4-ipcprobe/`, kept in-tree, not a default
> component) exercised it: **`socketpair` PASS** (stream data path) **and the full
> named `bind`→`listen`→`accept`/`connect`→`send`/`recv` dance PASS** → verdict
> "AF_UNIX READY for X11". So `/tmp/.X11-unix/X0`-style local sockets work; the X
> server↔client transport is not a blocker. Remaining Phase-1 risk is now the X11
> *library port* itself (net-new), not the OS IPC foundation. (Known kernel TODOs
> in `unix.c`: `listen` backlog ignored, MSG_PEEK, some race FIXMEs — none block the
> single-listener X server.)
>
> **What this means going forward:** the *software* TinyX demo described
> below (phases 1–7: Xfbdev on `/dev/fb0`, twm, st/xterm, keyboard+mouse)
> is now mostly *unblocked on the Phoenix side* — the remaining cost is
> porting the X11 library stack + a kdrive fbdev/input backend, not waiting
> on caches/fb/USB. **GPU-accelerated X** is a separate, much harder
> question now studied in full in
> **[`2026-06-16-x11-accelerated-desktop-plan.md`](../inprogress/2026-06-16-x11-accelerated-desktop-plan.md)**
> (path from this software TinyX to a full accelerated X11 desktop). The
> headline of that study: **software X is the realistic milestone and the
> practical ceiling; accelerated X is a research stretch gated on an EGL
> port and on solving single-client GPU arbitration.**
>
> ## ✅✅ STATUS UPDATE — 2026-06-18: the X11 library stack + server core are BUILT
>
> The "remaining cost is porting the X11 library stack + a kdrive fbdev/input
> backend" from the 2026-06-16 note above is now **largely paid** (host-side; the
> only un-done piece is the fbdev DDX, which is runtime-only-verifiable and so
> deferred until the Pi is on). Concretely, all in `tools/x11-port/` (isolated
> `/tmp/x11-phoenix`, flagship untouched; recipe in `tools/x11-port/PROGRESS.md`):
> - **The entire X11 client+render+font+TOOLKIT library stack cross-compiles** for
>   aarch64-phoenix — ~45 archives incl. libX11/libxcb(+exts)/libXext/libXrender/
>   pixman/freetype/libXfont2 + libICE/SM/Xt/Xmu/Xaw/Xrandr. All libphoenix libc
>   gaps it needed are committed (getpw*_r, sys/poll, hypot, full wide-char +
>   C-locale multibyte set, `setlinebuf`).
> - **The kdrive xorg-server 1.20.14 SERVER CORE compiles** — 28 static archives,
>   0 errors, incl. `libkdrive.a` (the DDX core: `KdInitOutput`/`KdScreenInit`) and
>   `libshadow.a` (the shadow-FB the fbdev backend will blit to `/dev/fb0`). Gaps
>   cleared with clean fixes: libphoenix `setlinebuf`, a public-domain `libmd.a`
>   SHA1 (`--with-sha1=libmd`), `--disable-xephyr` (Xephyr is a nested server,
>   unrunnable on Phoenix). The **only remaining new-code step is the fbdev DDX**
>   (`main()` + `KdCardFuncs`/`KdScreenFuncs` → shadow → `write()`/`/dev/fb0`).
> - **Three X *clients* link as static aarch64-phoenix ELFs** (staged to the NFS
>   `/bin`): `twm` (window manager), `xphxdemo` (a native Xlib *drawing* client),
>   and **`xeyes` 1.1.2** (a real upstream X app). `xprobe` is RUN-verified on HW
>   (Xlib executes; XOpenDisplay returns NULL gracefully with no server yet).
>
> Net: the software-TinyX path this doc scoped is **de-risked down to one remaining
> code artifact (the fbdev DDX) + its HW bring-up**. Everything that DDX links
> against is built and the client apps are ready to run against it.

It depends on essentially
every other Pi 4 subsystem reaching a working state: framebuffer
(`/dev/fb0`), USB HID (keyboard, then mouse), persistent rootfs (X
binaries + fonts won't fit in syspage), GENET (only for remote demos
and X11 forwarding — not required for the core demo), and Stage-1
caches. **(All of these are now DONE — see the reconciliation banner above.)**

---

## 1. Goal and minimum viable demo

**Goal.** Boot Pi 4 from SD card to a Phoenix-RTOS shell, type
`X &` (or autostart via `psh` rc), and see:

1. A 1080p HDMI desktop replace the fbcon banner.
2. At least one X client window drawn on top of the root window —
   `xclock` ticking, `xeyes` tracking, or `xlogo` rendered.
3. Keyboard input routed from a plugged-in USB-HID keyboard into the
   focused X client. (Mouse is a phase-5 stretch.)

**Minimum viable demo (MVD).** The smallest "X is up" we can show on
real hardware:

- `Xfbdev` reaches its event loop and renders the dotted-pattern X
  root window (`X Window System` cross-hatch background).
- A hardcoded test client on the same Pi connects over the AF_UNIX
  display socket (`/tmp/.X11-unix/X0`), opens a window, and exits
  cleanly.
- No keyboard, no mouse, no clock — just proof the server boots,
  registers, accepts a local connection, and paints.

The MVD is the gate. Once it passes, phases 4–7 add real input and
real clients.

---

## 2. PR analysis: scope, status, dependencies

Sources for this section:
- `phoenix-rtos/phoenix-rtos-ports#82` (open, single squash commit
  `d6e600f`, awaiting review from Darchiv)
- the PR's "Files changed" tree (split by component below)
- the X.org wiki [kdrive
  page](https://www.x.org/wiki/kdrive/) and X server documentation

### What "tinyx" means here

Tinyx (Kdrive) is the small X server fork kept as
`xserver/hw/kdrive`. The PR ships the `Xfbdev` backend — fully
unaccelerated, pixman software rendering on the ARM cores into a
Linux fbdev-shaped framebuffer. No V3D, no GLX, no DRI.

### What the PR brings in

Component breakdown from the PR's file tree:

| Group | Components | Note |
|---|---|---|
| **X server** | `tinyx` (16 patches: `configure.ac`, kdrive modules, kbd/mouse input, vga, fbdev, tty mode, XDMCP) | Xfbdev backend. Patches add Phoenix-specific input glue and platformctl integration. |
| **Core protocol libs** | `libX11`, `libxcb`, `xtrans`, `xorgproto` | One patch each. xtrans's AF_UNIX paths are the Phoenix-relevant lift. |
| **Extension libs** | `libXext`, `libXrender`, `libXt`, `libXaw`, `libICE` | Stock except for build-system patches. |
| **Fonts** | `libXfont` (6 patches), `libXfont2`, `fontconfig` | Bitmap font path (PCF/BDF). FreeType used through fontconfig. Six patches on libXfont indicate non-trivial Phoenix surgery (probably FS path + dirent + `bitscale.c` math fixes). |
| **Other libs** | `imlib2` (2 patches incl. `loaders-no-dl.patch`), `libpng` (companion PR #88) | `loaders-no-dl` removes dlopen and statically links image loaders — important on Phoenix where dlopen is weak. |
| **Window managers** | `twm` (1 patch), `dwm` (Makefile patch) | twm is the conservative, classic "MIT" WM and the safest bet. dwm is one C file but suckless-style and aggressive. |
| **Apps** | `xclock`, `xeyes`, `xmessage`, `xinit`, `ico`, `xbill`, `feh`, `st` | st = simple terminal (suckless). The PR is generous — we only need 2-3 working apps for the demo. |

**Build target in the PR:** `ia32-generic-qemu` and `ia32-generic-pc`.
There is **no aarch64-rpi4b target** in PR #82. Adding it is the first
piece of work this plan owns (Phase 1).

### Companion PRs (cross-repo dependencies)

PR #82 lists: `ports#78` (build improvements), `kernel#572` (POSIX
sockets), `kernel#596` (graphmode — overlaps with our existing
`pl011_fbcon` handling), `devices#515` (fbcon), `devices#512` (PS/2
mouse — PC-only; we need a USB-HID equivalent), `ports#88` (libpng).
PR test report: 7,916 tests pass on ia32-generic-qemu — validates
X.org port mechanics, says nothing about Pi 4 fb/input glue.

### Phoenix-RTOS dependencies the PR assumes

| Need | Phoenix status |
|---|---|
| `mmap()` of fb | Yes ([`sys/mman.h`](../../sources/libphoenix/include/sys/mman.h)) |
| `FBIOGET_VSCREENINFO`/`FBIOGET_FSCREENINFO`/`FBIOPUT_VSCREENINFO` | [`gpu-vc6-impl.md` §4](gpu-vc6-impl.md) already targets these exact numbers (0x4600, 0x4601, 0x4602) |
| AF_UNIX sockets | Header present ([`sys/un.h`](../../sources/libphoenix/include/sys/un.h)); aarch64-rpi4b runtime path is **the first audit** in Phase 1 |
| pthreads (mutex/cond/create) | Yes ([`pthread.h`](../../sources/libphoenix/include/pthread.h)) |
| `select`/`poll` | Yes |
| POSIX shm (`shm_open`) | **Missing** in libphoenix → MIT-SHM extension must be `--disable`d in tinyx build |

---

## 3. Pi 4 specifics: framebuffer + input plumbing

### Framebuffer

> **UPDATED 2026-06-16:** `/dev/fb0` exists and is HW-validated. The
> remaining gap is the *ABI shape*, not the device.

plo allocates the firmware framebuffer via the BCM2711 property mailbox;
`pl011-tty` adopts that address and runs a tiny VT100/ANSI fbcon
(`pl011_fbcon_*`). On top of that, **`sources/phoenix-rtos-devices/video/rpi4-fb/`
now registers `/dev/fb0`**: `read()`/`write()` byte access, an
`RPI4FB_GETMODE` devctl returning `{width,height,bpp,pitch,smemlen,framebuffer}`,
and `getattr`. Geometry is **runtime-queried** (`platformctl(pctl_graphmode)`),
validated at `1024x768x32 pitch=4096` on hardware; the GPU render-to-scanout
path drives the same surface at scanout resolution. The surface is mapped
**uncached by physical address** (`MAP_PHYSMEM`).

**Two ABI gaps remain (the real Phase-3 work now):**

1. **No Linux-fbdev `FBIOGET_VSCREENINFO`/`FBIOGET_FSCREENINFO` veneer.**
   The device exposes `RPI4FB_GETMODE`, not the Linux ioctls kdrive's
   `fbdevHW`/`KdFb` backends expect. The kdrive fbdev backend must be
   pointed at `RPI4FB_GETMODE` (small driver edit) or a veneer added (an
   attended ABI one-way door — see `2026-06-05-fb0-attended-decisions.md`).
2. **No `mmap(fd, 0)` backing.** The in-tree device-fd mmap path
   demand-pages a *private copy*, not the live framebuffer — so a memory-mapped
   fbdev "direct draw to screen" model does **not** work yet. The pragmatic
   path: the X server keeps a **shadow framebuffer in its own memory** and
   **blits it to `/dev/fb0` via `write()`** (the device's `write()` does the
   `memcpy` into scanout DRAM). kdrive's *shadow framebuffer* layer
   (`fbInitializeBackingStore`/`KdShadowFbAlloc`) is designed for exactly this
   and sidesteps the kernel-VM one-way door entirely.

`FBIOPUT_VSCREENINFO` mode-setting: there is no DRM/KMS, so live mode change
is firmware-mediated only; ship matching the plo-allocated mode.

### Input — keyboard

> **UPDATED 2026-06-16:** the raw-HID gap this section worried about is
> **already closed in the driver.** usbkbd has a raw mode, and a working
> reference consumer exists.

Phoenix's
[`usbkbd`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)
exposes a cooked ASCII character stream on `/dev/kbd0` (consumed by
`pl011-tty`'s `pl011_kbdthr` into libtty for the psh console) **and a
raw 8-byte HID boot-report mode**: a client writes a single `1` byte to
`/dev/kbd0` and subsequent `read()`s deliver raw packet-aligned HID
reports (`[0]=modifier bitmask, [2..7]=pressed usages`); writing `0`
restores cooked mode (`usbkbd.c` `rawMode`, lines ~109–114, 413, 641–644).
Raw reports carry full key state on every change, so a consumer can diff
successive reports into real key-**down** and key-**up** events plus
shift-vs-shifted distinction.

**This is exactly what X needs** (press AND release, autorepeat source,
modifier latching). A *proven reference* already does it in-process:
`tools/quakespasm-port/platform/pl_phoenix_in.c` turns raw `/dev/kbd0`
into Quake `Key_Event(down/up)` + `Char_Event`.

**Resolution paths**, re-ranked for current reality:

1. **Recommended:** a small **kdrive input driver** (a `KdKeyboardDriver`)
   that opens `/dev/kbd0`, toggles raw mode, and translates HID usages →
   X keycodes via `KdEnqueueKeyboardEvent`. Mirrors `pl_phoenix_in.c`'s
   HID→key map; no separate evdev server, no `/dev/input/event*`. ~few days.
2. **Optional extra:** if layout flexibility is wanted, port **xkbcommon**
   (not in PR #82) and feed it scancodes. Defer — a US-QWERTY hardcoded
   map is fine for the demo.
3. **Avoid:** building a full Linux-evdev `/dev/input/event0` server just
   to satisfy the stock `-keybd evdev` backend. It's more IPC and glue than
   a direct kdrive driver, given we already have raw HID.

### Input — mouse

> **UPDATED 2026-06-16:** `usbmouse` exists; the PS/2 path is irrelevant.

`sources/phoenix-rtos-devices/usb/usbmouse/` ships and registers
`/dev/mouse0`, delivering raw 4-byte HID boot-mouse packets on change
(`[0]=buttons, [1]=dx int8, [2]=dy int8, [3]=wheel`). Enumeration of
kbd0+mouse0 is reliable on HW (11/11 benches). The same kdrive input
driver (path 1 above) opens `/dev/mouse0` and feeds
`KdEnqueuePointerEvent` (relative motion + buttons). `pl_phoenix_in.c`
again is the working reference for the parse + accumulate math.

The PR #82 PS/2 mouse path (`devices#512`) is the wrong transport for
Pi 4 and is **not needed**. Mouse is no longer a separate hard phase —
it shares the keyboard's kdrive input driver.

### IPC and storage

X clients connect over AF_UNIX at `/tmp/.X11-unix/X0`. Phoenix has
[`sys/un.h`](../../sources/libphoenix/include/sys/un.h) and
[`sys/socket.h`](../../sources/libphoenix/include/sys/socket.h); the
posixsrv path implements the kernel side. Whether AF_UNIX actually
works on aarch64-rpi4b is a Phase-1 audit (20-line ping/pong).

X binaries + bitmap fonts are several MB — won't fit in the
syspage-based ramdisk. Phase 6 needs M4 (SDHCI + FAT/ext2 per
[`scope-pi4-uncovered.md` §2.1, §2.2](../knowledge/scope-pi4-uncovered.md)).
Interim: tmpfs / cpio-initrd with a stripped-down X tree.

---

## 4. Per-gap implementation plan

### 4.1 `/dev/fb0` server — DONE

> **UPDATED 2026-06-16:** done. `sources/phoenix-rtos-devices/video/rpi4-fb/`.
> Remaining work is the kdrive *backend* binding (point it at
> `RPI4FB_GETMODE` + `write()`-blit a shadow FB), not the device. See the
> Framebuffer subsection in §3.

### 4.2 Keyboard input — driver gap already closed

> **UPDATED 2026-06-16:** the "build an evdev shim" plan is **obsolete**.
> usbkbd already ships raw 8-byte HID mode (write `1` to `/dev/kbd0`), and
> `tools/quakespasm-port/platform/pl_phoenix_in.c` is a proven HID→events
> reference. The remaining work is a thin **kdrive input driver** that
> reuses that map and calls `KdEnqueueKeyboardEvent` — no `/dev/input/event0`
> server, no extra IPC hop. ~few days. (Designs A/B below are superseded.)

### 4.3 USB HID mouse driver — DONE

> **UPDATED 2026-06-16:** `sources/phoenix-rtos-devices/usb/usbmouse/`
> ships and registers `/dev/mouse0` (raw 4-byte boot-mouse packets). No
> new driver needed; the kdrive input driver from §4.2 also handles the
> mouse via `KdEnqueuePointerEvent`.

### 4.4 X dependencies audit (Phase 1)

One-time audit pass before any tinyx code lands. **(UPDATED 2026-06-16:
the X11 library stack itself — libX11, libxcb, xtrans, xorgproto, pixman,
freetype, fontconfig, Xau/Xdmcp, libXext, libXfont — is NOT in
`sources/phoenix-rtos-ports/` yet; PR #82 has not landed. Treat the whole
stack as net-new porting work, not a cherry-pick. `dlopen`/`dlfcn.h` is
absent → full Xorg's loadable-module DDX is impossible; kdrive's static
link is the only viable server. `shm_open` still missing → `--disable-mitshm`.)**

| Need | Phoenix status | Risk | Action |
|---|---|---|---|
| `mmap()` of fb (private copy only) | Partial — `MAP_PHYSMEM` works for drivers; `mmap(fb_fd,0)` demand-pages a private copy, not the FB | Medium | Use shadow-FB + `write()`-blit to `/dev/fb0` (no mmap of the live FB) |
| AF_UNIX sockets | Header present, runtime unverified on aarch64-rpi4b | Medium | 20-line ping/pong test in Phase 1 |
| pthreads (mutex+cond+create) | Yes | Low | None |
| `select`/`poll` over multiple fds | Yes | Low | Smoke test |
| `shm_open` (MIT-SHM) | **Missing** | Low (extension is optional) | Build with `--disable-mitshm` |
| `dlopen` / dynamic loaders | **Absent** (`dlfcn.h` not present) | High for full Xorg / Low for kdrive | Forces the static-link kdrive model (no loadable DDX/modules); static-loader patches (e.g. imlib2 `loaders-no-dl`) needed for any dlopen consumer |
| FS support for `/tmp` and font dirs | Tied to M4 | Medium | tmpfs interim, ext2 final |
| `gettimeofday` / `clock_gettime` | Yes | Low | None |
| `signal` / SIGCHLD for client reaping | Partial | Low | Verify SIGCHLD; may need `waitpid` |
| `setlocale` / nl_langinfo | Yes | Low | C locale only |

### 4.5 Font and asset packaging

Tinyx with bitmap fonts (the PCF / BDF route in the PR's `libXfont`
patches) needs ~1 MB of PCF font files. FreeType + fontconfig adds
TTF support for client-side rendering at the cost of ~5 MB of
TTF + cache + libs. For the MVD: ship just `fixed.pcf` and `cursor.pcf`
(both ~50 kB). For the full demo (xterm/st): add Liberation Mono or
DejaVu Sans Mono.

---

## 5. Phased delivery

### Phase 1 — port skeleton + dep audit (~1 week)

- Cherry-pick PR #82 onto a worktree branch in `phoenix-rtos-ports`.
- Add `aarch64-rpi4b` to the per-port target list in `x11/build.sh`.
- Build everything for aarch64 against the existing
  `phoenix-rtos-project` toolchain. Expect failures; fix them
  one component at a time. Likely hit-list: `pixman` SIMD
  detection (NEON path on aarch64), libxcb pthread-tls usage, twm
  Imake quirks.
- Run the AF_UNIX socket audit: write a 50-line client/server pair
  and confirm round-trip on aarch64-rpi4b. **Gate** for Phase 2.
- Verify `loaders-no-dl` patch on imlib2 and equivalent
  static-loader paths elsewhere.
- **Exit:** every PR-listed component (libX11, libxcb, libXext, ...,
  twm, st, xclock) produces an aarch64 ELF that opens against
  Phoenix's libc.

### Phase 2 — Xfbdev on a stub framebuffer (~1 week)

- Use a userspace stub that fakes `/dev/fb0` (anonymous mmap,
  in-memory framebuffer, devctl returns canned mode info). No
  hardware writes.
- Run `Xfbdev :0 -screen 1024x768x32 -nolisten tcp` on aarch64-rpi4b.
  Goal: server reaches its event loop, prints
  `XINIT: connecting to ...` lines.
- Connect a hardcoded test client (`xeyes -display :0`) over
  AF_UNIX. xeyes connects, opens window, draws into the stub fb.
- Read back the stub framebuffer over UART (CRC of the rendered
  region) — known-good "X is up" signal without HDMI.
- **Exit:** MVD met against stub fb. Demonstrable in QEMU also.

### Phase 3 — real fb on HDMI (~3 days) — **fb0 DONE; remaining work = kdrive fbdev backend (§3)**

> **UPDATED 2026-06-16:** `/dev/fb0` exists. The work is no longer "wait for
> M8" but: point a kdrive fbdev backend at `RPI4FB_GETMODE` for geometry and
> use a shadow framebuffer flushed to `/dev/fb0` via `write()` (no fbdev mmap;
> see §3 Framebuffer).

- Replace the stub with the real `/dev/fb0` (`rpi4-fb`) device.
- Run Xfbdev pointing at `/dev/fb0`. The fbcon banner should
  disappear; X dot-pattern root window appears on HDMI.
- Confirm pixel format / pitch agreement (Pi 4 firmware prefers
  BGR vs xorg's RGB — test, swap a tinyx config flag if needed).
- **Exit:** xeyes window visible on HDMI; pixels are correct
  (no shift, no torn rows).

### Phase 4 — keyboard input (~few days) — **evdev shim superseded (§4.2)**

> **UPDATED 2026-06-16:** the evdev-shim design is superseded. usbkbd has raw
> HID mode and `pl_phoenix_in.c` is a proven HID→events reference. Implement a
> small **kdrive input driver** that opens `/dev/kbd0` (write `1` for raw),
> diffs reports, and calls `KdEnqueueKeyboardEvent` — no `/dev/input/event0`.

- Write the kdrive keyboard driver (HID-usage map reused from `pl_phoenix_in.c`).
- Type `a` on the USB keyboard; `xev -display :0` echoes press/release events.
- **Exit:** pressing keys on the real USB keyboard produces visible X events.

### Phase 5 — USB mouse (~days) — **usbmouse DONE; shares the kdrive input driver**

> **UPDATED 2026-06-16:** `usbmouse`/`/dev/mouse0` already exists. No new
> driver. The kdrive input driver from Phase 4 also opens `/dev/mouse0` and
> calls `KdEnqueuePointerEvent`.

- Extend the kdrive input driver to read `/dev/mouse0` (4-byte HID packets).
- Plug a USB mouse, see the X cursor track.
- **Exit:** mouse cursor visible and tracking on HDMI.

### Phase 6 — first decorative X client (~3 days)

- Add `xclock` to the system image. Autostart with `xinit` or via
  psh rc script: `Xfbdev :0 -screen ... & xclock -display :0`.
- Confirm xclock ticks (clock hands move every second). Validates
  X server timer event delivery and pixman software rendering at
  reasonable speed (target: at least 1 FPS post-cache-enable).
- Optional same-phase: `xeyes` + `xclock` simultaneously,
  demonstrating multi-client.

### Phase 7 — terminal emulator running a shell (~1-2 weeks, full demo)

- Add `st` (or `xterm` if patches available) to the image.
- `xinit` rc: `Xfbdev :0 & twm & st -e psh`. Now we have a window
  manager (twm), an X terminal (st), and Phoenix-RTOS's `psh`
  running inside it.
- Demo script: open st, type `ls /`, see directory listing,
  type `(psh)% sysinfo`, see Phoenix-RTOS sysinfo output.
- **Exit:** screenshot worth posting publicly. M11/M12 milestone
  achieved.

---

## 6. Test strategy

- **Host unit tests:** none — PR #82's ia32-generic-qemu CI already
  validates X.org port correctness; this plan is integration.
- **aarch64-rpi4b QEMU smoke:** Phase 1+ builds the stack; Phase 2+
  starts Xfbdev against the stub fb and a test client.
- **Real Pi 4:**
  - Phase 3+: HDMI capture frame-grab vs. golden PNG.
  - Phase 4+: scripted keystrokes via another Pi as a USB-HID gadget;
    confirm `xev` log on UART.
  - Phase 7: desktop screenshot diff (twm + st + fixed `psh` greeting).
- **Probe-parity rule:** every input-shim diagnostic runs in QEMU
  first (stub kbd0 + stub fb) and then on real Pi 4, with both
  outputs diffed in `tracking/current-step.md`.

---

## 7. Inter-dependencies

**(UPDATED 2026-06-16: the first four rows are all DONE — see banner.)**

| Dependency | Source | Status | Why this plan needs it |
|---|---|---|---|
| **Stage-1 caches (M2)** | [`cache-mmu-smp-impl.md`](../done/cache-mmu-smp-impl.md) | **DONE** (TD-16) | Frames render into cacheable DRAM at speed. |
| **`/dev/fb0`** | `phoenix-rtos-devices/video/rpi4-fb/` | **DONE** | The X server's display target (shadow-FB + `write()`-blit). |
| **USB HID keyboard** | `phoenix-rtos-devices/tty/usbkbd/` (raw HID mode) | **DONE** | Keyboard input via kdrive input driver. |
| **USB HID mouse** | `phoenix-rtos-devices/usb/usbmouse/` | **DONE** | Pointer input (same kdrive input driver). |
| **SDHCI + FAT/ext2 (M4)** | [`scope-pi4-uncovered.md` §2.1, §2.2](../knowledge/scope-pi4-uncovered.md) | X assets (~10 MB) don't fit syspage. Phase 6/7 fully blocked without it. |
| **Companion Phoenix PRs** | kernel#572 sockets, kernel#596 graphmode, devices#515 fbcon, ports#88 libpng | All listed in PR #82's description. |

This plan **does not** require:

- M5 GENET. Useful for X11 forwarding (`ssh -X`) demos but not core.
- M6 SMP. X is single-threaded; SMP gives apps room to run but
  isn't a gate.
- M7 4 GiB DRAM. X + twm + st fits well under 256 MiB.

---

## 8. Effort estimate

Per-phase at 1 dev-FTE. **(UPDATED 2026-06-16: the OS prereqs (caches, fb0,
USB HID kbd+mouse) are all DONE, so the critical path no longer runs through
them — the remaining cost is the X11 library port + the kdrive backend.
Phases 4/5 shrink because raw-HID input + usbmouse already exist; Phase 1
grows because the whole X11 stack is net-new, not a PR #82 cherry-pick.)**

| Phase | Best | Likely | Worst | Note |
|---|---|---|---|---|
| 1 — port the X11 lib stack + audit | 2 wk | 4 wk | 8 wk | net-new (no PR #82); biggest item |
| 2 — Xfbdev stub fb | 3 d | 1 wk | 1.5 wk | |
| 3 — real fb on HDMI (kdrive fbdev backend) | 2 d | 4 d | 1 wk | fb0 done; backend small |
| 4 — keyboard (kdrive input driver) | 2 d | 4 d | 1 wk | raw HID + reference exist |
| 5 — USB mouse (same driver) | 1 d | 2 d | 4 d | usbmouse done |
| 6 — xclock client | 1 d | 3 d | 1 wk | |
| 7 — twm + xterm/st + psh-in-window | 1 wk | 1.5 wk | 3 wk | |
| **Total** | **~4 wk** | **~6–10 wk** | **~16 wk** | software X |

Most cost is now the X11 **library port** (Phase 1), not the input/fb
plumbing the original plan worried about. Critical path from today:
**X11 lib stack → kdrive fbdev backend → kdrive input driver → twm + xterm**.
GPU-accelerated X is a separate, much larger effort — see
[`2026-06-16-x11-accelerated-desktop-plan.md`](../inprogress/2026-06-16-x11-accelerated-desktop-plan.md).

---

## 9. Open questions

1. **License fit of X.org code.** Most of the X.org tree is
   MIT-flavoured but per-component COPYING varies (libXaw, xbill,
   etc.). Audit each component before public release per
   [`docs/knowledge/code-quality-and-upstreaming.md`](../knowledge/code-quality-and-upstreaming.md).
2. **Hardware-accel via Mesa shim.** ~~Deferred past v1.0.~~
   **UPDATED 2026-06-16:** the Mesa gallium v3d driver + GL frontend now
   render accelerated GL on the real V3D (GL Quake runs). **However**, the
   winsys is per-process, no-DRM, synchronous, and single-client (it
   reprograms the V3D's one MMU base register to its own page table on
   init), so it does **not** drop in as X-server GPU acceleration. The full
   feasibility analysis — Glamor, EGL, single-client GPU arbitration, and
   why **software X remains the practical ceiling** — is the dedicated
   companion study
   [`2026-06-16-x11-accelerated-desktop-plan.md`](../inprogress/2026-06-16-x11-accelerated-desktop-plan.md).
3. **Window manager choice.** `twm` (MIT) vs `dwm` (GPL-2.0,
   single C file). Public-demo license argues for twm.
4. **Tinyx vs Wayland.** Tinyx ships in PR #82 today; Wayland would
   be a from-scratch port. Recommend tinyx for v1 graphical demo.
5. **xev availability.** Not in PR #82's app list; confirm it builds
   standalone or pick an alternative key-debug client.
6. **Font scope.** PCF bitmaps only (MVD) vs FreeType + 1 TTF (Phase 7
   for st antialiased rendering at 1080p).
7. **Autostart vehicle.** `psh` rc-script vs `xinit` wrapper.

---

## 10. Stretch goals (post-demo)

- `xterm` instead of `st` (heavier, pulls libXaw, but X-default polish).
- Multi-client desktop screenshot — xclock + xeyes + st under twm.
- Pre-recorded screencast: ffmpeg-composited UART log + HDMI capture
  for the v1.0 release notes asset.
- `xrandr` live 720p ↔ 1080p mode-set, contingent on gpu-vc6 Tier 1
  honouring `FBIOPUT_VSCREENINFO`.
- Network X (`ssh -X`) once M5 GENET + an SSH port land.

---

## 11. Cross-references

Source plans:
[`00-master-plan.md`](../knowledge/00-master-plan.md),
[`cache-mmu-smp-impl.md`](../done/cache-mmu-smp-impl.md) (M2),
[`usb-xhci-impl.md`](../done/usb-xhci-impl.md) (M3 + usbmouse reuse),
[`gpu-vc6-impl.md`](gpu-vc6-impl.md) (M8 `/dev/fb0`),
[`scope-pi4-uncovered.md`](../knowledge/scope-pi4-uncovered.md) (§2.1
SDHCI, §2.2 FAT/ext2, §2.16 SD-boot).

Phoenix source landmarks:
[`sources/phoenix-rtos-devices/tty/usbkbd/`](../../sources/phoenix-rtos-devices/tty/usbkbd/)
(ASCII-only `/dev/kbd0`; needs raw/evdev sibling),
[`pl011-tty.c`](../../sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c)
(fbcon + `pl011_kbdthr`),
[`sources/libphoenix/include/sys/{un,socket,mman}.h`](../../sources/libphoenix/include/sys/),
[`sources/phoenix-rtos-ports/`](../../sources/phoenix-rtos-ports/)
(no tinyx / freetype / fontconfig / libpng yet — all arrive via PR
#82 + #88).

External:
[PR #82](https://github.com/phoenix-rtos/phoenix-rtos-ports/pull/82),
[KDrive Tiny X Server](https://www.irif.fr/~jch/software/kdrive.html),
[X.org wiki — kdrive](https://www.x.org/wiki/kdrive/),
Linux `linux/input-event-codes.h` (reference only; copy needed
constants, do not include verbatim).
