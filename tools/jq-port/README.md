# jq on Phoenix-RTOS / Raspberry Pi 4

**jq 1.7.1** — the command-line JSON processor — cross-compiled and running on
Phoenix-RTOS on the Pi 4. jq is **MIT-licensed** (permissive; no GPL concern for
the Phoenix repos, unlike the bash/coreutils ports).

## Result (HW-verified 2026-08-14, netboot) — fully functional

**jq works** — parser, bytecode compiler, execution engine, the ~250 jq-defined
builtins, object/array construction, arithmetic, and the number formatter all
produce **correct** output on hardware:

    /bin/jq -n '{a:(1+2),b:[1,2,3]|add}'   ->  {"a":3,"b":6}
    /bin/jq -n -f /selfcheck.jq            ->  "ALL-OK"   (30 feature assertions)
    /bin/jq --run-tests /jqcore.test       ->  12 of 12 tests passed (0 malformed)

`selfcheck.jq` exercises map/select/group_by/reduce/foreach/recurse/to_entries/
sort_by/unique/split/join/paths/getpath/utf8-length/pow/sqrt/… — each result checked
against a hardcoded expected value, emitting `"ALL-OK"` or the mismatches. It matches
the native x86 build exactly.

## Root-caused + fixed: a libphoenix `malloc(0)` bug

Early testing hit `jq: error: cannot allocate memory` on some programs. Instrumenting
jq's allocator showed the failing call was **`calloc(0, 24)`** — jq allocating an
empty collection. libphoenix's `malloc(0)` returned **NULL**, and jq (like most
portable software, following the glibc/BSD convention) does `p = calloc(0,n);
if (!p) out_of_memory()` — so it mis-reported OOM. The apparent "intermittency" was
deterministic by code path: filters that never build a zero-length allocation
(`[1,2,3]|add`) always ran; those that do (`-n 42`, `selfcheck`) always failed.

Fixed in **libphoenix** (`stdlib/malloc_dl.c`: `malloc(0)` now returns a distinct,
freeable, non-NULL pointer — glibc/dlmalloc behavior). This benefits **any** port
that relies on the standard `malloc(0) != NULL` convention, not just jq. After the
fix + a `--scope core` rebuild + syncing `libphoenix.a` into the toolchain, all three
invocations above pass.

## Build

    ./build.sh        # downloads jq-1.7.1 release (SHA-checked) + cross-compiles

The jq **release** tarball ships the pre-generated `parser.c`/`lexer.c` (no
bison/flex needed). Approach (SQLite-style direct compile, no autoconf on-target):

- jq has **no** `config.h`; configure normally emits the `HAVE_*` feature macros as
  `-D` flags. `build.sh` bakes a curated, Phoenix-valid set.
- `builtin.inc` / `config_opts.inc` / `version.h` (BUILT_SOURCES) are generated in
  the script (`builtin.inc` is a one-line `sed` transform of `builtin.jq`).
- `-Wno-incompatible-pointer-types`: jq 1.7.1's cfunction dispatch table stores
  different-arity function pointers in one slot (arity is checked at runtime); GCC 14
  makes the resulting cast an error by default. Benign for jq.

### Dropped features (all obscure)

- **Regex builtins** (`test`/`match`/`sub`/`gsub`/`splits`, and regex `split`) — need
  oniguruma; compiled out (gated on `HAVE_LIBONIG`). Plain `split(",")` still works.
- **Obscure math builtins** libphoenix lacks: `tgamma lgamma drem exp10 gamma scalb
  significand lgamma_r remainder nexttoward nextafter logb log1p expm1 frexp ldexp
  acosh asinh atanh cbrt ilogb j0 j1 jn y0 y1 yn`. **Kept**: `sqrt floor ceil round
  trunc fabs exp exp2 log log2 log10 pow sin cos tan asin acos atan atan2 sinh cosh
  tanh hypot fmin fmax fma fmod copysign erf erfc rint nearbyint scalbn modf fdim`.

## Run (psh-safe)

Pass the filter via **`-f FILE`** — psh splits the command line on `;` (used in
jq's `reduce`/`range`/multi-arg builtins) and mangles some quoting, so an inline
filter with `;` breaks. `-f` keeps all argv tokens clean:

    /bin/jq -c -n -f /filter.jq            # filter from a file, null input
    /bin/jq -c -f /filter.jq /data.json    # filter file + input file

Inline filters **without** `;` are fine (`jq -n '[1,2,3]'`), since psh passes
`[](){},|` through unmangled.
