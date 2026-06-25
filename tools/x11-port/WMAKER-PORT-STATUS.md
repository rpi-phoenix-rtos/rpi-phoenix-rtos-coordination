# Window Maker (wmaker) port — status

**Date:** 2026-06-26
**Outcome:** BUILT + STAGED (host-side). Not yet HW-validated (no Pi boot in this session).

The heaviest window manager ported to the Pi 4 X11 stack so far. Window Maker
0.95.9 — core WM + the WINGs widget toolkit + the wrlib raster lib + all 13
`util/` helper programs — cross-compiles to **static aarch64-phoenix ELFs with
0 undefined symbols**, on top of a newly built antialiased-font stack.

## What builds

| Component        | Result                                              |
|------------------|-----------------------------------------------------|
| expat 2.5.0      | static `.a` (fontconfig's XML parser)               |
| fontconfig 2.14.2| static `.a` (2 Phoenix source fixes, gperf host dep)|
| libXft 2.3.8     | static `.a`                                         |
| libftw (gap-fill)| static `.a` — nftw/ftw, scandir/alphasort, nice     |
| **wmaker**       | **5.1 MB static aarch64 ELF, nm -u = 0**            |
| util helpers (13)| all static, nm -u = 0 (wmsetbg, wdwrite, wmiv, …)   |

Build driver: `tools/x11-port/build-wmaker.sh` (idempotent). It snapshots the
X11 lib closure out of the shared, read-only `/tmp/x11-phoenix` into an isolated
`/tmp/wmaker-deps` and builds only against that, so it is insulated from the
concurrent rebuilds of the shared prefix.

## Pre-flight (passed, no Pi)

- `file` → ELF 64-bit, ARM aarch64, statically linked
- `nm -u` → 0 undefined symbols
- `strings` → `/nfstest/bin/sh` (shell), `/nfstest/share/WindowMaker` +
  `/nfstest/etc/WindowMaker` (data dirs) compiled in
- `strings` → `/nfstest/etc/fonts` is baked as fontconfig's `FONTCONFIG_PATH`
  (= `sysconfdir/fonts`), so the statically-linked libfontconfig loads
  `/nfstest/etc/fonts/fonts.conf` — exactly where the alias config is staged.
  This is the linchpin of the font deliverable: if it pointed at a stock
  `/etc/fonts`, no aliases would load and `XftFontOpenName("sans serif")` would
  return NULL (wmaker won't start). Confirmed correct.

## Staged to /srv/phoenix-rpi4-nfs

- `bin/wmaker` + util helpers
- `share/WindowMaker`, `share/WINGs`, `share/WPrefs`, `etc/WindowMaker`
- `etc/fonts/fonts.conf` (self-contained; generic family → DejaVu)
- `usr/share/fonts/truetype/dejavu/{DejaVuSans,DejaVuSans-Bold,DejaVuSerif,DejaVuSansMono}.ttf`
- `var/cache/fontconfig/` (empty cache dir)

## libphoenix gaps encountered (the gap list)

| Gap                          | Disposition                                                                 |
|------------------------------|-----------------------------------------------------------------------------|
| `timercmp()` non-standard    | libphoenix `<sys/time.h>` uses a value-based (and buggy for `==`) macro; glibc/BSD use pointers. **Local fontconfig source patch** redefines it. A proper libphoenix header fix is a legitimate upstream change (not done — would need a core rebuild + sysroot propagation mid-session). |
| `_SC_LINE_MAX`               | **Committed to libphoenix** (`include/unistd.h` + `unistd/conf.c`, commit on the libphoenix sibling): sysconf returns `_POSIX2_LINE_MAX`. Build also passes `-D_SC_LINE_MAX=5` so it compiles against the un-rebuilt sysroot. |
| `<ftw.h>` / `nftw()` / `ftw()`| **No libphoenix header/impl.** Minimal correct impl in `ftw-phoenix/ftw.c`. Upstream-worthy libphoenix addition (not committed — a new libc feature, untested mid-session). |
| `scandir()` / `alphasort()`  | Absent from libphoenix `<dirent.h>`. POSIX impl in `ftw-phoenix/ftw.c`. Upstream-worthy. |
| `nice()`                     | No process-priority API in libphoenix. No-op stub (returns 0) in `ftw-phoenix/ftw.c`; the one caller (wmsetbg) only warns on failure. |
| `rint()`                     | libphoenix libm has no `rint`. Build define `-Drint=round` (adequate for wmaker's UI coordinate/colour rounding; differs only on .5 ties). Upstream `rint` in libm would remove the define. |
| `random()`/`initstate()`/`setstate()` | Absent. Forced fontconfig's FcRandom onto its `rand_r()` path via configure cache vars. No libphoenix change needed. |
| `FcRandom` static init       | `static unsigned int seed = time(NULL)` is not a constant expression. **Local fontconfig patch** seeds lazily. (fontconfig source bug on the non-glibc path, not a libphoenix gap.) |

Host build dependency installed this session: **gperf** (`apt-get install gperf`).

## NOT done / next steps

1. **HW validation** — no Pi was booted (host-side-only task). The real test:
   ```
   boot netboot image
   ls /nfstest/bin                 # expect wmaker + wmsetbg
   HOME=/nfstest/root PATH=/nfstest/bin:$PATH /nfstest/bin/startx wmaker
   ```
   `startx` is the `pl_phoenix_xlaunch` ELF; `startx wmaker` brings up Xphoenix
   then forks `/nfstest/bin/wmaker` as the WM. **HOME and PATH must be set in the
   psh env** (the launcher passes `environ` through; it does not set them).
   Risks to watch on first boot: (a) Xft/fontconfig actually resolving "sans
   serif" to the DejaVu TTF (the `fonts.conf` aliases are designed for this but
   are untested on-target); (b) `~/GNUstep` writes on the (write-flaky) NFS root
   — if they fail, wmaker runs off the global defaults in
   `/nfstest/share/WindowMaker` with warnings, which is acceptable for a first
   boot; (c) the same input/`/dev/kbd0`-EBUSY caveats as the rest of the X stack.

2. **Upstream libphoenix** — `nftw`/`ftw`, `scandir`/`alphasort`, `nice`, and
   `rint` (libm) are the candidate libc additions that would let the gap-fill
   lib and the `-D` defines be dropped. Only `_SC_LINE_MAX` was committed (a
   safe 2-line change); the others are larger and were left as build-local
   shims to avoid an untested core change.

3. **Ports recipe** — `sources/phoenix-rtos-ports/windowmaker/{port.def.sh,README.md}`
   is a draft mirroring the working build. It is blocked on the X11 + font lib
   stacks not being phoenix-rtos ports yet (same blocker as `xterm`; feeds task
   #12).

## Residuals / known untested paths

- **The libphoenix `_SC_LINE_MAX` commit is UNBUILT.** The next `--scope core`
  is its first actual compile (per the project's build-validate norm for core
  changes). It is correct by inspection (`<limits.h>` is included in
  `unistd/conf.c`, `_POSIX2_LINE_MAX` is reachable), but has not been compiled
  this session. The wmaker build does not depend on it (it uses
  `-D_SC_LINE_MAX=5` against the un-rebuilt sysroot).
- **`build_wmaker()`'s fresh-build codepath was not exercised end-to-end this
  session** — the staged binary came from manual configure+make steps with the
  same flags; every script run hit the "already built" guard. The constituent
  commands were validated by hand, but the one-shot script path is the single
  untested codepath. (Re-running clean is intentionally avoided: it re-exposes
  the concurrent shared-prefix race and is expensive.)
- **libc `system()`/`popen()` use `/bin/sh`, not `WMAKER_SHELL`.** The
  `WMAKER_SHELL` patch only covers `ExecuteShellCommand()` (menu/`<exec>`).
  These call sites shell out via libc, which hardcodes `/bin/sh` (absent on the
  netboot RAM root, present at `/nfstest/bin/sh`):
    - `util/wmsetbg.c:1007` — `system(cmd_smooth)` for background image smoothing
    - `src/rootmenu.c:1186` — `popen()` for a menu pipe (`<OPEN PIPE>` menu entry)
    - `WINGs/proplist.c:1587` — `popen()` reading a proplist via a command
    - `src/main.c:468/524/542` — `system("wmaker.inst …")` / first-run setup
  None are on the path to "wmaker shows a managed desktop" (the WM core and
  menu `<exec>` go through the fixed `ExecuteShellCommand`). They would fail
  gracefully (with a werror) if hit. If a desktop boots and the background or a
  pipe-menu entry silently does nothing, this is why — not a mystery. A future
  fix would be a `/nfstest/bin/sh` -> `/bin/sh` symlink on the root, or a libc
  build configured with the alternate shell path.
