# GNU coreutils port to Phoenix/RPi4 (owner "full bash/zsh + coreutils beyond busybox")

Scouted 2026-08-13. coreutils 9.5 (https://ftp.gnu.org/gnu/coreutils/coreutils-9.5.tar.xz,
sha256 cd328edeac92f6a665de9f323c93b712af1858bc2e0d88f3f7100469470a1b8a, 6007136 bytes).
**Status: DEDICATED MULTI-CYCLE PROJECT — banked with this precise resume note.** Unlike bash (a short flat list of
libphoenix gaps), coreutils drags in gnulib, whose modules substitute their own impls and each pull the next Phoenix
gap — a dependency *tree*. Advisor-endorsed to timebox + bank once the walls turned to whack-a-mole.

## DONE
- **configure now PASSES** (exit 0). The one fatal configure wall was gnulib mountlist: *"could not determine how to
  read list of mounted file systems"* — Phoenix's `<mntent.h>` was an empty file (header present → configure "yes",
  functions absent → "no"). **Fixed: libphoenix 29f5373** implements the getmntent family (`mntent/mntent.c` +
  populated `include/mntent.h`). configure then reports `getmntent... yes`, `one-argument getmntent... yes`.
  - PENDING for 29f5373: `--scope core` + Pi boot-verify (additive, 0 regression risk — no in-tree consumer yet) +
    org push. Batch with the first real coreutils build cycle.
- Cross-configure invocation (drop `--enable-static`, coreutils warns it's unrecognized; static comes from the
  toolchain/LDFLAGS): `./configure --host=aarch64-phoenix CC=aarch64-phoenix-gcc --disable-nls`.

## PROGRESS 2026-08-13 session 10: 325 → 34 build errors (2 walls cleared)
- **Wall #1 CLEARED (gettime/settime collision, was 122 errors).** Phoenix uses bare `gettime`/`settime` at 108 sites
  across the device/sensor tree (native time API) → the Phoenix-side namespace fix is OUT (too broad). Instead a
  **port-local rename of gnulib's** gettime/settime → gl_gettime/gl_settime (word-boundary, leaves clock_gettime/
  gettimeofday/gettime_res untouched) across 10 files. **Captured: sources/phoenix-rtos-ports/coreutils/patches/
  0001-rename-gnulib-gettime-settime.patch** (dry-run applies clean to pristine). 325 → 75.
- **Wall #2 CLEARED (assert, was 39 errors) — a real Phoenix libc bug.** gnulib's `<assert.h>` substitute does
  `#include_next <assert.h>` relying on assert being redefined each inclusion; Phoenix's assert.h had a permanent
  once-guard so assert was defined only once → gnulib assure()/affirm() → undeclared assert. **Fixed: libphoenix
  26317c2** (drop the once-guard, `#undef assert`+redefine each include, glibc/musl parity). 75 → 34.
- libphoenix commits pending propagation (all LOCAL, additive/low-risk): 29f5373 getmntent + d2a2c1f (its -Werror
  unused-var fix) + 26317c2 assert re-includable. `--scope core` validation in progress this session.

## SESSION 11 (2026-08-13): 2 more libphoenix-hardening walls cleared → 32 errors; BANKED at the FILE-internal wall
Advisor-guided stop line: **keep clearing walls only while they yield reusable libphoenix value; stop at the
gnulib-internal-glue tar pit.** Cleared the two remaining libphoenix wins:
- **getprogname/setprogname** added to libphoenix (**a7abcfd**, stdlib/progname.c backed by crt0 argv_progname;
  declared in <stdlib.h>). configure now HAVE_GETPROGNAME=1 → gnulib's getprogname.c #error gone.
- **pthread_sigmask** declared in <signal.h> (a7abcfd; was only in <pthread.h>). POSIX-correct; gnulib pselect.c
  implicit-decl gone.
These are standard-libc hardening reusable by ANY port. --scope core validation in progress; then org push.

