# jq on Phoenix-RTOS / Raspberry Pi 4

**jq 1.7.1** — the command-line JSON processor — cross-compiled and running on
Phoenix-RTOS on the Pi 4. jq is **MIT-licensed** (permissive; no GPL concern for
the Phoenix repos, unlike the bash/coreutils ports).

## Result (HW-verified 2026-08-14, netboot)

**Core jq works** — the parser, bytecode compiler, execution engine, the
~250 jq-defined builtins, object/array construction, arithmetic, and the number
formatter all produce **correct** output on hardware:

    /bin/jq -n '{a:(1+2),b:[1,2,3]|add}'   ->  {"a":3,"b":6}
    /bin/jq -n -f /tiny.jq   ([1,2,3]|add)  ->  6
    /bin/jq -n -f /med.jq    (reduce .[] as $x (0;.+$x)) ->  15

The reference oracle (`selfcheck.jq`, 30 feature assertions — map/select/group_by/
reduce/foreach/recurse/to_entries/sort_by/unique/split/join/paths/getpath/utf8-length/
pow/sqrt/…) returns `"ALL-OK"` on the native x86 build (same source, same config).

## Known limitation — intermittent `cannot allocate memory` (unresolved)

Under **netboot**, some invocations abort with `jq: error: cannot allocate memory`
even though jq's own `malloc` wrapper is the thing returning NULL:

- Small filters read from a file (`-f /tiny.jq`, `-f /med.jq`) run reliably.
- Larger programs (the 30-assertion `selfcheck.jq`, jq's `--run-tests` harness)
  fail **consistently**.
- A bare inline program (`-n 42`) failed **intermittently** (works in one boot,
  ENOMEM in another).

Characterization (honest scope):

- It is **not** a simple process-heap cap: SQLite (file DBs), bash, and the Quake
  ports all allocate far more than jq without ENOMEM.
- It is **not** decNumber (`USE_DECNUM`): rebuilding without it (numbers → plain
  doubles) reproduces the failure identically.
- Every jq run compiles the entire `builtin.jq` (~250 functions) at startup — a
  transient burst of many tiny `jv` allocations. The failure scales with total
  allocation volume and is state-dependent, which is most consistent with **heap
  fragmentation / a robustness issue in the jq × libphoenix-malloc interaction
  under a many-small-object workload**, possibly aggravated by memory pressure from
  the netboot lwip + nfs-fs + RAM-root stack.

**Recommended follow-up** (not autonomously testable — needs physical SD-card
handling): re-run under **SD boot**, where the netboot network stack is absent and
more RAM is free. If jq runs reliably there, the ENOMEM is netboot memory pressure,
not a jq/allocator bug. Otherwise the next step is instrumenting libphoenix `malloc`
for fragmentation under jq's alloc/free trace.

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
