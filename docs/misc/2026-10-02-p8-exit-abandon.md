# P8: a client that exits while a server holds its request never dies

**Status 2026-10-02:** written and compile-checked, **not run on hardware, not merged.**

| Repo | Branch (worktree) | Commits on `master` |
|---|---|---|
| kernel | `proc-exit-msg-abandon` (`/home/houp/.claude/jobs/c8f1289c/tmp/wt-p8/kernel`) | `3c5f210b` killable wait · `07bd1384` diagnostic · `a8880d9d` abandon (on `fcae86d0`) |
| tests | `proc-exit-msg-abandon` (`/home/houp/.claude/jobs/c8f1289c/tmp/wt-p8/tests`) | `65184fe` `proc/test-msg-abandon` (on `907cc6a`) |

The three kernel commits are independent layers: `3c5f210b` alone changes nothing, `+07bd1384`
only adds one console line per stuck exit, `+a8880d9d` is the behaviour change. The coordinator can
merge the first two and hold the third.

Symptom: build 33, `b33-browser` / `b33-persist`. Every short `b6.sh start` run leaves its
WebProcess behind: `role=web pid=N main returned 0`, then `ps` shows it forever with PPID 1, 2
threads `sleep`, VMEM 327 M. One more clue from `b33-persist`: the two **persist** runs, whose
WebProcess lived ~90 s, are **not** among the lingering ones (pids 111, 277 exited; 674 … 827, the
3 s `start` runs, linger). So the stuck request is one a WebProcess issues early and which a page
that lived longer got answered. `ps -t` shows both threads asleep, which cannot say where.

Line numbers are `master` @ `fcae86d0` unless marked "branch".

## 1. Root cause

1. `exit()` → `proc_exit` (`proc/process.c:1641-1663`) → `proc_kill` (`proc/threads.c:1194-1201`)
   → `_proc_threadExit` for every thread (`threads.c:1130-1136`; `SIGKILL` reaches the same
   function through `_threads_sigdefault`, `threads.c:1717-1719`). It sets `exit = THREAD_END` and
   **wakes the thread only if `interruptible`**.
2. A thread whose request a server has `msgRecv`'d waits in `proc_sendEx` with the plain
   `proc_threadWait` (`proc/msg.c:510-517`, the upstream FIXME), enqueued with `interruptible = 0`
   (`threads.c:1409` → `1277`). So it stays asleep on `kmsg.threads`.
3. Threads end only by running: at the syscall exit (`syscalls.c:2442-2443` → `proc_threadEnd`), or
   when the scheduler picks a thread with `exit` set (`threads.c:632-646`). A sleeper is never
   picked.
4. The process is destroyed only when its last thread's `thread_destroy` drops the last reference
   (`threads.c:425-434` → `proc_put`, `process.c:128-145`). `process_destroy` (`process.c:77-125`) is
   what calls `posix_died` (`:88`, so the parent's `waitpid` returns) and `vm_mapDestroy` (`:101`,
   so the memory goes). Neither happens: the PID stays, reparented to init, with all its memory.
