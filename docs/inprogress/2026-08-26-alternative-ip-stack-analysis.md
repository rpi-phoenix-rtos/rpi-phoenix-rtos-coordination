# Alternative IP-stack analysis — candidates to replace lwIP on Phoenix-RTOS / Pi 4

Date: 2026-08-26
Status: research / analysis only (no source changed)
Author: research agent
Motivation: owner question — is a liberally-licensed open-source TCP/IP stack that is
**not** built around single-thread packet processing a good candidate to *replace* lwIP,
to lift single-flow throughput above lwIP's ~37.5 MB/s raw / ~26 MB/s NFS ceiling
(vs 112 MB/s for Linux on the same BCM2711 GENET hardware)?

---

## TL;DR / headline finding

**The premise does not survive contact with the actual bottleneck.** Our problem is
*single-flow* TCP throughput (one NFS connection). Every "scalable / multi-core" stack in
the candidate list (F-Stack, mTCP, Seastar) scales throughput **across many connections**
by hashing distinct flows onto per-core queues via NIC **RSS**. A single flow still lands
on exactly one core in all of them — so none of them raises the ceiling for our workload.
Worse, they all assume **DPDK** (hugepages, VFIO/UIO, poll-mode drivers), for which there
is no GENET driver and which is itself a months-to-person-year port onto a microkernel.

Two hardware facts I verified locally reinforce this:

1. **GENET (BCM2711) has no hash-based RSS.** The Linux `bcmgenet` driver
   (`external/linux/drivers/net/ethernet/broadcom/genet/bcmgenet.c`) implements RX
   spreading only through the **Hardware Filtering Block (HFB)** — explicit
   pattern-match n-tuple filters mapped to up to 16 priority RX queues
   (`bcmgenet_hfb_set_filter_rx_queue_mapping`, lines ~492–520). There is **no Toeplitz
   hash, no hash key, no indirection table**. So the "hash flows onto cores automatically"
   mechanism the per-core stacks depend on is simply absent on this NIC; you would have to
   hand-install filters, and a single flow still cannot be split.
2. **Linux hits 112 MB/s essentially on one core**, not via multicore — through per-packet
   efficiency: NAPI batching, GRO (RX coalescing), and TX checksum offload. 1 GbE line rate
   is only ~118 MB/s, so 112 is ~95% of wire on a single flow. The lever is **per-packet
   cost**, not core count.

Our own measured trajectory is the strongest evidence: the gigabit-throughput work already
moved single-flow NFS 7.86 → 24.4 MB/s and socket-recv to 27.86 MB/s purely by attacking
per-packet cost (RBUF_64B_EN, core-locking-input, cacheable RX, recvmbox segment
coalescing). The remaining 27.86 → 37.5 → ~112 gap is wakeup/IPC latency and lack of
GRO/large-receive, **not** thread count. A stack swap does not address any of that; it just
replaces a stack we understand and have already tuned with a much larger, harder-to-port one.

**Recommendation (detail at the end): do not replace lwIP to chase single-flow throughput.**
Keep pushing the per-packet/IPC path in lwIP (software GRO / receive-batching, wakeup
latency, zero-copy RX). If the app-level workload can tolerate it, **multiple parallel TCP
connections (NFS `nconnect`-style striping)** is the one architectural change that actually
parallelizes our bulk-transfer workload and composes with the *existing* stack. The only
credible "real replacement" — if the owner insists — is a **FreeBSD-derived stack ported
directly (without DPDK)**, at months-to-person-year cost.

---

## Comparison table

