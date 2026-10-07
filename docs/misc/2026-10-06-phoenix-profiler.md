# A system-wide profiler for Phoenix-RTOS (2026-10-06)

Owner request (2026-10-06): a generic profiling infrastructure that shows, for every thread of every
process over a time window, **where it runs** and, when it is blocked, **what it is blocked on, for how
long and who woke it**. The goal is to collect that data systematically instead of guessing in stages.

Motivating cases on the Pi:
1. On the WPE WebGL page, requestAnimationFrame stops for about 3.3 s again and again.
2. A rare browser start stall: a fresh WebProcess never takes its first navigation.
3. The browser is slow in general: JS runs about 12x slower than on the host, and the display path
   has its own costs.

Status: designed, implemented and compile-checked on branches, **not built into an image and
not run on the Pi** (no Pi cycles in this session). See §6 for the first runs.

---

## 1. What already exists

| facility | where | what it gives | what it lacks for this job |
|---|---|---|---|
| **upstream kernel trace** (`perf_mode_trace`) | `phoenix-rtos-kernel/perf/` (trace.c, trace-events.h, buffer-mem.c, tsdl/metadata), syscalls `sys_perf_start/read/stop/finish` (`syscalls.c:758`) | A CTF event stream per CPU: meta and event channels (1 MB + 4 MB per CPU, allocated at `perf_start`, freed at `perf_finish`). Events cover scheduling (scheduling/preempted/enqueued/waking), syscall enter/exit (number + tid), kernel lock set/acquired/clear with lock names, IRQ enter/exit (all IRQs except the timer), thread create/end, process exec/kill and priority. The rolling mode keeps the last window. | No **samples** (no PC, no stack). A block is recorded only as `thread_enqueued(tid)`: there is no wait object, no deadline, no syscall arguments and no code location, and nothing says **who woke** the thread. There are no **message** events, so a client's wait cannot be tied to the server thread that serves it. The `arg/sz` of `perf_start` is ignored. |
| **psh `perf`** | `phoenix-rtos-utils/psh/perf/perf.c` + **libtrace** (`phoenix-rtos-corelibs/libtrace`) | `perf -m trace -o DIR -t MS` writes `channel_meta<cpu>`/`channel_event<cpu>`; `-j start/stop` gives a background rolling trace. Already built into the Pi's psh. | `startTrace()` hardcodes `perf_start(..., NULL, 0)`, so no config is passed. Its read buffer (512 KB) is malloc'd and never touched: see §5 R4. |
| **hostutils trace** | `phoenix-rtos-hostutils/trace/convert.sh` → `ctf_to_proto` (babeltrace2 + Perfetto protobuf) | Perfetto timeline of the CTF trace: thread states, syscalls and locks. | `lower()` raises `ValueError` on array fields, so with `--debug-annotations` any event with an address list ended the conversion. Fixed on a branch (§3). Needs babeltrace2 and python3-bt2, which this host does not have. |
| **libdbg** | `phoenix-rtos-corelibs/libdbg` | In-process crash backtrace (`dbg_init`), `dbg_backtrace()`, and a SIGALRM hang watchdog (`dbg_arm_watchdog`). Uses frame pointers and needs `-fno-omit-frame-pointer`. | One process at a time, built into it, and fp-only. It cannot see a thread blocked in the kernel or what that thread waits on. |
| **kernel backtrace (B2)** | `hal/aarch64/exceptions.c:211` `hal_exceptionsBacktrace`; kernel built `-fno-omit-frame-pointer` (Makefile:30) | pc + lr + x29 chain on an **EL1 fault**. | Runs only on a fault. Its walk is not reusable from IRQ context (no `at` probe). |
| **threadsinfo** | `proc_threadsInfo`, `threadinfo_t` (`include/sysinfo.h:55`) | State, CPU time, priority, CPU id, and `wait` = the longest ready→running latency (`maxWait`). psh `ps`/`top`. | A snapshot. It shows neither where a thread is nor what it waits on. |
| **launcher stall report** | `phoenix-rtos-ports/webkit_wpe/files/launcher/wpe-browser.cpp:205-512` | On a main-loop stall: threadsinfo per thread (CPU delta, wait), then SIGUSR2 to every thread and its pc/lr/fp/sp from the handler. Stacks are recovered by **scanning for return addresses** after BL/BLR, because WebKit is built without frame pointers. | Works only in the browser, only after 10 s+, only on the main loop, and only on threads that can take a signal. It cannot see what a thread in the kernel waits on, or across processes. |
| **MSG_SEND_WATCHDOG** | `proc/msg.c:590` (compile-time) | One line naming a msgSend stuck as waiting or received. | Diagnostic only, one line, and needs a rebuild. |

