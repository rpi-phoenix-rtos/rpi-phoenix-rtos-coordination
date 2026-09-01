# --with-ports clean-build grind (2026-07-23, autonomous)

Goal: make the FULL public build (`--with-showcase --with-ports`) green from clean.
User asleep; autonomous; as many rebuilds as needed.

## Fixed + pushed so far
- GL: phoenix_mesa_compat.h `#ifndef LIBMCS_MATH_H` guard (coord f209218). LINK OK confirmed.
- MicroPython objfloat.c compile: libphoenix libmcs math.h INFINITY/HUGE_VAL*/NAN -> __builtin_*
  (libphoenix ee92022). objfloat.c now COMPILES.

## Setup for fast iteration
- Promoted .toolchain-fresh -> .toolchain (fresh libmcs toolchain now the active local one;
  old backed up at .toolchain-old-prelibmcs). Local builds now reproduce the clean-env bugs.
- Cleared tools/.gpu-libs archives to force GPU rebuild with the fresh toolchain.
- Iterating LOCALLY (build.logs on disk) then confirming with a Docker clean build.

## Open blockers (grind list)
- MicroPython LINK fails (collect2/ld, mkrules.mk:244) — undefined symbols TBD. IN PROGRESS.
- (likely more in --with-ports: X11 stage, dillo/mc/nano — TBD)

## Blocker 3 FIXED: MicroPython link — multiple definition of __signbitd
libphoenix compatibility.c defined __signbitd (+ __signbitf/__fpclassifyf/d/nanf); MicroPython's
bundled lib/libm_dbl also defines __signbitd -> "multiple definition" at link (fresh libphoenix
added these; old lacked them). Fix (libphoenix): mark the 5 libmcs-compat helpers __attribute__((weak))
so a port's bundled strong def overrides. Also named nanf's (previously unnamed) param. Committed
libphoenix eee04d8, pushed. Verified: MicroPython LINKs in both build #2 (incremental) and #3 (clean).

## Blocker 4 FIXED: stale-object contamination (`U schedInfo`) — nuked .buildroot
After promoting the fresh (libmcs) toolchain, the incremental `--scope core` reused core objects
compiled 2026-07-06 against the OLD sysroot; the Makefile does not track the external
`<phoenix/syscalls.h>` dep, so `syscalls.o` kept an old 104-stub list WITHOUT the `schedInfo`
syscall -> glib2 `gtester` link failed `undefined reference to schedInfo` (libphoenix.a had
`U schedInfo`, never `T`). schedInfo IS in the kernel (1ddeed82) AND the pushed fork
(ancestor of publish/master) — pure LOCAL staleness, NOT a publish gap. Fix = `rm -rf .buildroot`
+ full clean rebuild (GPU libs survive under tools/.gpu-libs/). Post-nuke checkpoint: fresh
`syscalls.o` now shows `T schedInfo`. A public Docker build (no .buildroot) never hits this.
NOTE (out of scope, latent): libphoenix Makefile lacks header-dep tracking on the sysroot header.

## Blocker 5 FIXED: lighttpd prepare — missing $PREFIX_ROOTFS/etc on a cold tree
lighttpd port.def.sh p_prepare greps `$PREFIX_ROOTFS/etc/lighttpd.conf` to build its static
plugin-init list. On a cold buildroot the explicit `--scope core`/`full-clean` stage lists omitted
`fs`, so root-skel (which carries etc/lighttpd.conf) was never staged -> `find .../etc` errored ->
prepare aborted. Fix (coord rebuild-rpi4b-fast.sh 0e9fe3f): prepend `fs` to both cold scopes
(build.sh runs clean->fs->core->ports->project fixed-order, so presence is enough; `fs` is a cheap
idempotent cp -a). The auto+sd/showcase and --ports-only paths already staged `fs`; the Docker
DEFAULT (auto+sd) already had it, so this was a cold explicit-scope bug, not a default-path blocker.

## Blocker 6 FIXED: nano/mc pwent shim static-vs-nonstatic clash
libphoenix now IMPLEMENTS getpwent/setpwent/endpwent (unistd/pwd.c, real `T` symbols) and declares
them non-static in <pwd.h>; the nano/mc force-include shims still declared local `static inline`
stubs -> "static declaration of 'getpwent' follows non-static declaration". Fix (coord 844470c):
drop the stubs from nano-phoenix-shim.h + mc-phoenix-shim.h; real libphoenix impls are used.

## RESULT: local clean build GREEN
Build #5 (`--variant nfsroot --with-showcase --with-ports --scope core`, nuked .buildroot, fresh
toolchain): ZERO failures, 47 [OK], rpi4b-sd.img exported + Verification OK. All ports (lighttpd,
micropython, glib2, X11 stack, dillo, wmaker, nano, mc) + rpi4-quake + image built clean.

## Fixes landed + pushed (all on org forks)
- coord f209218  phoenix_mesa_compat.h LIBMCS guard
- libphoenix ee92022  libmcs math.h INFINITY/HUGE_VAL*/NAN -> __builtin_*
- libphoenix eee04d8  libmcs-compat helpers weak + nanf param named
- coord 0e9fe3f  rebuild-rpi4b-fast: `fs` in cold core/full-clean scopes
- coord 844470c  drop nano/mc pwent stubs
- project 5b62e21  bump libphoenix gitlink -> eee04d8

