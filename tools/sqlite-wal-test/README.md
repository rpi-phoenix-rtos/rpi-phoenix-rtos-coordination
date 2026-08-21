# SQLite WAL on Phoenix-RTOS RPi4 — HW test + finding (§C4)

Closes the §C4 "SQLite WAL" deferred feature with a precise result.

## Result (2026-08-21, netboot HW, 0 faults)
**WAL works in EXCLUSIVE (single-process) locking mode; multi-process WAL is
unsupported.**

- With `PRAGMA locking_mode=EXCLUSIVE;` set BEFORE `PRAGMA journal_mode=WAL;`,
  SQLite uses a heap-memory wal-index (no `-shm` file / no mmap). Verified:
  write engaged WAL (`journal_mode=wal`), inserted 3 rows, `integrity_check=ok`,
  `-wal` file persisted to disk; a fresh `sqlite3` REOPEN (also exclusive) reloaded
  all 3 rows correctly (`integrity_check=ok`). Full write→close→reopen round-trip.
- A DEFAULT (non-exclusive) reopen of a WAL db FAILS with
  `SQLITE_PROTOCOL "locking protocol (15)"` — the default WAL path needs the
  shared-memory wal-index (VFS `xShmMap`/mmap), which the Phoenix SQLite VFS does
  not implement (consistent with Phoenix's no-file-mmap limitation). So WAL is
  single-process only.

## Usage on Phoenix
Always `PRAGMA locking_mode=EXCLUSIVE;` first when using WAL:
`sqlite3 -init walwrite.sql /path/db` (see walwrite.sql / walread.sql here).
For multi-process concurrency, use the default rollback journal (DELETE/TRUNCATE),
not WAL, until the VFS gains shared-memory support.
