# X11 sample apps for the Pi 4 demo — ico, oclock, xlogo, xclock, xcalc, xbill

**Date:** 2026-06-27
**Scope:** Host-side cross-build only. Six classic X11 client apps ported as
static `aarch64-phoenix` ELFs, staged to the NFS export, to run as X clients
alongside `xterm`/`twm` under the Xphoenix kdrive fbdev server. The orchestrator
boots the Pi and visually verifies rendering — this work does **not** boot the Pi.

## Result summary

All six apps **link as static `aarch64-phoenix` ELFs with 0 undefined symbols**
and are staged to `/srv/phoenix-rpi4-nfs/bin/` (and mirrored to
`artifacts/x11/`). Link-correctness is proven (`file` = aarch64 static ELF,
`nm -u` = empty); **runtime rendering is the orchestrator's to confirm** (see
"Runtime unknowns" below).

| App     | Built | Size (KiB) | Toolkit / deps          | Notes |
|---------|-------|-----------:|-------------------------|-------|
| ico     | yes   | 2273       | core Xlib only          | spinning wireframe polyhedron; lowest risk |
| oclock  | yes   | 2912       | Xmu/Xt/Xext over X11    | round shaped-window analog clock; `--without-xkb` |
| xlogo   | yes   | 3377       | Xaw/Xmu/Xt over X11     | the X logo; `--without-render` (no Xft → core draw) |
| xclock  | yes   | 3412       | Xaw/Xmu/Xt over X11     | analog/digital clock; `--without-xft` |
| xcalc   | yes   | 3433       | Xaw/Xmu/Xt over X11     | calculator; **needs app-defaults** (button layout) |
| xbill   | yes   | 3446       | Xaw/Xmu/Xt/Xpm over X11 | **headline game**; Athena backend; loads runtime sprites |

**No apps failed to build.** One source-selection trap was navigated (xbill, see
below).

## Build scripts (reproducible, idempotent)

One `build-<app>.sh` per app under `tools/x11-port/`, each mirroring the proven
`xeyes` autotools cross-recipe (toolchain, `--sysroot`, `-I/-L` into the
`/tmp/x11-phoenix` X11 prefix, static link, `pkg-config --static`, staging):

- `tools/x11-port/build-ico.sh`
- `tools/x11-port/build-oclock.sh`
- `tools/x11-port/build-xlogo.sh`
- `tools/x11-port/build-xclock.sh`
- `tools/x11-port/build-xcalc.sh`
- `tools/x11-port/build-xbill.sh`  (+ `tools/x11-port/xbill/xbill-phoenix-shim.h`)

Each fetches its release tarball (the five xorg apps from
`x.org/archive/individual/app/`), configures core-font-only (no Xft/fontconfig),
overrides the program's `*_LDADD` with an ordered static closure wrapped in
`-Wl,--start-group … --end-group` (the Xaw↔Xmu↔Xt↔X11 libs are circular), then
stages + runs pre-flight checks (aarch64 static ELF, 0 undefined symbols).

## Isolation — no shared code touched

**No libphoenix changes and no X11-prefix (`/tmp/x11-phoenix`) changes were
needed.** The work is fully isolated to the six new build scripts plus two small
xbill-only port headers. The flagship launcher (`pl_phoenix_xlaunch.c`) and the
parked WIP (sdcard / wifi) were not touched.

Two things looked like libc gaps at first but were **not**:

- **`strncasecmp` / `strcasecmp` (xbill).** Both are in libc and prototyped in
  the system `<strings.h>`. xbill ships its *own* `strings.h` (a table of game
  UI strings) and the build adds its dir with `-I.`, shadowing the system
  header → implicit-declaration error. Fixed by force-including a 2-line shim
  (`tools/x11-port/xbill/xbill-phoenix-shim.h`) that re-declares the two libc
  functions. No new symbol; pure header-shadowing workaround.
- **`XkbStdBell` (xlogo / xclock).** It is the alarm-bell helper these apps call
  when configure detects `xkbfile.pc`. It lives in `libxkbfile.a` (already in the
  prefix), not in libX11 — it was simply missing from the hand-rolled link
  closure. Fixed by adding `-lxkbfile` to those two closures. (oclock took the
  alternative `--without-xkb` route → plain `XBell()`.)

## xbill source trap (recorded so it isn't re-hit)

The SourceForge release **xbill-2.1.4 is GTK-only** — `ui.h` hard-includes
`<gtk/gtk.h>` and there is no Athena backend, so it is unbuildable on this stack
(no GTK+/GLib/GDK port). The classic multi-toolkit xbill 2.1 (with
`x11.c` + `x11-athena.c` + `x11-motif.c`) is preserved at the GitHub mirror
`alistairmcmillan/Xbill`; `build-xbill.sh` clones that. Its `configure.in` is
also GTK-tilted and fragile under cross-compile, so the script **bypasses
`./configure`**: it writes a minimal `config.h` (`USE_ATHENA`) and compiles the
Athena object set directly with the cross gcc (the same direct-compile idiom as
`tools/x11-port/apps/build.sh`). Runtime sprite dir + score path are baked in via
a force-included `phoenix_paths.h` (avoids shell quote-stripping of the embedded
C string literals).

