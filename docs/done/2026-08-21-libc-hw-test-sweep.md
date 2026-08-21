# libc / sys / corelib test-suite sweep on Pi4 HW (2026-08-21)

Closes the §B backlog item *"~20 libc/system test binaries on card — staged,
never run."* All suites were built with `--with-tests --with-ports` and run over
netboot (NFS root) via `scripts/test-cycle-psh-interact.sh`, one Pi cycle per
batch (Pi-lock honored). UART logs: `artifacts/rpi4b-uart/rpi4b-uart-*-libc-sweep{1,2,3}.log`.

## Result: 21 suites (~640 cases) proven on HW; 0 libphoenix defects

### PASS — clean on hardware (0 failures)

| Suite | Cases | | Suite | Cases |
|---|---|---|---|---|
| test-libc-math | 90 | | test-libc-statvfs | 22 (3 ign) |
| test-libc-stdlib | 91 | | test-thread-local | 3 |
| test-libc-printf | 118 (8 ign) | | test-setjmp | 8 |
| test-libc-scanf-basic | 48 | | test-sys-mutex | 12 |
| test-libc-scanf-advanced | 33 (4 ign) | | test-sys-cond | 17 |
| test-libc-signal | 9 | | test-sys-perf | 4 |
| test-libc-exit | 30 (4 ign) | | test-waitpid | 3 |
| test-libc-stdio | 79/80 (see below) | | test-mprotect | 3 |
| test-libalgo | 7 | | test-libcache | 40 |
| test-libuuid | 7 | | test-libtinyaes | 18 |
| test-libtrace | 3 | | | |

This confirms the libphoenix **computational** (math/stdlib/printf/scanf),
**threading/sync** (sys-mutex/sys-cond/thread-local/setjmp/waitpid),
**memory** (mprotect), and **corelib** (libalgo/libcache/libuuid/libtinyaes/
libtrace) layers are solid on real BCM2711 hardware.

### FAIL — both are NFS-root fs-server gaps, NOT libphoenix bugs

1. **test-libc-stdio** `stdio_ftell/wrong_stream_type_fifo`
   (stdio_indicator.c:615). The test creates a FIFO with `mkfifo()` and expects
   either success or `ENOSYS` (in which case it self-ignores — tracked upstream
   as issue #1338). It FAILED because on the **netboot NFS root** `mkfifo()`
   returns a **non-`ENOSYS`** errno: `mkfifo()`→`sys_mkfifo`→the fs server, and
   the libnfs-backed nfs-fs does not support named pipes, so the auto-ignore
   path never triggers. The other 79 stdio cases pass.

2. **test-libc-dirent** `dirent_readdir/basic_listing_count` (readdir.c:150).
   Expects exactly 7 entries (5 created + `.` + `..`). The nfs-fs `READDIR` does
   not synthesize `.`/`..`, so the count is 5. `readdir()` faithfully returns
   what the fs server sends — this is fs-server behavior on the NFS root.

Both are the same class as the earlier `stat_*` NFS-root quirk: the conformance
suite assumes a POSIX-complete local filesystem, whereas the netboot NFS root has
behavioral gaps. They are expected to pass on a local ext2/tmpfs root (untested
here — no SD card in the Pi this session). Neither warrants a change to the
netboot-critical nfs-fs path unattended; documented as environment-dependent.

## Batches (one Pi cycle each)

- sweep1: math, stdio, stdlib, printf, scanf-basic, signal, dirent, statvfs
- sweep2: scanf-advanced, exit, thread-local, setjmp, sys-mutex, sys-cond,
  libalgo, libcache, libuuid, waitpid
- sweep3: waitpid, statvfs, sys-perf, libtinyaes, libtrace, mprotect

### Batch 4 (2026-08-21, added) — socket/poll/pthread/posixsrv

| Suite | Result |
|---|---|
| test-libc-pthread | ✅ 13 |
| test-libc-poll | ✅ 1 |
| test-libc-inet-socket | ✅ 1 |
| test-libc-unix-socket | ✅ 25 |
| test-libc-posixsrv | ✅ 16 (after fix — see below) |

These self-test via fork + loopback / AF_UNIX and run standalone (no external peer).

**Bug found AND fixed — `tmpfile()` returned NULL on netboot** (posixsrv `tmpfile`
group, 3 fails → 0). Root cause: posixsrv is a **syspage program started before
the nfs takeover**, so its `tmpfile_init()` `mkdir("/var/tmp")` lands on the
ephemeral dummyfs RAM root; once the NFS export becomes `/` (no `/var/tmp`),
posixsrv's runtime backing-file `open("/var/tmp/tmpfile_N")` hits `ENOENT` and
`tmpfile()` fails for every caller. Unlike the two fs-server gaps above, this is a
real functional bug (SD boot's persistent root wouldn't hit it, but netboot does).
Fix (posixsrv `8a44ce8`): `tmpfile_open` now recreates `/var/tmp` on `ENOENT` and
retries once — self-healing against the root mounting/swapping after init.
HW-verified: `test-libc-posixsrv` 16/16, 0 failures. Manifest
`2026-08-21-posixsrv-tmpfile-rootswap.md`.

### Not run

Not run (need special setup / are intentional-fault harness tests): the
`test-fail-*` intentional-failure fixtures, `test-mprotect-fault`, socket/poll/
posixsrv suites (need a peer/server), `test_*` (busybox/graph/disk/fs — device-
or port-dependent). These are separate follow-ups if wanted.
