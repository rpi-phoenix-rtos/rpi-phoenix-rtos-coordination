# poll-wake — readiness wake-up for `poll()` on server-backed descriptors

Closes register item **P9** ([KNOWN-ISSUES](../KNOWN-ISSUES.md)), M3 gap **G12**
([M3](M3-libdrm-phoenix.md)), and the kernel follow-ups 1–2 of [E5](E5-deferred-reply.md) §5.
Branches: `phoenix-rtos-kernel` **`gpu-lane/poll-wake`** and `libphoenix` **`gpu-lane/poll-wake`**
(worktrees, not merged). Tools: `tools/gpu-lane/pollwake/` (new) and `tools/gpu-lane/kms/`
(`--poll-notify`, additive).

**Status (2026-09-27, after build 11):** both branches are merged (kernel `ee5939fc`, libphoenix
`d40050c`) and cycles 1–3 have run. The microbenchmark PASSES: a notifying device fd wakes `poll()`
in 38–62 µs. The kmscube and quakespasm predictions FAILED: 30.00 and 29.0 fps. The root cause is in
`rpi4-kms`, not the kernel. The fix is built as `tools/gpu-lane/kms/out-gate`, and the next cycle is
pre-registered in "Result + analysis — build 11 cycles" at the end. §§1–9 below are the
pre-registration as written, with ↩ markers where build 11 proved them wrong.

## 1. The problem

`poll()`/`select()` end up in `posix_poll` (`posix/posix.c`). For every descriptor that another
process serves (a device, a pipe, a socket), the kernel sends one `mtGetAttr/atPollStatus` query and
gets back a snapshot. If nothing is ready it has no way of hearing back, so it sleeps
`POLL_INTERVAL` (20 ms) and asks again. Only two sets avoid that sleep:

- a set of exactly one inet socket (lwip holds the query open for `block_ms`);
- a set containing an AF_UNIX socket, which blocks on the kernel's AF_UNIX readiness queue. Even
  there, the server-backed fds in the same set are seen only on that queue's 20 ms fallback.

Measured cost: E5 `poll` on an emulated vblank device woke **p50 8.2 ms, max 16.7 ms** late. A
blocking `read()` on the same fd woke in **19.9 µs**, so the server side is fine. **kmscube** on
Mesa GBM/EGL + rpi4-kms + rpi4-v3d-async ran at **29.85 fps**, which is 60 Hz halved, with the GPU
mostly idle. Its legacy loop `select()`s stdin plus the card fd for every flip event.
**quakespasm-drm** (SDL KMSDRM, which `poll()`s card0 for every flip) ran at **24.4 fps**, against
40.4 for the same game without the page-flip poll. Xorg, Weston and SDL all multiplex a DRM fd with
`poll()`, so M4/M6 need this fixed.

## 2. Options considered

| | Idea | Verdict |
|---|---|---|
| 1 | **`block_ms` for device fds.** The kernel sends the block time (already packed into `atPollStatus` bits 16+ for lwip), and a server that supports it parks the query and answers at readiness or timeout. | Works for **one** fd. A multi-fd set queries its fds one after another: a parked query on fd #1 stalls fd #2. Fixing that means sending all queries at once and waiting for any reply. The kernel has no asynchronous send (the kernel message lives on the sender's stack, E5 (a)), so that needs a kernel thread per fd, plus a protocol to cancel the parked queries once one fd fires. Worse, a parked request **cannot be interrupted** (E5 condition 1), so a `poll()` would stop honouring signals and could not be killed while a query is parked. Every server would also need parking logic. **Rejected.** |
| 1b | Single-fd `block_ms` fast path, plus something for multi-fd sets | Still needs a second mechanism for multi-fd sets, which are what Xorg, SDL (card + input) and kmscube (card + stdin) use. That mechanism alone is enough. **Rejected as redundant.** |
| 2 | **Readiness notification.** The server tells the kernel "oid X may have changed" and pollers watching X wake up and re-query. This is the device equivalent of `usocket_pollWait`. | Correct for any set. Existing servers need no change, and the server side is one call. **Chosen.** |

Choices made inside option 2:

- **Global sequence counter + one shared queue** (the AF_UNIX model). Every notify wakes every
  poller. Worse, a notify that finds no sleeper leaves `wakeupPending` on the **shared** head, and
  whichever poller enqueues next consumes it, so the poller that needed it can sleep the full 20 ms.
  Rejected.
- **A wait queue per port.** A thread sleeps on one queue at a time, so a set with fds on two ports
  cannot wait on both. Rejected.
- **A waiter per poller** (chosen). It lives on the poller's kernel stack and holds a private queue
  plus a 64-bit mask of hashed oids. It sits on one global list that a notify walks. Each thread
  owns its queue, so `wakeupPending` always lands on the right thread. That sentinel is what makes
  the design race-free without extra state.

## 3. The design

### API

```c
/* libphoenix <sys/msg.h>; kernel syscall appended after memUnexport */
int pollNotify(const oid_t *oid);
```

A server calls it **after** it has updated the state its `atPollStatus` answer reads (an event
queued, space freed, peer gone). Only the owner of `oid->port` may notify. Otherwise the call
returns `-EPERM`, the same gate as `memExport` (`syscalls_portOwned`). With no poller watching, the
call is a no-op. A kernel without the call returns `-EINVAL` (syscall number out of range,
`syscalls_dispatch`), so servers probe once at start and may otherwise call it unconditionally.

### Kernel (`posix/pollwake.{c,h}`, `posix_poll`)

```
pollwake_waiter_t { next, prev; thread_t *queue; u64 mask; u8 watchUnix; }   // on the poller's stack
pollwake_common   { spinlock_t lock; pollwake_waiter_t *waiters; }            // global list
bit(oid) = 1 << (golden-ratio hash of (port, id), top 6 bits)                 // 32-bit arithmetic only
```

`posix_poll` now makes one classification pass over the set before any query goes out. It counts
AF_UNIX fds and server-backed fds, and ORs every server fd's `bit(oid)` into a stack waiter. Then:

