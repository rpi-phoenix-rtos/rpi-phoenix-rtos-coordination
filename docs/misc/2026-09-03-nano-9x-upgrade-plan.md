# Upgrading the `nano` port from 2.2.6 to 9.2

Date: 2026-09-03
Status: **PROPOSAL / plan only.** No file under `sources/phoenix-rtos-ports/nano/` was
modified. No build was run (an authoritative Docker build owns the machine).
Every configure-time conclusion below is derived from reading nano 9.2's shipped
`configure` / `m4` / `lib` and from grepping libphoenix — none of it is
build-verified yet. The verification order is in §8.

Owner request: ship a **modern** nano instead of the deliberately-pinned 2.2.6.

---

## 1. The current recipe (2.2.6) — what it actually does

`sources/phoenix-rtos-ports/nano/port.def.sh`

| field | value |
|---|---|
| `version` | `2.2.6` |
| `source` | `https://www.nano-editor.org/dist/v2.2` |
| `archive_filename` | `nano-2.2.6.tar.gz` |
| `size` | `1572388` |
| `sha256` | `be68e133b5e81df41873d32c517b3e5950770c00fc5f4dd23810cd635abce67a` |
| `license` | `GPL-3.0-or-later` (`COPYING`) |
| `depends` | `ncurses` |

`p_prepare()` (lines 41-56)
1. `b_port_apply_patches "${PREFIX_PORT_WORKDIR}"`
2. **Donor-copy of `config.sub`/`config.guess`**: nano 2.2.6 (2010) ships an
   autoconf helper pair that predates the `phoenix` OS triplet, so
   `./configure --host=aarch64-phoenix` is rejected. The recipe greps
   `${PREFIX_PORT_BUILD}/../*/*/config.sub` for a file containing `phoenix`
   (ncurses is a `depends`, so it is already extracted) and copies it in.

`p_build()` (lines 58-82)
- `xcflags = ${CFLAGS} -O2 -I${PREFIX_H} -I${PREFIX_H}/ncurses -include ${PREFIX_PORT}/nano-phoenix-shim.h`
- `./configure --host=${HOST} --build=x86_64-pc-linux-gnu --disable-nls --disable-utf8`
  with `LDFLAGS="… -static -L${PREFIX_A}" LIBS="-lncurses"` and the cache hints
  `ac_cv_lib_ncursesw_initscr=no ac_cv_lib_ncurses_initscr=yes`
- `make`, assert `src/nano` exists, `$STRIP` into `PREFIX_PROG_STRIPPED`,
  `b_install … /bin`.

Force-included shim `nano/nano-phoenix-shim.h` — supplies only `P_tmpdir` today
(the `getpwent/setpwent/endpwent` stubs it once carried were deleted once
libphoenix grew the real passwd-enumeration API).

Patches — `sources/phoenix-rtos-ports/nano/patches/` contains exactly **one** file:

- `0001-nano-bool-init-not-null.patch` — `src/global.c`:
  `bool edit_refresh_needed = NULL;` → `= FALSE`. In 2.2.6 `bool` resolves to
  ncurses' `NCURSES_BOOL` (an `int`), so the `NULL` initializer is an
  int-from-pointer conversion — a warning for a decade, a **hard error** since
  GCC 14 (and `.toolchain` is GCC 16.2).

### Why 2.2.6 was pinned

`port.def.sh:35-39` states it plainly:

> nano 2.2.x is used deliberately: it bundles NO gnulib, so it sidesteps the
> gnulib-vs-Phoenix namespace collisions (gettime/getprogname/...) that block the
> modern (6.x) nano.

That rationale is **obsolete**: the coreutils port has since shipped a fully
gnulib-based program on Phoenix, and the collision set turns out to be tiny and
already solved.

---

## 2. How coreutils solved the gnulib-vs-libphoenix collisions

Mechanism: **a source patch applied with `patch -p1` that renames the gnulib
function**, not a `-D` on the command line, not a `config.h` override.

`sources/phoenix-rtos-ports/coreutils/patches/0001-rename-gnulib-gettime-settime.patch`

```
 1: --- acu/lib/timespec.h
 2: +++ bcu/lib/timespec.h
 3: @@ -90,8 +90,8 @@
 5:  long int gettime_res (void);
 6:  struct timespec current_timespec (void);
 7: -void gettime (struct timespec *) _GL_ARG_NONNULL ((1));
 8: -int settime (struct timespec const *) _GL_ARG_NONNULL ((1));
 9: +void gl_gettime (struct timespec *) _GL_ARG_NONNULL ((1));
10: +int gl_settime (struct timespec const *) _GL_ARG_NONNULL ((1));
```

…plus the matching definition/call-site renames in `lib/gettime.c`,
`lib/settime.c`, `lib/parse-datetime.y`, `lib/parse-datetime.c`,
`lib/utimens.c`, `lib/gethrxtime.c`, `src/date.c`, `src/ls.c`, `src/pr.c`.

**Symbols renamed:** exactly two — `gettime` → `gl_gettime`,
`settime` → `gl_settime`.

**Why they collide.** libphoenix exports its own *native* Phoenix API under
those names, with incompatible signatures:

- `sources/libphoenix/include/sys/time.h:34` — `extern int gettime(time_t *raw, time_t *offs);`
- `sources/libphoenix/include/sys/time.h:37` — `extern int settime(time_t offs);`

