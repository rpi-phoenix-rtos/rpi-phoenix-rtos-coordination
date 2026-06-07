# NFS-root for Phoenix-RTOS on Raspberry Pi 4 — Feasibility Study

**Date:** 2026-06-07
**Author:** orchestrator (research)
**Status:** feasibility + plan (no implementation)
**Goal:** mount a Pi 4 rootfs (and/or a test-deploy directory) over **NFS from the
Linux dev host** in the netboot scenario, eliminating SD-card swaps for deploying
new test binaries. SD-card rootfs must keep working; NFS is *additive*.

---

## Verdict: **GO** (staged, low-to-moderate risk)

All the load-bearing primitives already exist in the Phoenix tree. The socket API
is complete and usable from an arbitrary userspace process (not only inside the
lwip process); libnfs's synchronous API needs nothing beyond `socket/connect/bind/
poll/getsockopt/setsockopt/fcntl/recv/send/read/write/writev/gettimeofday`, all of
which Phoenix provides; libnfs is self-contained (bundles its own XDR/RPC, no
rpcgen/libtirpc); and the Phoenix VFS `mt*` message protocol is a clean target for
a userspace NFS fs-server modeled on `dummyfs`.

A first compile of libnfs core against the real Phoenix `aarch64-phoenix` toolchain
already succeeds once the standard configure feature-defines are supplied
(see [05-build-port-and-license.md](05-build-port-and-license.md)).

**Caveat on the verdict:** it is "go, **expect a small OS-seam port + a DHCP-wait in
the client + an `insecure` host export**," not "go, clean." Those three are the only
non-trivial pieces; each is detailed below.

**Other arches:** this is *not* Pi 4-specific. The socket stack
(`libphoenix` + `phoenix-rtos-kernel/posix` + `phoenix-rtos-lwip`), the `mt*` VFS
protocol, and the libnfs port are all arch-neutral and shared across targets — only
the port's `--host` toolchain triple and the NIC driver (GENET on Pi 4) differ. Any
Phoenix target that runs lwIP + the `/dev/netsocket` server gets the NFS client for
free once libnfs is ported.

### The three biggest risks (none flips the verdict)

1. **Root + network ordering (option c only).** To mount NFS *as `"/"`*, lwIP +
   GENET + DHCP must be up *before* `"/"` exists, and Phoenix's namespace does not
   support remounting/pivoting `"/"` (a second `portRegister("/")` fails `-EEXIST`,
   per the #120 note in `user.plo.yaml:13-14`). NFS-as-root therefore means
   *reordering the network ahead of dummyfs-root and having the NFS server register
   `"/"` directly* — feasible but invasive. **Mitigation: do option (b) first**
   (NFS at `/mnt/nfs`, no ordering change), which delivers ~all the dev-velocity
   win at a fraction of the risk. See [04-rootfs-ordering.md](04-rootfs-ordering.md).

2. **Privileged source port + NFS export security.** libnfs by default binds a
   *reserved* (<1024) source port for NFS (`lib/socket.c` `rpc_bind_reserved`,
   the `EACCES`/`bind()` path at lines ~1480/1539). Phoenix lwIP imposes no
   privileged-port restriction (no check in `phoenix-rtos-lwip/port/sockets.c`), so
   the bind should succeed — **but** the safe and simplest host posture is to export
   with `insecure` (allow >1023 source ports) so the port works regardless. Call it
   out in `/etc/exports`. See [02-phoenix-socket-api.md](02-phoenix-socket-api.md)
   and [03-host-side.md](03-host-side.md).

3. **A few small OS-seam patches, not a rewrite.** Expect roughly **2–4 tiny
   patches** to make libnfs build/behave on Phoenix: a `config.h` (configure
   normally generates it), possibly a `writev`-on-socket nuance, and the
   reserved-port behavior. These are the difference between "go, clean" and "go,
   expect a handful of one-liners," documented per-area below.

4. **DHCP completion is asynchronous — the client must wait for an address.**
   `-x lwip;genet` only *launches* lwIP; DHCP discover/offer/request/ack completes
   seconds later while `go!`/psh proceed. An NFS client launched on the next plo line
   would call `nfs_mount` **before the interface has an address** → no-route failure.
   **Nothing in the tree waits for DHCP today** (grep: no `dhcp_supplied`/bound-wait
   anywhere) — the NFS client is the first component that must. The mechanism exists:
   poll `netif_is_dhcp`/interface address (the data behind `devs.c:242`
   `%s%d_dhcp=%u`) until bound, with a timeout, before `nfs_mount`. This is a
   required step in T1 and a hard requirement for option (c) (network-down = no
   root). See [02](02-phoenix-socket-api.md) (OQ-4) and [04](04-rootfs-ordering.md).

### Recommended protocol: **NFSv4.1** (with v3 as fallback)

NFSv4 uses a single well-known TCP port (2049) and folds mount + lock + portmap
into the protocol — **no separate portmapper/rpcbind/mountd round-trips**, which is
the simplest possible client/host posture. libnfs supports it via
`nfs_set_version(nfs, NFS_V4)` or URL `nfs://host/path?version=4`
(`include/nfsc/libnfs.h:359-367`). Caveat: libnfs's v4 path is newer/less
battle-tested than its v3 path; keep v3-over-TCP as a fallback (v3 also works but
pulls in the portmap/mount RPCs — still bundled in libnfs, no extra host daemon
config beyond `nfs-kernel-server`). See
[01-libnfs-inventory.md](01-libnfs-inventory.md).

