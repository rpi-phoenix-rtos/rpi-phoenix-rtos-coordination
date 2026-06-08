# T0 — libnfs cross-compile for Phoenix-RTOS (aarch64a72-generic-rpi4b)

**Status:** DONE (host-only). A static `libnfs.a` (NFSv3 + NFSv4 sync-API core)
cross-compiles for the Phoenix RPi4 target and a trivial Phoenix userspace
program links against it. No hardware / netboot involved.

**Validated against two libnfs trees with the *same* config.h + Makefile:**
- `research/libnfs` @ `f0b109d` (master tip used by the feasibility study)
- the `libnfs-6.0.2` release tag (what the port pins) — identical seam, builds clean.

---

## 1. What was produced

| Path | Role |
|------|------|
| `sources/phoenix-rtos-ports/libnfs/port.def.sh` | Phoenix port recipe (git_source clone of tag `libnfs-6.0.2`, drives the Makefile below) |
| `sources/phoenix-rtos-ports/libnfs/files/config.h` | hand-written OS-seam `config.h` (HAVE_* + shims) |
| `sources/phoenix-rtos-ports/libnfs/files/Makefile.phoenix` | standalone cross-build → `libnfs.a` (no autotools) |

`p_build` stages into the Phoenix sysroot dirs:
- `${PREFIX_A}/libnfs.a`
- `${PREFIX_H}/nfsc/{libnfs.h,libnfs-raw.h,libnfs-zdr.h}`

---

## 2. Why a hand-written config.h + own Makefile (not ./configure / CMake)

- The libnfs **git tree ships no pre-generated `configure`** (it is produced by
  `./bootstrap` → `autoreconf`), and **this dev host has no autotools**.
- GitHub release "tarballs" for libnfs are just auto-generated **tag archives** —
  they ALSO lack a pre-generated `configure` (unlike curl/openssl release tarballs).
- A **cross target cannot run** the feature-probe binaries that `./configure` and
  CMake `check_*` link to detect `HAVE_*` — they'd misdetect.
- The RPC/XDR stubs are **pre-generated and committed** (`*/libnfs-raw-*.{c,h}`),
  so no `rpcgen`/libtirpc is needed — just a C compiler + the Phoenix sysroot.

So we compile the portable C subset directly with a `config.h` whose `HAVE_*` set
was verified **header-by-header** against the toolchain sysroot
`.toolchain/aarch64-phoenix/aarch64-phoenix/usr/include`.

---

## 3. Exact commands (reproduce host-side)

```sh
export PATH=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin:$PATH

# Target CFLAGS = the port-export CFLAGS the build system would hand a port:
# arch flags from sources/phoenix-rtos-build/target/aarch64.mk (cortex-a72) plus
# the early Makefile.common additions, *without* -Wall/-Werror (those are added
# AFTER the EXPORT_CFLAGS snapshot at Makefile.common:141, so ports build without
# them — important: do NOT add -Werror or third-party warnings become errors).
ARCH="-mcpu=cortex-a72 -mtune=cortex-a72 -fomit-frame-pointer -mstrict-align \
      -mno-outline-atomics -ffunction-sections -fdata-sections \
      -fno-strict-aliasing -O2"

make -f sources/phoenix-rtos-ports/libnfs/files/Makefile.phoenix \
  CROSS=aarch64-phoenix- \
  CFLAGS="$ARCH" \
  LIBNFS_SRC=<libnfs source tree> \
  CONFIG_H_DIR=sources/phoenix-rtos-ports/libnfs/files \
  OUT=<build dir>
# -> <build dir>/libnfs.a
```

In-tree this runs via the port harness: `port.def.sh` `p_build` calls the same
Makefile with `CROSS="${HOST}-"` (HOST = `aarch64-phoenix`) and `CFLAGS="${CFLAGS}"`
(the exported port CFLAGS), then stages the `.a` + headers.

### Acceptance evidence (per plan §3.7)

```
file libnfs.a                  -> current ar archive
objdump -f <member>.o          -> architecture: aarch64  (elf64-littleaarch64)
nm libnfs.a | grep ' T '       -> nfs_mount nfs_open nfs_pread nfs_pwrite
                                  nfs_stat64 nfs_opendir (all DEFINED, type T)
ar t libnfs.a                  -> includes nfs_v3.o, nfs_v4.o,
                                  libnfs-raw-nfs.o, libnfs-raw-nfs4.o
nm libnfs.a | grep -i pthread  -> (empty)  # single-threaded sync build
trivial link int main(){nfs_init_context();...} -> ELF aarch64 executable, OK
```

