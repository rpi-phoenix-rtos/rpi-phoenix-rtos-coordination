# Kernel heap free-list corruption — work order

Caught once on hardware, fully characterised, **not fixed**. Recording it while
the evidence is fresh, because the class (kernel heap corruption) is far more
serious than the trigger that exposed it.

## What was seen

`test-libc-misc` on an NFS root, immediately after the symlink group
(`symloop_max`, `//link1`) and during `stat_nlink_size_blk_tim`:

```
Exception #37: Data Abort (EL1)
 x0=74735f747365742f   far=74735f747365742f   pc=ffffffffc000df2c  lr=ffffffffc000e170
 x7=00316b6e696c2f2f
```

Resolved against the kernel ELF:

- `pc` → `_vm_zalloc`, `vm/zone.c:98`
- `lr` → `_kmalloc_alloc`, `vm/kmalloc.c:64`

Line 98 is the free-list walk:

```c
block = zone->first;
zone->first = *((void **)(zone->first));   /* faults here */
```

`x0` and `far` are `0x74735f747365742f` = ASCII **`/test_st`**, and `x7` is
`//link1`. So `zone->first` pointed at a block whose first eight bytes are a
**path string**, and dereferencing it faulted.

## What that means

The zone allocator keeps its next-pointer *inside* the free block. A free block
therefore holds a path string exactly when either:

1. something wrote into the block after it was freed (use-after-free), or
2. a block was put on the free list without its link being written — which is
   what a **double free**, or a `vm_kfree()` of an interior/mis-computed
   pointer, produces.

Either way the corruption is committed long before the crash: the allocator only
falls over at the *next* allocation that happens to reach that block. The
reported stack is therefore the victim, not the culprit, and instrumenting
`_vm_zalloc` will not find it.

## Where to look

Path strings of exactly this shape are copied into kernel memory by the posix
layer. `posix.c` has ~38 `vm_kmalloc`/`vm_kfree` sites; the per-fd canonical
path (`f->path`, allocated in `posix_open`, freed in the refcounted close) is the
one that holds strings like these, though inspection did not find a fault in it:
the refcount is taken before the read and the free happens once at `refs == 0`.

Worth checking first:

- every `vm_kfree` in `posix.c` that could run twice on one pointer, or on a
  pointer that is not the start of its allocation;
- the error paths in `posix_open` — one of them frees `f` without freeing
  `f->path` (a leak, not this bug, but it shows the paths are not symmetric);
- anything freeing a buffer that a message send still references.

## How to reproduce — CORRECTED

The first sighting followed removing the redundant `nfs_chmod` from
`nfs_ops_create`, and I wrongly attributed it to that. **It reproduces in the
shipped configuration too** (chmod restored, poisoning off), with a
byte-identical signature: same `pc`, same `far` = `/test_st`, same `x7` =
`//link1`, same `x16/x17` = `aaaaaaaa`, and the same preceding tests
(`symloop_max`, `fifo_type`, `sock_type` pass, `chr_type` ignored,
`stat_nlink_size_blk_tim/nlink` passes, then the fault during `tim`).

So: `/bin/test-libc-misc` on an NFS root, repeatedly. Roughly 2 runs in 5.

**What changed to make it appear:** every sighting is from a run *after* NFS FIFO
support landed. Before that, `mkfifo()` failed early with `-1`, so the kernel's
mkfifo path stopped at `posix_create` and never completed. Now `fifo_type`
passes, which means `posix_mkfifo`'s full sequence — `proc_create` of the pipe in
posixsrv, `proc_link` into posixsrv's namespace, then `posix_create` of the
otDev node — runs to completion for the first time. That newly reachable kernel
path is the prime suspect, not the filesystem change that reached it.

Note also that the corrupting content is a *full path* beginning with `/`, and
`posix_create` is precisely the function that `lib_strdup`s the full path into a
kernel block (`name`) and frees it at the end. Its own alloc/free is balanced;
the question is whether anything else holds or re-frees that block, or an
interior pointer into it (`lib_splitname` hands out `basename`/`dirname` pointing
*into* `name`).

