# Port feature audit: compile-time cuts in the rpi4b image (2026-09-30)

This is a read-only audit. Nothing was edited or built.

**Scope.** Every port in `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml`, plus the libraries those ports pull in through `depends=`.

**Evidence used.**
- **Recipes:** `port.def.sh`, `patches/` and `files/` under `sources/phoenix-rtos-ports/<port>/`.
- **Configure results:** the ports' own build directories were wiped by the running build 5 (full-clean). The configure evidence therefore comes from the ad-hoc mirror builds the ports were derived from:
  - `tools/gpu-lane/*/build-out*/…/meson-logs/meson-log.txt`, `config.log` and `CMakeCache.txt`
  - job logs `build4.log` and `gfx-B.log`
  - older `config.log` files in `.buildroot-gcc16` and `phoenix-rpi-rollback`
  - `pyconfig.h` from the live build 5, which had just finished python
- **Object code:** the static archives and programs build 5 produced so far in `.buildroot/_build/aarch64a72-generic-rpi4b/{lib,prog}`.

Rows marked (✔) were re-checked against the primary source by me. The others come from five parallel sub-audits, and I spot-checked their file:line citations.

**Libc baseline, verified.** This corrects the one given in the brief.
- **Absent from libphoenix:**
  - Process and IPC: `posix_spawn`, `shm_open`, `sem_*`, `sem_open`, SysV `shmget`
  - Descriptors and events: `pipe2`, `dup3`, `memfd_create`, `F_ADD_SEALS`, `inotify`, `epoll`, `eventfd`, `timerfd`, `signalfd`, `futex`
  - Memory and loader: `madvise`, `mremap`, `msync`, `posix_fallocate`, `dl_iterate_phdr`
  - Threads and CPU: `pthread_setname_np`, `sched_getaffinity`, `getauxval`
  - Locale and text: `newlocale`/`uselocale`, `gettext`, `iconv_open`
  - Hardening: `__stack_chk_fail`/`__stack_chk_guard`, all `__*_chk`
  - Credentials and ttys: `setresuid`, `getspnam`, `utmp`, `posix_openpt`/`openpty`
  - Headers: `threads.h`, `uchar.h`, `spawn.h`, `ftw.h`
  - Flags: `O_NOFOLLOW`, `O_DIRECTORY`
  - `unistd.h:73` defines `_POSIX_FALLOC -1`, and `:127-128` says "no posix_spawn, no shm_open, no sem_*".
- **Present:** `dlopen`/`dladdr`, `backtrace`, `crypt`, `getrandom`, `clock_nanosleep`, `vfork`, `accept4`, `mkostemp`, `pthread_barrier_*`, `pthread_cond_clockwait`, `_SC_NPROCESSORS_ONLN`.
- **From kernel headers:** `SO_PEERCRED`, `SCM_RIGHTS`, `O_CLOEXEC`, `SOCK_CLOEXEC`, `F_DUPFD_CLOEXEC`.
- **`MAP_SHARED` is `0x0`**, the same value as `MAP_PRIVATE` (`phoenix-rtos-kernel/include/mman.h:28-29`) (✔).
- Kernel `SCTLR_EL1.A = 0`, so unaligned access at EL0 is legal (`hal/aarch64/_init.S:285,316`) (✔).

---

## 0. The headline: most autotools ports are built at `-O0` (✔)

`phoenix-rtos-build/Makefile.common:160` snapshots `EXPORT_CFLAGS := $(CFLAGS)` **before** `CFLAGS += $(OLVL)` at `:179`. `port_manager/port_internal.subr:40` then hands ports exactly that snapshot:

```
-mcpu=cortex-a72 -mtune=cortex-a72 -fomit-frame-pointer -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections -std=gnu17
```

There is no `-O` in it. Autoconf adds its default `-g -O2` only when `CFLAGS` is unset, and the recipes all set it. Some recipes replace `CFLAGS` wholesale: the X11 ones and the literal strings in `xorg_fonts` and `xorg_libs`. Those get **neither `-O` nor `-mcpu`**.

**Measurement.** I measured the share of instructions that are stack-slot loads and stores (`[sp|x29]`). Optimised code sits at about 7–15%; `-O0` code sits at 30–50%. I also disassembled one function per library to confirm.

