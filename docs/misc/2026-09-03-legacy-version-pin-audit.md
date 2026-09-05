# Legacy version-pin audit: where did we pin an old release because of a Phoenix limitation, and is that limitation still real?

**Date:** 2026-09-03 · **Status:** ANALYSIS ONLY. No `port.def.sh`, build script,
patch or `ports.yaml` was modified; nothing was built; no Pi cycle was run.

## Why this document exists

The `nano` port is pinned at **2.2.6 (2010)** for one written reason: nano 2.2.x
bundles no gnulib, and gnulib was hard to port to Phoenix
(`sources/phoenix-rtos-ports/nano/port.def.sh:35-39`). That reason is **obsolete** —
the later `coreutils` port ships gnulib-based software successfully by renaming the
clashing symbols in a patch. Nobody went back to revisit nano.

The owner asked for that pattern to be hunted systematically:

> *"review our ports and look for similar situations in which we based on a legacy,
> old version due to some porting difficulties. Maybe in other, later ports (or
> general system extensions) we actually solved or improved the situation to a point
> that a newer version can now be easily ported — just as in nano's case."*

This is the audit. Scope: every `sources/phoenix-rtos-ports/*/port.def.sh`, every
version pin hard-coded in `tools/**/build-*.sh`, the x.org sub-component pins, and
the `external/` reference clones.

**Two prior instances prove the pattern is real, not hypothetical:**

1. **nano** (open, already scheduled — `docs/inprogress/WEEK-2026-W36.md:47`).
2. **libiconv** — `docs/AI-DRIVEN-PORT-JOURNEY.md:245`: the port had carried a
   *documented ASCII-only stub* because an older libiconv had "refused to
   cross-compile"; when it was revisited, **"the modern gnulib that a two-years-older
   release had 'refused to cross-compile' simply worked"** and the port became real
   GNU libiconv 1.18. Same shape: an old blocker outlived by the system, discovered
   only when someone re-tried.

---

## Method and its limits

- Pins enumerated mechanically from `port.def.sh` (`version=` / `commit=` /
  `git_rev=` / `archive_filename=`), from `tools/*/build*.sh` (`NV=` / `VER=`
  variables), and from the x.org aggregate recipes' in-script tarball names.
- **Provenance** separates *our* choices from inherited ones. Every port whose
  `port.def.sh` was added on **2026-05-12 by Adam Debek** came in with the upstream
  `phoenix-rtos-ports` import (that is the squashed import commit); those pins were
  never a decision of this project. Ports added 2026-06…2026-09 by Witold Bołt are
  ours.
- **Current upstream** was verified live on 2026-09-03 by `git ls-remote --tags`
  against each project's canonical repo (no builds, no clones). Those version
  numbers are machine-checked.
- **Release dates** of the *pinned* versions are from upstream release history and
  are accurate to the month; treat exact days as approximate. Ages are relative to
  2026-09.
- Classification rubric, applied strictly:
  - `UNBLOCKED` — the stated blocker is gone; a named later fix/technique removes it.
  - `STILL BLOCKED` — the blocker holds; the doc says what would have to be built.
  - `POLICY` — old on purpose for a good non-blocker reason (licence,
    reproducibility, deliberate API-compat).
  - `CURRENT` — at or effectively at upstream; zero-action row, listed for
    completeness so the audit is honest about its denominator.
  - `UNKNOWN` — **no recorded reason.** Flagged, never guessed.
- **Discipline observed throughout:** "the blocker is gone" is an evidence claim
  (the API exists at this path / this technique is in use in that port). "Therefore
  the upgrade is easy" is **not** claimed anywhere — every UNBLOCKED entry carries
  an explicit residual-risk line.

---

## What the system gained (the toolkit an upgrade can now draw on)

This is the "later ports or general system extensions" half of the owner's question.
Anything below is available to a re-port *today*:

**Techniques already in production use in this repo**

| Technique | Where it is proven | Defeats |
|---|---|---|
| Port-local **rename of clashing gnulib symbols** | `coreutils/patches/0001-rename-gnulib-gettime-settime.patch` | gnulib ↔ libphoenix namespace collisions (`gettime`/`settime`) |
| **gnulib stdio-internals patch** teaching gnulib about Phoenix's `FILE` | `coreutils/patches/0002-port-gnulib-stdio-internals-phoenix.patch` (`freadahead`/`freading`/`freadptr`/`fpending`/`fseterr`/`freadseek`) | gnulib `#error "port to your platform"` walls |
| **`mini-gmp` Phoenix stdio recognition** | `coreutils/patches/0003-mini-gmp-recognize-phoenix-stdio.patch` | bundled-GMP builds without external GMP |
| **`CONFIG_SITE` / `--cache-file` cross answer files** | `coreutils/config.site`, `bash/config.cache`, `mc/mc.cache`, `glib2/glib2.cache`, `python/config.site` | configure mis-guessing present libc functions as missing when cross-compiling |
| **Force-included compat shim header** (`-include`) | `nano/nano-phoenix-shim.h`, `redis/phoenix-compat.h`, `python/phoenix-py-compat.h`, fltk/dillo shims | small libc gaps without patching upstream source |
| **Donor-copy of a phoenix-aware `config.sub`/`config.guess`** from an already-extracted dependency | `nano`, `glib2`, `mc` `p_prepare` | pre-`phoenix`-triplet autotools trees |
| **`autoreconf` once** (modern autoconf knows `aarch64-phoenix`) | `dillo`, `openvpn` | same, without a donor copy |
| **Stub archive + glibc-compatible header staged into the prefix** | `mc/mc-support` (`libmcsupport.a`), `glib2` (`libintl.h`, `resolv.h`+`libresolv.a`), `windowmaker` (`libftw.a`) | absent libc/OS APIs |
| **`fake-pkg-config.sh`** answering configure's pkg-config queries from env | `mc` | no pkg-config DB in the cross sysroot |
| **Host-configure → transcribe the TU list → cross-compile it ourselves** | Mesa (`docs/misc/2026-09-02-v3d-migration-plan.md:10-16`), vkQuake (`vkquake/port.def.sh:166`) | meson-only build systems — at high cost, see caveat below |
| **`-std=gnu17` pinned before the flag capture** | `phoenix-rtos-build` `8b0c29c` | gcc-16 C23 hard-error implicit-function-declaration in gnulib ports |

**libphoenix / kernel capabilities that landed *after* several of these pins**