gnulib declares `void gettime(struct timespec *)` / `int settime(struct timespec const *)`.
Both declarations are visible in any TU that includes `<sys/time.h>` **and**
gnulib's `lib/timespec.h` (which `lib/gettime.c` and `lib/utimens.c` both do) →
*conflicting types for 'gettime'* → compile stops.

**Why a `-D` would NOT work.** `-Dgettime=gl_gettime` rewrites the identifier on
*both* sides of the collision: libphoenix's declaration becomes
`int gl_gettime(time_t*, time_t*)` and gnulib's becomes
`void gl_gettime(struct timespec*)` — the same conflict, one name later. The
rename has to be applied to gnulib's source only, hence the patch. (This
explains the coreutils choice; it is not merely stylistic.)

Two companion coreutils patches are relevant precedent for nano:

- `0002-port-gnulib-stdio-internals-phoenix.patch` — teaches gnulib's
  `freadahead/freading/freadptr/fpending/**fseterr**/freadseek` about
  libphoenix's `FILE`. The fseterr hunk (patch line 70) is
  `fp->flags |= (1u << 3) /*F_ERROR*/;` under `#if defined __phoenix__`.
  Still accurate: `sources/libphoenix/include/stdio.h:57-70` defines
  `typedef struct _FILE { int fd; unsigned flags; int mode; size_t bufeof;
  size_t bufpos; size_t bufsz; char *buffer; handle_t lock; … } FILE;` and
  `sources/libphoenix/stdio/file.c:38-41` has `F_EOF (1<<0)`,
  `F_WRITING (1<<1)`, `F_ERROR (1<<3)`.
- `coreutils/config.site` — the cross `ac_cv_*` / `gl_cv_*` answer sheet.
  Header comment in `coreutils/port.def.sh`: *"configure otherwise mis-guesses
  many present Phoenix libc functions as 'missing/broken' when cross-compiling
  and pulls in gnulib replacements that then fail to build."*
  Also load-bearing there: **`-O2`**, because at `-O0` GCC defines
  `__NO_INLINE__`, `gl_cv_c_inline_effective` fails, `HAVE_INLINE` stays unset
  and gnulib's extern-inline helpers are neither inlined nor emitted →
  undefined references at link. (nano's recipe already passes `-O2`.)

---

## 3. The nano 9.2 tarball

Latest v9.x on <https://www.nano-editor.org/dist/v9/> is **9.2** (9.0, 9.1, 9.2
present; no 9.3). `NEWS` line 1: `2026.07.31 - GNU nano 9.2 "Alquézar"`.

Downloaded to `/home/houp/.claude/jobs/c8f1289c/tmp/nano-recon/` and extracted
(not compiled):

| file | size (bytes) | sha256 |
|---|---|---|
| `nano-9.2.tar.xz` | `1760684` | `05ecb99247b782e8a5b3a25ed4101dd034b0236902f7449bc9795b717642f7e9` |
| `nano-9.2.tar.gz` | `3682452` | `362d4cedbcefc20b4898382e325abf35cad00c2054a445b474260fff99bbbeb4` |

Recommendation: **use `.tar.xz`** (half the size; the coreutils port already
proves the framework handles `.tar.xz`). The `.tar.gz` numbers are recorded so
either choice is a one-line edit.

---

## 4. What nano 9.2 bundles, and what actually collides

### 4.1 gnulib modules

`m4/gnulib-cache.m4` — 23 directly-requested modules:

```
canonicalize-lgpl  futimens  getdelim  getline  getopt-gnu  glob  isblank
iswblank  lstat  mkstemps  nl_langinfo  regex  sigaction  snprintf-posix
stdarg-h  strcase  strcasestr-simple  strnlen  sys_wait  vsnprintf-posix
wchar-h  wctype-h  wcwidth
```

Transitively these pull ~400 files into `lib/` (full gnulib `regex`, `glob`,
`vasnprintf`, `unictype`/`uniwidth`/`unicase`/`unistr` tables, `glthread/`,
`malloc/` dynarray+scratch_buffer, the `*.in.h` system-header overrides, …).

### 4.2 The collision analysis

Two mechanical cross-checks were run (script in the scratch dir, not committed):

1. every function **defined** at column 0 in `nano-9.2/lib/**/*.c` vs every
   function **declared** in `sources/libphoenix/include/**/*.h` → 75 raw name
   matches;
2. every function **declared** in nano's *own* gnulib headers
   (`lib/*.h`, excluding the `*.in.h` system-header overrides) vs the same
   libphoenix set.

Almost all 75 are false alarms: they are gnulib *replacements* for system
functions, declared inside a `*.in.h` override (`stdio.in.h`, `string.in.h`,
`unistd.in.h`, `wchar.in.h`, `signal.in.h`, `fcntl.in.h`, `glob.in.h`, …). Those
compile only when configure says the platform needs them, and when they do they
are renamed to `rpl_<name>` by the header's own machinery. Cross-check 2 — which
looks only at gnulib's *own* API surface — narrows it to a real set of **two**:

