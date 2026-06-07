# VFS `mt*` protocol mapping + the rootfs/network ordering problem

Two things here: (A) how a Phoenix userspace fs-server works and how each `mt*`
message maps to a libnfs call; (B) the boot-ordering constraint that governs
NFS-as-root, and the three deployment options ranked by risk.

---

## A. The fs-server model and `mt*` → libnfs mapping

### The server skeleton (model on `dummyfs`, not on `libext2`)

A Phoenix userspace filesystem is a process that: creates a port, registers it in
the namespace, then loops on `msgRecv` dispatching `mt*` messages. Canonical
skeleton — `phoenix-rtos-filesystems/dummyfs/srv.c`:

- mount point registration: `portCreate(&port)` then `portRegister(port, "/", ...)`
  for root, or `portRegister(port, mountpt, ...)` for a subtree (`srv.c:219-241`).
- main loop: `for(;;){ msgRecv(port,&msg,&rid); switch(msg.type){...};
  msgRespond(port,&msg,rid); }` (`srv.c:278-357`).

**Why model on dummyfs, not ext2/libstorage:** ext2 (`phoenix-rtos-filesystems/
ext2`) and the `libstorage`/libext2 pattern assume a **block device** underneath
(read/write sectors via a storage `oid`). NFS has **no block device** — it is
network-backed, so its backing store is a TCP socket to the host, not a
`libstorage` device. That makes an NFS server structurally identical to
`dummyfs` (a standalone server that *synthesizes* file contents), with libnfs
replacing dummyfs's in-RAM object store. Confirmed: dummyfs's loop and registration
have **no `libstorage`/`storage_*` calls**; it owns its data directly.

### `mt*` → libnfs call mapping