| Capability | Where | Landed |
|---|---|---|
| POSIX `*at()` family (`openat`, `unlinkat`, `fstatat`, …) | libphoenix `unistd/at.c` | 2026-09-01 |
| Real `fchdir()` via kernel `sys_fdpath` fd→path record | libphoenix + kernel | 2026-09-01 |
| `getmntent`/`setmntent`/`endmntent`/`hasmntopt` | libphoenix `mntent/mntent.c` (`29f5373`) | **2026-08-13** |
| `nl_langinfo()` + `<langinfo.h>` | libphoenix `locale/langinfo.c` (`7bf090f`, codeset fix `491618c`) | **2026-07-12** |
| `getprogname()` | libphoenix `stdlib/progname.c` | 2026-08 (coreutils work) |
| `statfs()`/`fstatfs()`, `RLIMIT_*` ids | libphoenix `sys`, `resource` | 2026-08-17 |
| termios input-flag macros (`IUCLC`/`IXANY`/`IMAXBEL`/`XCASE`) | libphoenix termios | 2026-08-27 |
| libm `rint`/`rintf` (+ `nextafter`, `log1p`, `exp2`, `tgamma`, …) | libphoenix `libm/phoenix/exp.c:475,509` | 2026-08-14…17 |
| `strptime()`, full `strftime` (incl. `%V/%g/%G`), `scanf` `%m`, `memccpy`, `stpncpy`, `strtok_r`, `psignal`, `siginterrupt`, `wctomb`, `makedev` | libphoenix | 2026-08-21…2026-09-01 |
| `umask` actually applied on create; `unlink(dir)`→`EISDIR` | libphoenix | 2026-09-01 |
| POSIX **fcntl record locking** (`F_GETLK`/`SETLK`/`SETLKW`) | kernel `3844d204` + libphoenix `ac3baed` | 2026-09-01 |
| `dlopen`/`dlsym`/`dlclose` (Phase A) | libphoenix `dl` | 2026-08-20 |
| detached-pthread stack-race + semaphore lost-wakeup fixes | libphoenix `4c97a79`, `c8ee89e`, semaphore fix | 2026-08/09 |
| `malloc(0)` returns non-NULL | libphoenix `stdlib/malloc_dl.c` | 2026-08 |
| `regcomp`/`regexec`, `glob`, `wordexp`, `fnmatch`, `getopt_long`, `wcwidth`, `mkstemps`, `grantpt`/`ptsname`, `strverscmp`, `getrandom`, `lchown`, `pthread_sigmask` | libphoenix (verified present 2026-09-03) | various |
| `select(NULL)` blocks instead of returning 0 | libphoenix | 2026-08-25 |
| gcc **16.2.0** is the default toolchain (C23-era diagnostics survivable) | `.toolchain` | 2026-08-28 |

**Still absent** (verified 2026-09-03 by symbol search in `sources/libphoenix`):
`ftw`/`nftw`, `scandir`/`alphasort`, `iconv_*` in libc (supplied by the libiconv
port instead), `posix_spawn`. And there is **no proven meson cross file** for
`aarch64-phoenix` — the Mesa/vkQuake recipes drive **host** meson and transcribe the
translation-unit list, they are not a cross build. That distinction is load-bearing
for the glib/cairo/pango family below.

---

## The full pin table

Provenance: **ours** = added by this project; **import** = arrived with the
2026-05-12 upstream `phoenix-rtos-ports` import. "In image" = registered in
`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml`
(✔ = `if:true`/unconditional, ○ = registered but `if:false`, — = not registered).

### A. Framework ports (`sources/phoenix-rtos-ports/*/port.def.sh`)