## Tools now in place

Both halves of the earlier suggestion are implemented:

- `_vm_zfree` rejects a **misaligned** free (kernel `b516c8a4`). Does not fix
  this bug — it is a write-after-free, not a misaligned free — but closes the
  neighbouring hole.
- **Free-block poisoning**, off by default, build with `-DVM_ZONE_POISON=1`
  (kernel `3e913c24`). Stamps freed blocks with 0xa5, records the freeing site,
  and verifies on the next allocation, printing block/freer/first-bad-offset and
  the first 16 bytes as hex+ASCII.

With poisoning on, the corruption did **not** reproduce in two runs. The memset
per free changes allocation timing, so it may be masking the race; the next
attempt should run many more iterations, and consider poisoning without the
memset (link + freer word only) so timing is barely perturbed.

---

## 2026-09-02 (later): real defect found and fixed; the crash is NOT yet explained

### Corrections to what this document said earlier

1. **The "~2 runs in 5" rate was wrong — it conflated two different crashes.**
   Scanning all 5100 UART logs, the heap signature (`pc` in `_vm_zalloc`,
   `far=74735f747365742f` = `"/test_st"`) appears in exactly **two** logs, both
   from 2026-09-02 (`07:00 nochmod-verify`, `07:35 poison-off-verify`). The third
   log I had counted (`05:32 lwip-regression`) is an unrelated fault: `pc` in
   **`hal_memcpy`**, `far=0x004130c0` (a *userspace* address) — an EL1 user-copy
   fault, the class in `project_el1_usercopy_fault_prot_user`, not a heap issue.
2. **"Poisoning's memset perturbs the timing that exposes the race" was
   speculation and should not have been stated as the reason.** It may simply be
   that the corruption is much rarer than the rate above implied.