**BANK LINE REACHED — remaining walls are gnulib-internal glue (marginal value over busybox, open-ended):**
The FILE-internal accessors #error on Phoenix's custom `struct _FILE` layout — and there are **6**, not 3 (exactly as
predicted): `fpending.c`, `freadahead.c`, `freading.c`, `freadptr.c`, `freadseek.c`, `fseterr.c`. Porting them means
per-file gnulib patches poking Phoenix FILE internals (bufpos/bufeof/buffer/flags — the fields ARE there, so it's
doable but fragile, port-local, and unbounded: more accessors may surface). Coreutils-the-binary is marginal over the
busybox utils Phoenix already ships, so value/cost goes negative here. **DECISION: bank; do NOT port the FILE-internal
glue.** Resume as a dedicated push only if GNU-coreutils-specific behavior is explicitly needed.

## RESUME STATE (exact, for a future dedicated coreutils push)
Scratch build: was at /home/houp/.claude/jobs/.../coreutils-build (ephemeral). Reproduce: extract coreutils 9.5,
apply ports `coreutils/patches/0001-rename-gnulib-gettime-settime.patch`, configure `--host=aarch64-phoenix
CC=aarch64-phoenix-gcc --disable-nls`, `make CFLAGS_FOR_BUILD="-std=gnu89 -Wno-error=implicit-function-declaration
-Wno-error=implicit-int"`. Requires libphoenix >= a7abcfd (getmntent, re-includable assert, getprogname,
pthread_sigmask-in-signal.h). Then the remaining walls, in order:
1. **Port the 6 gnulib FILE-internal files** (fpending/freadahead/freading/freadptr/freadseek/fseterr) with a Phoenix
   branch each, using `struct _FILE` { fd, flags, mode, bufeof, bufpos, bufsz, buffer } — e.g. freadptr returns
   `buffer+bufpos` with size `bufeof-bufpos` when in read mode. Capture as coreutils patches 0002+.
2. **config.cache** (add to a coreutils/config.cache for the port): force lchown "works" (gl_cv_func_lchown_works=yes
   or REPLACE_LCHOWN=0) so gnulib doesn't compile its lchown.c; force the struct-rlimit detection so src/sort.c does
   NOT define its `struct rlimit { size_t rlim_cur; }` fallback (Phoenix HAS struct rlimit in <sys/resource.h>).
3. **EXCLUDE from the built subset:** `stat` (needs struct statfs/statfs() — add <sys/statfs.h> to libphoenix if
   wanted later) and `stty` (termios macro gap). Build specific targets, not `make all`.
4. Formalize sources/phoenix-rtos-ports/coreutils/port.def.sh once it links a subset.