| Port | Pinned | ~date | Current upstream (2026-09-03) | Behind | Prov. | In img | Stated reason | Class |
|---|---|---|---|---|---|---|---|---|
| nano | 2.2.6 | 2010-11 | **9.2** | ~16 y | ours | ○ | *"nano 2.2.x is used deliberately: it bundles NO gnulib, so it sidesteps the gnulib-vs-Phoenix namespace collisions"* (`port.def.sh:35`) | **UNBLOCKED** |
| busybox | 1.27.2 | 2017-08 | **1.38.0** | ~9 y | import | ✔ | none recorded | **UNKNOWN** |
| zlib | 1.2.11 | 2017-01 | **1.3.2** | ~9 y | import | ✔ | none recorded | **UNKNOWN** (security-stale) |
| lzo | 2.10 | 2017-03 | 2.10 | — | import | ✔ | n/a | CURRENT |
| pcre | 8.42 (PCRE**1**) | 2018-03 | PCRE1 EOL @8.45; **pcre2-10.48** | ~8 y (EOL branch) | import | ✔ | none recorded | **UNKNOWN** |
| openssl111 | 1.1.1**a** | 2018-11 | 1.1.1 series ended **1.1.1w** (EOL 2023-09); **3.6.4** | ~8 y, EOL | import | ✔ | none recorded (port *name* encodes the 1.1.1 pin) | **UNKNOWN** (security-stale) |
| jansson | 2.12 | 2018-11 | **2.15.1** | ~8 y | import | ✔ | none recorded | **UNKNOWN** |
| dropbear | 2018.76 | 2018-02 | **2026.94** | ~8 y | import | ✔ | none recorded | **UNKNOWN** |
| picocom | 3.1 | 2018-12 | 3.1 (latest tag) | — | import | ✔ | n/a | CURRENT |
| curl | 7.64.1 | 2019-03 | **8.22.0** | ~7 y | import | ✔ | none recorded | **UNKNOWN** (security-stale) |
| openvpn | 2.4.7 | 2019-02 | **2.7.6** | ~7 y | import | — | none recorded | **UNKNOWN** |
| bzip2 | 1.0.8 | 2019-07 | 1.0.8 | — | ours | (dep) | n/a | CURRENT |
| libevent | 2.1.12 | 2020-07 | 2.1.13 | ~1 rel | import | ✔ | none recorded | CURRENT-ish |
| glib2 | 2.56.4 | 2018-12 | 2.88.x stable (2.89.x devel) | ~8 y | ours | (dep) | *"2.56 is the LAST autotools (./configure) glib series — glib went meson-only at 2.60. 2.56 also still bundles PCRE1"* (`port.def.sh:12`) | **STILL BLOCKED** |
| harfbuzz | 2.6.7 | 2020-07 | **14.4.0** | ~6 y | ours | (dep) | *"the same 2.6.7 release that tools/x11-port cross-builds via autotools; here we drive its CMake build instead"* (`port.def.sh:33`) | **UNBLOCKED (candidate)** |
| openiked | 6.9.0 | 2021-05 | **7.4** | ~5 y | import | — | none recorded (42 patches) | **UNKNOWN** |
| mbedtls | 2.28.0 | 2021-12 | 2.28 LTS ended **2.28.10**; **3.6.7** LTS; **4.2.0** | ~5 y, EOL branch | import | ✔ | none recorded; `curl` pins `mbedtls>=2.28.0` | **UNKNOWN** (security-stale, coupled) |
| xorg_server | 1.20.14 | 2021-12 | **21.1.24** | ~5 y | ours | ✔ | *"the CURRENT fbdev-DDX server (the working interim); modernizing to a glamor/modesetting path is the future goal G-XORG-MODERN"* (`port.def.sh:15`) | **UNBLOCKED (candidate)** — see §3 |
| azure_sdk | lts_01_2022 | 2022-01 | later LTS tags exist (tag query inconclusive) | ~4 y | import | — | LTS branch chosen upstream | POLICY (upstream LTS) |
| ncurses | 6.4 | 2022-12 | 6.5 / 6.6-snapshots | ~1 rel | ours | (dep) | none recorded (narrow, `--with-fallbacks`) | **UNKNOWN**, low value |
| libpng | 1.6.40 | 2023-06 | **1.6.58** | ~18 rel | ours | (dep) | none recorded | **UNKNOWN**, easy |
| ffmpeg | 6.1 | 2023-11 | **9.0.1** | ~3 y | ours | ○ | none recorded (LGPL decode-only config *is* recorded) | **UNKNOWN** |
| xorg_libs (aggregate: xorgproto 2023.2, libX11 1.8.7, libxcb 1.16, libXt 1.3.1, …) | 2023 snapshot | 2023 | xorgproto **2025.1**, libX11 **1.8.13**, libxcb **1.17.0** | ~2–3 y | ours | ✔ | none recorded (coherent snapshot) | **UNKNOWN** |
| xorg_fonts (freetype 2.13.2, fontconfig 2.14.2, expat 2.5.0, libXft 2.3.8) | 2023 snapshot | 2023 | freetype **2.14.3**, fontconfig **2.18.3**, expat **2.8.4** | ~2–3 y | ours | ✔ | none recorded | **UNKNOWN** |
| bash | 5.2.21 | 2023-11 | 5.3 (rc/beta tags only in git) | ~1 series | ours | ✔ | none recorded | CURRENT-ish |
| jq | 1.7.1 | 2023-12 | **1.8.2** | ~1 series | ours | ✔ | none recorded | **UNKNOWN**, easy |
| oniguruma | 6.9.9 | 2024-01 | 6.9.10 | 1 rel | ours | (dep) | n/a | CURRENT-ish |
| redis | 7.2.4 | 2024-01 | 8.10.1 | ~2 y | ours | ✔ | *"Redis 7.2.4 is BSD-3-Clause (pre-SSPL)"* (`port.def.sh:20`) | **POLICY (licence)** |
| coreutils | 9.5 | 2024-03 | **9.11** | ~2 y | ours | ✔ | none recorded (patch set 0001–0004 is documented) | **UNKNOWN**, moderate |
| mc | 4.8.31 | 2024-04 | 4.8.33 | 2 rel | ours | ○ | version itself unexplained; the *accommodations* are documented (`port.def.sh:33-44`) | **UNKNOWN** + obsolete accommodations (§4) |
| xz | 5.4.7 | 2024-05 | **5.8.3** | ~2 y | ours | (dep) | none recorded | **UNKNOWN** (see note) |
| libjpeg (turbo) | 3.0.4 | 2024-08 | **3.2.0** | ~2 y | ours | (dep) | port exists *because* the framework version parser rejected the legacy IJG string (journey doc) | **UNKNOWN**, easy |
| libnfs | 6.0.2 | 2024 | **7.0.1** | 1 major | ours | ✔ | none recorded; we carry 3 patches incl. the readlink truncation fix | **UNKNOWN** |
| wpa_supplicant | 2.11 | 2024-07 | hostap 2.12 | 1 rel | import | ✔ | none recorded | CURRENT-ish |
| fltk | 1.3.10 | 2024-11 | 1.3.x is the legacy series; **1.4.5** current | 1 major series | ours | (dep) | *"No source patches: FLTK 1.3.10 ships a phoenix-aware config.sub and cross-compiles cleanly"* — a *reason it works*, not a reason to stay | **UNKNOWN** + obsolete accommodation (§4) |
| xterm | 396 | 2024-11 | later patch numbers exist upstream (no git tags) | ~2 y | ours | ✔ | none recorded | **UNKNOWN**, low value |
| lua | 5.4.7 | 2024-06 | 5.4.9 | 2 rel | import | ✔ | n/a | CURRENT-ish |
| sdl2 | 2.30.12 | 2025-01 | SDL2 branch **2.32.10**; SDL3 **3.4.16** | ~1 y within branch | ours | ○ | SDL2 chosen because every ported game targets SDL2; 6 Phoenix patches + a `src/video/phoenix` overlay | **POLICY** (SDL2 branch) + minor in-branch drift |
| dillo | 3.2.0 | 2025-01 | **v3.2.0** (latest tag) | — | ours | ✔ | n/a | CURRENT |
| libiconv | 1.18 | 2025-01 | 1.18 | — | ours | (dep) | *retired* an ASCII-only stub — the pattern's precedent | CURRENT |
| libffi | 3.4.6 | 2024 | **3.8.0** | ~2 y | ours | (dep) | none recorded | **UNKNOWN**, easy |
| micropython | 1.26.0 | 2025-08 | 1.29.0 | 3 rel | import | ✔ | none recorded | CURRENT-ish |
| lighttpd | 1.4.79 | 2025 | 1.4.85 | 6 rel | import | ✔ | none recorded | CURRENT-ish |
| wamr | 2.4.2 | 2025 | 2.4.5 | 3 rel | import | — | none recorded | CURRENT-ish |
| windowmaker | 0.95.9 | 2020-12 | **0.96.0** | 1 rel | ours | ✔ | none recorded; the *gap-fills* are documented (`port.def.sh:40-56`) | **UNKNOWN** + obsolete accommodation (§4) |
| xorg_apps (xcalc 1.1.2, xclock 1.1.1, xlogo 1.0.7, xedit 1.2.2) | 2023-ish | 2023 | minor bumps exist | ~2 y | ours | ✔ | none recorded | **UNKNOWN**, low value |
| xbill | 2.1 (git) | — | 2.1 | — | ours | ✔ | n/a | CURRENT |
| python | 3.14.4 | 2026 | 3.14.x | — | ours | ✔ | n/a | CURRENT |
| sqlite3 | 3.53.4 | 2026 | 3.53.x | — | ours | ✔ | n/a | CURRENT |
| libogg | 1.3.5 | 2021 | 1.3.6 | 1 rel | ours | (dep) | none recorded | CURRENT-ish |
| libvorbis / libsamplerate / heatshrink / enet / smolrtsp / micro-ecc / fs_mark | 1.3.7 / 0.2.2 / 0.4.1 / 1.3.18 / 0.1.3 / git / 3.3 | — | all at latest tag | — | mixed | mixed | n/a | CURRENT |
| coreMQTT | 2.3.0 | 2022 | **5.0.2** | ~2 major | import | — | none recorded | **UNKNOWN** |
| sscep | 0.9.0 | 2021 | **0.10.0** | 1 rel | import | — | none recorded | **UNKNOWN** |
| coremark / coremark_pro | 1.0 (pinned commits) | 2009 / 2019 | coremark `v1.01` | n/a | import | ✔ / — | a fixed **benchmark specification** — the version *is* the benchmark | **POLICY** |
| llama2 | `350e04f` (20240529) | 2024-05 | upstream `karpathy/llama2.c` is a frozen reference implementation | n/a | ours | — | pinned commit of an unmaintained upstream | **POLICY** |
| quakespasm / quake3 / yquake2 / vkquake | fork commits (`external/<game>`, branch `phoenix-rpi4-port`) | — | tracked by `docs/misc/2026-09-03-game-fork-upstream-sync-plan.md` | n/a | ours | ✔ | decision **D10**: the fork is the single source of truth, port patches are generated from it | **POLICY** |
| supertuxkart | 1.4 | 2022-01 | 1.5 is still **beta/rc** upstream | ~4 y | ours | ✔ | 1.4 = last stable release; 10 portability patches + a pinned matching asset archive | **POLICY** (last stable) |

### B. Version pins hard-coded in `tools/**/build-*.sh`

These are the pre-framework ad-hoc build paths. The **build-path duplication** itself
is a separate audit (`docs/misc/2026-09-03-port-build-deduplication-audit.md`); here
only the *version* dimension matters.