The fs-server holds one `struct nfs_context *nfs` (after `nfs_mount`) and maps each
message. The Phoenix `oid_t {port,id}` is the file handle: the server keeps an
`id → {NFS path or struct nfsfh*}` table (an idtree, like dummyfs's object store).

| Phoenix message (`dummyfs/srv.c` line) | Handler args | libnfs call(s) (`include/nfsc/libnfs.h` line) |
|----------------------------------------|--------------|-----------------------------------------------|
| `mtLookup` (335) | parent oid + name → returns child oid | resolve child path → `nfs_stat64` (520) to confirm + allocate id |
| `mtOpen` (284) | oid | `nfs_open(path, flags, &nfsfh)` (675); cache fh under id |
| `mtClose` (288) | oid | `nfs_close(nfsfh)` (709) |
| `mtRead` (292) | oid, `i.io.offs`, `o.data`, `o.size` | `nfs_pread(nfsfh, buf, count, offset)` (740) |
| `mtWrite` (296) | oid, offs, `i.data`, `i.size` | `nfs_pwrite(nfsfh, buf, count, offset)` (862) |
| `mtTruncate` (300) | oid, `i.io.len` | `nfs_ftruncate`(1091) on open fh / `nfs_truncate(path,len)`(1061) |
| `mtGetAttr` (320) | oid, `i.attr.type` → `o.attr.val` | `nfs_stat64` (520) → map fields (size/mtime/mode/...) |
| `mtGetAttrAll` (324) | oid → `struct _attrAll` | one `nfs_stat64`, fill all attrs |
| `mtSetAttr` (316) | oid, type, val | `nfs_chmod`(1569)/`nfs_chown`(1658)/`nfs_utimes`(1752)/`nfs_truncate`(1061) |
| `mtCreate` (308) | parent oid, name, mode, type, dev → child oid | `nfs_creat`(1208) / `nfs_mkdir2`(1148) / `nfs_mknod`(1237) / `nfs_symlink`(1911) by type |
| `mtDestroy` (312) | oid | drop id; for unlink-on-last-close semantics, track refcount |
| `mtUnlink` (343) | parent oid, name | `nfs_unlink`(1268) (file) / `nfs_rmdir`(1177) (dir) |
| `mtLink` (339) | parent oid, name, target oid | `nfs_link`(1962) |
| `mtReaddir` (347) | oid, `i.readdir.offs`, `o.data`, `o.size` | `nfs_opendir`(1302)/`nfs_readdir`(1338)/`nfs_closedir`(1379); serialize `struct dirent` into `o.data` like dummyfs's `readdir` |
| `mtStat` (352) | → `o.data` (statfs) | `nfs_statvfs64`(1498) |
| `mtDevCtl` (304) | — | `-EINVAL` (NFS exposes no devctls), same as dummyfs |

Notes:
- The server is **synchronous one-message-at-a-time** (dummyfs is too). libnfs's
  sync API blocks in its own `poll` loop per call — a clean fit. Concurrency
  (multiple in-flight RPCs) is a later optimization using libnfs async + folding its
  service fd into the server loop.
- Path reconstruction: NFS is path-based at the libnfs sync API level, while Phoenix
  is handle (`oid`) based. The server maintains `id → absolute path` (and/or
  `struct nfsfh*` for open files), exactly the bookkeeping dummyfs's `object.c`
  already does for in-RAM nodes — reuse that structure.

---

## B. The rootfs + network ordering problem

### Today's boot order (`_projects/aarch64a72-generic-rpi4b/user.plo.yaml`)

```
kernel + dtb                                    (lines 6-8)
[netboot] dummyfs-root  → owns "/"              (line 16)
dummyfs;-N;devfs;-D     → devfs as named port   (line 21)
pl011-tty                                       (line 22)
[sd] bcm2711-emmc -r ... → ext2 as "/"          (line 28, sd variant only)
mkdir /dev ; bind devfs /dev                    (lines 29-30)
posixsrv                                        (line 34)
... thermal, hwrng, fb, gpio, usb ...           (lines 45-74)
lwip;genet:0xFD580000:...                        (line 77)   <-- network up HERE
psh                                             (line 78)
go!
```

**The constraint:** `"/"` is established *first* (line 16, dummyfs-root), but the
network (`lwip;genet`) starts *much later* (line 77). lwIP uses **DHCP**
(`lwip/lwipopts.h:24 LWIP_DHCP 1`; `netifapi_dhcp_start` at
`phoenix-rtos-lwip/port/sockets.c:380`), so the Pi gets `10.42.0.x` only after line
77 runs and DHCP completes.

**DHCP is asynchronous — the NFS client must wait for it (do NOT assume "after the
lwip line = address is up").** Launching `lwip;genet` only *starts* the lwIP process;
DHCP discover→ack finishes seconds later while subsequent plo lines and `go!` run.
A naive `-x nfs;...` on the next line calls `nfs_mount` before the interface is
bound → no-route. **Nothing in the tree waits for DHCP today** (no `dhcp_supplied`/
bound-wait found anywhere), so the NFS client itself must poll interface status
(the `netif_is_dhcp`/address state behind `devs.c:242` `%s%d_dhcp=%u`) with a
timeout before mounting. This is OQ-4 in note 02; it is a step in T1 and a hard,
fallback-bearing requirement for option (c).

**The hard part for NFS-as-root:** the namespace does **not** allow re-owning `"/"`.
`user.plo.yaml:13-14` documents the #120 lesson verbatim: dummyfs-root must NOT run
in the sd variant *"or it would own '/' and the ext2 mount's `portRegister('/')`
would fail with -EEXIST."* A grep of the tree finds **no `pivot_root`, no umount of
`"/"`, no re-`portRegister('/')` path** — Phoenix has no rootfs-pivot mechanism.
So "mount NFS over an existing `/`" is **not** available.

**The encouraging part:** the *same* file proves a server can come up and register
its port **before `"/"` exists** — `devfs` registers as a *named port* at line 21
and `create_dev` targets it directly, and the **sd variant already mounts its real
root (ext2) at line 28 with dummyfs-root absent** so its `portRegister("/")`
succeeds. That is the existence proof for option (c): if the network + NFS server
run *before* dummyfs-root and the NFS server calls `portRegister("/")`, it owns the
real root with no conflict.

### The three options

#### (a) initramfs / tiny RAM root → pivot to NFS
A minimal RAM `"/"` brings up network + NFS, then pivots root to the NFS mount.
**Phoenix has no pivot_root** (verified above), so this requires implementing a
root-pivot or re-`portRegister` mechanism in the kernel/posix layer. **Highest
effort, most invasive, kernel-touching.** Not recommended as the path.

#### (b) NFS as a *secondary* mount at `/mnt/nfs` — **RECOMMENDED FIRST**
Keep dummyfs-root (or sd ext2) as `"/"`. After lwIP is up (after line 77), launch
the NFS fs-server which does `portRegister(port, "/mnt/nfs", ...)` (the
`mountpt != NULL` branch in `srv.c:228-237`). No ordering change, no pivot, no
kernel work. The Pi can then **execute test binaries from `/mnt/nfs`** that the host
dropped into the export tree — **no SD swap**. This captures essentially the entire
dev-velocity benefit. Lowest risk; purely additive (a new `-x nfs;/mnt/nfs;...`
line after lwip, mirroring how thermal/hwrng/gpio were added).

#### (c) full NFS-as-root
Reorder the boot so lwIP+GENET+DHCP and the NFS server run **before** dummyfs-root,
and have the NFS server `portRegister("/")` directly (the `mountpt == NULL` branch,
`srv.c:219-227`). The sd-variant ext2-at-`/` flow (line 28) is the precedent. This
needs: (1) moving the network bring-up to the front of `user.plo.yaml`; (2) the NFS
server claiming `"/"`; (3) ensuring nothing earlier needs `"/"`. **Feasible but
invasive** to the boot sequence, and it makes the network a hard boot dependency
(no network → no root → no boot), so it wants a fallback. Do it **after** (b) proves
the libnfs↔mt* stack on HW.

### Recommended path: **(b) → (c)**

Ship (b) first (T3). It is additive, deterministic, netboot-testable, and removes
the SD-swap loop. Only then attempt (c) (T4) as an attended boot-sequence change
with an SD/dummyfs fallback for network-down boots.