---

## 4. Link recipe for a Phoenix userspace program (T1+)

A util/fs-server that uses libnfs compiles + links with:

```sh
# include dirs
-I<ports-install>/include -I<ports-install>/include/nfsc
# (in-tree the ports system adds these via -I$(PREFIX_H); the public header is
#  <nfsc/libnfs.h>)

# link
-L<ports-install>/lib -lnfs
```

Source includes just `#include <nfsc/libnfs.h>`. No extra libs (krb5/gnutls/tls/
talloc/pthread are all OFF; sockets are in libc). Confirmed: a program calling
`nfs_init_context()`/`nfs_destroy_context()` links to a complete AArch64 Phoenix
executable against the staged `libnfs.a`.

---

## 5. config.h shims — what each is and WHY

`HAVE_*` set: every header/feature define is set ONLY where the corresponding
header or struct member actually exists in the Phoenix sysroot (verified). The
notable HAVE_* and the deliberate omissions are documented inline in
`files/config.h`. Highlights:

- `HAVE_SOCKADDR_STORAGE=1` — Phoenix `sys/socket.h:33` already defines the
  struct; libnfs (`libnfs-private.h:105`) redefines it *only* when this is unset,
  so setting it avoids a "redefinition" error. (The one quirk the study predicted.)
- `HAVE_ARPA_INET_H=1` + an explicit `#include <arpa/inet.h>` in config.h — on
  Phoenix the byte-order funcs (`htonl`→`htobe32`, `ntohl`→`be32toh`) live in
  `arpa/inet.h`, NOT `netinet/in.h`. Some units (e.g. `nfs_v4.c`) include only
  `netinet/in.h` then call `ntohl`; pulling `arpa/inet.h` in via config.h (first
  include in every .c) makes them visible without patching each source.
- `MAJOR_IN_SYSMACROS=1` — `major()/minor()/makedev()` come from
  `<sys/sysmacros.h>`.
- Omitted (header/feature absent): `HAVE_SYS_FILIO_H`/`HAVE_SYS_SOCKIO_H` (→
  `set_nonblocking` falls back from `ioctl(FIONBIO)` to `fcntl(O_NONBLOCK)`,
  supported), `HAVE_SOCKADDR_LEN`, `HAVE_SO_BINDTODEVICE`, `HAVE_SYS_VFS_H`,
  `HAVE_DLFCN_H`/`HAVE_FUSE_H`/`HAVE_DISPATCH_DISPATCH_H`, `HAVE_STDATOMIC_H`
  (MT only), `HAVE_PTHREAD`/`HAVE_MULTITHREADING` (sync build),
  `HAVE_LIBKRB5`/`HAVE_TLS`/`HAVE_TALLOC_TEVENT`.

### Four explicit OS-seam shims (all `#ifndef`-guarded, in config.h)

These are the genuine Phoenix-vs-Linux gaps libnfs hits. None required patching a
libnfs source file — all are resolved in config.h:

| Shim | libnfs site | Why / risk |
|------|-------------|------------|
| `SOL_TCP` → `IPPROTO_TCP` | `socket.c:196` setsockopt level for `TCP_NODELAY` | Linux-ism; `IPPROTO_TCP` is the POSIX/BSD equivalent. **No risk.** |
| `CLOCK_MONOTONIC_COARSE` → `CLOCK_MONOTONIC` | `init.c:78,90` time stamping | COARSE is a Linux perf hint only; `CLOCK_MONOTONIC` is correct, marginally costlier. **No correctness risk.** |
| `O_NOFOLLOW` = `0x20000` (spare bit) | `nfs_v3.c:335`, `nfs_v4.c:2527` symlink guard | Phoenix `fcntl.h` lacks it. Used only as an internal libnfs flag-word bit (never passed to host `open()`), so any bit not colliding with Phoenix's O_* (which top out at 0x10000) is correct. A Phoenix caller of `nfs_open()` that wants NOFOLLOW must use this same value. **Low risk** (see §6). |
| `ELOOP`=40, `ENOTEMPTY`=39, `ESTALE`=116 | `nfs.c nfsstat3_to_errno()`, nfs4 equiv | Phoenix's small `errno.h` omits these 3. Values match Linux/asm-generic. They're return codes to the libnfs caller. **Low risk** (see §6). |