| Script | Pin | Framework-port pin | Note |
|---|---|---|---|
| `tools/ports/build-nano.sh:15` | `nano-2.2.6` | 2.2.6 | same pin, dual path — bump both in one step |
| `tools/ports/build-mc.sh:35` | `mc-4.8.31` | 4.8.31 | same |
| `tools/ports/build-ncurses.sh:19` | `ncurses-6.4` | 6.4 | same |
| `tools/ports/build-glib2.sh:30` | `glib-2.56.4` | 2.56.4 | same |
| `tools/ports/build-fltk.sh:30` | `fltk-1.3.10` | 1.3.10 | same |
| `tools/ports/build-dillo.sh:43` | `dillo-3.2.0` | 3.2.0 | same |
| **`tools/ports/build-libffi.sh:20`** | **`libffi-3.3`** (2019-11) | **3.4.6** | ⚠ **version DRIFT between the two build paths** — cross-ref the dedup audit |
| `tools/python-port/build.sh:80,195,220,60` | vendored `ZVER=1.2.11`, `BZVER=1.0.8`, `XZVER=5.4.7`, `SQLVER=3530400` | zlib 1.2.11 / bzip2 1.0.8 / xz 5.4.7 / sqlite 3.53.4 | the ad-hoc python path re-pins zlib **1.2.11** independently — a zlib bump must touch here too |
| `tools/x11-port/build-cairo.sh:29` | `cairo-1.16.0` (2018-10) | *no framework port* | **1.16.0 is the last autotools cairo** (1.18 is meson-only) — same blocker family as glib2 |
| `tools/x11-port/build-pango.sh:26` | `pango-1.42.4` (2018-08) | *no framework port* | **1.42 is the last autotools pango** (1.44+ meson-only) — same family |
| `tools/x11-port/build-harfbuzz.sh:12` | `harfbuzz-2.6.7` | 2.6.7 | the autotools path the framework port was pinned to *match* |
| `tools/x11-port/build-wmaker.sh:57-60` | `expat-2.5.0`, `fontconfig-2.14.2`, `libXft-2.3.8`, `WindowMaker-0.95.9` | mirrored in `xorg_fonts`/`windowmaker` | same pins |
| `tools/x11-port/build-xserver-core.sh:32` | `1.20.14` | 1.20.14 | same |
| `tools/x11-port/build-jwm.sh:22` | `jwm-2.4.6` | *no framework port* | none recorded |
| `tools/x11-port/build-{xterm,xcalc,xclock,xlogo,xedit,ico,oclock}.sh` | 396 / 1.1.2 / 1.1.1 / 1.0.7 / 1.2.2 / 1.0.6 / 1.0.5 | mirrored in `xterm`/`xorg_apps` | same pins |

### C. `external/` reference clones (not ports)

Read-mostly source references for offline queries, plus the two that are actually
*built from*: `external/mesa` (`mesa-26.2.0`, the V3D driver source) and
`external/ffmpeg` (`n6.1`, matching the ffmpeg port). `external/linux` is a recent
stable snapshot; `external/{quake3e,quakespasm,vkquake,yquake2,ioquake3}` are the game
forks that are the D10 source of truth; `barebox`/`u-boot`/`rpi-eeprom`/`bsd-genet`
are bring-up references. **No legacy-pin finding here** — the mesa pin is current and
the game pins are governed by D10.

**Totals: 67 framework ports + 28 tools-script pins + 13 external clones = 108 pins
audited.** Classification of the 67 framework ports:

| Class | Count | Which |
|---|---|---|
| **UNBLOCKED** | 1 | `nano` |
| **UNBLOCKED (candidate)** | 2 | `harfbuzz`, `xorg_server` |
| **STILL BLOCKED** | 1 | `glib2` (plus its cairo/pango siblings, which live only in the tools scripts) |
| **POLICY** | 11 | `redis` (licence), `sdl2` (SDL2 branch), the 4 game forks (`quakespasm`/`quake3`/`yquake2`/`vkquake`, D10), `supertuxkart` (last stable), `coremark`+`coremark_pro` (benchmark spec), `azure_sdk` (upstream LTS), `llama2` (frozen upstream) |
| **CURRENT / effectively current** | 24 | `bash` `bzip2` `dillo` `enet` `fs_mark` `heatshrink` `libevent` `libiconv` `libogg` `libsamplerate` `libvorbis` `lighttpd` `lua` `lzo` `micro-ecc` `micropython` `oniguruma` `picocom` `python` `smolrtsp` `sqlite3` `wamr` `wpa_supplicant` `xbill` |
| **UNKNOWN — no recorded reason** | 28 | `busybox` `coreMQTT` `coreutils` `curl` `dropbear` `ffmpeg` `fltk` `jansson` `jq` `libffi` `libjpeg` `libnfs` `libpng` `mbedtls` `mc` `ncurses` `openiked` `openssl111` `openvpn` `pcre` `sscep` `windowmaker` `xorg_apps` `xorg_fonts` `xorg_libs` `xterm` `xz` `zlib` |

Note the shape of that distribution: **only one pin in the whole tree is old for a
written Phoenix-limitation reason that still holds** (`glib2`). The large bucket is
not "blocked" — it is *unexamined*.

---

## 1. UNBLOCKED — nano 2.2.6 → modern (confirmed, already scheduled)

Included only to confirm the classification is consistent; **excluded from the ranked
list** because `docs/inprogress/WEEK-2026-W36.md:47` already schedules it as an owner
task.

**Stated blocker** (`nano/port.def.sh:35-39`): nano 2.2.x bundles no gnulib and
therefore sidesteps "the gnulib-vs-Phoenix namespace collisions
(gettime/getprogname/...) that block the modern (6.x) nano."

**Evidence the blocker is gone** — every collision named in that comment now has a
production answer:
- `gettime`/`settime` collision → renamed in
  `coreutils/patches/0001-rename-gnulib-gettime-settime.patch`.
- `getprogname` → **implemented in libphoenix** (`stdlib/progname.c`), so gnulib's
  `getprogname.c` `#error "not ported"` never fires.
- gnulib's `#error "port to your platform"` stdio internals → answered by
  `coreutils/patches/0002-port-gnulib-stdio-internals-phoenix.patch`.
- The whole configure-mis-guess class → `coreutils/config.site`.
- gnulib's `save_cwd`/`fchdir` emulation, historically the thing that broke gnulib
  consumers → real `fchdir()` landed 2026-09-01 (kernel `sys_fdpath` + libphoenix),
  and the recorded conclusion was *"every gnulib port heals"*.

**Residual risk (not a blocker, but do not skip it):** modern nano prefers **ncursesw**
for UTF-8, and our `ncurses` port is built **narrow** with `--with-fallbacks` and no
on-disk terminfo. Expect either a `--disable-utf8`-shaped configuration or an
ncursesw variant of the ncurses port. Also nano ≥5 wants `<sys/statvfs.h>` niceties
and a locale/`nl_langinfo` surface — both now present.

---

## 2. UNBLOCKED (candidate) — harfbuzz 2.6.7 → 14.x

**Stated reason** (`harfbuzz/port.def.sh:33`): *"No patches. This is the same 2.6.7
release that `tools/x11-port` cross-builds via autotools; here we drive its CMake
build instead (cleaner, no libtool)."*

So the pin is **parity with the older ad-hoc autotools script**, not a Phoenix
limitation. The framework port already uses **CMake**, which is the build system that
survived: harfbuzz dropped autotools at 3.0 but still ships `CMakeLists.txt` —
verified 2026-09-03: `https://raw.githubusercontent.com/harfbuzz/harfbuzz/14.4.0/CMakeLists.txt`
returns **HTTP 200**. The port's own configuration (freetype backend on via
`FindFreetype` cache-var pinning, glib/icu/cairo/subset off) maps onto the modern
option names essentially unchanged.

**Upgrade sketch:** bump `version=`+`sha256=`, keep the CMake invocation, keep the
`FREETYPE_*` cache-var pinning, re-emit the hand-written `harfbuzz.pc`. The only
consumer is `supertuxkart` (via `hb-ft`, `hb_ft_font_create*`), so the blast radius
is one game.

**Residual risk:** modern harfbuzz is C++17 (fine under gcc-16) but pulls in more of
`<atomic>`/`<thread>`; freetype 2.13.2 is old enough that the `hb-ft` bridge should
be re-checked; STK's own harfbuzz API usage may need a look. **Not claimed easy.**

---

## 3. UNBLOCKED (candidate) — xorg_server 1.20.14 → 21.1.x

This is the finding I expected to close as STILL BLOCKED and could not.

