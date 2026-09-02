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


---

## 2026-09-02 (evening): a BETTER reproducer — `test-libc-unix-socket`

The `test-libc-misc` sighting has not reproduced in ~20 runs. `test-libc-unix-socket`
is a far better hunting ground: across 18 runs today it produced **4 fatal events**
(run did not complete, or a test failed), plus 2 earlier non-fatal ones.

| time | abort | result |
|---|---|---|
| 05:32 | 2 | 25/0 — recoverable EL1 user-copy faults (dumped then; silenced since `b49268e5`) |
| 09:19 | 2 | 25/0 — same |
| 09:41 → 14:12 | 0 | 25/0 × 10 |
| **14:34** | 2 | **no summary** |
| **14:42** | 1 | **no summary** |
| 14:46, 14:48, 14:50 | 0 | 25/0 × 3 |
| **14:53** | 2 | **25 Tests 1 Failures** |
| **(reverted build) P** | 0 | **no summary** |
| **(reverted build) Q** | 2 | **25 Tests 1 Failures** |
| (reverted build) R | 0 | 25/0 |

**Today's commits are NOT the cause.** The fatal events cluster after 14:21, which
looked damning (0 in 12 runs before, 4 in 6 after) — so I reverted the two commits
in that window (`7ac1bd10` ftConstructing, `39135453` fstat) and the fatal events
**continued** (P and Q above). The clustering was sampling noise.

### Four distinct signatures, all in the fork + AF_UNIX + accept region

1. `pc` in `hal_memcpy`, `far=0x004130c0` (a *user* address) — EL1 user-copy, recoverable.
2. `pc` in **`proc_threadWakeup`+0x58**, `far=0x753e4bed115dc310` — the kernel following
   a **corrupted thread wait-queue link**. Wild, high-entropy address.
3. EL0, registers full of `0xbabababa` (a freed-memory poison pattern), `far=0xbababac090909082`,
   with ASCII path fragments (`mp/test_`, `file_15`) in other registers.
4. EL0 `far=0x10` (near-NULL) in a **forked child**, which is why
   `accept_connect_async` fails its `TEST_ASSERT(WIFEXITED(status))` at
   `unix-socket.c:1482` — the child died rather than exiting.

The variety of signatures — free-list link, wait-queue link, userspace poison — is
the signature of memory corruption rather than a logic bug in any one path.

### Next step

Run `test-libc-unix-socket` repeatedly against a kernel built with
`-DVM_ZONE_TRACE=1` (and the always-on link check). At ~1 fatal event in 3 runs this
is cheap to catch, unlike the `test-libc-misc` crash. Signature 2 also suggests
widening the check beyond the zone allocator: a corrupted `proc_threadWakeup` queue
link means whatever scribbles is not confined to kmalloc blocks.

---

## 2026-09-02 (late): the AF_UNIX audit — named suspects, and two of my framings corrected

### Corrections to what I wrote earlier today

1. **`0xba` is not a userspace malloc poison. It is the kernel's fresh thread-stack
   fill** — `proc/threads.c:653`, `hal_memset(t->kstack, 0xba, t->kstacksz)`. So
   signature 3 (`0xbabababa` everywhere, `far=0xbababac090909082`) means a freed block
   was **recycled as a thread kstack and then read** — a read-after-free, not a
   structured overflow. That reframes the whole signature.
2. **"Always in the fork + fd-passing tests" was wrong.** `accept_connect_async`
   contains no `fork()`, no `sendmsg`, and no `SCM_RIGHTS`. What the crashing tests
   actually share is high **socket churn**. (Also `dgram_sock_fd_flags` is
   code-identical to the crashing `stream_sock_fd_flags` and has never crashed.)

### Prime suspects, both reachable and both cross-process (NOT yet fixed)

