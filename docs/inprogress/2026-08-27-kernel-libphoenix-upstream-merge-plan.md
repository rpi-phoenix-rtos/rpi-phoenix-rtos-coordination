# Kernel + libphoenix coordinated upstream merge — execution plan (2026-08-27)

Task-2 upstream pulls, the last two hard repos. **Coordinated** because libphoenix's
merged `pthread/pthread.c` calls new kernel syscalls at **compile time** — the two
merge together or libphoenix won't build.

## Risk: MEDIUM (subagent-analyzed) + one confirmed ABI hazard (below).

## Confirmed hazard: syscall table mid-insertion (binary-compat)

Upstream `include/syscalls.h` **inserts** `mutexConsistent` + `mutexPrioCeiling`
after `mutexUnlock` (mid-list) → shifts syscall numbers of everything below
(`phCondCreate`, all `sys_*`, …). `schedGet`/`schedSet` are appended at the end (fine).

The syscall list order == syscall number (kernel: `syscalls[] = { SYSCALLS(SYSCALLS_NAME) }`;
libphoenix aarch64 `syscalls.S`: `SYSCALLS(SYSCALLS_LIBC)` from the SHARED
`<phoenix/syscalls.h>` = the kernel header installed to the sysroot — single source).

Mid-insertion silently breaks every **prebuilt** binary in the hand-maintained netboot
NFS root `/srv/phoenix-rpi4-nfs/usr/bin` (games/X11/python — not rebuilt each cycle).

### Mitigation (Option A, chosen): append-only ordering
Post-kernel-merge, move `mutexConsistent`/`mutexPrioCeiling` to the **end** of the
`ID()` list (after `schedSet`), with a comment explaining WHY (preserve binary compat
for the prebuilt app base). Single-file kernel deviation; the shared header propagates
to libphoenix; all existing syscall numbers preserved. Handlers in `syscalls.c` are
name-referenced by the macro, so only the `ID()` list order matters.

## libphoenix conflict resolutions (subagent recipe)
1. `sys/select.c` → **take theirs** — upstream rewrite PRESERVES our `select(NULL)`-blocks
   fix (`n>0,to==NULL → poll(-1)`; `n==0,to==NULL → pause()`) and is more correct. The
   libnfs poll-stall fix is in the KERNEL, not here — untouched.
2. `include/sys/types.h` → **take theirs** — our `_ATOMIC` hack superseded; theirs adds
   `pthread_spinlock_t` that the auto-merged `pthread.c` references. No `_ATOMIC(` used elsewhere.
3. `include/unistd.h` → **union**: keep theirs' `_SC_SPIN_LOCKS 5` + `_POSIX_SPIN_LOCKS`;
   move our `_SC_LINE_MAX`/`_SC_NPROCESSORS_CONF`/`_SC_NPROCESSORS_ONLN` to 100+ (avoid
   re-collision); keep our `_POSIX_VERSION`/`_POSIX2_VERSION`/`getentropy()`.
4. `unistd/conf.c` → **union**: our `sysconf()` superset + add
   `case _SC_SPIN_LOCKS: return _POSIX_SPIN_LOCKS;` (value matches step 3).
5. `sys/Makefile` → **union**: `OBJS` keeps BOTH `interrupt.o` (theirs) + `statfs.o` (ours).

Auto-merged fixes verified preserved: semaphore lost-wakeup (e75c4fe), pthread_detach
guard, `string2mode`. No Pi4/ports file deleted (the D-list is all ours-only additions,
kept). Our `libm/` wins over upstream's `math/` reorg (acceptable — ours is newer/tested).

## Build + verify
- `--scope core` (stale-core hazard; committed core change). If gcc-16 toolchain: re-copy
  built `libphoenix.a` into `.toolchain` (known gotcha) — but default is gcc-14.
- Boot-verify: psh prompt + lwip + NFS takeover + 0 faults; ideally a pthread mutex/cond +
  select(NULL) smoke.
- Snapshot a manifest capturing BOTH sibling SHAs (paired rollback).