**Recorded reasons, and what each turns out to be worth:**

- `xorg_server/port.def.sh:15` — *"This is the CURRENT fbdev-DDX server (the working
  interim); modernizing to a glamor/modesetting path is the future goal
  G-XORG-MODERN."* That is a **roadmap statement about the DDX architecture**, not a
  claim that 21.1 cannot be built.
- `tools/x11-port/PROGRESS.md:341-343` — the 2026-06-18 scout finding: *"modern
  xorg-server (master) no longer ships the kdrive `fbdev` backend — `hw/kdrive/` has
  only `ephyr` + `src` + `meson.build`"*, listing *"An **old xorg-server (~1.13)**
  that still has `hw/kdrive/fbdev`"* as a fallback path.

**Evidence that neither reason blocks 21.1.x** (all verified live 2026-09-03 against
`gitlab.freedesktop.org/xorg/xserver`):

1. **`hw/kdrive/` is structurally identical between the two versions.** The
   GitLab tree API returns exactly `ephyr`, `src`, `Makefile.am`, `Xkdrive.man`,
   `meson.build` for **both** `xorg-server-1.20.14` **and** `xorg-server-21.1.24`.
   The 2026-06 scout note is correct that `hw/kdrive/fbdev` is gone — but it is gone
   from **1.20.14 too**. We never used it: our server is a **hand-written Phoenix
   fbdev DDX** (`xorg_server/files/`) hand-`ld`-linked against the kdrive **core**
   archives. That core is still there in 21.1.
2. **21.1.x still ships autotools.** `configure.ac` at tag `xorg-server-21.1.24` is
   **87,875 bytes** (1.20.14's is 94,506). So the meson-only concern — the real
   blocker for the glib/cairo/pango family — **does not apply here**. Our recipe's
   `./configure --disable-xephyr` + "make only builds the libs" + hand-link shape is
   preserved in principle.
3. The one former source patch is already gone for an unrelated system reason: the
   `record/record.c` `malloc(0)`→NULL assert-guard was retired when libphoenix's
   `malloc(0)` began returning a valid pointer (`stdlib/malloc_dl.c`) — noted in
   `port.def.sh:37-41`. The port carries **zero patches** today.

**Upgrade sketch:** bump `VER`/`version` + sha256 in both
`sources/phoenix-rtos-ports/xorg_server/port.def.sh` and
`tools/x11-port/build-xserver-core.sh`; rebuild the kdrive core archives with the
same `--disable-*` set; re-resolve the DDX against the 21.1 DDX/screen-privates ABI
(the part that will actually cost time: `ScreenRec` privates, `miPointer`, XKB
`ddxLoad`, `dixChangeWindowProperty`-era signature churn between 1.20 and 21.1);
keep the compiled-in XKB keymap and `libmd` SHA1 choices.

**Residual risk (substantial):** 21.1 raised its minimum X client-lib and
`xorgproto` versions, which couples this to the `xorg_libs`/`xorg_fonts` 2023
snapshot; the kdrive/DDX internal ABI did change across 1.20→21.1 and our DDX is our
own code, so it *will* need real work. **This is "the blocker we recorded is not the
blocker", not "this is easy."** The payoff is security: an X server exposed to
X11 clients that is 5 years and dozens of upstream CVE fixes behind.

---

## 4. Obsolete *accommodations* (not version pins, same pattern)

These are places where a port carries a workaround whose Phoenix gap has since been
filled. They do not need a version bump — they need **deleting**. They are the purest
form of the owner's pattern and the cheapest wins in this document.

### 4a. `mc`'s `mc-support` stub library — the API arrived and nobody looked back

`mc/port.def.sh:38-40` states: *"Phoenix lacks getmntent (empty `<mntent.h>`) and
`nl_langinfo` (`<langinfo.h>`): the stub reports no mounts + `CODESET="UTF-8"`"*, and
`p_prepare` stages glibc-compatible headers **ahead of the target sysroot** plus a
`libmcsupport.a`.

Both APIs now exist in libphoenix:
- `getmntent`/`setmntent`/`endmntent` — `mntent/mntent.c`, added **2026-08-13**
  (`29f5373`, *"mntent: implement getmntent family (fstab/mtab access)"*). It is a
  real implementation that degrades to an empty table when `/etc/mtab` is absent —
  exactly what mc's `mountlist.c` needs.
- `nl_langinfo` + `<langinfo.h>` — `locale/langinfo.c`, added **2026-07-12**
  (`7bf090f`), with a deliberate correction to report **ASCII**, not UTF-8
  (`491618c`, *"mb/wc are single-byte"*).

**The timeline is the finding.** The stub was written for the ad-hoc build on
**2026-06-29** (`tools/ports/build-mc.sh`, commit `f196a8dc4`), when the gap was
real. The framework port was created on **2026-08-22** — *nine days after* mntent
landed — and copied the stub and its comment verbatim. Note also that `mc`'s only
committed change since is `7614905` *"force `ac_cv_func_hasmntopt=no` (fixes mc port
build)"*: a cache override fighting the very API libphoenix had just gained.

Additionally, `mc/port.def.sh:34` records `--without-subshell` because it *"sidesteps
the grantpt/ptsname pty dependency"* — libphoenix now has **both** `grantpt`
(`unistd/file.c`) and `ptsname` (`stdlib/pty.c`), and `posixsrv` provides `/dev/pts`.
So mc's subshell is a *feature* that is now at least attemptable. Note the stub's
`CODESET="UTF-8"` also **contradicts** libphoenix's deliberate ASCII answer — a
latent correctness divergence, not just dead code.

**Residual risk:** removing the header shadowing changes which `<mntent.h>` and
`<langinfo.h>` mc compiles against, and `mc.cache` carries a pre-seeded
`getmntent`-method answer that must be re-derived. Enabling the subshell is a
separate, larger experiment. **Not claimed easy.**

### 4b. `rint`/`rintf` shims — the libm gap is filled

- `fltk/port.def.sh:43-44` carries a force-included shim aliasing `rint`/`rintf` onto
  `round`/`roundf`, with its own **`TODO: drop the shim once libphoenix libm's
  rint-family lands in the sysroot libm.a`**.
- `windowmaker/port.def.sh:50` compiles with **`-Drint=round`** for the same reason:
  *"libphoenix libm has no rint(); round() suffices"*.
- `dillo/port.def.sh:87-88` force-includes a shim whose contents include `rint/rintf`.

`rint()` and `rintf()` are now real in libphoenix: `libm/phoenix/exp.c:475` and
`:509`, dispatching to `__ieee754_rint`/`__ieee754_rintf`, added in the 2026-08-14…17
libm wave. The port's own TODO is therefore **satisfied**, and the aliases are now
subtly *wrong* — `rint` honours the current rounding mode, `round` does not (the
libphoenix source comment says so explicitly). This is a correctness cleanup, not
just tidiness.

`windowmaker`'s other gap-fills are **still needed**: `ftw`/`nftw` and
`scandir`/`alphasort` remain absent from libphoenix (verified 2026-09-03), so
`files/ftw-phoenix/` + `libftw.a` stay. `nice()` also remains a stub.

### 4c. `xorg_server`'s retired patch — already done right

`xorg_server/port.def.sh:37-41` documents the *correct* handling of this exact
pattern: the `record/record.c` patch was **deleted** when libphoenix's `malloc(0)`
started returning non-NULL, and the empty `b_port_apply_patches` hook was left in
place. Worth naming as the model for 4a/4b.

---

## 5. STILL BLOCKED — the glib2 / cairo / pango family (one blocker, three pins)