**S1 — `unix_accept4` uses the connecting socket with no reference held.**
`posix/unix.c:486-503`: `r = s->connecting;` comes straight off a list, never through
`unixsock_get()`, so `r->refs` is not incremented — and the code says so itself, two
lines down: `/* FIXME: handle connecting socket removal */`. The accepting thread then
writes `r->state`, `r->remote`, takes `r->spinlock` and calls
`proc_threadWakeup(&r->queue)` on an object another core may be freeing in
`unixsock_put` (`unix.c:284-331`), which does `proc_lockDone`, `hal_spinlockDestroy`
and `vm_kfree`. **This is signature 2 exactly**: `pc` in `proc_threadWakeup+0x58` with
a high-entropy `far`.

**S2 — `unixsock_put` never unlinks a socket from its peer's `connecting` list.**
A socket mid-connect has `remote == NULL` by definition (`unix_connect` waits on
exactly that, `unix.c:735`), so the `s->remote != NULL` cleanup is skipped and the
`else` branch is an unfixed `/* FIXME: handle connecting socket */` (`unix.c:313`).
The code already knows the consequence — `unix.c:753` carries
*"a socket left on the list would cause use-after-free in unix_accept()"* — but that
guard exists **only on the `-ETIME` path**. The non-blocking `-EINPROGRESS` path
(`unix.c:729-733`) returns to userspace with the socket still linked and no cleanup
hook, and `accept_connect_async` drives precisely that path in a loop over
`SOCK_STREAM` and `SOCK_SEQPACKET`.

Aggravator: `unixsock_alloc` deliberately **reuses freed ids** (`unix.c:194-215`), so a
stale `oid.id` aliases a *different process's* socket on the next `socket()` call.

Fixing S2 needs a design choice: a connecting socket does not record which listener's
list it is on, so either add a back-pointer (set/cleared under the listener's
spinlock) or have `unixsock_put` search. S1 alone is not sufficient — S2 is what
leaves a freed node linked in the first place.

### Also found (real, none of them this repro)

- **`fdpass_pack` trusts a user-controlled `cmsg_len`** (`fdpass.c:79-128`): never
  checked against `controllen` nor for `>= sizeof(struct cmsghdr)`, re-read from user
  memory in a second loop, and `tot_cnt` accumulates in `unsigned int` so it can wrap —
  giving an allocation of 40 bytes and a loop that pushes up to 0x3FFFFFFC entries with
  no bound check in `FDPACK_PUSH`. An unbounded kernel-heap write from any userspace
  process. Not the repro (the tests always send `cmsg_len == CMSG_LEN(4n)`), but it
  should be fixed on its own merits.
- **`CMSG_NXTHDR` advances by `CMSG_SPACE(cmsg_len)`** (`fdpass.c:28`) where `cmsg_len`
  already includes the header, so it over-advances by 12 bytes: **multi-cmsg messages
  can never be parsed**. `libphoenix/include/sys/socket.h:49-63` has the identical
  error, which is why single-cmsg works. Its guard is also `>` where it should be `>=`.
- **`unix_shutdown` drops one reference too many** (`unix.c:1145-1162`: one
  `unixsock_get`, two `unixsock_put`). With id reuse, the still-open fd then aliases
  another socket and the eventual `close()` destroys **someone else's**. No test uses
  `shutdown()`.
- `fdpass_unpack`/`fdpass_discard` call `posix_fileDeref` under `p->lock`, whose last-ref
  branch does a blocking `proc_close` — the same hazard just removed from the exit/exec
  sweeps. `fdpass_discard` also ignores its own failure return, which would leak every
  packed reference.

### Fixed this pass

✅ **`recv()` now reports the control length it delivered** (kernel `381152c6`). It only
ever wrote `*controllen` on the deliver path, so EOF/`-EWOULDBLOCK`/`MSG_PEEK` left the
caller's input length over an untouched buffer — and the test suite's own helper then
parses uninitialised stack and memcpys an arbitrary length into its fd array. Closes one
userspace-smash mechanism (a good fit for signature 4) but **not the bug**: fatal events
continue at the same rate (1 in 4 runs).

### New evidence on where the corruption lands

Across runs the EL0 crashes are all the **same site**: `__fflush_unlocked`
(`libphoenix/stdio/file.c:805`), walking `file_common.list`, with `iter` corrupted to
either kstack poison (`0xbababababababaea`) or a **small integer** (`far=0x30`). So the
parent's libphoenix stream-list pointer is the thing being overwritten, with varying
garbage — a fixed target, which is worth exploiting: a guard/canary around that global,
or catching the writer, should be quicker than chasing the allocator.