5. Why it waits uninterruptibly: the `kmsg_t` lives on the sender's kernel stack (`msg.c:453`).
   While the request is received the server reaches it through its rid (`ports.c:45-59`,
   `msg.c:713-744`), and its payload may be **mapped into the server** (`msg_map`, `msg.c:109-275`):
   a window of the sender's own pages, plus shadow pages owned by the kmsg. `thread_destroy` frees
   the stack (`threads.c:421`) and `vm_mapDestroy` the pages. An interruptible wait would also return
   at once for every pending signal and spin with `p->spinlock` held (the FIXME's own words).

The launcher's watchdog not printing `exit-stall` fits this (`exit()` itself completed), but
it does not prove it. A stall *inside* `exit()` before the syscall (an atexit handler or a stdio
flush blocked on a server) also prints nothing, and then no thread has `exit` set. The diagnostic
below tells the two apart (§5).

## 2. Diagnostic (`07bd1384`)

The sender itself reports, so no new context is needed and nothing prints from the scheduler (see
the `lib_printf`-from-scheduler self-deadlock memory) or under a spinlock.

- `proc_threadWaitKillable` (`3c5f210b`): like `proc_threadWait`, but an exit request ends it with
  `-EINTR`. A `killable:1` bit next to `interruptible` (same bitfield word, no layout change) is set
  only while enqueued by this wait and cleared on every dequeue. `_proc_threadExit` wakes a thread
  with either bit set. `threads_sigpost` still wakes only `interruptible` threads
  (`threads.c:1844`), so signals do not end it. `exit` is checked under `threads_common.spinlock`
  before sleeping, the lock `_proc_threadExit` holds, so the request cannot be missed.
- `proc_sendEx` uses it for a received request, and for any request of a
  `proc_sendUninterruptible` caller. Once woken by the exit request (`exiting`), the sender never
  uses the killable wait again: it would return at once every time. It waits in **1 s timed
  uninterruptible steps** (absolute deadline from `proc_gettime`, computed with no spinlock held),
  and after **2 s** prints once:

  ```
  proc: pid <P> (<path>) exit waits for tid <T> in msgSend to port <N> (server pid <S> tid <R> <server path>): received, type <mt>, <isz>/<osz> bytes in/out, <tail>
  proc: pid <P> (<path>) exit waits for tid <T> in msgSend to port <N>: not received yet, type <mt>
  ```

  `<tail>` = `payload mapped into the server` (has windows, cannot be abandoned) /
  `uninterruptible kernel request` / `being answered` (not in the rid tree: a responder holds it)
  / `no payload mapped` (only possible without `a8880d9d`). The receiving thread's tid is recorded
  in the kmsg by `proc_recv` (`kmsg->receiver`, MMU only). The server's pid and path are read through
  a `threads_findThread` reference on that thread, which keeps its process alive. Nothing is read
  through `kmsg->dst`, which can dangle when the receiver did not own the port. At most 16 lines per
  boot (unsynchronised bound, as `msg_reportDevice`).
- No ABI change. `threadinfo_t` / `ps -t` were not touched: the console line names more than a
  wait-channel field could (port, server, payload shape).

## 3. Fix (`a8880d9d`): abandon a received request that has no payload windows

This is port-death §4's "smallest safe design". In the exiting branch, before each step, the sender
calls `msg_detach` (branch `msg.c:530`), which takes `p->lock` and removes the kmsg from the rid
tree **if** all of these hold:

- the sender used `proc_send`/`msgSend`. `proc_sendUninterruptible` callers (`proc_close`,
  `proc/name.c:488`) are kernel paths that expect the response, so they keep waiting and are
  reported;
- the kmsg is in the rid tree: `idlinkage.id >= 0` (initialised to `-1` at send) and
  `lib_idtreeFind(&p->rid, id) == &kmsg->idlinkage`;
- `kmsg->i.w == NULL && kmsg->o.w == NULL`, i.e. nothing of the sender is mapped into the server
  (shadow pages exist only with a window).

Then it returns `-EINTR`. The thread ends at the syscall exit (or the kernel path unwinds, as it
already does for an interrupted queued request), and the process can die. The server's later
`msgRespond` gets `-ENOENT`, and since `ee437693` it drops its port reference. Otherwise
(being received, being answered, or windowed) it waits one more step and retries.

`proc_recv` is reordered so that it copies the message to the server's `msg_t` **before** it
publishes the rid, and uses the rid `proc_portRidAlloc` returned rather than reading
`kmsg->idlinkage` afterwards. On `master` it read the kmsg (`*rid = lib_idtreeId(...)`, the
`hal_memcpy` of `kmsg->msg`, the packed-offset fixups) **after** publishing (`msg.c:684-694`).
With detach, that would read a stack the sender has already left.

### Why a windowless request is safe to drop

What a received request leaves in the server, by payload kind:

| Kind | What the server holds | After detach |
|---|---|---|
| none (`i.data`/`o.data` NULL) | its own `msg_t` copy | nothing refers to the sender |
| packed (typed message, size ≤ `raw`) | its `msg_t.i.raw`/`o.raw` copy; the answer would be copied into `kmsg->msg.o.raw` by `proc_respond`, **after** `RidGet` | `RidGet` fails, nothing is written |
| same map (`msg_map` shortcut, `msg.c:161-163`, `w == NULL`) | the sender's own pointer. Only a same-process server, or a vfork child on its parent's map | the memory belongs to a map that outlives the sender: its own process (whose server thread is dying too) or the vfork parent |
| window (`w != NULL`) | the sender's pages mapped by PA, shadow pages, kernel views `bvaddr`/`evaddr` | **not detached** |

A kernel caller of `proc_send` with a kernel buffer is covered by the same rule: packed if small
(copied), otherwise a window (not detached).

### Races

| # | Race | Why it is safe |
|---|---|---|
| R1 | exit request vs. going to sleep | checked under `threads_common.spinlock` in `proc_threadWaitEx`, which `_proc_threadExit` holds |
| R2 | detach vs. `proc_recv` mid-flight (dequeued, `msg_received`, still mapping) | not in the tree yet, so no detach. Retried at the next 1 s step. `proc_recv` no longer touches the kmsg after publishing. A map failure rejects it and wakes the sender |
| R3 | detach vs. `proc_respond` | both remove the node under `p->lock` (`proc_portRidGet`). If respond wins, the sender waits for `msg_responded`, which comes after bounded kernel work with no server code in between |
| R4 | detach vs. `proc_msgRejectPending` (receiver died) | same hand-off under `p->lock` |
| R5 | wakeup while the sender is between steps (spinlock dropped) | `wakeupPending` is sticky; the state is re-read under `p->spinlock` before every wait |
| R6 | late or duplicate `msgRespond` after detach | rids rotate (`3adc9950`), so `-ENOENT` unless `0..INT_MAX` wrapped meanwhile |
| R7 | killable wait spinning with `p->spinlock` held | after the first `-EINTR` only timed uninterruptible waits are used (`exiting` flag) |
| R8 | a parked sender ghosted without running (`THREAD_END_NOW`) | only ever set on the current thread in fault paths (`process.c:314`, `vm/map.c:1231`); a parked sender gets `THREAD_END`, and a supervisor-mode thread with it is scheduled (`threads.c:636`) |
| R9 | a caught signal | does not wake a killable thread (`threads.c:1844` tests `interruptible`). Test `signal_does_not_abandon` |
| R10 | printing | from the sender's own syscall context, no spinlock held. `threads_findThread` takes a mutex only |

**Side effects, intended:** `exec` (`process_execve` kills and joins the other threads) and thread
cancellation (`SIGCANCEL` → `_proc_threadExit`) now also free a thread parked on a windowless
received request, instead of hanging.

**Semantics:** as for any interrupted call, the client cannot tell whether the server acted. The
server learns of the abandonment only from `-ENOENT` at answer time.

**NOMMU** (`msg-nommu.c`) is unchanged. It has its own `proc_sendEx`/`proc_recv`, and its NOMMU
`proc_recv` still writes the kmsg after publishing (port-death §2). `ports.c`/`msg-nommu.c` compile
with `-DNOMMU`.

## 4. Not covered: requests with payload windows (design for later)

If the browser's stuck request is windowed (`payload mapped into the server` in the gate), this
fix does not touch it. The design that would:

1. **Orphan.** The sender allocates a heap copy of its kmsg (layout `i`/`o` with `w`/`bp`/`ep`, the
   sizes, `abandoned = 1`, `bvaddr = evaddr = NULL`). Under `p->lock`, if its own node is still in
   the tree, it removes it and `lib_idtreeInsert`s the orphan **with the same id**. Then it unmaps
   its `bvaddr`/`evaddr` kernel views, so no kernel alias of its soon-freed end pages remains (a
   mismatched-attribute alias otherwise), and leaves.
2. **Pins.** Before publishing the orphan, the sender pins the pages the server's window maps: for
   each map entry overlapping `[CEIL(data), FLOOR(data+size))`, `amap_ref` + `amap_getanons` over
   the range, plus `vm_objectRef(entry->object)` for object pages that were never copied on write.
   `vm_objectPut` frees pages but sends no message (`vm/object.c:158-203`), so it can be called from
   any thread. This needs a new VM entry point (`vm_mapPin`/`vm_mapUnpin`, a bounded segment array).
   Pins are taken **before** the orphan is published: once it is in the tree, a responder may free
   it.
3. **Respond on an orphan:** `RidGet` → no copy-back. Unmap the windows from the responder's map
   **first**, then free the shadow pages (today's `msg_release` frees them first, a small existing
   ordering wart), unpin, `vm_kfree`, return `EOK`. **`proc_msgRejectPending` on an orphan**
   (receiver died, windows gone with its map): free the shadows, unpin, `vm_kfree`.
4. **Known holes to close first:**
   - (a) Pinning by VA pins the pages the sender maps **now**, which can differ from the PAs the
     server maps if the sender's mapping changed after `msgRecv` (a sibling thread's COW break on a
     shared anon, or munmap+mmap). `msg_map` would have to record the PAs, and the pin would check
     `pmap_resolve(sender, va)` against them.
   - (b) Kernel-memory payloads (a user thread's syscall sending a kernel buffer, possibly on its own
     kernel stack) cannot be pinned, so they would still wait.
   - (c) `msg_release` unmaps from `proc_current()`'s map, so a response from a *different* process
     than the receiver unmaps the wrong map. This exists today; with orphans it would leak a
     pinned window.