| Pin | Where | Recorded reason |
|---|---|---|
| glib **2.56.4** (2018-12) | `glib2/port.def.sh:12`, `tools/ports/build-glib2.sh:30`, `tools/ports/GLIB2-MC-PORT-NOTES.md:33` | *"2.56 is the LAST autotools (./configure) glib series — glib went meson-only at 2.60."* |
| cairo **1.16.0** (2018-10) | `tools/x11-port/build-cairo.sh:29` | no reason in-script; 1.16.0 **is** the last autotools cairo (1.18 is meson-only) |
| pango **1.42.4** (2018-08) | `tools/x11-port/build-pango.sh:26` | no reason in-script; 1.42 **is** the last autotools pango (1.44+ meson-only) |

**The blocker holds.** We have **no proven meson cross build for
`aarch64-phoenix`**. What we do have is deliberately *not* that:
`docs/misc/2026-09-02-v3d-migration-plan.md:10-16` describes the Mesa recipe as
"`meson setup` on `external/mesa`" run **on the host**, then *"takes every per-file
compile flag from the resulting host `compile_commands.json`, transforms those flags
for the cross toolchain, and runs `ninja` inside the host build tree"*. `vkquake`
does the cheaper cousin: *"TU lists, transcribed from `external/vkquake/meson.build`"*
(`vkquake/port.def.sh:166`). Both are one-off, high-maintenance recipes for
code we heavily patch anyway — not a reusable cross path.

**To unblock, one of these must be built:**
1. A **real meson cross file** for `aarch64-phoenix` (`[binaries]`/`[host_machine]`,
   a `system` name these projects' `meson.build` accept, and cross-run answers for
   every `cc.run()` probe), validated on something small before glib. This is the
   right long-term investment: it unblocks glib, cairo, pango, modern fontconfig,
   and modern xorg-server-master in one stroke.
2. Or the mesa-style host-configure-then-transcribe recipe applied to glib — viable
   but glib's generated-source surface (`glibconfig.h`, `gmarshal`, enum/glue
   codegen) makes it materially worse than mesa's.

**And a second, independent blocker for glib specifically:** our recipe relies on
glib 2.56's bundled PCRE1 (`--with-pcre=internal`, quoted in `port.def.sh:12`),
whereas modern glib requires **system PCRE2**. We ship **PCRE1 8.42** and have **no
pcre2 port**. A glib bump therefore needs a `pcre2` port first — which is also the
cleanest reason to build one, since PCRE1 is EOL.

**Important scoping note that keeps this off the critical path:** the only consumer
of glib2 is `mc`, and modern mc still accepts glib ≥ 2.32. So **mc can be
modernized without touching glib** (see §4a). Likewise cairo/pango have no framework
port and are consumed only by the ad-hoc X11 path.

---

## 6. UNKNOWN-reason pins that are security-stale and look bumpable

26 pins have **no recorded reason at all**. Most are harmless (`libpng`, `libffi`,
`jq`, `xterm` — nobody wrote down "we took whatever was current", because that is
what happened). But a subset is both old **and** on the network attack surface of an
image we boot. Listing them is the honest output of the rubric — I did **not**
invent blockers for them.

| Pin | Age | Why it matters | Coupling |
|---|---|---|---|
| **zlib 1.2.11** (2017-01) | ~9 y | Two well-known fixed defects post-date it: **CVE-2018-25032** (memory corruption in deflate with certain `memLevel`/window settings) and **CVE-2022-37434** (heap over-read in `inflateGetHeader`). We *decompress attacker-controlled data* — Dillo over HTTPS, PNG images, NFS-fetched paks. | Consumed by `dropbear`, `glib2`, `libpng`, `curl`, `python`, `xorg_fonts`, `xorg_server`, `supertuxkart` — **and re-pinned separately** at `tools/python-port/build.sh:80`. `1.3.x` is API/ABI compatible. |
| **openssl 1.1.1a** (2018-11) | ~8 y, EOL | 1.1.1**a** is the *first* release of the 1.1.1 line; the line ended at **1.1.1w** (2023-09) and is EOL. Backs Python's `_ssl`/`hashlib`, `wpa_supplicant`, `lighttpd`, `openvpn`, `openiked`, `sscep`. | **Two-tier path:** 1.1.1a→**1.1.1w** is a same-API drop-in that recovers ~5 years of fixes with (probably) no consumer changes; 3.x is effectively a new port (`openssl3`) with provider/API churn across all six consumers. |
| **mbedtls 2.28.0** (2021-12) | ~5 y, EOL branch | The 2.28 LTS line ended at **2.28.10**; current LTS is **3.6.7**. This is the TLS stack **Dillo's HTTPS actually uses** and a `supertuxkart`/`curl` dependency. | Same two-tier shape: 2.28.0→**2.28.10** is in-branch; 3.6 is an API break that would need `curl`'s mbedtls glue and Dillo re-checked. |
| **curl 7.64.1** (2019-03) | ~7 y | Seven years of accumulated security releases; the HTTP client used by STK and available on the image. | `depends="zlib mbedtls? (mbedtls>=2.28.0)"` — moving curl usefully means moving mbedtls too. |
| **busybox 1.27.2** (2017-08) | ~9 y | Provides `/bin` utilities on the image; upstream is at **1.38.0**. | 23 local patches would need rebasing, and its value has dropped now that `coreutils` (105 tools) + `bash` ship. Arguably the right move is to *shrink* busybox's role rather than bump it. |
| **pcre 8.42** (2018-03) | ~8 y, EOL branch | PCRE1 ended at 8.45 and is EOL; the world moved to **pcre2 (10.48)**. Only consumer is `lighttpd`. | A `pcre2` port is *also* the glib prerequisite from §5 — one build, two unblocks. |
| **expat 2.5.0 / freetype 2.13.2 / fontconfig 2.14.2 / libX11 1.8.7** (2023) | ~2–3 y | The font/X client stack parses untrusted font and X data; all four have had security releases since. | These move as the `xorg_libs`/`xorg_fonts` **snapshot**, and `xorg_server` 21.1 (§3) would raise their floor anyway — do them together. |
| **xz 5.4.7** (2024-05) | ~2 y | Current is **5.8.3**. Flagged explicitly because the choice was made on **2026-08-27**, when 5.8.x already existed, and **no reason is recorded**. It is *plausible* this was deliberate caution around the 5.6.x supply-chain incident — 5.4.x being the maintained pre-incident branch — but **that is not written down anywhere, so I am not asserting it.** Either write the reason down or bump it. |

**A note on the TLS cluster.** `curl`, `openssl111`, `mbedtls` and Python's `_ssl`
are a **coupled set**: `curl` declares `mbedtls>=2.28.0`, `python` declares
`openssl>=1.1.1a`, `dillo` declares `mbedtls`. Upgrade them as one change or not at
all; a partial bump risks two TLS stacks with mismatched expectations in one image.

---

## 7. Ranked list — value over effort (nano excluded, already scheduled)

Value = what a user or the project actually gains. Effort = size of the jump plus how
much of the port's patch set / accommodation set must be reworked. Effort is a
*prediction*; nothing here is validated by a build.

