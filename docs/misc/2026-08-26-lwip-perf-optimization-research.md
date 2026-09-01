# lwIP RX throughput optimization — documentation & community research

**Date:** 2026-08-26
**Scope:** Research-only. No source changed. Consolidates what the *official* lwIP
documentation (opt.h source comments, the nongnu.org doxygen docs, the lwIP Savannah
wiki) and reputable third‑party ports say about single‑flow TCP RX throughput, mapped
against our measured Pi 4 / GENET findings.

---

## 0. What we measured (the anchor for everything below)

Single TCP flow, real gigabit link, Pi 4 (BCM2711, 4× Cortex‑A72 @ 1.5 GHz), Phoenix‑RTOS:

| Path | Throughput | vs line rate |
|---|---|---|
| Raw lwiperf (in‑process raw API) | 37.5 MB/s (~300 Mb/s) | 32% |
| Socket path (Phoenix IPC socket server) | ~29.6 MB/s (~237 Mb/s) | 25% |
| NFS read | 26.3 MB/s | 22% |
| Linux on the same Pi 4 | 112 MB/s (~940 Mb/s) | 95% |

Root‑caused ceilings:

* **Raw ceiling = CPU‑per‑frame, and that per‑frame cost is dominated by kernel mutex
  syscalls.** `netif->input` processing = **39.5 µs/frame** single‑core; **~27 µs of
  that is kernel mutex syscalls** (~6 lock pairs/frame; a Phoenix `mutexLock`/`mutexUnlock`
  pair ≈ 4.5 µs, with *no uncontended userspace fast path*). The stack is single‑thread +
  syscall‑lock‑bound.
* **Socket ceiling = window‑credit latency.** Window advances once per `recv()` op
  (`NETCONN_NOAUTORCVD` → `tcp_recved` deferred to the consumer). The port's own model:
  `throughput = TCP_WND / effective-credit-RTT`; `32*MSS / 1.60 ms ≈ 27.9 MB/s`, matching
  measurement. Raising `TCP_WND` 32→44·MSS gave only **+8%**.

These two numbers — 27 µs of mutex per frame, and a credit pipe pinned by `TCP_WND` — are
the yardsticks against which every lever below is ranked.

---

## 1. Verified current configuration

Authoritative config is the **project overlay**, not the generic default‑opts file:
`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/lwip/lwipopts.h`
(`LWIPOPTS_DIR` points here for the rpi4b build; the generic
`sources/phoenix-rtos-lwip/include/default-opts/lwipopts.h` still says `TCP_WND=32*MSS`
and is *not* the one that ships — noting the discrepancy so nobody tunes the wrong file).

| Option | Current value | Source |
|---|---|---|
| `LWIP_TCPIP_CORE_LOCKING` | 1 | overlay:13 |
| `LWIP_TCPIP_CORE_LOCKING_INPUT` | 1 | overlay:21 |
| `TCP_MSS` | 1460 | overlay:56 |
| `TCP_WND` | **44·MSS = 64240** (= the max unscaled window, <65535) | overlay:68 |
| `TCP_SND_BUF` | = `TCP_WND` | overlay:69 |
| `TCP_SND_QUEUELEN` | 192 | overlay:70 |
| `LWIP_WND_SCALE` / `TCP_RCV_SCALE` | **0 / 0 (OFF)** | opt.h default (not set in overlay) |
| `TCP_WND_UPDATE_THRESHOLD` | `min(TCP_WND/4, MSS*4)` = **5840** | opt.h:1480 default |
| `TCP_OVERSIZE` | `TCP_MSS` | opt.h:1462 default |
| `MEM_LIBC_MALLOC` / `MEMP_MEM_MALLOC` | **1 / 1** (all pbufs/pcbs via libc `malloc`) | overlay:41‑42 |
| `PBUF_POOL_SIZE` / `MEMP_NUM_*` | **moot** (pools bypassed by `MEMP_MEM_MALLOC=1`) | — |
| `CHECKSUM_GEN_TCP` / `CHECKSUM_CHECK_TCP` | **1 / 1 (software both ways)** | opt.h:2346/2381 default |
| `LWIP_CHKSUM_ALGORITHM` | 3 (guarded default in `port/arch/cc.h`) | overlay:22 comment |
| `LWIP_CHECKSUM_ON_COPY` | 0 | opt.h:2403 default |
| `LWIP_NETIF_TX_SINGLE_PBUF` | 0 | opt.h:1708 default |
| `LWIP_SO_RCVBUF` | 0 | opt.h:2062 default |
| `DEFAULT_TCP_RECVMBOX_SIZE` | 32 | overlay:82 |
| `TCPIP_MBOX_SIZE` | 256 | overlay:79 |