The building block was already there: upstream has a per-CPU, lock-protected, zero-cost-when-off
event trace with a userspace reader and a host converter. What it lacked is listed in the table's
last column. The design below **extends that trace** rather than building a parallel one.

---

## 2. Design

### Kernel (`phoenix-rtos-kernel` branch `prof-sampling`)

**No new syscall.** The profiler adds:
- new **CTF event ids 0x40-0x45** in the existing stream, declared in `perf/tsdl/metadata`;
- one `perf_start` **flag**, `PERF_TRACE_FLAG_SAMPLE`;
- an optional `perf_start` argument, `perf_trace_cfg_t`, through the `arg/sz` that upstream already
  validates and passes along.

Syscall numbering is therefore untouched. `upstream-sync-2026-10-06` appends `sys_cpuTime` at the
end, and this branch adds nothing after it.

| event | emitted at | fields |
|---|---|---|
| `thread_sample` 0x40 | `threads_timeintr()` (every CPU's timer IRQ), every `samplePeriodUs` per CPU (default 1 ms) | tid, mode (user / kernel-for-a-process / kernel thread), kernel pc + kernel fp chain (≤12), user pc/lr/sp/fp, user fp chain (≤`depth`), and `nstack` words copied from user sp (`sampleStack` bytes) |
| `thread_wait` 0x41 | `_threads_enqueued()`, the single point every queue wait and every sleep passes through | tid, flags (bit 0: **already waiting at trace start**), wait queue (kernel address cropped to u32; 0 = sleep), µs left to the deadline, **syscall args x0..x3** from the user frame at the top of the kstack, kernel fp chain, the same user part (`waitStack` bytes) |
| `thread_wakeup` 0x42 | `_proc_threadWakeup` (explicit), `threads_timeintr` (timeout), `_thread_interrupt` (signal/exit), `_proc_lockUnlock` (lock handover) | tid, **waker tid**, cause |
| `msg_send` 0x43 / `msg_recv` 0x44 / `msg_respond` 0x45 | `proc_sendEx`, `proc_recv`, `proc_respond` | tid, port, (type, sender pid), **mid** = kernel address of the kmsg, which ties a send to the server thread that received it and to the responder |

At `perf_start`, after upstream's thread list, a `thread_wait` with flag 1 is emitted for **every
thread that is already asleep**. Its kernel chain comes from the saved context, and it also carries
the queue, the deadline, the syscall args and the user pc/lr. For case 2 this matters most: a trace
started after a stall began still names what each thread waits on.

The reason for a wait is derived on the reader side from three things already in the trace: the
syscall in flight (upstream's `syscall_enter`), its arguments (msgSend port, futex address,
mutex/cond handle) and the first kernel frame outside the wait plumbing. This adds **no
per-call-site edits**. Only the message path got explicit events, because the server side
(recv/respond) cannot be inferred otherwise.

**HAL** (`hal/aarch64`, opt-in via `HAL_PERF_FRAMES`):
- `hal_cpuGetPC/LR/FP/Arg` read a `cpu_context_t`.
- `hal_cpuCanRead(va)` is an `at s1e1r` probe. It takes no lock, saves and restores PAR_EL1, and
  **rejects Device memory**, so a garbage x29 that points at a mapped MMIO page (such as the
  mailbox) is never read.
- `hal_cpuBacktrace()` is a bounded AAPCS64 frame-record walk: 16-aligned, ascending, inside
  `[lo,hi)`, with one `CanRead` check per page.

On other HALs `PERF_TRACE_FLAG_SAMPLE` returns `-ENOSYS`, and the wait/wakeup/msg events still work
(with no frames).

**Safety rules** (IRQ / scheduler context, `threads_common.spinlock` held):
- **Never `lib_printf`.** Events go to the cbuffer under the trace spinlock, which is a leaf lock;
  upstream already emits events under the threads spinlock.
- **User memory** is read only:
  - in the current address space: the interrupted thread at a sample, the blocking thread at a wait.
    Waits that began before the trace get registers only.
  - below `VADDR_USR_MAX` (a hostile sp cannot make the kernel copy kernel memory into a world-readable
    trace);
  - inside the thread's own stack `[ustack, ustack+ustacksz)`, or a 1 MiB span when that is unknown;
  - page by page, after `hal_cpuCanRead()`.

  The stack words go straight from user VA into the ring through a new multi-part writer
  (`_writeEventParts`), so no large buffer sits on the 8 KiB kstack. Per-CPU frame buffers (~350 B
  each) are allocated at init.
- **Kernel frames** are walked only inside the thread's own kstack.
- **Off by default.** While no trace runs, each hook costs a `trace_isRunning()` call and a branch,
  as upstream's events do. Nothing is allocated until `perf_start`.

### Userspace

- **`prof`** (`phoenix-rtos-utils/prof`, branch `prof-tool`; a default component on
  aarch64a72-generic):
  - `prof record [-t secs] [-o dir] [-p pid] [-f period_us] [-b min_wait_us] [-d depth] [-s bytes] [-w bytes] [-M MB] [-r] [-L dirs]`
    writes the same channel files as psh `perf` (so `convert.sh` works on them) and `prof.info`. That
    file holds the process list at the start and the end, the file mappings of every process
    (meminfo `OBJECT_OID` entries), and `file <port> <id> <path>` lines found by `stat()` in `-L` dirs,
    so shared objects can be symbolized.
  - `prof report [-p pid] [-n N] [dir]` runs on the target and prints, with raw addresses:
    - CPU per process,
    - the top PCs of the busiest threads,
    - the longest single waits (what, deadline, cause, waker),
    - waits per (thread, object) by total time,
    - msgSend edges client → port → serving thread.
- **`scripts/prof-report.py`** (coordination repo, uncommitted) is the host report with symbols:
  - **Binaries:** pid → process name → the unstripped ELF (`_build/<target>/prog`, `webkit_wpe-build`,
    `versioned-ports/*/{bin,lib}`, or `--bin NAME=PATH`/`--search`). Shared objects are mapped through
    prof.info's maps, and the kernel through `prog/phoenix-*.elf`.
  - **Unwinding:** the fp chain where it is valid, otherwise a **stack scan** (words in executable
    code that follow a BL/BLR, the launcher's method) over the copied stack.
  - **Report:**
    - CPU per process with idle split out;
    - hottest functions per thread;
    - longest waits with user stack + kernel reason, and a **blocking chain**: for a msgSend wait,
      the serving thread's CPU and its own overlapping waits, two levels deep;
    - a **timeline of waits ≥ `--long-ms`** for the process of interest;
    - waits by reason;
    - who-blocked-whom;
    - `--folded` (CPU) and `--folded-waits` (blocked µs) for flamegraph.pl, speedscope or inferno.

  It was checked against a synthetic trace (layout generated from the kernel structs) and against
  real ELFs: BL/BLR detection on `prog/psh`, addr2line batching, kernel ELF.
- **Test** (`phoenix-rtos-tests/perf/test_prof_sampling.c`, branch `prof-sampling`, unity):
  - one thread spins in a known function: its user-mode samples must land in it (≥ 90 %);
  - another thread blocks in msgSend on a server that holds the request for 300 ms:
    - msg_send names the port, and msg_recv names the server tid;
    - thread_wait has the port as its args[0] and msgSend's caller as its user lr;
    - thread_wakeup names the server as the waker, with cause explicit, ≥ 250 ms after the wait.

  On a kernel without the feature it **fails**, because no such events are recorded.

---

## 3. Branches and SHAs (none merged, none pushed)

| repo | branch | commits |
|---|---|---|
| phoenix-rtos-kernel | `prof-sampling` (from master 28e93f4a) | `f54c96c9` hal/aarch64: context and frame-record access for the perf sampler; `41bc26b3` perf: trace where threads run, what they wait for and who wakes them |
| phoenix-rtos-utils | `prof-tool` (from 749e2ac) | `40b9695` prof: system-wide profiler on the kernel trace |
| phoenix-rtos-tests | `prof-sampling` (from f7f3d36) | `4e3b808` perf: test the profiling events of the kernel trace |
| phoenix-rtos-hostutils | `prof-sampling` (from 49a1fd9) | `0d9a5f0` trace: ctf_to_proto: accept array fields |
| coordination | (uncommitted) | `scripts/prof-report.py`, this note |

Worktrees: `/home/houp/.claude/jobs/c8f1289c/tmp/wt-prof/{kernel,utils,tests,hostutils}`.

**Compile checks.**
- `scripts/wt-syntax-check.sh` reported CLEAN (`-Werror`, real flags, nothing staged) for these
  kernel files: `perf/trace.c`, `perf/perf.c`, `proc/threads.c`, `proc/msg.c`, `hal/aarch64/cpu.c`,
  `syscalls.c`, `proc/process.c`, `hal/aarch64/interrupts_gicv2.c`, `hal/aarch64/exceptions.c`.
- `prof/*.c` and the test compile clean under the util/test build flags against the branch's
  `<phoenix/perf.h>`, and `prof` links. They are new directories, so `wt-syntax-check` cannot derive
  their command. The same command was used by hand.
- `report.c` also ran on the host under ASan/UBSan against the synthetic trace.

**Merging with `upstream-sync-2026-10-06`.** `git merge-tree` reports one trivial conflict in
`threads_timeintr`. Upstream adds `(void)context;` where this branch adds the
`trace_eventThreadSample(..., context)` line. Resolution: keep the sample line and drop the `(void)`.

**Rebuild scope:**
- **`--scope core`**: kernel, the installed `<phoenix/perf.h>`, utils (prof) and tests. No syscall
  was added, so no `full-clean` / stale-binary ABI concern.
- The kernel change adds no string to grep, so grade the kernel by artifact mtimes (the
  `rpi4-core-change` skill, "no greppable string"): `prog/phoenix-aarch64a72-generic.elf` and
  `loader.disk` must be newer than `41bc26b3`.
- For prof, check `strings .buildroot/_build/aarch64a72-generic-rpi4b/prog.stripped/prof | grep -c
  "no thread_sample events"` (expect 1) and check that `/bin/prof` exists in the rootfs.

---

## 4. Using it on the Pi for the three cases

psh has no `&`, `;` or quotes. Run concurrent work from a small bash script placed on the NFS root
(the **live** fsid=0 export, see memory `feedback_stage_to_live_fsid0_export`), then fetch the
directory to the host.

**Case 1: WebGL rAF gaps of ~3.3 s.** `/root/prof-webgl.sh`:
```
#!/bin/bash
/usr/share/wpe-browser/b7.sh webgl &
sleep 90                                  # page loaded, frames running, gaps occurring
prof record -t 30 -o /root/prof-webgl -s 1024
wait
```
psh: `/bin/bash /root/prof-webgl.sh`. Host:
```
scripts/prof-report.py <export>/root/prof-webgl --long-ms 1000 --pid <WebProcess pid> \
    --folded webgl.folded --folded-waits webgl-waits.folded
```
Keep `-w` at 512: the browser blocks thousands of times per second, and the scan finds callers
within a few hundred bytes. Raise it only if the waits' user stacks come back empty. Read the
**Timeline** first. Every ~3.3 s gap should show as a wait of ≈3300 ms whose `timeout`
equals its length and that ends `-> timeout`. That is the "wait that times out", together with the
WebKit caller (stack scan) and the kernel reason (futexWait / phCondWait / msgSend / unix socket).
If the wait instead ends `-> wakeup by <tid>`, the blocking chain shows what that thread or the
server was doing. Run without `--pid` to see the compositor/UI side of the same window.

**Case 2: start stall (rare).** The stall is long-lived, so record **after** it is detected. The
waits that began before the trace come out as `thread_wait` flag 1, with kernel chain, syscall args
and user pc/lr. When the launcher logs `start-stall` (or the HDMI shows a blank view):
```
prof record -t 5 -o /root/prof-stall -w 0
```
Then on the host run `prof-report.py ... --long-ms 0` and read the waits with
`(began before the trace)`: which thread of the new WebProcess sits where, and on which port or
socket. To also catch the moment it goes wrong, loop trials with `prof record -r -t 120`
(rolling: the last ~4 MB per CPU) and keep the trace of the trial that stalled.

**Case 3: general slowness.** During a benchmark (Speedometer/JetStream/MotionMark, see the test
tools in `79e6743f4`):
```
prof record -t 20 -o /root/prof-bench -s 2048
```
On the host:
- `--folded bench.folded` → `flamegraph.pl bench.folded > cpu.svg` shows where the WebProcess
  spends CPU (JS interpreter vs. layout vs. raster).
- `--folded-waits` shows where it does **not** run: display-path waits on the compositor or GPU
  server.
- `CPU by process` splits browser / compositor / GPU server / kernel / idle. If the WebProcess is
  far below 100 % of a CPU, the time is lost in waits, not in JS.

The on-Pi `prof report /root/prof-bench` gives the same tables without symbols, which is enough for
"who is busy, who is blocked, on what" without a host round trip.

---

## 5. Risks and limits

- **R1: user-stack reads in IRQ context.**
  - Guards: the `at` probe per page, Normal-memory only, the thread's own stack bounds, below
    `VADDR_USR_MAX`.
  - Fixed during review: the walk's "page already probed" marker started at 0, so a frame pointer
    in page 0 (sp = 0, x29 = 0, from a crashing or hostile program) skipped the probe. A NULL read
    in IRQ context would have followed. The marker now starts at `~0`, and sp = 0 is rejected.
  - Remaining hole: a sibling thread on another CPU that **munmaps this thread's stack** between
    the probe and the read (a few ns). The result would be an EL1 fault in IRQ context, which is
    fatal. This is pathological (the stack of a running thread), but not impossible. A later
    hardening could read through the PA instead (kernel scratch mapping), or take an exception-table
    fixup.
  - `-s 0 -w 0` removes all stack copying and leaves frame-pointer walks only.
- **R2: overhead while recording.**
  - Each sample is ~60 B, plus 8 B per frame, plus the stack copy (default 512 B), at 1 kHz × 4 CPUs.
    That is ≈ 2.3 MB/s at the defaults.
  - Each wait is ~90 B, plus frames, plus `waitStack`. A busy browser blocks thousands of times per
    second, so `-w 2048` can dominate. Lower `-w` or use `-f 2000+`.
  - The copy happens with interrupts off, under the trace spinlock, on the sampled CPU (~1 µs per
    512 B).
  - Upstream property: **all CPUs serialize on one trace spinlock** for every event.
- **R3: ring sizing.**
  - 4 MB/CPU event channel + 1 MB meta, allocated at `perf_start`.
  - Non-rolling: `prof` drains every 100 ms (≥ 40 MB/s headroom). If the reader falls behind,
    events are **dropped whole** and the kernel prints `event discard detected` at `perf_finish`.
  - Rolling (`-r`): a busy CPU fills its 4 MB in ~5-7 s at the default stack copies, ~40 s
    without them (`-s 0 -w 0`). Idle CPUs fill much more slowly.
  - The kernel discards **bytes, not events**, so a rolling stream starts mid-event. `prof report`
    and `prof-report.py` resynchronize: an offset counts as an event boundary once 6 events parse
    back to back with time not going back. They report the skipped bytes. Checked on a synthetic
    stream cut by 7 bytes: exactly one event was lost. upstream `convert.sh` trims by trial
    instead.
  - Timestamps are u32 µs, so a trace is < 71 min (upstream limit).
- **R4: `perf_read` copies into the user buffer under the trace spinlock, with IRQs off.** A page
  fault there (a fresh malloc'd buffer) would be a fault with a spinlock held. `prof` and the test
  pre-touch the buffer and read 64 KB at a time. **libtrace (psh `perf`) does not**: it uses a 512 KB
  untouched buffer. This is an upstream issue to report or fix separately.
- **R5: attribution limits.**
  - WebKit and most ports have **no frame pointers**. User stacks come from the stack scan, which can
    show stale frames: treat them as leads. Building the port with `-fno-omit-frame-pointer` makes
    them exact.
  - JIT code (if JSC JIT is on) does not symbolize.
  - A wakeup from a userspace interrupt handler runs in IRQ context: the reported waker is the
    interrupted thread. The host script relabels it as `from irq N` using upstream's
    interrupt_enter/exit events (all IRQs but the timer).
  - The sampler sees only **running** threads. Blocked time is in the wait events, so CPU% + waits
    together cover a thread's time.
  - Kernel frames need the kernel's `-fno-omit-frame-pointer` (set for aarch64 here, not upstream).
- **Syscall args** are stored as full x0..x3. The readers mask the 32-bit ones (port, handle, pid,
  tid) to their low half, because AAPCS64 leaves the upper bits unspecified. They keep 64 bits for
  `futexWait`'s address.
- **R6: wait reason when the syscall is unknown.** For waits that began before the trace, and for
  page-fault waits (no syscall event), the reason comes from the kernel chain only. It is named by
  the first kernel function outside the wait plumbing.
- **Not verified:**
  - the TSDL with babeltrace2 (not installed on this host);
  - any runtime behaviour (no Pi or QEMU run). The first runs are in §6.

---

## 5b. Build 38 on the Pi (2026-10-07) and the fixes

**Result.** `test-prof-sampling` passed 2/0, but the kernel printed `event discard detected`.
`prof record -t 5 -o /root/prof-idle` never returned: `channel_event0` reached 942 MB in about a
minute, every other channel stayed at 0 bytes, and `prof.info` stopped at 4096 bytes (one stdio
buffer).

**Causes.**
1. **The recorder traced itself.** Each drained 64 KB chunk was written straight to the NFS root.
   That write is messages to the file server, lwip and genet, mostly on CPU 0, and with 512 B
   wait stacks their events outgrew the chunk.
2. **The drain loop could not end.** Its `do { read } while (full read)` never left CPU 0's event
   channel. It therefore never checked the deadline, and never read channels 2-7, whose files
   stayed empty.
   - meta0 had less than 4 KB, still in its stdio buffer. The thread list went to the meta channel
     of whichever CPU prof's `perf_start` ran on.
   - This is not a channel-indexing bug: the index is `cpuChan + cpu*2`, and the files are named the
     same way.
3. **Volume.** The test left the trace unread for ~0.35 s, and a 4 MB channel still overflowed:
   more than 10 MB/s on one CPU on an idle board. Which events filled it was **not measured**. A
   wait record is ~200 B even without a stack (upstream's `thread_enqueued` is 7 B), so per-block
   wait records are the likeliest cause.

**Fixes** (new commits on the branches):

| repo | commit | change |
|---|---|---|
| kernel | `cfb6c12c` | `perf_trace_cfg_t.waitMinUs` (appended; a shorter cfg means 0, the old behaviour): a wait is captured into a per-thread slot when it begins, and written only when it ends, if it lasted `waitMinUs` (flag 2, new `blocked` field). Wakeups of shorter waits are dropped too. At `perf_stop`, waits still going on are written as open (flag 4). `event discard detected (N events ...)`, and waits that found no slot are reported. |
| utils | `fbf400c` | `prof record` keeps the trace **in memory** (`-M MB`, default 256) and writes it after `perf_stop`. Drain passes are bounded (32 reads per channel). The deadline is authoritative. Defaults: `-f 2000 -b 1000 -s 512 -w 512`. `self <pid>` goes into prof.info, and the reports leave the recorder out. `prof report` opens with the **event mix** (bytes per event type). |
| tests | `98df3b3` | records in deferred mode (as prof does), with a reader thread every 20 ms; asserts the client's wait is deferred and `blocked ≥ 250 ms`. |
| coord | `scripts/prof-report.py` | new wait layout, deferred/open waits, `self` exclusion, event mix. |

**Host checks.**
- **record** against a fake kernel whose every channel always holds a full buffer:
  - `-t 2` stops after 2.1 s, with all 8 channels read;
  - `-M 64` stops at 64 MB.
- **report** (C under ASan/UBSan, and the Python script) against synthetic traces with deferred,
  open and existing waits.

## 5c. Build 39 (2026-10-07): an idle profile still took 78 MB in 5 s

**What happened.** `prof record -t 5 -M 128` on an idle Pi held 78 MB, 54 MB of it on CPU 0. The
kernel reported 34954 lost events. Writing the 54 MB `channel_event0` in a single write() timed out
the NFS server, which crashed (a separate fix is in progress). The report never ran, so the event
mix was never seen.

**Likely cause (from the code; the mix was not measured).** Most of upstream's events fire at the
rate of the operation they describe:

| class | events | cost |
|---|---|---|
| scheduling | sched_enter + preempted/scheduling + sched_exit | ~26 B per switch |
| scheduling | enqueued + waking | 14 B per block |
| syscall | syscall_enter + syscall_exit | 16 B per syscall |
| lock | set_enter/exit/acquired + clear | 44 B per kernel lock/unlock, plus lock_name per lock and epoch |
| interrupt | interrupt_enter + interrupt_exit | 12 B per interrupt (all but the timer's) |

CPU 0 takes every device interrupt and runs most of the drivers and servers behind them. The
profiler's own events are small at idle: samples are ~143 B per sample per idle CPU (~0.3 MB/s for
four CPUs), and only waits ≥ 1 ms are recorded.

**Fixes** (kernel `2970e569`, utils `33b9736`, tests `eb73591`):

- **Kernel event classes.** `perf_trace_cfg_t.events` selects classes (`PERF_TRACE_EV_*`).
  - `0` means all of them, as before. That is also what a caller without a cfg gets, so psh
    `perf` and libtrace see no change.
  - The check is `trace_isEnabled(id)`, used in place of `trace_isRunning()` in the event macros.
- **thread_wait carries its syscall** (the immediate of the SVC before its user pc), so syscall
  events are not needed. In deferred mode, waits that began before the trace also go through the
  slots, so every deferred wait is complete by itself (thread_waking is not needed).
- **trace_stats** (meta channel, written last) records the events and waits that were lost.
- **`prof record`:**
  - records `PERF_TRACE_EV_PROFILE` by default; `-e sched,syscall,lock,irq|all` adds classes, and
    `-b 0` adds sched;
  - prints the event mix (type × count, bytes, bytes per CPU, losses) **from memory before writing
    anything**;
  - writes the files in checked 1 MB write() calls, and a failed file does not stop the others.
- **Test.** `idle_volume` records 2 s of an idle system with prof's defaults. It fails above 2 MB,
  or if trace_stats reports any loss, and logs bytes per event type when it fails.

**Risk.** If idle daemons sleep in short periodic loops (≥ 1 ms each), the 512 B stack in every
wait record dominates: 100 waits/s from 10 threads ≈ 0.8 MB/s. In that case the printed mix shows
`thread_wait` on top, and the remedy is `-w 256` or `-b 10000` (or a larger default).

## 5d. Build 40 (2026-10-07): kernel time was "at hal_spinlockClear"

**What happened.** The JetStream `splay` trace showed ~70% of the WebProcess main thread's samples
in the kernel, 57% of them at `hal_spinlockClear`, and every other thread looked the same. This is
skid. The timer interrupt can only be taken where the kernel unmasks interrupts, which is mostly
the end of a spinlock section, so the sampled pc is where the work ended, not where it was done.
The idle 5 s trace was 5.7 MB, and in splay 75% of the bytes were wait records.

**Fixes** (kernel `04fa2876`, utils `f22bfee`, tests `dae4a70`):

- **thread_sample now carries more for a sample taken in the kernel:**
  - `klr` (the caller of a leaf such as hal_spinlockClear, which the frame chain skips);
  - a **skid** flag, set when the instruction before kpc is `msr daif`/`msr daifclr`
    (verified against the encodings in the built kernel ELF);
  - the **entry reason**: the syscall number (the SVC before the user pc), or for an exception
    its ESR.EC and fault address, read from the `exc_context_t` that `_exceptions_dispatch`
    pushes below the user context.
- **Reports.**
  - `prof report` shows, per thread, kernel time by entry (syscall names from the kernel's
    `SYSCALLS` table; page fault, ...) and "kernel time at", with a skid sample charged to its
    caller.
  - `prof-report.py` adds a "Kernel time by entry" section with symbolized callers, two frames
    deep, and inserts klr into the kernel frames of the folded stacks.
- **Wait volume.**
  - `waitStackMinUs` (prof `-B`, default 10 ms): only waits at least that long carry a user
    stack. Every wait of 1 ms or more is still timed.
  - A wait in the same place as its thread's previous recorded wait (same syscall, user pc/lr,
    caller, first two kernel callers) is written without frames or stack (flag bit 3, ~90 B). The
    reports take the frames from that previous wait.
- **idle_volume budget stays at 2 MB for 2 s.** Estimate: ~0.5 MB of waits plus ~0.7 MB of
  samples.

**New test:** `kernel_entry_attributed` loops mmap, a page fault and munmap. ≥90% of the thread's
kernel samples must name their entry, and ≥50% must be mmap/munmap/data abort.

**Checks.** The image build was running, so all compiles were private (toolchain + worktree only,
nothing read from `.buildroot`):
- kernel: 11 files with `-Werror`;
- prof sources and the test with `-Werror`;
- report and Python script against synthetic traces with kernel-mode samples.

The real-flags `syntax-check.sh` pass is still to run once the build is done.

## 6. Next steps (in order)

1. **Build.** Merge the four branches into a test build with `--scope core`. Run the unity test
   `test-prof-sampling` on the Pi (or `aarch64a53-zynqmp-qemu`).
2. **Smoke-test recording.** `prof record -t 5`, then `prof report`, then the host
   `prof-report.py`, with the Pi idle and then under the X/Wayland desktop. Watch the console for
   `event discard detected`. Under the desktop, check for no faults and for a stable frame rate
   (overhead).
3. **Case 1**, then case 3, then case 2, as in §4.
4. **Upstream feedback:**
   - the libtrace buffer issue (R4);
   - `ctf_to_proto` array support;
   - perhaps `libtrace` taking a `perf_trace_cfg_t`, so that psh `perf` can sample too.
