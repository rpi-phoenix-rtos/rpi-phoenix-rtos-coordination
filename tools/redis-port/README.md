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

## Status — FULLY FUNCTIONAL (HW-verified 2026-08-14, netboot, 0 faults)

`redis-server` starts clean on the Pi (`Redis version=7.2.4, bits=64 … Ready to
accept connections tcp`, 0 faults), and a host client (`10.42.0.1`) drives it over
the netboot network (`10.42.0.12:6379`). Verified end-to-end across data types:

    PING => +PONG              SET/GET/APPEND/STRLEN => bar / barbaz / 6
    INCR ctr => 1,2            LPUSH/LLEN/LRANGE => [c,b,a]
    HSET/HGET/HGETALL => v2    SADD s x y z x / SCARD => 3 (dedup)
    EXPIRE foo 100 / TTL => 100   TYPE => list   EXISTS/DEL/DBSIZE   COMMAND COUNT => 241

So strings, integers, lists, hashes, sets, key-expiry, and key management all work
over real lwip TCP with the `ae_select` event loop. Uses the libphoenix `malloc(0)`
fix (jemalloc off) and the new `floorl`/`ceill`/`llroundl`.

Known cosmetic issue: log timestamps print garbage (`4294967295…`) — Phoenix
`time()`/`gmtime` quirk in the log formatter, not a functional problem. Persistence
(RDB/AOF, which fork()) is disabled by config and untested.