5. **Cost:** a server that never answers keeps exactly the payload pages, not the whole process.

**Rejected:**
- *The sender replaces the server's window pages with private copies.* That edits a live
  server's page tables from another process. ARM break-before-make leaves the PTE invalid in
  between, so a server thread touching its window faults (at EL1 inside a kernel copy). The
  server's map pointer is also not stable against the server dying.
- *Defer the sender's whole `vm_mapDestroy` until its orphans are answered.* Correct by
  construction and simpler, but it keeps the full 85 MB until the server answers. `vm_map_t` is
  embedded in `process_t`, so the `process_t` would have to outlive its PID: a bigger lifecycle
  change. A possible stopgap.

**Other residuals** (unchanged from port-death §4): a same-process server whose own process is
killed; a port destroyed explicitly whose owner then dies holding rids; a received request held by
another live process (all still wait, now with a report after 2 s). Also: the reaper itself calls
`posix_died` → file closes via `proc_sendUninterruptible` (`proc_close`); a close that is never
answered would stall all reaping. Seen nowhere yet, noted only.

## 5. Validation done (no hardware)

- `scripts/syntax-check.sh` (full `-O2 -Werror` compile to `/dev/null`, `SYNTAX_CHECK_SRC=` the
  worktree file) CLEAN for `proc/{threads,msg,ports,process,name,resource,userintr}.c`,
  `syscalls.c main.c usrv.c`, `posix/{posix,inet,pollwake}.c`, `vm/{map,object,amap}.c`,
  `log/log.c`. Also `proc/msg.c` with `-DMSG_SEND_WATCHDOG=10`, and `proc/{msg-nommu,ports}.c` with
  `-DNOMMU`.
