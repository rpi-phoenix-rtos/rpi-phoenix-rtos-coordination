## Application and game ports, target integration, build system

This section covers three of the fork's repositories: `phoenix-rtos-ports` (174 commits, +55 210/−397), `phoenix-rtos-project` (222 commits, +3 412/−18) and `phoenix-rtos-build` (35 commits, +957/−23). The ports repo grew **41 new `port.def.sh` recipes** (36 userland, 5 3D game engines) and modified 7 existing ones; nothing was removed. The project repo carries the whole Raspberry Pi 4 board bring-up — a new `_targets/aarch64a72/generic` target plus an `aarch64a72-generic-rpi4b` project with its own ARM stub, relocating `kernel8.img` trampoline, firmware `config.txt` and three boot variants (NFS-root / netboot / SD). The build repo adds aarch64 generic target admission, the gcc-16.2 / binutils-2.47 toolchain rebase, and — most reusable — a set of `port_manager` staleness fixes that close real silent-stale-build holes. Note for reviewers: `phoenix-rtos-project/.gitmodules` is repointed at the `rpi-phoenix-rtos` org forks (commit `3a38eb5`), so submodule gitlinks in this repo do not reference canonical upstream.

The single most valuable material for upstream is not the port count but §3: nearly every patch in this repo is a written-up diagnosis of a libphoenix/libstdc++ gap, several with byte-exact hardware evidence.

### 1. Application ports

36 new userland recipes, all in `phoenix-rtos-ports/<name>/port.def.sh`. Licence is called out only where it constrains a maintainer's reuse. They are not independent: the interesting ones sit at the top of real dependency chains that the framework now resolves (`supertuxkart` alone declares twelve `depends=`), which is itself part of what is being demonstrated — `port_manager` can build a non-trivial DAG, not just leaf packages.

