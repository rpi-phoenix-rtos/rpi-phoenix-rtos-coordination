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