| Set | Path | Change |
|---|---|---|
| exactly one inet socket | `block_ms` handed to lwip | none |
| AF_UNIX fds only | `usocket_pollWait` | none |
| **≥ 1 server-backed fd** (possibly with AF_UNIX fds) | **pollwake waiter** | new; replaces the blind `proc_threadSleep(20 ms)` and, for mixed sets, the AF_UNIX-queue wait |

On the waiter path:

1. `pollwake_register(w)` puts the waiter on the list **before the first query**.
2. `do_poll_iteration` calls `pollwake_watch(w, oid)` before every `atPollStatus` query. This only
   takes the lock if the bit is missing, for example after a `dup2` raced the classification pass.
3. When nothing is ready, `pollwake_wait(w, cur + now)` sleeps on `w->queue` under the list lock.
   `now` is the **same** `min(POLL_INTERVAL, remaining timeout)` that the old sleep used. The sleep
   is `proc_threadWaitInterruptible` when the set also holds an AF_UNIX socket, which is what
   `usocket_pollWait` did. Otherwise it is `proc_threadWait`, keeping the old timed sleep's
   behaviour of not ending `poll()` on a signal (§5).
4. When it wakes (notify or timeout, or a signal on a mixed set), the kernel re-queries. `-EINTR`
   ends the `poll()`.
5. On every exit path, `pollwake_unregister(w)`.

`pollwake_notify(oid)` takes the list lock, walks the waiters, and calls
`proc_threadWakeup(&w->queue)` for each waiter whose mask has `bit(oid)`. `uchannel_pollNotify()`
(every AF_UNIX state change) also calls `pollwake_notifyUnix()`, which wakes the waiters that have
`watchUnix` set. That flag is set only on mixed sets, so pure AF_UNIX pollers such as X clients cost
nothing extra.

**Race-freedom.** Take a server that updates its state S and then calls `pollNotify`, and a poller
that registers, sets the bit, queries S, and sleeps.

- If the notify's critical section runs after the poller set the bit, the poller is woken. If it is
  not asleep yet, its private queue holds `wakeupPending`, and the next `pollwake_wait` returns at
  once. `_proc_threadEnqueue` consumes the sentinel.
- If the notify ran before the bit was set, then the state update came earlier still. The chain
  "server stores S → server unlock → poller lock → `proc_send` → server `msgRecv` → read S" orders
  that store before the query, so the query sees S ready.

No window loses a wake-up. `wakeupPending` is never cleared between loop iterations, so a notify
that lands during a re-query forces one more re-query, which is correct. A stale sentinel cannot
outlive the call because the waiter is re-initialised on every `poll()`.

**Lock order:** list lock, then `threads_common.spinlock`. That is what `proc_threadWaitEx` does
when handed a spinlock, and it is the same order `uchannel_pollWait` uses. `pollwake_notifyUnix`
runs inside a held channel mutex, where taking a spinlock is allowed.

**Cost.** A notify costs one syscall, a port lookup for the owner check, and a walk of the waiter
list, which holds only pollers blocked on server fds right now (a handful). `poll()` costs one extra
`posix_getOpenFile` per fd per call for the classification pass, plus two uncontended spinlock pairs
to register and unregister. Hash collisions and unrelated notifies cause at most one extra round of
queries, never a missed or early return. A timed-out `poll()` still returns at its deadline, and the
`timeout` test in §7 checks that.

### libphoenix

`include/sys/msg.h` declares `pollNotify()`. The stub needs no code: every `arch/*/syscalls.S`
generates one per `ID()` in `<phoenix/syscalls.h>`, which the kernel installs into the sysroot.

### Server opt-in

**rpi4-kms** (`tools/gpu-lane/kms`, built with `build.sh --poll-notify`, i.e. `-DKMS_POLL_NOTIFY`.
The default build runs the same code as before; the only unconditional changes are one unused
struct field and a comment):

- `claim_names()` probes `pollNotify({port, 0})` once and logs
  `KMS srv poll_notify=1 rc=0` (or `=0 rc=-22` on an old kernel).
- `ev_push()` is the single place a client's event queue grows. It runs for flip-complete, vblank
  and CRTC-sequence events, under `srv.lock`, after `evtail++`. It calls
  `pollNotify({srv.port, client id})`. The descriptor's `oid.id` is the client id that `mtOpen`
  returned, so `/dev/kms` and `/dev/dri/card0` (one port) are both covered.
- `atPollStatus` is unchanged (a snapshot: `POLLIN` iff the queue is non-empty).

**rpi4-v3d-async (sketch, not done).** Today its `mtGetAttr` answers only `atMode`, and G15 notes
that `poll()` on a sync file is unsupported. When the `/v3dsync/<id>` namespace from
`drm_phoenix_ext.h` is built:

- `atPollStatus` on a sync-file oid answers `POLLIN` once the fence page's seqno has reached the
  file's point.
- The thread that publishes fence completions calls `pollNotify` for every open sync-file oid whose
  point it just passed.

To keep the syscall count bounded at high completion rates, use the **armed** pattern. A not-ready
`atPollStatus` answer sets `armed` on that oid under the server lock. The completion path notifies
and disarms only the armed oids. That path is race-free by the same argument as above, and it costs
nothing when nobody polls. rpi4-kms does not need this at 60 events/s.

**Other candidates (follow-ups, each additive):**

| Server | What gains |
|---|---|
| posixsrv pipes/pty | `select()` on pipes: bash, make, psh via `/dev/pts` |
| pl011-tty | stdin in a GPU app's poll set |
| usbkbd/usbmouse | Xorg and SDL input: M4 `phxhid.c` notes today's 20 ms input quantum |
| lwip | multi-fd socket sets |

## 4. Exact changes

**`phoenix-rtos-kernel` `gpu-lane/poll-wake` @ `ee5939fc`** (one commit on `master` `38ad32cf`):