| symbol | libphoenix | nano 9.2 gnulib | verdict |
|---|---|---|---|
| `gettime` | `include/sys/time.h:34` `int gettime(time_t*, time_t*)` | `lib/timespec.h:93` `void gettime(struct timespec*)` | **HARD conflict** |
| `settime` | `include/sys/time.h:37` `int settime(time_t)` | `lib/timespec.h:94` `int settime(struct timespec const*)` | **HARD conflict** (declaration only) |

i.e. **the identical pair coreutils already patched.**

`gettime` reach in nano 9.2 (exhaustive grep):
`lib/timespec.h:93` (decl), `lib/gettime.c:29` (definition), `lib/gettime.c:49`
(call from `current_timespec`), `lib/utimens.c:161` and `lib/utimens.c:166`
(calls). `settime` reach: `lib/timespec.h:94` **only** — nano ships no
`lib/settime.c` (coreutils did), so the settime rename is declaration-only.

`lib/gettime.c` and `lib/utimens.c` are **unconditional** members of
`libgnu_a_SOURCES` (`lib/Makefile.am:952`, `:4277`), and the module is genuinely
needed: `src/files.c:1661` calls `futimens(descriptor, filetimes)`, libphoenix
has neither `futimens` nor `utimensat` (grep of `include/` finds neither), so
gnulib's `futimens.c` → `utimens.c` path is live. This is not avoidable by
configure flags.

Names the old comment worried about, resolved:

- **`getprogname`** — *no longer a problem.* libphoenix declares
  `extern const char *getprogname(void);` at `include/stdlib.h:126` with
  exactly gnulib's signature, and implements it in `stdlib/progname.c`.
  `m4/getprogname.m4` will see the declaration and the module becomes a no-op.
- `getline`, `getdelim`, `mempcpy`, `strnlen`, `strcasestr`, `strncpy`,
  `stpcpy`, `mkstemps`, `nl_langinfo`, `isblank`, `iswblank`, `wcwidth`,
  `mbrtowc`, `wcrtomb`, `sigaction`+`sig*set`, `openat`, `fstatat`,
  `readlink`, `raise`, `fcntl`, `open`, `opendir`/`readdir`/`closedir`,
  `getrandom`, `getlogin_r`, `localeconv`, `pthread_once`, `pthread_sigmask`,
  `snprintf`/`vsnprintf`/`strerror` — all present in libphoenix **and** all
  routed through a gnulib `*.in.h` with `rpl_` renaming. No action.
- `basename`/`dirname` — nano bundles only the `-lgpl` variants, whose exported
  names are gnulib-internal (`last_component`, `base_len`, `mdir_name`). No clash.
- `canonicalize_file_name` — libphoenix declares it (`include/stdlib.h:263`) and
  gnulib routes it through `stdlib.in.h`. No clash.
- `error` — libphoenix does not declare `error`; gnulib owns `error.in.h`. No clash.
- `getopt`/`getopt_long` — libphoenix declares both (`unistd.h:268`,
  `getopt.h:39`); gnulib's `getopt-pfx-core.h` `#define`s the names to
  `rpl_getopt*` *before* including `<unistd.h>`, which is precisely the
  glibc/musl case it is written for. No action expected.

### 4.3 A third, non-symbol Phoenix gap: `fseterr`

`lib/fseterr.c:78` ends its platform ladder with

```
#error "Please port gnulib fseterr.c to your platform! …"
```

and `m4/gnulib-comp.m4:587` makes it compile whenever
`ac_cv_func___fseterr = no` — which is Phoenix. So **nano 9.2 will not build
unpatched**, independently of the `gettime` clash. Same file, same hunk shape as
coreutils patch `0002`.

Nothing else in `lib/` carries a `#error … port …` (grep confirms `fseterr.c:78`
is the only one).

### 4.4 Build-time requirements vs what Phoenix has

