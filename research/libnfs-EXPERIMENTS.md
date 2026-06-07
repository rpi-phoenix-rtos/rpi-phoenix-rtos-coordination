# libnfs experiment artifacts (NFS feasibility study, 2026-06-07)

Clone: `research/libnfs/` @ `f0b109df8fd865a2f8d39e78310fd875e15f3ac1`
(outside `sources/` so the Phoenix build's `find` ignores it).

## Compile-test against the Phoenix toolchain

Compiler: `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc` (GCC 14.2.0).

1. `autoreconf`/`./bootstrap` NOT runnable here — autotools absent on this host.
   The libnfs git tree has no pre-generated `configure`. The port should use the
   release **tarball** (ships `configure`), matching curl/openssl ports.

2. Raw `gcc -c lib/libnfs-zdr.c` (no config):
   - `redefinition of 'struct sockaddr_storage'` vs Phoenix sysroot
     `sys/socket.h:33`. libnfs guards its own def with `#if !HAVE_SOCKADDR_STORAGE`
     (`include/libnfs-private.h:105`). Adding `-DHAVE_SOCKADDR_STORAGE=1` resolved it.
   - `libnfs-raw-nfs4.h: No such file` → it is in `nfs4/`; add `-I nfs4`.

3. With a minimal `config.h` + `-I include -I include/nfsc -I nfs -I nfs4 -I mount
   -I portmap`: only remaining error was implicit `htonl`/`ntohl`, provided by
   Phoenix `arpa/inet.h` (`#define htonl htobe32`). A standalone `#include
   <arpa/inet.h>; htonl()` program compiled cleanly with the Phoenix toolchain.

Conclusion: the OS seam = standard POSIX headers Phoenix already ships; configure's
`HAVE_*` feature-defines are all that's needed. Full report:
`docs/research/2026-06-07-nfs-rootfs-feasibility/`.
