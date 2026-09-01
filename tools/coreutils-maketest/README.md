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

### Fixes — status

1. **libphoenix hygiene — DONE (committed):** the stub now returns `-ENOSYS`
   instead of a false `0` (a libc must never report success for work it didn't do),
   turning the silent wrong-cwd corruption into a loud failure. Pinned by a contract
   test (`test-libc-misc -g unistd_fsdir`, case `fchdir`): fchdir on a non-directory
   fd must fail and leave cwd unchanged.
2. **coreutils `rm -r` — root-caused, NOT yet fixed (the gnulib route cascades).**
   Both gnulib angles hit walls: `ac_cv_func_fchdir=no` makes gnulib emit a *plain*
   `fchdir` that **multiply-defines** against libphoenix's symbol; forcing
   `REPLACE_FCHDIR=1` via `DIR_HAS_FD_MEMBER=0` (a configure patch — Phoenix's
   `opendir()` leaves `DIR.fd=-1` so `dirfd()` is genuinely unreliable) emits
   `rpl_fchdir` with no clash **but also swaps `DIR` for gnulib's `struct
   gl_directory`**, which then conflicts with the system `DIR` in `lib/fdopendir.c`
   / `lib/getcwd.c`. Making that consistent needs the whole dirent family
   (opendir/fdopendir/readdir/closedir/dirfd) gnulib-replaced — a real multi-issue
   gnulib port, out of scope for a quick toggle. Reverted; port builds as before.
3. **The proper fix (flagged, dedicated): a real `*at` family in libphoenix.**
   Implement `openat`/`unlinkat`/`fstatat`/… so gnulib uses them directly
   (`HAVE_UNLINKAT=1`) and never needs the fchdir/cwd dance. Seed: `rmdir()` already
   unlinks via `mtUnlink` to the parent dir's **oid** + leaf name, so the fs servers
   support oid-relative ops; `openat` additionally needs a kernel path to mint an fd
   from a (dir-oid, name).

## Other findings (psh/environment limits, lower priority)

- `init.sh` line 106 `$stderr_fileno_: Bad file descriptor` — a bash fd-redirection
  limitation on Phoenix (surfaces in some tests' setup).
- perl/gawk absent → `.pl` tests and gawk-specific tests are inherently SKIP.