## Docker clean build #1: COMPLETED (image built) but X11 partially broken
`sudo docker build --no-cache --pull <org Dockerfile URL>` -> "Successfully built 913ef19f79dd"
+ tagged; rpi4b-sd-2part.img exported + Verification OK. Core + ports + MOST showcase green.
BUT two best-effort ([WARN], non-fatal) X11 failures — so image-exported != X11 works:
  - Xphoenix (X server) FAILED: miarc.c:89 `static declaration of 'cbrt' follows non-static`.
    xorg-server 1.20.14's `#ifndef HAVE_CBRT { static double cbrt(){} }` fallback now clashes
    with libphoenix libmcs math.h `extern double cbrt(double)`. BLOCKS all X apps.
  - xclock FAILED: `undefined reference to iconv_open/iconv/iconv_close` (Clock.c). The X app
    link closures pull iconv transitively from libX11 i18n but have no `-liconv`; libphoenix has
    no iconv (known gap). The self-written stub (tools/ports/iconv-stub via build-libiconv.sh)
    IS staged into $SYSROOT/lib, just never linked.

### X11 local-cache masking (why local build #5 looked green but Docker didn't)
The `.buildroot` nuke did NOT clear the X11 OUT-OF-TREE caches: tools/x11-port/src/
xorg-server-1.20.14/*/.libs/*.a + /tmp/x11-phoenix (45 X libs) + /tmp/phoenix-iconv, all dated
2026-06-18 (pre-libmcs). build-xserver-core.sh's all_present skip reused the old xserver core
(built against pre-libmcs math.h -> no cbrt decl -> no clash), and the stale X libs didn't yet
reference iconv. Docker (truly clean) was the first fresh X11 build. Same masking class as the
stale toolchain/core objects.

## Blocker 7 FIXED: xserver cbrt static clash
build-xserver-core.sh configure CFLAGS += `-DHAVE_CBRT=1` (source tree is a regenerable
download, so a CFLAG is the persistent fix, not a miarc.c patch). Uses libphoenix cbrt.

## Blocker 8 FIXED: X11 Athena apps missing -liconv
Added `-liconv` (after -lX11) to the link closures of the 6 Athena/-lXaw apps: xclock, xedit,
xlogo, xbill, xcalc, xterm. The stub libiconv.a is already in $SYSROOT/lib (build-libiconv.sh
stages it; runs before the X apps in build-showcase-apps.sh), so -liconv resolves.

## Blocker 9 FIXED: -DHAVE_CBRT exposed that libphoenix declared cbrt but never implemented it
build #6 (fresh X11, caches cleared): -DHAVE_CBRT fixed the miarc COMPILE clash but Xphoenix then
failed at LINK — `undefined reference to cbrt`. Root cause: LIBM_USE_LIBMCS defaults to `n` so the
build uses the NATIVE Phoenix libm (libm/phoenix/*.c: has sin/cos/pow/sqrt, NOT cbrt), while the
libmcs HEADERS are installed unconditionally (libphoenix Makefile:90) and declare `extern cbrt`.
Header promised cbrt; archive (libphoenix.a == libm.a symlink) never provided it. Fix (libphoenix
2457b17): implement cbrt/cbrtf in libm/phoenix/power.c (pow-seed + 1 Newton step; NaN/±0/±Inf/odd
handling) — auto-globbed into libphoenix.a. Now `T cbrt`/`T cbrtf` present.

## RESULT: full local X11 build GREEN (build #7)
All 9 X11 components OK: Xphoenix + xlaunch + xterm + xedit + xcalc + xclock + xlogo + xbill +
WindowMaker. Zero failures/WARN. rpi4b-sd.img exported + Verification OK.

## Fixes landed + pushed (X11 round)
- coord 4b36d45  x11: -DHAVE_CBRT (build-xserver-core.sh) + -liconv (6 Athena app closures)
- libphoenix 2457b17  cbrt/cbrtf in native libm (power.c)
- project e4d2236  bump libphoenix gitlink -> 2457b17

## FINAL GATE: Docker clean build #2 (in progress)
Docker #1 (before X11 fixes): built image but Xphoenix+xclock failed (best-effort). Docker #2
(`--no-cache`, re-clones the pushed X11 fixes) is the real end-to-end public-user gate WITH X11.
Logs: docker #1 dockerbuild.log, docker #2 dockerbuild2.log, local #6/#7 localbuild6/7.log.
KEY LESSON: the .buildroot nuke does NOT clear out-of-tree caches (toolchain-bundled headers,
tools/x11-port/src xserver core archives, /tmp/x11-phoenix X libs, /tmp/phoenix-iconv). A truly
clean local proxy must also clear those. All these blockers were latent shortcuts the OLD local
toolchain/artifacts masked; libmcs (fuller math headers) + a fresh toolchain surfaced them.

## DONE — Docker clean build #2 GREEN (2026-07-23)
`docker build --no-cache --pull <org Dockerfile URL>` -> "Successfully built 921cb94a03fc" +
tagged rpi-phoenix-clean:27b. rpi4b-sd-2part.img (835 MiB) exported + Verification OK. ZERO
failures/WARN across the whole log; all 9 X11 components [OK] (Xphoenix + xlaunch + xterm + xedit
+ xcalc + xclock + xlogo + xbill + WindowMaker). The full public build (--with-showcase
--with-ports, default sd variant) now builds end-to-end from truly clean org repos — a public user
can clone + build. 9 blockers fixed; all pushed. TASK COMPLETE.