**GENET hardware facts (from driver + Linux reference):**
* RX pbuf pool slot = `GENET_MAX_FRAME = 2048` bytes; `UMAC_MAX_FRAME_LEN` programmed to
  **1536**; `netif->mtu = 1500` (`drivers/bcm-genet.c:1764‑1771`).
* **RX checksum checker deliberately OFF** (`RBUF_CHK_CTRL=0`); the driver comment records
  that enabling `RBUF_RXCHK_EN` delivered frames corrupt and lwIP's software checksum
  rejected them (`bcm-genet.c:876‑968`). So there is currently **no HW checksum offload**.
* The Linux upstream `bcmgenet` programs `UMAC_MAX_FRAME_LEN = ENET_MAX_MTU_SIZE`, where
  `ENET_MAX_MTU_SIZE = ETH_DATA_LEN + ETH_HLEN + VLAN_HLEN + …` ≈ **1536**
  (`external/linux/.../genet/bcmgenet.h:35`). **BCM2711 GENET does not support jumbo
  frames** — see §4.

**SYS_ARCH_PROTECT is a raw kernel mutex.** `include/arch/sys_arch.h:76‑77` maps
`SYS_ARCH_PROTECT` → `sys_arch_global_lock()`, and `port/protect.c` implements that as an
unconditional `mutexLock(global_mutex)` syscall (with only a *same‑thread* recursion
guard — no cross‑thread uncontended fast path). This is the mechanism behind the measured
4.5 µs/pair. On a typical lwIP target `SYS_ARCH_PROTECT` (a.k.a. `SYS_LIGHTWEIGHT_PROT`) is
a cheap interrupt‑disable, *not* a syscall — so the 27 µs/frame is a **Phoenix‑port
artifact, not an lwIP property**. This distinction drives the priority order.

---

## 2. Already tried — do NOT re‑propose these

Filed here explicitly so the recommendation list in §3 stays clean:

* **`LWIP_TCPIP_CORE_LOCKING_INPUT=1`** — done. The overlay comment records the win: stock
  message‑passing `tcpip_input` (mbox post + cross‑thread wake to `tcpip_thread`) cost
  ~94 µs/frame (raw capped ~14.5 MB/s); running `tcpip_input()` inline under the core lock
  removed that handoff and is what got us to the current 39.5 µs/frame.
* **Recvmbox segment coalescing** (Option B, `LWIP_RECVMBOX_COALESCE=1`, cap 32 KB) — done;
  folds the per‑segment tcpip→socket handoff. This is why our socket path only loses ~21%
  vs raw (see §5) instead of Xilinx's ~45%.
* **Cacheable / streaming‑DMA RX pool** (`GENET_RX_CACHEABLE=1`) + 256‑buffer pool — done.
* **RX input core‑lock batching** (`GENET_RX_INPUT_BATCH`) — hold the core lock across an
  N‑frame drain burst instead of per packet. Done (Option C lever 1); bumping N further is
  incremental, covered under §3‑A note.
* **`LWIP_CHKSUM_ALGORITHM=3`** — done (uncached‑pbuf checksum win).
* **`TCP_WND` 32→44·MSS** — done (+8%). Note 44·MSS = 64240 is essentially the 16‑bit
  unscaled window ceiling, so **no further window increase is possible without
  `LWIP_WND_SCALE`** (see §3‑B).
* **Large scaled window (256·MSS + `LWIP_WND_SCALE`)** — *tried and it backfired* (3.67 vs
  8.5 MB/s: host burst outran the RX drain → drops/retransmits). Critically, that test
  predates cacheable‑RX + coalescing, which fixed the RX‑drain cause. See §3‑B for the
  re‑test recommendation.

---

## 3. Prioritized recommendations we have NOT (fully) tried

Ranked by expected leverage against the measured `39.5 µs/frame (27 µs mutex)` and the
`throughput = TCP_WND / credit-RTT` socket model.