| File | Change |
|---|---|
| `include/syscalls.h` | `ID(pollNotify)` appended after `ID(memUnexport)` (syscall 110). Nothing renumbered. |
| `syscalls.c` | `syscalls_pollNotify()`: `vm_mapBelongs` + owner gate + `posix_pollNotify`. `syscalls_portOwned()` loses its `#ifndef NOMMU` guard, since it is now used on all targets. |
| `posix/pollwake.c`, `posix/pollwake.h` | new, as §3 |
| `posix/Makefile` | `pollwake.o` |
| `posix/posix.c` | include; `POLL_INTERVAL` comment; `do_poll_iteration(…, w)` sets the bit before each query; `posix_poll` classification pass + waiter path; `posix_pollNotify()`; `posix_init` → `pollwake_init()` |
| `posix/posix.h` | `posix_pollNotify` prototype |
| `posix/uchannel.c` | `uchannel_pollNotify()` also calls `pollwake_notifyUnix()` |

**`libphoenix` `gpu-lane/poll-wake` @ `9b54153`** (on `master` `8fb82ae`): `include/sys/msg.h`,
prototype and contract comment only.

**Coordination repo (uncommitted):**

| Path | Change |
|---|---|
| `tools/gpu-lane/pollwake/pollwake.c` | new test tool |
| `tools/gpu-lane/pollwake/build.sh` | builds the tool |
| `tools/gpu-lane/pollwake/pollnotify_shim.S` | stub numbered from a kernel tree's table |
| `tools/gpu-lane/pollwake/pollnotify-obj.sh` | links the shim only when the sysroot `libphoenix.a` lacks `pollNotify`, and refuses unless that table minus `pollNotify` equals the sysroot's |
| `tools/gpu-lane/kms/kms_main.c` | `#ifdef KMS_POLL_NOTIFY` blocks + comment |
| `tools/gpu-lane/kms/build.sh` | `--poll-notify` |

## 5. Backwards compatibility

- **ABI:** one syscall appended, nothing renumbered, no existing syscall's arguments or meaning
  changed. Stale binaries keep working. A new binary on an old kernel gets `-EINVAL` from
  `pollNotify`.
- **Servers that never notify** (every server today) see the same `atPollStatus` traffic. The poller
  still re-queries after `min(20 ms, remaining)`, as the old sleep did. The only difference is that
  a notify from **another** server whose oid hashes into the same bucket wakes the poller early,
  which costs one extra snapshot query. The kernel still masks `block_ms` into bits 16+ only on the
  single-inet path, as before.
