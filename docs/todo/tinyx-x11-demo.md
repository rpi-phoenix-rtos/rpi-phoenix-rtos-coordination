# tinyx X11 demo on Raspberry Pi 4

Implementation and integration plan for landing the Phoenix-RTOS tinyx
X11 port (PR `phoenix-rtos/phoenix-rtos-ports#82` — "x11: introduce X11
port", JIRA RTOS-826, branch `adamgreloch/RTOS-861`) on the
aarch64-rpi4b target and getting an X server running on HDMI with
keyboard input from a USB device.

This is a **stretch / "wow" milestone** — see
[`00-master-plan.md` M12](../knowledge/00-master-plan.md). It depends on essentially
every other Pi 4 subsystem reaching a working state: framebuffer
(`/dev/fb0`), USB HID (keyboard, then mouse), persistent rootfs (X
binaries + fonts won't fit in syspage), GENET (only for remote demos
and X11 forwarding — not required for the core demo), and Stage-1
caches (an unaccelerated 1080p X server on a cache-disabled BCM2711
would be slideshow-slow).

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

Today: plo allocates a 1024x768x32 framebuffer via the BCM2711
property mailbox; `pl011-tty` adopts that address and runs a tiny
VT100/ANSI fbcon (`pl011_fbcon_*`). No `/dev/fb0` yet.

[`gpu-vc6-impl.md`](gpu-vc6-impl.md) M8 owns the `/dev/fb0` Tier 1
server, which registers Linux-fbdev devctl numbers
(`FBIOGET_VSCREENINFO` etc.) plus mmap. Tinyx Xfbdev binds directly.
Phase 3 of this plan is gated on M8.

ABI gap to confirm: tinyx may call `FBIOPUT_VSCREENINFO` to set
res/bpp; Pi 4 firmware can round/cap the response. Ship Phase 3
with `Xfbdev -screen <w>x<h>x<bpp>` matching the plo-allocated
mode; live mode change is a stretch sub-phase.

### Input — keyboard

Phoenix's
[`usbkbd`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)
(post-M3) **translates HID usage codes to ASCII inside the driver**
(`usbkbd_translateUsage` at lines 217–250) and exposes a character
stream on `/dev/kbd0`. `pl011-tty`'s `pl011_kbdthr` (lines 929–980 of
[`pl011-tty.c`](../../sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c))
consumes that ASCII into libtty.

Tinyx wants either Linux evdev (`/dev/input/event*` emitting `struct
input_event` with `KEY_*` codes and press/release semantics — the
recommended `-keybd evdev` backend, lets xkbcommon handle layouts) or
a Phoenix-custom kdrive input driver.

**Gap.** ASCII `/dev/kbd0` is insufficient: no key-release events,
no shift-vs-shifted distinction (`A` vs `a` arrive as already-resolved
codepoints), no layout flexibility. X autorepeat, modifier latching,
and xkbcommon all need raw scan codes plus press AND release.

**Resolution paths**, ranked:

1. **Best:** add a raw-HID endpoint to usbkbd; new evdev shim
   translates raw HID → evdev `input_event` on `/dev/input/event0`.
   xkbcommon binds directly. Keep ASCII `/dev/kbd0` for legacy.
2. **Interim:** custom kdrive driver reading `/dev/kbd0` ASCII,
   synthesizing press+release. No xkb. ~70% faster but a dead-end.
3. **Reject:** ASCII→fake-evdev inside Xfbdev — same limits as (2)
   plus glue.

Plan picks (1) for Phase 4 with (2) as fallback.

### Input — mouse

No USB HID mouse driver in Phoenix today. PR #82 brings PS/2 mouse
(`devices#512`) — right shape (`/dev/mouse*` character device emitting
button/movement records) but wrong transport for Pi 4. We need a
`usbmouse` sibling to `usbkbd`, reusing the host stack from
[`usb-xhci-impl.md`](../done/usb-xhci-impl.md). HID boot-mouse (subclass 0x01,
proto 0x02) is simpler than the keyboard. Output: a raw report
endpoint that the same evdev shim translates to `EV_REL`/`EV_KEY`.

Mouse is deferred to **Phase 5**; demo Phases 1-4 are keyboard-only,
consistent with classical kdrive framebuffer demos.

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

### 4.1 `/dev/fb0` server (M8 dependency, owned by gpu-vc6 plan)

Owned by [`gpu-vc6-impl.md` §6 Phase 2](gpu-vc6-impl.md). This plan
consumes it; a small mmap-gradient test program acts as the ABI
smoke test before layering tinyx.

### 4.2 Evdev shim for keyboard (new work, Phase 4)

Two designs:
- **A — extend usbkbd:** add a second `/dev/event0` endpoint
  emitting Linux-`KEY_*` press/release records. Pros: one less
  server. Cons: complicates a driver used only for ASCII today.
- **B — separate `phoenix-rtos-devices/input/evdev/` server:**
  reads raw HID reports via a new `/dev/kbd0_raw` endpoint on
  usbkbd, translates to `input_event` on `/dev/input/event0`.
  Pros: clean separation, reusable for mouse. Cons: extra IPC hop.

Recommend **Design B**; ~1.5 weeks.

### 4.3 USB HID mouse driver (new work, Phase 5)