| # | Action | Value | Effort | The specific thing that unblocks it |
|---|---|---|---|---|
| 1 | ~~**Delete `mc`'s `mc-support` stub + the `hasmntopt=no` override**~~ — **ALREADY DONE 2026-09-03** (ports `319130f` staging removal, `a33541b` cache flip), i.e. this row was stale the day it was written. Residual = four orphaned git-tracked files under `mc/mc-support/`, referenced by nothing; delete them. Verified 2026-09-04: libphoenix is a strict *superset* of the stub (it also has `hasmntopt`/`getmntent_r`/`addmntent`, which the stub lacked — that gap **was** the shadowing bug), `mc.cache` needs no further change, and no port stages a `<mntent.h>`/`<langinfo.h>` any more | High — removes a live *correctness* divergence (stub says `CODESET="UTF-8"`, libphoenix deliberately says ASCII) and a whole staged-header shadowing hack; exactly the nano pattern at accommodation level | **Low** | libphoenix `getmntent` family — `mntent/mntent.c`, commit **`29f5373`** (2026-08-13); `nl_langinfo` — `locale/langinfo.c`, **`7bf090f`** (2026-07-12, codeset corrected in `491618c`) |
| 2 | **Drop the `rint`/`rintf` shims** — edit list verified 2026-09-04, see §7c below; NOT a uniform delete (dillo's shim also carries `AI_*` fallbacks that must stay) | High per unit of work — the aliases are semantically wrong (`round` ignores the rounding mode) and the ports' own comments schedule the removal | **Low** | libphoenix libm `rint`/`rintf` — `libm/phoenix/exp.c:475,509` (2026-08-14…17 libm wave) |
| 3 | **zlib 1.2.11 → 1.3.x** (framework port **and** the re-pin at `tools/python-port/build.sh:80`) | High — two known fixed defects (CVE-2018-25032, CVE-2022-37434) on paths that decompress untrusted data, in **every** image; 8 dependent ports benefit at once | **Low-medium** (API/ABI compatible; the risk is breadth, not depth: 8 consumers + a second build path) | Nothing had to be fixed — this is a pin with **no recorded reason** that simply never got revisited |
| 4 | **harfbuzz 2.6.7 → 14.x** | Medium — 6 years of shaping correctness/security for STK; retires a pin whose only stated reason was parity with a retired script | **Medium** | The stated reason is parity with `tools/x11-port/build-harfbuzz.sh` autotools, but the framework port already drives **CMake**, and harfbuzz **14.4.0 still ships `CMakeLists.txt`** (verified HTTP 200, 2026-09-03) |
| 5 | **openssl 1.1.1a → 1.1.1w** (in-branch), then decide on a separate `openssl3` port | High — recovers ~5 years of TLS fixes for Python `_ssl`, `wpa_supplicant`, `lighttpd`, `openiked`, `sscep`, `openvpn` at same-API cost | **Low-medium** in-branch; **High** for 3.x | No Phoenix blocker was ever recorded; the port *name* (`openssl111`) is the only trace of the decision |
| 6 | **mbedtls 2.28.0 → 2.28.10** (in-branch) | Medium-high — this is the stack Dillo's HTTPS actually uses | **Low-medium** | Same: no recorded blocker; in-branch API stability |
| 7 | **Build a `pcre2` port** (retire PCRE1 8.42 for `lighttpd`) | Medium standalone — **but it is also the hard prerequisite for any modern glib**, so it buys down §5 | **Medium** | PCRE1 is EOL; the techniques inventory (config.site/cache, autoreconf) covers a plain autotools/CMake library |
| 8 | **xorg_server 1.20.14 → 21.1.x** | High security value (an X server exposed to clients, 5 years and dozens of upstream CVE fixes behind) | **High** — the DDX/screen-privates ABI between 1.20 and 21.1 is real work, and it couples to the `xorg_libs`/`xorg_fonts` snapshot | Both recorded reasons fail on inspection: `hw/kdrive/` is **structurally identical** in 1.20.14 and 21.1.24 (tree API: `ephyr`, `src`, `Makefile.am`, `Xkdrive.man`, `meson.build`), and **21.1.24 still ships autotools** (`configure.ac`, 87,875 bytes). Our DDX is our own code, so kdrive-fbdev removal never applied to us |
| 9 | **`xorg_libs`/`xorg_fonts` 2023 snapshot → 2025** (xorgproto 2025.1, libX11 1.8.13, libxcb 1.17, freetype 2.14.x, fontconfig 2.18.x, expat 2.8.x) | Medium-high — untrusted font/X data parsers | **Medium-high** (a coherent ~30-tarball snapshot move; the pinned-tarball mirror plumbing must follow) | No recorded reason for 2023; and #8 raises the floor anyway, so pair them |
| 10 | **curl 7.64.1 → 8.x** | Medium | **Medium-high** — coupled to the mbedtls decision | No recorded reason |
| 11 | **Cheap no-reason bumps: `libpng` 1.6.40→1.6.58, `libffi` 3.4.6→3.8 (+ close the `libffi-3.3` tools drift), `jq` 1.7.1→1.8.2, `mc` 4.8.31→4.8.33, `ncurses` 6.4→6.5, `libjpeg-turbo` 3.0.4→3.2, `coreutils` 9.5→9.11, `libnfs` 6.0.2→7.0.1** | Low-medium each; `libnfs` 7.x may already carry upstream fixes for what our 3 local patches work around | **Low each** | Nothing needed unblocking — these are simply pins nobody revisited |
| — | **Not recommended now: glib2 / cairo / pango** | — | **Blocked** | Needs a proven `aarch64-phoenix` **meson cross file** (or a mesa-style transcription recipe) **plus** a `pcre2` port — see §5. Note `mc` does **not** need this |
| — | **Not recommended: busybox 1.27.2 → 1.38** | Low now | High (23 patches to rebase) | Its role should shrink in favour of `coreutils` + `bash` rather than be modernized |

---

## 7b. Prepared upgrade inputs (fetched + hashed 2026-09-04, edits pending)

Ranked items #5 and #6 are now a mechanical pin change — the artifacts were fetched and
hashed, so nothing has to be discovered at edit time:

| Port | From | To | size | sha256 |
|---|---|---|---|---|
| `openssl111` | 1.1.1a | **1.1.1w** | `9893384` | `cf3098950cb4d853ad95c0841f1f9c6d3dc102dccfcacd521d93925208b76ac8` |
| `mbedtls` | 2.28.0 | **2.28.10** | `4369619` | `0f2e0525903a89ae1d39ce439d858be66933bda54c5b6102b72a29ed8fe7c088` |

**Trap for the openssl bump:** the port's `source="https://www.openssl.org/source/"` only
serves the *current* release of a branch. 1.1.1w now lives under
`https://www.openssl.org/source/old/1.1.1/`, so `source=` must move with `version=` or the
fetch 404s. (1.1.1a resolves today only because the local tarball is already cached.)

Both are in-branch, API/ABI-stable moves; the risk is breadth (openssl has six dependent
ports: Python `_ssl`, `wpa_supplicant`, `lighttpd`, `openiked`, `sscep`, `openvpn`), so each
gets its own build + a dependent check, exactly as zlib 1.3.1 did.

## 7c. Verified edit list for the `rint`/`rintf` shims (2026-09-04)

`rint` is real in libphoenix — `libm/phoenix/exp.c:475` (generic body: `modf` + explicit
ties-to-even), `rintf` at `:509`; built under **either** `LIBM_USE_LIBMCS` setting
(`libm/Makefile:44-47` vs `:42`), present as `T rint`/`T rintf` in `libphoenix.a` and
`libm.a`, declared in the sysroot `math.h:125,210`. Nuance worth keeping honest: it
hard-codes ties-to-even and does **not** read `fegetround()`, so the correctness claim
holds for the default FE_TONEAREST tie behaviour, not for full fenv honouring.
`rintl` is **declared but never defined** — do not introduce a use of it.

**The shims are not uniform; only fltk's can go wholesale.**

| Port | Edit | Keep |
|---|---|---|
| `fltk` | delete `fltk-phoenix-shim.h` entirely, drop `-include ${shim}` + the `local shim=` line (`port.def.sh:52-53`) and the TODO at `:41-44`/`:35` | — (the shim holds nothing else) |
| `dillo` | delete **only** `dillo-phoenix-shim.h` lines 20-33 | the shim FILE and its `-include` (`port.def.sh:90`) — lines 43-58 carry the `AI_ADDRCONFIG`/`AI_NUMERICSERV`/`AI_V4MAPPED` fallbacks `dns.c` needs |
| `windowmaker` | drop **only** the `-Drint=round` token from `gapdefs` (`port.def.sh:99`) + comments `:50-51`, `:96-97`, `README.md:41` | `-include wmaker-phoenix-compat.h`, `libftw.a`, `files/ftw-phoenix/` — `ftw`/`nftw`/`scandir`/`alphasort`/`nice` are still absent from libphoenix |

Build **fltk before dillo** (dillo statically links `libfltk.a`). Per-port check:
`nm <artifact> | grep -w rint` must show the real symbol referenced/pulled in, where
before the edit there was no `rint` at all.

**Rebuild footgun that would fake a pass:** all three gate configure on
`[ ! -f config.status ]` and bake CFLAGS at configure time (wmaker also into
`make CFLAGS=`), so an *incremental* rebuild keeps the old flags and the shim. Wipe the
port workdir first — same class as the documented stale-core hazard.

**Behaviour this genuinely changes** (exact `.5` ties only; `round` goes away-from-zero,
real `rint` to-even — the new behaviour matches upstream Linux):
- **Reachable:** wmaker `WINGs/wbrowser.c:568` — one column of overflow with the scroller
  at mid-travel gives exactly `0.5`, so the browser scroll position shifts by one column.
- **Likely visible:** FLTK half-pixel geometry (`fl_vertex.cxx:107,247-250`, `Fl_Chart.cxx`)
  and value snapping (`Fl_Valuator.cxx:128,147` — a slider with step 0.5/0.25 now snaps to
  the even step).
- **Provably not:** dillo `dw/style.cc` (`borderWidth/3.0` can never land on a tie) and
  wmaker `wcolorpanel.c` (2.55 is not representable; wheel coords irrational).

**Adjacent find, separate cleanup:** every symbol in vkQuake's `__PHOENIX_VKQ_MATH_GAPS`
block (`vkquake/glue/vkq_phoenix_compat.h:45-46` and neighbours — copysign, remainder,
log2f, fmin/fmax…) is now in `libm.a`, so that whole block is droppable too.

## 8. Answers to the questions this audit was asked

**Is the nano case unique?** No, but it is the only *pure* instance — a version pinned
explicitly to dodge a Phoenix limitation that a later port then defeated. The pattern
recurs mostly one level down, as **obsolete accommodations** (§4): stub libraries,
`-D` aliases and cache overrides written when a gap was real and carried forward
after libphoenix filled it. `mc`'s mntent/langinfo stub (§4a) and the three
`rint` shims (§4b) are the clearest cases, and both are cheaper to fix than nano.

**Where the recorded reason turned out to be wrong rather than stale:**
`xorg_server` (§3). Both cited obstacles — kdrive-fbdev removal and the move to
meson — are verifiably not obstacles at 21.1.24. The real cost is DDX ABI work that
nobody had written down.

**Where a stated blocker genuinely holds:** the glib/cairo/pango family (§5). "Last
autotools release" is exactly true, we have no meson cross path, and modern glib adds
a PCRE2 requirement we cannot yet satisfy. This one should stay pinned until a meson
cross file exists — and it is worth noting that the same investment would unblock
four pins at once.

**Pins with no recorded reason:** 28 of 67 framework ports. Most are benign. Two
deserve a written decision either way: **`xz` 5.4.7**, chosen on 2026-08-27 when
5.8.x existed (possibly deliberate post-5.6 caution — unrecorded, so unasserted),
and **`openssl111`**, where the version is encoded in the port's *name* but the
reason for `a` rather than `w` exists nowhere.

## Cross-references

- Build-path duplication (a different question): `docs/misc/2026-09-03-port-build-deduplication-audit.md`.
  One version-dimension overlap worth carrying across: **`tools/ports/build-libffi.sh:20`
  pins `libffi-3.3` while the framework port pins `3.4.6`**, and
  `tools/python-port/build.sh` re-pins zlib/bzip2/xz/sqlite independently — so the
  §7 items 3 and 11 must touch both paths or they will re-drift.
- Game-engine fork pins: `docs/misc/2026-09-03-game-fork-upstream-sync-plan.md` (D10).
- nano's scheduled owner task: `docs/inprogress/WEEK-2026-W36.md:47`.

**Re-verify:** the "current upstream" column was captured 2026-09-03 via
`git ls-remote`. Re-run before acting on any row.

---

## ADDENDUM 2026-09-05 — §2 done; §3 re-measured against the real 21.1 source

**§2 harfbuzz: DONE.** 2.6.7 → **14.4.0** (ports `da41b10`). No source change, no
undefined `hb_*`; SuperTuxKart (the only consumer) HW-verified rendering correctly
*shaped* text. The only extra work was turning off `HB_BUILD_RASTER` /
`HB_BUILD_VECTOR` / `HB_BUILD_GPU`, three libraries 14.x adds with default ON.

**§4a and §4b: already done** — the `mc` stub library is retired (`port.def.sh`
documents it), and no `rint`/`rintf` shim survives in `fltk`, `windowmaker` or
`dillo`. Nothing to do.

**§3 xorg_server: the predicted cost is not there.** This section warned that "the
kdrive/DDX internal ABI did change across 1.20→21.1 and our DDX is our own code, so
it *will* need real work." Measured against the actual 21.1.24 tarball
(sha256 `1a4eb36ca65cc3b1b936566d677a9786e13c11cd5806e951ac55f3f5ce3984af`), that is
**not what the source says**:

| check | result |
|---|---|
| `hw/kdrive/src/kdrive.h` 1.20.14 vs 21.1.24 | **byte-identical** |
| the 21 `Kd*` APIs our DDX calls | **all 21 signatures identical** |
| `fb/fb.h`, `miext/shadow/shadow.h` | **byte-identical** |
| `include/windowstr.h`, `include/xkbsrv.h` | 2 diff lines each |
| `include/scrnintstr.h` | 32 lines — comment `master/slave`→`primary/secondary` renames plus `slave_list`/`slave_head`/`output_slaves` field renames |
| `include/inputstr.h` | 32 lines — **additive**: XI 2.4 gesture structs, `XI2LASTEVENT` bumped |
| does our DDX reference any renamed/removed field? | **no** (`slave_list`, `slave_head`, `output_slaves`, `XI2LASTEVENT`, `GestureInfo`: zero hits in `files/ddx/`) |
| new mandatory dep `libxcvt` | only inside the `XORG` branch of `configure.ac` (lines 1767-1774), which `--disable-xorg` excludes |
| build system | still autotools, as §3 already established |

So the DDX-facing surface did **not** churn. The remaining unknown is the *build
plumbing* — configure flag drift and the hand-`ld` link of the kdrive core archives —
which is a build experiment, not a porting effort. Re-classify §3 from "unblocked
candidate, substantial residual risk" to **"unblocked, cost concentrated in the build
recipe"**, and note that the payoff (five years of X server CVE fixes) is unchanged.

Not yet attempted: the actual 21.1.24 configure+build. That is the next step, and it
must be done in a scratch prefix — the shipping `Xphoenix` is part of a verified
image, so nothing about this may touch the live tree until it links and runs.