### A. Give the per‑frame locks an uncontended userspace fast path  — HIGHEST LEVERAGE
**This is a Phoenix‑port change, not an lwIP config knob**, but it is the single biggest
prize and follows directly from our own root‑cause. `SYS_ARCH_PROTECT`/`sys_arch_global_lock`
(`port/protect.c`) unconditionally enters the kernel via `mutexLock`. lwIP's contract for
this macro (`SYS_LIGHTWEIGHT_PROT`, opt.h:205‑) is that it be a *lightweight* critical
section; every other lwIP port satisfies it with an interrupt‑disable or a futex‑style
atomic that only syscalls on contention.

* **Mechanism:** ~6 lock pairs/frame × 4.5 µs ≈ 27 µs of the 39.5 µs is pure syscall entry.
  A CAS‑based fast path (syscall only when actually contended — and this lock is almost
  never contended on a single‑flow RX path) removes most of that.
* **Expected magnitude:** if the 27 µs largely disappears, per‑frame cost → ~12–15 µs →
  raw ceiling could roughly **2–2.5×** (toward ~75–90 MB/s), before other terms dominate.
* **Caveat:** requires a Phoenix futex/fast‑mutex primitive; correctness under the
  microkernel scheduler must be proven. This is the "no uncontended userspace fast‑path"
  gap named in our own findings.
* **Cite:** opt.h `SYS_LIGHTWEIGHT_PROT` (https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__thread.html);
  local `port/protect.c`, `include/arch/sys_arch.h:76`.

**Sub‑lever A2 — flip `MEMP_MEM_MALLOC` (and possibly `MEM_LIBC_MALLOC`) to static pools.**
With `MEMP_MEM_MALLOC=1`, *every* `pbuf_alloc`/`pbuf_free`/pcb alloc is a libc `malloc`/
`free` — each carrying its own allocator lock and heap bookkeeping, on the hottest path in
the stack. lwIP's static `memp` pools are O(1) and protected only by `SYS_ARCH_PROTECT`
(which becomes cheap once A1 lands).
* **Mechanism:** removes malloc‑internal locking + allocator work per frame; complements A1.
* **Action:** set `MEMP_MEM_MALLOC=0`; then `PBUF_POOL_SIZE`, `MEMP_NUM_PBUF`,
  `MEMP_NUM_TCP_PCB`, `MEMP_NUM_TCP_SEG` become **live and must be sized** (RX pool + a full
  `TCP_WND` of in‑flight segments + headroom). This is exactly the wiki's "use pools, size
  them for the window" guidance (see §4).
* **Caveat:** mis‑sizing a pool = allocation failures = drops. Needs the LWIP_STATS
  `MEMP` counters (already on) to verify no pool exhaustion.
* **Cite:** lwIP wiki "Maximizing throughput" / opt.h memory‑pool section
  (https://www.nongnu.org/lwip/2_1_x/group__mempool.html). *[pool‑vs‑malloc perf claim
  corroborated by opt.h comments + wiki summary; see citation note §7.]*

### B. Re‑test `LWIP_WND_SCALE=1` with a *moderate* scaled window — HIGH, but previously backfired
The socket path is credit‑latency‑bound and **pinned at the 64240‑byte unscaled ceiling**.
The port's own model says socket‑recv ≈ `TCP_WND / credit-RTT`, so the only stock way to
raise the numerator further is window scaling.
* **Mechanism:** `LWIP_WND_SCALE=1` + `TCP_RCV_SCALE=1` lets `TCP_WND` exceed 65535; more
  bytes in flight → the sender is not throttled while the consumer copies/re‑credits.
* **Why revisit:** the earlier 256·MSS scaled‑window failure was caused by the RX drain
  being outrun (drops) — a cause since fixed by cacheable‑RX + coalescing (drop=0,
  rbuf_ovfl=0 at 24 MB/s per the overlay comment). The failure mode is gone; the lever was
  never re‑tested post‑fix.
* **Action:** graduated re‑test — 88·MSS, then 128·MSS — **watching LINK_STATS drop /
  rbuf_ovfl every step**. Do *not* jump back to 256·MSS.
* **Expected magnitude:** near‑linear on socket‑recv per the credit model, until the
  *denominator* (credit‑RTT, see C) or consumer‑copy rate becomes the limit. Our own +8%
  from the last window bump caps optimism — pair B with C.
