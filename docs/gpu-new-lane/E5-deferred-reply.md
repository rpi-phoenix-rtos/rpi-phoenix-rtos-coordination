# E5 — deferred reply, event blocking, IPC cost, shared fence page

Experiment E5 of the [new-lane plan](PLAN.md), for the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §4.3 (fence waits),
§4.5 (vblank events, implicit sync) and §6.

**Status:** kernel reading done, probe written and compiled. The first Pi run (2026-09-26 18:41)
produced no data: the run plan used a psh feature that does not exist (see "First run" below). The
probe is fixed and the run is re-registered in §3. Kernel read at `sources/phoenix-rtos-kernel` `master` @ `df9da09d`.

## Questions

- **(a)** Can a server `msgRecv` a request on one thread and `msgRespond` to it much later from
  **another** thread (the IRQ or worker thread that sees a fence signal), while the receiving thread
  keeps receiving?
- **(b)** Can a device server make `poll()` or `read()` on an event fd put the client to sleep until
  a vblank, with no spinning? Does the `block_ms` extension work for a device fd?
- **(c)** What does one `msgSend`→`msgRespond` round trip cost, with a small message and with a 4 KiB
  payload? (The design quotes ~20 µs. That figure is the "GPU compute dispatch floor" from
  `2026-08-22-concurrent-gpu…` M1-2b, not a measurement of IPC alone.)
- **(d)** Can a server give clients a read-only view of a page it writes, using what exists today?

## 1. What the kernel says (read, not run)

The source answers (a) and (b) on its own. The Pi run only has to measure (c) and the numbers
around (a), (b) and (d), and confirm the reading once.

### (a) Deferred reply from another thread: YES, same process. Three conditions apply.

| Fact | Where |
|---|---|
| `proc_recv` stores the kernel message in a **per-port** rid tree and gives its id to the receiver. Nothing records which thread received it. | `proc/msg.c:574,587`; `proc/ports.c:24-33` |
| `proc_respond` finds the message **by rid alone**, removes it from the tree and wakes the sender. It never checks the calling thread. | `proc/msg.c:605-653`; `proc/ports.c:36-50` |
| `syscalls_msgRespond` checks only that `msg` (and `o.data`) are in the caller's map. | `syscalls.c:1070-1094` |
| The calling thread matters in two places. `kmsg->src` is set to it (`msg.c:645`, cosmetic). `msg_release` unmaps the payload windows from **the responder's process map** (`msg.c:208-214`). Those windows were mapped into the **receiver's** process at recv time (`msg.c:561-570`). | `proc/msg.c` |
| **So any thread of the receiving process may respond. A different process must not:** `proc_portGet` has no owner check (`ports.c:53-72`), and a responder from another process would unmap a VA range from the wrong address space. | [read] |
| Production code already does this. posixsrv returns `NULL` from a handler and answers later from the timeout thread or from the writer's thread (`phoenix-rtos-posixsrv/posixsrv.c:216-221,296-305,310-340`; pty/pipe wake-ups). The kernel's own log reader answers from the log writer's context (`log/log.c:145-169,289-306`). psh's interactive input over `/dev/pts` has run on this every day since #117. | [read] |
| **How many requests can be outstanding:** the server imposes no limit. The kernel message lives **on the sender's kernel stack** (`msg.c:359`), so each outstanding request is one blocked client thread. Rids are `int`s up to `MAX_ID` (`lib/idtree.h:20`). | [read] |
| **What a deferred payload costs the server:** each parked request with `i.data`/`o.data` keeps a VA window in the server until it is answered. An unaligned head or tail adds a whole shadow page plus a kernel mapping (`msg.c:111-137,150-182`). `mtDevCtl` is **never** packed into `raw` (`msg.c:284-286,334-336`), so every pointer payload is page-mapped. | [read] |

