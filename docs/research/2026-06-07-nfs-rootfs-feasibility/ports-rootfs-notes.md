# Ports rootfs on the NFS export (#153 follow-on) — host build inventory

**Date:** 2026-06-08. **Mode:** host-only (NO HW; orchestrator boots + runs).
**Goal:** build a broad set of phoenix-rtos-ports for `aarch64a72-generic-rpi4b` and
stage them onto the NFS export `/srv/phoenix-rpi4-nfs` so the Pi can exec them live
from the NFS root (exec-from-NFS-root proven in T3 design-A).

## Build invocation

A new `--ports-only` flag was added to `scripts/rebuild-rpi4b-fast.sh`. It builds ONLY
the `ports` build stage (`build.sh ports`), which writes port binaries straight into the
rootfs tree `_fs/<target>/root` (`PREFIX_ROOTFS`). It does NOT run `project`/`image`, so
**loader.disk is never rebuilt** (verified byte-identical sha256 before/after:
`fb369245578da9acd4446a688c1fc7427c99c5977a2eddf2f0a5f5cbf855414d`).

```
./scripts/rebuild-rpi4b-fast.sh --ports-only      # prepare (sync buildroot) + build.sh ports
```

The set of ports is selected by
`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml`.

## Two build-infra fixes required (no aarch64 target had built autotools/openssl ports before)

1. **`phoenix-rtos-build/port_manager/port_internal.subr`** — `reset_env()` derived the
   autotools host triplet as `${TARGET_FAMILY}-phoenix` = `aarch64a72-phoenix`, which
   `config.sub` rejects (`machine aarch64a72 not recognized`). Added an `aarch64*` branch
   normalizing `HOST=aarch64-phoenix` (matching the `aarch64-phoenix-gcc` CROSS prefix),
   mirroring the existing `arm*` collapse. `HOST_TARGET` kept as `$TARGET_FAMILY`
   (only set, never consumed elsewhere — verified by grep). Without this, every autotools
   port (pcre, curl, dropbear, libevent, jansson, lzo, ...) fails at `./configure`.

2. **`phoenix-rtos-ports/openssl111/30-phoenix.conf`** — openssl uses its own
   `./Configure phoenix-${TARGET_FAMILY}-${TARGET_SUBFAMILY}` = `phoenix-aarch64a72-generic`,
   which had no target entry (only arm/ia32/riscv64). Added a `phoenix-aarch64a72-generic`
   target (aarch64_asm, cortex-a72 cflags, linux64 perlasm).

## Inventory — ports BUILT and staged on `/srv/phoenix-rpi4-nfs`

15 of 16 attempted ports built. Standalone binaries on the export:

| binary | path on export | size |
|---|---|---|
| busybox | /bin/busybox | 399672 |
| coremark | /bin/coremark | 56928 |
| micropython | /bin/micropython | 455328 |
| picocom | /bin/picocom | 131608 |
| lua | /usr/bin/lua | 243064 |
| luac | /usr/bin/luac | 155384 |
| curl | /usr/bin/curl | 1203608 |
| openssl | /usr/bin/openssl | 2213008 |
| dropbear (server) | /usr/sbin/dropbear | 452896 |
| dropbearmulti | /usr/bin/dropbearmulti | 452896 |
| dbclient | /usr/bin/dbclient | 452896 |
| scp | /usr/bin/scp | 452896 |

Library-only ports (no standalone binary; statically linked into the above): **zlib,
lzo, pcre, jansson, libevent, mbedtls**. (zlib→dropbear; mbedtls→curl TLS; openssl is its
own binary; the rest are linked but no consumer binary was requested.)

All staged binaries are `ELF 64-bit ... ARM aarch64, statically linked, stripped`
(verified with `file`) — no host x86 contamination, no shared-lib loader needed
(`/lib` absent on the export, by design).

### busybox applets = HARDLINKS (not symlinks)

The busybox config uses `CONFIG_INSTALL_APPLET_HARDLINKS=y`, so `make install` already
created the applet dispatch entries as **84-way hardlinks** to busybox in `/bin` and
`/usr/bin` (sh, ls, cat, grep, tar, vi, find, wc, sort, ...). No symlink setup needed —
busybox dispatches on `basename(argv[0])` and a hardlink is a complete independent entry.
`rsync -aH` preserved the hardlink set on the export (total export = 6.5M, not ~30M).
`/bin/sh` resolves to busybox aarch64 ELF and is executable. ~41 applets in /bin, ~42 in
/usr/bin.

## Ports that FAILED / dropped (one line each)

- **lighttpd** — DROPPED. Its `p_prepare` does `find $PREFIX_ROOTFS/etc -name lighttpd.conf`
  to generate the static-plugin header; that conf comes from the project rootfs-overlay,
  which the standalone `ports` stage does not stage, so `/etc` is absent and prepare aborts
  under `set -e`. Needs the full `project` stage (which rebuilds loader.disk — out of scope
  for a live-NFS, no-image-rebuild task). Builds fine in a normal full image build.
- (No other failures: openssl/mbedtls/curl/dropbear/micropython/lua/coremark/picocom/
  pcre/jansson/lzo/libevent/zlib/libnfs/busybox all built.)

## Staging

```
rsync -aH _fs/aarch64a72-generic-rpi4b/root/.  /srv/phoenix-rpi4-nfs/   # no --delete
mkdir -p /srv/phoenix-rpi4-nfs/{root,dev,tmp,mnt}
chmod -R a+rX /srv/phoenix-rpi4-nfs/{bin,sbin,usr}                      # Pi uid 0 exec
```

Preserved scaffolding (no `--delete`): `etc/hostname` (=`phoenix-rpi4-nfs`),
`etc/nfs-smoke.txt`, `bin/nfs-smoke`, `test/`, `pi-copy.txt`, `nfs-smoke-marker.txt`.
Runtime dirs present: `/dev` (mountpoint for the devfs re-bind), `/tmp`, `/mnt`, `/root`.
(busybox `make install` also dropped a `/linuxrc` busybox-hardlink at the root — harmless.)

## Suggested psh run-list on the Pi (from the NFS root)

```
/bin/busybox            # prints applet list + version (1.27.2)
/bin/sh                 # busybox ash interactive shell
/bin/busybox ls -la /   # busybox ls of the NFS root
/bin/busybox uname -a
/usr/bin/lua -v         # Lua 5.3.6
/usr/bin/lua -e 'print("hello from NFS rootfs")'
/bin/micropython -c 'print(2**100)'
/bin/coremark           # EEMBC CoreMark benchmark (runs ~10-12 s, prints score)
/usr/bin/openssl version
/usr/bin/curl --version
/usr/sbin/dropbear -h   # SSH server usage (full server needs host keys + net)
```

Note: only busybox lands in `/bin`; lua/curl/openssl/dropbear land in `/usr/bin`
(`/usr/sbin` for the dropbear server). Use absolute paths from psh.
