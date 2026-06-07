# Build / port integration, RPC-XDR self-containment, license

## Where libnfs lives as a Phoenix port

It belongs in **`phoenix-rtos-ports/libnfs/`**, following the existing port pattern.
The closest precedents are the network library ports:

- **`phoenix-rtos-ports/curl/port.def.sh`** — a libcurl/curl port that cross-builds
  with autotools: `./configure --host="${HOST}" --prefix=... CFLAGS=... LDFLAGS=...`,
  then `make && make install`, then copies `include/`, `lib/*.a`, and the binary
  into the Phoenix staging dirs (`$PREFIX_H`, `$PREFIX_A`, `$PREFIX_PROG`). It even
  passes `--disable-pthreads --disable-threaded-resolver` — exactly the
  single-threaded posture libnfs's sync API wants.
- **`phoenix-rtos-ports/openssl111/port.def.sh`** — autotools-style cross build with
  a per-target config profile.

A `libnfs` port.def.sh would: fetch the libnfs **release tarball** (ships a
pre-generated `configure`, so no `autoreconf` needed on the build host — the dev
host here lacks autotools, confirmed), then:

```sh
./configure --host=aarch64-phoenix \
    --disable-pthread \
    --disable-werror \
    --without-libkrb5 \
    --disable-shared --enable-static \
    --prefix="$PREFIX_PORT_INSTALL"
make && make install
# stage libnfs.a + include/nfsc/*.h into $PREFIX_A / $PREFIX_H
```

The toolchain triple is **`aarch64-phoenix`** (from
`scripts/build-phoenix-toolchain-linux.sh:22 target="aarch64-phoenix"`; the GCC is
present at `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc`, v14.2.0). The
`${HOST}` the ports system passes for this target is the same triple.

The NFS **fs-server** itself (the `mt*` wrapper) is *Phoenix code*, not a port — it
would live in `phoenix-rtos-filesystems/nfs/` (alongside dummyfs/ext2), link
against the `libnfs.a` produced by the port, and be added to the build like any
other fs server. It gets launched from `user.plo.yaml` with `-x nfs;...`.

## Compile-test evidence (T0 pre-validation, done in this study)

Compiling libnfs core against the real Phoenix toolchain already works once the
standard configure feature-defines are supplied:

1. First raw attempt (`aarch64-phoenix-gcc -c lib/libnfs-zdr.c`) showed exactly the
   expected configure-gated issues:
   - `redefinition of 'struct sockaddr_storage'` — because libnfs's
     `include/libnfs-private.h:105-119` defines it *only when
     `HAVE_SOCKADDR_STORAGE` is undefined*, and **Phoenix's sysroot
     `sys/socket.h:33` already defines it**. Setting `-DHAVE_SOCKADDR_STORAGE=1`
     (what `./configure` does) **resolved the conflict** — confirmed by re-compile.
   - `libnfs-raw-nfs4.h: No such file` — it lives in `nfs4/`; just an `-I nfs4`
     matter, handled by the build's include dirs.
2. With a minimal `config.h` (`HAVE_SOCKADDR_STORAGE`, `HAVE_POLL_H`, etc.) and the
   `-I include -I include/nfsc -I nfs -I nfs4 -I mount -I portmap` dirs, the only
   remaining error was implicit `htonl`/`ntohl` — supplied by Phoenix's
   `arpa/inet.h` (`#define htonl htobe32`, `#define ntohl be32toh`), which configure
   includes via `HAVE_ARPA_INET_H`. A standalone `#include <arpa/inet.h>` +
   `htonl()` program compiled cleanly with the Phoenix toolchain.

**Takeaway:** the OS seam is the standard POSIX header set Phoenix already ships;
the port is "let configure detect the sysroot," with at most a tiny static `config.h`
if we can't run autoconf on the cross host. No `win32_compat`-style new file needed.

## RPC / XDR — self-contained (no rpcgen, no libtirpc)

- libnfs **bundles its own XDR runtime**: `lib/libnfs-zdr.c` + `include/nfsc/
  libnfs-zdr.h` (header comment: *"It aims to be compatible with normal rpcgen
  generated functions"*). No dependency on `<rpc/xdr.h>`, libtirpc, or a system
  rpcgen (grep for `rpcgen|libtirpc|<rpc/` in `configure.ac`, `CMakeLists.txt`,
  `lib/*.c` finds **only** that comment line — no real dependency).
- The RPC stubs are **pre-generated and committed**: `nfs/libnfs-raw-nfs.{c,h}`,
  `nfs4/libnfs-raw-nfs4.{c,h}`, `mount/libnfs-raw-mount.{c,h}`,
  `portmap/libnfs-raw-portmap.{c,h}`. The `.x` IDL files are present as *sources* but
  building does **not** require regenerating them (so no rpcgen on the build host).
- Implication: the cross build needs only a C compiler + the Phoenix sysroot. No
  extra host RPC tooling, no codegen step.

## License (factual)

- **Core library is LGPL-2.1** — `include/nfsc/libnfs.h:6-15`, `lib/socket.c:6-15`,
  `lib/libnfs.c:6` all carry *"GNU Lesser General Public License ... version 2.1 or
  (at your option) any later version."* `COPYING` / `LICENCE-LGPL-2.1.txt` is the
  primary license.
- The repo *also* carries `LICENCE-GPL-3.txt` and `LICENCE-BSD.txt`. These cover
  ancillary pieces (sample utilities/tools and some BSD-derived bits) that are **not
  linked into `libnfs.a`** — the linked library is LGPL-2.1. (We would not ship the
  GPL-3 utility programs.)
- **Static-link + "published publicly" posture:** LGPL-2.1 allows static linking
  into a proprietary or differently-licensed program *provided* recipients can relink
  against a modified libnfs (e.g. by providing the object files or, more simply, by
  the libnfs source + our build being public). This project's code is **published
  publicly** (per CLAUDE.md), and the port would carry libnfs source + the
  `port.def.sh` recipe, which satisfies the LGPL relink obligation by construction.
  No copyleft obligation propagates to *Phoenix's own* code (kernel, fs-server,
  drivers) from LGPL dynamic-or-static use, as long as the relink path exists. This
  is a factual note, not legal advice — confirm with whoever owns licensing before
  shipping.
- Practical: record the libnfs `license="LGPL-2.1"` + `license_file` in the
  `port.def.sh` metadata block, the same way `curl`/`openssl` ports do.
