# T2 + T3b wiring — NFS mt* filesystem server

**Status:** Source written, image BUILT, fs server BUNDLED + launch lines embedded
(host-only). **Runtime is UNVERIFIED here** — no HW/netboot was run in this session.
The orchestrator runs the Pi test; see §"Expected HW behavior" for what to watch.

Builds on T0 (libnfs.a) + T1 (nfs-smoke, HW-proven mount+read+write). This stage adds
a dummyfs-shaped userspace VFS server that wraps libnfs and exposes the NFS export as a
Phoenix filesystem mounted at **`/nfstest`** (subtree, NOT root — root is T3).

---

## 1. Files created

| Path | Role |
|------|------|
| `sources/phoenix-rtos-filesystems/nfs/srv.c` | `main()`: arg parse, DHCP-wait, `nfs_mount`, `portCreate`, async `mtSetAttr(atDev)` splice, the `msgRecv` dispatch loop. Both the loop and the splice run on explicit **64 KB** (`16*_PAGE_SIZE`) stacks. |
| `sources/phoenix-rtos-filesystems/nfs/nfs_node.{c,h}` | id↔path node table: two rbtrees (by id, by path) over one node set + monotone id allocator. Root = id 0, path `/`. |
| `sources/phoenix-rtos-filesystems/nfs/nfs_ops.{c,h}` | the `mt*`→libnfs handlers. |
| `sources/phoenix-rtos-filesystems/nfs/Makefile` | builds `libnfsfs` static lib + the `nfs` binary (→ `/sbin/nfs`); links `-lnfs`; `-Wno-error=stringop-truncation` for the ifstatus parser (same as nfs-smoke). |

Registered:
- `sources/phoenix-rtos-filesystems/_targets/Makefile.aarch64a72-generic`: `DEFAULT_COMPONENTS += nfs`.
- `sources/phoenix-rtos-project/.../ports.yaml`: `libnfs` (already present from T1).
- `sources/phoenix-rtos-project/.../user.plo.yaml`: `mkdir;/nfstest` then `nfs;/nfstest;10.42.0.1;/;v4`, after lwip + nfs-smoke, before psh (nfs-smoke kept as a baseline probe).

## 2. mt* → libnfs mapping table