---

## Staged plan (T0 → T4)

| Tier | What it proves | Effort | Validation |
|------|----------------|--------|------------|
| **T0** | libnfs cross-compiles for `aarch64-phoenix` (OS seam ported) | S | host build only; `aarch64-phoenix-gcc` link of a static `libnfs.a` |
| **T1** | sockets + RPC work end-to-end on HW: a userspace test app `nfs_mount`s the host and reads one file | M | netboot the app via plo `-x`, read `/etc/hostname` from host, print bytes over UART |
| **T2** | a Phoenix userspace fs-server wraps libnfs and answers the `mt*` VFS protocol | M–L | server registers a port; `mtLookup/mtRead/mtGetAttr` serviced; unit-test against host export |
| **T3** | **NFS mounted at `/mnt/nfs`** — drop test binaries with no SD swap (**option b**) | S (after T2) | netboot, `portRegister("/mnt/nfs")`, run a binary copied to the host export tree |
| **T4** | NFS-as-rootfs via network-before-`"/"` reorder (**option c**) | L | new plo ordering; lwip+NFS bring `"/"`; full boot to psh from NFS root |

**Recommendation:** ship **T0→T3** as the primary deliverable. T3 alone removes the
SD-swap deploy loop, which is the headline benefit. Treat **T4** as a later,
attended, one-way-door change to the boot sequence.

Per-tier detail, dependencies, and exact validation commands:
[06-feasibility-and-plan.md](06-feasibility-and-plan.md).

---

## Evidence map (where each claim is grounded)

| Area | Note file | Key primary-source anchors |
|------|-----------|----------------------------|
| libnfs inventory, API, portability seam | [01-libnfs-inventory.md](01-libnfs-inventory.md) | `research/libnfs@f0b109d`; `lib/socket.c`, `lib/libnfs-sync.c`, `include/win32/win32_compat.h` |
| Phoenix socket surface (cross-process, TCP/UDP, poll, O_NONBLOCK) | [02-phoenix-socket-api.md](02-phoenix-socket-api.md) | `libphoenix/sys/socket.c:56-66`; `phoenix-rtos-kernel/posix/posix.c:1758`, `inet.c:269`; `phoenix-rtos-lwip/port/sockets.c:814-843,1246` |
| Host side (dnsmasq subnet, exports, mount opts) | [03-host-side.md](03-host-side.md) | `scripts/netboot-server.sh:19-25,69-101` |
| VFS `mt*` protocol → libnfs mapping; rootfs ordering | [04-rootfs-ordering.md](04-rootfs-ordering.md) | `phoenix-rtos-filesystems/dummyfs/srv.c:219-357`; `_projects/aarch64a72-generic-rpi4b/user.plo.yaml:9-79` |
| Build/port integration + license + RPC/XDR | [05-build-port-and-license.md](05-build-port-and-license.md) | `phoenix-rtos-ports/curl/port.def.sh`; libnfs `lib/libnfs-zdr.c`, `nfs/libnfs-raw-nfs.c`; compile test |
| Verdict + tiered plan | [06-feasibility-and-plan.md](06-feasibility-and-plan.md) | synthesis |

Cloned libnfs commit: **`f0b109df8fd865a2f8d39e78310fd875e15f3ac1`** at
`/home/houp/phoenix-rpi/research/libnfs/` (outside `sources/`, not under the build
tree's `find`).
