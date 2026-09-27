# poll-wake — readiness wake-up for `poll()` on server-backed descriptors

Closes register item **P9** ([KNOWN-ISSUES](../KNOWN-ISSUES.md)), M3 gap **G12**
([M3](M3-libdrm-phoenix.md)), and the kernel follow-ups 1–2 of [E5](E5-deferred-reply.md) §5.
Branches: `phoenix-rtos-kernel` **`gpu-lane/poll-wake`** and `libphoenix` **`gpu-lane/poll-wake`**
(worktrees, not merged). Tools: `tools/gpu-lane/pollwake/` (new) and `tools/gpu-lane/kms/`
(`--poll-notify`, additive).

**Status:** code written, the kernel links with `-Werror` from the worktree, and the tools build.
No Pi cycle has run. The run is pre-registered in §7 and the merge gate is in §8. Existing callers
keep their semantics, signal behaviour included (§5). Returning POSIX `EINTR` for sets of server-only
fds is left as a separate one-line decision.

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
