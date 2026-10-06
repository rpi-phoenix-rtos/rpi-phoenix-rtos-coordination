# AF_UNIX poll() wake-up: regression test + code reading (2026-10-07)

**Trigger.** Build 37's wait trace (`b37-trace`, [W41](../inprogress/WEEK-2026-W41.md) 10-07 00:00):
during each 3–4 s rAF gap the UI has sent every `FrameDone`, but the web compositor is still waiting
for one. The navigation message at a start stall never arrives at all. The hypothesis was a lost or
late `poll()` wake-up on AF_UNIX, in the kernel or in libphoenix.

**Short answer from the code.** The kernel's AF_UNIX `poll()` path cannot hold a ready socket for
seconds. Every wait in `posix_poll` has a deadline of at most `POLL_INTERVAL` = 20 ms
(`posix/posix.c:53`, `:3396–3401`), and after it the socket is queried again directly
(`usocket_poll`, `posix/usocket.c:1739`). The reading found **two definite AF_UNIX bugs**. Both are
fixed on a branch, and neither can produce a gap of seconds:

- a lost wake-up, which costs up to 20 ms;
- a POLLOUT busy-spin.

For a multi-second stall, the poll path would need an **unbounded wait outside the timed sleep**.
The only one is a server query inside `do_poll_iteration` that does not come back (S5 below). The
new test settles the kernel side on the Pi: any case with `gt1s>0` or `rx_timeout=1` proves the
kernel guilty; all PASS with `max_us` ≤ ~20 ms clears it.

libphoenix adds nothing on top of the kernel here. `poll()` is the raw syscall, `select()` is built
on `poll()` (`libphoenix/sys/select.c:125`), there is no `ppoll`, and `sendmsg`/`recvmsg` only
flatten the iovec (`sys/socket.c:137–230`).

## 1. The test: `test-libc-unix-poll-wake`

`phoenix-rtos-tests` branch **`unix-poll-wake` @ `2a3c434`** (on master `2355e90`). The source is
`libc/unix-poll-wake/unix-poll-wake.c`, registered in `libc/Makefile` (`add_test_libc,
unix-poll-wake, -lpthread, -Wno-attribute-warning`, the same way as `test-libc-poll`) and in
`libc/test.yaml`. It installs as `/bin/test-libc-unix-poll-wake`. Unity group `unix_poll_wake`,
11 tests.

**How it measures.** A receiver sleeps in `poll(timeout = -1)`, then drains with
`recvmsg(MSG_DONTWAIT)`, the way WebKit's `readyReadHandler` does. The sender end is non-blocking and
runs WebKit's loop: `sendmsg` → `EAGAIN` → `poll(POLLOUT, -1)`. Framed messages go out as two iovecs
(header + body). Each 64-byte message carries `CLOCK_MONOTONIC` taken just before `sendmsg()`, which
is valid across processes. Latency is the `poll()` return time minus the send time. Sequence numbers
are kept per writer.

A case fails on any of:

- a message lost, duplicated, reordered or malformed;
- an fd that does not travel with its message;
- any wake slower than 1 s;
- a message not seen within 5 s of the last send. A watchdog then sends a QUIT message, falls back to
  `shutdown()` for a thread receiver and `SIGKILL` for a forked one, and reports `rx_timeout=1`;
- a sender that finds no room for 5 s (`tx_err=110`, ETIMEDOUT). A receiver that never wakes fills
  the ring. The bound is on time, not per `poll()`, because on the current kernel (S2) a SEQPACKET
  `poll(POLLOUT)` returns at once.

Options:

- `--strict` also fails a readiness-woken case on any wake ≥ 15 ms. Such a wake came from the 20 ms
  fallback timer, which means a notify was lost.
- `--count N` sets the number of messages per case (default 5000, sent with random 0–2 ms gaps).