`_U_=__attribute__((unused))` is passed on the **compiler command line** in
`Makefile.phoenix` (not config.h) — that's exactly where upstream
CMake/autotools define it (`CMakeLists.txt:69`).

---

## 6. What was excluded / stubbed, and the T1 risk it creates

**Excluded object files (not in the `.a`):**
- `lib/krb5-wrapper.c` — Kerberos (HAVE_LIBKRB5 off). Risk: **none for T1** — we
  mount with `sec=sys` (AUTH_UNIX) over the host's `insecure,no_root_squash`
  export. If a future stage needs `sec=krb5` it must be revisited.
- `lib/multithreading.c` — MT support (sync build). Risk: **none for T0–T2**; the
  sync API is single-threaded poll-driven. MT is plan §8 (T2-MT) and flips
  `HAVE_MULTITHREADING` + adds `stdatomic.h`/a thread shim.
- `tls/` (handshake.c, ktls.c) — NFS-over-TLS (HAVE_TLS off). Risk: none (plain TCP).
- `nlm/`, `nsm/` — NLM/NSM byte-range locking. Risk: **no POSIX advisory locking
  over NFS**. For a rootfs this is acceptable (Phoenix fs servers don't rely on
  NLM); revisit only if a consumer needs `fcntl` locks on NFS files.
- `rquota/` — quota queries. Risk: none for rootfs.
- `win32/`, `aros/`, `ps2_ee/`, `ps3_ppu/` — other-OS seams. N/A.
- `examples/`, `utils/`, `tests/` — the GPL-3 sample programs. Deliberately not
  linked (keeps the shipped artifact LGPL-2.1 only).

**Included (NFSv3 + NFSv4 core, sync API):**
`lib/{init,libnfs,libnfs-sync,libnfs-zdr,nfs_v3,nfs_v4,pdu,socket}.c`,
`nfs/{nfs,nfsacl,libnfs-raw-nfs}.c`, `nfs4/{nfs4,libnfs-raw-nfs4}.c`,
`mount/{mount,libnfs-raw-mount}.c`, `portmap/{portmap,libnfs-raw-portmap}.c`.
(`nfs/nfsacl.c` is required: `nfs_v3.c` references `rpc_nfsacl3_getacl_task`. It is
plain LGPL core with no extra deps.)

**Shim risks to watch at T1 (HW socket smoke):**
1. `O_NOFOLLOW` spare-bit value (0x20000): only matters if a Phoenix consumer
   passes flags to `nfs_open()`. The smoke test uses plain `O_RDONLY`/`O_RDWR`, so
   T1 is unaffected. Document the chosen bit wherever the fs-server maps Phoenix
   open flags → libnfs flags so the two agree.
2. `ESTALE`/`ELOOP`/`ENOTEMPTY` numeric values: these surface only as error
   returns. T1 (read one file) shouldn't hit them; if it does, the value is a
   plausible POSIX errno and won't crash.
3. `writev` chattiness and reserved-port bind are **not** seam problems here (no
   patch); see plan §3.5. Host export is set `insecure` to allow a high source port.
4. The `set_nonblocking` `ioctl(FIONBIO)`→`fcntl(O_NONBLOCK)` fallback path is the
   one taken on Phoenix — confirm at T1 that non-blocking connect actually works
   over the real lwIP socket (this is exactly what T1 is for).

No libnfs **source file** was patched; the port's `patches/` dir is empty. Every
seam is isolated in `files/config.h` + the `-D_U_=` on the Makefile command line,
which keeps the port trivially re-poppable onto a newer libnfs.

---

## 7. Port pin / determinism

`port.def.sh` pins `git_rev="libnfs-6.0.2"` (an immutable tag) with the harness's
git_source hashes computed from that tag's tree:
- `size="2112336"` (sum of non-`.git` file sizes)
- `sha256="8818a12d82f6df874afe669d893808b2d45129710e8ec147467c8def3e651d07"`
  (`git archive --format=tar HEAD | sha256sum`)

These were computed host-side from a `--depth 1` checkout of the tag. If a future
libnfs version is chosen, recompute both. (The study's `research/libnfs` clone is
master@`f0b109d`, which is *ahead* of 6.0.2; the same config.h/Makefile builds
both, so the seam is version-robust across that range.)