* **Cite:** opt.h:1503‑1512 `LWIP_WND_SCALE`; overlay:57‑67 window history.

### C. Shrink the credit‑RTT (the denominator of the socket model) — HIGH→MEDIUM
B widens the pipe; C makes each credit round‑trip cheaper/rarer. **C1 is the most direct
attack on the measured socket bottleneck and likely outranks the window bump in §3‑B.**

* **C1 — Credit the window at buffer time, not copy time (early `tcp_recved`). HIGH.**
  This is the converse of the "defer `tcp_recved` to the consumer" question in the brief,
  and it answers it: today the socket recv path *defers* crediting. In `recv_tcp`
  (`api_msg.c:338‑402`) an incoming data pbuf is posted to `recvmbox` **without** calling
  `tcp_recved`; the window is credited only later in `do_recved` (`api_msg.c:1705`,
  `tcp_recved(msg->conn->pcb.tcp, recved)`), driven by the consumer's `netconn_tcp_recvd`
  after `lwip_recvfrom_tcp` copies to the user buffer (`sockets.c:1028`). So the credit loop
  includes: recvmbox post → consumer wake → IPC round‑trip to the socket server → copy to
  user → `netconn_tcp_recvd` message → core lock → `tcp_recved` → window‑update ACK. That is
  the ~1.60 ms effective credit‑RTT.
  * **Mechanism:** call `tcp_recved(pcb, len)` at post time inside `recv_tcp` (exactly what
    the recvmbox‑deleted branch already does at `api_msg.c:360`), so the window re‑opens the
    instant lwIP has *buffered* the segment — removing consumer‑copy + IPC latency from the
    credit loop entirely. This is a port‑level patch in the same file we already patch for
    coalescing, not a config flag.
  * **Expected magnitude:** collapses the denominator of `TCP_WND / credit-RTT`; potentially
    the largest socket‑path win and complementary to §3‑B (bigger window buys slack while
    early credit shortens the loop).
  * **Semantics risk (state honestly):** the advertised window then reflects *lwIP buffering*,
    not *application consumption* — i.e. it defeats the flow‑control back‑pressure that
    `NETCONN_NOAUTORCVD` was introduced to provide. Worst‑case buffering per connection is
    bounded by `recvmbox` depth × coalesce cap (32 slots × ≤32 KB ⇒ up to ~1 MB queued
    before the app reads); must add an explicit bound / high‑water gate, or a slow consumer
    lets the peer flood memory. Validate with the `MEMP`/`PBUF` stat counters (already on).
* **C2 — `TCP_WND_UPDATE_THRESHOLD` (currently 5840). MEDIUM.** Governs how much window must
  free before lwIP proactively emits a window‑update ACK, *independently* of the consumer's
  `netconn_tcp_recvd`. Lowering → window re‑advertised sooner (more ACKs, fewer sender
  stalls); raising → fewer credit round‑trips per MB. Note the interaction with **delayed
  ACK**: lwIP follows RFC 1122 and ACKs roughly every second full segment, and window
  updates normally *ride* those ACKs — the explicit‑update threshold is the escape hatch
  when the freed window outpaces the ACK cadence. One‑line stock knob; **test both
  directions**, the `NETCONN_NOAUTORCVD` interaction is undocumented, so measure. If C1 is
  applied this knob's importance drops (credit no longer waits on the consumer). Cite:
  opt.h:1476‑1481.
* **C3 — bigger application reads (NFS `rsize`, socket recv buffer). MEDIUM.** Each `recv()`
  credits once via `netconn_tcp_recvd`; larger reads credit more bytes per round‑trip →
  fewer round‑trips per MB. App/NFS‑side, not lwIP, but attacks the same term. (Ties to the
  memory note "PROFILE wakeup/IPC next.")
* **C4 — `LWIP_SO_RCVBUF`** is off; enabling changes recv accounting but is unlikely to help
  throughput. Low priority; completeness only.

### D. Hardware checksum offload + `LWIP_CHECKSUM_ON_COPY` — MEDIUM‑LOW, blocked by a known bug
lwIP's explicit throughput advice is to offload checksums to the NIC when possible
(`CHECKSUM_GEN_* = 0`, `CHECKSUM_CHECK_* = 0`). XAPP1026 builds all its AXI‑Ethernet/GigE
systems "with full checksum (both TCP and IP checksums) offload" and the ZC702 GigE has
"built‑in TCP/IP checksum offload."
* **Mechanism:** removes one full software pass over every RX byte (1460 B/frame). GENET v5
  has an RX checksum checker (`RBUF_RXCHK`).
