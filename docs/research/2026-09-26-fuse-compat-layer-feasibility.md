# FUSE compatibility layer for Phoenix-RTOS — feasibility

Date: 2026-09-26. Research only: nothing was built for Phoenix and nothing was run on the Pi; the only code is the host spike below.

Paths `kernel/`, `libphoenix/`, `filesystems/` and `devices/` mean `sources/phoenix-rtos-*`. External clones are in `external/`: libfuse (3.19.0-rc0), openbsd-src (`lib/libfuse`, `sys/sys`), netbsd-src (`lib/librefuse`), freebsd-src (`sys/fs/fuse`), ntfs-3g, exfat, squashfuse, sshfs, e2fsprogs. **[inferred]** marks claims I did not read in the code or run.

Self-verified citations: `name.c`, `msg.h`, `posix.c` (open/fsync/SEEK_END), `msg.c:412-420`, `dir.c:465-475`, `user.c`, `stat.c:200-317`, nfs `srv.c`/`nfs_ops.c`, `umass.c:758-792`, sdstorage atSize, libntfs-3g headers, and all BSD/libfuse code. Other Phoenix file:line references come from two source surveys and were not re-verified.

Spike sources and a one-command host rebuild are in `docs/research/2026-09-26-fuse-spike/`; run `build-host.sh` to rebuild and rerun it.

## Verdict

**Feasible, and cheaper than it looks.** The Phoenix filesystem-server contract is already close to the FUSE low-level API. The NFS server (`filesystems/nfs`, ~2.8k lines) is an in-tree example of wrapping a path-based library as a server.

**Recommended design: (A), an in-process library.** Port OpenBSD's ISC-licensed `lib/libfuse` and replace only its `/dev/fuse0` transport with a Phoenix `msgRecv` dispatcher. The FUSE filesystem then *is* a Phoenix filesystem server. There is no kernel work, and each operation costs one IPC round-trip, the same as a native server.

**First target: exfat-fuse (relan, 1.4).** It is read-write, uses only `fuse_main` plus a FUSE 2.6 `fuse_operations` table, and has no ioctls on its Linux code path. ntfs-3g should come second. OpenBSD's ports tree already ships `sysutils/exfat-fuse` (1 patch), `sysutils/ntfs-3g` 2026.2.25 (6 patches) and `sysutils/sshfs-fuse` 2.10 on exactly this ISC library (`obsd-ports` tree read 2026-09-26).

**Host spike (done).** Unmodified exfat-fuse `main()` and libexfat, plus unmodified OpenBSD `fuse_ops.c`, ran against a 490-SLOC Phoenix-shaped transport. The driver used the real `msg_t` layout and Phoenix message sequences. 18/18 checks passed in 37 messages. `fsck.exfat` reported the image clean, and 3/3 checks passed after a remount (10 messages).

**Headline Phoenix gap: there is no rename message.** libphoenix emulates `rename()` as link+unlink (`libphoenix/sys/stat.c:283-311`, with "FIXME: add renaming dirs support" at `:287`). That can never work on exFAT, FAT or NTFS directories. The spike's `mtLink` returned ENOSYS on exFAT; a proposed `mtRename` worked.

## 1. The Phoenix contract, and how it maps to FUSE

