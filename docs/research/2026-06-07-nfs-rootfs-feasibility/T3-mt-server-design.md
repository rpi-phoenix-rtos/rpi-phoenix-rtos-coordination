# #156 residual (g): multithreaded NFS fs server — design (implementation DEFERRED to attended)

**Date:** 2026-06-09. **Status:** designed, NOT implemented. Concurrency correctness in a
shared-state fs server is the textbook "statistical regression invisible in 1–2 boots"
class (cf. memory `feedback_unattended_scoping`), so implementation is an **attended** task
with a stress harness + soak. This doc is the plan to pick up then.

## Why MT, and what it does NOT fix
The server today runs a **single** `nfs_loopThread` (srv.c) doing `msgRecv` → dispatch →
`msgRespond` in a loop. Every request is serialized end to end, including the libnfs RPC
round-trip to the host. Under a single client (psh) this is fine and is NOT the exec-speed
problem — that was the per-page RPC count, already fixed by the lazy-close fh cache
(f14838c). MT buys **concurrency across clients**: while client A blocks on a slow NFS RPC,
client B's request can be served. It does nothing for single-stream latency.

So MT is only worth it once there are concurrent FS users (e.g. lighttpd serving while psh
runs, or parallel builds on the NFS root). Until then it is latent risk with no payoff —
which is the other reason to defer it rather than ship it speculatively.

## The hard constraint: libnfs is single-threaded
libnfs's sync API is a single `poll`-driven loop over one socket/context. A
`struct nfs_context` is **not** safe to call from two threads concurrently. Two designs
follow from that:

### Design 1 — worker pool + ONE shared context under a lock (REJECTED)
N threads `msgRecv` on the same port; each takes a global mutex around every libnfs call.
Result: all libnfs work still serializes on the mutex, so there is **no I/O concurrency**
— a second thread just blocks on the lock while the first does its RPC. Pure overhead +
new lock-ordering risk, zero benefit. Do not build this.

### Design 2 — worker pool + ONE libnfs context PER WORKER (the real design)
Spawn N worker threads, each `msgRecv`-ing on the shared server port (Phoenix supports
multiple receivers on one port — same pattern as `storage_run(2, ...)` used for the SD
pool, devices 07bb181). Each worker owns its **own** `struct nfs_context` (its own TCP
connection + mount handle to the host export). Now worker A's RPC and worker B's RPC run
truly concurrently on separate sockets. The host nfs-kernel-server handles N client
connections fine.

Shared mutable state that then needs locking:
- **The node table** (`nfs_node.{c,h}`: the path→node hash, refs, type, mnt). Concurrent
  `nfs_node_get`/alloc/free across workers → must be under a node-table mutex (or a
  fine-grained per-bucket lock). The lazy-close idle LRU (idleHead/idleTail/idleCount and
  per-node idle/idleNext/idlePrev) is shared state too and MUST move under the same lock —
  today it is single-thread-implicit.
- **The fh cache lifetime**: an fh parked on the idle list by worker A may be reused by
  worker B. The `nfs_fh` belongs to A's context, so a parked fh is only reusable by the
  **same** context. => the idle cache must become **per-context** (per worker), not global,
  OR the cache key must include the owning context and reuse must be context-matched.
  Simplest correct form: per-worker idle LRU. This slightly lowers hit rate (a path
  reopened on a different worker misses) but keeps correctness trivial.

### Recommended shape (Design 2, conservative)
- `NFS_WORKERS` = 2 to start (matches the SD pool count; enough to prove concurrency
  without a thundering herd against the host). Make it a compile constant.
- Per-worker: own `nfs_context` (mount the export N times at startup), own idle fh LRU.
- Shared under one `nfs_nodeLock` mutex: the path→node hash and node refcounts only.
  Keep the critical section tiny — never hold `nfs_nodeLock` across a libnfs RPC (look up /
  refcount the node under the lock, drop it, then do the RPC). Holding it across an RPC
  reintroduces Design-1 serialization and risks priority inversion.
- The takeover/root-register path stays single-threaded (runs once at startup before
  workers spin up); only the steady-state request loop becomes a pool.

## Risks (why attended)
1. **Lock-across-RPC** mistakes → either serialization (no benefit) or deadlock/inversion.
2. **Node lifetime races** → use-after-free of a node freed by one worker while another
   holds a stale pointer. Refcount discipline must be exact; these surface only under
   concurrent load, not in a 1-boot smoke test.
3. **fh/context cross-use** → a parked fh reused by the wrong context = wrong-file reads or
   ESTALE. The per-worker-cache rule above avoids it but must be enforced.
4. **Host connection count / fd limits** under many workers.

## Validation plan (the part that makes it attended)
- A **concurrent stress harness**: e.g. launch 2+ readers on the Pi (busybox `sh` loops
  cat-ing distinct large files from the NFS root) plus a writer, run for minutes, diff
  against host content; assert zero corruption, zero ESTALE, zero faults. A single netboot
  smoke cannot exercise the races — needs sustained concurrent load + a soak (≥several
  minutes, ideally ≥2 boots).
- Measure: concurrent throughput vs the single-thread baseline (should scale with workers
  for independent streams), and confirm single-stream latency is unchanged.

## Bottom line
Implement Design 2 (per-worker context, per-worker fh cache, tiny shared node-lock,
NFS_WORKERS=2) **attended**, behind the stress harness above. Do not ship MT from an
unattended session — a concurrency bug here corrupts the rootfs silently and won't show in
the boot smoke that gates unattended work.