* **Reality check:** our root‑cause puts the checksum in the ~12 µs *non‑lock* remainder,
  already trimmed by `LWIP_CHKSUM_ALGORITHM=3`. So expect **≤10–20% of the raw ceiling**,
  not a step change — and it is **blocked by a live GENET RX‑checksum corruption bug** the
  driver disabled it for. Only worth it after A. TX side (`LWIP_CHECKSUM_ON_COPY=1`, folds
  TX checksum into the copy) does not help our RX‑bound case.
* **Cite:** XAPP1026 v5.1 (Nov 2014) Table 1 notes + p23; opt.h:2343‑2403; driver comment
  `bcm-genet.c:876‑968`.

### Enumerated options dispatched (leave as‑is — closing the brief's checklist)
* **`TCP_QUEUE_OOSEQ`** — leave default (`=LWIP_TCP`). Only matters under loss/reordering;
  our link runs drop=0 / rbuf_ovfl=0, so out‑of‑sequence queueing is not on the hot path.
* **`MEM_SIZE`** — moot under `MEM_LIBC_MALLOC=1` (the lwIP heap is bypassed; allocations go
  to libc `malloc`). Becomes relevant only if §3‑A2 flips `MEM_LIBC_MALLOC=0`.
* **`PBUF_POOL_SIZE` / `MEMP_NUM_*`** — moot under `MEMP_MEM_MALLOC=1`; become live levers
  only under §3‑A2 (static pools), where they must be sized to RX pool + a full `TCP_WND`.
* **`LWIP_NETIF_TX_SINGLE_PBUF`** — leave 0. It forces a coalescing **memcpy** before TX for
  DMA MACs that lack scatter‑gather (opt.h:1690‑1694); it is TX‑side and *adds* a copy, so
  it is a throughput negative for our RX‑bound case. GENET does scatter‑gather.

### E. Zero‑copy RX — LARGELY DONE; residual copy is architectural
`LWIP_SUPPORT_CUSTOM_PBUF=1` and the driver already runs a zero‑copy/cacheable RX path
(RXSTATS distinguishes `zerocopy` vs `copyfb`). lwIP's zero‑copy‑RX guidance (PBUF_REF +
custom pbufs, best paired with checksum offload) is essentially already applied inside the
stack. **The remaining copy is the lwIP→application copy across the Phoenix IPC socket
server**, which is inherent to running lwIP as an isolated userspace process — not
removable by lwIP config. Note only; no stock knob left here.

---

## 4. Jumbo frames — DEAD END on this NIC (documenting the negative result)

The canonical "max gigabit throughput" lever in XAPP1026 is jumbo frames: it recommends
raising `TCP_MSS` 1460→8060, `pbuf_pool_bufsize` 1700→9700, `mem_size` 131072→524288,
`ip_reass_bufsize`→65535, `ip_frag_max_mtu` 1500→9000, and testing with `iperf … -M 8060`
(XAPP1026 v5.1 p24). Mechanistically this is attractive for us: 8060/1460 ≈ **5.5× fewer
frames per MB → 5.5× fewer of our expensive per‑frame lock bursts.**

**But BCM2711 GENET cannot do jumbo:**
* The Phoenix driver's RX pool slot is only `GENET_MAX_FRAME = 2048` bytes and it programs
  `UMAC_MAX_FRAME_LEN = 1536` / `mtu = 1500`.
* The **upstream Linux `bcmgenet` driver itself caps the MAC at `ENET_MAX_MTU_SIZE ≈ 1536`**
  — it does not implement jumbo either. This is a hardware limit of the GENET UMAC, not a
  Phoenix shortcut.

So the highest‑theoretical‑leverage lever from the vendor literature **is not available**;
pursuing it would require a MAC/pool redesign the silicon won't honor. This is why §3 leads
with lock‑cost reduction instead. (If a future revision proves GENET can be coaxed to a
larger frame, revisit — but the Linux reference says no.)

---

## 5. Where our findings agree / disagree with lwIP's documented guidance

