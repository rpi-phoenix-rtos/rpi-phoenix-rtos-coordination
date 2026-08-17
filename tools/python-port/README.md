# CPython on Phoenix-RTOS / Raspberry Pi 4

**CPython 3.14.4** cross-compiled to a **static `python3`** and **running on the
Pi 4** — a full Python 3 interpreter on Phoenix-RTOS. PSF-licensed (permissive).

## Result (HW-verified 2026-08-14, netboot)

    /bin/python3 -S -c print(6*7)     ->  42
    /bin/python3 -S /selftest.py      ->  PYVER 3.14.4 / ALL-OK

`selftest.py` asserts core language: `sum(range(100))`, list comprehensions,
`sorted`, Unicode `.upper()` (`héllo`→`HÉLLO`), `map`/`lambda`, generators,
exceptions, `os.getpid()` (builtin `posix`), classes, dict-merge — all pass.

**Static C extension modules also work** (`selftest2.py` → `MODULES-OK … ALL-OK`):
`array`, `struct` (binary pack/unpack), `json` (dumps/loads), `math` (incl.
`math.nextafter` via the new libm fix), `heapq`, `bisect`, `pickle` (round-trip),
`csv`, `random`, `statistics`, and **`socket`** (`AF_INET`/`SOCK_DGRAM` fd via lwip).
These are compiled **static into the interpreter** (Phoenix avoids runtime `.so`
loading) — see `Setup.local`. Also baked in: `select`, `mmap`, `fcntl`, `grp`,
`_posixsubprocess`, `_queue`, `_zoneinfo`, `unicodedata`.

**`sqlite3` works too** (`selftest_sqlite.py` → `sqlite_version 3.53.4` / `ALL-OK`):
`connect(:memory:)`, CREATE TABLE, parameterized `executemany`, commit, `ORDER BY`,
aggregates, `LIKE`, transaction rollback, fetchall/fetchone. `build.sh` cross-builds
`libsqlite3.a` from the SQLite amalgamation and links the static `_sqlite3` module
against it (Python + a real SQL database on Phoenix).

The interpreter starts (core init, reads `/dev/urandom` via `/dev/hwrng`), runs
frozen `importlib`, and imports pure-Python stdlib modules from disk.

(Startup prints a harmless `Could not find platform dependent libraries <exec_prefix>`
— there is no `lib-dynload` dir because the `.so` extension modules aren't built yet.)

## Dynamic C extensions (`.so`) work — HW-verified

Python can **`dlopen` a C extension module** on Phoenix (owner's "dynamic-linking used in
Python" goal). Using libphoenix's Phase-A dlopen (undefined symbols resolve against the
host binary's `.symtab`), an extension built with `build-extension.sh` loads and runs:

    import spam            # spam.cpython-314.so on sys.path
    spam.add(3, 4)  == 7   # HW-verified (DLOPEN-EXT-OK)

Recipe (`build-extension.sh` + `ext-example.c`): compile `-shared -fPIC -nostartfiles`,
leaving the Py C-API + libc **undefined** so they resolve against the (non-stripped)
`python3` binary at import time; name it `<mod>.cpython-314.so`. Constraints: the
python binary must stay **non-stripped** (keep `.symtab`); the extension must not link
libpython/libc (a 2nd copy corrupts state); no `__thread` (Phase-A has no dynamic TLS).

## Build + deploy

    ./build.sh        # downloads CPython 3.14.4, patches, cross-compiles `python`

Then stage the binary + the pure-Python stdlib at the compiled prefix (so startup
finds `encodings`), as `build.sh` prints:

    cp python     <nfsroot>/bin/python3
    cp -r Lib/*   <nfsroot>/usr/local/lib/python3.14/

## How it was ported (the interesting parts)

A genuinely multi-cycle port. The pieces:

1. **Teach configure about Phoenix** — two cross-build `MACHDEP` blocks hard-error
   on an unknown host; added `*-*-phoenix*` cases (`build.sh` patches them).
2. **libphoenix gaps fixed properly** (these benefit *all* ports, not just Python):
   - **C99 libm** `log1p/expm1/asinh/acosh/atanh` (configure's "requires C99 libm"
     gate) + `floorl/ceill/llroundl`.
   - **wide-char** `wcstol/wcstok/wcstoul/wcstod/wcsstr/wcsspn/wcscspn/wcspbrk/…`.
   - **`sysconf(_SC_CLK_TCK)`** now returns 100 (was `-1` → CPython's
     `_Py_GetTicksPerSecond` aborted startup with "cannot read ticks_per_second").
   - **`malloc(0)`** returns non-NULL (earlier fix, needed by the allocator paths).
3. **config.site** — ~149 `ac_cv_func_*=yes` overrides for functions Phoenix has but
   the cross func-check missed (fork/execv/sysconf/timegm/clock/gettimeofday/…),
   plus `py_cv_module_*=n/a` to drop external-lib modules.
4. **phoenix-py-compat.h** (`-include`) — small shims: early `sys/time.h`/
   `sys/resource.h`/`sys/mman.h` (complete `struct timeval`/`rusage`), missing
   `_SC_*` names, `clock_getres`/`msync` no-ops, `O_NOFOLLOW=0`, `SOMAXCONN=128`.
5. **`--without-mimalloc`** (needs `madvise`/rusage fields Phoenix lacks → pymalloc),
   **`--disable-shared`**, cross `LDSHARED`, and `make python` (static interpreter).

## Deferred

- **C extension modules as `.so`** (array, _socket, mmap, …) — not built; the
  interpreter has its builtin modules + the pure-Python stdlib. Re-enable by
  building them static into the binary (Modules/Setup) or shared once Phoenix's
  runtime `.so` import is validated.
- **External-lib modules** (zlib/_ssl/_hashlib/_ctypes/readline/…) — disabled;
  re-enable by cross-building the libs (e.g. libz for `zlib`, mbedtls/openssl for
  `_ssl`). `_sqlite3` is already wired (see above).
- **Runtime breadth** — only the self-test exercised so far; broader stdlib +
  `fork`/subprocess behavior on Phoenix is untested.