| requirement | status |
|---|---|
| **ncurses** (`AC_MSG_ERROR` if absent) | ✅ `sources/phoenix-rtos-ports/ncurses/port.def.sh` — ncurses 6.4, static, `-fPIC`, terminfo fallbacks compiled in (`vt100,linux,ansi,xterm,…`), headers in `PREFIX_H` **and** `PREFIX_H/ncurses`. `src/definitions.h:69/71` includes `<ncurses.h>` else `<curses.h>` — both mirrored by the ncurses port. |
| **ncursesw** (needed for `--enable-utf8`) | ❌ narrow only. Keep `--disable-utf8` (same as today — parity, not a regression). A widec ncurses variant is deliberately banked: `mc` and CPython `_curses` both link the narrow `libncurses.a`. |
| `config.sub` knows `phoenix` | ✅ **new in 9.2** — `config.sub:1754` lists `phoenix*`. (2.2.6's has zero occurrences.) The donor-copy hack is obsolete. |
| `P_tmpdir` (`src/files.c:1432`) | ✅ now native: `sources/libphoenix/include/stdio.h:50` `#define P_tmpdir "/var/tmp"`. nano falls back to `/tmp/` if that is not writable (`src/files.c:1421-1432`). |
| `TIOCGWINSZ` | ✅ `libphoenix/include/termios.h:239`; HW-proven on `pl011-tty` (the `ncurses_smoke` turn read a true 67×240 console). |
| `SIGWINCH` | ✅ `libphoenix/include/signal.h:60`. |
| `fork`/`pipe`/`waitpid`/`fsync`/`fchmod`/`fchown`/`geteuid`/`flockfile` | ✅ all declared in libphoenix. |
| `getpwnam_r`, `getlogin_r` | ✅ (`pwd.h:45`, `unistd.h:295`) — prerequisites of gnulib `glob`'s `GLOB_TILDE`. |
| gettext / NLS | not needed; `--disable-nls`. `AM_GNU_GETTEXT([external])` degrades to the no-op `gettext.h` macros. |
| libmagic | ❌ absent, and **must be switched off explicitly** — see the `--disable-libmagic` rationale in the §5 `p_build` comment. |
| `pkg-config` | present on the *host*, which is a hazard — see the `NCURSES_CFLAGS`/`NCURSES_LIBS` rationale in the §5 `p_build` comment. |

### 4.5 gnulib cross-guess policy (affects how much gnulib gets compiled)

`m4/gnulib-common.m4:1015` defaults `--enable-cross-guesses=conservative`, i.e.
`gl_cross_guess_normal="guessing no"`, `gl_cross_guess_inverted="guessing yes"`
("if we don't know, assume the worst"). Consequences worth knowing before the
first build:

- **`glob` gets replaced.** `m4/glob.m4` sets
  `gl_cv_glob_overflows_stack="$gl_cross_guess_inverted"` = *guessing yes* →
  `REPLACE_GLOB=1` → `gl_REPLACE_GLOB_H`. That is the **good** outcome:
  `lib/glob.in.h:28` is `#if @HAVE_GLOB_H@ && !@REPLACE_GLOB@ … include_next`,
  so with `REPLACE_GLOB=1` gnulib does *not* pull libphoenix's `<glob.h>` at
  all — it uses its own `glob-libc.gl.h` `glob_t` and glibc-style positive
  `GLOB_NOMATCH`, and `src/rcfile.c:993`'s `glob(...)` becomes `rpl_glob`.
  libphoenix's BSD-flavoured `glob_t` and negative `GLOB_NOMATCH (-3)`
  (`include/glob.h:86-88`) are bypassed entirely. **Do not** pin
  `gl_cv_glob_*` to force the system glob unless the gnulib glob fails to build.
- Many other replacements (`snprintf`/`vsnprintf` POSIX, `mbrtowc`, `wcwidth`,
  `nl_langinfo`, `regex`, `canonicalize`, `lstat`, `fstatat`, `open`, `fcntl`)
  will likewise be compiled from gnulib rather than taken from libphoenix. All
  are portable C and self-consistent (`rpl_` names). The coreutils lesson is
  that this is *usually* fine and occasionally produces one replacement that
  will not compile on Phoenix — which is what `config.site` exists to suppress.
  Expect one or two iterations here; the failures are loud compile errors, not
  silent misbehaviour.

---

## 5. Deliverable (a): the `port.def.sh` diff

```diff
--- a/nano/port.def.sh
+++ b/nano/port.def.sh
@@
 	name="nano"
-	version="2.2.6"
+	version="9.2"
 	desc="GNU nano — small console text editor (static, links the ncurses port)"
 	cpe23="cpe:2.3:a:gnu:nano:${version}:*:*:*:*:*:*:*"
 
-	source="https://www.nano-editor.org/dist/v2.2"
-	archive_filename="${name}-${version}.tar.gz"
+	source="https://www.nano-editor.org/dist/v9"
+	archive_filename="${name}-${version}.tar.xz"
 	src_path="${name}-${version}/"
 
-	size="1572388"
-	sha256="be68e133b5e81df41873d32c517b3e5950770c00fc5f4dd23810cd635abce67a"
+	size="1760684"
+	sha256="05ecb99247b782e8a5b3a25ed4101dd034b0236902f7449bc9795b717642f7e9"
 
 	license="GPL-3.0-or-later"
 	license_file="COPYING"
@@
-# patches/0001-nano-bool-init-not-null.patch fixes `bool edit_refresh_needed =
-# NULL;` (global.c). nano 2.2.6 predates C99 <stdbool.h> being the norm, [...]
-#
-# nano 2.2.x is used deliberately: it bundles NO gnulib, so it sidesteps the
-# gnulib-vs-Phoenix namespace collisions (gettime/getprogname/...) that block the
-# modern (6.x) nano. The only Phoenix gaps are P_tmpdir + the passwd-enumeration
-# API — the former is supplied by the force-included nano-phoenix-shim.h, the
-# latter by libphoenix's own getpwent/setpwent/endpwent.
+# nano 9.x IS gnulib-based, and that is fine now: the "gnulib-vs-Phoenix
+# namespace collision" that pinned this port at 2.2.6 turns out to be exactly
+# two symbols, and coreutils already solved them the same way (patches/0001).
+#
+#   0001 renames gnulib gettime/settime -> gl_gettime/gl_settime. libphoenix
+#        exports NATIVE Phoenix calls under those names with different
+#        signatures (sys/time.h:34,37), so gnulib's lib/timespec.h declarations
+#        conflict. A -D rename cannot work: it rewrites BOTH declarations.
+#        Live because src/files.c calls futimens(), which libphoenix lacks, so
+#        gnulib's futimens -> utimens -> gettime path is compiled.
+#   0002 gives gnulib's lib/fseterr.c a Phoenix branch. Without it the file
+#        (compiled because Phoenix has no __fseterr) hits its
+#        `#error "Please port gnulib fseterr.c"` ladder end.
+#
+# config.site carries the cross ac_cv_*/gl_cv_* answers: gnulib's default
+# --enable-cross-guesses=conservative otherwise assumes the worst about every
+# libc function it cannot run a test program for.
+#
+# Phoenix needs no shim any more: P_tmpdir is in libphoenix <stdio.h> and
+# getpwent/setpwent/endpwent are native. Force-including a header is in fact now
+# HARMFUL — gnulib headers hard-`#error` unless config.h is included first.
+#
+# UTF-8 stays off: the ncurses port is narrow (no ncursesw), same as 2.2.6.
 
 p_prepare() {
 	b_port_apply_patches "${PREFIX_PORT_WORKDIR}"
-
-	# nano 2.2.6 (2010) ships a config.sub/guess that predates the `phoenix`
-	# triplet, so ./configure would reject `aarch64-phoenix`. Copy a phoenix-aware
-	# pair from an already-extracted dependency under port-sources (ncurses is a
-	# dep, built + extracted first). Same donor-copy idiom as the glib2 port.
-	local psrc donor
-	psrc="$(dirname "${PREFIX_PORT_BUILD}")"
-	donor="$(grep -l phoenix "${psrc}"/*/*/config.sub 2>/dev/null | grep -v nano | head -1)"
-	if [ -n "${donor}" ]; then
-		cp "${donor}" "${PREFIX_PORT_WORKDIR}/config.sub"
-		[ -f "$(dirname "${donor}")/config.guess" ] && \
-			cp "$(dirname "${donor}")/config.guess" "${PREFIX_PORT_WORKDIR}/config.guess"
-	fi
+	# No config.sub donor-copy needed: nano 9.2's config.sub already lists
+	# `phoenix*` among the known OS triplets (config.sub:1754).
 }
 
 p_build() {
-	# ncurses (headers + libncurses.a) live in the shared PREFIX_H/PREFIX_A. Force
-	# -include the shim (P_tmpdir). ac_cv_lib_* answers steer nano's configure at
-	# the NARROW ncurses (no ncursesw). -static is load-bearing: the deliverable is
-	# a static aarch64-phoenix ELF with zero undefined symbols.
-	local xcflags="${CFLAGS} -O2 -I${PREFIX_H} -I${PREFIX_H}/ncurses -include ${PREFIX_PORT}/nano-phoenix-shim.h"
+	# ncurses (headers + libncurses.a) live in the shared PREFIX_H/PREFIX_A.
+	# -O2 is load-bearing for gnulib: at -O0 GCC defines __NO_INLINE__,
+	# gl_cv_c_inline_effective fails, HAVE_INLINE stays unset and gnulib's
+	# extern-inline helpers are neither inlined nor emitted -> undefined refs
+	# at link (same trap as the coreutils port). -static is load-bearing: the
+	# deliverable is a static aarch64-phoenix ELF with zero undefined symbols.
+	local xcflags="${CFLAGS} -O2 -I${PREFIX_H} -I${PREFIX_H}/ncurses"
 
 	if [ ! -f "${PREFIX_PORT_WORKDIR}/config.status" ]; then
-		(cd "${PREFIX_PORT_WORKDIR}" && ./configure \
-			--host="${HOST}" --build=x86_64-pc-linux-gnu --disable-nls --disable-utf8 \
+		# NCURSES_CFLAGS/NCURSES_LIBS are preset ON PURPOSE. nano 9.x's
+		# configure tries PKG_CHECK_MODULES([NCURSES],[ncurses]) FIRST, and on a
+		# dev host with libncurses-dev installed that would silently inject the
+		# HOST's -I/-L. Presetting both short-circuits the pkg-config query
+		# (configure: `if test -n "$NCURSES_CFLAGS"`), so the cross ncurses port
+		# is the only curses in play. --disable-libmagic is also deliberate:
+		# nano's libmagic check defaults to "on unless disabled" and its
+		# AC_CHECK_LIB(z, inflate) would pick up the sysroot's zlib, adding an
+		# undeclared dependency. --disable-maintainer-mode keeps automake out of
+		# the loop (nano ships AM_MAINTAINER_MODE([enable])).
+		(cd "${PREFIX_PORT_WORKDIR}" && CONFIG_SITE="${PREFIX_PORT}/config.site" ./configure \
+			--host="${HOST}" --build=x86_64-pc-linux-gnu \
+			--disable-nls --disable-utf8 --disable-libmagic \
+			--disable-maintainer-mode \
 			CC="${CROSS}gcc" AR="${CROSS}ar" RANLIB="${CROSS}ranlib" \
 			CPPFLAGS="${CFLAGS} -I${PREFIX_H} -I${PREFIX_H}/ncurses" \
 			CFLAGS="${xcflags}" \
 			LDFLAGS="${LDFLAGS} -static -L${PREFIX_A}" LIBS="-lncurses" \
-			ac_cv_lib_ncursesw_initscr=no ac_cv_lib_ncurses_initscr=yes)
+			NCURSES_CFLAGS="-I${PREFIX_H} -I${PREFIX_H}/ncurses" \
+			NCURSES_LIBS="-lncurses")
 	fi
 
 	make -C "${PREFIX_PORT_WORKDIR}" CFLAGS="${xcflags}"
```

Everything below that line (`src/nano` assert, strip, `b_install … /bin`) is
unchanged. `depends="ncurses"`, `license`, `supports="phoenix>=3.3"` unchanged.

**Also delete** `sources/phoenix-rtos-ports/nano/nano-phoenix-shim.h` (git-tracked)
— see risk (v) in §6 for why keeping the `-include` would be actively harmful.

---

## 6. Deliverable (b): the patch set

### Drop

| patch | why it becomes unnecessary |
|---|---|
| `0001-nano-bool-init-not-null.patch` | `grep -n edit_refresh_needed nano-9.2/src/global.c` → **no match**. Upstream removed the variable; nano 9.x uses `<stdbool.h>` properly. Nothing to fix. |

The `p_prepare` **config.sub/config.guess donor copy** also goes (not a patch,
but the same class of 2010-era workaround): `nano-9.2/config.sub:1754` already
lists `phoenix*`. `config.guess` has no `phoenix` entry, which does not matter —
`--build=x86_64-pc-linux-gnu` is passed explicitly and `--host` is validated by
`config.sub`.

The `nano-phoenix-shim.h` force-include goes too: `P_tmpdir` is now
`libphoenix/include/stdio.h:50`.

### Write new

**`patches/0001-rename-gnulib-gettime-settime.patch`** — modelled directly on
coreutils' patch of the same name, trimmed to nano's (much smaller) call graph.
Five hunks total, all verified by exhaustive grep:

```diff
--- a/lib/timespec.h
+++ b/lib/timespec.h
@@ -91,8 +91,8 @@
 long int gettime_res (void);
 struct timespec current_timespec (void);
-void gettime (struct timespec *) _GL_ARG_NONNULL ((1));
-int settime (struct timespec const *) _GL_ARG_NONNULL ((1));
+void gl_gettime (struct timespec *) _GL_ARG_NONNULL ((1));
+int gl_settime (struct timespec const *) _GL_ARG_NONNULL ((1));
--- a/lib/gettime.c
+++ b/lib/gettime.c
@@ -28,1 +28,1 @@
 void
-gettime (struct timespec *ts)
+gl_gettime (struct timespec *ts)
@@ -48,1 +48,1 @@
   struct timespec ts;
-  gettime (&ts);
+  gl_gettime (&ts);
--- a/lib/utimens.c
+++ b/lib/utimens.c
@@ -160,1 +160,1 @@
   else if (timespec[0].tv_nsec == UTIME_NOW)
-    gettime (&timespec[0]);
+    gl_gettime (&timespec[0]);
@@ -165,1 +165,1 @@
   else if (timespec[1].tv_nsec == UTIME_NOW)
-    gettime (&timespec[1]);
+    gl_gettime (&timespec[1]);
```

Notes for whoever writes the real file: generate it with a real `diff -u` so the
`@@` line counts and context are exact (`b_port_apply_patches` applies with
`patch -d <srcdir> -p1`, `port.subr:129`). `settime` is **declaration-only** in
nano — there is no `lib/settime.c` (unlike coreutils) and no caller anywhere in
`lib/` or `src/`; renaming the declaration alone is sufficient and provably
complete (`grep -rn '\bsettime\b' lib/ src/ m4/` → `lib/timespec.h:94` only).
`current_timespec` and `gettime_res` are gnulib-only names and stay as they are.

**`patches/0002-port-gnulib-fseterr-phoenix.patch`** — one hunk, lifted from
coreutils patch `0002`'s fseterr section:

```diff
--- a/lib/fseterr.c
+++ b/lib/fseterr.c
@@ -32,7 +32,9 @@
   /* Most systems provide FILE as a struct and the necessary bitmask in
      <stdio.h>, because they need it for implementing getc() and putc() as
      fast macros.  */
-#if defined _IO_EOF_SEEN || defined _IO_ftrylockfile || __GNU_LIBRARY__ == 1
+#if defined __phoenix__               /* Phoenix-RTOS libphoenix */
+  fp->flags |= (1u << 3) /*F_ERROR*/;
+#elif defined _IO_EOF_SEEN || defined _IO_ftrylockfile || __GNU_LIBRARY__ == 1
   /* GNU libc, BeOS, Haiku, Linux libc5 */
   fp->_flags |= _IO_ERR_SEEN;
```

Documented **cheaper alternative** if one prefers zero patches here: put
`ac_cv_func___fseterr=yes` in `config.site`. That makes `GL_COND_OBJ_FSETERR`
false (`m4/gnulib-comp.m4:587`) so `fseterr.c` is never compiled, and
`fseterr.h` maps `fseterr` → `__fseterr`. It is safe **today** because the only
caller of `fseterr` in nano 9.2 is `lib/stdio-consolesafe.c`, which is
MSVC-only (`GL_COND_OBJ_STDIO_CONSOLESAFE` = `test $USES_MSVCRT = 1`,
`gnulib-comp.m4:1074`). The patch is still preferred: it is truthful, it is the
same hunk already carried by coreutils, and a future gnulib that calls
`fseterr` from portable code would turn the lie into an undefined-symbol link
error instead of just working.

**`config.site`** (new file, `nano/config.site`, wired via
`CONFIG_SITE=${PREFIX_PORT}/config.site`) — start from
`coreutils/config.site` and trim. Keep the generic cross answers that gnulib
cannot probe by link test:

```
# malloc(0)/realloc(0) return non-NULL on Phoenix (fixed in malloc_dl.c)
ac_cv_func_malloc_0_nonnull=yes
ac_cv_func_realloc_0_nonnull=yes
gl_cv_func_malloc_0_nonnull=1
gl_cv_func_realloc_0_nonnull=1
gl_cv_func_working_mkstemp=yes
ac_cv_func_working_mktime=yes
gl_cv_func_working_mktime=yes
```

Drop the coreutils-specific entries (`gl_cv_func_chown_*`, `ac_cv_func_chown_works`,
the `mntent` family, `statfs`/`fstatfs`/`sys_statfs.h`/`sys_vfs.h`, `chroot`,
`gethostbyname`/`getservbyname`, the `RLIMIT` bits) — nano needs none of them,
and asserting things a build does not exercise is how a config.site rots.

The remaining `ac_cv_func_*` entries in coreutils' file are mostly *link* tests,
which do work when cross-compiling, so they are redundant for nano; leave them
out and add entries **only** when the first build shows configure guessing
wrong. Expect this file to gain 1-3 lines during the first build iteration —
that is normal and is exactly what happened for coreutils.

---

## 7. Deliverable (c) recap: the exact collision workaround

1. **Patch gnulib's source, never `-D`.** `patches/0001` renames
   `gettime`→`gl_gettime` and `settime`→`gl_settime` in
   `lib/timespec.h` + `lib/gettime.c` + `lib/utimens.c`.
   A command-line `-Dgettime=gl_gettime` provably cannot work because it
   rewrites libphoenix's declaration too, reproducing the conflict under the new
   name. This is the same reasoning that produced coreutils'
   `0001-rename-gnulib-gettime-settime.patch`, and the same two symbols.
2. **Patch `lib/fseterr.c`** with a `#if defined __phoenix__` branch setting
   `F_ERROR` (`1u << 3`) on libphoenix's `FILE.flags` — the `fseterr` slice of
   coreutils' `0002-port-gnulib-stdio-internals-phoenix.patch`. (nano needs
   *only* fseterr; it does not bundle `freadahead`/`freading`/`freadptr`/
   `fpending`/`freadseek`, so the other five hunks of coreutils 0002 have no
   counterpart here.)
3. **Answer configure honestly via `config.site`**, not by lying broadly.

---

## 8. Deliverable (d): risks, ranked, and the build/test order

### Risks

**(i) `isatty()` — the one genuinely new runtime gate. HIGHEST.**
nano 9.2's NEWS line 2 is *"Nano refuses to start when standard output is not a
terminal."* Concretely:

- `src/nano.c:2161-2162` — `if (!isatty(STDOUT_FILENO)) die(_("Standard output is not a terminal\n"));`
- `src/nano.c:2626-2627` — `if (!isatty(STDIN_FILENO)) die(_("Standard input is not a terminal\n"));`

nano **2.2.6 has no isatty check at all** (`grep -n isatty nano-2.2.6/src/nano.c`
→ no match), so this code path has never been exercised by nano on Phoenix.
libphoenix implements `isatty` as *"did `tcgetattr` succeed"*
(`libphoenix/unistd/file.c:885-889`). Reasons for optimism: `mc`'s full
two-panel TUI renders on this console (2026-09-03), the standalone
`ncurses_smoke` passes `setupterm`/`initscr` and reads a true 67×240 geometry
via `TIOCGWINSZ` on `pl011-tty`, and interactive `bash` works — all of which
imply `tcgetattr` succeeds on the psh console. But it is untested for nano's
exact fds, and it is a *hard die*, not a degraded mode. **Watch for the literal
strings** `Standard output is not a terminal` / `Standard input is not a
terminal` in the first Pi run. If it fires, the fix is a libphoenix/tty-side
`tcgetattr` gap on that fd (or a psh redirection), not a nano patch — and
diagnosing it is cheap because the message is unambiguous.

**(ii) gnulib replacement churn. MEDIUM, self-announcing.**
Under `--enable-cross-guesses=conservative` (the default) gnulib will compile
its own `glob`, `regex`, `vasnprintf`, `mbrtowc`, `wcwidth`, `nl_langinfo`,
`canonicalize`, `lstat`/`fstatat`/`open`/`fcntl` replacements rather than trust
libphoenix. That is mostly *good* — for `glob` it is strictly better, because
`lib/glob.in.h:28` then skips `include_next <glob.h>` entirely and nano never
sees libphoenix's BSD `glob_t` / negative `GLOB_NOMATCH`. The risk is that one
replacement does not compile on Phoenix (coreutils hit exactly one:
`rpl_chown`). Failures are loud compile errors; each is answered with one
`config.site` line or one small hunk. Budget 1-3 build iterations.

**(iii) UTF-8 stays off. LOW — parity, not regression.**
`--disable-utf8` matches the current 2.2.6 recipe. Multibyte editing will remain
unavailable, as it is today. Enabling it needs a widec ncurses, which is banked
because `mc` and CPython `_curses` link the narrow `libncurses.a`.

**(iv) Netboot / rootfs staging drift. LOW but historically recurrent.**
See `[[project_netboot_export_drift]]`: a rebuilt `nano` must actually reach
`/bin` on whichever root is booted. The port `b_install`s to `/bin` and is
`if: true` in
`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml:127`,
and `tools/ports/build-nano.sh` no longer exists (the old dual-path trap is
gone) — so an SD/image build is clean. For a **netboot** test the binary has to
be copied into the hand-maintained NFS root explicitly, or you will validate the
old 2.2.6 binary and believe the upgrade works.

**(v) Keeping the shim would break the build. LOW because it is a known action.**
Do not leave `-include nano-phoenix-shim.h` in place "just in case". The shim
`#include <pwd.h>`, i.e. it pulls libphoenix headers *before* `config.h`, and
gnulib headers hard-fail on that: `lib/glob.in.h:36-38` is
`#if !_GL_CONFIG_H_INCLUDED / #error "Please include config.h first." / #endif`.
Delete the file with the recipe change.

**(vi) Ports-mirror seeding. LOW today, but check before the release build.**
`phoenix-rtos-build/port_manager/port.subr:42` is
`if [ "${PORTS_ALWAYS_USE_MIRROR}" = "1" ] || ! run_wget "${baseurl}${orig_filename}"; then run_wget "${PORTS_MIRROR_BASEURL}${filename}"`,
with `PORTS_MIRROR_BASEURL=https://files.phoesys.com/ports/`
(`port_internal.subr:45`). A brand-new `nano-9.2.tar.xz` will **not** be on that
mirror. Verified: `PORTS_ALWAYS_USE_MIRROR` defaults to `0`
(`port.subr:20`) and **nothing in this repo, `phoenix-rtos-project`,
`phoenix-rtos-build` or the Dockerfiles sets it to `1`** — so origin
(`nano-editor.org`, reliable) is tried first and the missing mirror copy is
harmless. Re-check that grep before the authoritative/clean-build release run,
and if a hermetic build ever forces the mirror, the tarball has to be seeded
there first, or the version flip passes `build-port.sh` locally and then fails
in the release build.

**(vii) Binary size. INFORMATIONAL.**
2.2.6 produced a ~500 KiB static `/bin/nano`. nano 9.2 + gnulib (regex, glob,
vasnprintf, and the unicode width/ctype tables) will be materially larger —
expect roughly 1-1.5 MiB stripped. Worth a glance against the image budget, but
`mc` at 2.4 MiB already ships, so this is not expected to bind.

### Build/test order

Builds are **deferred until the authoritative Docker build releases the
machine**. Then, in order:

1. `./scripts/build-port.sh nano` — the standalone port build-verify. Success
   criteria: exit 0; `src/nano` is a **static aarch64 ELF**; `readelf`/`nm`
   shows **0 undefined symbols**. Iterate on `config.site` / patches here — this
   loop needs no Pi and no image.
2. On the first failure, expect it in this order: (a) a `gettime`/`settime`
   conflicting-declaration error if patch 0001 is incomplete; (b) the
   `fseterr.c` `#error` if patch 0002 is missing; (c) a gnulib replacement that
   will not compile → one `config.site` line. Nothing else is predicted.
3. Confirm the tarball actually fetched from origin (`nano-editor.org`) — see risk (vi); nothing forces the ports mirror today, but re-check before the clean-build release run.
4. Confirm no host contamination: grep the generated `config.log`/`Makefile` for
   `/usr/include/ncurses` or a host `-L/usr/lib` from pkg-config. If present,
   the `NCURSES_CFLAGS`/`NCURSES_LIBS` preset did not take.
5. Full image build (`--with-ports`) and check `_fs/root/bin/nano` is the new
   binary (`strings … | grep 'GNU nano 9.2'`).
6. **One Pi cycle** via the `rpi4-run` skill. In psh: `nano /tmp/t` — the
   verdict is whether it clears the isatty gate at all. Then type a line, `^O`
   `Enter` to save, `^X` to exit, `cat /tmp/t` to confirm the content, and check
   the terminal is restored (no stuck raw mode). Grab an HDMI snapshot of the
   nano screen for the record. `TERM=vt100` if the console needs it (the ncurses
   port's compiled-in fallbacks cover `vt100/linux/ansi`).
7. Only then update the port comment block, `docs/` shipping lists, and record
   the integration state.

### Honest bottom line

The stated blocker really is obsolete, and the collision surface is smaller than
the old comment implies: **two symbols** (`gettime`, `settime`), already solved
by coreutils in exactly this way, plus **one** unrelated Phoenix gap
(`fseterr`). Nothing found in nano 9.2 requires a facility Phoenix genuinely
lacks — `futimens`, `glob`, `regex`, `getopt` and the POSIX printf family are
all supplied by bundled gnulib, and ncurses, `TIOCGWINSZ`, `SIGWINCH`, `fork`,
`P_tmpdir` and `getpwnam_r` are all present.

The two things that could still make this cost more than a day are honestly
uncertain: (1) nano 9.2's new hard `isatty(STDOUT)`/`isatty(STDIN)` refusal has
never been exercised by nano on the Phoenix console, and (2) the volume of
gnulib that gnulib's conservative cross-guessing will compile means there may be
a replacement or two that does not build on Phoenix. Both surface as loud,
specific errors in step 1 or step 6 — neither is a silent-corruption class of
risk — but neither can be ruled out from source reading alone.
