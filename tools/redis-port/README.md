# Redis on Phoenix-RTOS / Raspberry Pi 4

**Redis 7.2.4** (`redis-server` + `redis-cli`) cross-compiled for Phoenix-RTOS on
the Pi 4 — a real in-memory data-store *service*, exercising Phoenix's lwip TCP
sockets and the `ae_select` event loop.

Redis **7.2.x is BSD-3-Clause** (the 7.4 SSPL/RSALv2 relicense is later) — no GPL
concern for the Phoenix repos.

## Build

    ./build.sh    # downloads Redis 7.2.4 (SHA-checked), patches, cross-compiles

`MALLOC=libc` (skip jemalloc — hard to cross-compile; relies on the standard
`malloc(0) != NULL`, see the libphoenix malloc(0) fix). The event loop uses
`ae_select` (Phoenix is neither Linux nor BSD). Two adaptations, both kept out of
the Redis tree so the port is a recipe not a fork:

1. **`phoenix-compat.h`** (`-include`): a few errno constants Phoenix lacks
   (`ESOCKTNOSUPPORT`, `ECANCELED`), a `pthread_setcanceltype` no-op, and
   `setitimer`/`itimerval` + `dladdr`/`Dl_info` + `SI_USER` stubs. **Every one of
   these feeds only Redis's crash-report / watchdog diagnostics — not core
   operation.** The endianness macros + `AF_LOCAL=AF_UNIX` are passed as `-D`.
2. **`src/Makefile` link flags**: Redis keys the link off the *build host's*
   `uname` (Linux) and adds `-rdynamic -ldl -pthread -lrt` — none valid/needed on
   Phoenix (pthread/dl/rt/clock live in libphoenix; the toolchain has no
   `-rdynamic`). `build.sh` strips them.

Also required (added alongside): libphoenix **`floorl`/`ceill`/`llroundl`** (128-bit
long double) — libmcs ships no `mathl`, so those C99 variants were undefined
(`llroundl` in hyperloglog.c, `ceill` in timeout.c).

## Run (foreground, no persistence)

    /bin/redis-server /redis-min.conf

`redis-min.conf`: `daemonize no`, `save ""`, `appendonly no` — so the fork()-based
BGSAVE/AOF paths are never hit — plus `protected-mode no` + `bind 0.0.0.0` so a
host client can verify over the netboot network (`redis-cli -h <pi-ip>` or raw RESP:
`PING` -> `+PONG`, `SET k v` -> `+OK`, `GET k` -> `v`).

## Status

Build: **complete** — `redis-server` links into a static aarch64 Phoenix ELF.
Runtime bring-up in progress (see the coordination board).
