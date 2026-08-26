# Multi-connection (nconnect-style) NFS striping — feasibility & effort scoping

Date: 2026-08-27
Status: research / analysis only — **no source changed** (read-only scoping)
Author: research agent
Scope: assess whether striping NFS reads across N TCP connections is worth building on
Phoenix-RTOS RPi4, and estimate concrete effort + risk.

Context recap: gigabit NFS-root read throughput is up 7.86 -> 29.9 MB/s (3.8x) via lwIP
work. The single-flow **input ceiling** is ~37.5-42 MB/s because lwIP processes RX on one
`tcpip_thread` under the core lock; NFS sits at ~75-80% of that. The prior alt-IP-stack
analysis (`docs/inprogress/2026-08-26-alternative-ip-stack-analysis.md`) named
multi-connection striping as the one architectural change that parallelizes the bulk
workload *without a kernel change*. This report checks that claim against the actual code.

---

## TL;DR / headline findings

1. **The read path is strictly synchronous, one READ RPC at a time — at BOTH layers.**
   - The fs server (`srv.c`) is single-threaded: exactly one `nfs_loopThread` runs
     `msgRecv -> dispatch -> nfs_ops_read -> nfs_pread (sync) -> msgRespond`, one message
     at a time (`srv.c:251-348`, design comment `srv.c:13-18`). At most **one** `mtRead`
     is ever being serviced, hence at most one READ RPC in flight from the server.
   - Inside the shipped libnfs 6.0.2, each `nfs_pread` issues exactly **one** READ and blocks
     on it: `nfs_pread` -> one `nfs_pread_async` -> `nfs4_pread_async_internal` builds a
     single `PUTFH+READ` COMPOUND, then `wait_for_reply` services until that one reply lands
     (`libnfs-sync.c:723-740, 200-290`; `nfs_v4.c:2874-2910`). A read larger than the
     server's rtmax simply returns a **short** read and the caller re-issues for the rest —
     there is no internal chaining and **no request pipelining** on the connection at all.
     (Note: the older `research/libnfs` clone is a *different* version with an `r_cb`
     multi-read chain; that chain is also strictly sequential, so the finding holds either
     way — but the shipping 6.0.2 tree is the one cited here.)
   - So the pcap "replies stream" observation is TCP **segmentation** of a large (up to
     ~1 MB) READ reply into hundreds of wire packets, **not** multiple concurrent RPCs. This
     **refutes** the task's tentative "pipelining" reading of the pcap: nothing in this path
     overlaps request N+1's send with reply N's receive.

2. **libnfs 6.0.2 has no native multi-connection / nconnect / NFSv4.1-trunking mode.**
   One `struct nfs_context` == one socket == one TCP connection. There is no
   `BIND_CONN_TO_SESSION`, `CREATE_SESSION`, `EXCHANGE_ID`, `nconnect`, or trunking token
   anywhere in the tree (`research/libnfs/lib/`, `include/nfsc/libnfs.h`). libnfs implements
   **NFSv4.0 only** (no minorversion/session machinery in `lib/nfs_v4.c` / `nfs4/nfs4.c`).
   The Phoenix port additionally **excludes** `lib/multithreading.c` from the build
   (`libnfs/port.def.sh:52` — "single-threaded sync build, no HAVE_MULTITHREADING"), so even
   libnfs's own thread-safety shims are not compiled in. Multi-connection therefore means
   **N independent `nfs_context`s** created and managed by the Phoenix nfs-fs itself.

3. **The parallelism has no *source* today.** Because the server is single-threaded and the
   client-side reader is typically one sequential `read()` loop, there is only ever one
   outstanding `mtRead`. N connections do nothing unless *something generates concurrent
   read demand* — either concurrent client readers, or a **speculative readahead engine in
   the fs server** that prefetches ahead of demand across the N connections. Building that
   readahead/prefetch cache is the dominant cost of this project, not "open N sockets."

