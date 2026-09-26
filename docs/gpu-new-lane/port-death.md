# Port death: clients of a dead server hang forever

Follow-up to [E5](E5-deferred-reply.md) §1 conditions 1–3 and §5 item 3, and M1's biggest risk
(R1): a crash of `rpi4-v3d` or `rpi4-kms` must not wedge every client that was waiting on it.

**Status:** kernel fix written and compile-checked on branch `gpu-lane/port-death`
(worktree `/home/houp/.claude/jobs/c8f1289c/tmp/wt-kernel-portdeath`, based on `master` @
`d0fb0ca9`, not merged). Probe `tools/gpu-lane/portdeath/` written and built. Pi run
pre-registered in §6, not yet run.

| Condition | Verdict |
|---|---|
| 2: a dead server's parked clients hang | **fixed** (+ 2 latent use-after-frees that the fix would otherwise have exposed) |
| 3: rids are reused immediately | **fixed** (rids rotate per port) |
| 1: a parked client ignores SIGKILL | **not fixed**: not safe as a small change; design in §4 |

Line numbers are `master` @ `d0fb0ca9` unless marked "branch".

## 1. How a request is tracked, and why it hangs

A request's `kmsg_t` lives **on the sender's kernel stack** (`proc/msg.c:359`). It goes through
three states (`msg.c:25`):

| State | Where the kmsg is | How the sender waits (`proc_sendEx`, `msg.c:414-433`) |
|---|---|---|
| `msg_waiting` | on the port queue `p->kmessages` (`msg.c:410`) | interruptible for `msgSend` (`proc_send`), uninterruptible for `proc_sendUninterruptible` |
| `msg_received` | off the queue, in the **per-port rid tree** `p->rid` (`msg.c:530-531,574`; `ports.c:24-33`). Nothing records which thread or process received it. | **uninterruptible, no timeout** (`msg.c:415-422`, the upstream FIXME) |
| `msg_responded` / `msg_rejected` | sender returns 0 / `-EINVAL` (`msg.c:456-466`) | — |

At `msgRecv` the payload is mapped into the **receiver's** address space (`msg.c:561-570`,
`msg_map` `msg.c:35-185`): a window `ml->w` found with `vm_mapFind` (entry on the kernel object,
no amap: `vm/map.c:436-443`) into which the sender's pages are `page_map`ped directly. An
unaligned first/last page instead gets a **shadow page** `bp`/`ep` (`vm_pageAlloc`) plus a kernel
mapping `bvaddr`/`evaddr` of the sender's page in `kmap`, copied back at respond time
(`msg.c:621-636`). `msg_release` (`msg.c:188-240`) frees the shadow pages and kmap mappings and
unmaps the windows from **`proc_current()`'s** map.

**What happens when the server process dies** (`process_destroy`, `proc/process.c:77-126`; it
runs only when the last thread is gone and the last reference is dropped, from the reaper or
whichever thread drops it: `threads.c:384-440`):

1. `vm_mapDestroy` (`process.c:99-106`, `vm/map.c:1276-1301`) frees the page tables and the
   entries. `pmap_destroy` returns only descriptors with `DESCR_TABLE` set, i.e. table pages
   (`hal/aarch64/pmap.c:356-405`). Leaf pages are freed only through `amap_putanons`, which does
   nothing for a NULL amap (`vm/amap.c:48-54`), and a window entry has no amap. So **neither the
   sender's pages nor the shadow pages are freed**. The windows are gone.
2. `proc_portsDestroy` (`ports.c:177-192`) calls `port_put(p, 1)` per port: `closed = 1` and a
   broadcast to receivers (`ports.c:83-96`). The port is not freed while senders hold references.
3. **Received requests:** nothing walks `p->rid`. Their senders sleep uninterruptibly on
   `kmsg.threads` forever. `SIGKILL` cannot reach them (condition 1), and their process can
   never finish dying either.
4. **Queued requests:** a closed port rejects a queued request only when a receiver calls
   `msgRecv` (`msg.c:518-527`), one per call. The dead server has no receiver, so **queued senders
   hang too** (interruptibly for `msgSend`, forever for kernel `proc_sendUninterruptible`
   callers). E5 assumed they were rejected; they are not.