- **Unchanged paths:** single-inet `block_ms`; pure AF_UNIX sets (`usocket_pollWait`, the X
  desktop's proven path); `poll()` with no valid fds.
- ↩ *Corrected after build 11: the premise below is wrong. The old kernel also returned `EINTR`
  for server-only sets, from the re-query's `proc_send`. See "Result + analysis", finding 3.*
- **Signals: unchanged.** A mixed set (AF_UNIX + server fds) sleeps interruptibly, as it did in
  `usocket_pollWait`, so a caught signal still ends it with `-1/EINTR`. A set of server-only fds
  sleeps uninterruptibly with the same ≤ 20 ms deadline. The old code ignored the interruption of
  its timed sleep and looped, so the handler ran only once an fd became ready or the timeout
  expired, and that is still what happens. POSIX asks for `EINTR` in that case too. Making
  `pollwake_wait` always interruptible is a one-line change, left out of this step on purpose
  (PLAN.md rule 3: no changed semantics for existing callers). The `eintr` test in §7 checks both
  set shapes.
- **Security:** only a port's owner can notify oids under it. The worst a rogue server can do is
  wake pollers of its own fds, or those sharing a hash bucket, into one extra query each.

## 6. Build / compile evidence (no Pi)

`./scripts/syntax-check.sh` always copies the file from `sources/<repo>` into `.buildroot` first, so
it cannot check a worktree, and staging a branch file into `.buildroot` would leak it into the next
`--skip-prepare` build. Instead:

1. **Recovered the real compile line** with
   `make -n -B .buildroot/_build/aarch64a72-generic-rpi4b/phoenix-rtos-kernel/posix/posix.o` in
   `.buildroot/phoenix-rtos-kernel`, with the toolchain on `PATH` and `TARGET=aarch64a72-generic-rpi4b`.
   Plain `make -n` prints nothing for an up-to-date object; that is the step `syntax-check.sh`
   papers over with its copy.
2. **Compiled** the touched files from the worktree with exactly those flags (`-std=gnu17 -Wall
   -Wstrict-prototypes -Wundef -Wimplicit-fallthrough -Werror -O2 -ffreestanding
   -mcpu=cortex-a72+nofp …`, `-I. -Ihal/aarch64 -Ihal/aarch64/generic/
   -I.buildroot/_projects/aarch64a72-generic-rpi4b`): `posix/pollwake.c posix/posix.c
   posix/uchannel.c posix/usocket.c syscalls.c`. All clean.
3. **Linked the whole kernel** from the worktree into a scratch prefix (nothing under `.buildroot`
   written):

   ```
   ln -s .buildroot/phoenix-rtos-build <worktree>/../phoenix-rtos-build
   make -C <worktree> -j8 TARGET=aarch64a72-generic-rpi4b \
       PROJECT_PATH=.buildroot/_projects/aarch64a72-generic-rpi4b \
       PREFIX_BUILD=<tmp>/pw-kbuild PREFIX_FS=<tmp>/pw-kfs all
   ```

   rc=0, no warnings. `nm` shows `syscalls_pollNotify`, `posix_pollNotify` and `pollwake_*` in
   `phoenix-aarch64a72-generic.elf`, and `strings` shows `pollwake` (the spinlock name, usable as the
   loader.disk check in §8).
4. **libphoenix:** the new `<sys/msg.h>` compiles against a caller (`-Wall -Werror`), and both
   `pollwake.c` and `kms_main.c -DKMS_POLL_NOTIFY` compile against it. The redeclaration is
   compatible.
5. **Tools:**
   - `POLLWAKE_KERNEL=<worktree> tools/gpu-lane/pollwake/build.sh` builds `out/pollwake` with the
     shim. `objdump` shows `svc #0x6e` (110) for `pollNotify` and `svc #0x6d` for the sysroot's
     `memUnexport`, so the numbering is consistent.
   - The same `build.sh` without `POLLWAKE_KERNEL` refuses, because `sources/` has no `pollNotify`
     yet.
   - `POLLWAKE_KERNEL=<worktree> tools/gpu-lane/kms/build.sh --poll-notify --out out-poll` builds
     `out-poll/rpi4-kms` (`T pollNotify`, `poll_notify=` log string).
   - The default kms build still succeeds: `build.sh --out out-base` (the A side of cycle 2);
     `out/` is untouched.

## 7. Pre-registered Pi test

**Question:** does a server that calls `pollNotify` wake `poll()` in well under a millisecond, for
single- and multi-fd sets? Does everything else keep today's ≤ 20 ms behaviour? Does kmscube reach
the display rate?

**Build (coordinator):**

1. Merge both `gpu-lane/poll-wake` branches, then `./scripts/rebuild-rpi4b-fast.sh --scope core`.
   It must be `core`: a clean-tree `auto` build ships a stale kernel.
2. Check that `strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep -x pollwake`
   prints `pollwake`.
3. Check that `nm .buildroot/_build/aarch64a72-generic-rpi4b/sysroot/lib/libphoenix.a | grep 'T pollNotify'`
   finds the stub.
4. Rebuild the tools against that sysroot, so no shim is used:
   - `tools/gpu-lane/pollwake/build.sh` should print `pollNotify from the sysroot libphoenix.a`;
   - `tools/gpu-lane/kms/build.sh --poll-notify --out out-poll`;
   - `tools/gpu-lane/kms/build.sh --out out-base`, the same source without `-DKMS_POLL_NOTIFY`,
     which is the A side of cycle 2.
5. Stage with `sudo install -m 755` into the live `fsid=0` export's `/bin`, then `cmp`. New names
   throughout, so the `-m3p2` binaries stay:

   | Built file (under `tools/gpu-lane/`) | Staged as |
   |---|---|
   | `pollwake/out/pollwake` | `pollwake` |
   | `kms/out-poll/rpi4-kms` | **`rpi4-kms-poll`** |
   | `kms/out-base/rpi4-kms` | **`rpi4-kms-base`** |
   | `kms/out-poll/kmstest` | `kmstest-poll` |

**Optional baseline, cycle 0, on the stock image before the merge.** The same `pollwake` binary
built with the worktree shim runs fine on the old kernel (`probe_rc=-22`). It shows the test
discriminates: every row reads like `legacy`. Run it with the same command as cycle 1, labelled
`pollwake-base`.

**Cycle 1 `pollwake`** (Bash `timeout: 600000`; the run itself takes about 15 s):

```
./scripts/test-cycle-psh-interact.sh --label pollwake --idle-secs 25 -- \
    "/bin/pollwake server" \
    "/bin/pollwake client all" \
    "/bin/pollwake client quit"
```

Grade on the `POLLWAKE` lines plus `./scripts/uart-summary.sh pollwake`. Allow ~1.3 % UART line
corruption: re-read a garbled line rather than count it.

| Line | Predicted (new kernel) | If instead… |
|---|---|---|
| `server ready … notify_supported=1 probe_rc=0` | as stated | `=0 probe_rc=-22`: the running kernel has no `pollNotify`. A stale image, so re-check the §8 `strings` step. `probe_rc=-1` (EPERM): the owner gate misfires on a port the caller created. That is a kernel bug. |
| `single dev=notify` | `events=60 errs=0 timeouts=0`, **p50 < 1000 µs** (expect 40–200), p99 < 3 ms, `per_event` ≈ 2, `notifies` ≈ events, `notify_errs=0` | p50 in the ms range with `notifies>0`: notifies do not reach the waiter (hash/mask, or the wait is not on the waiter path). p50 ≈ 8–10 ms with `notifies=0`: the server's probe failed. |
| `single dev=legacy` | p50 **3–12 ms**, max ≤ 21 ms, min < 2 ms, `per_event` ≥ 1.5 | max ≫ 21 ms: the fallback deadline is broken, and that is a regression. p50 < 1 ms: impossible without a notify, which would mean the measurement is wrong. |
| `pipe dev=notify` (card-like fd + idle pipe) | p50 < 1 ms | ≈ legacy numbers: the multi-fd waiter path is not taken. This is the kmscube shape. |
| `pipe dev=legacy` | as `single dev=legacy` | |
| `unix dev=notify` (+ idle AF_UNIX socket, the Xorg shape) | p50 < 1 ms | 0–20 ms: the mixed set still takes the AF_UNIX-queue wait. |
| `unix dev=legacy` | as legacy | |
| `pipewake` (legacy dev POLLOUT + a written pipe; posixsrv does not notify) | `events=60 errs=0 timeouts=0`, p50 3–12 ms, max ≤ 21 ms | max ≫ 21 ms: a non-notifying fd in a waiter set no longer re-polls. |
| `unixwake` (legacy dev + a written AF_UNIX socket) | p50 < 1 ms, max < 5 ms | ms-range: `pollwake_notifyUnix` is missing. On the old kernel this row was already fast, through the AF_UNIX queue. |
| `timeout` | `rc_nonzero=0 early=0`, elapsed 100–121 ms, `pollstatus_msgs` ≈ 80–160 (60 Hz notifies wake and re-query) | `early>0` or `rc_nonzero>0`: a spurious wake returned from `poll()`, which would be a bug. > 125 ms: the deadline is lost across wakes. |
| `eintr set=server` | `rc=0 errno=0 elapsed_ms≈3000 handler_ran=1`, as before the change | `rc=-1 errno=4 ≈1000`: server-only sets became interruptible, which is a semantics change (§5). |
| `eintr set=mixed` | `rc=-1 errno=4 elapsed_ms≈1000 handler_ran=1`, as before | `rc=0 ≈3000`: mixed sets lost the EINTR they had through `usocket_pollWait`, which is a regression. |
| `client done … fails=0`, server `quit … notify_errs=0`, 0 exceptions | as stated | |

On the old kernel (cycle 0), every `notify` row matches its `legacy` twin, `unix dev=*` is 0–20 ms,
`unixwake` is < 1 ms, and both `eintr` lines read as predicted above: the signal behaviour is the
same on both kernels.

**Cycle 2 `pollwake-kmscube`** (A/B on the new kernel; render server and flags as `m3p3b-kmscube`).
Run A and run B use the same kms source, and `-DKMS_POLL_NOTIFY` is the only difference:

```
./scripts/test-cycle-psh-interact.sh --label pollwake-kmscube --idle-secs 30 --max-cmd-secs 150 \
    --hdmi-dense-on 'Using display' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-base -G" \
    "/bin/kmscube-m3p3b -D /dev/dri/card0 -N -c 600" \
    "/bin/kmstest-poll quit" \
    "/bin/rpi4-kms-poll -G" \
    "/bin/kmscube-m3p3b -D /dev/dri/card0 -N -c 600" \
    "/bin/kmstest-poll stats"
```

| Observation | Predicted | If instead… |
|---|---|---|
| run A (`rpi4-kms-base`, not opted in) | `Rendered … ≈ 30 fps` (29–31), as in m3p3b | ≫ 31: the non-notifying server is being woken by something else (a hash-collision storm?). Investigate before believing run B. |
| `KMS srv poll_notify=1 rc=0` from `rpi4-kms-poll` | once | `=0 rc=-22`: stale kernel. |
| run B (`rpi4-kms-poll`) | **55–60 fps**, steady; HDMI shows the cube as before | 30–45: some flips still ride the fallback. Compare `KMSTEST stats` events against the flips, and check whether a flip event is pushed through a path other than `ev_push`. < 30: a regression. |

If `kmstest quit` leaves `/dev/kms` registered, the second server reclaims it: `claim_names`
already handles a dead owner.

**Optional cycle 3 `pollwake-qsdrm`:** the `m3p4-qsdrm` command with `/bin/rpi4-kms-poll -G`.
Predicted: 24.4 fps rises to **≥ 36 fps**, parity with `quakespasm-v3da` at 40.4.

## 8. Merge gate (coordinator, before `master`)

1. **Stock `--scope core` build** of the merged kernel + libphoenix (`rebuild-rpi4b-fast.sh --scope
   core`), with no warnings. Check `strings … loader.disk | grep -x pollwake` and the sysroot
   `T pollNotify`.
2. **Boot to `(psh)%`** on netboot, 0 exceptions (`uart-summary.sh`).
3. Cycle 1 passes (the table above) and cycle 2 run B reaches ≥ 55 fps.
4. **Showcase gate:** `nohup ./scripts/run-showcase-gate.sh --label pollwake-gate > gate.log 2>&1 &`.
   That runs the five games plus the glamor X desktop, 0 faults each, with HDMI frames checked by
   eye. The X desktop is the main thing to watch: X clients take the unchanged AF_UNIX-only path,
   but the X server's own set (AF_UNIX + input device fds) now uses the waiter. A slower or wedged
   desktop would point at `pollwake_notifyUnix`.
5. Record a manifest (`scripts/snapshot-integration-state.sh`). Then update P9 in KNOWN-ISSUES and
   G12 in M3.

## 9. Risks

- **Syscall-table merge order.** Another lane appending a syscall at the same time conflicts in
  `include/syscalls.h`, and the resolution decides the numbers. Rebuild both tools after the merged
  core build. `pollnotify-obj.sh` refuses a shim whose table does not match the sysroot's, so a
  wrong number cannot be linked silently.
- **Thread kill during `poll()` on server-only fds** now waits out the ≤ 20 ms deadline before
  the re-query's `proc_send` notices the exit. The old interruptible sleep was woken at once. That
  is the price of keeping the old signal semantics (§5); making the wait interruptible removes it.
- **Classification pass cost:** one extra fd lookup per fd per `poll()` call. This matters only for
  very large sets. Today's largest is X, with tens of fds.
- **A server that notifies too often** (for example on every packet) costs one syscall each time,
  plus one wake and one re-query for each watching poller. Use the armed pattern (§3) for
  high-rate sources.
- **Notify before the state update** (a server bug) degrades to the 20 ms fallback, never to a hang.
- **Not tested on the Pi** (a coordinator step). The 32-bit targets are not compiled (no toolchain
  here). The hash uses only 32-bit multiplies, and `id_t` is widened before the shift, so an
  `id_t` of `u32` (armv7r, sparcv8leon) is fine.

## Result + analysis — build 11 cycles (2026-09-27)

Build 11 has kernel `ee5939fc` and libphoenix `d40050c`, both on `master` (build-versions in each
log). Three cycles ran: `pollwake` (`rpi4b-uart-20260927-061124-pollwake.log`),
`pollwake-kmscube` (`…-061400-…`) and `pollwake-qsdrm` (`…-061949-…`).

### Pi numbers

**Cycle 1, the microbenchmark: PASS.** `POLLWAKE server ready … notify_supported=1 probe_rc=0`,
`client done fails=0`, `server quit events=244 notifies=244 notify_errs=0`.

| Row | p50 | p99 / max | Other | Verdict |
|---|---|---|---|---|
| `single dev=notify` | **38.1 µs** | 67.7 µs | 60 events, `per_event` 2.28, `notifies=60` | ✅ |
| `pipe dev=notify` (the kmscube shape) | **62.4 µs** | 89.7 µs | | ✅ |
| `unix dev=notify` (the Xorg shape) | **42.8 µs** | 65.3 µs | | ✅ |
| `single` / `pipe` / `unix dev=legacy` | 9.0 / 10.5 / 11.5 ms | ≤ 20.0 ms | min 0.1–0.8 ms | ✅ unchanged |
| `pipewake` | 9.2 ms | 19.5 ms | | ✅ |
| `unixwake` | 40.1 µs | 54.0 µs | | ✅ |
| `timeout` | | 100.0–100.0 ms | `rc_nonzero=0 early=0` | ✅ |
| `eintr set=mixed` | | | `rc=-1 errno=4 1000.0 ms handler_ran=1` | ✅ as predicted |
| `eintr set=server` | | | **`rc=-1 errno=4 1001.4 ms`** | ✗ the prediction was wrong, not the kernel (finding 3) |

**Cycle 2, kmscube A/B: the prediction failed.** The "render server" is `rpi4-v3d-async-m3p2 -r 1
-m serial -i`, as in m3p3b. `rpi4-kms-poll` printed `KMS srv poll_notify=1 rc=0`.

| | Server | Frames / time | fps | Vblanks spanned |
|---|---|---|---|---|
| A | `rpi4-kms-base -G` | 599 / 20.049 s | 29.88 | 1203 = 2 × 599 + 5 |
| B | `rpi4-kms-poll -G` | 599 / 19.966 s | **30.000** (every 2 s window 30.00x) | **1198 = 2 × 599 exactly** |

`kmstest stats` after B reported `applied=601 completed=601 fence_deferred=600 events=600
dropped=0 apply_us_max=5535`. `V3DA qstat` for the 600 frames of B reported bin 600 jobs / 65 ms
and render 600 jobs / 1144 ms, so the GPU spends **1.9 ms per frame** and sits idle about 94 % of
the time.

**Cycle 3, quakespasm-drm with `rpi4-kms-poll -G`:** `969 frames 33.5 seconds 29.0 fps`. The
m3p4 run without notify got 24.4. The `≥ 36` prediction failed. The only exceptions in that log
are ntpclient's C9 fault, which is unrelated.

### Finding 1: kmscube's 30 fps comes from the display server, not from poll

Every kmscube flip takes exactly two vblanks after poll-wake. The cause is in `rpi4-kms`: a flip
that has to wait for its in-fence cannot reach the next latch.

1. **Every kmscube flip is fence-deferred.** kmscube renders, calls `eglSwapBuffers`, and then
   calls `drmModePageFlip` without a fence. libdrm-phoenix's G13 `implicit_attach` gives the flip
   the BO's last render fence. The render has only just been submitted, so `commit()` finds the
   fence unsignalled and takes the deferred branch. That accounts for `fence_deferred=600` out of
   600 flips. (`kmstest` flips dumb BOs with no fence: `fence_deferred=0` in m2-kms-b, and it runs
   at 60.00 fps. That is the control.)
2. **Nothing tells the vblank thread that a commit is waiting.** `kms_vblank_thread` reads
   `gate = (c->pend == KMS_PEND_FENCE)` once, at the top of its loop, and only then picks its wait:
   `gate_us` (500 µs fence polls) when a commit is waiting, otherwise up to 20 ms for the next vblank.
   When the vblank that completes flip *k* is processed, `pend` is `NONE`, so the thread goes back
   into the 20 ms wait. kmscube's commit for frame *k+1* lands during that wait. `commit()` neither
   signals the thread nor sets a flag, and `vbl_wait`'s loop only tests `vb.head == vb.seen`. The
   field meant for this, `srv.evt_cond` ("dispatch → vblank thread: a fence-gated commit is
   waiting"), was declared in `kms.h` and never wired up.
3. **So the fence is first checked at the next vblank V+1.** It has signalled by then: the render
   takes about 2 ms. `kms_on_vblank` → `kms_try_apply` arms the commit at V+1 with `since ≈ 0`, sets
   `ptarget_seq = seq + 1`, and the commit completes, event included, at **V+2**. kmscube wakes about
   60 µs later (poll-wake), renders, flips, and the cycle repeats. That is **two vblanks per frame by
   construction, whatever the poll latency or GPU speed**. Run B is exactly 30.000.
4. **Run A adds only 5 vblanks in 599 frames.** The 20 ms quantum only matters when kmscube's wake
   plus its CPU work run past V+1. The event→commit window is a whole frame, not the latch margin,
   so this rarely happened. P9's 20 ms quantum was real and is fixed (quakespasm 24.4 → 29.0), but
   **it was never kmscube's binding constraint.** §1 and the M3/PLAN lines that attribute kmscube's
   29.9 fps to it are wrong on that point.

Hypotheses from the brief, checked against the code and logs:

| | Hypothesis | Verdict and evidence |
|---|---|---|
| (a) | `select()` takes another path or misses the waiter | **Refuted.** libphoenix `sys/select.c` builds a `pollfd` array and calls `poll()`, so it goes through the same `posix_poll`. {stdin (pl011-tty/posixsrv), card0} is two server fds, which takes the waiter path. The microbenchmark's `pipe dev=notify` row (62 µs) is exactly that shape. |
| (b) | The event arrives one frame late | **Confirmed, and it is the root cause**, with the mechanism above. The latch rule itself (E3, `guard_us=2000`) is fine. The commit is armed at the vblank instead of mid-frame, so `ptarget_seq` is always V+2. |
| (c) | GPU latency or serial mode | **Refuted for kmscube.** Render takes 1.9 ms per frame and the GPU is 94 % idle. `fence_deferred=600` shows the fence was pending at commit time, but that costs about 2 ms, not a frame. |
| (d) | libdrm-phoenix blocks in `drmHandleEvent` / `drmWaitVBlank` | **Refuted.** The server logs `read_dump … path=immediate`: reads are answered at once, never parked. kmscube's legacy loop never calls `drmWaitVBlank`. |
| (e) | 2-BO double buffering caps it at 30 | **Refuted as a cap.** kmscube's legacy loop renders only after the flip event (`drm-legacy.c`: draw → swap → `drmModePageFlip` → `select` until the event → release the old BO). That runs at 60 as long as event → commit → fence → arm fits in one frame minus the 2 ms guard. It needs about 2–4 ms. |
| — | The `SYNCOBJ_WAIT timeout=∞` in every frame of the m3p3b DRMPHX trace | **Not a stall.** It is Mesa's DRI swap throttle: it exports this frame's fence (`HANDLE_TO_FD`) and waits on the **previous** frame's fd, which alternates 8/9. That fence signalled long ago. It is named here so nobody chases it. |

### Finding 2: quakespasm-drm is limited by vsync'd flip latency plus GPU time per frame

The kmscube bug costs quakespasm almost nothing. Quake's GPU time per displayed frame is about
**19.5 ms** (14.2 s of render in a 25 s window at 29 fps, 1.25 jobs per frame, 57 % busy), so its
fence nearly always signals **after** V+1. From then on the existing `gate_us` poll is already
running and arms the commit mid-frame.

SDL KMSDRM keeps one flip in flight: swap N waits for flip N−1's event. So each frame costs about
L = (swap → fence ≈ 19.5 ms) + (wait for the next latch, 0–16.7 ms plus guard misses) ≈ 28–33 ms,
which is 30–35 fps. The observed 29.0 matches. **The ≥ 36 fps prediction was wrong** because it
counted only the poll quantum.

quakespasm-v3da's 40.4 fps (24.7 ms per frame) has no vsync'd flip in its loop. Getting above about
33 fps with vsync needs a shorter GPU frame (E2b render phase, bin∥render `overlap=0` in serial
mode), or a present path that does not wait on the pending flip. Both are out of scope here.

This is a model, not a measurement. The `flipstat` line added below measures it in the next cycle.

### Finding 3: why `eintr set=server` returned `EINTR` after 1001.4 ms

- `pollwake_wait` for a server-only set calls `proc_threadWait`, which is **not** interruptible
  (`_proc_threadEnqueue(…, 0)`). `threads_sigpost` does not wake the thread. It only sets
  `sigpend`, because `thread->interruptible == 0`.
- After at most `POLL_INTERVAL` (20 ms) the wait times out and `do_poll_iteration` re-queries the
  fd. That `proc_send` is `proc_sendEx(…, interruptible=1)`: the message is still `msg_waiting`, so
  it calls `proc_threadWaitInterruptible`. That checks `_threads_checkSignal` **before** enqueueing,
  finds SIGALRM pending with a handler installed, and returns `-EINTR`. The signal is still pending
  because delivery happens only on the return to user mode. `proc_sendEx` unlinks the message and
  returns `-EINTR`. `do_poll_iteration` passes it straight through (`if (err == -EINTR) return err;`),
  and `posix_poll` returns it.
- **The old kernel did the same, only sooner.** `38ad32cf`'s `posix_poll` slept in
  `proc_threadSleep`. `_proc_threadSleepAbs` sets `interruptible = 1`, so the signal woke it at once
  (the `-EINTR` return value was ignored). The very next `do_poll_iteration` → `proc_send` then
  returned `-EINTR` exactly as above. So the §5 and §7 baseline "`rc=0` ≈ 3000 ms, as before" was a
  code-reading error. It looked only at the sleep's ignored return value and missed the interruptible
  re-query. Cycle 0 (`pollwake-base`) was never run, so nothing on the Pi checked it.
- **This is the intended POSIX behaviour.** `poll()` shall fail with `EINTR` when a signal is caught
  before any requested event. Both kernels comply for both set shapes.
- **The one real change:** a server-only set now notices a signal up to 20 ms **later**. The 1.4 ms
  here is what was left of the uninterruptible chunk the signal landed in; `set=mixed` returns at
  1000.0. The §9 thread-kill risk is the same effect. **Recommended follow-up, not done here:** make
  `pollwake_wait` always use `proc_threadWaitInterruptible`. That one line restores the old immediate
  reaction without changing any semantics. Predicted result: `eintr set=server rc=-1 errno=4
  elapsed_ms≈1000.0`.

### The fix (tools, additive): `rpi4-kms` wakes its vblank thread for a deferred commit

The fix is in `tools/gpu-lane/kms` and applies to both build variants:

- `commit()`'s deferred branch now bumps `srv.gate_kick` (atomic, release) and calls
  `condSignal(srv.vbl_cond)`.
- The vblank thread reads `gate_kick` **before** `pend` at the top of its loop and passes it to
  `vbl_wait`, which returns 0 once it changes. The irq source checks it in its 2 ms `condWait`
  chunks; hvs checks it in its 250 µs polls. A vblank that has already arrived still takes priority.
- The between-vblanks branch now tests `c->pend == KMS_PEND_FENCE` instead of the stale `gate`, so
  the first look happens right after the kick. From the next iteration the thread polls the fence
  every `gate_us`.
- **Race-free:** `commit()` stores `pend` before the release bump. The thread's acquire load of
  `gate_kick` either sees the bump, and then also sees `pend`, or it does not, and then `vbl_wait`
  sees the bump. A lost `condSignal` is bounded by the existing 2 ms chunk.
- The unused `evt_cond` is replaced by `gate_kick`.
- `-K` restores the old behaviour, as an A/B control inside one binary. The `srv ready` line now
  prints `gate_us=… kick=…`.
- New server-local counters (`kms_stats_t` and the kmstest wire format are unchanged): when a client
  that flipped closes, the server prints one line
  `KMS srv flipstat client=… flips vbl1 vbl2 vbl3p deferred applied_gate applied_vblank kicks
  late_target q2a_us_avg q2a_us_max kick gate_us` and then resets the counters. `vblN` is the
  number of vblanks from commit acceptance to completion.

Built with `tools/gpu-lane/kms/build.sh`:

| Output | Build | Check |
|---|---|---|
| `out-gate/rpi4-kms` | `--poll-notify --out out-gate`, which printed `pollNotify from the sysroot libphoenix.a` (no shim) | 2 `bl pollNotify` (the probe and `ev_push`), stub `svc #0x6e`, 1 `bl condSignal` |
| `out-gate-base/rpi4-kms` | `--out out-gate-base` | 0 `bl pollNotify`, 1 `bl condSignal` |

`out-poll/` and `out-base/`, the binaries staged for build 11, are untouched. libdrm-phoenix is not
changed and does not include `kms.h`, so its host test was not re-run.

A note on the binaries staged for cycle 2: `out-poll/rpi4-kms` calls `pollNotify` from
`ev_push.constprop.0` at `0x401240`, behind the `m.poll_notify` test, and from the start-up probe
at `0x400674`. The stub is `svc #0x6e` (110). `out-base/rpi4-kms` links the same stub, because the
sysroot's syscall stubs come in together, but has **no** call site. So run B's notifies were real,
which is what the 38 µs microbenchmark wake and the unchanged 30.000 both require.

### Pre-registered next cycle

**Staging (coordinator):**

| Built file (under `tools/gpu-lane/kms/`) | Staged as |
|---|---|
| `out-gate/rpi4-kms` | **`rpi4-kms-gate`** |
| `out-gate-base/rpi4-kms` | **`rpi4-kms-gate-base`** |

Stage with `sudo install -m 755` into the live `fsid=0` export's `/bin`, then `cmp`. `kmstest-poll`
from build 11 still works: the protocol is unchanged.

Two cycles, split in advance so neither comes near the 10 min cap. For scale, the 7-command build-11
kmscube cycle spent about 260 s in capture windows plus about 90 s booting. Each run gets two
readouts, because the UART corrupts about 1.3 % of lines: the server's `KMS srv flipstat` line, and a
`KMSTEST stats` line from `kmstest-poll stats`. The two must agree: `flips` ≈ `completed` minus
whatever that server had completed before this client, and `deferred` = the `fence_deferred`
increase.

**Cycle `pollwake-gate`** (runs A and B plus the unfenced-flip check; Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label pollwake-gate --idle-secs 30 --max-cmd-secs 150 \
    --hdmi-dense-on 'Using display' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G -K" \
    "/bin/kmscube-m3p3b -D /dev/dri/card0 -N -c 600" \
    "/bin/kmstest-poll stats quit" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/kmscube-m3p3b -D /dev/dri/card0 -N -c 600" \
    "/bin/kmstest-poll stats" \
    "/bin/kmstest-poll -n 300 flip" \
    "/bin/kmstest-poll stats quit"
```

**Cycle `pollwake-gate-c`** (run C; Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label pollwake-gate-c --idle-secs 30 --max-cmd-secs 150 \
    --hdmi-dense-on 'Using display' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate-base -G" \
    "/bin/kmscube-m3p3b -D /dev/dri/card0 -N -c 600" \
    "/bin/kmstest-poll stats quit"
```

| Run | Predicted | If instead… |
|---|---|---|
| A: `-K` (old behaviour, notify on) | 29.9–30.0 fps. flipstat: `vbl2 ≥ 590`, `vbl1 ≈ 0`, `applied_vblank ≈ 600`, `applied_gate ≈ 0`, `kicks=0`, `kick=0` | Anything else: the new counters or the `-K` path are wrong. Fix the instrument before believing B. |
| B: the fix (kick + notify) | **58–60 fps**, steady. flipstat: `vbl1 ≥ 570`, `applied_gate ≈ deferred ≈ 600`, `applied_vblank ≈ 0`, `kicks ≈ 600`, `late_target ≈ 0`, `q2a_us_avg` 2000–5000. HDMI shows the cube as before, with no torn or partial frames: the fence still gates every flip. | **≈ 30 with `kicks=0`:** the kick never reaches `vbl_wait`. Check that `condSignal` on the ISR's cond wakes a thread waiter. **≈ 30 with `kicks ≈ 600` and `applied_vblank ≈ 600`:** the fence page publishes completion late, so the render server is the next suspect. **40–55 with `late_target` high:** arms land within 2 ms of the vblank. Read `q2a_us_avg` against the frame budget. **`vbl1` high but fps < 55:** the latency is client-side (kmscube CPU). |
| `kmstest-poll -n 300 flip` against B | 300 flips at 60.00 fps. Between the two `stats` lines `fence_deferred` is unchanged, because dumb BOs take the immediate path. That client's flipstat shows `vbl1 ≈ 300 deferred=0` | < 60: the kick path disturbed unfenced flips, which would be a regression |
| C: the fix without notify (`rpi4-kms-gate-base`) | 35–55 fps with jitter. Once commits can make V+1, the 0–20 ms poll quantum decides whether a frame gets there. flipstat: `vbl1` 30–80 % | ≈ 60: the poll quantum does not matter after the fix, so notify is not needed for kmscube (it still is for quakespasm, 24.4 → 29.0). ≈ 30: C behaves like A, and the fix only works together with notify. Explain before merging. |

**Cycle `pollwake-gate-qsdrm`:** the `pollwake-qsdrm` command with `/bin/rpi4-kms-gate -G`, followed
by `kmstest-poll stats quit`. The flipstat line prints when quakespasm's client closes.

| Observation | Predicted | If instead… |
|---|---|---|
| timedemo fps | **29–33**: no significant change from 29.0 (finding 2) | ≥ 36: the model is wrong and the kick bug did cost quake frames. Re-check with flipstat. |
| flipstat | `vbl2 + vbl3p ≫ vbl1`, `applied_gate ≈ deferred`, `applied_vblank` small, `q2a_us_avg` ≈ 15 000–22 000 (the GPU frame) | `q2a_us_avg ≪ 10 000` with `vbl2` dominant: frames miss the latch for another reason. Look at `late_target`. |

**Gate for adopting `rpi4-kms-gate` as the M3 server:** run B ≥ 55 fps, the kmstest flip rate
unchanged, 0 exceptions, and the cube renders cleanly on HDMI. The kernel follow-up (interruptible
`pollwake_wait`) is separate, and needs its own worktree branch plus the §8 gate.

## Merge gate — PASS (queue23, 2026-09-27 06:24–07:01); merged and pushed

Stock `--scope core` build 11 (`loader.disk` has `pollwake`, sysroot `T pollNotify`), boot 0 faults,
cycle 1 PASS (see the analysis above), and the six-app showcase gate `pollwake-gate`: X desktop,
quakespasm, Quake III, Quake II, vkQuake (torches present, 15/15 reference frames), STK — every app
`rc=0`, prompt, 0 faults, command echo, frames counted; HDMI looked at for all six (a late frame each:
the glamor X desktop with its GL window, XBill and xclock; every game in-level and correct). Kernel
`ee5939fc` and libphoenix `d40050c` pushed; manifest `2026-09-27-build11-poll-wake.md`. Cycle 2's
kmscube result was not a poll problem (rpi4-kms deferred-flip wake, fixed separately, A/B in queue26).