| port | version | notes |
|---|---|---|
| **★ python** | 3.14.4 | Static `python3` + extension modules, `.so` `dlopen`, zlib/ssl/hashlib, HTTPS, curses. The heaviest single proof that libphoenix is a usable POSIX libc. 302-line recipe, PSF-2.0 |
| **★ coreutils** | 9.5 | All 104 GNU tools, output verified bit-exact against the host. Also runs the gnulib `tests/*.sh` suite under the ported bash. GPL-3.0-or-later |
| **★ bash** | 5.2.21 | Fully interactive GNU shell (job control, readline). GPL-3.0-or-later |
| **★ sqlite3** | 3.53.4 | In-memory + file VFS, `integrity_check=ok`; multi-process rollback-journal proven over real `fcntl` locks. WAL is single-process only (no `xShmMap`) |
| **★ redis** | 7.2.4 | Serves 241 commands over lwIP TCP; RDB persistence works. `MALLOC=libc`, `ae_select` event loop |
| **★ sdl2** | 2.30.12 | Real SDL 2.30.12 with two *new upstream-shaped backends* written for Phoenix: `src/video/phoenix` (one fullscreen `/dev/fb0` window, input drained from `/dev/kbd0` + `/dev/mouse0`) and `src/audio/phoenix` (pull model over `/dev/audio0`). Zlib licence; the GL-context glue is kept outside `libSDL2.a` to preserve that |
| **★ libnfs** | 6.0.2 | Backs NFS-as-rootfs. Carries three real NFSv4 bug fixes (see §3). LGPL-2.1 |
| xorg_libs | 2023.2 | 24 tarballs in one recipe (libX11 1.8.7, libxcb 1.16, libXt/Xaw/Xmu/Xpm/Xext/Xrandr/Xrender, xcb-util family, pixman 0.42.2, xtrans, xkbfile). Version anchored on xorgproto |
| xorg_server | 21.1.24 | Xorg with a **new Phoenix DDX** in-tree at `xorg_server/files/ddx/` (`fbdev.c` 1020 lines, `ddxLoad.c` 631, built-in keymap, HID→evdev map). Both a software-fb and a glamor/GPU server are built |
| xorg_fonts | 2.13.2 | freetype 2.13.2 + fontconfig 2.14.2 + cairo 1.16 + expat + libXft/libXfont2/libfontenc + PCF fonts (`font-misc-misc`, `font-cursor-misc`, `font-adobe-75dpi`, `encodings`, `font-alias`) |
| xorg_apps | 1.1.2 | xcalc, xclock, xlogo, xedit (Xaw/Xt clients) in one recipe, anchored on xcalc |
| windowmaker | 0.95.9 | Window manager; the desktop actually used on HDMI. GPL-2.0-or-later |
| xterm | 396 | Interactive terminal emulator over `/dev/ptmx` — see the pty gap in §3 |
| dillo | 3.2.0 | Renders live HTTPS pages under Xphoenix (TLS via mbedTLS in-process). GPL-3.0-only |
| mc | 4.8.31 | Midnight Commander, full-screen curses app. GPL-3.0-or-later |
| nano | 9.2 | Editor; gnulib-based, hence two gnulib patches. GPL-3.0-or-later |
| xbill | 2.1 | Small Xaw game — an X11 client-stack smoke test. GPL-2.0-or-later |
| jq | 1.7.1 | Incl. regex via oniguruma; exposed a `malloc(0)` bug (§3) |
| oniguruma | 6.9.9 | Regex engine, jq dependency |
| ncurses | 6.4 | Terminal library; `-fPIC` build feeds Python's `curses` module |
| glib2 | 2.56.4 | With `libintl`/`nameser`/`resolv` stub headers supplied by the recipe. LGPL-2.1-or-later |
| fltk | 1.3.10 | Dillo's widget toolkit. LGPL-2.0-only |
| harfbuzz | 14.4.0 | Text shaping (STK) |
| **★ ffmpeg** | 6.1 | Registered `if: false` (build-proven, no in-tree consumer). LGPL-2.1-or-later |
| libjpeg-turbo | 3.0.4 | IJG AND BSD-3-Clause AND Zlib |
| libpng | 1.6.40 | |
| libogg | 1.3.5 | Ogg container; STK/game music |
| libvorbis | 1.3.7 | Vorbis decode — where the pthread-stack bug in §3 was found |
| libsamplerate | 0.2.2 | Audio resampling |
| libiconv | 1.18 | LGPL-2.1-or-later |
| libffi | 3.4.6 | Python `ctypes` |
| enet | 1.3.18 | UDP networking; needed `SOMAXCONN`/`MSG_TRUNC` fallbacks (§3) |
| bzip2 | 1.0.8 | |
| xz | 5.4.7 | 0BSD; feeds busybox seamless tar |
| **★ ca_certificates** | 2026.7.22 | Mozilla trust store installed at `/etc/ssl/certs/ca-certificates.crt` — the missing piece that made cross-built curl/dillo TLS actually verify. MPL-2.0 (recipe installs `LICENSE` alongside, per MPL §3.2) |
| **★ llama2** | 20240529 (`350e04f`) | llama2.c CPU inference: 260K model at 370 tok/s, 15M at 5.8 tok/s, bit-identical to host. MIT |