The same happens on `exec` without vfork (`process.c:1976-2003`): old map destroyed, ports
destroyed, pending clients never answered.

### Two latent use-after-frees on the same path

Any fix that wakes those senders makes each of them call `port_put(p, 0)`. Two existing bugs
would then turn into live memory corruption:

- **The owner's reference was dropped twice.** `proc_portCreate` gives the owner one reference,
  held by the entry on `owner->ports`. `proc_portDestroy` (`ports.c:157-174`) drops it with
  `port_put(p, 1)` but leaves the port **on the list** whenever clients still hold references. The
  owner's exit then drops it again in `proc_portsDestroy`, which can free the port under a client.
- **`port_put` locked a dead owner.** The final `port_put` does
  `proc_lockSet(&p->owner->lock)` (`ports.c:102-106`). A port outlives its owner whenever a client
  still holds a reference at the owner's exit, and `p->owner` then points at a freed `process_t`.
  Today a queued sender that is interrupted after its server died already reaches this.

A third, smaller one: `proc_respond` returns `-ENOENT` for an unknown rid **without** dropping the
reference it just took (`msg.c:611-619`; same in `msg-nommu.c`). Every stale or duplicate
`msgRespond` leaks a port reference, so the port is never freed.

## 2. The fix (branch `gpu-lane/port-death`, 4 commits)

| Commit | What |
|---|---|
| `ac5ee1a6` proc: drop a port's owner reference exactly once | New `port_disown()`: the owner's reference goes with the list entry, so only the caller that unlinks the port drops it, and it clears `p->owner`. `proc_portDestroy` and `proc_portsDestroy` both use this rule. `port_put` touches `p->owner` only if it is still set. |
| `9ce228c7` proc: fail pending requests when a port's owner dies | New `kmsg->dst` (the receiving process, set in `proc_recv`) and `proc_msgRejectPending(p, receiver)` in `msg.c` / `msg-nommu.c`, called by `proc_portsDestroy` for every port. It closes the port, fails every **queued** request, and fails every **received** request whose `dst` is the dead process. Senders get `-EINVAL`. |
| `ee437693` proc: release the port reference when msgRespond finds no request | `port_put` before `return -ENOENT` (both variants) |
| `3adc9950` proc: hand out message rids in rotation | `port_t.nextRid`: `proc_portRidAlloc` allocates from a rotating minimum, wrapping to 0 after `MAX_ID` (condition 3) |

`proc_msgRejectPending` (branch `msg.c:654-708`):

1. Under `p->spinlock`: `closed = 1`, then drain `p->kmessages`, setting `msg_rejected` and
   waking each sender. `closed` is set first, so no new request can queue after the drain
   (`proc_sendEx` checks it under the same lock, `msg.c:406`).
2. Under `p->lock` (the rid-tree lock `proc_portRidGet` uses): collect every kmsg with
   `dst == receiver` onto a local list (`kmsg->next` is free once a request has been received),
   then remove each from the tree. Removal from the tree is the ownership hand-off, exactly as in
   `proc_respond`: a concurrent `msgRespond` either got the kmsg first or gets `-ENOENT`.
3. Without `p->lock`: free the shadow pages and their kmap mappings (`msg_releaseShadow`, split out
   of `msg_release` unchanged), then set `msg_rejected` and wake the sender under
   `p->spinlock`. The kmsg is not touched after the wake: it is on the sender's stack.

The windows are **not** unmapped, and must not be: both callers have already destroyed the
address space (`vm_mapDestroy` precedes `proc_portsDestroy` in `process_destroy` and in
`process_execve`). `msg_release` cannot be reused here: in `process_destroy` `proc_current()` is
the reaper (process NULL, so `kmap`), and in `exec` `mapp` is already NULL. The shadow pages were
mapped only in the destroyed page tables, so freeing them afterwards is safe.

