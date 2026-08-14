# Lua on Phoenix-RTOS / Raspberry Pi 4

**Lua 5.4.7** — the `lua` interpreter and `luac` compiler — cross-compiled and
running on Phoenix-RTOS on the Pi 4. Lua is **MIT-licensed** (no GPL concern).

Lua is the cleanest possible port: pure C89, no autoconf, no external dependencies.
It cross-compiled on the **first try, zero libphoenix gaps** — even `-DLUA_USE_POSIX`
(popen, gmtime_r, …) links, so libphoenix provides those.

## Result (HW-verified 2026-08-14, netboot)

    /bin/lua /selfcheck.lua      ->  ALL-OK
    /bin/lua -e print(2^10)      ->  1024.0

`selfcheck.lua` is a self-validating feature suite (~30 asserts, prints `ALL-OK` or
errors out at the first mismatch — the same self-oracle shape as the SQLite/jq ports).
It exercises the Lua 5.4 language + stdlib on hardware:

- **integers vs floats** (`math.type`, integer wraparound at `math.maxinteger`),
  floor-division `//`, `%`, float `^`
- **bitwise operators** (`& | ~ << >>`, 5.4)
- **strings + patterns**: `string.format`, `gsub` (+ count), `match` with captures,
  byte-length vs `utf8.len`
- **tables**: `table.sort`, `table.concat`, closures/upvalues, `ipairs`/varargs
- **metatables**: `__index`, `__add`
- **coroutines** (`coroutine.wrap`/`yield`)
- **error handling** (`pcall`/`error`)
- **`string.pack`/`string.unpack`** (binary serialization)
- **`tonumber`** with base/hex, and `goto`/labels

## Build

    ./build.sh        # downloads Lua 5.4.7 (SHA-checked) + cross-compiles lua + luac

One set of gcc calls, no autoconf:

    aarch64-phoenix-gcc -O2 -static -DLUA_USE_POSIX -c <core+lib>.c
    aarch64-phoenix-ar rcs liblua.a *.o
    aarch64-phoenix-gcc -O2 -static lua.c  liblua.a -lm -o lua
    aarch64-phoenix-gcc -O2 -static luac.c liblua.a -lm -o luac

Left off (a static interpreter on a console with no line editing): dynamic `require`
of `.so` modules (`LUA_USE_DLOPEN`) and `readline`. The core language + all standard
libraries are present.

## Run (psh-safe)

    /bin/lua /script.lua          # run a script file (no shell quoting issues)
    /bin/lua -e print(2^10)       # one-liner: keep it a single token (psh splits on
                                  # spaces and `;`, and mangles quotes)

For anything non-trivial, put the code in a `.lua` file and run that — psh mangles
quotes/spaces on the command line (same gotcha as the jq/sqlite ports).