| mt* | handler | libnfs call(s) | notes |
|-----|---------|----------------|-------|
| `mtLookup` | `nfs_ops_lookup` | `nfs_lstat64` per component | **don't-follow** so symlinks keep `otSymlink`; returns **chars consumed** (not 0); fills both `o.lookup.fil` and `.dev`; `..`-at-root returns `fs->parent`. |
| `mtOpen` | `nfs_ops_open` | `nfs_lstat64` (re-stat) + `nfs_open` (files only) | re-stat on open so a redeployed file isn't shadowed (OQ-B); caches `fh`, bumps `refs`. |
| `mtClose` | `nfs_ops_close` | `nfs_close` | drops `refs`; closes `fh` at 0. |
| `mtRead` | `nfs_ops_read` | `nfs_readlink` (otSymlink) / `nfs_pread` (file) | branches on node type — symlink read = target string (no `mtReadlink` enum exists). Opens-on-demand if no cached fh. |
| `mtWrite` | `nfs_ops_write` | `nfs_pwrite` | open-on-demand `O_RDWR` if no cached fh. |
| `mtTruncate` | `nfs_ops_truncate` | `nfs_ftruncate` (open) / `nfs_truncate` | |
| `mtGetAttr` | `nfs_ops_getattr` | one `nfs_lstat64` | maps `atMode/atSize/atType/atMTime/...`; `atPollStatus`=always-ready. |
| `mtGetAttrAll` | `nfs_ops_getattrAll` | one `nfs_lstat64` | `_phoenix_initAttrsStruct(attrs,-ENOSYS)` then fills known fields. |
| `mtSetAttr` | `nfs_ops_setattr` | `nfs_chmod`/`nfs_truncate`/`nfs_ftruncate`/`nfs_utimes` | `atMTime/atATime` re-stats the other time and writes both (libnfs `nfs_utimes` takes both). `atDev` = **accept-only** (see incomplete). |
| `mtCreate` | `nfs_ops_create` | `nfs_creat`+**`nfs_chmod`** / `nfs_mkdir2` / `nfs_symlink` / `nfs_mknod` | by `i.create.type`. **chmod-after-creat** fixes the v4 mode-000 bug (below). |
| `mtDestroy` | `nfs_ops_destroy` | (none) | closes fh, drops node. |
| `mtUnlink` | `nfs_ops_unlink` | `nfs_unlink` / `nfs_rmdir` | chooses by `nfs_lstat64` (don't-follow): symlink-to-dir → `nfs_unlink`. |
| `mtLink` | `nfs_ops_link` | `nfs_link` | hardlink. |
| `mtReaddir` | `nfs_ops_readdir` | `nfs_opendir`/`nfs_readdir`/`nfs_closedir` | one dirent per call, cumulative cookie (below). |
| `mtStat` | `nfs_ops_statfs` | `nfs_statvfs64` | fills `struct statvfs`. |
| `mtDevCtl` | — | (none) | `-EINVAL` (like dummyfs). |

libnfs failures map to negative errno via `nfs_err()` (`-1`→`-EIO`).

## 3. oid scheme

Phoenix is handle-based (`oid_t {port,id}`); libnfs is path-based. The node table keeps a
**bidirectional id↔path map** so a repeated `mtLookup` of the same path returns the **same
id** (no id leak, stable inode identity). `oid.id` indexes the by-id rbtree; the export
root is **id 0, path `/`** (seeded by `nfs_node_init`, mirrors dummyfs `DUMMYFS_ROOTID`).
New ids are a monotone counter. Paths are export-relative; `nfs_node_joinPath` collapses the
parent/child slash.

## 4. Mount mechanism (/nfstest splice — the `atDev` path)

Mirrors `dummyfs_do_mount` (the ia32 `dummyfs -m /tmp` and ext2 SD-root precedent), NOT
`portRegister("/")` (that is T3) and NOT the devfs `-N` named-port shortcut:

1. `mkdir;/nfstest` (separate plo launch line) creates the mountpoint dir first.
2. `nfs` server: DHCP-wait → `nfs_init_context` + `nfs_set_version(v4)` + `nfs_set_timeout(5000)` (bounds every RPC, §5.5) + `nfs_mount` → `portCreate` (NOT register).
3. Async splice thread (own 64 KB stack): spin on `lookup("/")` until the root fs is up, then `lookup(/nfstest)` + `stat` (assert dir), record its parent oid (for `..`-at-root), and `msgSend(mtSetAttr, atDev, data=&{my_port,0})` to the parent fs. That splices our root under `/nfstest`.

`main()` refuses a `/` mountpoint arg (guards against accidental T3).

## 5. The chmod-after-creat fix (v4 mode-000 bug)

HW-observed in T1: `nfs_creat(...,0644,...)` over **NFSv4** leaves the file mode **000** on
the server. `nfs_ops_create` issues `nfs_chmod(path, mode & ALLPERMS)` immediately after a
successful `nfs_creat`, so created files (esp. executables needing +x for the T3b exec test)
get the requested perms. (v3 would not need this, but we prefer v4 + chmod.)

## 6. Stack sizes (#120 lesson)

Both the `msgRecv` loop AND the async splice run on explicit **`16*_PAGE_SIZE` = 64 KB**
stacks (`beginthread`). The default 8 KB pool-thread stack overflows on this port's deep fs
chains (#120); the NFS chain (msgRecv→handler→libnfs sync→XDR→socket-to-lwip) is deeper than
ext2-over-SD. `main()` parks itself in a sleep loop so the port stays alive (never returns).

## 7. Build + verify (host-only, reproduce)

```sh
# libnfs.a already staged from T1 in .buildroot/_build/.../lib/ (prepare-buildroot
# preserves _build). If a clean tree: run the ports pass first (see T1 notes §5).
./scripts/rebuild-rpi4b-fast.sh --scope core --variant netboot
```

Verification (this session):
```
prog.stripped/nfs                       -> 283472 bytes (links libnfs.a)
strings .../loader.disk | grep 'nfs;/nfstest'
  -> app ram0 -x nfs;/nfstest;10.42.0.1;/;v4 ddr ddr
strings .../loader.disk | grep 'mkdir;/nfstest'
  -> app ram0 -x mkdir;/nfstest ddr ddr
strings .../loader.disk | grep -c 'nfs-fs:'   -> 14   (srv.c text bundled)
```
Also syntax-clean under `-Wall -Wextra -Werror -Wstrict-prototypes`.

## 8. Implemented vs stubbed/incomplete

**Implemented:** lookup, open, close, read (file + symlink-target), write, truncate,
getattr, getattrAll, setattr (mode/size/times), create (file/dir/symlink/mknod), destroy,
unlink (file/dir/symlink), link (hardlink), readdir, statfs.

**Incomplete / known-soft (watch, don't over-trust):**
- **Inbound `atDev` (nested mount under /nfstest) = accept-only** — we don't yet forward ops
  to a child fs mounted onto a dir inside the NFS tree. Returns success; nested mounts won't
  actually route. Rare for T3b.
- **`mtCreate` does not pre-check EEXIST** — relies on libnfs/server to reject; the VFS
  normally lookups first, so benign.
- **`mknod` uses dev=0** — char/block special files won't carry a real device number.
- **readdir `d_type`** comes from `nfsdirent.mode`, which can be 0 if v4 readdir carries no
  attrs → `otUnknown`. `ls` (names) works; `ls -l` does its own getattr so it's correct.
- **readdir re-opens the dir every call** (snapshot-per-call) → O(n²) for a big `ls`.
  Correct, just slow; streaming/caching is a perf follow-up.
- **`cd /nfstest/..`** — `..`-at-root returns `fs->parent` for both res+dev (load-bearing
  part, correct) but the char-count return (`len+2`) is a guess vs dummyfs's murky `len-1`;
  not in the core acceptance, verify separately if needed.
- **Single-threaded** (libnfs sync). Head-of-line blocking under concurrent exec is the
  §5.5/§8 MT upgrade — deferred per instructions. `nfs_set_timeout(5000)` bounds each RPC so
  one dropped packet can't wedge the loop forever.

## 9. Expected HW behavior (for the orchestrator)

Boot a netboot image with the host NFS export up (10.42.0.1:/srv/phoenix-rpi4-nfs, fsid=0).
**Watch UART for, in order:**
1. `nfs-fs: interface bound, ip=10.42.0.x`
2. `nfs-fs: mounted 10.42.0.1:/ via v4`
3. **`nfs-fs: mounted at /nfstest`** ← the make-or-break splice line.
4. Boot reaches `(psh)%` (the `nfs` server backgrounds like lwip/dummyfs-root; psh-reached
   is expected, not at risk).
5. **0 Data Aborts** (the 64 KB stacks are the hypothesis under test — a list-corruption /
   Data Abort points back at stack size first, then HOL/timeout, then attr cache).

**Then from psh (T2/T3b acceptance):**
- `ls /nfstest` → lists the host export's entries (e.g. `etc`, `test`, `bin`, the
  `nfs-smoke-marker.txt` from T1).
- `cat /nfstest/etc/hostname` → byte-identical to the host file.
- write a file under `/nfstest/`, verify content on the host (round-trip).
- **exec a binary from `/nfstest/`** (T3b headline) — small then large, expect deterministic
  exit code + stdout, 0 faults across repeats.

If `nfs-fs: mounted at /nfstest` never prints: check the `mkdir;/nfstest` line ran (the
splice `lookup`+`stat` needs the dir to pre-exist) and that `/` (dummyfs-root) came up.
For triage, `nfs-smoke` (still launched) isolates libnfs/network from the mt* layer.
