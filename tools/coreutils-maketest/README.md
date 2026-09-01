# coreutils `make check` on Phoenix-RTOS (RPi4) — runner + root-cause findings

Owner task (2026-08-21): *build + run coreutils' own test suite on Phoenix; assess
psh limits; fix libphoenix gaps.* This dir holds the on-Pi driver + the diagnosis.

## How it's run (the automake harness needs make+perl; we don't use it)

The gnulib test scripts (`tests/<tool>/<t>.sh`) are self-contained: each sources
`$srcdir/tests/init.sh`, gates on `print_ver_ <tool>` (→ `require_built_` against
`$built_programs`), makes a scratch dir, and `Exit`s 0=pass / 77=skip / 99=framework.
We run them directly under the ported **bash**, launched from psh as a single command
(psh has no `|`/`;`/redirection):

    bash /root/ct/run.sh

`run.sh` sets `PATH=/usr/bin:/bin` (the image's coreutils live in `/usr/bin`),
`built_programs`, and an **absolute** `srcdir`, runs each test with CWD on tmpfs
(`/tmp`), and writes results to files on the NFS export so the host reads them in
full regardless of UART truncation:

- `/root/ct/out.txt` — one `CTEST <name> PASS|FAIL|SKIP|ERROR` line + `CTSUMMARY`
- `/root/ct/logs/<t>.log` — full stdout/stderr of each test

Stage: copy `tests/` + `init.cfg` from the coreutils-9.5 tarball to
`/srv/<nfs-export>/root/ct/cu/`, and `run.sh` to `/srv/<nfs-export>/root/ct/`.
Harness deps confirmed present on the image: `diff cmp getlimits sed grep awk`
(perl + gawk are absent → skip `.pl` tests).

## Result: the harness WORKS on Phoenix, and it exposed one dominant bug

Tests execute correctly (arg-validation negatives like `sleep`/`nl` overflow behave
right; `sleep`/`false-status` PASS). But nearly every test that **creates files**
failed at cleanup with:

    chmod: cannot access '<tmp>/<file>': No such file or directory
    rm: cannot remove '<tmp>': Directory not empty

### Root cause: `fchdir()` is a no-op stub → `rm -r` (and all fts/openat consumers) broken

Isolated with `fsdiag.sh` / `fsdiag2.sh` (reproduces on **both tmpfs and NFS**):

- Not ENOSPC (357 GB free; writes succeed), not a readdir/lookup inconsistency
  (`ls` + `stat` + `chmod -R` on plain files all rc=0).
- Single-file ops work: `rm f`, `unlink f`, `rmdir d`, and a manual
  `rm a; rm b; rmdir d` loop all succeed.
- **`rm -r d` fails**: `rm: cannot remove 'd/g1': No such file or directory` (ENOENT)
  for every entry, though the entries exist.

coreutils `rm -r` uses **fts**, which removes entries with `unlinkat(dirfd, name, 0)`
relative to an open directory fd. libphoenix has **no `*at` family**, so coreutils'
gnulib emulates `unlinkat` via `save_cwd()` → **`fchdir(dirfd)`** → `unlink(name)`.
But `sources/libphoenix/posix/stubs.c` has:

    int fchdir(int fildes) { return 0; }   /* reports success, never chdir's */

So `fchdir(dirfd)` no-ops, and `unlink("g1")` runs in the *unchanged* cwd (`/tmp`),
where `g1` doesn't exist → ENOENT. Phoenix's cwd is a **userspace path string**
(`unistd/dir.c` `dir_common.cwd`); there is no kernel cwd and no fd→path reverse
map, so the stub couldn't chdir even if it wanted to. (Today's behaviour is
fail-safe by luck — the wrong-cwd names don't exist, so nothing wrong is deleted.)

### Fix — DONE + HW-VERIFIED (2026-09-01): real `fchdir()` via a kernel fd→path record

`rm -r` (and all fts/openat consumers) is fixed by making `fchdir()` actually work,
so gnulib's already-compiled fchdir emulation of `openat`/`unlinkat`/`fstatat`
composes correctly — no coreutils port surgery, and every gnulib-based port heals.

- **Kernel** (`posix.c`/`syscalls.c`, `sys_fdpath`): store the canonical path in the
  refcounted `open_file_t` at `sys_open` (shared across `dup()` by construction — the
  exact property a userspace fd→path table can't guarantee), and add an append-only
  `sys_fdpath(fd, buf, size)` syscall to read it back. Freed in `posix_fileDeref`.
- **libphoenix** (`unistd/dir.c`): `fchdir(fd)` = `sys_fdpath` → `chdir(path)`. Composes
  with `resolve_path()`/`getcwd()`; a regular-file fd yields its path and `chdir` then
  fails `ENOTDIR` (never a false success); a path-less fd (socket/pipe) yields `ENOENT`.
- Earlier gnulib-only routes were dead ends (recorded so they aren't re-tried): a
  plain-`fchdir` from `ac_cv_func_fchdir=no` **multiply-defines** vs libphoenix, and the
  `REPLACE_FCHDIR=1` route swaps `DIR`→`struct gl_directory` and conflicts with the
  system DIR in fdopendir/getcwd.

**HW-verified:** `test-libc-misc -g unistd_fsdir` `fchdir` PASS (dir fd moves cwd; file
fd fails, cwd intact), 16/16 OK, boot healthy; `fsdiag2` `rm -r` now `rc=0` (was rc=1
"Directory not empty"); the previously-failing `nl.sh` flips to **PASS**.

**Residual:** `printenv.sh` still fails, but *differently* — the old batch-wide
`chmod: cannot access` cascade is gone; it now leaves a single
`rm: … Directory not empty` (a deeper cleanup-cwd edge, likely gnulib's fd-based
cwd-restore in `remove_tmp_`), under investigation. The `*at` family is no longer
required for `rm -r` but remains a nice future addition (thin path-based wrappers on
the same `sys_fdpath` record).

**Build footgun hit:** editing the kernel `syscalls.h` did **not** rebuild libphoenix's
`arch/*/syscalls.S` object (make missed the installed-header dep) → `undefined
reference to sys_fdpath`. Fix: `touch` the `.S` (or clean libphoenix) after a kernel
syscall-list change.

## Other findings (psh/environment limits, lower priority)

- `init.sh` line 106 `$stderr_fileno_: Bad file descriptor` — a bash fd-redirection
  limitation on Phoenix (surfaces in some tests' setup).
- perl/gawk absent → `.pl` tests and gawk-specific tests are inherently SKIP.
