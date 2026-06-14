# Network / Filesystem Performance — Plan & Backlog (2026-06-14)

Status: **(a) and (b) below are DEFERRED** (documented here for a focused future
session). **TD-16 (cache enable) is reaffirmed a near-future MUST-HAVE.** The
per-RPC latency bug is already fixed (see "Done" below); these are the residual
performance items. Proceeding now with the Quake gamma/world fix (item "(c)").

## Done (2026-06-14) — the headline fix
NFS was ~20× too slow (0.43 MB/s on a 100 Mbps link; **100 ms per RPC** while
ICMP RTT ≈ 0.9 ms). Root cause: the Phoenix lwip-port socket `poll()` **never
wakes on data-ready** — it blocks the full timeout (`port/sockets.c` only answers
`atPollStatus` queries; `// oh crap, there is no lwip_poll()`). libnfs's sync API
polls with a 100 ms default `poll_timeout`, so every RPC stalled 100 ms.
Workaround committed: `nfs_set_poll_timeout(nfs, 1)` (filesystems `af30007`,
utils `a3731ba`) → mount 505→12 ms, RPC 100400→1340 µs (**75×**); the NFS boot now
reaches the app render. Boot-time `FSHEALTH` micro-benchmark added to nfs-smoke
(utils `739b4c8`) so FS-perf regressions are visible every boot.
Memory: `project_nfs_poll_stall_fix`.

---

## (a) Bulk-read throughput still ~0.5 MB/s  [DEFERRED]

**Symptom.** Per-RPC *latency* is fixed (1.3 ms), but a 2 MB sequential read is
still 0.54 MB/s (the 18 MB pak0 slurp ≈ 33 s). So the *transfer rate*, not the
round-trip latency, is now the limiter. ~23× below the 100 Mbps line rate.

**Hypotheses (ranked).**
1. **Caches-off CPU/memcpy (TD-16).** The rx path copies data several times
   (genet DMA buffer → pbuf → socket buffer → libnfs buffer → app) plus TCP
   checksums, all on *uncached* memory (caches globally off). Uncached A72
   memcpy is ~10–50× slower than cached; multiple copies + lwip overhead can
   bottleneck to ~0.5 MB/s. **This is the leading suspect** and would largely
   dissolve with TD-16.
2. **TCP delayed-ACK / window dynamics.** Server sends `TCP_WND` (32×1460 ≈
   46 KB) then waits for the Pi's ACK; if lwip delays ACKs / window-updates per
   window, each window stalls. Note: at the fixed 1.3 ms RTT, 46 KB/1.3 ms ≈
   35 MB/s, so the window is *not* the limiter unless the effective per-window
   RTT is inflated by delayed ACKs.
3. **recv() granularity.** If `nfs_service`'s `recv()` drains only one pbuf per
   poll iteration (~1.5 KB) at the 1 ms poll cadence → ~1 MB/s ceiling.

**Decisive first test.** Bump `TCP_WND` (and `TCP_SND_BUF`) in
`sources/phoenix-rtos-lwip/include/default-opts/lwipopts.h` from `32*TCP_MSS` to
`128*TCP_MSS`, boot, read FSHEALTH throughput:
- throughput **rises** → window/ACK-bound → pursue delayed-ACK + window scaling
  (`LWIP_WND_SCALE`), and ensure `MEM_SIZE` / `PBUF_POOL_SIZE` /
  `PBUF_POOL_BUFSIZE` can back the larger window.
- throughput **flat** → caches-off/memcpy-bound → it's a TD-16 problem; also
  audit the genet rx path for redundant copies (consider fewer copies / a
  larger recv drain per call).

**Files.** `sources/phoenix-rtos-lwip/include/default-opts/lwipopts.h`,
`sources/phoenix-rtos-lwip/port/sockets.c` (recv drain), the genet driver rx
path. **Validate** with the FSHEALTH `read=… MB/s` figure.

---

## (b) Proper general `poll()`/`select()` readiness fix  [DEFERRED]

**The real bug** (the `poll_timeout=1` commit is only an NFS-scoped workaround):
the lwip-port socket `poll()`/`select()` does not wake waiters when a socket
becomes readable/writable. `port/sockets.c` only answers `mtGetAttr(atPollStatus)`
*queries* (non-blocking `poll_one`/`lwip_select` with timeout 0). So the kernel
`poll()` cannot be woken by readiness — it spins/times out → `poll(fd, T)` costs
~T regardless. **Any** poll/select-based socket app on Phoenix is crippled.

**Plan.**
1. Map the Phoenix poll/event path: how does the kernel `poll()` block and get
   woken for a port-backed object? Find a driver that already supports poll
   readiness (pty/uart console) and copy its notification pattern.
2. In the lwip-port, when a socket transitions to readable (lwip recv callback)
   or writable (sent/space), **post the Phoenix poll/event notification** for
   that socket's waiters.
3. Likely touches the kernel poll/event API + `libphoenix` poll() + the
   lwip-port socket server thread.

**Validation.** Revert the `poll_timeout=1` workaround → with readiness wakeups,
the libnfs 100 ms default no longer adds latency (FSHEALTH RPC stays ~1 ms). Add
a generic poll/select socket unit test. **Benefit:** correctness + perf for all
socket apps, not just NFS. **Files:** `sources/phoenix-rtos-lwip/port/sockets.c`,
kernel poll mechanism, `libphoenix` poll/select.

---

## TD-16 (global cache enable) — REAFFIRMED NEAR-FUTURE MUST-HAVE

Caches are **globally OFF** today. This makes *everything* CPU/memcpy-bound:
the NFS bulk throughput (a), the Quake BSP/lightmap build, `v3d_compile` (GLSL
shader compiles), and the network rx path. It is the **single largest perf
lever** in the port and gates a genuinely fast system. Item (a) is most likely a
direct symptom of it.

**These performance issues need to be solved soon.** TD-16 should be scheduled as
a near-term priority, not left indefinitely parked.

**Approach** (per the standing plan, Phase 1.8): a research spike to re-enable
caches on the current armstub/MMU baseline with strict rollback (netboot is
recoverable — a bad image just fails to boot and the next netboot of a reverted
image recovers with no human). The parked blocker was BCM2711 SLC (system-level
cache) stale-read non-determinism; retry + bisect on the current baseline.
Escalate to JTAG only if the SLC stale-read recurs. Mostly [U+T]
(netboot-bisectable). See `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`
(TD-16) and `tracking/`/the Phase-1.8 plan.

---

## (c) Quake world gamma/brightness — DOING NOW

Out of scope for this doc; tracked in `project_quakespasm_port`. Summary: the
world *renders correctly* (proven via in-engine readback: full-coverage,
non-black) but ~3× too dark — the device lacks the gamma the host applies
(`gl_glsl_gamma_able=false`, no hardware gamma ramp on /dev/fb0). Fix = apply
gamma in the FBO→/dev/fb0 blit (`pl_phoenix_vid.c`). Now iterable because the NFS
poll fix made boots fast/reliable.