---

## 2026-09-02 (later): attempted the S1/S2 fix, reverted — and why the obvious fix is wrong

**Attempt** (kept for reference as `docs/misc/2026-09-02-unix-connecting-ref-attempt.c.txt`):
make the `connecting` list own a reference — `s->refs++` before
`LIST_ADD(&r->connecting, s)` in `unix_connect`; `unix_accept4` inherits it when it
unlinks the node and releases it at the end; the `-ETIME` path releases it; and the
listener's teardown drains the list, waking each queued connector.

That fixes S1 and S2 *as stated* — a queued socket can no longer be freed while linked,
and `accept4` no longer touches an unreferenced object.

**But it is wrong, and reverted.** `remote` is a WEAK pointer in this design (nothing
holds a reference for the connection; `unixsock_put` clears the peer's `remote` and sets
`US_PEER_CLOSED` at teardown). So for the sequence `accept_connect_async` actually
drives — non-blocking `connect()` returns `-EINPROGRESS`, the app closes the socket, the
listener accepts it later — the list's reference is the LAST one, and releasing it in
`accept4` frees the peer at the instant the connection is established: `new->remote`
goes NULL, the freshly accepted socket is dead on arrival, and the test blocks forever.
Previously that was a leak, which masked it.

**So the real fix needs the connection to own references symmetrically** (`r->remote`
and `new->remote` each holding one, released when the peer relationship is torn down),
which is a genuine ownership refactor of `unix.c`, not a two-line reference bump. That
is the next attempt.

### Methodology note — I mis-measured this twice today

The reproducer is roughly **50% flaky**, and it has (at least) two failure modes:
a crash (`abort>0`) and a silent **hang** (no summary, `abort=0`). Both are pre-existing:
baseline after reverting gave clean / hang / failure on three runs. My attempt gave 4 bad
of 4, which I first read as "my change made it worse" — with n=4 against n=3 at ~50%
flakiness that conclusion was unsupportable, and the honest statement is that the attempt
is unjustifiable on design grounds, not on those numbers.

**For any future attempt here: n >= 8 runs per configuration**, and count hangs as
failures. Anything less cannot separate a real effect from this test's own noise.

---

## 2026-09-02 (night): S1 + S2 FIXED — every crash signature gone, one hang left

Kernel `9c60b783`. The second attempt kept the existing lifetime model instead of
fighting it (the first attempt's reference-owning list turned the bug into a hang,
because `remote` is deliberately weak — see the previous section):

