# Feasibility verdict + tiered plan

## Verdict: GO (staged)

Every load-bearing capability is already present in the tree:

- **Sockets are complete and cross-process** — a real BSD socket API backed by
  message-passing to the lwIP socketsrv at `/dev/netsocket`, usable from any process
  (note 02). libnfs's sync API needs only `socket/connect/bind/poll/getsockopt/
  setsockopt/fcntl/recv/send/read/write/writev/gettimeofday/htonl`, all present and
  correctly routed for `ftInetSocket`.
- **libnfs is self-contained and portable** — single-threaded `poll` sync loop,
  bundled XDR/RPC (no rpcgen/libtirpc), POSIX-header OS seam that Phoenix's sysroot
  satisfies; core already compiles with `aarch64-phoenix-gcc` given configure's
  feature-defines (notes 01, 05).
- **The VFS target is clean** — the `mt*` protocol maps 1:1 onto libnfs calls, and
  the server model is dummyfs-shaped (standalone, no block device) (note 04).
- **The host is one `apt-get` + one `/etc/exports` line away** on the existing
  `10.42.0.0/24` netboot subnet (note 03).

### Single biggest risk

The **root-ordering problem for NFS-*as-root* (option c)**: Phoenix has no
root-pivot/remount, so v4-style "mount over `/`" is impossible; NFS-as-root requires
reordering the network ahead of dummyfs-root and having the NFS server own `"/"`.
This is invasive to the boot sequence and makes the network a hard boot dependency.
**It is fully sidestepped by shipping option (b) first** (NFS at `/mnt/nfs`), which
needs no ordering change and delivers the SD-swap-elimination benefit.

Secondary risks: reserved-port bind vs export security (mitigated by `insecure`
export); a possibly-chattier `writev` path (correct, just more IPC); single-message
payload caps affecting `rsize`/`wsize` tuning (throughput, not feasibility).

---

## Tiered plan

### T0 — Cross-compile libnfs for `aarch64-phoenix`
- **Proves:** the OS seam ports; `libnfs.a` builds for the target.
- **Work:** add `phoenix-rtos-ports/libnfs/port.def.sh` (model on `curl`), fetch the
  release tarball (has `configure`), `./configure --host=aarch64-phoenix
  --disable-pthread --disable-shared --enable-static --without-libkrb5`, stage
  `libnfs.a` + headers. Resolve any `HAVE_*` the cross-configure misdetects (the
  study already proved `HAVE_SOCKADDR_STORAGE` + `arpa/inet.h` are the only quirks
  hit so far).
- **Effort:** S (a day-ish; mostly configure plumbing).
- **Deps:** Phoenix toolchain (present).
- **Validation:** host-only — successful static link; `nm libnfs.a` shows
  `nfs_mount`, `nfs_pread`, etc. **Not** netboot-testable yet.

### T1 — Userspace NFS test app on HW
- **Proves:** sockets + RPC + XDR work end-to-end on real Pi 4 hardware over GENET:
  `nfs_init_context` → `nfs_set_version(NFS_V4)` → `nfs_mount("10.42.0.1","/")` →
  `nfs_open` + `nfs_pread` one file → print bytes over UART. Resolves OQ-1/OQ-2/OQ-3.
- **Work:** a ~100-line `nfs-smoke` program linked against `libnfs.a`, added to the
  build, launched from `user.plo.yaml` with `-x nfs-smoke` **after** the
  `lwip;genet` line. **Must first wait for DHCP to bind** (poll interface address /
  `netif_is_dhcp` with a timeout) before `nfs_mount` — nothing in the tree does this
  today (OQ-4 / note 02). Use `nfs_set_version(NFS_V4)` then
  `nfs_mount("10.42.0.1","/")`.
- **Effort:** M.
- **Deps:** T0; host NFS export up (note 03); lwIP/GENET (already working —
  ping RTT ~0.9ms per project memory).
- **Validation:** netboot cycle; UART shows the file's contents + RTTs. This is the
  decisive feasibility gate — if v4 misbehaves, fall back to v3-over-TCP here.

### T2 — NFS fs-server speaking `mt*`
- **Proves:** libnfs can be wrapped as a Phoenix userspace filesystem.
- **Work:** new `phoenix-rtos-filesystems/nfs/` server: dummyfs-style `msgRecv` loop,
  an `id → {path, struct nfsfh*}` idtree, and the `mt*`→libnfs mapping in note 04
  (start with read-only: `mtLookup/mtOpen/mtRead/mtGetAttr/mtReaddir/mtClose`; add
  write ops next).
- **Effort:** M–L.
- **Deps:** T1.
- **Validation:** register at a test mountpoint, `cat`/`ls` host files via psh;
  compare bytes against the host tree.

### T3 — NFS at `/mnt/nfs` (option b) — **PRIMARY DELIVERABLE**
- **Proves:** test binaries deploy to the Pi **without SD swaps**.
- **Work:** add one `-x nfs;/mnt/nfs;10.42.0.1;/;v4` line to `user.plo.yaml`
  *after* `lwip;genet` (additive, like thermal/hwrng/gpio were). `mkdir /mnt/nfs`
  first. Point the rootfs/overlay staging at `/srv/phoenix-rpi4-nfs` so a rebuild
  drops binaries straight into the export.
- **Effort:** S (after T2).
- **Deps:** T2; host export populated.
- **Validation:** netboot, then run a freshly-built binary from `/mnt/nfs` that was
  never flashed to SD. **This is the headline win.**

### T4 — NFS-as-rootfs (option c)
- **Proves:** full `"/"` over NFS.
- **Work:** reorder `user.plo.yaml` to bring `lwip;genet` + the NFS server up
  **before** dummyfs-root; NFS server `portRegister("/")` (the sd-ext2-at-`/` flow at
  `user.plo.yaml:28` is the precedent); keep an SD/dummyfs fallback for network-down
  boots. Likely needs care around what runs before `"/"` exists.
- **Effort:** L; **one-way-door boot-sequence change → attended only.**
- **Deps:** T3 proven stable.
- **Validation:** full boot to psh with `/` served from the host; verify writes
  persist on the host; verify graceful fallback when the host export is down.

---

## Net recommendation

Build **T0 → T3** as the project's NFS deliverable. T3 alone eliminates the
SD-card-swap deploy loop — the entire motivating benefit — with only additive,
deterministic, netboot-testable changes. Defer **T4** to an attended session as a
boot-sequence change with a fallback, once the libnfs↔`mt*` stack is proven on
hardware at T1–T3.