| Stack | License | Threading / scalability model | Microkernel portability (userspace GENET + BSD sockets) | Maturity / completeness | Est. porting effort to Phoenix | Verdict |
|---|---|---|---|---|---|---|
| **lwIP (baseline)** | BSD-3-Clause (liberal) ✅ | Single core thread (`tcpip_thread`); `LWIP_TCPIP_CORE_LOCKING` serializes all core processing. Multi-instance possible in principle. | Already ported & running as `/sbin/lwip`; drives userspace GENET; BSD-socket API over IPC. | Mature, IPv4+IPv6+TCP/UDP, small footprint. TCP CC is basic (no CUBIC/BBR, limited SACK). | n/a (in place; tuning ongoing) | **Keep & tune.** Multi-instance-per-core needs a SW flow demux (no HW RSS) and still can't split one flow. |
| **smoltcp** (Rust) | **0-clause BSD** ✅ (verified in repo README; formerly MIT/Apache) | Single-threaded, explicit `poll()` event loop, no background thread, `no_std`, no heap. Not multi-core. | OS-agnostic by design (you supply the device + timer). But **Rust toolchain in Phoenix** is a large prerequisite; needs `alloc`/no_std runtime, socket-shim to expose BSD API over IPC. | Growing, embedded-focused. IPv4+IPv6, TCP/UDP; TCP is less battle-tested than BSD/lwIP (limited SACK/window scaling historically). | Months (mostly Rust-in-Phoenix bring-up + socket/IPC shim), and it is **also single-threaded** so it does not fix the problem. | Not worth it for throughput. Interesting only if Rust adoption is a separate goal. |
| **F-Stack** (= "Tencent f-stack"; same project) | Core BSD-2-Clause; bundles FreeBSD stack (BSD) + DPDK (BSD-3) ✅ | Per-process/per-core, **shared-nothing**; each core runs its own FreeBSD stack instance. Scales across connections via **NIC RSS**. | **Hard wall: assumes DPDK** (hugepages, VFIO/UIO, poll-mode driver). No GENET PMD. Uses its own `ff_*` API (not drop-in BSD sockets, though a socket shim exists). | Very mature stack (FreeBSD 11/13), production at Tencent for L7 proxying. | Person-year class (port DPDK to a microkernel *first*, write a GENET PMD, then integrate). | No. Single flow still one core; DPDK dependency is the real killer. |
| **mTCP** | Modified BSD (BSD-3) ✅ | Per-core TCP thread affinitized per app thread; multicore. Needs **RSS** to spread flows to cores. | Assumes DPDK or netmap/PSIO packet I/O; Linux-oriented. Custom epoll-like API, not BSD sockets. | **Research-grade, effectively unmaintained since ~2018.** TCP only; UDP/IPv6 weak or absent. | Person-year+ and you inherit an abandoned codebase. | No. |
| **FreeBSD network stack (standalone / direct port)** | BSD-2/3-Clause ✅ | Fully SMP, **fine-grained locking**; genuinely multi-threaded core. Mature CC (CUBIC, RACK, SACK, TSO/LRO). | No DPDK required if ported directly, but you must supply mbuf allocator, callout/timer, locking primitives, an `ifnet` shim over userspace GENET, and a BSD-socket layer. This is the substance of the port. | The gold standard for correctness/perf/completeness. | Months → person-year on a microkernel. | **The only credible "real switch."** Best per-connection perf and future headroom; large project. Still won't beat a well-tuned single flow by a huge margin at 1 GbE. |
| **Seastar** (ScyllaDB) | **Apache-2.0** ✅ | Shared-nothing per-core, futures/continuations; own userspace TCP written lock-free, connections divided across cores. | C++17 framework, **DPDK-based**, assumes Linux/hugepades and its whole reactor runtime. Not a library you slot under a BSD-socket app; it *is* the app model. | Mature within ScyllaDB; TCP is functional but Seastar-idiomatic, not a general drop-in. | Person-year+ (port DPDK + the entire reactor). Rewrites the app model. | No — wrong shape for a microkernel + existing BSD-socket apps. |
| **NuttX networking** | **Apache-2.0** ✅ | Net stack largely single-threaded/IOB-buffer driven, similar architecture class to lwIP (one net worker context). Not per-core scalable. | Tightly coupled to NuttX's own kernel/scheduler/IOB; extracting just the net stack is invasive. BSD sockets yes (within NuttX). | Mature RTOS, IPv4/IPv6/TCP/UDP, BSD sockets. | Months to disentangle from NuttX internals; **no throughput upside** (same single-context class as lwIP). | No net gain over lwIP. |
| **Zephyr networking** | **Apache-2.0** ✅ | Multi-threaded TX/RX queues with handoff points, but a single core TCP context; designed for IoT/low-throughput, not multi-core bulk TCP. | Coupled to Zephyr kernel primitives (k_fifo, net_buf, etc.); extracting is invasive. BSD-socket API + `IP offload` hooks. | Mature RTOS net stack; IPv4/IPv6/TCP/UDP. TCP throughput historically modest. | Months to disentangle; **no throughput upside**. | No net gain over lwIP. |
| **picoTCP** | **GPLv2 + commercial (dual)** ❌ | Single event loop. | — | Mature, small. | — | **Disqualified** (license). |
| **picoTCP-NG** (fork) | **GPLv2/v3 only** ❌ | Single event loop. | — | Community fork of the above. | — | **Disqualified** (license; NG dropped even the commercial option). |
| **FreeRTOS+TCP** (added) | **MIT** ✅ | Single "IP task" — same architecture as lwIP. Not multi-core. | Coupled to FreeRTOS primitives; BSD-ish sockets. | Mature, IPv4/IPv6/TCP. | Months; **no throughput upside** (same single-task class). | No net gain over lwIP. |
| **rump kernel (NetBSD stack)** (added) | **BSD-2-Clause** ✅ | Uses the real NetBSD TCP/IP stack (SMP-capable, mature). Anykernel — runs kernel components in a userspace process. | **Most microkernel-relevant option:** already runs on L4 (via Genode), Xen, and bare metal — no mandatory DPDK. You supply a `rumpuser` hypercall layer + an `ifnet`/GENET binding + BSD-socket client shim. | Real NetBSD stack: full IPv4/IPv6/TCP/UDP, SACK, modern CC. | Months (write the `rumpuser` port + GENET glue); heavy imported tree. | Plausible alternative to a raw FreeBSD port; same effort class, same single-flow caveat. *(worth a spike if a swap is mandated)* |
| **libuinet** (added) | BSD ✅ | Userspace FreeBSD stack, SMP-capable. | No mandatory DPDK (netmap-based), but **unmaintained (~2013–2014)**; would need heavy resurrection. | Was a real FreeBSD-9 stack. | Person-year (dead codebase). | Low confidence; effectively a stale FreeBSD-port shortcut — prefer a fresh FreeBSD port. *(uncertain — flag)* |
| **OpenFastPath** (added) | BSD-3 ✅ | FreeBSD-derived fast-path, per-core with ODP/DPDK. | Assumes ODP/DPDK; **dormant project**. | FreeBSD-derived TCP/IP. | Person-year+. | No (dormant + DPDK/ODP). *(uncertain — flag)* |
| **gVisor netstack (netstack/tcpip)** (added) | Apache-2.0 ✅ | Go, goroutine-per-connection, GC runtime. | **Non-starter:** requires the Go runtime + scheduler in Phoenix. | Mature (powers gVisor). | Person-year+ (Go runtime). | No. |
| **VPP (fd.io) host stack** (added) | Apache-2.0 ✅ | Vectorized per-worker graph, multi-core. | DPDK/graph framework; heavy; not a BSD-socket library. | Mature (carrier-grade). | Person-year+. | No (DPDK + wrong shape). |