**Who is failed, and why only them.** `proc_portsDestroy(proc, unmapped)` passes the dying
process as `receiver` only when its map really was destroyed: `process_destroy` passes
`borrowed == 0` (a vfork child dies on its **parent's** map, which is not destroyed), and
`process_execve` passes 1. Requests are failed only if taken by the dead process's own threads.
None of those threads can still be running `proc_recv` or `proc_respond`: `process_destroy` runs
after every thread is gone, and `exec` kills and joins the others first. A request received by
**another, live** process (any process may `msgRecv` on any port: `proc_portGet` has no owner
check) is left alone. Its window still maps the sender's pages, so failing it would let the
sender free pages that process can still read and write. That is the C1 hazard class. Such a
receiver can still answer it.

**NOMMU** (`msg-nommu.c`) gets the same function. There the payloads are private copies
(`imapped`/`omapped`, `vm_mmap`'d into the receiver), which `vm_mapDestroy(p, mapp)` removes with
the dead process's entries. `kmsg->dst` is set **before** `proc_portRidAlloc`, because NOMMU
`proc_recv` still writes the kmsg after publishing its rid. That is why requests held by a live
receiver are not failed there either.

**Condition 3.** A stale rid now fails with `-ENOENT` unless the whole `0..INT_MAX` space has
cycled while it was held. `lib_idtreeAlloc(tree, n, min)` returns the lowest free id ≥ `min`
(`lib/idtree.c:136-181`), falling back to `min = 0` when everything above is taken. A generation
in the high bits was rejected because it produces negative rids. rids stay non-negative `int`s:
a grep of every sibling for narrowing casts, `u8`/`u16` rid fields, or rids used as indexes found
none, and `msg_rid_t` is `int` in both the kernel and libphoenix. The E5 §5 item-4 "responder
must be the owner" check was **not** added: the kernel log answers parked readers from the
writer's context (`log/log.c:145-169,289-306`), and posixsrv answers from other threads.

## 3. What changes for existing callers

- **A live server:** nothing changes. The new code runs only in `proc_portsDestroy`, when a
  process has no threads left. An explicit `portDestroy` keeps its semantics: the owner may still
  answer what it received, and receivers still reject queued requests one per `msgRecv`.
- **A server that dies or execs with requests pending:** the clients get `-EINVAL` at once,
  instead of hanging forever. This is the same error a request rejected at recv time on a closed
  port gets today. For a failed request, `o.data` is undefined: the server may have written part
  of it through a direct window, and whatever it wrote to a shadow page is discarded.
- **`proc_portDestroy` while clients hold references:** the port leaves the owner's list at
  once. It used to stay listed until the owner's exit. That changes nothing visible except that
  the double drop is gone.
- **rids** are no longer small. Servers that log rids will see large numbers. The E5 `ipcprobe`
  `ridreuse` prediction flips: on this kernel `rid_stale != rid_hold`, so expect
  `victim_got_stale=0 dup_respond_rc=1` (the "never sent" sentinel) and `hold_elapsed_ms≈500`.
  That is the **expected** result, not "inconclusive".
- Memory: `kmsg_t` gains one pointer (it lives on the sender's kernel stack); `port_t` gains an
  `int`.

## 4. Residual limits

- **Condition 1: a parked client still cannot be killed while its live server holds its
  request.** With condition 2 fixed, killing the **server** frees them. That remains the
  operational escape hatch.
  **Why it is not a small change:** to leave, the sender must take its kmsg out of the rid tree
  under `p->lock`, a sleeping lock it cannot take while it holds `p->spinlock` in the wait loop.
  Meanwhile `proc_recv` may still be between dequeueing it and publishing its rid (`msg.c:530-574`),
  and then the sender must stay. Worse, if the request has a window, the sender's pages are mapped
  into the **live** server, which may be using them. The sender cannot unmap them from its own
  context without pulling pages from under a running server thread, and if it leaves without
  unmapping, its exit frees pages the server still maps. Finally, the wait is uninterruptible on
  purpose: an interruptible wait returns at once for every pending signal, and would spin with
  `p->spinlock` held (the FIXME).
  **Smallest safe design, for later:** (a) a *killable* wait (woken by `thread->exit` only, not by
  ordinary signals; needs a second flag bit next to `thread->interruptible` in `threads.c`);
  (b) on kill, allow detaching only for a request **without windows** (raw-only; that is what E5
  already mandates for fence WAITs): drop the spinlock, take `p->lock`, remove the kmsg from the
  tree if it is still there, and return `-EINTR`. If it is not in the tree, a responder owns it:
  wait for `msg_responded`. The server's later `msgRespond` gets `-ENOENT`. Requests with windows
  would need heap-allocated kmsgs plus an "orphaned" state that the responder cleans up, which is
  a redesign.
- **Same-process client and server:** if a thread sends to a port that its own process serves,
  and the process is killed, that thread is parked uninterruptibly on a request that its own dead
  sibling received. `process_destroy` never runs, so neither the old code nor the fix frees it.
- **Explicit `portDestroy`, then death without answering:** the port is off the owner's list, so
  the owner's exit no longer walks it, and those clients still hang. Before the fix this path
  dropped the owner's reference twice. It is now a clean leak instead of a use-after-free. The
  E5 discipline ("never exit while holding rids") covers it.
- **Requests received by another live process, or by a vfork child on a borrowed map:** left as
  they were (§2).
- Queued requests on an explicitly destroyed port are still rejected only by `msgRecv` calls, as
  before.
- **Not run on hardware yet.** Only compile-checked (§5). NOMMU is compile-checked with `-DNOMMU`
  against the aarch64 headers, not on a real NOMMU target.

## 5. Validation done

- Every kernel C file built for the Pi (`log/ main.c posix/* proc/* syscalls.c syspage.c usrv.c
  vm/* lib/* perf/* test/* hal/aarch64/*`, which covers every includer of `proc.h`, `msg.h` and
  `ports.h`) passes `-fsyntax-only` under the **real** kernel flags
  (`-Wall -Wstrict-prototypes -Wundef -Wimplicit-fallthrough -Werror …`). The command line was
  recovered with `make -n -W proc/msg.c <obj>` in `.buildroot/phoenix-rtos-kernel`, as
  `scripts/syntax-check.sh` does, and run in the worktree with `-I.buildroot/_projects/<target>`.
  Wrapper: `/home/houp/.claude/jobs/c8f1289c/tmp/kcheck.sh`. It fails a file that adds an
  undeclared identifier (negative control).
- The same check passes **for each of the 4 commits on its own**, with `-DMSG_SEND_WATCHDOG=10`,
  and with `-DNOMMU` for `msg-nommu.c` and `ports.c`. `process.c` does not compile with `-DNOMMU`
  on aarch64 headers even on `master` (`arch/elf.h`), so it was not checked that way.
- `git merge-tree gpu-lane/e1-export gpu-lane/port-death` merges cleanly. e1 only adds
  `vm_objectUnexportPort()` in `port_put`'s free path, and `syscalls_portOwned` checks
  `owner == proc && !closed`, which is still correct now that a closed port's owner is NULL.
- **Still to do before `master`:** the stock `--scope core` build, the five-game + X desktop gate
  (PLAN rule 3) and §6.

## 6. Pre-registered Pi test

**Question:** on the fixed kernel, does every client parked on a dying server get an error
promptly, for queued requests, for received ones, and for every payload shape, with no fault and
no change to the normal path?

**Probe:** `tools/gpu-lane/portdeath/portdeath.c` (SPDX BSD-3-Clause, one static binary). One
process plays both sides. It **forks the server itself**, so there is no devfs node, no stale
`/dev` entry, and no psh `&`, and it gets the port number over a pipe. N client threads (default
6) each send one `mtDevCtl` request. The payload shape rotates: raw only (no mapping), an aligned
4 KiB in+out buffer (direct window), and an unaligned 5000-byte in+out buffer at offset 16 (shadow
pages at both ends). Each thread reports `rc` and its time.

| Mode | Server | Graded by |
|---|---|---|
| `respond` | receives N, fills `o.data` with a pattern, answers all, exits | control: `rc=0`, `odata_ok=1` (normal path and the `msg_release` refactor) |
| `stale` | answers A, receives B, answers **A's rid again** (`o.err=99`), then B (`o.err=2`) | condition 3 |
| `queued` | never calls `msgRecv`; exits after 200 ms | queued requests |
| `exit` | receives all N, writes into the output windows, starts a 2nd receiver blocked in `msgRecv`, `_exit(0)` | received requests, voluntary exit |
| `kill` | as `exit`, then the parent `SIGKILL`s it | received requests, killed server |

Client lines: `PORTDEATH client=<i> kind=<raw|aligned4k|unaligned5000> rc= o_err= odata_ok=
elapsed_ms= after_death_ms=` or `… HUNG waited_ms=`, then
`PORTDEATH result mode= n= returned= hung= rc_zero= rc_einval= rc_other= odata_bad=
max_after_death_ms= verdict=PASS|FAIL`. The client allows 3 s after `waitpid` reports the server
gone, then prints the verdict **before** exiting.

Build (clean with `-Wall -Wextra`):

```
S=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -std=gnu11 \
    --sysroot=$S/ -B$S/lib/ -o tools/gpu-lane/portdeath/portdeath \
    tools/gpu-lane/portdeath/portdeath.c -lpthread
sudo cp tools/gpu-lane/portdeath/portdeath /srv/phoenix-rpi4-nfs-gcc16/bin/   # coordinator only
```

**Run A: fixed kernel** (a `--scope core` image with `gpu-lane/port-death` merged. The fix adds
no string, so `strings loader.disk` cannot confirm it: check the kernel SHA in the manifest, and
the `stale` line below, which reads differently on a stale image):

```
./scripts/test-cycle-psh-interact.sh --label portdeath-fixed --idle-secs 8 --max-cmd-secs 30 -- \
    "/bin/portdeath respond" "/bin/portdeath stale" "/bin/portdeath queued" \
    "/bin/portdeath exit" "/bin/portdeath kill" "/bin/portdeath exit -n 16"
```

(Bash `timeout: 600000`.) Each command finishes in well under 5 s on the fixed kernel.

| Line | Predicted (fixed) | If instead… |
|---|---|---|
| `result mode=respond` | `returned=6 rc_zero=6 odata_bad=0 verdict=PASS` | regression in the normal path (the `msg_releaseShadow` split): stop, do not merge |
| `stale` | server: `rid_b = rid_a + 1`, `dup_respond_rc=-2`, `real_respond_rc=0`; client: `b_err=2 verdict=PASS` | `rid_b == rid_a`: rotation not in the image (stale build) |
| `result mode=queued` | `returned=6 rc_einval=6`, `max_after_death_ms` < 50 (0 or even negative is fine: the wake can land before `waitpid` returns), `verdict=PASS` | `hung>0`: the drain did not run; the image is stale or the port was not in the owner's list |
| `result mode=exit` / `kill` | `returned=6 rc_einval=6`, `max_after_death_ms` < 50, `verdict=PASS`; `server holding=6`; `kill` shows `signaled=1 sig=9` | `hung>0` on `raw` only: the dst match failed. `hung>0` on window kinds only: the release path is wrong. An EL1 fault / kernel panic: shadow-page release bug, so capture the dump, `addr2line` the PC and do not merge |
| `exit -n 16` | `returned=16 rc_einval=16 verdict=PASS` | as above (5 unaligned clients hold 4 shadow pages each) |
| whole log | no `Exception` dump, `uart-summary` unchanged, psh prompt after the last command | a fault anywhere is a fail |

**Run B (optional baseline): current kernel, one cycle, run LAST.** It shows the bug; it is not
needed to judge the fix.

```
./scripts/test-cycle-psh-interact.sh --label portdeath-baseline --idle-secs 8 --max-cmd-secs 20 -- \
    "/bin/portdeath respond" "/bin/portdeath stale" "/bin/portdeath exit"
```

Predicted on the current kernel: `respond` PASS. `stale`: `rid_b == rid_a`, `dup_respond_rc=0`,
`real_respond_rc=-2`, `b_err=99 verdict=FAIL` (misdelivered). `exit`: `returned=0 hung=6
verdict=FAIL`, then **the process never exits**: its client threads are parked uninterruptibly.
psh never returns, and the cycle ends by `--max-cmd-secs` and power-off. **Do not run `queued`
or `kill` after `exit` on the current kernel**, and do not run `queued` there at all. When a
queued client finally leaves (at `_exit`), its last `port_put` locks the freed server's
`process_t` (§1). That is a use-after-free on the old kernel and could corrupt the rest of the
cycle.

**Then:** record the results below, and update E5 §1 (condition 2 fixed, condition 3 fixed,
condition 1 standing) and the `ridreuse` prediction. If Run A passes and the `--scope core` +
showcase gate is green, the coordinator merges `gpu-lane/port-death` and deletes the branch.

## Result

*(to be filled after the cycle)*