## Launch recipes

### Primary route — from an xterm shell (recommended; most flexible)

twm + xterm come up via the existing `startx term` mode. The shell xterm forks is
busybox **ash**, which (unlike psh) **has job control / `&`**, and `DISPLAY=:0`
is already in xterm's environment and inherited by the shell. So, on HW:

```
startx term                 # twm (WM) + xterm (managed terminal)
```

then **inside the xterm window**:

```
# app-defaults resolver — REQUIRED for xcalc, harmless for the others.
# libXt's compiled default search path points at the host build prefix
# (/tmp/x11-phoenix, absent on the Pi), so point it at the staged copies:
export XFILESEARCHPATH=/nfstest/usr/share/X11/app-defaults/%N

ico -faces -sleep 1 &       # spinning polyhedron
oclock &                    # round analog clock
xlogo &                     # the X logo
xclock &                    # analog clock   (xclock -digital & for digital)
xcalc -mono &               # calculator     (see color note below)
xbill &                     # the game
```

All run **in parallel**, each decorated/draggable under twm. `%N` expands to the
resource class (`XCalc`, `XClock`), which matches the staged filenames.

### Alternate route — `startx <app>` (one app, no xterm)

The launcher's `startx` convenience mode runs `<prefix>/bin/<name>` as a single
managed client (no WM unless you ask for `desktop`/`term`). For a single
fullscreen-ish demo app:

```
startx xbill                # Xphoenix + xbill (bare root, no WM)
```

(Bare-root mode gives PointerRoot focus and no titlebar; prefer the xterm route
for the multi-app parallel demo.)

### Launcher note

I deliberately **did not** add an "apps" combo branch to `pl_phoenix_xlaunch.c`.
It is HW-proven flagship code with subtle vfork/`volatile` constraints and the
edit cannot be runtime-validated from the host; the xterm route already delivers
"multiple apps in parallel under the WM" with zero flagship risk. If a
one-command combo is later wanted, the only safe form is a strictly-additive
`strcmp(client,"apps")` else-if (twm + a few apps; `MAX_CLIENTS` is 4) followed
by re-running `build-xlaunch.sh` to confirm it still compiles — and it must be
labelled runtime-unverified until booted.

## Staged files (on `/srv/phoenix-rpi4-nfs`)

- `bin/{ico,oclock,xlogo,xclock,xcalc,xbill}`
- `usr/share/X11/app-defaults/{XCalc,XCalc-color,XClock}`  (xclock/xcalc resources)
- `usr/share/xbill/bitmaps/*.xbm` (15), `usr/share/xbill/pixmaps/*.xpm` (49),
  `usr/share/xbill/scores` (seed high-score file)

## Runtime unknowns (orchestrator to verify on HW)

The pre-flight checks prove the binaries **link** correctly; they do **not**
prove an app survives `XtAppInitialize`/maps a window. Open items:

1. **app-defaults resolution.** `XFILESEARCHPATH` MUST be exported (value above)
   or `xcalc` comes up as a **bare empty window** (its entire button layout lives
   in the `XCalc` app-default). The clocks/logo/ico have sensible built-in
   fallbacks and run without it.
2. **Named-color resolution.** There is **no `rgb.txt` color database** on the
   export. The kdrive server has a small compiled-in `BuiltinColors` table, so
   common names (black/white/grey/red/blue — the same ones xeyes already renders)
   should resolve, but `XCalc-color`'s broader palette may not all resolve and
   could fail widget creation. **Recommend launching `xcalc -mono`** for the demo
   (or just `xcalc`, letting the `XCalc` not `XCalc-color` file apply). Flag the
   `-color` path as unverified.
3. **Core-font availability for Xaw labels.** xterm proves the misc `fixed`/
   common fonts are served over `-fp`, but if any app-default requests a
   specific font not in the served set, label creation could fail. Unverified.
4. **xbill sprite load.** Sprites load at runtime from
   `/nfstest/usr/share/xbill/{bitmaps,pixmaps}` (compiled-in `IMAGES`, verified
   present in the binary); the XBM/XPM trees are staged. If `IMAGES` is somehow
   absent at runtime xbill falls back to `.` (cwd), so launching from
   `/nfstest/usr/share/xbill` is a safe fallback.
5. **xbill scorefile.** `SCOREFILE=/nfstest/usr/share/xbill/scores`; writes may
   fail on the NFS export but xbill degrades gracefully (read+write both
   NULL-check `fopen`).