License legend: ✅ = liberal / usable for Phoenix core; ❌ = GPL, hard-disqualified for core.

---

## Per-candidate notes

### lwIP (baseline)
- License BSD-3-Clause. Already the shipping stack (`/sbin/lwip`), already drives the
  userspace GENET driver, already exposes BSD sockets over microkernel IPC. Zero port cost.
- Architecture: one `tcpip_thread` owns the core; `LWIP_TCPIP_CORE_LOCKING` serializes
  everything. Confirmed by upstream docs: lwIP "started targeting single-threaded
  environments" and chose a single-core-thread + message-passing model rather than a
  thread-safe core (https://lwip.nongnu.org/2_1_x/multithreading.html).
- **Owner's specific sub-question — per-core lwIP instances:** technically possible (N
  independent lwIP instances, one per core), but (a) with **no hardware RSS on GENET** you'd
  have to build a software flow-steering demux to fan RX packets to the right instance by
  hash, and (b) it still does **nothing for a single flow**, which is our actual bottleneck.
  So multi-instance lwIP is effort spent on the wrong axis for this workload.
- The realistic wins for single-flow throughput are all per-packet/IPC:
  software GRO / receive batching (coalesce RX segments before the socket layer — we already
  did a version of this in the recvmbox path), reducing per-packet wakeup/IPC latency, and
  zero-copy RX. These are lwIP-local changes, not a stack swap.