| Phoenix | Where | FUSE equivalent / adapter job |
|---|---|---|
| `mtLookup(oid dir, "a/b/c")`: the server resolves as many components as it owns, returns the consumed length in `o.err` and the next server in `o.lookup.dev` | `kernel/proc/name.c:387-421` | loop `lookup(parent, name)` per component. `..` at the root and spliced mountpoints/device nodes must come from a local table, as `nfs_ops.c:415-438` does |
| `mtGetAttrAll` (15 `{val,err}` pairs; if any fails, `stat` fails) | `kernel/include/msg.h:53-69`, `posix.c:1751-1830` | `getattr` → `struct stat` |
| `mtOpen`/`mtClose` = per-oid refcount; no per-open handle | `ext2.c:154-178`, `dummyfs.c:903-940` | one shared `fi->fh` per oid; `release` on the last close. A positive `mtOpen` return does become a per-fd id (`posix.c:920-928`), but directories ignore it (`libphoenix/unistd/dir.c:465-475`) |
| `mtRead`/`mtWrite(oid, offs)`: stateless, and the kernel keeps the file offset | `posix.c:1079,1144` | `read`/`write` with the shared fh |
| `mtCreate(dir, name, otDir/otFile/otSymlink/otDev, mode)`; a symlink is sent as `"name\0target\0"` | `libphoenix/sys/stat.c:229-237`, `file.c:409-459` | `mkdir`/`mknod`(+`create`)/`symlink`; otDev goes to the local splice table |
| `mtUnlink` covers both unlink and rmdir | `dir.c:689-747` | look the name up, then `unlink` or `rmdir` |
| `mtLink` | `posix.c:1514` | `link` |
| `mtReaddir(offs)`: **one entry per message**; the cookie advances by `d_reclen`; `-ENOENT` means end | `dir.c:386-430` | snapshot the whole listing once per scan, as `nfs_ops.c:1295-1434` does |
| `mtTruncate`, `mtSetAttr(atMode/atSize/atMTime/atATime)` (whole seconds) | `posix.c:490-505,1759-1774` | `truncate`/`chmod`/`utimens` |
| `mtStat` = statvfs, with a zeroed oid | `posix.c:734-803` | `statfs` |
| `mtSync` (0xf52) from `fsync()` | `posix.c:1850-1884` | `fsync` |
| Mount: `portCreate`, then send `mtSetAttr(atDev = our root oid)` to the mountpoint directory | `dummyfs/srv.c:59-85`, `libphoenix/sys/mount.c:94-104` | done in `fuse_mount()` |

**Absent:** rename; xattrs; credentials (`msg.pid` only, `proc/msg.c:391`; `getuid()`=0, `unistd/user.c:20-29`; chown a stub, `stat.c:268,314`); FORGET; shared-mmap write-back (`kernel/include/mman.h:28-29`; read faults work via `mtRead`, `vm/object.c:240-328`); locks reaching servers; interruptible requests (`msg.c:412-420`). Multiple receive threads per port and out-of-order replies are supported (`msg.c:512-533`; libstorage uses them, `storage.c:189-237`).

## 2. FUSE side, and which layer to replace

- **libfuse 3** (LGPL-2.1). The high-level `fuse.c` (5350 lines) is built on the low-level `fuse_lowlevel.c` (5678 lines), which decodes the kernel protocol (7.45, 53 opcodes).
  - Its transport can be replaced in process through `fuse_session_custom_io` (`include/fuse_lowlevel.h:135`).
  - It is Linux-heavy: eventfd (`fuse_lowlevel.c:39`), splice, io_uring, and `fusermount3` (`mount.c:38`).
- **OpenBSD `lib/libfuse`** (ISC, ~3.7k lines) has the same layering at the FUSE 2.6 API.
  - `fuse_session.c`, `fuse_lowlevel.c` and `fuse_chan.c` (1154 lines together) are the fusebuf codec. **Replace these.**
  - `fuse_ops.c` (1113 lines) maps inodes to paths through a vnode tree. **Keep it.**
  - Limits: it is single-threaded (`fuse_loop_mt` returns -1 at `fuse.c:230`; the current request is a global at `fuse_ops.c:37`), its low-level API is partial, and it has no xattrs.
- **NetBSD refuse** (BSD) sits on puffs. It is high-level only and single-threaded, and it covers FUSE API versions 1.1 to 3.10 through versioned headers (`refuse/v11..v38.h` plus the 1845-line `fs.c` shim). Borrow it for the FUSE 3 headers.
- **FreeBSD fusefs** implements the Linux wire protocol 7.35 in its kernel (`sys/fs/fuse/fuse_kernel.h:226`, 9.3k lines) and runs unmodified libfuse. It is the model for design B.

**Adapter surface.** Compiling the reused OpenBSD files leaves only these unresolved: `fuse_reply_{err,entry,attr,open,buf,write,readlink,statfs,none}`, `fuse_add_direntry`, `fuse_req_userdata`, `fuse_req_ctx`, and the session functions.

## 3. Candidate filesystems