3. **The FIFO correlation is confounded.** `9a0593d0` ("posix: record
   fd->canonical-path") landed 2026-09-01, in the same window as NFS FIFO
   support. "Every sighting is after FIFO landed" is equally "every sighting is
   after `open_file_t.path` landed".

### The real defect found (fixed, kernel `e5c5f833`)

`posix_fileDeref()` frees `f->path` unconditionally when non-NULL, but two of
the three `open_file_t` allocation sites never initialised it — `posix_pipe()`
and the `pp == NULL` branch of `posix_clone()` — and `vm_kmalloc` does not zero.
So closing such an fd frees whatever the recycled block held there. A block last
used by a regular file holds a **real, already-freed path pointer**: in range and
block-aligned, so both `_vm_zfree` guards accept it and the block lands on the
free list twice. That is precisely the shape needed to produce this crash.

`posix_newFile()` (sockets) escapes it only because it `hal_memset`s — which is
why sockets never triggered anything.

**But it is currently latent, and I am not claiming it as the diagnosis.**
Measured on hardware with a temporary probe on every `posix_fileDeref`:
regular files (`type=0`) 51/51 carry a path; pipes (`type=1`) **60/60 read
`path == NULL`**, because pipe blocks are currently served from a zone that has
never held a path pointer (a property of the present allocation pattern, not a
guarantee). Fixed as a defect on its own merits — zeroing the whole struct as
`posix_newFile` does, which also covers `f->ln`.

Falsification check the analysis proposed — *no sighting may predate `9a0593d0`*
— **passes**: the signature exists in no log older than that commit.

### Also fixed

`_vm_zfree` returned void, so `_kmalloc_free` adjusted `allocsz`/`hdrblocks` and
moved zones between lists even when the free had been **rejected** by the guards.
Now returns `EOK`/`-EINVAL` and the caller skips the accounting (kernel
`8522b1db`) — a loose end in `b516c8a4`.

### Tools now in place

- **Always-on link validation** in `_vm_zalloc` (kernel `521320e9`): the link is
  checked before it becomes `zone->first`, so the corrupted block is *named* and
  the list is truncated instead of followed. This converts the hard EL1 Data
  Abort into a `vm: CORRUPT free-list link block=... link=...` line plus a
  graceful allocation failure. Cost: two compares and an AND.
- **Opt-in trace ring** (`-DVM_ZONE_TRACE=1`): block/caller/seq per alloc+free,
  dumped for the corrupted block by the check above. No memset, unlike poisoning.
- **`tools/heap-stress/heapstress.c`**: replays the crashing shape — the
  `nlink` link/unlink churn then `tim`'s create-of-a-just-unlinked-name — plus
  mkfifo, an AF_UNIX socket, a symlink chain, and the pipe recycle sequence.

### Today's negative results (all on the shipped configuration)

| Trial | Result |
|---|---|
| 6× `test-libc-misc` in one boot (trace on) | clean |
| 5 further boots, one `test-libc-misc` each | clean |
| `heapstress` 1500 iterations, link/unlink/recreate | clean |
| `heapstress` 800+ iterations, + fifo/socket/symloop | clean |
| `heapstress` 500 + 200 iterations, pipe recycle | clean, and 0 stale-path frees |
| post-fix: misc 207/2, stdio 80/0, stress 300 | clean, 0 faults |

**Status: open.** One real latent double-free removed; the crash itself has not
reproduced under any instrumentation today, so nothing here may be reported as
having fixed it. Next time it appears, the validation above should print the
corrupted block instead of faulting — start from that line.

### Follow-ups found during this pass (verified by inspection, not yet fixed)

1. ~~**SMP hazard, `posix/posix.c:676` vs `722`**~~ — **FIXED, kernel
   `e88c8b75`.** And it was not merely "reachable in principle": with a close
   sweep racing an open loop, **869 of 869 raced closes read `refs == 0`**. It
   never crashed because `posix_fileDeref` decrements to zero *before* freeing,
   so a recycled `open_file_t` almost always reads 0 and `--refs` yields −1; the
   real damage was a leaked file + path string per event. Fixed by holding a
   construction reference for the duration of the open. A/B on hardware: unfixed
   869/869 garbage `refs=0`, fixed 968/968 accounted `refs=2`, 0 garbage.
   Regression test: `test-libc-pthread` → `test_pthread_fdrace`.
2. **Leak, `posix_putUnusedFile` (`posix.c:181-187`)** — frees `f` but not
   `f->path`. **Effectively mooted** by `e5c5f833`: its only call path is the
   clone-tty OOM unwind, and those files now carry `path == NULL`. Left listed
   because the asymmetry (a free of `f` that ignores `f->path`) is still there
   and would bite if another caller appeared.
3. **`unix_close` (`posix/unix.c:1278-1290`)** — one `unixsock_get` and **two**
   `unixsock_put`. Balanced by design (one for the get, one for the tree ref),
   but if the first put ever reached `refs == 0` the second would write into and
   re-free a freed `unixsock_t`.

### A caveat on the quarantine (kernel `1ff99ec4`)

My first cut of the link check (`521320e9`) truncated the free list but left
`used < blocks`, so the next allocation from that zone loaded `first == NULL`
and faulted anyway — the diagnostic bought one allocation, not survival. The
branch now also forces `used = blocks`, which genuinely retires the zone. Worth
remembering if this check is ever ported elsewhere: truncating the list is not
sufficient on its own.


### Still open in this class (found while fixing the above; do NOT need the crash)

- **`posix_open` stomps the flags of a descriptor it no longer owns.** The
  construction reference closes the use-after-free and the leak, but one write
  still assumes the fd is ours after the IPCs:
  `p->fds[fd].flags = (oflag & O_CLOEXEC) ? FD_CLOEXEC : 0`. Raced sequence: a
  concurrent close clears the slot, another thread's `open` re-allocates the
  **same** fd (`_posix_allocfd` looks for `file == NULL`), and our flags write
  then sets or clears `FD_CLOEXEC` on **someone else's** descriptor. Pre-existing
  (the old code had the identical write) and not memory-unsafe, so it did not
  block the fix; the remedy is the one-line pattern already used in the error
  tail — guard the write with `if (p->fds[fd].file == f)` under `p->lock`.
- **Wording to verify:** `e88c8b75`'s message says a racer observing the zeroed
  file "fails cleanly" because `oid = {0,0}`. That rests on port 0 not being a
  live port — worth one grep of the port allocator before the claim is repeated.
- **Test hygiene:** `test_pthread_fdrace` unlinks its file in the test body, so a
  failing run leaves `pthread_fdrace.txt` in the NFS export — the leftover-file
  footgun that has bitten this project before. Move it to `TEST_TEAR_DOWN` next
  time that file is touched.

### Trade-off accepted in `b49268e5`/`a follow-up` (fault-dump narrowing)

Not dumping recoverable kernel-PC/user-map faults also hides a *storm* of
them. That matters here: the PROT_USER COW-storm was diagnosed precisely
because every fault printed. EL0 faults already have this blindness, so the
trade is at least consistent — but the recorded lesson is "never dismiss an
exception-storm as benign". **Option, not built:** a per-boot counter of
resolved kernel-PC user-map faults, printed once past a threshold, would
restore the canary without the per-fault noise.

### Canary semantics (designed, so nobody misreads a lone line)

`map_usercopyFaults` never resets and the first report is an exact `== 64` match, then every
1024. So a **storm** blows through 64 and keeps hitting milestones (the intended signal),
while a **slow accumulation** over long uptime prints once at 64 and then only once per
1024. That is deliberate noise control, not an undercount: a single
`vm: 64 kernel user-copy page faults resolved so far` line means "this crossed 64 at some
point", not "there were exactly 64". Read the milestone sequence, not one line.

Unexplained and worth a future look: something produced **2 EL1 faults** in
`test-libc-unix-socket` on 2026-09-02 (`zfinal`) and in an earlier 05:32 run, then nothing
across ~6 later runs. It is not fork-COW — 1024 COW writes in a forked child resolve zero
faults on this kernel. The lazily-mapped path involved is unidentified.

### The same construction-window bug in the socket family — **FIXED** (kernel `d3862861`)

Resolved 2026-09-02: `posix_newFile` now returns the file with a construction
reference (refs = 2) and `socket`/`socketpair`/`accept4` write through that
pointer, never through the slot; `posix_fileConstructAbort/Done` end the state
correctly. HW-proven that the window really fires: with 199k sockets created
against 29k sweep-closes, a probe caught `slot stolen … refs=1` — the slot's
reference already dropped by a racing close, only the construction reference
keeping the file alive. On the old code that same event freed the file under the
constructor, and the error path freed it a second time. Also replaced the
all-ones construction oid with `POSIX_PORT_CONSTRUCTING` (all-ones is
bit-identical to `US_PORT`) and fixed `posix_socketpair`'s leaked pinfo
reference on `-EAFNOSUPPORT`. Original analysis follows.

#### Original finding (for the record)

`e88c8b75` fixed `posix_open`. The socket calls have the identical shape and were
not touched — this is the highest-severity open item in this class:

`posix_newFile()` (`posix/posix.c:321-328`) publishes `p->fds[fd].file` with
`refs = 1` and **releases `p->lock` before returning**. Its callers then
dereference `p->fds[fd].file` repeatedly with no lock, no reference of their own,
and no re-check that the slot is still theirs:

- `posix_socket`   — `posix.c:2310-2312` (AF_UNIX), `2321-2323` (AF_INET)
- `posix_socketpair` — `posix.c:2377-2382`
- `posix_accept4`  — `posix.c:2424-2426`, `2432-2434`

Two consequences, both the class being hunted here:

1. A racing `close(fd)` (or the exec CLOEXEC sweep) takes `refs` 1→0, so
   `posix_fileDeref` frees `f` and NULLs the slot; the caller then writes
   `p->fds[fd].file->type = …` through a freed pointer or a NULL. The error
   paths call `posix_putUnusedFile` (`2332`, `2370`, `2390`, `2391`, `2446`),
   which takes no lock, does not NULL-check, and ignores the refcount — so on
   that path it is `proc_lockDone` on a freed lock plus a **second `vm_kfree` of
   the same block**. Both `_vm_zfree` guards accept a legitimately block-aligned
   pointer, so the block lands on the free list twice: one block, two owners,
   next write overwrites the link. That is this document's whole subject.
2. Between `posix_newFile` returning and the caller writing `oid`, the published
   file has `oid.port == 0` — a live port, per `19dcbd5d`. `unix_accept4` /
   `inet_accept4` **block until a connection arrives** inside that window, so it
   is arbitrarily long.

Fix is the same construction reference: `refs = 2` in `posix_newFile`, a
slot re-check before each write, and a refcounted teardown in
`posix_putUnusedFile`. The reproducer is the one in `tools/heap-stress`
(`mode=race`) with `socket()`/`socketpair()` in place of `open()`.

Also noted while reviewing: `posix_socketpair` returns `-EAFNOSUPPORT`
(`posix.c:2358-2360`) without `pinfo_put(p)` — a leaked pinfo reference.

### Lower-severity items from the same review — 3 of 5 now FIXED (kernel `c3f60f1b`)

- ✅ `f->path` published before being filled → build into a local, publish last.
- ✅ `posix_open` returning a descriptor a racing close had taken (and
  `_posix_allocfd` may have reallocated to a different file) → now `-EBADF`.
  **Behaviour change under a race, and pronounced on this port:** `open()` does
  several blocking IPCs (root on NFS), so the window is far wider than a sweep
  iteration and the sweeper wins essentially every attempt in the regression
  test — which was updated to assert the contract (succeed, or fail with EBADF,
  nothing else) rather than one side of a timing race.
- ✅ `posix_exit` leaving dangling pointers in a zombie's fd table → slots cleared.
- ✅ `F_SEEKABLE` accepting a half-built file in `fcntl` → new `ftConstructing`
  type (appended to the enum, set at both publish sites). Every `f->type` test is
  an equality against a real type or a switch with a default, so the state is now
  rejected everywhere instead of looking like `ftRegular` (enumerator 0, which a
  zeroed struct leaves behind). `posix_fileDeref` also short-circuits it: a
  construction that opened nothing has nothing to close, and its oid names a
  port that cannot exist, so the old `proc_close` only spent an IPC to be told
  -EINVAL. kernel `see manifest 2026-09-02-ft-constructing`.
- ⬜ `posix_exit`/`posix_exec` calling `posix_fileDeref` under `p->lock`, whose
  `refs == 0` branch does a blocking `proc_close` — pre-existing and structural
  (needs collecting the files, then dereferencing outside the lock).

### Original list (for the record)

- `posix_open`'s construction sentinel `f->oid.port = (u32)-1` is bit-identical
  to `US_PORT`. Latent only — nothing tests `f->oid.port == US_PORT` today, but
  `posix_truncate` already tests an oid that comes from `&f->oid`, so such a test
  is one edit away. An explicit "under construction" `type` would be safer and
  would also stop `F_SEEKABLE(f->type)` accepting a half-built file in `fcntl`.
- `posix_open` publishes `f->path` before filling it (`posix.c:800-806`), so a
  concurrent `posix_fdpath` can `hal_strlen` an uninitialised buffer. Build into
  a local and store the pointer last.
- `posix_open` returns `fd` even when the slot was lost to a racing close and
  reallocated to a *different live file*. Returning `-EBADF` when
  `p->fds[fd].file != f` costs one compare under a lock already held.
- `posix_exit` derefs each fd but never NULLs `p->fds[fd].file`, leaving a
  zombie's table full of dangling pointers.
- `posix_exit`/`posix_exec` call `posix_fileDeref` while holding `p->lock`, and
  its `refs == 0` branch does a blocking `proc_close`. Pre-existing.
