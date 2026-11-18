# Upstream sync 2026-11-17/18: phoenix-rtos v3.4.0 into the Pi 4 forks

**Status: ✅ ADOPTED 2026-11-18 04:45.** All 14 forks were merged on branches and fast-forwarded. The image was built
full-clean with a rebuilt toolchain, the Pi gate passed, and every master was pushed to `publish`. Manifest
[`2026-11-18-upstream-sync-v3.4.0-adopted`](../../manifests/2026-11-18-upstream-sync-v3.4.0-adopted.md); rollback
point [`2026-11-17-pre-upstream-sync`](../../manifests/2026-11-17-pre-upstream-sync.md), plus
`artifacts/rollback-pre-sync-20261117/` (loader, toolchain, NFS export).

Procedure as in [2026-10-08](2026-10-08-upstream-sync.md): census with `git merge-tree`, merges on branches in
worktrees, compile checks of every touched file with the real flags (`-Werror`), host harnesses, then the adoption
build and the Pi gate.

## Incoming (upstream commits not yet in the fork)

libphoenix 43, project 43, devices 40, lwip 30, kernel 27, tests 27, build 9, plo 8, ports 6, utils 4,
filesystems 3, corelibs 1, usb 1, doc 86. The hostutils and posixsrv forks were already up to date.

## ABI breaks (why full-clean plus a toolchain rebuild)

- **Syscall renumber.** `signalPending` and `signalWait` were inserted mid-table. Our table is still upstream's
  order with the fork's 8 syscalls appended (`sys_fdpath`, `memExport`, `memUnexport`, `pollNotify`,
  `sigreturnContext`, `sys_sigaltstack`, `futexWait`, `futexWake`).
- **`struct utsname`** grew (release 16→32, version 32→64).
- **Moved to the kernel headers:** `timeval`, `linger`, `useconds_t`, `suseconds_t`.
- **POSIX priorities** (libphoenix `b9175d5`): `posix = 31 − native`, valid range 0..63, native default 4 = POSIX 27.
  An audit found no POSIX scheduling call in the core repos; lua `-p` was adapted.
- **CPU time accounting is now opt-in** (`CPUTIME_SPLIT`). It is enabled in the rpi4b `build.project`; without it,
  system time reads 0.
- **The kernel version comes from the build system** (`OS-VERSION` in the project repo, `makes/version.mk`). The
  banner now reads `v3.5.0-dev`. Upstream's toolchain script reads `$PREFIX_PROJECT/OS-VERSION`, which is our
  flat `sources/`, so `sources/OS-VERSION` is a symlink to `phoenix-rtos-project/OS-VERSION` (git-ignored).

## Merge results (merge commit, then follow-ups)

| repo | commits | notes |
|---|---|---|
| kernel | `9000a41e`, `d6fba79a` | 9 conflicts. AF_UNIX: upstream's getsockname/getpeername, source address, MSG_TRUNC/CTRUNC and SO_PROTOCOL are taken; ours kept: growing ring, fd packs, atomic writes, poll wake-ups, stream mode. `UCHANNEL_OP_ATOMIC` moved to bit 3. `uchannel_returnPacks()` was silently deleted by the auto-merge and is restored. The write-offset guard is kept: upstream `a704b255` never advances the offset twice, and the guard covers servers that report no offset |
| libphoenix | `a8f454a`, `5451204`, `203a347` | 10 conflicts. **The auto-merge disabled every TSD destructor**: upstream reused `ctx->exiting`. Fixed in the merge and proven by the pthread host harness. scanf: upstream's rewrite plus our 3 glibc-parity fixes. Upstream's duplicate `scandir()` was dropped |
| tests | `c4d4f6f`, `62809a4` | 4 conflicts. Adds a far-future semaphore deadline test |
| filesystems | `7e6b5ba` | dummyfs unlinked-directory check; ext2 sparse write identical on both sides. libext2 harness ALL GREEN |
| lwip | `a09f9c2` | upstream's `ephy_configure()`, with our BCM54213PE settings kept |
| ports | `71b4f1f`, `e8590d2` | Relative-path recipes. **jq 1.8.2 reverted to our 1.7.1** direct build: upstream's recipe installs the bundled oniguruma into the host's `/usr/local/lib`, which fails without root (to do: move 1.8.2 to our scheme) |
| project | `38e635c` | Gitlinks kept at the fork's commits; `CPUTIME_SPLIT=y` |
| build, corelibs, devices, doc, usb, utils, plo | clean | — |

## Adoption

- **Toolchain** (gcc 16.2.0, 13 min): `bits/c++config.h` is **identical** to the old one, and its libphoenix has
  the new syscalls. The old toolchain is kept as `.toolchain.pre-sync-20261117`.
- **Build:** `--scope full-clean --with-tests --with-ports --with-showcase`. It stopped at jq (above) and resumed
  with `--scope core`; the image was built 03:26. Stale census on the export: 0 of 414 stale (one hand-built probe,
  `hwconc`, was moved to `/root/quarantine-pre-sync-20261117/`).
- **Host harnesses on the landed tree:** libc (string, wchar, num, fmt, time, scanf `known=35`, was 87): all
  clean. libext2: ALL GREEN.

## Pi gate (baseline on the pre-sync image vs after the sync, same commands)

| | baseline `base1118` | after `sync1118` |
|---|---|---|
| showcase | 7/7, rc 0, 0 faults, torches present | **7/7**, rc 0, 0 faults, torches present (HDMI checked: STK, Q3, Q2, XFCE) |
| libc, 10 programs | 0 failures | **0 failures** (unix-socket 56→72 and misc 270→237 tests: upstream added and moved tests) |
| extra checks | — | 15 more test programs 0 failures, including `test_cputime` (7/7: user/system split works), `test-rqtimeout`, `test-objcache`, `test-frag`, `test-sys-threadinfo`, pthread lifetime/fork/tsd-alloc and posixsrv. `test_priority` returns its prompt (silent by design). `uname` reads `3.5.0-dev`. Append on `/tmp` and on the NFS root: `a b`; 4 MiB + 1 MiB `>>` = 5242880 bytes. jq works. |

The only fault-pattern lines in either run are `test-libc-signal`'s deliberately provoked SIGSEGVs.

## Not done from the 10-08 gate list

The USB `dd` + `cmp` check, the SSH/lighttpd check and the browser smoke test were not run. Do them in the next
session that has the bench for longer.
