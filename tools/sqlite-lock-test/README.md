# sqlite-lock-test — multi-process SQLite locking on Phoenix RPi4

Validates that the kernel's POSIX fcntl record locks (F_GETLK/F_SETLK/F_SETLKW,
implemented 2026-09-01, kernel `3844d204` + libphoenix `ac3baed`) make
**multi-process SQLite** correct. SQLite's default unix VFS locks via
`fcntl(F_RDLCK/F_WRLCK)`; under the previous no-op `EOK` stub those locks were
fake, so concurrent writers could silently corrupt the database.

Two phases, built + staged by `build.sh` into the netboot NFS export:

## Phase 0 — `sqlite-crossopen` (raw fcntl, no SQLite)

`fs/test_fcntl` already proves the lock table, but only with fds shared across
fork()/dup() — the same `open_file_t`/oid by construction. Phase 0 covers what
that could not: two **independent `open()`** calls of the same path must resolve
to the **same kernel oid** (over an NFS root this depends on nfs-fs lookup
stability). Parent takes a whole-file WRLCK, forks; the child opens the path
itself and checks F_GETLK reports the parent's pid and a conflicting F_SETLK is
refused with EAGAIN.

## Phase 1 — `sqlite-lockhammer` (SQLite, links libsqlite3.a)

Two processes hammer one database. SQLite footguns avoided: fork() **before**
`sqlite3_open()`; `journal_mode=DELETE` (rollback journal — WAL needs the
`xShmMap` shared-memory index we lack); `busy_timeout(0)` + a manual BUSY-retry
**counter**. Verdict = row count == expected ∧ `integrity_check == ok` ∧
**BUSY retries > 0** (the discriminator: zero contention proves nothing, since
the old stub could never surface BUSY from locking).

## Build / run

```
./tools/sqlite-lock-test/build.sh                 # needs a prior --with-ports build (libsqlite3.a + port sources)
# netboot, card OUT:
./scripts/test-cycle-psh-interact.sh --label sqlite-lock --inter-cmd-secs 8 --idle-secs 45 -- \
  "cd /tmp" \
  "/bin/sqlite-crossopen /tmp/xopen.lk" \
  "/bin/sqlite-lockhammer /tmp/hammer.db" \
  "/usr/bin/sqlite3 --version"
```

## Result (2026-09-01, HW-verified over netboot)

- Phase 0: **PASS** — F_GETLK across independent opens reports the owner pid;
  conflicting F_SETLK refused (EAGAIN).
- Phase 1: **PASS** — 100/100 rows, `integrity_check == ok`, BUSY retries = 421
  (contention engaged the lock table).
- Fresh `sqlite3` shell (3.53.4, new libphoenix wrapper) runs; restaged to
  `/usr/bin/sqlite3` (closes the stale-binary residual from the fcntl turn).

`build.sh` also relinks the shell against the current libphoenix so it carries
the fixed 64-bit `fcntl()` wrapper (the previously staged shell predated it).