## REMAINING BUILD WALLS (original 34-error breakdown) — next session
1. **[~16] `struct statfs`/`statfs()` (src/stat.c).** Phoenix lacks `<sys/statfs.h>` + statfs(). **EXCLUDE `stat`**
   from the built subset (it's a leaf util); revisit statfs later if wanted.
2. **[1] `getprogname` #error "not ported" (gnulib lib/).** Used widely (error messages) — NOT excludable. Add
   `getprogname`/`setprogname` (+ maybe `program_invocation_name`) to libphoenix. HIGH priority next.
3. **[3] fseterr.c / freadptr.c / freadseek.c #error "port to your platform" (gnulib lib/).** These poke Phoenix's
   FILE internals (buffer ptrs, ferror/clearerr/getc/fflush internals). HARDEST class. Check which built utils pull
   them (od, tac, shuf?) — may be excludable; else port to Phoenix's FILE layout (sources/libphoenix/stdio).
4. **[1] `struct rlimit` redefinition (sort.c).** Phoenix `<sys/resource.h>` vs gnulib — header conflict; sort is
   WANTED. Reconcile (likely a gnulib config/guard or a Phoenix resource.h guard).
5. **[1] `pthread_sigmask` implicit (gnulib pselect.c).** Add pthread_sigmask to libphoenix pthread (or gnulib sub).
6. **[1] `lchown` implicit.** Add lchown to libphoenix (symlink-aware chown; or gnulib substitute).
7. **[1] stty.c expected-expression (termios macro gap).** EXCLUDE `stty`.

## ORIGINAL BUILD WALLS (make -k → 325 errors, clustered) — for reference
1. **[122 errors] `gettime`/`settime` namespace collision (HIGHEST LEVERAGE).** Phoenix `sys/time.h:34,37` declares
   NON-STANDARD `int gettime(time_t *raw, time_t *offs)` + `int settime(time_t offs)` (Phoenix's native time API).
   gnulib `lib/timespec.h:93,94` declares `void gettime(struct timespec *)` + `int settime(struct timespec const *)`.
   Every TU including both → conflicting-types. **Fix options:** (a) port-local gnulib patch renaming its gettime/
   settime → gl_gettime/gl_settime (contained, but touches many gnulib files); (b) the RIGHT long-term fix — stop
   Phoenix `sys/time.h` from exposing bare `gettime`/`settime` in the default namespace (guard behind a
   `_PHOENIX_SOURCE`-style feature macro). (b) is a broad libphoenix change with existing in-tree users (drivers/
   kernel-adjacent) — do it deliberately, grep all callers first, NOT rushed. Prefer (a) for the port initially.
2. **[39 errors] `assure.h`: implicit `assert`.** gnulib assure.h expects `<assert.h>` in scope. Check Phoenix
   `<assert.h>` (exists? NDEBUG behavior?) — likely a one-line include or a config.cache/gnulib fix.
3. **[~15 errors] `struct statfs` / `statfs()` undefined (stat.c).** Phoenix lacks `<sys/statfs.h>`/`<sys/vfs.h>` +
   `statfs()`. Feeds `stat -f` and df. Either add a minimal `struct statfs` + `statfs()` stub to libphoenix, or
   exclude `stat`/`df` from the built subset (see below).
4. **Singles:** `getprogname` "module not ported to this OS" (#error — add getprogname/program_invocation_name to
   libphoenix, or gnulib port stub); `pthread_sigmask` implicit (pselect.c — Phoenix pthread gap); `lchown` implicit
   (add to libphoenix or gnulib substitutes); `struct rlimit` redefinition (sort.c — Phoenix sys/resource.h vs
   gnulib); `stty.c` expected-expression (termios macro gap); `mini-gmp.c` assert (same as #2).

## STRATEGY (when this project is picked up)
- Scope to a value SUBSET (advisor): ls, cat, cp, mv, rm, mkdir, echo, printf, wc, sort, head, tail, cut, tr, uniq,
  seq, true, false, env, basename, dirname, tee, touch, ln, pwd, sleep, yes, comm, join, paste, nl, tac, rev.
  EXCLUDE the exotic/OS-heavy ones whose gnulib deps aren't worth chasing: df, stat (-f), who, pinky, users, uptime,
  chcon, runcon, stty (maybe), df. coreutils build order still compiles gnulib lib/ wholesale, so walls #1/#2 must be
  cleared regardless; only #3/#4 are subset-avoidable.
- Verify built utils non-interactively through psh-interact exactly like `bash /t.sh` (stage into the NFS root, run
  `/bin/ls -la /`, `/bin/wc /etc/...`, etc., grade on clean output + 0 faults).
- Formalize as `sources/phoenix-rtos-ports/coreutils/port.def.sh` (autoconf template, like the bash port) + patches
  (the gnulib gettime rename, getprogname) + a config.cache once the run-tests settle.

## NEXT ACTION when resumed
Clear wall #1 first (it blocks everything; 122 errors). Try the port-local gnulib timespec rename patch; if that's
clean, rebuild and reassess from wall #2. Then decide subset vs full from the remaining wall count.