4. **NFSv4.0 + a single shared client name makes N contexts clobber each other.**
   `nfs_makeContext` sets a *fixed* client name `"phoenix-rpi4-nfsfs"`
   (`srv.c:395`). Two contexts with the same name = same `SETCLIENTID`; the second
   `SETCLIENTID_CONFIRM` (new verifier) **replaces** the first's state on the server (this is
   exactly the RFC 7530 same-id-new-verifier behaviour the reclaim path in
   `nfs_ops.c:133-153` deliberately relies on). So N connections require **N distinct client
   names** -> **N independent clientids** -> N independent lease/state domains, N leases to
   renew, N opens (N stateids) of the same file. This is protocol-legal (the server just sees
   N ordinary clients) but is *not* true trunking — there is no shared session.

**Bottom line recommendation: owner-gate it, alongside the multi-core RX item.** The
realistic payoff is the ~1.3x the context already predicts (30 -> ~40 MB/s, hard-capped by
the single-thread lwIP input ceiling), and capturing it requires building a real
readahead/prefetch subsystem in the fs server plus N-clientid state management — a
1.5-3 week project with real coherency/lease/regression risk on the boot-critical root
filesystem. A **much cheaper** first step with overlapping benefit exists (single-connection
READ pipelining via the libnfs async API — see below); if throughput past 30 MB/s is wanted
autonomously, do that spike first and reserve multi-connection for when the owner also
greenlights the multi-core input work it composes with.

---

## 1. Read-path finding (synchronous vs pipelined) — with cites

**Server layer — one RPC at a time, structurally.**
- `srv.c:251-348` `nfs_loopThread`: a single `for(;;)` around `msgRecv` / switch /
  `msgRespond`. `mtRead` -> `nfs_ops_read(fs, ..., msg.o.data, msg.o.size)` (`srv.c:272-273`).
- `srv.c:13-18` header: *"Single-threaded: the libnfs sync API drives one msgRecv loop."*
  All libnfs access is confined to this one thread by design (the renew helper self-sends a
  message rather than touching libnfs — `srv.c:335-339`, `364-373`).
- `nfs_ops_read` (`nfs_ops.c:409-506`) calls `nfs_pread(fs->nfs, fh, buf, len, offs)`
  (`nfs_ops.c:475`) — the blocking **sync** API. The loop cannot pick up the next `mtRead`
  until this returns.

**libnfs layer (shipped 6.0.2) — one READ per `nfs_pread`, no pipelining.**
- Sync `nfs_pread` (`libnfs-sync.c:723-740`) calls `nfs_pread_async` **once**, then
  `wait_for_reply` (`libnfs-sync.c:200-290`) services the socket until the single
  `pread_cb` (`libnfs-sync.c:708-720`) marks the request finished.
- `nfs_pread_async` (`libnfs.c:1443-1461`) dispatches straight to
  `nfs4_pread_async_internal` (`nfs_v4.c:2874-2910`), which builds a single `PUTFH+READ`
  COMPOUND (`nfs4_op_read`, `nfs_v4.c:2903`). No internal multi-read splitting or batching in
  6.0.2 — a request over rtmax returns short and the caller re-issues.
- Net: at most one READ RPC is ever outstanding on the connection.

**Consequence for throughput.** Each READ is: send request -> wait for the *entire* reply ->
`nfs4_pread_cb` copies it -> return -> server responds `mtRead` -> kernel issues next
`mtRead` -> next READ. The link is idle during every request-turnaround. This per-RPC
serialization gap is precisely the ~20-25% between NFS (30) and the raw input ceiling (~40):
multiple connections would let connection B's reply land during connection A's turnaround.
That also caps the gain at the input ceiling — every reply still funnels through the one
lwIP `tcpip_thread`.

`readmax`/`writemax` are set to 1 MB (`srv.c:411-412`), clamped to the server's rtmax
(Linux nfsd ~1 MB) via FSINFO. `poll_timeout` is 1 ms (`srv.c:404`) so the turnaround gap is
small but non-zero and paid once per RPC.

