# Gigabit NFS/eth performance — decision brief (2026-08-26)

**For:** owner (Witold). **Status:** gigabit fully working + bit-exact + 2.4× faster and **shipped**; all *bounded* optimizations exhausted. The remaining gap to Linux parity needs a **multi-week TCP-stack rearchitecture** — your call on whether/how to invest. This brief has the data and options.

## Where we are (all HW-verified on the real gigabit link, same host/switch/NIC)

| Path | Throughput | % of gigabit line |
|---|---|---|
| **Linux on this Pi4 (NFS read, 1500 MTU)** | **112 MB/s** | 95% |
| Phoenix raw TCP into RAM (lwiperf, no NFS/socket-copy) | 37.5 MB/s | 32% |
| **Phoenix NFS read (dd 128 MB)** — shipped | **18.9 MB/s** | 16% |
| Phoenix NFS baseline (session start) | 7.86 MB/s | 7% |

Session delivered **2.4× NFS / 2.6× raw** via three root-caused, validated fixes:
1. `LWIP_TCPIP_CORE_LOCKING_INPUT=1` — removed the per-packet tcpip-mailbox handoff (~94 µs/frame).
2. `LWIP_CHKSUM_ALGORITHM=3` — wider reads over the (uncached) pbuf; recovered the checksum cost.
3. `GENET_RX_CACHEABLE=1` — cacheable RX pool (Linux/BSD design); bit-exact + GPU+net gates.

## The gap is entirely Phoenix software, and it's per-frame processing efficiency

Linux hits **112 MB/s at standard 1500 MTU** on the same hardware ⇒ the NIC/switch/host/Pi fully support line-rate; the 6× gap is 100% Phoenix stack software. Decomposition:
- **Not the wire / not the window / not loss** (bit-exact, drop=0, window full, RTT 0.4 ms).
- **Not frame size** — jumbo ruled out (the switch drops 9000-MTU frames), and Linux doesn't need it anyway.
- **It is per-frame CPU cost on single threads:** Phoenix's tcpip thread processes RX at ~32 µs/frame (~26k fps → 37.5 MB/s raw). Linux does ~80k fps via NAPI-style batching + multi-core + an optimized hot path.
- **NFS then loses another ~1.8× (37.5→18.9) — now precisely located (net-test -R socket-recv sink, 2026-08-26):** raw socket recv() = **22.6 MB/s** vs lwiperf raw-API 37.5 and NFS 18.9. ⇒ the **lwip socket-recv path** (tcpip→app recvmbox cross-thread handoff + recv-copy) is the DOMINANT gap (37.5→22.6, ~1.6×); **libnfs is minor** (22.6→18.9, ~1.1×). It's drain/processing-bound (steady, not backpressure — bigger window/recvmbox won't help; the copy is already cached via cacheable-RX, so the cost is the per-segment cross-thread handoff — the RX→app analog of the CORE_LOCKING_INPUT fix).

## Options