**Existing recipes modified** (a different review ask — these are fixes/bumps to upstream's own ports):
`openssl111` 1.1.1a → **1.1.1w** (`e8fc54f`, plus the EOL `old/1.1.1/` source URL and a timestamp-preserving install patch); `zlib` 1.2.11 → **1.3.1**; `mbedtls` 2.28.0 → **2.28.10**; `wpa_supplicant` 2.9 → **2.11**; `lua` **5.3.6 → 5.4.7** (whole patch set migrated and rebased, incl. the healthcheck/priority patches); `curl` gains `--with-zlib` and `--with-ca-bundle=` (a cross build silently left `CURL_CA_BUNDLE` undefined, so *every* HTTPS transfer had no trust store); `lighttpd` gains a webdav mmap guard plus a fix to its static-plugin-table generation (the old `grep mod_` also matched **commented-out** modules, compiling 13 plugins where the config enables 9); `busybox` config enables awk, xz decompress and seamless tar.

### 2. Game ports

Five 3D engines, all folded into a **single static ELF each** (Phoenix has no dynamic-executable loading, so upstream's `.so` game/renderer split cannot be used), all installing into the rootfs at `/usr/bin`. All are GPL-2.0-or-later except SuperTuxKart (GPL-3.0-or-later) — relevant to what a maintainer can look at.

| engine | version (pin) | renderer path | state |
|---|---|---|---|
| **★ quakespasm** (GLQuake) | 0.97.0 (`f5fe178`) | desktop GL → Mesa/V3D → `/dev/fb0` | Flagship. Textured real levels at ~1080p/~40 fps on HDMI, audio wired. Cleanest HW evidence of the five |
| **★ supertuxkart** | 1.4 | GLES3 (STK "SP" renderer) | Boot → fully-lit in-game 3D race, 0 crashes, host-comparison SSIM 0.991. 11 patches, 12 port dependencies |
| yquake2 (Quake II) | 8.71 (`a9e88f6`) | `ref_gl3` / GLES3 default, `ref_gl1` selectable | Renders full 3D. Client + integrated server + baseq2 game + one renderer in one ELF. Asset load is slow over NFS (mitigated by RAM-staging to `/tmp`) |
| quake3e (Quake III) | 1.32 (in-tree "Q3 1.32e", `f694bbb`) | desktop GL → Mesa/V3D | Runs; QVM bytecode modules need no `dlopen`. Known open defect: a V3D CT0 binner wedge on `q3dm7` (a lightmap-black bug on the same map was root-caused and fixed) |
| vkquake | 1.34 (`1aa13a5`) | **Vulkan** via the ported V3DV ICD (SPIR-V→NIR→QPU) | Runs — the only user-shader Vulkan consumer. No SDL dependency at all: SDL is *entirely* shimmed (`glue/sdl-shim/SDL.h` + `pl_phoenix_sdlcompat.c`). Known open defect: torch sprites intermittently missing (~10–20 % of runs) |

Per engine the recipe carries a `glue/pl_phoenix_*.c` Phoenix backend plus one generated single-ELF patch (`quakespasm` 857 lines, `vkquake` 557, `yquake2` 476, `quake3` 331). vkQuake additionally vendors pre-compiled shaders (`vkquake_shaders.c`, 30 506 lines) and Vulkan entry trampolines (`vk_trampolines.c`, 648 lines).

Two structural notes a maintainer may care about more than the games themselves:

- **The `.so` seam is the recurring problem, not the graphics.** yQuake2 has two dynamic-load seams (game DLL, renderer DLL) and quake3e has three module slots; each port had to be re-plumbed into one link unit. Any Phoenix port of a plugin-architecture application will hit this until `PT_INTERP`/auxv loading exists.
- **Four of the five sit on the ported SDL2** (`depends="sdl2"`), so the SDL video/audio backends in `phoenix-rtos-ports/sdl2/overlay/src/{video,audio}/phoenix/` are the real reusable asset here: ~1 300 lines of driver that any future SDL application on Phoenix inherits for free. vkQuake is the exception and shims SDL away entirely.

### 3. ★ Phoenix gaps found by porting

Each item below is a platform gap, evidenced by a patch that states its own root cause; paths are relative to `phoenix-rtos-ports/`. They are ordered by what they would cost upstream to fix against what they buy — the first five are all small, self-contained libc changes that remove whole classes of future port patches.

A patch in this repo is therefore best read as a **bug report with a workaround attached**, not as a local hack: the workaround stays in the port, but the fix belongs in libphoenix.

1. **★★★ Default pthread stack is 4 KiB with `guardsize` 0 — no guard page.** `supertuxkart/patches/0011-stk-ogg-heap-pcm-buffer.patch`. A `std::thread` (NULL `pthread_attr_t`) gets `ALIGN(PTHREAD_STACK_MIN, PAGE_SIZE)` = 4096 B. STK's ogg decoder wrote a 44 100-byte buffer on that stack and silently overwrote neighbouring mappings. Proven byte-exact, not inferred: a kernel dump of the faulting stack was found verbatim inside decoded `menutheme.ogg` PCM, and two crashes' corrupted return addresses sat exactly 44 100 bytes apart. Symptoms varied per run (jump to a garbage address, or a fault inside `malloc`). **A default stack that small with no guard page turns every stack overrun into silent corruption.**
2. **★★ libphoenix exports `gettime`/`settime` as global symbols** (`libphoenix/include/sys/time.h:34`). gnulib declares its own `gettime`/`settime`, so *every* gnulib-based GNU package collides. Patched identically in two ports — `coreutils/patches/0001-` and `nano/patches/0001-rename-gnulib-gettime-settime.patch` — and will recur for every future GNU port. These two names belong under a reserved prefix (`phoenix_`/`_`) or in a Phoenix-specific header.
3. **★★ No wide-character output at all: no `swprintf`/`vswprintf`/`fwprintf` in libphoenix, and no wide iostreams in libstdc++.** `supertuxkart/patches/0006-irrlicht-phoenix-swprintf-shim.patch` had to hand-write a 106-line wide-printf shim (integers, floats, pointers, wide strings) because Irrlicht — and therefore all of STK — calls `swprintf`; `0009-stk-spinner-no-wide-iostream.patch` replaces `std::wstringstream`. Also `xterm/files/include/wctype.h` is a locally supplied header.
4. **★★ `<fenv.h>` in the sysroot is a poison pill that `#error`s on include** (verified: `.toolchain/aarch64-phoenix/.../usr/include/fenv.h` says "shall not be used as is"), and `aligned_alloc`/`posix_memalign` are absent from `<stdlib.h>` entirely. See `supertuxkart/patches/0004-simde-skip-poison-phoenix-fenv.patch` and `0005-vma-phoenix-aligned-alloc.patch` (which hand-rolls an over-allocating aligned malloc). Both are C11-mandatory surface; any C++/SIMD/graphics library trips over them, and a header that cannot be included is worse than one that returns errors.
5. **★★ gnulib's stdio internals need a Phoenix branch, and there is no public API for them.** `coreutils/patches/0002-port-gnulib-stdio-internals-phoenix.patch` and `nano/patches/0002-port-gnulib-fseterr-phoenix.patch` reach *directly into* `FILE` — `fp->bufeof - fp->bufpos`, `fp->flags & (1<<1)` for `F_WRITING`, `fp->flags |= (1<<3)` for `F_ERROR` — because `<stdio.h>` exposes no `__freadahead`/`__freading`/`__fseterr`. Without the `fseterr` branch gnulib's `#error` fires and nano does not build at all. Exporting these three (as musl/glibc do) removes a whole class of port patches.

Bugs the fork fixed in *its own dependencies* while chasing these, worth mentioning because they were long-lived and silent (`libnfs/patches/`):

- `01-nfs4-renew-lease.patch` adds a synchronous NFSv4 `RENEW`; without it an idle NFSv4.0 lease expires and the client sees `NF4ERR_EXPIRED` surfacing as `ERANGE` / `exec -12`.
- `02-nfs4-st-blocks-512-units.patch`: the v4 path divided used-space by `NFS_BLKSIZE` (4096) instead of 512, so `stat().st_blocks` came back ~8× low and `du`/`ls -s` under-reported. The v3 path was already correct.
- `03-nfs4-open-create-excl-typo.patch`: `flags|O_EXCL` where `flags&O_EXCL` was meant — a bitwise OR that is non-zero for every value — so **every** NFSv4 create was sent as `EXCLUSIVE4`. That carries a random verifier instead of an attribute list, which is why new files came back mode 000 with garbage atimes (measured: 2147411476 — the verifier, not a clock bug).

Further gaps, same evidence standard:

- **No `dlfcn.h` / dynamic-executable loading at port time.** `sdl2/patches/0003-dynapi-disable-on-phoenix.patch`; all five games fold their `.so` seams into one ELF. libphoenix has since gained `dlopen`/`dlsym`/`dlclose` for `ET_DYN` objects, but `PT_INTERP`/auxv is still unimplemented — and the `zlib` recipe documents the sharp edge that follows: a stray `libz.so` in the shared prefix makes the linker prefer it, stamping a `PT_INTERP` requesting `/lib/ld.so.1` into the binary, which then **dies before `main()` with no message** (on 2026-09-04 that shipped Xphoenix, python3, dillo, wmaker, curl and lighttpd all unrunnable).
- **`pthread_getschedparam`/`pthread_setschedparam` are declared but not implemented** — link error, not compile error. `sdl2/patches/0004-systhread-priority-noop-on-phoenix.patch`.
- **`-pthread` is rejected by the driver and there is no `libpthread`** (it is a symlink to `libphoenix.a`), so stock autoconf/CMake pthread probes fail. `sdl2/patches/0001-cmake-phoenix-pthread-detection.patch`; also the cause of a link-line collision fixed in the build repo (§5).
- **No `<semaphore.h>`, no `LC_MESSAGES`, no `CLOCK_PROCESS_CPUTIME_ID`, and `struct rusage` lacks `ru_maxrss`/`ru_minflt`/`ru_majflt`.** `supertuxkart/patches/0008`, `0010`, `0003`.
- **`MSG_TRUNC`/`MSG_CTRUNC`/`SOMAXCONN` missing from the lwIP-backed socket headers**, and `htonl` is a macro not reached transitively. `enet/patches/0001-phoenix-socket-const-fallbacks.patch`, `xorg_libs/patches/libxcb-1.16-phoenix.patch`.
- **Session/pty model gaps:** Phoenix has SysV `setpgrp(void)`+`setsid()` but no BSD `setpgrp(pid,pgrp)` and no `TIOCSPGRP`; the SVR4 `/dev/ptmx` master exists but xterm needed the child slave-open path wired by hand (`xterm/patches/xterm-396-phoenix.patch`, 152 lines, well documented).
- **No file-backed `MAP_PRIVATE` to rely on**, so `llama2/patches/01-phoenix-no-mmap-checkpoint.patch` reads the whole model into RAM and `lighttpd/patches/04-mod-webdav-mmap-guard.patch` guards a no-mmap build.
- **No `/proc` and no `kvm`/`sysctl`,** so WindowMaker's `GetCommandForPid()` had no implementation. `windowmaker/patches/0001-phoenix-getcommandforpid.patch` implements it from the kernel's `threadsinfo()` table — the author marks it upstream-inclinable.
- **`malloc(0)` returned NULL** (found via jq; fixed in `libphoenix/malloc_dl.c` to allocate size 1). Helps every port that assumes `malloc(0) != NULL`.
- **Reported as a strength, not a gap:** `xorg_libs/patches/libX11-1.8.7-phoenix-fontset-basename-ownership-58.patch` is a genuine upstream libX11 bug — `destroy_oc()` `Xfree()`s a `.rodata` string literal. glibc silently tolerated it; **libphoenix's allocator correctly aborted** (status 0x46), which is how it was found. The patch is upstreamable to xorg/libX11 as-is.

### 4. New board / target integration

`phoenix-rtos-build` (`c80264a`, `f05f148`, `makes/include-target.mk`) admits two new generic aarch64 targets, `aarch64a72-generic` and `aarch64a53-generic`, and adds `build-core-aarch64a72-generic.sh` — a core lane that additionally builds `phoenix-rtos-lwip` (`3e4028a`) and links the xHCI HCD against `libvcmbox` plus hosts `usbkbd`/`usbmouse` inside the USB daemon (`aad9a50`, `30f6867`).

`phoenix-rtos-project` adds a conventional shared target (`_targets/aarch64a72/generic/`: `build.project`, `nvm.yaml` — a 32 MB `loader` disk with `plo` at 0 and `kernel` at 0x200000 — `preinit.plo.yaml`, `user.plo.yaml`), a mirror `aarch64a53/generic`, and the board project `_projects/aarch64a72-generic-rpi4b/`. What the board actually required beyond a normal target:

- **`board_config.h`** (113 lines): GIC-400 distributor/CPU bases, PL011 base + 48 MHz clock + an early virtual address, BCM2711 mailbox at `0xfe00b880`, the PCIe outbound window (`0x6_00000000` CPU ↔ `0xf8000000` PCIe) and the VL805 xHCI BDF/class, framebuffer geometry, `PLO_SMP_ENABLE`, and two capacity knobs whose rationale is written into the header: `KERNEL_LOG_SIZE` 2 KiB → 64 KiB (the 2 KiB klog ring overflowed before userspace attached to drain it, making the replayed boot log non-deterministic — a genuinely confusing symptom) and `DUMMYFS_SIZE_MAX` 32 → 256 MiB for RAM-staging game assets.
- **`phoenix-armstub8-rpi4.S`** (464 lines, BSD-3, derived from the Raspberry Pi / Circle armstub8 lineage): EL3 → EL2/EL1 drop, `SCR_EL3`, GIC and local-timer/prescaler init, `CPUECTLR_EL1.SMPEN`, and the spin-table release for cores 1–3.
- **`phoenix-kernel8-reloc.S` + `.lds`** (129 + 26 lines): a position-independent `kernel8.img` trampoline that carries `plo` as a `.payload` section, copies it to `PLO_RPI_PLO_DEST` with cache maintenance and re-inits the UART to 115200 so early failures are visible. It exists because the firmware's load address and plo's link address differ.
- **`config.txt`** (65 lines, mostly comments recording *why*): `armstub=`, `initramfs loader.disk 0x08000000`, `dtoverlay=vc4-fkms-v3d` (fake-KMS so the firmware ungates V3D — its MMIO reads `0xdeadbeef` otherwise — while the firmware framebuffer stays for fbcon), `gpu_mem=128` + `max_framebuffer_height=4096` sized for a triple-height framebuffer, and `uart_2ndstage=0` because VideoCore firmware shares the console UART and once dumped 191 000 lines of xHCI trace into a test run.
- **Three boot variants driven by `RPI4B_VARIANT`**, in `build.project` (285 lines) and `user.plo.yaml` (290 lines):

  - `nfsroot` (default) — a real `/` on NFS, with a second `dummyfs` instance mounted at `/tmp`, because an NFS root cannot host the AF_UNIX node X11 needs at `/tmp/.X11-unix/X0`.
  - `netboot` — dummyfs root, NFS subtree at `/mnt`, plus the `nfs-smoke` triage probe.
  - `sd` — ext2 root on the eMMC2 card, RAM-backed `/tmp`.

  The variant also decides what gets *built*: `nfs` and `nfs-smoke` link the libnfs **port**, which the ports stage builds *after* core, so neither can be a core default component (that would demand a not-yet-built port during a cold-sysroot build). They are built in `b_build_project` per variant instead (`aa177cd`) — a dependency-ordering constraint any project mixing ports into the boot set will meet.
- **`lwip/lwipopts.h`** (119 lines) with a documented gigabit tuning series: `LWIP_TCPIP_CORE_LOCKING_INPUT=1` (~1.8× RX, `d2c4a6f`), `LWIP_CHKSUM_ALGORITHM=3` moved into the lwIP `arch/cc.h` to avoid clashing with a stock build (`b49fb77`), `TCP_WND` 32→44×MSS (`0993e81`), `LWIP_INGRESS_CREDIT` (`07ba705`) — NFS read 26.3 → 29.9 MB/s. A second netif token `wifi43455` is registered (`0281848`).
- **Two further projects ride the same target definitions:** `_projects/aarch64a53-generic-rpi4b/` (an A53-flavoured Pi 4 project — same SoC, generic-A53 core settings, its own `config.txt`; commit `22376b7` corrects its GIC-400 base addresses) and `_projects/aarch64a53-generic-qemu/` with `scripts/aarch64a53-generic-qemu.sh`, which gives the aarch64 work a QEMU lane that needs no board at all. Useful precedent: the generic `_targets/aarch64aXX/generic` split means a third aarch64 board should need only a `_projects/` directory.
- Also: a 1120-line `busybox_config`, a rootfs overlay (`etc/rc.psh`, `etc/ntp.conf` for boot-time clock sync, a curses smoke test), firmware staging + DTB staging helpers (`rpi4b_stageDtb`, `rpi4b_stageFirmware`, with an optional `fdtput` memory patch for the QEMU lane), and `ports.yaml` (266 lines) — the per-project port selection list, whose comments record the deliberate `if: false` → `if: true` promotion order (sdl2 → X11 stack → dillo/nano/mc → python → games).

### 5. Build system improvements

Everything here is board-independent and reusable. The common theme is *silent staleness*: in each case the build produced a plausible, working artefact that did not contain the change that had just been made, and in each case the fix is a dependency or invalidation key that upstream's build was missing rather than a workaround.

- **★ Relink every program when libphoenix changes.** `makes/binary.mk` + `Makefile.common` (`382d7dd`, `a80c1fe`). Most components never name libphoenix in `LIBS` — they get it from the sysroot — so their link rule had no dependency on it. On 2026-09-04 a libphoenix stdio fix left **119 of 336 rootfs ELFs still linked against the previous libc**; the ABI was unchanged, so they worked, which is precisely why nobody would notice. The fix adds `$(wildcard $(PREFIX_SYSROOT)/lib/libphoenix.a)` as a prerequisite. Its own footgun is fixed in the same series: prerequisites are expanded *inside* `--whole-archive`, and since `libc`/`libm`/`libpthread` are all symlinks to `libphoenix.a`, the sentinel collided with `-lpthread` ("multiple definition of `setsid`" ×40) — hence `LINK_INPUTS = $(filter-out $(LIBC_RELINK_DEP),$^)`.
- **★ `port_manager` staleness model** (`5868ef9`, `4361efc`, `f44fd69`, `49ca648`, `f05ded2`, `24bf9c1`, `fc3de5c`, `15d5fc6`, `925550b`, `18c3e04`). Three independent silent-stale-build classes closed:

  - *Frozen libc feature detection.* autoconf and CMake cache their probe answers, so a port configured before libphoenix gained `strtok_r` kept compiling its own `static` fallback forever — and once the libc **header** declares the function that fallback stops being redundant and becomes a hard error (`evdns.c: static declaration of 'strtok_r' follows non-static declaration`, which broke the entire ports stage). The fix fingerprints libphoenix's exported symbol list and drops configure markers only when the **libc API** actually changes, so an ordinary rebuild does not force everything to re-configure. 16 autoconf + 11 CMake ports shared the bug; ffmpeg was additionally missed because it writes `config.log` under `ffbuild/` rather than at the root.
  - *Filename-keyed patch markers.* A regenerated patch with an unchanged name was silently skipped and the port kept building the old patch's source with no diagnostic — not theoretical here, since the game patches are generated from forks. Markers are now keyed on **content hash**, and the refusal is self-healing: it removes the stale work directory and says to re-run, one failure instead of a permanent manual step (`PHOENIX_PORT_KEEP_STALE_WORKDIR=1` opts out).
  - *Dependency and recipe edits.* A dependency's **version** change, and editing a recipe at all, now rebuild the dependents.
- **★ `ports.mk` second source of truth documented.** `PORTS_SUPPORTED_VERSIONS`/`PORTS_DEFAULT_VERSIONS` expand into `-I`/`-L` flags for every make-built component, so a version there that no longer matches the recipe points the whole system at a `versioned-ports/openssl-<old>/` directory the ports stage never creates. Bumped to 1.1.1w with a comment naming the hazard.
- **★ Pin the language standard.** `Makefile.common`: `CFLAGS += -std=gnu17`, `CXXFLAGS += -std=gnu++17`, placed before the `EXPORT_*FLAGS` capture so ports inherit it. gcc ≥ 15 defaults to C23, where an implicit function declaration is a hard error — which breaks every gnulib-based port. No-op on gcc-14.
- **★ `PhxVersion` learns upstream letter patch-releases** (`575632a`, `port_manager/version.py`). openssl ships `1.1.1`, `1.1.1a` … `1.1.1w`. PEP 440 knows only `a`/`b`/`c`/`rc` and reads them as **pre**-releases, so `PhxVersion("1.1.1w")` raised `InvalidVersion` and aborted `discover_ports()` outright, while the letters that *did* parse sorted backwards (`1.1.1a < 1.1.1`, the opposite of upstream's meaning). A trailing letter is now translated to a PEP 440 post-release (`a → .post1` … `w → .post23`), giving `1.1.1 < 1.1.1a < 1.1.1w < 1.1.2` for all 26 letters, while `__str__` still returns the original string so namevers and install directories read `1.1.1w`. Covered by doctests and `port_manager_test.py`.
- **`port.subr` grows two public helpers** — `b_port_apply_patches` (content-hashed markers, self-healing refusal) and `b_port_invalidate_stale_configure` — plus a `b_port_download` form that saves a remote file under a different local name, which is what makes GitHub's `/<tag>.tar.gz` and `/archive/<sha>.tar.gz` endpoints usable as reproducible, hash-pinned sources. Seven ports here pin a commit archive that way.
- **Toolchain rebase to gcc-16.2.0 + binutils-2.47** (`f90bc71`, `96f5697`, `20bc28f`): `binutils-2.47-04-aarch64-phoenix.patch` (BFD/config.bfd target vector), `gcc-16.2.0-11-aarch64-phoenix.patch` (`aarch64*-*-phoenix*` in `config.gcc`), `-05-libstdcpp.patch` (265 lines, libstdc++ PIC configury), `-09-fix-libc-spec.patch` (`STD_LIB_SPEC` → `LIB_SPEC`), `-04-arm-pic_crtstuff.patch`. Plus multi-mirror GNU fetch (`643293e`), `-j nproc` for toolchain and image builds (`7463000`), and autotools host-triplet normalisation for aarch64 (`2a6aebb`).