**Agree:**
* **RAW ≫ socket cliff is expected and documented.** XAPP1026 Table 2 shows the *same
  phenomenon on the same silicon*: ZC702 GigE **RAW RX 943 Mb/s vs socket RX 521 Mb/s**
  (KC705: 380 vs 58; AC701: 205 vs 37). lwIP attributes it to socket/netconn API overhead
  ("the socket API … contains significant overhead for all operations … it is slow";
  "RAW API … much higher throughput because it does not have a high overhead", XAPP1026
  p4‑6). Our 37.5→29.6 MB/s is the same effect, same cause (per‑op mbox/lock handoff).
* **Single‑threaded core ceiling.** lwIP docs state the core is entered by one context at a
  time (core lock / `tcpip_thread`); our single‑thread 39.5 µs/frame is that ceiling made
  concrete.
* **Checksum offload matters** (XAPP1026) — we don't do it (§3‑D).
* **`-O2` matters for max throughput** (XAPP1026 p22 note) — confirm our lwip build is `-O2`
  (the port already relies on `-O2` elsewhere; verify for `lwip-core`).

**Disagree / important nuance:**
* **The headline lwIP tuning advice — "raise `TCP_WND`/`TCP_SND_BUF`, enable window
  scaling" — is largely NOT our bottleneck.** On a low‑RTT gigabit LAN the bandwidth‑delay
  product is tiny, so a bigger window buys little for *raw* throughput; our +8% from the
  last window bump confirms it. Window size only matters for us as the *numerator* of the
  socket credit‑latency model (§3‑B/C), not as the classic "fill the fat pipe" lever.
* **lwIP does not document a per‑frame *syscall* lock cost**, because in normal deployments
  `SYS_ARCH_PROTECT` is an interrupt‑disable, not a kernel mutex. Our dominant 27 µs/frame
  is therefore invisible in lwIP's own tuning docs — it is a Phoenix‑port property. The most
  impactful lever for us is *not in opt.h at all* (§3‑A).
* **Our socket layer is actually relatively efficient.** Xilinx's socket path loses ~45%
  vs raw even at line rate (943→521); ours loses only ~21% (37.5→29.6) thanks to recvmbox
  coalescing. The absolute deficit is the *raw* ceiling, not the socket wrapper.

---

## 6. Diminishing returns / architectural wall

**Does lwIP acknowledge a single‑thread throughput ceiling?** Yes, implicitly and by
construction: with core locking, exactly one thread executes core code at a time; without
it, everything funnels through the single `tcpip_thread`. A single TCP flow is therefore
bound to one CPU. lwIP offers no native way to spread one flow across cores.

**But we are not at that wall yet — we are at a Phoenix‑port wall.** The decisive evidence:
XAPP1026 shows lwIP **RAW RX hitting 943 Mb/s (~118 MB/s ≈ our Linux number) on a 666 MHz
Cortex‑A9** with HW checksum offload + DMA. Our **1.5 GHz A72 gets ~300 Mb/s raw.** A CPU
~2.3× slower reaching ~3× our throughput proves the gap is *implementation overhead*
(kernel‑mutex `SYS_ARCH_PROTECT`, no HW checksum), **not lwIP's architectural limit.** lwIP
*can* saturate a single gigabit link in RAW mode on weaker hardware than ours.

**What high‑performance lwIP users do:**
1. HW checksum offload + zero‑copy DMA + the RAW/callback API → saturate a single gigabit
   link (XAPP1026 proves it).
2. Accept that the socket/netconn API costs ~30–45% and use RAW for the hot path.
3. For *beyond* single‑gigabit or many flows: run **one lwIP instance per core behind a
   multi‑queue/RSS NIC** (per‑flow affinity — not single‑flow acceleration), or move to a
   multi‑threaded stack. There is no single‑flow multicore path inside lwIP. (This matches
   the memory note "Option C multi‑core/NAPI, owner‑gated.")

**Expected return ladder for us (in order):**
1. **§3‑A lightweight `SYS_ARCH_PROTECT`** — removes ~27 µs/frame → the biggest single jump
   for both raw and socket; no external dependency. *(port change)*
2. **§3‑A2 static memp pools** — removes per‑frame malloc locking; complements A1.
3. **§3‑C1 early `tcp_recved`** — collapses the socket credit‑RTT; biggest *socket‑specific*
   win. *(port change, with a buffering bound)*