| FS | API / version | OS needs | License | Size | Verdict |
|---|---|---|---|---|---|
| exfat (relan) | high-level, 2.6 or 3.0; `fuse_main` only | `pread`/`pwrite`/`fsync`; device size via `lseek(SEEK_END)`, which Phoenix answers with `GetAttr(atSize)` (`posix.c:1621+`); needs a `__phoenix__` branch in `platform.h:27-70` | GPL-2+ | 708 + 4340 lines | **1st** |
| ntfs-3g | `ntfs-3g.c` high-level / `lowntfs-3g.c` low-level; **2.6 only**; bundles libfuse-lite (LGPL) | `pread`/`pwrite`; size ioctls fall back to a pread binary search (`device.c:600-612`); `--disable-mtab`, `--disable-plugins`; no threads | GPL-2+ (libntfs-3g too: 32/33 files) | 5.8k + 54k lines | 2nd. Also a design-C candidate (see below) |
| squashfuse | `hl.c` 2.6/3.2 | `pread`; zlib/xz/lzo ports exist (lz4/zstd do not) | BSD-2 | ~7k lines | cheap read-only; no GPL |
| sshfs | 3.1 (2.10 is FUSE 2) | fork+exec of an ssh client (dropbear is ported) or `directport`; **glib**; threads | GPL-2 | 5.4k lines | late |
| fuse2fs | **3.14** | `linux/fs.h`/`xattr.h` required by configure, `prctl`, libext2fs (58k lines, replaceable `io_manager`), com_err/uuid/blkid/e2p | GPL frontend; libext2fs LGPL-2 but 8 files carry GPL headers | 5.8k + 58k lines | last. Valuable as a mature ext2/3/4 alongside the 21-defect libext2 |

**Can libntfs-3g be driven without FUSE? Yes.** Public API: `ntfs_mount`/`ntfs_device_mount`/`ntfs_umount` (`volume.h:298-302`); `ntfs_pathname_to_inode`, `ntfs_create`, `ntfs_delete`, `ntfs_link`, `ntfs_readdir` + `ntfs_filldir_t` (`dir.h:68-110`); `ntfs_inode_open`, `ntfs_attr_open/pread/pwrite` (`attrib.h:295-302`); pluggable `struct ntfs_device_operations` (`device.h:103-116`) via `ntfs_device_alloc`+`ntfs_device_mount` (could talk to umass by message). Design C for NTFS = re-writing the 5.8k-line `ntfs-3g.c` frontend as a server. lwext4 is a design-C candidate for ext4 (license not read — verify). OpenBSD also ports `fuse-zip` and `unionfs-fuse` on the same library.

## 4. Designs compared

| | (A) in-process OpenBSD engine | (B) `/dev/fuse` bridge server + ported libfuse3 | (B′) libfuse3 in process via `custom_io` | (C) native server per library |
|---|---|---|---|---|
| Effort to exFAT RW on Pi | **14-22 pd** | 25-35 pd | 18-25 pd | 8-12 pd, for one fs only |
| Round-trips per op | 1 (same as native) | 3 (client→bridge, daemon read, daemon write) plus copies | 1 | 1 |
| API coverage | 2.6 (exfat, ntfs-3g, squashfuse, sshfs 2.10); 3.x needs a header layer | full 3.x, low-level, MT loop | full 3.x | n/a |
| Licence home | **core-eligible** (ISC + BSD-3) | bridge is BSD; libfuse is LGPL → ports | LGPL → ports | per library |
| Main risks | engine is single-threaded; rename rehash; no FORGET | a whole protocol state machine (nlookup, INTERRUPT) for no binary-compat gain, since everything is recompiled anyway | Linux-isms in libfuse; LGPL relink duty for BSD filesystems | cost repeats for every fs |

Performance in (A) is dominated by client-side traffic that native servers pay too: `resolve_path` does lookup + `GetAttr(atMode)` per component (26 round-trips at depth 5, `nfs/nfs_node.h:52-59`) and readdir is one message per entry — hence an attr cache and readdir snapshot in the adapter. A standalone daemon adds one IPC per block I/O to umass/sdstorage (ext2 avoids it by living in the driver, `ext2/block.c:89`).

