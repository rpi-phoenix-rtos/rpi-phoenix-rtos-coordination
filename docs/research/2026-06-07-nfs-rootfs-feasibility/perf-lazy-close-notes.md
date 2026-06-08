# NFS fs server — lazy-close fh cache (#156)

Date: 2026-06-08. Scope: `sources/phoenix-rtos-filesystems/nfs/`. HOST build only
(`--scope core --variant nfsroot`); HW timing measured by the orchestrator.

## Problem

The kernel program loader faults pages **serially**: `object_fetch` does
`proc_open` + `proc_read(4 KB)` + `proc_close` **per page**. Against the NFS fs
server every page therefore hit a fresh `mtOpen`/`mtRead`/`mtClose` round, and
each of those expanded into libnfs sync RPCs:

- `mtOpen` → `nfs_ops_open` → `nfs_refreshStat` (**`nfs_lstat64` RPC**) + `nfs_open` (**RPC**)
- `mtRead` → `nfs_pread` (**RPC**)
- `mtClose` → at `refs==0`, `nfs_close` (**RPC**)

So **~4 RPCs per 4 KB page**. Execing a ~300 KB binary (~75 pages) cost ~300
serial round-trips → exec took many seconds; lua/busybox sometimes didn't finish
inside a 35 s capture.

## Fix: lazy-close handle cache (handle caching, not data caching)

Keep the NFS fh open past `refs==0` in a small LRU so the next open (the loader's
next page) reuses it. Only the fh is cached — **no file data is cached**, so there
is no stale-data risk; correctness is preserved.

### Where it lives

- **`nfs_node.{h,c}`** — the idle LRU is part of the node table (`nfs_nodeTree_t`):
  `idleHead`/`idleTail`/`idleCount`, plus `idle`/`idleNext`/`idlePrev` on each
  `nfs_node_t`. Helpers are **structural only** (`nfs_node_idlePush`,
  `nfs_node_idleUnlink`, `nfs_node_idleLru`) — they never call `nfs_close`
  (node.c has no libnfs context). Intrusive doubly-linked list, MRU at head,
  mirroring dummyfs's `next`/`prev` style.
- **`nfs_ops.c`** — all `nfs_close`-bearing logic lives here, where `fs->nfs` is
  available. This split keeps node.c pure and the teardown paths clean.

### Behavior

- **close → refs==0** (`nfs_ops_close`): do NOT `nfs_close`. If `fh != NULL` and
  not already idle, `nfs_node_idlePush` (MRU head, `idle=1`). Then if
  `idleCount > NFS_IDLE_MAX`, evict the LRU tail: `nfs_close` its fh, `fh=NULL`,
  `idleUnlink`, and emit one low-rate log line `nfs-fs: fh-cache evict, N idle`
  (only on eviction, not per op).
- **open → refs 0→1** (`nfs_ops_open`): if the node is `idle` and `fh != NULL`,
  `idleUnlink` and reuse the fh — **skip both `nfs_open` and `nfs_refreshStat`**.
  Only the cold path (`fh == NULL`) does `nfs_refreshStat` + `nfs_open`.
- **destroy** (`nfs_ops_destroy`): already `nfs_close`s `fh` then
  `nfs_node_remove`; `nfs_node_remove` now defensively `idleUnlink`s first.
- **unlink** (`nfs_ops_unlink`): a `refs==0` node may still hold a cached fh on
  the idle LRU — close it before `nfs_node_remove`.
- **`nfs_node_remove`**: defensive `idleUnlink` before free so no teardown path
  can leave a dangling idle-list pointer.

### Cap + eviction

`NFS_IDLE_MAX = 16` cached-open fhs. Bounds open-fh consumption on the NFS
server/client (lazy-close can't exhaust either side). The loader pages one file
at a time, so 16 is ample headroom while staying tiny. Eviction is strict-LRU on
the tail.

### Why skipping `nfs_refreshStat` on reuse is safe (not a tradeoff)

`nfs_refreshStat` stats **by path** and updates `n->type`; it never reopens
`n->fh`. Under lazy-close the cached fh persists across `refs==0`, so if a file is
redeployed server-side (unlink+recreate → new inode), the cached fh is stale
**regardless** of whether refreshStat ran. Skipping it on the idle-reuse path
therefore loses nothing beyond what lazy-close already gives up, and a file the
loader is actively paging in is not being redeployed mid-exec. The cold path
(`fh == NULL`) still does refreshStat for the `otFile` type + fail-fast-if-gone.

## RPC math (corrected — the task's "3→1 / 225→75" undercounted the lstat)

- **Before:** per page = `lstat + nfs_open + pread + nfs_close` = **4 RPCs/page** →
  ~300 RPCs for a 75-page binary.
- **After:** page 0 = `lstat + open + pread` = 3; every later page = `pread` only
  = **1 RPC/page**. Total ≈ **N + 2** (~77 for 75 pages).
- ≈ **4× fewer RPCs**; steady state **1 RPC/page**.

## Thread-safety

The server is single-threaded (one `msgRecv` loop, `nfs_ops.h` header note), so
the idle LRU needs no lock beyond the implicit serialization of that loop. No new
data race introduced; the open-on-demand `fh==NULL` read/write fallback is
unchanged.

## Known residual (acceptable, in scope)

Lazy-close changes one behavior vs. today: a server-side replace of a file that
currently has a cached fh is shadowed by the stale fh until eviction or an
unlink-through-this-server. This is the documented scope of #156 (handle caching,
not data caching). Data readahead is explicitly **out of scope** here (separate
later step, coherence concerns).

## Build status

`./scripts/rebuild-rpi4b-fast.sh --scope core --variant nfsroot` — **clean**,
image exported, `nfsroot` launch line intact
(`app ram0 -x nfs;/;10.42.0.1;/;v4;takeover ...`). The rebuilt
`_fs/.../root/sbin/nfs` contains the new code (eviction string present). The
existing T2/T3 DIAG prints were intentionally kept.

## What the orchestrator should measure on HW (nfsroot)

- Time to exec a big binary before vs after — e.g. `/usr/bin/lua -v` or `/bin/sh`
  launching. Expect a **large drop** (steady-state 1 RPC/page vs 4).
- `busybox` ash must still launch.
- **0 faults**; boot still reaches psh.
- Optionally watch for `nfs-fs: fh-cache evict, N idle` to observe cache pressure
  (should be rare/absent for single-binary exec given cap 16).