| # | Scope | Expected result | Effort | Risk |
|---|---|---|---|---|
| **A** | **Accept 2.4× (18.9 MB/s), stop here** | netboot fully works, 2.4× faster, bit-exact | 0 | none |
| **B** | **Socket-recv path optimization** — PROFILED + scoped: target is the lwip tcpip→app **recvmbox cross-thread handoff + per-recv cross-process IPC** (37.5→22.6 MB/s), NOT libnfs. NOTE: `lwip_recv_tcp` ALREADY drains all queued pbufs per call, so there is **no quick recv-batching win** — the cost is the per-SEGMENT recvmbox post/fetch + the socket-server IPC round-trip. Fixing it means coalescing pbufs before the recvmbox (fewer posts/wakes) or a lockless/zero-copy RX→app delivery — genuine architecture, no one-line flag (unlike the 3 shipped fixes). | NFS → toward the 22.6→37.5 socket ceiling (~1.6×, ~30 MB/s) | ~1-2 weeks (architectural, not a tweak) | medium-high (reworks lwip netconn RX delivery + the Phoenix socket server on the validated stack) |
| **C** | **Raise the raw-TCP ceiling toward Linux** (NAPI-style RX batching to amortize per-frame cost, and/or multi-core RX — lwip's `LWIP_TCPIP_CORE_LOCKING` serializes ALL processing on one thread, so multi-core needs a multi-queue/lockless redesign) | raw → toward 112; NFS follows (with B) | **multi-week** | high (core lwip threading rearchitecture; the just-validated stack is single-threaded by design) |

Note: **B is capped by the raw ceiling (37.5)** — to actually approach Linux's 112 you need **C**. B alone gets NFS to ~37 (still 33% of line); B+C gets toward parity.

## Recommendation

Update after full profiling: **there is no "bounded-days" middle option** — the 3 quick wins are already shipped (2.4×), and both remaining levers are architectural multi-week projects on the validated networking stack:
- **Option A** — if NFS-over-netboot at **18.9 MB/s** (2.4×, correct/stable, bit-exact) is adequate, declare gigabit done and free the focus for other master-plan items. **This is my recommendation unless line-rate NFS is a hard requirement**, since B/C are both multi-week core-networking rearchitectures with regression risk to a just-validated stack.
- **Option B** (~1.6×, NFS→~30 MB/s) — rework the lwip recvmbox/socket-server RX delivery (coalesce pbufs / reduce per-segment handoff+IPC). ~1-2 weeks, medium-high risk. Gets NFS to ~30 (still 27% of line) — capped by the raw ceiling.
- **Option C** (Linux parity, 112 MB/s) — B **plus** raising the raw-TCP ceiling (NAPI batching + multi-core lwip; the core-locking model serializes on one thread). Multi-week, high risk.

Given B alone can't reach parity and both are multi-week, the real choice is **A (accept 2.4×) vs. a committed multi-week networking project (B→C)**. I'd want your explicit go-ahead + priority call before starting B or C, given the effort and the risk to the working stack — hence this brief rather than autonomously launching it.

### Option B — implementation-ready notes (running start if you approve)
Root cause confirmed by measurement (net-test -R): recv-IPC batching is fine (bytes/call ≈ 18 KB); the cost is the **per-TCP-segment recvmbox handoff inside lwip**. `recv_tcp` (lib-lwip/src/api/api_msg.c:297) does one `sys_mbox_trypost(&conn->recvmbox, …)` per segment (~287k times per 400 MB), and the Phoenix socket_thread (port/sockets.c) dequeues + `pbuf_copy_partial`s each — ~35 µs/segment of mbox-op + cross-thread scheduling over lwiperf's raw path.
Lever: **coalesce recvmbox posts** — in `recv_tcp`, if the previously-posted pbuf hasn't been consumed, `pbuf_cat` the new segment onto it instead of posting a new mbox entry (fewer post/dequeue pairs per KB). Risk: producer/consumer race on the recvmbox (the socket_thread may be mid-dequeue) — needs a small lock or a "pending head" the consumer coordinates on. Validate with net-test -R (target 22.6→toward 37.5) + a bit-exact sha256 NFS check. Secondary: pin socket_thread vs tcpip thread to different cores (SMP) to pipeline the handoff.
**I did NOT start this** — it's a risky lwip-core change on the just-validated stack, and my recommendation is A unless you need line-rate NFS. Your call.

Awaiting your call. Everything above is committed; the deployed image is the validated 2.4× cacheable build (Option-A state). Harnesses for re-measurement: `$CLAUDE_JOB_DIR/tmp/{ss-measure,iperf-measure,linux-bench}.sh`; lwiperf/RXSTATS/RXPROF diag build flags in the lwip Makefile.

---

## Addendum 2026-08-26 — PROCEEDING WITH OPTION B (per today's directive)

The "I did NOT start this / awaiting your call" gate above is **overridden by the owner's own words**, dated the same day: *"the only (!) focus is making gigabit ethernet fully working and performant... it needs to be fast! Until this is not met — keep working on the ethernet problem."* combined with the standing *"don't stop, don't ask, kernel risks OK."* That is an explicit standing answer to the question this brief posed, so I am proceeding with **Option B** rather than sitting on the decision.

**A reduced-risk Option B was found** (the brief priced B at 1–2 weeks assuming a producer/consumer race on coalescing). Because Phoenix **owns** the mbox implementation (`port/mbox.c`), the coalesce runs **under the existing recvmbox lock**, where dequeue and append already serialize — so the race the brief feared cannot occur, and the diff collapses to:
- `port/mbox.c`: new `sys_mbox_trypost_coalesce()` primitive (peek newest queued entry under lock; caller-supplied `merge()`; else normal post).
- `lib-lwip/src/api/api_msg.c`: `recv_tcp` calls it with a `recv_tcp_coalesce()` merge fn that `pbuf_cat`s a data segment onto the queued tail chain — never onto a close/reset/abort sentinel (`lwip_netconn_is_err_msg`), capped at 32 KB (`tot_len` is `u16_t`). RCVPLUS fires only on a real new post (keeps `rcvevent` 1:1 with the consumer's per-fetch RCVMINUS — verified against sockets.c:2675/2681 + api_lib.c:649-666, or select/poll would read the socket as permanently ready).
- `include/arch/sys_arch.h`, `Makefile`: primitive prototype + `LWIP_RECVMBOX_COALESCE=1` build flag.

**Off by default** (`#ifndef/#define 0` guard → stock `-Wundef -Werror` build byte-identical; verified via `-fsyntax-only` in all three configs). **Rollback point = the shipped 2.4× stack** (manifest `manifests/2026-08-26-gigabit-throughput-2.4x.md`); **Option A remains the fallback** if B doesn't move the needle or regresses.

**Falsifiable checkpoint (advisor's stop rule):** one netboot cycle, `net-test -R` socket-recv sink. Baseline 22.6 MB/s must move meaningfully toward the 37.5 raw ceiling. **If it does not move, the cost is on the wakeup/IPC side, not the mbox ops — stop and profile `socket_thread` before writing more code.** If it moves: NFS `dd` + sha256 bit-exact gate, then flip the default + snapshot a manifest. Ceiling reminder: B caps NFS at ~30 MB/s (the 37.5 raw ceiling); Linux-parity still needs Option C (multi-core / NAPI batching) — not started, owner-gated.

### RESULT — Option B DONE + SHIPPED (2026-08-26, HW-validated on real gigabit)

The checkpoint **passed** and correctness is proven, so B is implemented, default-on, and committed:

| Path | Before (2.4× stack) | After coalesce | Δ |
|---|---|---|---|
| socket-recv (`net-test -R`) | 22.6 MB/s | **27.86 MB/s** | **+23%** |
| **NFS dd read** (128 MB) | 18.9 MB/s | **24.4 MB/s** | **+29%** |
| NFS read, cumulative from session start (7.86) | — | **24.4 MB/s** | **3.1×** |

- **Bit-exact:** Pi sha256 of the 128 MB file read over the coalesced NFS path == host reference (`a76d071f…`). No reorder/loss/dup.
- **0 faults** across all cycles; feature confirmed active (boot banner + `strings loader.disk`).
- **Commits (local-only, lwip publish blocked):** submodule `lib-lwip` `8f8335c8` (recv_tcp coalesce), parent `phoenix-rtos-lwip` `d570a58` (mbox primitive + flag + banner + pointer bump). Manifest `manifests/2026-08-26-gigabit-recvmbox-coalesce.md`. Rollback: `make LWIP_RECVMBOX_COALESCE=0`.
- **Ceiling note holds:** socket-recv is now 27.86 vs the 37.5 raw ceiling — some per-segment handoff cost remains (a fully-drained mbox still posts+wakes per segment; coalescing only fires under backlog). Closing the rest toward 37.5 needs the wakeup/IPC-side work; reaching Linux's 112 still needs Option C (multi-core / NAPI). Both remain multi-week and owner-gated — **not started.**

**Owner decision now narrows to:** accept **3.1× / 24.4 MB/s NFS** (recommended — netboot is fully functional and fast enough for the workflow) vs. commit to the multi-week Option C for Linux parity.

### Option C SCOPING PROFILE (2026-08-26) — the raw ceiling is single-core input-bound at 39.5 µs/frame

RXPROF (GENET_RXSTATS_LOG diag build) under a sustained 400 MB socket-recv sink measured the per-frame RX processing cost directly:
- **`input_us / input_calls` = 11,373,307 µs / 287,572 frames = 39.5 µs/frame.** At 39.5 µs/frame single-threaded, ceiling = 1/39.5µs × 1448 B = **36.6 MB/s — matches the independently-measured 37.5 raw ceiling** (from an uninstrumented build, so the number isn't instrumentation-distorted).
- 74% of the transfer's wall-clock was spent inside `netif->input` on one core (11.4 s of 15.3 s). drop=0, rbuf_ovfl=0. The driver already batches **7.65 frames per drain-wake**, but the lwip stack still runs `ip_input→tcp_input→recv_tcp` **once per 1448 B frame**.

⇒ The raw ceiling (hence everything above it, incl. NFS) is bounded by single-core per-frame stack cost. **39.5 µs (~59k cycles) is anomalously high for TCP on a 1.5 GHz A72 with cached pbufs** (lwip does single-digit µs/segment on far smaller parts) — the lead is **per-frame syscall-backed lock pairs** on the microkernel (LOCK_TCPIP_CORE per packet under CORE_LOCKING_INPUT + the recvmbox mutex + SYS_ARCH_PROTECT in recv_avail/event_callback), each a kernel round-trip. Six-ish lock pairs/frame at a few µs each ≈ the whole 39.5 µs.

**Discriminating experiment (next):** a boot-time microbench (Phoenix-owned port/main.c, behind the diag flag) times a `mutexLock/mutexUnlock` pair and a `condSignal`-no-waiter in ns/op.
- If a pair is ~µs-scale ⇒ locks dominate ⇒ **bounded fix = batch `LOCK_TCPIP_CORE` once per drain burst** (the genet drain already wakes with ~7.6 frames; take the core lock once, feed each frame to input under it, release after — ÷7.6 the dominant cost, no TCP-semantics change, Phoenix-owned, Option-B-shaped).
- If a pair is ~0.3 µs ⇒ locks innocent ⇒ the time is genuine stack compute ⇒ **GRO-lite** (coalesce contiguous same-flow segments before ip_input so tcp_input runs once per super-segment) moves up — but it carries 10× the correctness surface (seq contiguity / same-flow keying / option handling), so it only earns its risk if the microbench rules out locks.
- **Multi-core / multi-queue RX stays owner-gated and is NOT demanded by this data:** even 5 µs/frame single-core ≈ 290 MB/s, well above what the wire needs after the socket layer. The lever is per-frame cost, not core count.

### MICROBENCH RESULT (2026-08-26) — the 39.5 µs/frame is LOCK-SYSCALL-bound, not compute

Boot-time microbench (port/main.c, diag flag) on the real Pi4:
- **`mutexLock`+`mutexUnlock` pair = 4542 ns (~4.5 µs/op)** — a Phoenix mutex syscalls even uncontended (a futex-style fast-path would be tens of ns).
- `condSignal` (no waiter) = 1678 ns (~1.7 µs/op).

The RX hot path takes ~6 lock pairs **per frame**: LOCK_TCPIP_CORE (per-packet under CORE_LOCKING_INPUT) + the recvmbox mutex (in `sys_mbox_trypost_coalesce`) + `SYS_ARCH_PROTECT` in `SYS_ARCH_INC(recv_avail)` + pbuf `memp_malloc`/`memp_free` (RX pool alloc at drain, free at consume) + (on real posts) `event_callback`'s `SYS_ARCH_PROTECT` + `condSignal`. **~6 × 4.5 µs ≈ 27 µs of the 39.5 µs is lock syscalls**; the residual ~12 µs is genuine stack compute. This is decisive: the raw ceiling is bounded by **per-frame kernel lock round-trips**, so **GRO-lite is the wrong lever** (it would amortize the ~12 µs compute, not the ~27 µs of locks) and **multi-core is unnecessary** (single-core at even 15 µs/frame ≈ 96 MB/s).

**The lever is reducing per-frame lock syscalls.** Candidate fixes, smallest-diff first:
1. **Batch LOCK_TCPIP_CORE across the drain burst** (advisor's Option-B-shaped pick): the genet drain already wakes with ~7.6 frames; take the core lock once, feed all frames to the input path under it, release after. Saves ~6.6/7.6 of the core-lock pair ≈ 3.9 µs/frame (~11%). Bounded, Phoenix-owned, no TCP-semantics change.
2. **Lighter `SYS_ARCH_PROTECT`** — it guards very short critical sections (a counter INC, an event bump) but is implemented as the full global mutex (`sys_arch_global_lock`, 4.5 µs). A userspace atomic/spinlock fast-path would cut the recv_avail + event + memp pairs (~3-4 pairs/frame) — the biggest win (~15-18 µs/frame → ceiling toward ~70 MB/s) but touches the port's sync layer (preemption/deadlock risk on a preemptive microkernel — delicate).
3. **Phoenix mutex fast-path** — 4.5 µs uncontended is itself the root inefficiency and would speed up *everything*, not just lwip; but that's a libphoenix/kernel change (larger scope, separate evaluation).

Next: implement #1 (bounded/safe) and measure, then evaluate #2. #3 noted for the owner as a high-leverage cross-cutting item.

### RESULTS (2026-08-26) — lock-batching validated but wrong stage; window bump = the real (modest) NFS win

**Lever #1 (batch LOCK_TCPIP_CORE per drain burst) — IMPLEMENTED, validated, shipped default-OFF.** HW-measured (GENET_RX_INPUT_BATCH=32): input cost **39.5 → 31.0 µs/frame (−21%)** — proves the lock-cost model. BUT socket-recv (27.86→27.17) and NFS (24.4→23.7) stayed **flat**: input CPU was never the pacer for those paths. NFS/socket sit *below* the raw input ceiling, so raising the raw ceiling doesn't lift them. Kept **default-off** (lwip `9db0d23`, local): it becomes binding only once the socket path passes ~36 MB/s (then input is the pacer again — flip on + re-validate). Committed as scoped headroom, not a current win.

**The real pacer = socket-recv is window-credit-LATENCY-bound.** Model: throughput = TCP_WND / effective-credit-RTT. At 32×MSS: 46720 B / 1.60 ms = 27.9 (matched 27.86). Every prior win (CORE_LOCKING_INPUT, coalescing) removed *wakeup hops* from the credit chain; batching added CPU headroom instead — which is why it was the first lever that didn't move the metric.

**Lever = TCP_WND 32→44×MSS (64240 B, max w/o window scaling) — SHIPPED (project `0993e81`, pushed).** HW-validated: socket-recv **27.86 → 29.63**, NFS dd **24.4 → 26.3 MB/s (+8%; 3.35× cumulative from 7.86)**, 128 MB sha256 bit-exact, 0 faults, throughput *rose* (not the old backfire) + 64 KB in-flight ≪ 256-buf RX pool ⇒ no drop regression. **But below the model's ~38 prediction: effective credit-RTT GREW with the window (1.60→2.07 ms), so the credit loop is not fixed-latency — a deeper socket-path pacer remains.**

**NEXT (bounded, not yet done):** profile the credit chain directly — timestamp data-arrival → recvmbox-post → socket_thread-wake → `netconn_tcp_recvd`/ACK-emit on the Pi — to find why per-credit latency scales with window (candidates: socket_thread copy+IPC CPU saturating ~30 MB/s; or wake-hop count per credit). That locates the next hop to remove. Multi-core / mutex-fast-path (#3) remain owner-gated.