> Uncertainty: the effective `mtRead` size the kernel pager/readahead hands the server is
> bounded by the message-buffer size, which I did not pin down here. If it is well below
> 1 MB, each `mtRead` is a single sub-`readmax` READ and the serialization gap is paid more
> often — strengthening the case for pipelining. Worth measuring with a quick `mtRead`-size
> probe before committing to any design.

---

## 2. Does libnfs support multi-connection natively? — No

- One `nfs_context` == one connection. No trunking / session / nconnect API exists in the
  6.0.2 tree (verified: no `BIND_CONN_TO_SESSION`, `CREATE_SESSION`, `EXCHANGE_ID`,
  `nconnect`, `multi_connect`, `trunk` tokens in `lib/` or the public header).
- NFSv4.0 only: `lib/nfs_v4.c` and `nfs4/nfs4.c` have no minorversion / SEQUENCE / session
  slots. NFSv4.1 sessions (the mechanism Linux `nconnect` binds extra connections to via
  `BIND_CONN_TO_SESSION`) are simply absent.
- `lib/multithreading.c` (libnfs's own locking for sharing a context across threads) is
  **not built** by the Phoenix port (`port.def.sh:52`). Even if it were, it protects *one*
  context; it is not a multi-connection feature.

**Therefore multi-connection = the Phoenix nfs-fs owns N `nfs_context`s, each with its own
mount (`nfs_mount` re-runs `SETCLIENTID`+confirm), its own socket, and — because of the
NFSv4.0 shared-client-name clobber (finding #4) — its own distinct client name / clientid.**

---

## 3. What it would take to stripe reads across N connections

Ordered from unavoidable to hardest.

**A. N contexts + N distinct clientids (small, but touches boot-critical mount).**
- Add `nfs_makeContext` variant taking an index -> client name
  `"phoenix-rpi4-nfsfs-<i>"` (avoids the same-name state clobber, `srv.c:395` + finding #4).
- Mount all N in `nfs_runTakeover` (`srv.c:550-753`) with the same bounded-retry/deadline
  logic now wrapping one mount. All N must succeed (or degrade cleanly) before takeover —
  more failure surface on the one boot path that must not brick "/".
- Lease renewal (`nfs_renewThread` + `NFS_MSG_RENEW`, `srv.c:335-339,364-373`) must renew
  **all N** leases. N clientids = N independent ~90 s leases; a lapse on any one triggers the
  reclaim path.

**B. Per-context filehandle caching (the real refactor).**
- Today a node caches exactly one `struct nfsfh *n->fh` bound to `fs->nfs`
  (`nfs_node.h:32`), with the #156 lazy-close idle-LRU built around that single fh
  (`nfs_ops.c:311-406`, `nfs_node.h:39-63`). An `nfsfh` is bound to the context that opened
  it (its stateid lives in that clientid), so it is **not** portable across contexts.
- Striping a file's reads across N connections needs **N fhs per node** (`fh[N]`), i.e. N
  `nfs_open`s (N stateids) of the same path, and the lazy-close/idle-LRU/reclaim/invalidate
  logic (`nfs_ops.c` open/close/reclaim; `nfs_node_invalidateHandles`) replicated per
  context. The alternative — open-on-demand per read (the `owned=1` branch,
  `nfs_ops.c:444-465`) — adds an OPEN+CLOSE RPC pair per read and **destroys** the very
  saving #156 exists to provide, so it is not viable for a perf feature.
- Reclaim (`nfs_ops.c:133-165`) becomes per-context: an expiry on context *i* must rebuild
  *only* context *i* and invalidate only its fhs.

**C. The prefetch/readahead engine (the part that actually creates parallelism).**
- With a single-threaded server and a single sequential reader there is only ever **one**
  outstanding `mtRead`; N idle connections buy nothing. To keep N connections busy the server
  must **prefetch speculatively**: on `mtRead(offset X)`, dispatch READ RPCs for
  `X, X+chunk, X+2*chunk, ...` across the N contexts on N worker threads, cache the results,
  and serve later `mtRead`s from that cache.
- That means: a worker-thread pool (one per context — libnfs sync is not concurrency-safe on
  one context, so strictly one thread per context), a bounded read-ahead cache keyed by
  (node, offset), demand/prefetch coordination, eviction, and readahead-window sizing/reset
  on non-sequential access. This is a mini NFS readahead subsystem — the bulk of the work and
  the bulk of the memory footprint on the Pi.
- Message plumbing: the loop `msgRecv`s and hands `(rid, msg)` to a worker; the worker
  `msgRespond`s from its own thread (deferred/cross-thread respond — needs a quick check that
  the kernel permits responding to a saved `rid` from a non-receiving thread; the existing
  splice code sends from other threads but does not defer a *respond*).
- Simpler-but-weaker alternative — skip prefetch and rely on the kernel pager issuing
  concurrent `mtRead`s — **does not work, confirmed from kernel source.** `object_fetchCluster`
  (`sources/phoenix-rtos-kernel/vm/object.c:379-383`, comment `object.c:176-184`) fetches a
  bounded page cluster per fault with **one read for the whole cluster**. So the pager merges
  readahead into a single larger `mtRead`, never overlapping messages — there is only ever one
  `mtRead` outstanding to the fs port. A worker pool without a server-side prefetch engine
  would therefore stay effectively single-connection. **Step C's prefetch engine is mandatory,
  not optional.**

**D. Ordering / coherency / reassembly — mostly a non-issue for reads.**
- Reads are idempotent and offset-addressed (`nfs_pread(..., offs)`), and each `mtRead`
  carries its own `offs`/`len` (`srv.c:273`). Striping is at whole-request / prefetch-chunk
  granularity, so there is **no sub-read reassembly** and no cross-connection ordering
  requirement for the read data itself.
- **Writes must stay on one connection.** N independent clientids/opens of the same file give
  no cross-connection write ordering or coherency guarantee; the workload here is bulk read,
  so pin all writes (and metadata mutations) to context 0 and stripe reads only.
- NFSv4.0 legality: N distinct clientids is fine (server sees N clients). True single-client
  trunking is **not** available (would need NFSv4.1 sessions, which libnfs lacks). Flag: N
  clientids multiply server-side state and, on rapid reboots, the stale-state/`NFS4ERR_EXPIRED`
  churn that #156/`srv.c:386-395` already fights — now times N.

---

## Cheaper alternative that overlaps most of the benefit

**Single-connection READ pipelining** via libnfs's async API. Keep one context, but instead
of one blocking `nfs_pread`, keep a small window (e.g. 4-8) of `nfs_pread_async` READs in
flight and drive `nfs_service()` until they retire, feeding the kernel's readahead. This
closes the per-RPC serialization gap (finding #1) on the single connection **without** N
clientids, per-context fh caching, or the lease-times-N problem.

**But be honest about its ceiling: it is capped well below multi-connection.** Everything
still funnels through **one** socket and one per-socket server thread, and the task's own
numbers put single-socket recv at ~33 MB/s vs raw ~42, with that residual attributed to
per-socket IPC round-trip cost. Pipelining cannot beat the single-socket recv path, so its
realistic cap is ~30 -> ~33 (a modest ~+10%), **not** the full ~40. The reason
multi-connection can reach ~40 is precisely that N connections give N `socket_thread`s, which
parallelizes the socket-server IPC that single-connection pipelining cannot. So this spike is
worth doing first as a cheap, low-risk win *and* as the async-plumbing prerequisite for the
multi-connection prefetch engine — but it captures only a fraction of the 1.3x, not most of
it. Its measured result then tells you whether the remaining ~33 -> ~40 gap justifies the
full multi-connection build.

---

## 4. Effort, risk, and recommendation

**Effort (multi-connection, full):**
- A (N contexts + N clientids + N-lease renew): ~2-3 days.
- B (per-context fh caching + reclaim/invalidate refactor): ~3-5 days; this is the sharp edge
  — it rewrites the #156 lazy-close/idle-LRU invariants on the boot-critical path.
- C (worker pool + readahead/prefetch cache + cross-thread respond): ~1-2 weeks; genuinely
  new subsystem, plus HW bring-up/tuning of window size and cache cap under Pi memory limits.
- Integration + regression hardening on the NFS-root boot (takeover timing, reclaim-times-N,
  reboot stale-state churn): several more days.
- **Total realistic: ~1.5-3 weeks**, most of it in C and in re-hardening the root-fs boot.

**Risk:** medium-high. It touches the one filesystem the system boots on; the fh-cache and
reclaim invariants are subtle and already carry #156 scar tissue; N clientids multiply the
`NFS4ERR_EXPIRED`/stale-state failure modes that intermittently threatened the boot; and the
payoff is hard-capped at the single-thread input ceiling (~40 MB/s) regardless of N.

**Payoff:** ~1.3x (30 -> ~40 MB/s), never line rate — line rate (112 MB/s, Linux) needs
multi-core RX input, which is owner-gated. Multi-connection *composes* with that multi-core
work (more flows to spread across cores) but delivers only the ceiling-limited 1.3x on its
own.

**Recommendation: owner-gate multi-connection, and bracket it with the multi-core RX item**
— they are the same "parallelize the input" bet and are worth deciding together. It is not a
"do it autonomously" change: the effort/risk on the boot-critical root fs is disproportionate
to a ceiling-capped 1.3x. If autonomous throughput-past-30 is desired now, do the
**single-connection async READ pipelining** spike first (cheaper, lower risk, overlapping
benefit, and a prerequisite for C) and let its measured result decide whether the remaining
gap justifies the full multi-connection build.

---

## Uncertainties / to verify before acting
- Effective `mtRead` size handed to the fs server (message-buffer cap) — governs how often
  the per-RPC gap is paid and how much pipelining/striping can recover. Measure first.
- (RESOLVED — moved to a finding, section 3C) The kernel pager never has >1 `mtRead`
  outstanding: `vm/object.c:379-383` fetches a page cluster with one read per fault. Step C's
  prefetch engine is therefore mandatory.
- That the kernel permits `msgRespond` on a saved `rid` from a thread other than the one that
  `msgRecv`'d it (needed for the worker-pool respond path).
- Exact host-nfsd behaviour under N simultaneous clientids from one box on rapid reboot
  (stale-state accumulation vs the single-client replace semantics #156 depends on).

## Key source references
- `sources/phoenix-rtos-filesystems/nfs/srv.c:13-18, 251-348, 364-373, 380-436, 550-753`
  (single-loop model, renew-via-self-message, `nfs_makeContext` fixed client name, takeover).
- `sources/phoenix-rtos-filesystems/nfs/nfs_ops.c:311-406` (open/lazy-close),
  `409-506` (read path, sync `nfs_pread`), `133-165` (reclaim).
- `sources/phoenix-rtos-filesystems/nfs/nfs_node.h:27-63` (single `n->fh`, idle-LRU).
- `sources/phoenix-rtos-ports/libnfs/port.def.sh:44-66` (NFSv4.0 sync-only build,
  `multithreading.c` excluded).
- `sources/phoenix-rtos-kernel/vm/object.c:176-184, 379-383` (`object_fetchCluster` — one
  clustered read per fault; the pager never overlaps `mtRead`s).
- Shipped libnfs 6.0.2 tree
  (`.buildroot/_build/aarch64a72-generic-rpi4b/port-sources/libnfs-6.0.2/libnfs-libnfs-6.0.2/`):
  `lib/libnfs-sync.c:200-290, 708-740` (`nfs_pread` = one async READ + `wait_for_reply`, no
  loop), `lib/libnfs.c:1443-1461` (`nfs_pread_async` -> single dispatch), `lib/nfs_v4.c:2874-2910`
  (single PUTFH+READ COMPOUND per call). No `BIND_CONN_TO_SESSION`/`CREATE_SESSION`/`nconnect`
  tokens present -> NFSv4.0 only, no trunking.
</content>
