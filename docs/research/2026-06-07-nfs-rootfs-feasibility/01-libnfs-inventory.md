# libnfs inventory + portability seam

**Clone:** `git clone https://github.com/sahlberg/libnfs research/libnfs`
**Commit SHA:** `f0b109df8fd865a2f8d39e78310fd875e15f3ac1`
**Location:** `/home/houp/phoenix-rpi/research/libnfs/` (deliberately *outside*
`sources/`, so the Phoenix build's recursive `find` never picks it up).

## Size / layout

Total ~49.9k LOC of `.c` (`find . -name '*.c' | xargs wc -l` → `49929 total`).
Per-area `.c`+`.h`:

| Dir | LOC | Role |
|-----|-----|------|
| `lib/` | ~22010 | core: `libnfs.c`, `libnfs-sync.c`, `socket.c`, `pdu.c`, `init.c`, `nfs_v3.c`, `nfs_v4.c`, `libnfs-zdr.c` (own XDR), `multithreading.c` (opt-in) |
| `nfs/` | ~6122 | NFSv3 RPC: `nfs.x` + **pre-generated** `libnfs-raw-nfs.{c,h}`, `nfs.c` |
| `nfs4/` | ~7630 | NFSv4 RPC: `nfs4.x` + pre-generated `libnfs-raw-nfs4.{c,h}` |
| `mount/` | ~1059 | MOUNT protocol (v3 only) |
| `portmap/` | ~2174 | portmap/rpcbind (v3 only) |
| `include/` | ~6877 | public + private headers |
| `nlm/`, `nsm/`, `rquota/` | small | locking/quota (optional) |
| `win32/`, `aros/`, `ps2_ee/`, `ps3_ppu/` | — | **the OS-specific ports** (the seam, see below) |
| `examples/`, `utils/`, `tests/` | — | sample programs (not linked into the lib) |

## Public headers (what a consumer includes)

- `include/nfsc/libnfs.h` — the public API (sync + async). This is all an
  fs-server needs.
- `include/nfsc/libnfs-raw.h` — low-level RPC API (not needed for our use).
- `include/nfsc/libnfs-zdr.h` — bundled XDR runtime declarations.

### Sync API we will actually call (all in `libnfs.h`)

```
nfs_init_context() / nfs_destroy_context()
nfs_set_version(nfs, NFS_V4)                 line 361   (default is v3)
nfs_mount(nfs, server, export)               line 395
nfs_stat64(nfs, path, struct nfs_stat_64*)   line 520
nfs_open(nfs, path, flags, struct nfsfh**)   line 675
nfs_pread(nfs, fh, buf, count, offset)       line 740
nfs_pwrite(nfs, fh, buf, count, offset)      line 862
nfs_close(nfs, fh)                           line 709
nfs_truncate(nfs, path, len)                 line 1061
nfs_opendir/nfs_readdir/nfs_closedir         lines 1302/1338/1379
nfs_mkdir2 / nfs_rmdir / nfs_creat / nfs_unlink   lines 1148/1177/1208/1268
nfs_rename / nfs_chmod / nfs_access / nfs_readlink lines 1940/1569/1847/1528
```

These map 1:1 onto the Phoenix `mt*` VFS messages — see
[04-rootfs-ordering.md](04-rootfs-ordering.md).

## Build system

Dual: **autotools** (`configure.ac`, `Makefile.am`, `bootstrap` →
`autoreconf -vif`) **and CMake** (`CMakeLists.txt`). The shipped *git* tree has no
generated `configure` — `bootstrap` needs autotools installed (the dev host here
does **not** have `autoreconf`, confirmed). The normal release **tarball** ships a
pre-generated `configure`, which is how the existing `phoenix-rtos-ports` autotools
ports work (e.g. `curl`, `openssl` fetch a release tarball, not a git checkout).
See [05-build-port-and-license.md](05-build-port-and-license.md).

## OS / libc primitives required

From `lib/socket.c` includes + call sites (grep of the file):

| Primitive | libnfs use | Phoenix has it? |
|-----------|------------|-----------------|
| `socket()` | `create_socket` (socket.c:115,122,139) | yes (`libphoenix sys/socket.h:69`) |
| `connect()` | socket.c:1565, UDP 1934 | yes (`socket.h:70`) |
| `bind()` | reserved-port + UDP (1539,1885) | yes (`socket.h:71`) |
| `setsockopt/getsockopt` | TCP_NODELAY, SO_ERROR, SO_TYPE, keepalive, linger | yes (`socket.h:83-84`) |
| `recv/recvfrom/send/sendto` | data path (623,737) | yes (`socket.h:75-79`) |
| `writev` | `rpc_write_to_socket` (socket.c:415) | yes (`libphoenix/sys/uio.c:72`) — userspace loop over `write()` |
| `read`/`write`/`close` | fd lifecycle | yes |
| `poll()` | the **only** wait primitive in the sync loop (`libnfs-sync.c:232,278`) | yes (`libphoenix/include/poll.h:44`) |
| `fcntl(F_SETFL,O_NONBLOCK)` | `set_nonblocking` (socket.c:148-151) | yes (header present) |
| `ioctl(FIONBIO)` | tried first, *falls back* to fcntl (socket.c:148) | FIONBIO/`sys/filio.h` absent → fcntl path used |
| `gettimeofday` | timeouts | yes (`libphoenix/sys/time.c:98`) |
| `htonl/ntohl` | XDR byte order | yes (`arpa/inet.h` → `htobe32`/`be32toh`) |
| `malloc` | allocations | yes |
| **threads** | **NOT required for sync API** | n/a — pthread is `--enable-pthread`, default **`no`** (`configure.ac:97-100`) |

### The sync API is single-threaded poll-driven

`lib/libnfs-sync.c` `wait_for_nfs_reply()` (lines 260-300): a loop of
`poll(&pfd, 1, rpc->poll_timeout)` → `nfs_service(nfs, revents)`. No background
thread, no event-loop library. This is the ideal model for a Phoenix fs-server: the
server's `msgRecv` loop calls a blocking `nfs_*()` which internally polls one
socket fd. (For concurrency later, libnfs also offers the async API + a service fd
you can fold into the server's own `poll`/`select` set — out of scope for T0–T3.)

## Portability seam — exactly where a Phoenix variant plugs in

libnfs isolates OS differences in **per-platform compat shims**, selected in
`lib/socket.c:19-102`:

```
#include "config.h"        // generated by ./configure: HAVE_* feature defines
... platform branches ...
#include <win32/win32_compat.h>   // Windows seam (the template to copy)
#include <arpa/inet.h>            // POSIX path — what Phoenix uses
#include <poll.h> <sys/uio.h> <sys/socket.h> <netinet/in.h> <netinet/tcp.h> ...
```

- On a POSIX-ish target, libnfs just includes the standard headers directly,
  gated by the `HAVE_*_H` defines that `./configure` writes into `config.h`.
- **The cleanest Phoenix approach is the POSIX path, not a new `phoenix/` dir:**
  Phoenix's sysroot already provides `sys/socket.h`, `poll.h`, `netinet/tcp.h`,
  `arpa/inet.h`, `sys/uio.h`, so we let configure detect them and define the
  matching `HAVE_*` symbols (exactly as the Linux build does).
- `include/libnfs-private.h:105-119` redefines `struct sockaddr_storage`
  **only when `HAVE_SOCKADDR_STORAGE` is undefined**. Phoenix's
  `sys/socket.h:33` already defines it, so configure must set
  `HAVE_SOCKADDR_STORAGE=1` (the compile-test in note 05 proved the conflict
  vanishes once that define is set).

**Conclusion on the seam:** no new OS abstraction file is required. The port is
"supply a correct `config.h` (a static one if we can't run autoconf on the cross
host) + the standard `-I` include dirs," with at most a tiny patch or two for any
define autoconf would otherwise compute. This is materially simpler than the
`win32_compat.c` route.