- The test binary cross-compiles clean with `-O2 -Wall -Wextra -Werror` (command in §6).
- Side effect: `syntax-check.sh` stages the file it checks (and its directory's headers) into
  `.buildroot/phoenix-rtos-kernel/`, so the branch's `proc/*.c`/`proc/*.h` and the other checked
  files sit there now. A `diff` of `.buildroot` against `sources/` shows them until the next build
  rsyncs `sources/` over them. A `--scope core` build does that first. An `auto` build with clean
  siblings reuses cached objects and never compiles them.
- Not done: the stock `--scope core` build, a Pi run, and QEMU. Every claim about runtime
  behaviour above is a prediction.

## 6. Pi gate (pre-registered)

**Build and install** (coordinator; the test binary is not part of the rpi4b image):

```
S=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot; T=sources/phoenix-rtos-tests
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -Werror -std=gnu11 \
    --sysroot=$S/ -B$S/lib/ -I$T/unity -o /tmp/test-msg-abandon \
    $T/proc/test_msg_abandon.c $T/unity/unity.c $T/unity/unity_fixture.c -lpthread
sudo cp /tmp/test-msg-abandon /srv/phoenix-rpi4-nfs-gcc16/bin/test-msg-abandon
```

(from the tests branch, or the worktree path above; the binary does not depend on the kernel.)

**Run A: baseline, on the current build 33 image (kernel `fcae86d0`), BEFORE the rebuild.** It
shows that the test fails without the fix:

```
./scripts/test-cycle-psh-interact.sh --label p8-baseline --idle-secs 10 --max-cmd-secs 120 -- \
    "/bin/test-msg-abandon -v" "ps"
```
(Bash `timeout: 600000`.)

| Line | Predicted (old kernel) |
|---|---|
| `TEST(msg_abandon, exit_while_received)` | `FAIL: the client did not go: its thread is still in msgSend` |
| `TEST(msg_abandon, kill_while_received)` | same `FAIL` |
| `msg_abandon_keep` ×4 | `PASS` |
| summary | `6 Tests 2 Failures 0 Ignored` |
| `proc: pid … exit waits for` | **absent** (the string is not in that kernel) |
| `ps` | no `test-msg-abandon` child left (the tear-down answered the held request) |

**Then:** merge `proc-exit-msg-abandon` into the build, then `./scripts/rebuild-rpi4b-fast.sh --scope core`.
Prove the image has it:
`strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep -c "exit waits for tid"`
→ `2` (the two `lib_printf` formats; only commit `07bd1384` adds strings, so also check the kernel SHA
in the manifest for `a8880d9d`).

**Run B: fixed kernel.**

```
./scripts/test-cycle-psh-interact.sh --label p8-fixed --idle-secs 20 --max-cmd-secs 600 -- \
    "/bin/bash /usr/share/wpe-browser/b6.sh start" "ps" "mem" "/bin/test-msg-abandon -v" "ps"
```
(Run in the background with Bash `timeout: 1800000`, like `b33-browser`, whose `b6.sh start` took
449 s.) The browser goes **first** because the report is bounded to 16 lines per boot: with ≥ 16
stuck WebProcesses the test's own line would otherwise take a slot.

| Line | Predicted (fixed) | If instead … |
|---|---|---|
| test summary | `6 Tests 0 Failures 0 Ignored` | `exit/kill_while_received` FAIL: stale image (check the SHA) or detach broken. Any EL1 `Exception` dump: stop, addr2line the PC, do not merge |
| during `window_waits_for_answer` | exactly one `proc: pid <child> (/bin/test-msg-abandon) exit waits for tid <T> in msgSend to port <N> (server pid <test> tid <srv> /bin/test-msg-abandon): received, type 5, 0/4096 bytes in/out, payload mapped into the server` | absent: the killable wake does not fire, so the diagnostic is dead |
| `B6 start run=K rc=…` | `rc=0` ×16 (an `rc=3` is the separate early WebProcess stall, not this) | — |
| `ps` after `b6.sh start` | **no `/usr/bin/wpe-bro` row**; `mem` used back near the post-boot value | see below |

**Decision table for the browser** (the part the test cannot predict):

| Reading | Meaning | Next |
|---|---|---|
| no lingering WebProcess, no `exit waits` line from a `wpe-browser` pid | the stuck request had no windows: **P8 symptom fixed** | merge after the showcase gate |
| lingering + `exit waits … payload mapped into the server` naming server X | windowed request | §4 orphan design, or make X answer / the client not exit with that request outstanding |
| lingering + `uninterruptible kernel request` | a kernel `proc_sendUninterruptible` path | read the type (`mtClose` = 1) and the server |
| lingering + `not received yet` | a kernel uninterruptible send that the server never took | server-side bug |
| lingering + **no** `exit waits` line at all | not P8: the process never reached `sys_exit` (stall inside `exit()`), or its threads wait on something else (a kernel lock) | `-DMSG_SEND_WATCHDOG=10` build, or a stack sample |

## 7. Risks

- **Behaviour change for every `msgSend` caller:** a thread told to exit no longer waits for the
  answer to a windowless received request. Only dying threads are affected. The server's late answer
  now gets `-ENOENT`. A server that treats a failing `msgRespond` as fatal would now see it. None
  found in our tree, but it was not audited exhaustively.
- **Kernel `proc_send` callers on behalf of a dying thread** now see `-EINTR` in the received
  state too, which they already handle for the queued state. A caller that assumes "`-EINTR` ⇒ not
  received" would be wrong, but that was never guaranteed either.
- **`proc_recv` reorder:** the server's `msg_t` is now written before the rid exists, and is also
  written on the (rare) rid-allocation `-ENOMEM` path. That is harmless: the call fails.
- **Periodic wakeups:** a dying thread that cannot abandon its request wakes once per second until
  answered. Only threads already stuck forever are affected.
- **The `killable` bit** shares the bitfield word with `state`/`exit`/`interruptible`/`lentKstack`.
  It is written only under `threads_common.spinlock`. `lentKstack`'s unlocked write
  (`process_vforkThread`) was already a read-modify-write hazard on that word; this adds no new
  unlocked writer.
- **The report can destroy the server.** `msg_reportExitWait` holds a `threads_findThread` reference
  on the receiving thread. If that thread ends meanwhile and this reference is the last, the dying
  client's own thread runs `threads_put` → `thread_destroy` → possibly `process_destroy` of the
  server. That is the same pattern as `proc_join`, from an ordinary syscall context with no lock
  held, but expect that call stack.
- **Does not fix the browser if its request is windowed**, and the logs cannot tell yet. That is
  what the diagnostic is for.