**Upstreamability** is moot under the fork-only policy; only A's core pieces (ISC engine, BSD-3 transport, `mtRename`) could ever go upstream. **Self-deadlock hazard:** a single-threaded daemon that resolves a path under its own mount (or calls `socket()`) in a handler messages itself — the nfs-fs "socket() deadlocks the owner of /" class. exfat's `/dev/null` open (`io.c:103`) and `syslog()` are safe only because they resolve via root/devfs.

**Choose A; C is the one-library fallback.** A is C amortised: C's per-library glue is the same work as A's transport, written once.

### Effort breakdown for A

Units are person-days (pd) for someone who knows Phoenix. The estimates assume the Pi bench works and do not include soak time.

| Component | pd |
|---|---|
| A1: import OpenBSD libfuse into corelibs. Phoenix transport: lookup walk, `..` and splice table, handle refcount, readdir snapshot, attr cache, mount/umount, `mtSync`, `mtStat`. Fix rename rehash and vnode LRU. Host test harness, starting from the spike | 6-9 |
| A2: `mtRename` (0xf55, libphoenix range) + libphoenix `rename()`, handlers in dummyfs, ext2 (libstorage + legacy), nfs; link+unlink fallback | 3-5 |
| A3: libphoenix bits (`reallocarray`, `utimensat`/`futimens`, `fdatasync`); stop dropping `o.err` in `posix_truncate`/`posix_fsync` (`posix.c:500,1879-1883`) | 1.5-3 |
| A4: exfat port (ports recipe, `platform.h`, mkfs/fsck) + Pi milestone | 3-5 |
| A5: ntfs-3g port | 4-6 |
| A6: squashfuse | 1-2 |
| A7: FUSE 3.x header layer (refuse-style) + complete low-level API | 7-10 |
| A8: multi-threaded loop (per-request context instead of the global `ireq`) | 3-5 |
| A9: fuse2fs | 8-12 |

Milestones:

- exFAT RW on the Pi: A1-A4, **≈14-22 pd**.
- Adding ntfs-3g and squashfuse: **≈19-30 pd**.
- Full FUSE 3 plus fuse2fs: **≈37-57 pd**.

## 5. Prioritised Phoenix-side gaps

1. **No rename** (`kernel/include/msg.h:28-36`, `libphoenix/sys/stat.c:283-311`). It breaks every non-hard-link filesystem and every directory rename. Fixing it helps native servers too.
   - Number the new message in the libphoenix range: `mtRename = 0xf55`, after `mtMountPoint = 0xf54` in `libphoenix/include/sys/file.h:32`. Extending the kernel enum before `mtCount` would collide with upstream growth at the weekly merge.
   - Handlers must land in *every* server, not just in a fallback. dummyfs's switch has no `default:` (`dummyfs/srv.c:278-357`), so it answers an unknown type with a stale `o.err`, not ENOSYS.
   - libphoenix should fall back to link+unlink only on an explicit -ENOSYS or -EINVAL.
2. **No FORGET or oid lifetime.** The kernel caches only registered names (`name.c:135-170`) and never says an oid is dead, so the adapter needs an LRU over unreferenced vnodes.
   - OpenBSD's engine also gives a renamed file a *new* ino (spike: id 3→5) and leaves the old vnode stale. An fd held open across a rename would break until the engine rehashes like libfuse's `rename_node`.
3. **No per-open handle.** One `fh` per oid, so per-open `O_APPEND`/`O_RDONLY` cannot be honoured (`posix.c:943-951` applies O_APPEND once at open).
4. **Errors dropped** by `posix_truncate`/`posix_fsync` (`posix.c:500,1879-1883`). ENOSPC or EIO from the filesystem never reaches the caller.
5. **Uninterruptible senders** (`msg.c:412-420`). A hung daemon wedges its clients; FUSE's INTERRUPT has no counterpart.
6. **No credentials, whole-second times, all-or-nothing GetAttrAll** (`posix.c:1751-1830`). `fuse_get_context()` reports uid/gid 0.
7. **Inconsistent `d_type`:** ext2 and dummyfs use DT_\*, nfs uses ot\* (`nfs_ops.c:1399-1412` vs `ext2/dir.c:154-170`). This needs settling before a fourth convention appears.
8. **umount only by device path** (`libphoenix/sys/mount.c:153-157`). A FUSE daemon has no device, so it needs a mountpoint-based umount or a daemon-side teardown message.
9. **xattrs absent.** Build ntfs-3g without `HAVE_SETXATTR`; fuse2fs's xattr ops go unused.