| Built at -O0 today | stack-slot share | Why it matters |
|---|---|---|
| **libnfs** | 34.5% | linked into the **NFS-root filesystem server** (`phoenix-rtos-filesystems/nfs/Makefile:26`); every netboot file operation. `nfs_get_readmax` is textbook `-O0` (spill, reload, reload) |
| **mbedtls** (libmbedcrypto) | 37.7% | TLS handshakes in curl, dillo and STK online. `mbedtls_aes_crypt_ecb` spills every argument |
| liblzma / xz | 39.6% | `xz`, Python `_lzma` |
| bash | 24.1% of the whole binary (libc included); `xmalloc`/`sh_single_quote` are `-O0` | every shell script |
| freetype | 39.6% | every glyph rasterised or hinted: GTK, labwc, X, games |
| libpng16 | 38.9% | every Adwaita icon decode. Its NEON filter intrinsics run unoptimised |
| fontconfig, expat | 35.4%, 32.1% | `FcFontSort` at every GTK app start |
| pixman 0.42 (X stack) | 41.7% | C fallbacks. The NEON assembly is unaffected. The Wayland stack's pixman 0.46 is meson `-O2` and fine |
| libX11, libxcb | 32.6%, 51.6% | X desktop |
| libffi, libiconv | 37.9%, 33.4% | GObject marshalling (`ffi_call` on signal emission), charset conversion |
| pcre, oniguruma (jq), jansson, lzo, libevent | 30–44% | |
| lighttpd, wpa_supplicant, xterm, windowmaker, xorg_apps | recipes carry no `-O` (lighttpd/wpa's own `-O2` defaults are overridden by the env `CFLAGS`) | |

**Already optimised**, because their recipes add it:
- `-O2`: coreutils, grep, sed, tar, gzip, bzip2, sqlite3, dropbear, jq's own code, redis, lua, nano, mc, ncurses, dillo, glib2, cairo 1.16
- meson `debugoptimized`: all new-lane meson ports
- CMake `Release`: libjpeg, zlib, harfbuzz
- `-O3`: python
- `-Os`: openssl, busybox, fltk, micropython

Several recipes already work around the problem in their comments. coreutils, for example, says "-O0 would disable gnulib's inline helpers and break the link".

**Fix:** row 1 of the table below and Batch 0 in §3.

---

## 1. The top 25 opportunities, ranked

Cost scale:
- **S:** an option or line change with no new dependency.
- **M:** a patch, a new small pure-C dependency, or a libc addition with tests (libc additions need `--scope core` plus the stale-core check).
- **L:** a new subsystem, or kernel work.

| # | Port(s) | Disabled / missing | Gain (concrete) | Cost (exact dep / system feature) | Recommendation |
|---|---|---|---|---|---|
| 1 | **all autotools ports** (✔) | No `-O` exported to ports (`Makefile.common:160` vs `:179`). The X11/fonts recipes also drop `-mcpu` | **Perf, system-wide.** Covers the NFS-root client (libnfs), TLS crypto (mbedtls), fonts, icons, pixman fallbacks, X11, bash, xz. The typical `-O0` → `-O2` factor for such C code is 2–4×. This is an estimate; measure NFS read MB/s and GTK app start before and after | **S change, M verification.** Add `EXPORT_CFLAGS += $(OLVL)` and `EXPORT_CXXFLAGS += $(OLVL)` after `Makefile.common:180` (a recipe's own later `-O` still wins). Also put `-O2 -mcpu=cortex-a72 -mtune=cortex-a72` in the literal `CFLAGS` strings at `xorg_fonts:109,137,177,209,229`, `xorg_libs:132,190,220`, `xterm:106,123`, `xorg_apps:89` and `windowmaker:115`. Risk: `-O2` can expose latent UB in never-optimised code | **Do first**, as its own full-clean build plus the showcase gate plus the libc tests |
| 2 | mesa_drm (vulkan variant) (✔ `:137`) | `-Dshader-cache=disabled` (G4) | **vkQuake first frame ~75 s → seconds on the 2nd and later starts.** STK's warm-up is shorter too | **S build, M verification.** v3dv keys on `driver_build_sha1`, which patch 0011 already derives without a build-id. Needs two launcher env vars: **`MESA_DISK_CACHE_DATABASE=1`** (the default MULTI_FILE index is `mmap(MAP_SHARED)`, and `MAP_SHARED` is 0 on Phoenix) and `MESA_SHADER_CACHE_DIR=<persistent dir>`. Mix a per-build salt into the key, or a local rebuild reuses stale blobs (the G3 speckle). The GL variant also needs a small patch to `v3d_disk_cache.c:55` (`build_id_find_nhdr_for_addr` exists only `#ifdef HAVE_DL_ITERATE_PHDR`), or `dl_iterate_phdr` in libphoenix (row 16) | Do the Vulkan variant first, then GL |
| 3 | libjpeg (✔ `:62`) | `-DWITH_SIMD=0`. The comment "needs the aarch64 NEON asm path wired" is stale: with GCC ≥ 12, libjpeg-turbo 3.0 uses NEON **intrinsics** and `init_simd` enables NEON unconditionally on aarch64 | NEON JPEG decode (libjpeg-turbo's own claim is 2–6×) for gdk-pixbuf (Thunar, Atril, wallpapers), dillo, fltk and STK | **S:** `-DWITH_SIMD=1`, then check that `nm libjpeg.a` shows `jsimd_*_neon` | Batch 1 |
| 4 | python (✔ `pyconfig.h`: `USE_COMPUTED_GOTOS` undef) | Computed gotos silently off: the cross build takes configure's `no` default | Faster eval-loop dispatch. Upstream quotes ~10–20%; not measured here | **S:** add `--with-computed-gotos` at `python/port.def.sh:125` | Batch 1 |
| 5 | python (`Setup.local`) | No `pyexpat`, `_elementtree`, `_asyncio`, `termios`, `_lsprof`, `syslog` or `_multibytecodec` + `_codecs_*` | **Features:** `xml.etree`/`minidom`/`plistlib`/`xmlrpc` (all broken today); C asyncio (the pure-Python fallback is slow); `tty`/`getpass`/PyREPL; cProfile; CJK codecs | **S:** everything is bundled in CPython (`Modules/expat`). One Setup.local line each. The port's "PyInit_* module set" check must be updated | Batch 1 |
| 6 | micropython (`files/001_mpconfigport.mk:31`) | `MICROPY_SSL_AXTLS = 1` takes precedence over `MBEDTLS = 1` (:34) | **Security:** axTLS is abandoned (TLS 1.2 with old ciphers, no working CERT_REQUIRED). The bundled mbedtls 3.6 replaces it | **S:** set `MICROPY_SSL_AXTLS = 0` | Batch 1 |
| 7 | curl 7.64.1 | Legacy protocols all on (dict, gopher, telnet, tftp, smb, rtsp, ldap, imap, pop3, smtp). The version is from 2019 (TFTP heap-overflow CVE-2019-5482, STARTTLS CVE-2021-22946/7, credential leaks 2022–2025). TLS is mbedtls 2.28, which is EOL | **Security:** smaller attack surface now. With 8.x on OpenSSL 3.5: TLS 1.3 and years of fixes | **S:** add `--disable-{dict,gopher,telnet,tftp,smb,rtsp,ldap,imap,pop3,smtp}` at `curl/port.def.sh:46`. **M:** bump to 8.x with `--with-openssl` (openssl 3.5.9 port exists) and drop `use: [mbedtls]` | S part in Batch 1; bump in Batch 3 |
| 8 | libphoenix → all ports | No `__stack_chk_guard`/`__stack_chk_fail`, so `-fstack-protector-strong` cannot link. dropbear builds `--disable-harden` (`dropbear/port.def.sh:48`) | **Security:** stack-smash detection in every network-facing program (dropbear, lighttpd, curl, redis, wpa_supplicant). Cost ~1–3% runtime | **M (libc):** ~20 lines. A global guard (aarch64's default `-mstack-protector-guard=global`), seeded from `getrandom()` in `crt0-common.c`, which must itself be built `-fno-stack-protector` (there is no ASLR, so per-boot seeding matters). Plus a libc test. Then add the flag to the export flags (row 1) and drop `--disable-harden`. FORTIFY is a separate M item (musl-style inline `*_chk` wrappers) | Batch 2 |
| 9 | labwc_desktop: foot + fcft (`:574,:588`) | foot `-Dgrapheme-clustering=disabled`, fcft `-Drun-shaping=disabled`, both because libutf8proc is missing | **Features:** foot's cell widths are correct **despite P7**. Today `c32width()` → libphoenix `wcwidth()` is 1 for everything, so CJK and emoji are mis-sized. Also ZWJ/combining clusters, and fuzzel shapes whole runs (ligatures, Arabic/Indic) | **S–M:** libutf8proc (MIT, 2 C files, no OS deps) as a sub-package. Link it into foot and fuzzel (`:659,:668`) | Batch 3 (first) |
| 10 | gtk3_wayland: gdk-pixbuf (✔ `:563-564`) | `-Dgif=disabled -Dothers=disabled -Dbuiltin_loaders=png,jpeg` | **Features:** GIF (including animated), BMP, ICO, XPM, PNM, TGA, XBM, ANI in every GTK app and Thunar | **S:** all in-tree pure C. `-Dgif=enabled -Dothers=enabled -Dbuiltin_loaders=png,jpeg,gif,bmp,ico,xpm,pnm,tga,xbm,ani` | Batch 1 |
| 11 | openssl 3.5.9 (✔ `30-phoenix.conf:57`) | `-Os` for aarch64. `ec_nistp_64_gcc_128` is not enabled | **Perf:** the C-only hot paths (X25519 has no aarch64 asm; the ML-KEM part of the 3.5 default hybrid group X25519MLKEM768; P-224/P-521) | **S:** `-Os` → `-O2` on line 57. Add `enable-ec_nistp_64_gcc_128` to Configure (`openssl/port.def.sh:49`). Measure with `openssl speed x25519 ecdhp256` | Batch 1 |
| 12 | video_player: FFmpeg (✔ `files/components.sh:12-23`) | `--disable-autodetect` leaves `CONFIG_ZLIB 0` although zlib is in `depends`. The decoder, demuxer and filter lists are minimal | **Features:** MKV compressed headers and MOV `cmov`; MPEG-1/2 (DVD, TS broadcast); AC-3/E-AC-3/DTS/ALAC audio; deinterlacing; VOB/FLV/IVF containers | **S:** `--enable-zlib` in `FF_COMMON`. Add `mpeg2video,mpeg1video,ac3,eac3,dca,alac` to `FF_DECODERS`, `bwdif,yadif` to `FF_FILTERS`, `mpegps,flv,ivf` to `FF_DEMUXERS`, and `mpegvideo,ac3,dca` to `FF_PARSERS`. All native LGPL; the port's GPL check (`:185`) stays valid | Batch 1 |
| 13 | FFmpeg, then other codec/compression ports | Global `-mstrict-align` (`target/aarch64.mk:20`; also hard-coded in the new-lane meson cross files, e.g. `gtk3_wayland:300`, `libdrm_phoenix/glue/newlane.subr:41`) | **Perf:** FFmpeg's `HAVE_FAST_UNALIGNED 1` bitstream readers compile to 4×`ldrb`+`orr` instead of one `ldr` (seen in `h264_cavlc.o`). The same applies to zlib, xz, sqlite and pixman's C paths | **M:** port-scoped `-mno-strict-align`, only for code that touches Normal memory (FFmpeg only touches malloc'd buffers). Needs a bench (1080p decode fps) and an audit of any Device/uncached mapping. **Not** a global change | Batch 3, FFmpeg first |
| 14 | gtk3_wayland: glib 2.88 (`:557`) | `file_monitor_backend=auto` finds nothing (no inotify/kqueue); this is P19 | **Features:** Thunar views, xfdesktop and panel launchers refresh when files change | **M, no kernel work:** a `GLocalFileMonitor` subclass `GPhxPollFileMonitor` (~300 lines). It polls every 2 s on the GLib worker context and reuses `gio/kqueue/dep-list.{c,h}` for the directory diff. Touches `meson.options`, `gio/meson.build:823-860` and `giomodule.c`, then `-Dfile_monitor_backend=phoenix-poll` | Batch 3 |
| 15 | libphoenix → glib, XFCE | `posix_spawn` absent. glib `gspawn-posix.c:1480` therefore uses `fork()` (+ a grandchild fork) and closes every fd by looping (no `close_range`/`fdwalk`) | **Perf and robustness:** every XFCE menu or panel launch, Thunar "open" and `g_spawn_*` forks a large GTK process instead of vfork+exec. This is fork+exec, the C3 hazard class (fixed in build 18, but still the costly path). CPython already uses vfork (`HAVE_VFORK 1`) | **M (libc):** a vfork-backed `posix_spawn`/`posix_spawnp` + `<spawn.h>` + file actions (open/close/dup2) + attrs (`SETSIGMASK`, `SETSIGDEF`, `SETPGROUP`, `SETSID`). glib 2.88 probes it (log: `Checking for function "posix_spawn" : NO`) | Batch 2 |
| 16 | libphoenix: promote the per-port compat code | `pipe2`, `pthread_setname_np`, `posix_openpt`, unnamed `sem_*`, C11 `threads.h`, `uchar.h` (`mbrtoc32`/`c32rtomb`), `newlocale`/`uselocale`, `msync` and `dl_iterate_phdr` exist as copies in `wayland_phoenix/files/compat` and `labwc_desktop/files/compat`, or not at all | **Features and fewer copies:** glib gets atomic CLOEXEC pipes (`HAVE_PIPE2`) and thread names. micropython threads (`THREAD=0`, "missing semaphore.h") and mc subshell/Python `pty` get `posix_openpt`. The Mesa GL disk cache gets `dl_iterate_phdr` (static binary: walk `__ehdr_start`'s PHDRs), which also retires Mesa patch 0011's workaround. Removes ~800 lines of per-port shims | **M (libc):** each piece is S–M with host tests. `sem_open`/`shm_open` (named, cross-process) are L | Batch 2 |
| 17 | sqlite3 (✔ `:24-30`) | No `HAVE_PREAD`/`HAVE_PWRITE` (sqlite only auto-defines them for `__linux__`/`__APPLE__`); `SQLITE_OMIT_LOAD_EXTENSION` | **Perf:** one syscall per page instead of `lseek`+`read`. **Features:** SQL math functions; `.load` extensions now that dlopen exists | **S:** add `-DHAVE_PREAD=1 -DHAVE_PWRITE=1 -DHAVE_LOCALTIME_R=1 -DSQLITE_ENABLE_MATH_FUNCTIONS -DSQLITE_DEFAULT_MEMSTATUS=0` (libphoenix declares `pread`/`localtime_r`). Keep OMIT_LOAD_EXTENSION until someone needs it | Batch 1 |
| 18 | xz 5.4.7 (`:56`) | `--disable-threads`: "single-threaded target" is stale (4-core SMP works) | **Perf:** `xz -T0` uses 4 cores | **S–M:** bump to **≥ 5.8.1** first (CVE-2025-31115 is in the 5.3.3–5.8.0 threaded decoder), then `--enable-threads=posix` | Batch 3 |
| 19 | labwc_desktop (`:579`) | `-Dlabnag=disabled`: never tried. All its deps (cairo, pangocairo, wayland-client/cursor, xkbcommon, compat) are already built | **Features:** labwc's Prompt/If action dialogs and its config-error bar | **S:** drop the flag, add a `_meson_objs` target, hand-link like swaybg, stage `/bin/labnag` | Batch 1b |
| 20 | atril_wayland (`:350`, `:355`, `:358`) | `-Denable_dbus=false`, `-Dpixbuf=disabled`, poppler `-DENABLE_UTILS=OFF` | **Features:** single instance (atrild raises the existing window); open PNG/JPEG in Atril; `pdfinfo`/`pdftotext` on the Pi | **S:** `enable_dbus=true` and stage atrild. **S:** `ENABLE_UTILS=ON` and stage 2 tools. **S–M:** `pixbuf=enabled` plus about 30 lines in patch 0004's built-in-backend list | dbus + utils in Batch 1b; pixbuf in Batch 3 |
| 21 | **Security version debt** (upgrades, not flag flips) | redis 7.2.4 (**CVE-2025-49844 "RediShell", Lua UAF RCE, CVSS 10**, plus 5 more); jq 1.7.1 (CVE-2024-23337, CVE-2025-48060); busybox 1.27.2 (awk CVE-2021-42378..86, CVE-2023-42363..66; gunzip CVE-2021-28831; ash CVE-2022-48174); mbedtls 2.28 (EOL, no TLS 1.3); PCRE1 8.42 (EOL; CVE-2019-20838, CVE-2020-14155) | **Security** | redis ≥ 7.2.11 and jq 1.8.x: S–M each. busybox: **S** now, by dropping the applets the GNU ports already cover (gunzip, bunzip2, unxz, tar, sed, grep); **L** to upgrade to 1.37 (23 patches). mbedtls: M (move consumers to OpenSSL, group H). PCRE2 port: M (group I) | redis + jq + busybox applet trim early |
| 22 | xorg_fonts: freetype (✔ `:138`) | `--without-png`: this is F5, no colour emoji | **Features:** NotoColorEmoji (CBDT) in fcft, cairo, STK | **M:** libpng16 is built earlier in the same recipe (`:104`), but every static freetype link needs `-lpng16 -lz`: fontconfig `:207`, Xft `:226`, cairo `:240`, the gtk3_wayland freetype2 view `:404`, the harfbuzz `FREETYPE_LIBRARY`, STK and fltk. Also make the `libpng` port the single producer of `libpng16.a` (today there are two, with no dependency edge) | Another agent is already on F5; coordinate |
| 23 | xorg_server_drm (`:162`) | `-Dinput_thread=false` + phxhid's 10 ms timer, justified by "poll … on the kernel's 20 ms cycle". That reason is obsolete: G12/P9 was closed by `pollNotify` | **Perf:** lower X input latency; the cursor stays live while glamor or clients block the main loop | **M:** make phxhid readiness-driven (`xf86AddEnabledDevice`), then `input_thread=true` | Batch 3 |
| 24 | dbus (`files/conf/session-phoenix.conf:21-22`, `port.def.sh:153`) | Session bus auth is **ANONYMOUS + `<allow_anonymous/>`** ("LAB ONLY"). `verbose_mode`, `checks` and `stats` are on | **Security:** any local process can own names on the session bus | **S–M:** either `DBUS_COOKIE_SHA1` (needs a writable keyring dir; `HOME=/root` is on the NFS root, so test it), or add a Phoenix `SO_PEERCRED` case to glib `gcredentialsprivate.h` (the kernel has it) and stage `session-phoenix-external.conf`. `verbose_mode=false`: S, but check first whether `xfce-desktop.sh` `VERBOSE=1` relies on it | Batch 3 |
| 25 | meson ports (gtk3_wayland `:376`; wayland_phoenix, labwc_desktop `:359,:390`) | `debugoptimized` with `-Db_ndebug=if-release`, so every `assert()` stays in cairo, pixman, pango, glib, wlroots and labwc hot paths | **Perf, small:** a few % in rendering and compositing paths | **S:** `-Db_ndebug=true` (libepoxy already does, `:63`). ⚠ It removes asserts that may currently catch C1/C3-class corruption early: decide deliberately | Batch 1 (owner's call) |

**Next ten**, all S–M and all lower value:
- fuzzel icons: `icons-enabled=yes` plus stage the adwaita icon theme. PNG and nanosvg are already compiled in.
- Stage the XFCE settings programs that are already built, about 17 MB each: xfdesktop-settings, xfce4-mime-settings/exo-open, xfce4-settings-editor, xfce4-display-settings.
- Quake music: vkquake/quakespasm `-DUSE_CODEC_VORBIS`/`WAVE` plus `libvorbisfile`/`libvorbis`/`libogg`. Useful only with non-shareware data.
- yquake2: re-measure `YQ2_GL3_MIPMAP=1` on the new stack. The "minutes" cost was measured on the old lane.
- fltk `--disable-xft`: its comment says the libs are "not in the stack", which is stale now that xorg_fonts ships Xft. This gives anti-aliased dillo.
- lighttpd `mod_deflate`: `use: [zlib]` is currently dead weight.
- curl threaded resolver.
- libxkbcommon xkbregistry (needs libxml2).
- Thunar UCA/SBR plugins (needs a static plugin-linking patch like panel 0001).
- tumbler (M core; L with a video thumbnailer).
- libdav1d for AV1 (M, new meson port).

**Not worth doing on this board:** Xwayland/GLX (L), VT/seat/logind/udev, SELinux/AppArmor, systemd, PAC/BTI (the A72 is ARMv8.0), PIE/RELRO (fully static, no ASLR), Mesa LLVM/valgrind/perfetto, GTK print backends (plugins need a static-link patch), and libinput pointer acceleration in the shim (S–M; it's a nice-to-have, not a cut).

### Stale comments and patches found (cleanup, zero risk)
- `wayland_phoenix/patches/wayland/0001`: its `#elif defined(__phoenix__)` peercred branch is **unreachable**. Upstream's `#elif defined(SO_PEERCRED)` now matches first, because the kernel has `SO_PEERCRED`. Only the `MSG_CMSG_CLOEXEC` hunk still does anything.
- These comments are wrong or out of date:
  - `libjpeg/port.def.sh:40-43`: SIMD "needs asm wiring" (row 3).
  - `xz/port.def.sh:47`: "single-threaded target".
  - `fltk/port.def.sh:43-46`: Xft "not in the stack".
  - `sdl2_kmsdrm` patch 0003: "no dlopen()/dlfcn.h".
  - GTK patch 0002: "cairo has no PDF/PS", but cairo 1.18 does.
  - `glib2/glib2.cache`: `langinfo_codeset=no`, `have_strlcpy=no` and "stub -liconv" (it is real GNU libiconv 1.18).
  - `xorg_server_drm` phxhid: the "20 ms cycle" justification.
- `xorg_libs:164` `--disable-mitshm` is an **unrecognised** option: a no-op.
- `libevent` has no shipped consumer (only openiked, which is not in ports.yaml). Its dead weight is built at `-O0`.
- `glib2`: GIO is absent, so the P19 fix cannot reach mc's glib (fine; mc does not monitor).
- `GSETTINGS_BACKEND=memory` (`xfce-desktop.sh:101`): the keyfile backend is compiled in. But `XDG_CONFIG_HOME` is under `/tmp` by design ("not on the NFS root"), so switching only persists settings within one boot. Low value.

---

## 2. Changes grouped by the system feature they share

| Group | System feature | Unlocks | Cost |
|---|---|---|---|
| **A. Port build flags** (framework) | Export `$(OLVL)` to ports (`Makefile.common:160/179`). Optionally also export `-z noexecstack` (added only after the snapshot, `:167`) and `-ftrivial-auto-var-init=zero` (compiler-only hardening, 0–2%) | Row 1. With B, also `-fstack-protector-strong` everywhere | S change, one full-clean build plus gate |
| **B. libphoenix hardening** | `__stack_chk_guard`/`__stack_chk_fail` seeded in crt0; later the fortify `__*_chk` family | Stack protector for every port; dropbear `--enable-harden`; `_FORTIFY_SOURCE=2` stops being a silent no-op | M (the SSP part is ~20 lines plus a test); fortify is M |
| **C. libphoenix POSIX gap fill** (promote the compat) | `posix_spawn` (vfork-backed), `pipe2`, `dup3`, `pthread_setname_np`/`getname_np`, `posix_openpt`, unnamed `sem_*`, `<threads.h>`, `<uchar.h>`, `newlocale`/`uselocale`, `dl_iterate_phdr`, `msync` | glib (spawn, atomic CLOEXEC, thread names); micropython threads; mc subshell; Python `pty`; Mesa GL shader cache and dropping Mesa patch 0011's workaround; foot/wlroots/GTK shedding ~800 lines of per-port compat. After a libc change, **every port's configure must re-probe** (the ports-staleness model: a libc symbol change must trigger a reconfigure, as `video_player`'s `NL_LIBC_SYMS` stamp already does) | M overall; each item S–M with host tests (the libc host harnesses exist) |
| **D. Kernel file semantics** | `O_NOFOLLOW`, `O_DIRECTORY` (today #defined to 0 in `dropbear/localoptions.h`, `labwc` compat, glib patch 0002); `setresuid`/`setresgid` | Security: symlink-race safety in scp, the GLib trash code and wlroots; dropbear privilege separation (`DROPBEAR_SVR_DROP_PRIVS`) | M (kernel plus libc) |
| **E. Change notification** | Userspace: a glib polling monitor (row 14). Kernel: an inotify-like API | Userspace fix: Thunar, xfdesktop, panel launchers, tumbler, xfconf. A kernel API would also give dbus config reload and foot/labwc config reload | M (glib) / L (kernel) |
| **F. Shared memory semantics** | Real `MAP_SHARED` (it is 0 today); SysV `shmget` over shmsrv; memfd **sealing** (`F_ADD_SEALS`); growable, non-contiguous shmsrv objects (today power-of-two ≥ 1 MiB, contiguous, fixed size) | Mesa MULTI_FILE cache without the DATABASE workaround; Xorg `mitshm` (fast PutImage for software clients); windowmaker shm; foot's scrollable shm pool; Python `_posixshmem`. Sealing only protects a compositor against a *malicious* client shrinking a pool, which is **low value on a single-user Pi** | L |
| **G. UTF-8 in libc** (P7, the group root) | A UTF-8 multibyte layer plus `wcwidth` tables | nano `--enable-utf8`, ncurses `--enable-widec`, mc's codeset, busybox UNICODE, Python and GTK text, foot without utf8proc. Row 9 is the cheap bypass for foot and fcft only | M (well-bounded, host-testable; see P7) |
| **H. One TLS library: OpenSSL 3.5** | The openssl 3.5.9 port exists and is CA-verified | curl 8.x (TLS 1.3), dillo (`--disable-openssl` today), redis `BUILD_TLS=yes`, FFmpeg https (`--enable-openssl` is LGPL-compatible in 6.1), and retiring EOL mbedtls 2.28. micropython uses its own bundled mbedtls 3.6 (row 6) | M per consumer |
| **I. PCRE2 as a shared port** | PCRE2 10.47 is already built *privately* inside gtk3_wayland | `grep -P`, lighttpd `--with-pcre2`, retiring PCRE1 8.42 (EOL, CVEs); optionally the pcre2 JIT (`PCRE2_SUPPORT_JIT=OFF` today; needs anonymous `PROT_EXEC` mmap on Phoenix, which is unverified) | M |
| **J. Shared image-codec ports** | libjpeg SIMD (row 3, S); new pure-C ports: libtiff, libwebp, libexif | gdk-pixbuf TIFF/WebP (Thunar, wallpapers), Atril TIFF, windowmaker JPEG/TIFF/WebP, dillo WebP, FFmpeg WebP, the Thunar APR exif page | S (jpeg) / M each |
| **K. Kernel event primitives** | `futex`, `eventfd`, `epoll`, `timerfd` (emulated over `poll()` + socketpairs in `wlphx_epoll.c`: level-triggered only, 1024 fds, the poll set is rebuilt on every wait) | glib GMutex/GCond on futex; Mesa `simple_mtx`; libxshmfence (polled fence word today, 50–1000 µs sleeps); O(1) compositor waits | L |
| **L. Alignment** | Drop `-mstrict-align` per port (SCTLR.A = 0) | FFmpeg, zlib, xz, sqlite, pixman C paths | M: per-port, benched, never in kernel or driver code |

---

## 3. Proposed first batch

The batches are split by **how they must be built**. A ports-only change and a libphoenix change cannot share an `auto` rebuild (CLAUDE.md stale-core hazard).

### Batch 0: framework optimisation level (one change, alone)

**Files.** `phoenix-rtos-build/Makefile.common`: after line 180 add

```make
EXPORT_CFLAGS   += $(OLVL)
EXPORT_CXXFLAGS += $(OLVL)
```

In `phoenix-rtos-ports`, append `-O2 -mcpu=cortex-a72 -mtune=cortex-a72` to the literal `CFLAGS` strings:
- `xorg_fonts/port.def.sh:109,137,177,209,229`
- `xorg_libs/port.def.sh:132,190,220`
- `xterm:106,123`
- `xorg_apps:89`
- `windowmaker:115` (`cf=`)

**Build.** `rebuild-rpi4b-fast.sh --scope full-clean --with-ports` (every port must rebuild).

**Verify.**
1. Re-run the stack-slot metric (§0) on `lib/*.a`. Every library should fall below ~15%.
2. `objdump` `nfs_get_readmax` should be 2–3 instructions.
3. Run the showcase gate plus `test-libc-*`.
4. Before/after measurements: NFS read MB/s (29.9 MB/s baseline), `curl https://…` handshake time, GTK app start time.

**Risk.** `-O2` may expose UB in code that has never been optimised: libnfs (the root fs server!), bash, mbedtls. Keep the build5 manifest as the rollback point.

### Batch 1: ports-only option flips (S; build together, one image, one gate)

Rows 1–9 and 11 are in `sources/phoenix-rtos-ports`; row 10 is in `sources/phoenix-rtos-project`. No new dependencies, no libc change.

| # | File:line | Change | Build-side check |
|---|---|---|---|
| 1 | `libjpeg/port.def.sh:62` | `-DWITH_SIMD=0` → `-DWITH_SIMD=1`, and fix the comment at `:40-43` | `nm libjpeg.a` shows `jsimd_idct_islow_neon` etc. |
| 2 | `python/port.def.sh:125` | add `--with-computed-gotos` | `pyconfig.h`: `USE_COMPUTED_GOTOS 1` |
| 3 | `python/Setup.local` | add, copying the lines from `Modules/Setup.stdlib.in` of the port's 3.14.4 tarball: `_asyncio _asynciomodule.c`, `_lsprof _lsprof.c rotatingtree.c`, `termios termios.c`, `syslog syslogmodule.c`, `pyexpat pyexpat.c` and `_elementtree _elementtree.c`. ⚠ pyexpat/_elementtree need the **bundled** expat: add `$(LIBEXPAT_CFLAGS)` and `$(LIBEXPAT_A)` (configure points them at `Modules/expat`; check the generated Makefile) instead of hand-writing `-D` flags. CJK codecs (`_multibytecodec` + `_codecs_*`, lines 111-117) are optional, about 1 MB | the port's PyInit_* set check (update its expected list); on the Pi, `python3 -c "import xml.etree.ElementTree, asyncio, termios"` |
| 4 | `micropython/files/001_mpconfigport.mk:31` | `MICROPY_SSL_AXTLS = 1` → `0` | the build links `extmod/modtls_mbedtls.c`; `import tls` on the Pi |
| 5 | `openssl/30-phoenix.conf:57` (the aarch64a72 entry) | `-Os` → `-O2`; add `enable-ec_nistp_64_gcc_128` at `openssl/port.def.sh:49` | `openssl speed x25519` before/after on the Pi |
| 6 | `curl/port.def.sh:46` | add `--disable-dict --disable-gopher --disable-telnet --disable-tftp --disable-smb --disable-rtsp --disable-ldap --disable-imap --disable-pop3 --disable-smtp` | `curl -V` protocol list |
| 7 | `gtk3_wayland/port.def.sh:563-564` | `-Dgif=enabled -Dothers=enabled -Dbuiltin_loaders=png,jpeg,gif,bmp,ico,xpm,pnm,tga,xbm,ani` | the gdk-pixbuf loader symbols are in the xfce/atril binaries |
| 8 | `video_player/files/components.sh:12-23` | `FF_DECODERS += mpeg2video,mpeg1video,ac3,eac3,dca,alac`; `FF_DEMUXERS += mpegps,flv,ivf`; `FF_PARSERS += mpegvideo,ac3,dca`; `FF_FILTERS += bwdif,yadif`; `--enable-zlib` in `FF_COMMON` | `config.h` `CONFIG_ZLIB 1`; the GPL check at `:185` still passes |
| 9 | `sqlite3/port.def.sh:24-30` | add `-DHAVE_PREAD=1 -DHAVE_PWRITE=1 -DHAVE_LOCALTIME_R=1 -DSQLITE_ENABLE_MATH_FUNCTIONS -DSQLITE_DEFAULT_MEMSTATUS=0` | the port's `smoke.sql`; `select sqrt(2);` |
| 10 | `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/busybox_config` (project repo, not ports; `build.project:32` selects it) | turn off the applets the GNU ports ship (gunzip/zcat, bunzip2, unxz, tar, sed, grep) to drop the CVE-bearing code | the busybox applet list; the GNU tools are the ones on `$PATH` |
| 11 | *(owner's call)* `gtk3_wayland:376`, `labwc_desktop:359,390`, `wayland_phoenix:333` | `-Db_ndebug=if-release` / `debugoptimized` → add `-Db_ndebug=true` | – |

**Build.**
1. Validate each changed port **alone** first, e.g. `scripts/build-port.sh libjpeg`, then `python`, `micropython`, and so on. Building everything at once means one broken recipe (micropython's bundled mbedtls on Phoenix, or a Setup.local typo) stalls the whole image.
2. Then build the image with `./scripts/rebuild-rpi4b-fast.sh --with-ports --with-showcase`, which forces the ports stage. After a commit, `auto` alone ships nothing new.
3. Check the strings or symbols listed above in the shipped binaries, per CLAUDE.md.

**Runtime gate.** The showcase gate (7/7), plus one Python XML/asyncio smoke, one HTTPS `curl`, one ffplay of an MPEG-2 TS clip, and a GIF opened in Atril or Thunar.

### Batch 1b: small code, still ports-only (S)
- labnag: `labwc_desktop:579` (drop the flag, hand-link, stage `/bin/labnag`).
- Atril: `-Denable_dbus=true` (`atril_wayland:358`) plus stage atrild; poppler `-DENABLE_UTILS=ON` (`:350`) plus stage pdfinfo/pdftotext.
- The cleanup items from §1 (the stale comments, the unreachable peercred hunk in wayland patch 0001, `--disable-mitshm`).

### Batch 1c: one Pi-verified M item, after Batch 1 is green
- Mesa shader cache, Vulkan variant (row 2):
  - `mesa_drm/port.def.sh:137`: `-Dshader-cache=enabled` for the `vulkan` variant only.
  - The vkQuake launcher exports `MESA_DISK_CACHE_DATABASE=1` and `MESA_SHADER_CACHE_DIR=/var/cache/mesa` (persistent, writable on both NFS and SD).
  - Add a per-build salt to the cache key.
- **Pass criterion:** 2nd-start `first present` ≪ 75 s, and no rendering artefacts over 3 starts (the G3 check).
- **Tripwire:** on the old stack a cold Mesa shader cache raised the C1 fire rate 5–7×. C1 is closed and the D12 detectors still ship, so treat the first cached and uncached vkQuake runs as a D12 reading as well, and grade the UART for `malloc_c1*` hits.

### Later batches (M; each is its own step)
- **Batch 2 (libphoenix, `--scope core`, libc host tests first):**
  1. The stack-protector symbols, then `-fstack-protector-strong` in the exported flags and dropbear `--enable-harden`.
  2. `posix_spawn`.
  3. `pipe2`/`dup3`/`pthread_setname_np`/`posix_openpt`/`sem_*`/`threads.h`/`uchar.h`.
  4. `dl_iterate_phdr` (then the Mesa GL shader cache).
- **Batch 3 (ports, M):**
  - libutf8proc for foot and fcft
  - the glib P19 polling monitor
  - FFmpeg `-mno-strict-align` with a decode bench
  - xz 5.8.x plus threads
  - redis ≥ 7.2.11, jq 1.8.x
  - curl 8.x on OpenSSL
  - the dbus auth fix
  - the Xorg input thread
  - F5 (with the agent already on it)
  - Atril pixbuf

---

## Appendix: per-port inventory

This condenses the five sub-audits. Each item cites file:line, and I spot-checked the load-bearing ones.

Paths are relative to `sources/phoenix-rtos-ports/` unless noted. In the tables, "–" means nothing to gain on this board. **NA** = not applicable on Wayland/Phoenix. **K** = blocked by a libc/kernel gap. **Dm** = the library is cheap, but the daemon it talks to does not exist.

### A.1 wayland_phoenix

**Build type.** Meson builds are `debugoptimized` with asserts kept (`:333`).

| Item | Where | Status / why | Enabling would buy | Cost |
|---|---|---|---|---|
| wayland `-Dscanner=false -Dtests=false -Ddocumentation=false -Ddtd_validation=false` | `:359-360` | uses the host scanner; build-time options only | – | – |
| wayland auto-probes | gfx-B.log | `posix_fallocate`, `mremap`, `prctl`: NO | `mremap` would avoid mmap+memcpy when a wl_shm pool grows | M (libc) |
| xkbcommon `-Denable-x11/wayland/docs/tools/bash-completion=false` | `:371-372` | – | xkbcli tools only | S |
| xkbcommon `-Denable-xkbregistry=false` | `:372` | needs libxml2; no consumer in the image | layout lists in settings dialogs | M (libxml2 here plus staging `rules/*.xml`) |
| xkbcommon auto-probes | gfx-B.log | icu NO; `secure_getenv` NO, so it uses `getenv` | – | – |
| seatd patches 0001-0004 | `patches/seatd/` | librt optional; no `MSG_CMSG_CLOEXEC`; only the noop backend; tests off | – | – |
| wayland patch 0001 | `patches/wayland/0001` | its `__phoenix__` peercred branch is now **unreachable** (the kernel has `SO_PEERCRED`); only the `MSG_CMSG_CLOEXEC` hunk is live | – | S cleanup |
| memfd | `files/compat/src/wlphx_memfd.c` | real shared memory over shmsrv. Objects are contiguous, power-of-two ≥ 1 MiB, ≤ 256 MiB and cannot grow (EFBIG). No seals (`F_ADD_SEALS` → EINVAL). ENOSYS without shmsrv | – | L |
| epoll / timerfd / signalfd / eventfd | `files/compat/src/wlphx_epoll.c` | emulated over `poll()` + socketpairs. Level-triggered only, 1024 fds, the set is rebuilt per wait. Timers are only visible to `epoll_wait` in the same process. Signals are delivered only inside `epoll_wait`. eventfd does not sum counters. The `ppoll`/`epoll_pwait` mask swap is not atomic | – | L (kernel) |
| other compat | `files/compat/src/wlphx_misc.c`, `files/compat/include` | `pipe2` (pipe+fcntl, not atomic), `msync` no-op, `O_NOFOLLOW=0`, `RTLD_NOLOAD`, `AF_LOCAL` | – | S–M (libc) |
| libinput shim | `files/shims/src/libinput_phoenix.c` | boot-protocol kbd/mouse only, max 4 devices, 8 ms polling thread. No accel, tap, touch, tablet or gestures; every config setter returns UNSUPPORTED. The udev shim is a fixed table with only `card0`, and hotplug never fires | – | accel S–M; hotplug L |

### A.2 labwc_desktop

**Build type.** Meson `debugoptimized` (`:359,390`). foot and fuzzel use `plain` + `-O2 -g` (`:585-588`).

**pixman.** 0.46 has NEON on (only tests, demos, gtk, libpng, openmp and timers are off, `:504-505`).

| Item | Where | Why | Enabling would buy | Cost |
|---|---|---|---|---|
| seatd `-Dlibseat-logind/seatd/builtin=disabled -Dserver=disabled` | `:508-509` | noop backend only | – | NA |
| wlroots `-Dbackends=drm,libinput` | `:559` | headless is always built | – | – |
| wlroots `-Drenderers=gles2` | `:559` | Vulkan renderer not wired | v3dv renderer | M–L, low value |
| wlroots `-Dallocators=gbm` | `:559` | – | – | – |
| wlroots `-Dxwayland=disabled` | `:560` | no Xwayland; `waitid` absent; xcb extras missing | X11 apps | L |
| wlroots `-Dlibliftoff=disabled` | `:560` | not ported | overlay planes | M |
| wlroots `-Dcolor-management=disabled` | `:561` | – | colour management; lcms2 is already built in atril_wayland | S, low value |
| wlroots `-Dxcb-errors=disabled`, examples off | `:560-561` | – | – | – |
| wlroots auto-probes | log | `linux/dma-buf.h`, `linux/sync_file.h`: NO | explicit sync | L |
| wlroots patch 0002 | `patches/wlroots/0002` | `fchmod` failure ignored | – | security note: the keymap fd is not read-only |
| wlroots patches 0003, 0004 | `patches/wlroots/0003`, `0004` | `F_DUPFD_CLOEXEC` DRM share; `fstat` ENOSYS tolerated | – | – |
| libxml2 `-Dzlib/icu/iconv/http/modules/readline/history/catalog/debugging/python/docs=disabled` | `:567-569` | – | – | – |
| fribidi docs/bin/tests off; patch 0001 drops `-ansi` | `:570` | – | – | – |
| pango `-Dintrospection=false`; patch 0002 | `:571` | – | – | – |
| pango auto-probes | log | libthai NO, xft NO | Thai line-breaking | M |
| fcft `-Drun-shaping=disabled` | `:574` | no utf8proc | whole-run shaping | S–M (row 9) |
| fcft `-Dsvg-backend=none` | `:574` | – | OT-SVG glyphs; nanosvg is bundled | S, low value |
| freetype without png | via xorg_fonts | – | colour emoji | F5 (row 22) |
| labwc `-Dxwayland=disabled` | `:578` | as wlroots | – | L |
| labwc `-Dsvg=disabled` | `:578` | librsvg needs Rust | – | L |
| labwc `-Dicon=disabled` | `:578` | libsfdo not ported; pure C, meson | window icons | S–M |
| labwc `-Dlabnag=disabled` | `:579` | all deps are present | labwc prompts | S (row 19) |
| labwc `-Dnls=disabled` | `:578` | no gettext | – | L |
| labwc man/test/systemd-session off | `:579` | – | – | – |
| labwc patch 0001 | `patches/labwc/0001` | `waitpid` instead of `waitid` | – | – |
| labwc patch 0002 | `patches/labwc/0002` | built-in keymap fallback | – | – |
| labwc: protocols compiled in | labwc `src/server.c` | text-input-v3, input-method-v2, fractional-scale, session-lock, security-context are all built, but no IME or locker client is ported | locker (swaylock) | M |
| foot `-Dgrapheme-clustering=disabled` | `:588` | no utf8proc; widths come from libphoenix `wcwidth`, which is 1 for everything | – | S–M (row 9) |
| foot `-Dutmp-backend=none` | `:589` | – | – | – |
| foot `-Dterminfo=disabled`, default `xterm-256color` | `:588-589` | – | – | S |
| foot `-Dime=true` | `:588` | compiled in, but no IME daemon exists | – | – |
| foot patch 0001 | `patches/foot/0001` | no scrollable shm pool (`max-shm-pool-size-mb=0`) | – | L |
| foot patch 0002 | `patches/foot/0002` | no memfd sealing | – | L, low value |
| fuzzel `-Denable-cairo=disabled`, png + nanosvg on | `:592` | `fuzzel.ini` has `icons-enabled=no`, and no theme is staged | icons | S–M |
| swaybg `-Dgdk-pixbuf=disabled` | `:594` | PNG only; gdk-pixbuf needs GIO/glib 2.88, but labwc_desktop `conflicts=gtk3_wayland` | – | L |

Compat stand-ins (`files/compat/src`):
- `pthread_setname_np` is a no-op.
- `posix_openpt` opens `/dev/ptmx`.
- Locale: `newlocale`/`uselocale` give the C locale only.
- `mbrtoc32`/`c32rtomb` are forced to UTF-8.
- `sem_*` are process-local.
- C11 `threads.h` has no `tss_*` and no timed waits.
- `shm_open` names are per-process and the mode is ignored.
- `timerfd` read goes through `--wrap=read`.
- `O_DIRECTORY` is 0.

### A.3 libepoxy / libxshmfence_phoenix / xkeyboard_config

**libepoxy** (`:63-64`)
- Options: `-Dglx=no -Dx11=false -Degl=yes`, `-Db_ndebug=true`.
- Patch 0001 gives static EGL dispatch.
- GL and GLES1 are not found, because mesa_drm is built GLES-only.

**libxshmfence_phoenix** (patch 0001)
- A polled 32-bit word in shmsrv memory: spin, then yield, then sleep 50–1000 µs.
- There is no futex, and pshared mutexes return EINVAL.
- The `/tmp` fallback can alias fences.
- Real fences need group K (L).

**xkeyboard_config** (`:227,:237-241`)
- `-Dnls=false`.
- Stages only rules evdev/base, keycodes, types, compat and symbols. No geometry, `*.xml` or `*.lst`, which only xkbregistry would need.

### A.4 gtk3_wayland

This port builds pcre2 10.47, GLib 2.88.3, fribidi, atk, gdk-pixbuf 2.42.12, harfbuzz 14, pango 1.54, cairo 1.18.4, GTK 3.24.52, gtk-layer-shell and libepoxy 1.5.10.

**GLib meson probes answering NO** (`tools/gpu-lane/gtk3-wayland/build-out-usr/glib-build/meson-logs/meson-log.txt`) (✔):

| Category | Probes |
|---|---|
| Process and fds | `posix_spawn`, `spawn.h`, `pipe2`, `close_range`, `fdwalk`, `O_DIRECTORY`, `pidfd_open`, `unshare`, `prlimit` |
| Event and wait primitives | `eventfd`, `futex`, `epoll_create1`, `inotify_init1`, `kqueue`, `ppoll` |
| Files | `splice`, `copy_file_range`, `utimensat`, `fallocate`, `lchmod`, `ftw.h`, `sys/xattr.h`, `statx` |
| Locale | `newlocale`, `uselocale`, `strtod_l`, `nl_langinfo` ERA/ALTMON/`_NL_*` |
| Networking | `res_query`/`res_n*`, `dn_comp`, `getservbyname_r`, `SIOCGIFADDR`, `recvmmsg`, `sendmmsg` |
| Misc | `pthread_setname_np` (all 4 forms), `getauxval`, `getresuid`, `sysinfo`, `tm_gmtoff`, `strerror_r` returning `char *`, `wcsnlen`, `valloc`, `free_sized` |

What GLib does at run time as a result:
- `gspawn` uses `fork()` (`gspawn-posix.c:1480`), plus a grandchild fork, and closes fds in a loop.
- GWakeup runs on a pipe.
- GMutex uses pthreads.
- The main loop is `poll()`.

| Sub-project | Cut (file:line) | Why | What enabling buys | Cost |
|---|---|---|---|---|
| glib | `file_monitor_backend=auto` → none (`:557`) | P19 | directory monitors | M (row 14) |
| glib | `-Dnls=disabled` + identity `libintl` stub (`files/src/intl/intl_stub.c`) | no gettext | translations | L (a gettext port, and P7) |
| glib | xattr, libmount, selinux, libelf, sysprof, dtrace, systemtap off (`:554-557`) | – | – | NA/L |
| glib | introspection off | – | – | L |
| glib | resolv stub (`files/src/resolv/resolv_stub.c:15`) | – | SRV/MX lookups | M |
| glib | patch 0002: trash without `O_NOFOLLOW` | – | – | D (security) |
| glib | patch 0001: `g_unix_fd_query_path` → ENOSYS | – | – | M |
| GIO TLS | none (glib-networking not built; static GIO modules cannot load) | – | – | L |
| pcre2 | `-DPCRE2_SUPPORT_JIT=OFF` (`:544`) | needs anonymous `PROT_EXEC` mmap | faster regex | M |
| gdk-pixbuf | `-Dgif=disabled -Dothers=disabled`, `builtin_loaders=png,jpeg` (`:563-564`) (✔ `meson_options.txt`) | – | more image formats | S (row 10) |
| gdk-pixbuf | tiff off | no libtiff port | TIFF | M |
| gdk-pixbuf | webp, svg not built | librsvg needs Rust | WebP; SVG icons | M; L |
| gdk-pixbuf | `-Dgio_sniffing=false` | – | – | S, low value |
| harfbuzz 14 (meson) | icu, graphite, cairo, gobject, png, zlib, subset, raster/vector/gpu, utilities off (`:571-575`) | no C++ runtime | Graphite only | M, low value |
| cairo 1.18 | xcb, xlib, tee, spectre, lzo, symbol-lookup, gtk2-utils off (`:582-585`) | PDF/PS/SVG/Script are ON | – | – |
| pango | `-Dlibthai=disabled -Dxft=disabled -Dsysprof=disabled` (`:589`) | – | Thai line-breaking | M |
| GTK | x11, broadway, xinerama, cloudproviders, tracker3, colord, profiler, introspection off (`:591-594`) | Wayland only | – | NA/L |
| GTK | `-Dprint_backends=none` + patch 0002 | the "cairo lacks PDF/PS" reason is stale; backends are shared_modules | print to file | L |
| GTK | atk-bridge absent | only linked on X11 | accessibility | L |
| libepoxy (bundled) | `gtkphx_noegl.c`, `-Dglx=no -Dx11=false` (`:491`) | cairo/wl_shm only | GtkGLArea | L |
| build type | `debugoptimized`, `b_ndebug=if-release` (`:376`) | – | – | S (row 25) |

### A.5 glib2 (2.56, for mc), xorg_fonts, xorg_libs, harfbuzz, libpng, libjpeg, libffi, libiconv

**glib2**
- `--disable-nls/libmount/selinux/dtrace/systemtap` (`:112-113`). No GIO: only glib, gthread, gmodule and gobject are built.
- `glib2.cache` has stale answers: codeset=no, strlcpy=no, "stub -liconv".
- Uses the internal PCRE1. This is a 2018 glib, so security debt; moving mc to 2.88 is L because of `conflicts=`.

**xorg_fonts**
- libpng 1.6.40 (`:104-113`; a second producer of `libpng16.a` besides the `libpng` port).
- freetype 2.13.2 `--without-zlib --without-png --without-harfbuzz --without-bzip2 --without-brotli` (`:138`).
- expat (`:177`).
- fontconfig 2.14.2 (`:202-209`):
  - `ac_cv_func_random/initstate/setstate=no`
  - statfs/statvfs members `=no`
  - `--with-cache-dir=/var/cache/fontconfig`. The cache is generated by the host `fc-cache` (`scripts/stage-desktop-fonts.sh:79-80`); check that the cache versions match.
- libXft (`:229`).
- cairo 1.16 (the only `-O2`, `:243`).
- **All of these except cairo are `-O0` and have no `-mcpu`.**

**xorg_libs** (`:132,190,220`)
- `-O0` and no `-mcpu`.
- pixman 0.42.2 has its NEON assembly active.
- libxcb has every extension; `--disable-mitshm` (`:164`) is unrecognised.
- libX11 has XCMS, XKB, XF86BigFont and threads.
- Not built: libXi, libXcursor, libXfixes, libXinerama, libXtst, libXcomposite, libXdamage, libXxf86vm. Each is S if something ever needs it.

**harfbuzz** (CMake, for STK) (`:74-82`)
- glib, ICU and graphite2 are off; subset, utils and raster are off.
- `harfbuzz-2.6.7.tar.xz` in the port dir is dead weight.

**libpng**
- NEON on by default, but `-O0`.

**libjpeg**
- `WITH_SIMD=0` (row 3); `WITH_TURBOJPEG=0`.

**libffi / libiconv**
- `-O0`. libiconv also has `--disable-nls`.

### A.6 xfce_wayland

X = `xfce_wayland/port.def.sh`. Everything is static, so any plugin needs a "link it in" patch; each extra GTK program costs about 17 MB stripped.

| Component (line) | Cut | Bucket / why | What enabling buys | Cost |
|---|---|---|---|---|
| libxfce4util (X:334) | introspection, vala, gtk-doc | NA | – | – |
| xfconf (X:335-336) | `--disable-gsettings-backend` | a GIO module | GSettings apps store settings in xfconf | M |
| libxfce4ui (X:337-339) | x11, libsm, startup-notification | NA | – | – |
| | glibtop, epoxy, gudev | "About" system tab only | system info | L |
| | gladeui2, tests | NA | – | – |
| garcon, exo, appfinder (X:340,341,365) | introspection only | – | – | – |
| libxfce4windowing (X:342-343) | `-Dx11=disabled` | NA (Wayland foreign-toplevel + ext-workspace) | – | – |
| Thunar (X:346-348) | `--without-x` + patch 0001 | NA | – | – |
| | `--disable-gudev` | K | – | – |
| | `--disable-notifications` | Dm (no libnotify or notifyd) | – | M |
| | `--disable-exif` | APR plugin only | "Image" properties page | M (libexif + plugin patch) |
| | `--enable-pcre2` | no-op: its only user, SBR, is off | – | – |
| | `--disable-uca/sbr/apr/tpa-plugin` | thunarx plugins | UCA: "Open Terminal Here"; SBR: renamers | M (plugin-linking patch) |
| | `--disable-wallpaper-plugin` | NA | – | – |
| | TPA plugin, trash | Dm (gvfs) | – | L |
| | patch 0003 + `thunar.xml:9` `THUMBNAIL_MODE_NEVER` | no tumbler | – | – |
| xfce4-panel (X:351-353) | all 11 plugins linked in (patch 0001) | – | – | – |
| | `-Ddbusmenu=disabled` | no SNI apps | SNI right-click menus | M, low value |
| | patch 0002 (file-monitor failure logged at debug) | P19 | – | – |
| xfdesktop (X:356-359) | `-Dfile-icons=false` | needs libyaml (pure C, no port) | desktop file icons | S–M |
| | thunarx, notifications | inert without file icons | – | – |
| | patch 0002 | no AccountsService | – | – |
| xfce4-settings (X:362-364) | xrandr, xcursor, xorg-libinput, libxklavier, libnotify | NA | – | – |
| | upower, colord, sound-settings | NA/Dm | – | – |
| Not built | xfce4-session | the loginctl stub stands in | session management | M |
| | tumbler | – | thumbnails | M core; L with video |
| | xfce4-terminal | VTE; foot is used instead | – | L |
| | xfce4-notifyd | – | notifications | M |
| Shims / runtime | `files/compat` | `daemon()`, `NGROUPS_MAX=8` | – | – |
| | `files/bin/msgfmt` + `--disable-nls` | – | translations | – |
| | `xfce-desktop.sh:101` | `GSETTINGS_BACKEND=memory` | see §1 | – |

**Built but not staged** (S each): xfdesktop-settings, xfce4-mime-settings/helper, exo-open, xfce4-settings-editor, xfce4-display-settings, thunar-settings, xfrun4, xfce4-popup-*.

**Thunar trash without gvfs.** Thunar hides Move to Trash unless the VFS lists a "trash" scheme (`thunar-action-manager.c:1614`, `glocalvfs.c:113`). A small patch gives Move to Trash (S). Browsing, emptying and restoring trash needs gvfsd (L).

### A.7 atril_wayland

A = `atril_wayland/port.def.sh`, PO = `atril_wayland/files/poppler-options.sh`.

| Item (line) | State / why | What enabling buys | Cost |
|---|---|---|---|
| pdf | **on**, linked in by patch 0004 | – | – |
| x11, mate_desktop (A:356) | NA (patches 0001-0003) | – | – |
| `-Dpixbuf=disabled` (A:355) | patch 0004 lets only pdf through | open PNG/JPEG in Atril | S–M |
| `-Dtiff=disabled` | no libtiff port | multipage TIFF / fax | M |
| `-Dcomics=disabled` | needs libarchive (no port) | CBZ/CBR | M |
| `-Dxps=disabled` | needs libgxps + libarchive | XPS/OXPS | M |
| `-Dps=disabled` | needs libspectre + Ghostscript | PostScript/EPS | L |
| `-Ddjvu=disabled` | needs djvulibre (C++) | DjVu | M–L |
| `-Ddvi=disabled`, `-Dt1lib=disabled` | needs kpathsea | DVI | L |
| `-Depub=disabled` | needs webkit2gtk | EPUB | L |
| `-Dcaja=disabled` | NA | – | – |
| `-Dgtk_unix_print=false` (A:357) | GTK has no print backends | print / print to file | M |
| `-Dkeyring=false` | Dm (libsecret, no Secret Service) | remember PDF passwords | L |
| `-Dthumbnailer=false`, `-Dpreviewer=false` | – | – | S / – |
| `-Denable_dbus=false` (A:358) | – | single instance (atrild) | S (row 20) |
| introspection, docs, help | NA | – | – |
| synctex | NA: no build option in 1.28.7 | – | – |
| libxml2 (A:340-342) | zlib, iconv, ICU, HTTP off | – | – |
| lcms2 (A:344) | fastfloat and threaded plugins off (GPL-3) | faster colour transforms | S, licence decision |
| openjpeg (A:346) | tools off | – | – |
| Poppler (PO:10-16) | openjpeg, lcms2 and libjpeg ON | – | – |
| | `ENABLE_LIBTIFF`, `BOOST`, `LIBCURL` off | no effect for Atril | – | – |
| | `ENABLE_NSS3`, `ENABLE_GPGME` off | no signature UI in Atril | – | L |
| | `ENABLE_HARFBUZZ` off | needs harfbuzz-subset, which gtk3_wayland disables | correct fonts in saved forms | S–M |
| | `ENABLE_UTILS=OFF` (A:350) | – | `pdfinfo`/`pdftotext` | S (row 20) |
| | Qt, cpp, introspection | NA | – | – |

### A.8 dbus

D = `dbus/port.def.sh`.

- `-Depoll/kqueue/inotify=disabled` (D:149): K. The daemon runs a `poll()` loop; config reload happens on SIGHUP.
- Off because NA: systemd, launchd, x11_autolaunch, user_session, selinux, apparmor, libaudit.
- Transport: `unix:path` only, with traditional activation.
- Session config: **auth ANONYMOUS + `allow_anonymous`** (row 24).
- `checks`, `verbose_mode` and `stats` are on (D:153).
- Patch 0001: the `MSG_CTRUNC` stub means truncated fd-passing cannot be detected.
- No system bus.

### A.9 video_player (FFmpeg 6.1)

**Configure line** (`files/components.sh:12-23`, `port.def.sh:158-160`):
- `--disable-autodetect --disable-everything --enable-pthreads --enable-asm`
- `--disable-network --disable-avdevice --disable-postproc`
- no GPL/nonfree
- probes: `HAVE_NEON 1`, `HAVE_FAST_UNALIGNED 1`, `CONFIG_ZLIB 0`

| Cut | Enabling buys | Cost |
|---|---|---|
| zlib and decoder/demuxer/filter lists | – | S (row 12) |
| Network (http/https/hls/rtp) | network input | M. https via `--enable-openssl`, which is LGPL-OK in 6.1. mbedtls would need `--enable-version3` |
| libdav1d | AV1 decode | M |
| libass (+ freetype, harfbuzz, fribidi, which exist) | text subtitles | M |
| libxml2 (DASH), libwebp | – | M, low value |
| v4l2m2m | HW decode | NA: no V4L2. The rpivid HEVC block is used only by `hevc-play` (D11) |
| `-mstrict-align` | – | row 13 |
| gtk-video paints with cairo | GtkGLArea over EGL | M |

### A.10 mesa_drm / libdrm_phoenix / sdl2_kmsdrm / xorg_server_drm

**mesa_drm** (`:115-119,:133-139`)

| Option | State | Note |
|---|---|---|
| `gallium-drivers` | v3d, vc4 | – |
| `vulkan-drivers` | broadcom (vulkan variant only) | – |
| `glx`, `gles1` | disabled | GLX is L |
| `llvm`, `spirv-tools`, `valgrind`, `libunwind`, `lmsensors` | disabled | – |
| `zstd` | disabled | no zstd port; M, low value |
| `expat`, `xmlconfig` | disabled | drirc per-app workarounds only; `mesa_glthread` still works from env. S, low value |
| **`shader-cache`** | **disabled** | row 2 |
| `perfetto`, `tools`, `video-codecs`, `gallium-va` | off | – |
| vulkan-variant platforms | none | Wayland WSI would be M (BO_EXPORT exists, Pi test pending) |
| `dl_iterate_phdr` probe | NO | the GL cache needs it |
| `linux/futex.h` probe | NO | `simple_mtx` uses a pthread mutex |
| `sys/shm.h` probe | NO | – |

Mesa runtime-feature patches:
- Keep: 0004 (NPOT) and 0013 (swrast only).
- 0007 (EZ forced off): the new-lane E2b measured 0 wedges and no fps change. Ship it as an env knob; low priority.
- 0012: removable once the BO_EXPORT Pi test passes.

**libdrm_phoenix** (`:78-81`): only vc4 is enabled. **libepoxy**: see A.3.

**sdl2_kmsdrm** (`port.def.sh:107-151`):
- Off: HIDAPI, LIBUDEV, DBUS, IBUS, every audio backend except Phoenix's `/dev/audio0`, X11, DIRECTFB, RPI, VIVANTE, OFFSCREEN.
- Dummy: joystick, haptic, sensor, loadso, filesystem, misc, locale.

| Cut | Enabling buys | Cost |
|---|---|---|
| Gamepads (joystick dummy, HIDAPI off) | USB gamepads in every game | L: a USB HID gamepad driver in `phoenix-rtos-devices/tty` (usbkbd/usbmouse only today) plus an `SDL_JOYSTICK_PHOENIX` backend |
| `SDL_FILESYSTEM_DUMMY` | real `SDL_GetBasePath`/`GetPrefPath` | S |
| `SDL_LIBSAMPLERATE=OFF` | better resampling | S, low value (the port exists) |
| dynapi off (patch 0003) | – | its "no dlopen()/dlfcn.h" comment is stale |

**xorg_server_drm** (`:153-167`)
- Options: `glx=false`, `dri1=false`, dri2/dri3/glamor on, `udev/hal/logind/pciaccess=false`, int10/vgahw off, xdmcp/secure-rpc/xselinux/xcsecurity off, **`mitshm=false`** (no SysV shm), xv on, xinerama off, **`input_thread=false`** (row 23), listen_tcp off, libunwind off.
- Probes: dbus NO, xkbcomp NO (patch 0003 bakes in a US keymap; a new xkbcomp port is M), setitimer NO (the smart scheduler loses client time-slicing; libphoenix `setitimer` would be M), `memfd_create` NO, `epoll` NO.
- Patch 0004 enlarges `SO_RCVBUF`, because the AF_UNIX default is one page. A kernel default would help Wayland too.

### A.11 Games

| Port | Cut | Enabling buys | Cost |
|---|---|---|---|
| quakespasm_drm (`:86-94`) | no music codec TUs | OGG/WAV music | S (libogg/libvorbis ports exist; useful only with non-shareware data) |
| vkquake_drm (`:101-102`) | `USE_CODEC_WAVE` only | Vorbis music | S |
| vkquake_drm | `TASK_AFFINITY_NOT_AVAILABLE` | – | – |
| vkquake patches 0005-0010 | – | keep all: G5 `-ENOSYS` still stands (`v3da_main.c:465-471`); 0006-0009 are HW- or compiler-bound; 0010 is a win | – |
| yquake2 (`:86,107,110`) | `USE_CURL`/`USE_OPENAL` compile empty | downloads; OpenAL audio | M, low value |
| yquake2 patch 0001 | no GL3 mipmaps by default | mipmapped textures | re-measure `YQ2_GL3_MIPMAP=1` on the new stack (S) |
| quake3e (`:119`) | opengl1 only, no curl | Vulkan renderer via phxvk | M |
| supertuxkart (`:150-161`) | wiiuse and recorder off; mbedtls instead of openssl | essentially full-featured already | – |
| kmscube_drm | no libpng or gstreamer | – | – |

### A.12 Base, network and tool ports

**python 3.14.4** (`port.def.sh:121-129`)
- Configure: `--disable-ipv6` (lwip `LWIP_IPV6` = 0), `--without-mimalloc` (needs `madvise`), no computed gotos (row 4).
- Setup.local: `_asyncio`, `pyexpat`, `_elementtree`, `termios`, `_lsprof`, CJK codecs, `syslog` and `_interpreters` are missing (row 5).
- config.site:
  - `resource` forced off (`:220`): `getrlimit` is a stub, but `getrusage` exists (S).
  - `ac_cv_file__dev_ptmx=no` (`:1`): posixsrv has a pty; verify on the Pi (S).
- `_multiprocessing`/`_posixshmem` need `sem_open`/`shm_open` (L).
- readline, uuid, dbm, tkinter, zstd: no library ports (M each).
- LTO: M, and it risks the dlopen'd `_curses`.

**openssl 3.5.9** (`30-phoenix.conf:35-36,57`)
- Off: shared, dso, async (needs `getcontext`), afalg, devcrypto, tests.
- asm on, but `-Os` (row 11).
- There is no `getauxval`, so feature detection falls back to SIGILL probing. That works for NEON. `ARMV8_CPUID` (the A72 RSA path) stays off; needs `getauxval` + MIDR (M).

**curl 7.64.1** (`:46-50`)
- `--disable-pthreads --disable-threaded-resolver --disable-ipv6 --disable-ntlm-wb`, mbedtls 2.28.
- No nghttp2, brotli, zstd, libssh2 or psl.
- Legacy protocols are on; the version is from 2019 (row 7).

**mbedtls 2.28.10**
- Stock config, `/dev/random` entropy patch, `-O0`.
- EOL, no TLS 1.3. The README still says 2.28.0.

**dillo 3.2.0**: `--enable-tls --disable-openssl --disable-webp` (`:109`).

**fltk 1.3.10** (`:58-59`): `--disable-gl --disable-xft --disable-xinerama --disable-xcursor --disable-xfixes --disable-xdbe`. The Xft comment is stale.

**lighttpd 1.4.79** (`:65-79`)
- `--disable-ipv6 --disable-mmap --with-bzip2=no`.
- HAVE_MMAP, HAVE_GETRLIMIT, HAVE_SYS_POLL_H, HAVE_SIGACTION and HAVE_DLFCN_H are deleted from config.h.
- Links PCRE1. `-O0`.
- `lighttpd.conf` loads `mod_openssl` but no `mod_deflate`.

**dropbear 2026.94**
- `--disable-harden` (`:48`).
- `localoptions.h:11-37`: `O_NOFOLLOW`=0, `SVR_DROP_PRIVS` 0 and stream forwarding 0 (both need `setresuid`).
- No sftp-server, so OpenSSH ≥ 9 `scp` needs `-O`.
- The cipher defaults are good.

**busybox 1.27.2** (project `busybox_config`)
- Off: LOCALE/UNICODE, IPv6, SHADOW, passwd/su/mount/less/ps/top/httpd/wget/hush.
- About 194 applets are on, including AWK (CVEs).

**coreutils 9.5** (`:51`)
- nls, acl, xattr, selinux, libcap off; no `--with-openssl`, which would give faster `sha*sum` (S).
- stdbuf, who/users/pinky, chcon/runcon are inert.

**grep 3.11**: `--disable-perl-regexp`, because PCRE2 is not a shared port.

**sed, tar, gzip**: nls/acl/xattr off. **bzip2**: fine at `-O2`.

**xz 5.4.7**: `--disable-threads`, `-O0` (row 18).

**sqlite3 3.53.4**: row 17.

**bash 5.2.21**
- `--without-bash-malloc --enable-static-link --disable-nls`, `-O0`.
- `-Wl,--allow-multiple-definition` (`:49`) is risky.
- Job control and readline are on.

**libnfs 6.0.2**: krb5, multithreading, TLS, nlm/nsm off; `-O0`, and it is linked into the NFS-root server.

**zlib 1.3.1**: `-O3`, ARMv8 CRC32 active. zlib-ng would be M.

**pcre 8.42**: `--disable-cpp`, no JIT or UTF, `-O0`, EOL.

**jansson 2.12**: `-O0`, old. **lua 5.4.7**: fine. **jq 1.7.1**: CVEs.

**redis 7.2.4**
- `MALLOC=libc BUILD_TLS=no`, ae_select event loop.
- CVE-2025-49844 and others.

**micropython 1.26.0**
- axTLS (row 6); `THREAD=0` (`sem_*`); TERMIOS/FFI/BTREE off.
- 16 KiB default heap: raise `UPYTH_HEAPSZ` (S).

**libevent 2.1.12**
- `--disable-thread-support --disable-openssl --disable-clock-gettime`, `-O0`.
- No shipped consumer.

**wpa_supplicant 2.11**
- `CONFIG_DRIVER_WIRED` only; `CTRL_IFACE=udp`, which is weaker than a unix socket.
- LEAP and MD5 EAP on; `-O0`.
- WiFi itself uses the firmware supplicant.

**nano 9.2**: `--disable-nls --disable-utf8 --disable-libmagic` (P7).

**mc 4.8.31**: `--without-subshell`, which needs `posix_openpt` (S–M); sftp/ftp/undelfs VFS off; ASCII codeset.

**ncurses 6.4**: narrow, no widec (P7).

**xterm 396**: `--disable-freetype --disable-luit --without-utempter`; `-O0`, no `-mcpu`.

**windowmaker 0.95.9**: `--disable-jpeg/tiff/gif/webp/magick/shm/xinerama/nls/xlocale`; `-O0`. JPEG is S, because the libjpeg port exists.

**ca_certificates**: current.

**Hardening** (GCC 16.2, `--disable-libssp`)

| Flag | Status |
|---|---|
| `-fstack-protector-strong` | blocked on the libc symbols (row 8) |
| `_FORTIFY_SOURCE` | a silent no-op today (M) |
| `-ftrivial-auto-var-init=zero` | S |
| `-fstack-clash-protection` | S–M; needs `--param stack-clash-protection-guard-size=12`, because pthread guards are one page |
| PAC/BTI | NA |
| PIE/RELRO | NA (static, no ASLR) |
| UBSan | available for debug builds (`libubsan.a`) |