| Test | Shape |
|---|---|
| `seqpacket_thread` (a) | SEQPACKET pair, receiver thread |
| `seqpacket_process` (b) | the same, with the receiver in a `fork()`ed child; stats come back over a pipe |
| `seqpacket_and_pipe` (c) | receiver polls {socket, pipe}: GLib's shape, since `glib2.cache:24 glib_cv_eventfd=no` makes GWakeup a posixsrv pipe. Every 50th message the sender goes quiet, writes a timestamped token into the pipe and stays quiet for 25 ms, so the **pipe's own wake latency** is reported as `pipe_*` |
| `stream_fds`, `dgram_fds`, `seqpacket_fds` (d) | each type, with an `SCM_RIGHTS` fd on every 10th message. Framed types check the fd per message; stream checks totals |
| `two_writers` (e) | two threads send to the same socket at once (+ fds) |
| `burst` (f) | 64 back-to-back messages, then a 2–6 ms pause; drained in a `recvmsg` loop |
| `crowd` | 4 idle threads sleep in `poll()` on other AF_UNIX pairs, so every notify finds sleepers. This is the case that **provokes suspect S1** |
| `cpu_load` | one equal-priority CPU-bound thread per CPU; measures the gap between waking up and actually running |
| `seqpacket_pollout` | fills a SEQPACKET ring with 4000-byte messages (WebKit's limit is 4096). If `poll(POLLOUT)` then says writable, that message must fit (**suspect S2**). A writer blocked in `poll(POLLOUT)` must wake when the ring drains |

**Output.** Each latency case prints one line, then a verdict line:

```
UPW case=<name> type=<t> expected= sent= recv= lost= dup= bad= fds_sent= fds_recv= fd_mismatch= wakes= empty_wakes=
    p50_us= p99_us= max_us= ge10ms= ge15ms= gt100ms= gt1s= complete= rx_timeout= rx_errs= tx_err= tx_eagain=
    [pipe_n= pipe_p50_us= pipe_max_us=] [idle_wakes=] [spins=] elapsed_ms=
UPW case=<name> verdict=PASS|FAIL
UPW case=seqpacket_pollout fill=4000 full_reports_pollout= pollout_then_fits= writer_rc= writer_revents= writer_wake_us= left=
```

**Checks done (no Pi).**

- `scripts/wt-syntax-check.sh phoenix-rtos-tests <wt> libc/unix-poll-wake/unix-poll-wake.c`
  (with `EXTRA=-Wno-attribute-warning`, the flag the Makefile adds) reported CLEAN.
- A trial link against the sysroot + `unity.a` succeeded. The binary is at
  `/home/houp/.claude/jobs/c8f1289c/tmp/wt-pollwake/link/test-libc-unix-poll-wake`, built outside
  the tree with `-O2`. It can be staged for a baseline run on the **current** kernel.
- Built and run on the Linux host (`--count 1000`): **11/11 PASS**. Linux reference numbers: p50
  31–35 µs, max ≤ 407 µs. `seqpacket_pollout`: `full_reports_pollout=0`, writer woke in 128 µs.
- A host build with the last message dropped: every case reported `rx_timeout=1 complete=0
  verdict=FAIL`, and the run finished in about 5.2 s per case. The watchdog works.
- A host build whose receiver never polls (a "deaf" receiver, the start-stall shape): every case
  FAILs in about 14 s with `rx_timeout=1 tx_err=110` and does not hang.

**Runtime** on the Pi: about 10 latency cases × (5000 × ~1.5–2 ms, because nanosleep rounds to the
1 ms tick) plus 2.5 s for the pipe tokens, so **~100–120 s**. Use `--max-cmd-secs 240` and Bash
`timeout: 600000`:

```
./scripts/test-cycle-psh-interact.sh --label upw --idle-secs 30 --max-cmd-secs 240 -- \
    "/bin/test-libc-unix-poll-wake -v"
```

### Pre-registered predictions

| Line | Current kernel (master `7127267b`) | Kernel `unix-poll-wake` | If instead… |
|---|---|---|---|
| a, b, d×3, e, f | PASS. p50 30–80 µs (`pollwake` measured 38–43 µs), p99 < 2 ms, `max_us` ≤ ~20 ms, `gt100ms=0` | same | **`gt1s>0` or `rx_timeout=1`: the kernel AF_UNIX/poll path loses wake-ups unboundedly. Hypothesis confirmed; profile it.** `ge15ms>0` without a crowd: someone else is polling AF_UNIX and S1 hit |
| c socket | p50 < 100 µs | same | ms range: the mixed-set waiter path does not get `pollwake_notifyUnix` |
| c `pipe_*` | `pipe_p50_us` ≈ 5–12 ms, `pipe_max_us` ≤ ~21 ms (P9: posixsrv does not notify) | same | ≫ 21 ms: posixsrv answers late (S5) |
| `crowd` | PASS, but **`ge10ms` ≥ 1 is likely** (S1: lost wakes bounded at 20 ms; `max_us` ≈ 20 000) | `ge10ms=0`, `max_us` < 5 ms | new kernel still ≥ 1: another lost-wake path |
| `cpu_load` | p99 ≤ ~5 ms | same | ≫ 10 ms: the scheduler does not round-robin equal priorities fairly (S7) |
| `seqpacket_pollout` | **FAIL**: `full_reports_pollout=1 pollout_then_fits=0` | PASS: `full_reports_pollout=0`, `writer_rc=1`, `writer_wake_us` < 1000 | |

If the whole suite passes on the Pi, with `max_us` ≤ 20 ms and the pipe ≤ 21 ms, then the 3–4 s gap
is **not** in the kernel's AF_UNIX poll path. Look next at S5–S8.

## 2. Code reading: every place a wake-up can be lost or delayed

The path: `syscalls_sys_poll` → `posix_poll` (`posix/posix.c:3306`), which classifies the set
(`:3333–3377`) and does a first `do_poll_iteration` (`:3224`). Then it loops: sleep, re-query. The
sleep depends on the set:

| Set | Sleeps on |
|---|---|
| set holding a server fd | a private `pollwake` waiter (`:3404–3412`) |
| AF_UNIX-only set | the shared AF_UNIX queue `usocket_pollWait` → `uchannel_pollWait` (`:3419–3425`) |
| single inet socket | lwip's `block_ms` |
| anything else | a timed sleep |

Readiness changes notify through `uchannel_pollNotify()` (`posix/uchannel.c:64`): a broadcast on the
shared queue plus `pollwake_notifyUnix()`. It is called on every write (`:200`, `:222`), read
(`:345`), shutdown (`:454`, `:468`) and resize (`:560`), and from usocket connect, accept and close.

| # | Where (master) | What | Effect | Verdict |
|---|---|---|---|---|
| **S1** | `posix/uchannel.c:51–85` (shared `uchannel_poll_common.queue`), `posix/posix.c:3419–3425`, with `proc/threads.c:1478–1504` (`_proc_threadWakeup` leaves `wakeupPending` only on an **empty** queue) and `:1519–1528` (broadcast) and `:1272–1279` (`_proc_threadEnqueue` consumes the sentinel) | **Readiness checked before the waiter is enqueued, on a queue shared system-wide.** An AF_UNIX-only poller queries (`usocket_poll` → `uchannel_pollRd` takes and drops the channel lock) and only then enqueues on the shared queue. A notify in that window: **(i)** if any *other* poller sleeps on the queue, the broadcast wakes it and leaves the head `NULL`, not `wakeupPending`, so ours enqueues and sleeps the full deadline; **(ii)** if nobody sleeps, it leaves one `wakeupPending`, which **any** poller that enqueues next takes, possibly not ours. The broadcaster never takes `uchannel_poll_common.lock` either, so that lock orders nothing. | ≤ 20 ms late per hit. Needs other AF_UNIX pollers asleep, which is the normal state in X and WebKit | **Definite bug, fixed** (kernel `6b62e39f`) |
| **S2** | `posix/uchannel.c:502` (`uchannel_pollWr`: framed → `free > sizeof(size_t)`), against `:212` (`uchannel_write` needs `len + 8` free) | **POLLOUT that the following write contradicts.** On a SEQPACKET/DGRAM ring with fewer than `len + 8` bytes free, `poll()` says writable and `sendmsg` says `EAGAIN`. WebKit's `ConnectionUnix.cpp:453–463` loops `sendmsg` → `EAGAIN` → `poll(POLLOUT, -1)`, so it **spins at 100 % CPU** until the reader has drained enough. That burns a core during the very backlog that caused it. | spin, not a delay | **Definite bug, fixed** (kernel `2310cde4`) |
| S3 | `posix/pollwake.c:99–155`, `posix/posix.c:3365–3374`, `:3265–3268` | The mixed-set waiter (AF_UNIX + server fds: **the WebKit IPC thread's GLib set**). It is listed before the first query and has a private queue, so a notify during the query leaves `wakeupPending` on *its own* queue. | — | Race-free, as argued in [poll-wake](../gpu-new-lane/poll-wake.md) §3; re-checked. The fix for S1 puts AF_UNIX-only sets on this path too |
| S4 | `posix/uchannel.c:235`, `:364` with `proc/threads.c:3069–3109` | Blocking `read`/`write` waits: `proc_lockWait` clears the mutex and enqueues under the same `lock->spinlock`, and the waker broadcasts while holding the mutex | — | Race-free |
| **S5** | `posix/posix.c:3238–3300`, `:3269` | **Serial, unbounded server queries.** `do_poll_iteration` sends one `atPollStatus` `proc_send` per server fd, in order, with **no timeout**. The whole `poll()` waits for each answer, even when the AF_UNIX fd in the same set is already readable. Every GLib loop has a posixsrv pipe in its set (GWakeup), so **a slow posixsrv stalls every GLib main loop in the system**, the IPC threads included. GWakeup's `write()` to the pipe is a blocking `proc_send` to posixsrv too. | **Unbounded**: as long as posixsrv takes to answer | **Hypothesis, the only kernel-adjacent path that can produce seconds.** posixsrv has **3** worker threads on its main port (`phoenix-rtos-posixsrv/srv.c:28`, `:56`). A worker blocks in synchronous forwards: `tmpfile.c:77` (`tmpfile_fw_op` → `msgSend` to the backing fs, NFS here) and `event.c:282`/`:302`. Three slow forwards stall every pipe/pty query. **Check with the build 38 profiler:** during a gap, is the web process's IPC thread (or the UI's sender) in `proc_send` to posixsrv's port, called from `posix_poll`/`posix_write`, and what are the 3 posixsrv workers waiting on? |
| S6 | `posix/posix.c:42–53`, posixsrv never calls `pollNotify` (P9) | Pipe readiness is seen only on the 20 ms re-query. Every WebKit cross-thread `RunLoop::dispatch` (UI main → IPC queue → socket → web IPC queue → compositing/main loop) wakes its target through such a pipe | 0–20 ms per hop, adds up over the hops of one FrameDone | Known (P9). Not seconds. Opting posixsrv pipes into `pollNotify` is the P9 follow-up |
| S7 | `proc/threads.c:244–254` (`_readyAdd`: no IPI to an idle CPU), `:291–339`, `hal/aarch64/arch/cpu.h:61` (`SYSTICK_INTERVAL 1000`), `hal/aarch64/cpu.c:491` (no low-power idle) | Gap between being woken and running: an idle CPU picks the thread up on its next 1 ms tick, a busy one at the waker's next reschedule. Strict priorities: a lower-priority IPC thread waits as long as higher-priority threads keep the CPUs busy | ≤ ~1 ms at equal priority; **unbounded under priority starvation** | Not a lost wake. `cpu_load` measures the equal-priority case. If WebKit sets thread priorities, check which ones (the profiler shows ready → running time) |
| S8 | `posix/posix.c:327–352` (`posix_getOpenFile` → `pinfo_find`, global `posix_common.lock` at `:180`/`:189`) | Each `poll()` takes the global posix lock 2× per fd for the classification pass and 2× per fd per iteration | contention only; nothing slow is held under it | No defect found |
| S9 | `posix/posix.c:3404–3412`, `posix/pollwake.c:110–112` | A notify while the poller re-queries leaves `wakeupPending`, so the next wait returns at once and the poller re-queries | one extra query, never a missed wake | Correct by design |

Nothing in the AF_UNIX data path itself loses data or descriptors: a frame and its fd pack travel
together (`uchannel.c:208–225`, `:292–324`). The test checks this in every framed case.

## 3. Fixes: `phoenix-rtos-kernel` branch `unix-poll-wake` (on master `7127267b`, which includes `prof-sampling`)

| SHA | Change |
|---|---|
| `6b62e39f` | **posix: wake AF_UNIX-only poll() sets through a waiter of their own.** `posix_poll` gives every set with an AF_UNIX or server fd a `pollwake` waiter, listed before the first query (`watchUnix` for AF_UNIX). The shared queue, `uchannel_pollWait`, `uchannel_pollInit` and `usocket_pollWait` are removed, and `uchannel_pollNotify` = `pollwake_notifyUnix()`. The `hasUnix` plumbing in `do_poll_iteration` goes away. Each AF_UNIX poller still gets one wake per AF_UNIX state change, as with the broadcast. The wait stays interruptible. Fixes S1 |
| `2310cde4` | **posix: report POLLOUT on a framed AF_UNIX socket only when a message fits.** `uchannel_pollWr`: SEQPACKET/DGRAM is writable while at most ¼ of the ring is in use (Linux's `unix_writable` rule), so any frame up to ¾ of the ring fits. Streams are unchanged (`free > 0`). Fixes S2 |

**Costs and edges, for review:**

- `6b62e39f` is not free. The old broadcast walked only the AF_UNIX pollers that were *asleep*.
  `pollwake_notifyUnix()` walks *every* registered waiter, server-only sets included, under the
  global `pollwake` spinlock. Every AF_UNIX-only `poll()` now also takes that spinlock twice, to
  register and to unregister.
- It **changes the X desktop's AF_UNIX-only path**, which [poll-wake](../gpu-new-lane/poll-wake.md)
  §5 deliberately left alone. The merge gate is therefore poll-wake's §8: stock `--scope core`, the
  `uchannel.poll` strings check below, then the full showcase gate with the glamor X desktop
  watched.
- When this merges, poll-wake.md §5 ("Unchanged paths: pure AF_UNIX sets") and the P9 row need a ↩
  correction.
- `2310cde4`: an unconnected DGRAM socket (no `tx`) is unaffected. A single frame larger than ¾ of
  its ring can still get POLLOUT and then `EAGAIN`, so it would still spin. That is accepted:
  Linux has the same edge, and WebKit's frames are ≤ 4 KB against a 64 KB ring.

Checks done (no Pi):

- `wt-syntax-check.sh` reported CLEAN for `posix/posix.c`, `posix/uchannel.c`, `posix/usocket.c` and
  `posix/pollwake.c`.
- The whole kernel linked from the worktree into a scratch prefix (`make … PREFIX_BUILD=<tmp>`):
  rc=0, no warnings, `nm` shows no `uchannel_pollWait`/`usocket_pollWait`.
- **loader.disk check after the build:** `strings … loader.disk | grep -c uchannel.poll` must print
  **0**. The shared queue's spinlock name is gone; master has it.

Tests in `test-libc-unix-socket` that touch POLLOUT were re-read. Every framed expectation is about
an idle socket (POLLOUT) or a ring full down to the byte (no POLLOUT), so all of them still hold
under the ¼ rule.

**Rebuild scope:** kernel + phoenix-rtos-tests ⇒ **`--scope core`**. A clean-tree `auto` build
ships a stale kernel. Merge the tests branch too: the test runs on either kernel, and on the current
one it is the baseline (`seqpacket_pollout` FAILs there by design).

**What these fixes do not explain.** S1 costs at most 20 ms per hit, and S2 is a spin, not a delay.
Neither yields a 3–4 s gap. If the Pi run of this test passes on the current kernel, **S5 (posixsrv
starvation) is the leading kernel-adjacent explanation**, followed by S7 (priority starvation). The
build 38 profiler (wait/wake/msg events) can tell them apart: look for the thread that receives
`FrameDone`, and for what it is blocked in during a gap.