**Condition 1: the client cannot be interrupted while its request is parked.** In `proc_sendEx` the
wait is interruptible only while the message is still queued (`msg_waiting`). Once a server has
received it, the wait is `proc_threadWait(…, 0, …)`: no timeout and no signals (`msg.c:414-423`, the
upstream FIXME: the message is on the sender's kernel stack, so the thread cannot leave). A client
parked in a fence WAIT that is never answered cannot be killed.

**Condition 2: if the server dies holding parked requests, their clients hang forever.** Closing a
port rejects only the messages still **queued** on `p->kmessages` (`msg.c:518-527`).
`proc_portsDestroy` (`ports.c:177-192`) never walks `p->rid`. Messages that were received but not
yet answered are never rejected, and their senders hold a port reference, so the port is not even
freed. **A crash of `rpi4-v3d` or `rpi4-kms` would therefore wedge every client that was waiting on
a fence or vblank, and those clients cannot be killed.** The probe does not test this on the Pi: it
would hang the cycle.

**Condition 3: rids are reused at once.** `lib_idtreeAlloc(…, 0)` hands out the **lowest free** id
(`lib/idtree.c:136-175`), so the rid freed by one response goes to the next message received. A late
or duplicate `msgRespond` with an old rid (for example a timeout path racing a fence-signal path)
does not fail. It **answers whichever request now holds that rid**. Only when no request holds it
does the call fail with `-ENOENT` (`msg.c:616-619`). Every parked request needs a single owner that
responds exactly once, claimed under the server's lock.

Also read: `proc_respond` ends with `hal_cpuReschedule` (`msg.c:648`), so the responding thread gives
up its CPU after **every** response. An IRQ thread that answers N waiters reschedules N times
[read; the cost is measured indirectly by `wait`].

### (b) Event blocking: `read()` YES today, `poll()` NO today (0–20 ms late, every time)

| Fact | Where |
|---|---|
| `open()` of a devfs node gives an `ftRegular` file (`posix.c:938`). If `mtOpen` answers with a value > 0, that value becomes this fd's `oid.id` (the `/dev/ptmx` multiplexer, `posix.c:921-928`). That provides **per-open state**, such as one DRM event queue per open file. | `posix/posix.c` |
| `poll()` = `sys_poll` → `posix_poll` (the libphoenix wrapper is `sys/select.c:34`). | |
| The `block_ms` fast path requires **exactly one fd of type `ftInetSocket`** (`posix.c:3218-3236`). Other fds go to `proc_threadSleep(POLL_INTERVAL)` and re-poll with `block_ms = 0` (`posix.c:3292-3295`). `POLL_INTERVAL` is **20 ms** (`posix.c:49`). | `posix/posix.c` |
| A poll set that includes an AF_UNIX socket blocks on the unix wait queue for at most 20 ms, then re-polls everything with `block_ms = 0` (`posix.c:3271-3276`). | |
| lwip honours `block_ms` by blocking **its own per-socket thread** in `lwip_select` (`phoenix-rtos-lwip/port/sockets.c:835-855,871-891`). It blocks inline and does not use a deferred reply. | |

So today `poll()` on a card fd **cannot** block in the server. It sends one `atPollStatus` snapshot,
sleeps 20 ms, and asks again. Wake-up comes 0–20 ms after the vblank, and the server sees about two
messages per event. The server cannot fix this by holding the snapshot reply: that would break
`poll(…, 0)`, and a multi-fd poll queries its fds one after another, so the others would stall.

**`read()` can block cleanly now.** `mtRead` reaches the server, which parks it and answers from the
vblank thread (condition set (a)). A blocking devctl (`drmWaitVBlank`) works the same way. Mesa's
`drmHandleEvent` reads the fd. Xorg, Weston and SDL multiplex with `poll()`.

### (c) Cost structure (read; the Pi measures the numbers)

- No payload (`i.raw`/`o.raw` only): the only copies are two 64-byte `raw` copies.
- Page-aligned 4 KiB `i.data`/`o.data`: one `page_map` of the client page into the server, **zero
  copy**, plus `vm_mapFind` and `vm_munmap` with its TLB work on respond.
- Unaligned (any `malloc`'d buffer, or an `ioctl()` struct by pointer): for each unaligned end,
  `vm_pageAlloc` + a kernel `vm_mmap` of the client page + `memcpy` in, and on respond `memcpy` back
  + `vm_munmap` + `vm_pageFree` (`msg.c:111-137,150-182,188-240,621-638`).
- Same-core versus cross-core **cannot be controlled**. There is no affinity syscall (the syscall
  list in `include/syscalls.h` has only `schedSet`/`schedGet`, which set policy and priority), and
  `threadinfo_t` has no CPU field. The probe measures unpinned and with 3 spinning threads.
- Side note [read]: `msg_map` resolves client PAs with `pmap_resolve` and never takes a fault
  (`msg.c:143`). That is safe only because non-lazy processes populate `mmap` eagerly
  (`process.c:227`, `vm/map.c:657-666`).

### (d) Fence page today: works through `MAP_PHYSMEM`, with no ownership

The server `mmap`s one page with `MAP_ANONYMOUS | MAP_CONTIGUOUS`. That page is object-backed, so its
PA cannot change, whereas an amap page could be replaced by copy-on-write if the server ever forked.
The server writes the page, which must be present before `va2pa` can resolve it
(`syscalls.c:1197-1204`), and sends the PA in a reply. A client maps it with
`MAP_PHYSMEM | MAP_ANONYMOUS`, `PROT_READ`. **Both sides must map it cached.** Two memory types on one
PA is the stale-dirty-line bug class (`done/2026-09-04-uncached-page-stale-cache-rootcause.md`).
Cached Normal memory is coherent between the A72 cores.

What is missing, all read from source (`syscalls.c:84-116`, `vm/object.c:382-390`, `vm/map.c:808-812`):
any process can map any PA, **writable too**. The mapping holds no page reference, so when the server
exits or frees the page, a client keeps a live view of a recycled page (the C1 hazard class). The
PA leaks physical layout. **E1's `vm_objectExport`** fixes all three: an oid-named object that holds
page references, has an enforced memory type, and grants a read-only mapping to the clients the
server chooses.

## 2. What was built

`tools/gpu-lane/ipcprobe/ipcprobe.c`: one static binary, SPDX BSD-3-Clause, no shipped component
touched.

- `ipcprobe server [-f] [-r recv_threads] [-t tick_us] [-v vblank_us]` **detaches itself**: it forks,
  and the parent returns to the shell once the child has registered `/dev/ipcprobe`. `-f` keeps it
  in the foreground. psh has no `&` (see "First run"). The server runs
  receive thread(s) (priority 4), a **worker** (priority 3; deadlines, timeouts, handoffs) and an
  **"irq" thread** (priority 2). Every `tick_us` (default 1000) the irq thread advances a fence seqno
  in the fence page. Every `vblank_us` (default 16667) it raises an emulated vblank. It answers WAIT,
  blocked `read()` and parked `poll` requests **itself**. The server serves `mtOpen` (per-open id),
  `mtClose`, `mtRead` (a 32-byte vblank event, parked until the next vblank) and `atPollStatus` (it
  honours `block_ms` if it ever arrives and counts every query). Every parked request has one owner,
  claimed under the lock. On quit the server answers all parked requests before the process exits.
  It prints a `IPCPROBE server ready …` banner and a `detached pid=` line. After that it prints one
  `IPCPROBE server first type= op=` line the **first** time each message type or devctl op arrives
  (at most about 30 lines in total), so a hang shows whether a request reached the server. The client
  prints `IPCPROBE client resolved oid=port/id` before its first call.
- `ipcprobe client <test>`: `rtt deferred timeout wait read poll fence fence-rw signal ridreuse
  fence-ro`, or `all`, or `quit`. Timestamps come from `cntvct_el0`, which EL0 can read on every core
  (`hal/aarch64/_init.S:325-336`), so server-side stamps returned in `o.raw` give one-way latencies.

| Test | What it does | Result line |
|---|---|---|
| `rtt` | 5000 round trips per variant: `small` (raw only), `i64_ptr` / `io64_ptr` (the shape of an `ioctl` call), `i4k`/`o4k` aligned and unaligned (+16 B), `io4k_aligned`, `small_handoff` (receiver → worker → respond), plus `small_hog3` / `i4k_aligned_hog3` (500 each, 3 spinning threads) | `IPCPROBE rtt <v> n= errs= p50_us= p90_us= p99_us= max_us= mean_us= srv_p50_us=` |
| `deferred` | 4 threads × 8 requests with delays of 5–200 ms. Odd requests carry 4 KiB unaligned both ways: the **worker** checksums `i.data` and fills `o.data`. Then an out-of-order pair: 300 ms sent first, 20 ms sent 30 ms later from another thread. | `IPCPROBE deferred ok=/32 bad_token= bad_err= early= late= odata_bad= idata_bad= max_delay_ms= max_late_ms= ooo= outstanding_max= recv_while_outstanding=` |
| `timeout` | 4 threads each WAIT on a seqno that never comes, timeout 100 ms, answered by the worker | `IPCPROBE timeout ok=/4 rc0= want=-110 elapsed_ms_min= elapsed_ms_max=` |
| `wait` | 4 threads × 50 WAITs for seq+3, answered by the irq thread. Wake latency = client wake − the irq thread's publish stamp. | `IPCPROBE wait ok=/200 errs= early= already_signalled= wake_p50_us= … wake_max_us=` |
| `read` | 60 blocking `read()`s of `/dev/ipcprobe` (≈1 s at 60 Hz). Latency = wake − vblank stamp. | `IPCPROBE read events= errs= missed= wake_p50_us= … wake_max_us=` |
| `poll` | 60 × `poll(fd, POLLIN, 1000)` then `read()`. Counts `atPollStatus` messages and how many carried `block_ms`. | `IPCPROBE poll events= errs= timeouts= wake_p50_us= … pollstatus_msgs= per_event= block_ms_seen=` |
| `fence` | Maps the fence page read-only and cached, then spin-reads it for 2 s (seqlock). Checks monotonicity and the achieved tick rate. Visibility = first observation − publish stamp. | `IPCPROBE fence pa= reads= seqlock_retries= nonmono= seq_rate_hz= expect_hz= ticks_skipped= vis_p50_us= …` |
| `fence-rw` | Maps the same PA **writable** and writes a magic value that the server then reads back | `IPCPROBE fence_rw map_rw= client_write_visible_to_server=` |
| `signal` | `alarm(1)` during a 3 s deferred request on the only thread | `IPCPROBE signal rc= elapsed_ms= handler_ran= handler_ms= interrupted=` |
| `ridreuse` | STALE (answered at once, rid remembered), then HOLD (500 ms). If HOLD got the same rid, the worker sends a late duplicate reply "to STALE" 50 ms later. Then a respond to a rid that was never issued. | `IPCPROBE ridreuse rid_stale= rid_hold= rc_stale= rc_hold= hold_got_token= hold_elapsed_ms= victim_got_stale= dup_respond_rc= real_respond_rc= badrid_rc=` |
| `fence-ro` | A forked child writes through its `PROT_READ` view | `IPCPROBE fence_ro child_exited= exit= signaled= sig= write_blocked= inconclusive=` |

Build (clean with `-Wall -Wextra`, no warnings; toolchain gcc against the tree sysroot, as
`tools/serrprobe/README.md` does):

```
S=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -std=gnu11 \
    --sysroot=$S/ -B$S/lib/ -o tools/gpu-lane/ipcprobe/ipcprobe \
    tools/gpu-lane/ipcprobe/ipcprobe.c -lpthread
sudo cp tools/gpu-lane/ipcprobe/ipcprobe /srv/phoenix-rpi4-nfs-gcc16/bin/   # coordinator only
```

## 3. Pre-registration (one netboot cycle)

**Question:** do the numbers match the reading above, and are RTT, `read()` wake-up and fence-WAIT
wake-up cheap enough for M1/M2 as designed?

**Method:** one netboot cycle with the stock image and the probe staged into the live NFS root's
`/bin`. No other change.

```
./scripts/test-cycle-psh-interact.sh --label e5-ipcprobe --idle-secs 25 -- \
    "/bin/ipcprobe server" \
    "/bin/ipcprobe client all" \
    "/bin/ipcprobe client quit"
```

(Bash `timeout: 600000`.) `ipcprobe server` returns to the prompt once its device is registered
(`server ready`, then `server detached`), so the next command runs after the idle window. `client all` prints a result line every ≤ 3 s. Its longest silent
stretch is the 3 s `signal` test, and the whole run takes ≈ 15 s. `fence-ro` runs last and is
**expected** to print one EL0 permission-fault dump for the child `ipcprobe` (twice, as every EL0
dump is). The dump names `process "/bin/ipcprobe"`: that is the forked **child**. The parent's
`IPCPROBE fence_ro …` and `IPCPROBE client done …` lines, which follow the dump, prove the parent
survived. psh passes quotes through literally, and none of these commands use quotes. Grade from
`./scripts/uart-summary.sh e5-ipcprobe` plus the `IPCPROBE` lines. Allow for the ~1.3 % UART line
corruption: re-read a garbled line rather than count it as a failure.

**Predictions and what each outcome means:**

| Line | Predicted | If instead… |
|---|---|---|
| `deferred` | `ok=32/32 bad_token=0 odata_bad=0 idata_bad=0 ooo=1 recv_while_outstanding>0 outstanding_max>=4`, `max_late_ms` a few ms | `bad_token>0` or `*_bad>0` means a kernel defect in rid/payload handling. M1 cannot rely on deferred reply until it is fixed (fallback: one blocked server thread per waiter). |
| `timeout` | `ok=4/4 rc0=-110`, 100–110 ms | Late by ≫10 ms: the worker's `condWait` timeout is coarse, so fence-WAIT timeouts have that granularity. |
| `wait` | `ok=200/200 early=0`, wake p50 ≈ 10–60 µs, p99 < 500 µs | p50 ≫ 100 µs: the IRQ-thread→client wake path (with `hal_cpuReschedule` per respond) is slow. The fast path (reading the fence page) becomes mandatory for per-frame waits, not just preferred. |
| `rtt small` | p50 5–25 µs | ≫ 20 µs: the design's per-submit budget (§4.3) and the "no sub-allocation needed" call (300 BOs/race) must be recomputed. |
| `rtt i4k_aligned` vs `i4k_unaligned`, `io64_ptr` | aligned ≈ small + 5–15 µs; unaligned and `io64_ptr` markedly above aligned (shadow page per end) | Unaligned ≈ aligned: the shadow path is cheap and payload alignment does not matter. Otherwise **libdrm-phoenix must put ioctl args in `i.raw`/`o.raw` (≤ 64 B) or page-aligned buffers**, never a struct by pointer. |
| `rtt small_handoff` | small + 10–40 µs | Large: answering from another thread has a real scheduling cost, which bounds how often async submit can reply late. |
| `read` | `events=60 errs=0 missed=0`, wake p50 < 100 µs | Confirms vblank delivery by blocking `read()` works now (M2 Stage A). Otherwise the deferred mtRead path has a problem. |
| `poll` | `block_ms_seen=0`, `per_event` 1–2 (1 when the previous wake has already drifted past the next vblank), wake p50 ≈ 5–12 ms, max ≈ 20 ms | `block_ms_seen>0` or wake < 1 ms: the reading of `posix.c:3223` is wrong, poll already blocks in the server, and the kernel change below is unnecessary. |
| `fence` | `nonmono=0`, `vis_p50_us` < 5, `seq_rate_hz` ≈ 1000 unless `usleep` is coarse (then `ticks_skipped>0`) | `nonmono>0`: the cached shared-PA view is not coherent. Stop and re-check memory attributes before any fence-page design. |
| `fence_rw` | `map_rw=1 client_write_visible_to_server=1` | This is the gap E1 closes. A refusal would mean some protection already exists. |
| `signal` | `rc=0 elapsed_ms≈3000 interrupted=0`. These two fields decide the question. `handler_ms` is informational: libphoenix `alarm()` runs a helper thread that `kill(getpid(), SIGALRM)`s with every signal blocked in itself (`unistd/alarm.c:39-73`), so the signal can only land on the parked main thread, and ≈3000 is expected. | `interrupted=1`: parked sends can be interrupted, and condition 1 is weaker than read. |
| `ridreuse` | `rid_stale == rid_hold`, `victim_got_stale=1 hold_elapsed_ms≈50 dup_respond_rc=0 real_respond_rc=-2 badrid_rc=-2` | `rid_stale ≠ rid_hold`: inconclusive, since rid allocation is not lowest-free here (then `dup_respond_rc=1`, the "never sent" sentinel, and `hold_elapsed_ms≈500`). |
| `fence_ro` | `write_blocked=1 inconclusive=0` and one EL0 dump for the child | `inconclusive=1` (child exit 3 or 4): the child never reached the store, so re-run. `write_blocked=0`: `PROT_READ` is not enforced on `MAP_PHYSMEM`, so clients can corrupt fence pages. E1 becomes a correctness item, not only a security one. |

## 4. What this means for M1 / M2

- **Fence waits (M1, §4.3).** A deferred `msgRespond` from the IRQ thread is **supported, and
  production already relies on it**. Required discipline:
  - Every WAIT carries a server-enforced timeout.
  - A GPU reset fails every waiter with an error.
  - Each parked request has exactly one owner and is answered exactly once, which guards against
    rid reuse.
  - WAIT messages carry no pointer payload (everything in `raw`), so parked waits hold no
    mappings or shadow pages.
  - The server must never exit while it holds rids.

  The fast path (a read-only fence page, no IPC) should serve the common case either way.
- **vblank / flip events (M2, §4.5).** Blocking `read()` and a blocking "wait vblank" devctl work
  today with no spinning. `poll()` on the card fd is quantised to 20 ms, which is unusable for a
  60 Hz event loop, so Xorg, Weston and SDL (M4/M6) need a kernel change (§5, items 1–2). M2 Stage A
  itself does not: `libdrm`'s `drmHandleEvent` reads, and our own clients can block in `read()`.
- **Implicit sync (§4.5).** If `rtt small` confirms ≈ 20 µs, one `rpi4-kms → rpi4-v3d` "fence for
  BO" query per flip costs ~0.1 % of a 16.7 ms frame and is **acceptable for M2**. The "resv page"
  removes even that, but it widens the shared-page surface. With today's `MAP_PHYSMEM` that surface
  is writable by anyone, so **build the resv page only on E1's export**, and use the per-flip query
  until then [inferred].
- **Per-open state:** answering `mtOpen` with a positive id gives each open file its own `oid.id`.
  That is the natural home for a DRM file's event queue and its handle namespace.

## 5. Kernel follow-ups this suggests (additive; not done here)

1. **`block_ms` for any single non-unix fd.** Widen the gate at `posix.c:3223-3236` beyond
   `ftInetSocket`. It is safe by construction: if a server ignores the high bits, the belt at
   `posix.c:3285-3290` sleeps the remainder. That holds provided every `atPollStatus` handler masks
   or ignores `i.attr.val`. A partial audit found that the handlers that matter on the Pi ignore it:
   `pl011-tty.c:984`, `usbkbd.c:696`, `usbmouse.c:449`, posixsrv `pipe.c:614` and `special.c:152`.
   The rest of `grep -rn atPollStatus sources/phoenix-rtos-devices` still needs auditing.
2. **Readiness wake-up for server-backed fds in multi-fd `poll()`.** The equivalent of
   `usocket_pollWait` for devices: a server tells the kernel that an oid's readiness changed, and
   pollers wake at once. Without it, any poll set that includes a card fd plus other fds sees
   DRM events up to 20 ms late. This is required for M4/M6.
3. **Reject received-but-unanswered rids when a port is destroyed.** Walk `p->rid` in
   `proc_portsDestroy`/`port_put(destroy)`, set `msg_rejected` and wake each sender. This removes
   the "server crash wedges its clients unkillably" failure (condition 2).
4. Optional hardening: check in `proc_respond` that the caller belongs to the port's owner process,
   and make rid allocation cycle (a generation or an increasing minimum) so that a stale rid fails
   instead of misdelivering.

## 6. Risks and caveats

- The probe's server is a model. Its "irq" thread is a priority-2 thread woken by `usleep`, not a
  real `interrupt()` handler. A real handler returns and signals a cond, and a thread then responds,
  so `wait`/`read` latencies here include one less wake-up hop than production will have [inferred].
- The tick period depends on `usleep` granularity. `ticks_skipped` and `seq_rate_hz` report the rate
  actually achieved.
- `poll` numbers are phase-locked. Poll starts right after the previous wake-up, so the latency
  saw-tooths across 0–20 ms, and the p50 depends on the phase.
- The server lets its threads finish any in-flight `msgRespond` for 50 ms before it exits. A client
  racing `quit` could still be caught by condition 2. Run `quit` last.
- `create_dev` of `/dev/ipcprobe` is not removed on exit. A second `ipcprobe server` in the same
  boot may fail to register.

## First run (2026-09-26 18:41, build 7 = kernel `df9da09d` + libphoenix `a844f10`): void, harness error

Log: `artifacts/rpi4b-uart/rpi4b-uart-20260926-184106-e5-ipcprobe.log`. `/bin/ipcprobe server &` printed
`IPCPROBE server ready port=22 … fence_pa=0x3b0c000 …`. Then **no `(psh)%` prompt came back**, and
`client all` and `client quit` printed nothing, not even `client start` or the 5 s
`cannot resolve` fallback, so neither client ever ran.

**Root cause: psh has no background jobs.** psh runs an external command through `psh_runfile`,
which `vfork`s, gives the terminal to the child and blocks in `waitpid`
(`phoenix-rtos-utils/psh/runfile/runfile.c:33-48`). Nothing in `pshapp.c` treats a trailing `&` as a
job request. The only `&` handling is the `&>` redirection in `psh_parseRedirections`
(`pshapp.c:1157`), which passes a bare `&` through unchanged as `argv[2]`. `getopt` stopped at it,
so the server ran in the **foreground** and psh waited for it for the rest of the cycle. The two
later commands were typed into a terminal that no reader owned. The kernel, the IPC path and the
probe's message handling were never exercised. The pre-registration's "psh has `&`" was wrong.
(Add this to the psh-limitation memory next to no `;`, `>` or `|`.)

**Fixes (probe only):**
1. `ipcprobe server` detaches itself: `fork` before any port, thread or mapping exists, then a pipe
   handshake so the parent exits only after the child has registered the device. A stray `&`
   argument is tolerated.
2. Traces added: the client prints `resolved oid=`, and the server prints one line the first time
   each message type or op arrives.
3. A latent bug found in review and fixed: the worker thread released its lock between scanning
   the parked list and calling `condWait`. A request parked in that gap (e.g. the first
   `small_handoff`) would have waited until the previous deadline, or forever if there was none,
   which would have hung the `rtt` test at `small_handoff`. The worker now waits without dropping
   the lock after a scan that found nothing due.

## Result

*(to be filled after the cycle)*