### smoltcp (Rust)
- **License: 0-clause BSD** — verified in the repo README
  (https://github.com/smoltcp-rs/smoltcp): "smoltcp is distributed under the terms of
  0-clause BSD license." (Older references say MIT/Apache-2.0; the current tree is 0BSD.
  Either way it is fully liberal.)
- Threading: single-threaded, explicit `poll()` loop, `no_std`, no heap, no background
  thread. Not multi-core. So it does **not** address the owner's motivation.
- Portability: genuinely OS-agnostic (you provide a device trait + timer), which is the good
  news. The bad news is it's **Rust**: standing up a Rust target for Phoenix aarch64 (core +
  `alloc`, panic/abort runtime, linking against libphoenix, plus a socket/IPC shim to present
  BSD sockets to existing C apps) is a substantial parallel project. Its TCP is embedded-grade
  (window scaling / SACK less battle-tested than lwIP/BSD).
- Verdict: only interesting if adopting Rust in Phoenix is a *separate* strategic goal; it is
  not a throughput fix.

### F-Stack (identical to "Tencent f-stack" in the candidate list — one project, not two)
- License: core BSD-2-Clause; bundles the FreeBSD stack (BSD) and DPDK (BSD-3-Clause) — all
  liberal (https://github.com/F-Stack/f-stack, https://github.com/F-Stack/f-stack/blob/dev/LICENSE).
- Model: shared-nothing, one FreeBSD-stack instance per core; scales to ~10M concurrent
  connections **by hashing flows across cores with NIC RSS**. Great for an L7 proxy with many
  connections; irrelevant to one NFS flow.
- Portability: **DPDK dependency is a hard wall.** DPDK needs hugepages, VFIO/UIO, and a
  poll-mode driver; there is no GENET PMD, and porting DPDK to a microkernel is itself a
  months-to-person-year effort before F-Stack integration even begins. API is `ff_*`, not
  drop-in BSD sockets (a shim exists but adds work).
- Verdict: no.

### mTCP
- License: Modified BSD (BSD-3) (https://github.com/mtcp-stack/mtcp) — liberal.
- Model: per-core TCP thread affinitized to each app thread; needs RSS-capable NIC queues
  equal to core count. Again cross-connection scaling, not single-flow.
- Portability: DPDK/netmap/PSIO packet I/O, Linux-oriented, custom epoll-like API.
  **Research-grade and effectively unmaintained since ~2018**; TCP-only, weak/absent
  UDP+IPv6.
- Verdict: no (abandoned + DPDK + wrong scaling axis).

### FreeBSD network stack (direct standalone port)
- License: BSD-2/3-Clause — ideal.
- This is the **only candidate that genuinely fixes multi-core scalability the right way**:
  fine-grained SMP locking, mature congestion control (CUBIC, RACK), SACK, TSO/LRO, decades
  of hardening. And crucially it does **not require DPDK** if you port the stack sources
  directly (F-Stack's value is mostly the DPDK glue, which we don't want).
- Portability cost is real: supply an mbuf allocator, `callout`/timer subsystem, locking
  primitives (mutex/rwlock/epoch), an `ifnet` shim binding to our userspace GENET driver, and
  a BSD-socket layer over IPC. This is the bulk of the work — months → person-year on a
  microkernel, with ongoing maintenance of a large imported tree.
- Reality check on payoff: even a perfect FreeBSD stack, on 1 GbE with a single flow, is
  bounded by ~118 MB/s wire and by our GENET driver + IPC path. A well-tuned lwIP that gains
  GRO-style batching could get most of the way there at a fraction of the cost.
- Verdict: the row that survives *if a swap is mandated*, but justify it by
  correctness/completeness/future-NIC headroom, not by single-flow numbers.

### Seastar
- License: Apache-2.0 (https://github.com/scylladb/seastar) — liberal.
- Model: shared-nothing per-core, futures/continuations, lock-free userspace TCP with
  connections divided across cores. Excellent design — for its target (ScyllaDB-style servers
  on Linux+DPDK).
- Portability: it is a whole C++ reactor runtime + DPDK, not a library you slot under existing
  BSD-socket C apps. Adopting it means adopting its programming model. Wrong shape for a
  microkernel with a heterogeneous app set.
- Verdict: no.

### NuttX / Zephyr / FreeRTOS+TCP networking
- All liberal (Apache-2.0, Apache-2.0, MIT respectively).
- All are **single-net-context architectures in the same class as lwIP** (NuttX IOB worker;
  Zephyr single TCP context with queue handoffs; FreeRTOS single IP task). None is per-core
  multi-threaded for TCP.
- All are tightly coupled to their own RTOS kernel primitives, so "just take the net stack"
  is an invasive extraction (months) — and yields **no throughput advantage over the lwIP we
  already run**.
- Verdict: no net gain; not worth the extraction.

### picoTCP / picoTCP-NG
- **Disqualified on license.** Original picoTCP is dual **GPL + commercial** (Altran-funded,
  needs commercial income; see tass-belgium/picotcp issues #428, #501). The community
  **picoTCP-NG** fork (virtualsquare/picotcp) is **GPLv2/v3 only** — it dropped even the
  commercial option, so it's *more* restrictive, not less. GPL in the Phoenix core is a hard
  no per project policy.

### Others surveyed (lower confidence — flagged)
- **libuinet** — BSD userspace FreeBSD-9 stack (netmap, no DPDK), but **unmaintained since
  ~2013–2014**; would need major resurrection. If a FreeBSD port is chosen, start from a
  current FreeBSD tree, not libuinet. *(uncertain)*
- **OpenFastPath** — BSD-3, FreeBSD-derived fast path over ODP/DPDK; **dormant**. *(uncertain)*
- **gVisor netstack** — Apache-2.0 but **Go runtime required** → non-starter on Phoenix.
- **VPP (fd.io)** — Apache-2.0, vectorized multi-core, but DPDK + graph framework, not a
  BSD-socket library. Wrong shape and heavy.

---

## Why "multi-core" doesn't help *our* number

RSS-based per-core stacks answer the question "how do I serve **millions of connections**
without a kernel bottleneck?" Their unit of parallelism is the **flow**: the NIC hashes each
5-tuple to a queue, each queue has its own core + stack instance, and connections never share
state (shared-nothing). Throughput scales with core count *because there are many flows*.

Our workload is the opposite: **one** big NFS TCP connection. That connection's packets all
hash to the same queue/core in every one of these designs, so adding cores or stack instances
does nothing. The single-flow ceiling is set by per-packet processing cost and by the
RX→app wakeup/IPC latency chain — exactly what Linux optimizes with NAPI + GRO + offload to
reach 112 MB/s **on one core**.

Additionally, **GENET can't even do the RSS these stacks assume**: verified in the Linux
driver, RX spreading is via the Hardware Filtering Block (explicit n-tuple → priority-queue
mapping), with no Toeplitz hash / hash key / indirection table. So the multi-core stacks would
be running on a NIC that can't feed them the way they expect.

## Recommended path (concrete)

1. **Do not replace lwIP for single-flow throughput.** The premise (multi-core stack →
   higher single-flow throughput) is false for our workload and hardware.
2. **Keep attacking per-packet + IPC cost in lwIP** — the same axis that already yielded
   3.1x (7.86 → 24.4 MB/s NFS). Highest-value next items:
   - **Software GRO / receive batching** at the GENET-RX → lwIP boundary (coalesce
     back-to-back TCP segments before per-segment socket/IPC overhead). This is the closest
     analogue to what gets Linux to 112 on one core.
   - **Wakeup / IPC latency** on the socket-recv path (already flagged in the
     gigabit-throughput memory as the 27.86 → 37.5 lever).
   - **Zero-copy / cacheable RX** continuation.
3. **If the app layer allows it, parallelize the transfer, not the stack:** multiple
   concurrent TCP connections for bulk transfer — NFS **`nconnect`-style** multi-connection
   striping. This is the *one* change that genuinely turns the bulk workload into multiple
   flows, at which point even the current single-thread lwIP benefits from more of them (and
   the aggregate can exceed one flow's ceiling). It composes with the existing stack and
   driver — no replacement needed. (Caveat: needs client-side support in our NFS/libnfs path;
   scope it separately.)
4. **Only if the owner mandates a stack swap for reasons beyond single-flow throughput**
   (e.g. modern congestion control, IPv6/TCP feature completeness, long-term SMP headroom for
   many-connection server workloads): the sole defensible target is a **direct FreeBSD-stack
   port without DPDK**. Budget months-to-person-year, plus ongoing maintenance of a large
   imported tree, and set expectations that single-flow 1 GbE numbers will be similar to a
   well-tuned lwIP.

## Uncertainties / things to verify before acting
- Exact remaining single-flow breakdown (how much of 37.5 → 112 is GRO-absence vs
  wakeup/IPC vs driver copy) — a `perf`-style attribution on the Pi would sharpen item 2.
- Whether our NFS client (libnfs) can drive `nconnect`-style multiple connections without
  server-side session issues — needs a spike.
- smoltcp license is 0BSD in the current tree but historically cited as MIT/Apache; if it ever
  mattered, pin to a specific commit's LICENSE file.
- libuinet / OpenFastPath maintenance status stated from search summaries, not a repo audit —
  treat as low confidence.

## Sources
- lwIP multithreading model: https://lwip.nongnu.org/2_1_x/multithreading.html
- smoltcp (repo, 0BSD license, poll model): https://github.com/smoltcp-rs/smoltcp
- F-Stack (BSD-2, FreeBSD+DPDK): https://github.com/F-Stack/f-stack ,
  https://github.com/F-Stack/f-stack/blob/dev/LICENSE , https://www.f-stack.org/
- mTCP (BSD-3, per-core, RSS, research): https://github.com/mtcp-stack/mtcp ,
  https://mtcp-stack.github.io/
- Seastar (Apache-2.0, shared-nothing, own TCP, DPDK): https://github.com/scylladb/seastar ,
  https://seastar.io/ , https://docs.seastar.io/master/tutorial.html
- NuttX networking (Apache-2.0): https://nuttx.apache.org/docs/latest/introduction/licensing.html ,
  https://cwiki.apache.org/confluence/display/NUTTX/Networking
- Zephyr net stack (Apache-2.0): https://docs.zephyrproject.org/latest/connectivity/networking/net-stack-architecture.html ,
  https://en.wikipedia.org/wiki/Zephyr_(operating_system)
- picoTCP dual GPL/commercial: https://github.com/tass-belgium/picotcp/issues/428 ,
  https://github.com/tass-belgium/picotcp/issues/501 ; picoTCP-NG GPL-only fork:
  https://github.com/virtualsquare/picotcp
- rump kernels / NetBSD stack (BSD-2): https://man.netbsd.org/rumpkernel.7 ,
  https://en.wikipedia.org/wiki/Rump_kernel ,
  https://www.dpdk.org/netbsd-tcp-ip-port-on-dpdk-using-rump-framework/
- GENET has HFB filtering, no hash RSS (local Linux clone):
  `external/linux/drivers/net/ethernet/broadcom/genet/bcmgenet.c`
  (`bcmgenet_hfb_set_filter_rx_queue_mapping`, priority RX queues; no Toeplitz/hash key)