New `phoenix-rtos-devices/usb/usbmouse/` mirroring `usbkbd`
(srv.c + usbmouse.c + Makefile). HID boot-mouse report is 3-4 bytes
(`{ buttons, dx, dy[, wheel] }`); raw endpoint feeds the same evdev
shim. ~1 week.

### 4.4 X dependencies audit (Phase 1)

One-time audit pass before any tinyx code lands:

| Need | Phoenix status | Risk | Action |
|---|---|---|---|
| `mmap()` of fb | Yes | Low | None |
| AF_UNIX sockets | Header present, runtime unverified on aarch64-rpi4b | Medium | 20-line ping/pong test in Phase 1 |
| pthreads (mutex+cond+create) | Yes | Low | None |
| `select`/`poll` over multiple fds | Yes | Low | Smoke test |
| `shm_open` (MIT-SHM) | **Missing** | Low (extension is optional) | Build with `--disable-mitshm` |
| `dlopen` / dynamic loaders | Limited | Medium | imlib2 patch already addresses this with `loaders-no-dl` |
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

### Phase 3 — real fb on HDMI (~3 days, blocked on M8)

- Replace the stub with the real `rpi4-vc6-fb` server.
- Run Xfbdev pointing at `/dev/fb0`. The fbcon banner should
  disappear; X dot-pattern root window appears on HDMI.
- Confirm pixel format / pitch agreement (Pi 4 firmware prefers
  BGR vs xorg's RGB — test, swap a tinyx config flag if needed).
- **Exit:** xeyes window visible on HDMI; pixels are correct
  (no shift, no torn rows).

### Phase 4 — keyboard input (~1 week, depends on M3)

- Implement the evdev shim per §4.2 (Design B preferred, Design A
  fallback).
- Configure tinyx with `-keybd evdev,,device=/dev/input/event0`.
- Type `a` on the USB keyboard; xeyes' eyes ignore it (xeyes
  doesn't read keys), but `xev -display :0` echoes the press/release
  events to its own UART-bound stdout.
- **Exit:** pressing keys on the real USB keyboard produces visible
  X events in `xev`.

### Phase 5 — USB mouse (~1 week, deferred / stretch)

- Implement `usbmouse` driver per §4.3.
- Extend evdev shim to handle mouse events too (`/dev/input/event1`).
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

| Dependency | Source | Why this plan needs it |
|---|---|---|
| **Stage-1 caches (M2)** | [`cache-mmu-smp-impl.md`](../done/cache-mmu-smp-impl.md) | Without caches, every X frame is rendered into uncached DRAM at fbcon speed (>10 min for 1080p in measured spike). Demo unwatchable. |
| **`/dev/fb0` (M8)** | [`gpu-vc6-impl.md`](gpu-vc6-impl.md) | Xfbdev's only display target. |
| **USB HID keyboard (M3)** | [`usb-xhci-impl.md`](../done/usb-xhci-impl.md) | Phase 4 input. |
| **USB HID mouse (NEW, this plan §4.3)** | New | Phase 5 input. |
| **SDHCI + FAT/ext2 (M4)** | [`scope-pi4-uncovered.md` §2.1, §2.2](../knowledge/scope-pi4-uncovered.md) | X assets (~10 MB) don't fit syspage. Phase 6/7 fully blocked without it. |
| **Companion Phoenix PRs** | kernel#572 sockets, kernel#596 graphmode, devices#515 fbcon, ports#88 libpng | All listed in PR #82's description. |

This plan **does not** require:

- M5 GENET. Useful for X11 forwarding (`ssh -X`) demos but not core.
- M6 SMP. X is single-threaded; SMP gives apps room to run but
  isn't a gate.
- M7 4 GiB DRAM. X + twm + st fits well under 256 MiB.

---

## 8. Effort estimate

Per-phase at 1 dev-FTE, assuming all upstream prereqs are met:

| Phase | Best | Likely | Worst |
|---|---|---|---|
| 1 — port skeleton + audit | 3 d | 1 wk | 2 wk |
| 2 — Xfbdev stub fb | 3 d | 1 wk | 1.5 wk |
| 3 — real fb on HDMI | 1 d | 3 d | 1 wk |
| 4 — keyboard (incl. evdev shim) | 1 wk | 1.5 wk | 3 wk |
| 5 — USB mouse | 4 d | 1 wk | 2 wk |
| 6 — xclock client | 1 d | 3 d | 1 wk |
| 7 — twm + st + psh-in-window | 1 wk | 1.5 wk | 3 wk |
| **Total** | **~3.5 wk** | **~5-6 wk** | **~12 wk** |

Most cost is plumbing (evdev shim, mouse driver, asset packaging),
not tinyx itself. Critical path from today: **M2 caches → M3 USB HID
→ M4 SDHCI+FS → M8 /dev/fb0 → tinyx Phases 1-7** — even optimistic
~4-6 months at 1 FTE.

---

## 9. Open questions

1. **License fit of X.org code.** Most of the X.org tree is
   MIT-flavoured but per-component COPYING varies (libXaw, xbill,
   etc.). Audit each component before public release per
   [`docs/knowledge/code-quality-and-upstreaming.md`](../knowledge/code-quality-and-upstreaming.md).
2. **Hardware-accel via Mesa shim.** GPU Tier 3 in
   [`gpu-vc6-impl.md` §10](gpu-vc6-impl.md) (V3D/Mesa) would unlock
   GLX/DRI for a future Xorg-modesetting server. Deferred past v1.0.
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