## 6. Licensing plan

| Piece | Licence | Home |
|---|---|---|
| OpenBSD libfuse engine + new Phoenix transport (`phoenix-rtos-corelibs/libfuse`) | ISC + BSD-3 | **core** |
| refuse-style FUSE 3 version headers (from NetBSD) | BSD-2/3 | core |
| `mtRename`, libphoenix fixes | BSD-3 | core |
| libfuse `include/fuse_kernel.h` (design B only) | dual GPL/BSD-2 | core, if ever needed |
| upstream libfuse `lib/` + `include/` (B′ or fuse-lite) | LGPL-2.1 | **ports only** |
| exfat, ntfs-3g, sshfs, fuse2fs frontend | GPL-2(+) | **ports only** |
| libext2fs | LGPL-2 (8 GPL-headed files) | ports only |
| squashfuse | BSD-2 | ports (third-party), no copyleft |

Never copy the libfuse API headers into core. Use OpenBSD's `fuse.h`, `fuse_lowlevel.h` and `fuse_opt.h` (ISC). GPL filesystems link the ISC library into a GPL executable, which is compatible.

## 7. Host spike

The spike sources are in `docs/research/2026-09-26-fuse-spike/`; `build-host.sh` rebuilds and runs everything.

OpenBSD `fuse_ops.c`, `fuse_subr.c`, `tree.c`, `dict.c`, `fuse_opt.c`, `debug.c` **unmodified**; `fuse.c`: `fuse_mount` → ~30 lines calling `phx_mount`, `daemon()` off, 3 Linux-only mount options stubbed; `fuse_private.h`: one field. New `phx_fuse.c` (490 SLOC) dispatches `msg_t` into the low-level ops; `driver.c` emulates `msgSend`/`msgRecv`/`msgRespond` between two threads (**[inferred]** equivalence to Phoenix IPC). relan exfat `main()` + libexfat **unmodified** (`FUSE_USE_VERSION=26`).

**Result:** 18/18 checks in 37 messages — mkdir, create, 300 KB write in 64 KiB `mtWrite` chunks read back byte-identical, GetAttrAll, multi-component lookup, readdir (one entry/message), rename via `mtLink` → ENOSYS, proposed `mtRename` → OK, truncate, rmdir via `mtUnlink`, nested create, statvfs.

`fsck.exfat -n`: clean (3 directories, 2 files). A remount run passed 3/3 checks in 10 messages; it read the renamed, truncated file back correctly.

**Two contract subtleties found:**

1. OpenBSD's engine requires a LOOKUP of the child name before mkdir, mknod or rename, because its kernel always sends one. Phoenix `mkdir` sends `mtCreate` without it, so the adapter must pre-lookup; this also yields EEXIST.
2. Filesystems pass a NULL stat for `.` and `..`, so the adapter must report their type as a directory.

## 8. First milestone on the Pi

**"Mount an exFAT USB stick via unmodified exfat-fuse 1.4 on the Phoenix libfuse, read-only, then read-write."**

1. On the host, create an exFAT stick with files and a sha256 manifest.
2. On a netbooted Pi, run `exfat-fuse /dev/umassN /mnt/usb -o ro`.
3. Compare every file's `sha256sum` against the manifest, and check `ls -l` sizes and mtimes.
4. Remount read-write: `cp` in a 50 MB file, `mkdir`, `mv` (this needs `mtRename`), `rm`.
5. Move the stick to the host and check that `fsck.exfat -n` is clean and the checksums match.

**[inferred]** `/dev/umassN` is per-partition (`umass.c:1288`) and serves byte-offset `mtRead` through libcache (`umass.c:767-771`). Unaligned reads when the cache is absent have not been checked.