- The queued socket now records **which listener holds it** (guarded by
  `unix_common.lock`; the list itself stays under the listener's spinlock), so
  `unixsock_put` unlinks itself on the way out from **any** path — closing while
  queued, the non-blocking `-EINPROGRESS` path, anything. That is S2.
- `unixsock_put` also **drains its own `connecting` list** when a listener is freed,
  clearing each back-pointer and waking each connector with `US_PEER_CLOSED` so a
  blocking `connect()` stops waiting. That replaces the last FIXME.
- `unix_accept4` takes `unix_common.lock` **before** unlinking and holds it until it is
  finished with the peer; `unixsock_put` takes the same lock, so the peer cannot be
  freed in that window. The lock is deliberately **not** held across
  `proc_threadWait` — that sleeps, and the connector needs the lock to make progress.

### Measured (n = 9, per the methodology rule in the previous section)

| | before | after |
|---|---|---|
| `test-libc-unix-socket` | ~50% of runs bad | **8 of 9 clean** |
| crash signatures 1-4 | all four observed | **none observed** |
| hang (no summary, 0 aborts) | observed at baseline | **1 of 9, unchanged place** |

If the bad rate were still 0.5, seeing ≤1 bad in 9 has probability ~1%, so the
improvement is not sampling noise — unlike the two readings I got wrong earlier today.

No regression: pthread 24/0, libc/misc 207/0, inet-socket 1/0, stdio 80/0, and X11
comes up with the desktop rendering (Xphoenix accepting connections, wmaker connected)
— worth checking explicitly because every `socket`/`accept4`/`close` in the system runs
through this path.

### What is still open

**The hang.** 1 run in 9, stopping after `dgram_sock_msg_fork` with no exception and
psh still alive, exactly where the baseline hangs stopped. So it is the residual of the
same area, not something this fix introduced. Next: instrument that specific point —
which thread is blocked and on what queue — rather than sampling whole runs.

Still unfixed and unrelated to this repro, from the earlier audit: the user-controlled
`cmsg_len` heap-overflow primitive in `fdpass_pack`, the `CMSG_NXTHDR` mis-advance in
both the kernel and libphoenix, `unix_shutdown` dropping one reference too many, and
`fdpass_*` dereferencing under `p->lock`.

---

## 2026-09-02 (late night): CORRECTION — the crash class is reduced, not eliminated

**I claimed "all four crash signatures gone" on the strength of 9 runs. That was
premature.** Run 11 (window raised to 150 s) crashed with `abort=2` and no summary. So
the honest figure after `9c60b783` is **1 crash in 11 runs (~9%), down from ~50%** — a
large, real improvement, but not a fix of the class.

### The residual failure is localised

- It is in **`TEST(test_unix_socket, transfer)`** — the test immediately after
  `dgram_sock_msg_fork`, whose name never prints because Unity prints on completion.
- `transfer` uses a **non-blocking** socketpair and both sides **busy-poll** on EAGAIN,
  so a stall there cannot be a missed wakeup.
- Not slowness: run in isolation it passes at the default `--transfer-loop-cnt 50`
  (and at 1) well inside a 100 s window.

### Sharper signature: exit-time libphoenix lists, corrupted with a KERNEL pattern

| run | site | detail |
|---|---|---|
| earlier | `__fflush_unlocked` (`stdio/file.c:805`) | walking `file_common.list`; `iter` = `0xbababababababaea`, and in another run a small integer (`far=0x30`) |
| run 11 | **`__cxa_finalize`** (`stdlib/atexit.c:108`) | `far=0xbababa000005a0`, `x2=0xbabababa00000000` — a 64-bit pointer whose **upper half** is `0xbabababa` |

Both are **exit-time** walks of a libphoenix global list, and `0xba` occurs in exactly
one place in the entire tree: `proc/threads.c:653`,
`hal_memset(t->kstack, 0xba, t->kstacksz)` — the kernel's fresh thread-stack fill. So a
kernel pattern is landing in user data. That is the thread to pull.

### Hypotheses tested and ELIMINATED (do not repeat these)

- **Fresh anonymous memory is not zeroed** → false. 40 × 2 MB mapped after process
  churn: every byte zero, and `.bss` is zero at startup (`heapstress zerocheck`).
- **fork does not isolate the parent** → false. 200 iterations, parent heap/stack/.data
  untouched by the child (`heapstress forkiso`).
- **SCM_RIGHTS receiving scribbles outside the control buffer** → not in a single
  process: 3000 round trips, 4 MB canary intact (`heapstress fdpass`). Untested across
  processes.
- **`fstat`/`ftConstructing`/the sweep changes caused it** → false, ruled out by revert
  (see the earlier section).

### One test that proved nothing — flagged so it is not miscounted

`heapstress leakcheck` (scan a caller-supplied `sockaddr` for `0xba` after
`getsockname`/`getpeername`) reported **"0 bytes written by the kernel"**: those calls
write nothing for an AF_UNIX socketpair on this port, so the buffer was never touched
and the pass is **vacuous**. A real version has to use a call that genuinely fills a
user struct — `accept()` with an address buffer, or `recvmsg` with `msg_name` — on a
*named* socket.

### Next step

Find the kernel→user write vector. The audit already showed `recv`/`send` copy directly
into and out of user buffers under `s->lock` via `lib/cbuffer.c`, and `fdpass_unpack`
writes user memory under two locks — any of those with a length that exceeds what was
initialised would put kstack bytes into user memory. Instrument a kernel-side check that
the destination length matches what is actually written, or paint kstacks with a
per-thread value so the *source* thread can be identified from the pattern.