4. **§3‑B/C2 window scaling + threshold** — widen the credit pipe once the loop is shortened.
5. **§3‑D HW checksum** — ≤10–20% once the GENET RX‑checksum bug is fixed.

Once 1–4 land, the raw ceiling should approach line rate, and the *residual* becomes lwIP's
genuine single‑thread wall **plus** the userspace socket‑server IPC copy. Only then is
multi‑core RX (Option C, non‑lwIP) the remaining lever — and it is owner‑gated. **Jumbo
frames, the vendor's favorite lever, are unavailable on GENET (§4).**

---

## 7. Sources & citation hygiene

**Primary, directly verified (local authoritative source — this is the exact text behind
the nongnu doxygen pages):**
* `sources/phoenix-rtos-lwip/lib-lwip/src/include/lwip/opt.h` — option defaults & comments
  (TCP_WND, WND_SCALE, WND_UPDATE_THRESHOLD, OVERSIZE, TX_SINGLE_PBUF, checksum,
  core‑locking). Web equivalent: https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__tcp.html
  and https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__lock.html
* Project overlay `…/aarch64a72-generic-rpi4b/lwip/lwipopts.h` — the config that actually
  ships (incl. the window/credit‑latency history comment).
* `sources/phoenix-rtos-lwip/port/protect.c`, `include/arch/sys_arch.h` — `SYS_ARCH_PROTECT`
  = raw `mutexLock` syscall.
* `sources/phoenix-rtos-lwip/lib-lwip/src/api/sockets.c:936,1028` — socket recv uses
  `NETCONN_NOAUTORCVD` + deferred `netconn_tcp_recvd` (upstream design; credit ping‑pong is
  deliberate, not a Phoenix bug).
* `drivers/bcm-genet.c` (MTU=1500, UMAC_MAX_FRAME_LEN=1536, RX‑checksum disabled) and
  `external/linux/.../genet/bcmgenet.h` (`ENET_MAX_MTU_SIZE`≈1536) — GENET jumbo dead end.

**Primary, fetched (PDF, real page numbers verified):**
* **Xilinx XAPP1026 v5.1, "LightWeight IP Application Examples," Nov 21 2014** — Table 1
  (checksum offload), Table 2 p23 (RAW vs socket throughput, 943 vs 521 Mb/s), p4‑6
  (socket overhead), p22 (`-O2` note), p24 (jumbo‑frame recipe).
  https://docs.amd.com (search "XAPP1026").

**Secondary, retrieved only as search‑result summaries — NOT independently fetched
(fandom returned HTTP 402; nongnu.org persistently returned HTTP 429 to the fetch service).
Treat the wording as paraphrase, verify before quoting in a commit:**
* lwIP wiki "Maximizing throughput" — https://lwip.fandom.com/wiki/Maximizing_throughput
* lwIP wiki "Tuning TCP" — https://lwip.fandom.com/wiki/Tuning_TCP
  (TCP_WND ≥ 2·MSS, set as high as possible; TCP_SND_BUF=TCP_WND; enable LWIP_WND_SCALE for
   >64 KB; leave checksums to HW when available)
* lwIP "Zero‑copy RX" — https://www.nongnu.org/lwip/2_1_x/zerocopyrx.html
* lwIP "Multithreading" — https://www.nongnu.org/lwip/2_1_x/multithreading.html
* lwIP "Core locking and MPU" — https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__lock.html
  (CORE_LOCKING is the default and "usually performs better than message passing";
   CORE_LOCKING_INPUT cannot be used from interrupt context — we satisfy this by running
   `tcpip_input` in the driver's thread‑context irqThread).
* Community throughput threads (STMicro/NXP/embeddedrelated) corroborating the single‑
  `tcpip_thread` serialization bottleneck.

**Flagged unverifiable / assumptions:**
* Exact per‑lever percentage gains in §3 are *estimates* derived from the 39.5 µs / 27 µs
  breakdown, not measured — every one needs a bench (`test-cycle-bench.sh` +
  `GENET_RXSTATS_LOG`/`RXPROF`) before being trusted.
* §3‑A assumes a Phoenix futex/fast‑mutex primitive can be built correctly under the
  microkernel; not yet demonstrated.
* Confirm `lwip-core` is compiled `-O2` (XAPP1026 says `-O0` alone costs a large fraction of
  max throughput) — not verified in this pass.
