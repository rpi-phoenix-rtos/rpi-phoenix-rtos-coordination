# Linux host side — NFS server for the Pi

The netboot lab already runs the host on a dedicated NIC; adding an NFS export is a
small additive step on the same subnet.

## Existing netboot network (from `scripts/netboot-server.sh`)

- NIC / subnet: `RPI4B_NETBOOT_IFACE` (default `eth1`), host IP
  **`10.42.0.1/24`** (`netboot-server.sh:19-21`).
- DHCP pool for the Pi: **`10.42.0.10 .. 10.42.0.20`**, 12h lease
  (`netboot-server.sh:22-23,81`). So the Pi's NFS client address is in
  `10.42.0.0/24`.
- TFTP root (kernel/bootfs) is served from the buildroot at
  `.buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs` (`:25`). dnsmasq is
  DHCP+TFTP only (`port=0` disables DNS, `:76`); it does **not** do NFS — NFS is a
  separate `nfs-kernel-server`.

## What to install / configure on the host

```sh
sudo apt-get install -y nfs-kernel-server   # kernel NFS server (v3 + v4)
```

Export the test/rootfs tree. A new helper (e.g. `scripts/nfs-export-up.sh`, to be
added when implementation starts) should manage this the way `netboot-server.sh`
manages dnsmasq — do not hand-edit. The export it would write:

### `/etc/exports` (sample)

```
# NFS export for Phoenix Pi 4 netboot test deploy / rootfs.
# Path is the directory the Pi mounts. Subnet matches the netboot DHCP pool.
/srv/phoenix-rpi4-nfs  10.42.0.0/24(rw,sync,no_subtree_check,no_root_squash,insecure,fsid=0)
```

Option rationale (each is load-bearing for an embedded NFS client):

| Option | Why |
|--------|-----|
| `rw` | the whole point — read/write persistent storage, drop binaries without SD swaps |
| `no_root_squash` | Phoenix processes run as uid 0; without this, root-owned writes get squashed to `nobody` and fail |
| `insecure` | accept NFS requests from source ports **>1023**. libnfs prefers a reserved port but this removes any dependency on the client binding <1024 (risk 2 in the main report) |
| `no_subtree_check` | standard modern default; avoids subtree-check overhead/bugs |
| `sync` | data integrity on the host (safe default; `async` only for throughput experiments) |
| `fsid=0` | makes this the **NFSv4 pseudo-root**; with v4 the client then mounts `host:/` (the export appears at the v4 root). For v3 this is harmless. |

Apply:

```sh
sudo mkdir -p /srv/phoenix-rpi4-nfs
sudo exportfs -ra
sudo systemctl enable --now nfs-kernel-server
showmount -e 10.42.0.1     # sanity: lists the export (v3 path)
```

Firewall: on the lab NIC this is a private crossover/point-to-point link, so no
extra firewalling is needed; if a host firewall is active, allow TCP/UDP 2049 (and
for v3 also 111 + the mountd/statd ports, or pin them).

## Client-side mount parameters (what libnfs passes)

- **NFSv4 (recommended):** server `10.42.0.1`, export path `/` (because `fsid=0`
  makes `/srv/phoenix-rpi4-nfs` the v4 pseudo-root). libnfs URL:
  `nfs://10.42.0.1/?version=4` or `nfs_set_version(nfs, NFS_V4)` +
  `nfs_mount(nfs, "10.42.0.1", "/")`.
- **NFSv3 (fallback):** `nfs_mount(nfs, "10.42.0.1", "/srv/phoenix-rpi4-nfs")`;
  libnfs handles portmap(111)→mountd→nfs(2049) internally.
- Tune `rsize`/`wsize` down (e.g. 32–64KB) if the Phoenix message transport caps
  single `mtRead`/`mtWrite` payloads (OQ-3 in note 02). libnfs chunks automatically.

## Populating the export for the dev-velocity win (T3)

The build already stages a rootfs tree (see `scripts/build-rpi4b-rootfs-ext2.sh`
and the `_fs` tree). For NFS test-deploy, the implementation step would point the
rootfs/overlay staging at `/srv/phoenix-rpi4-nfs` so that a rebuild drops new
binaries straight into the exported tree — the Pi sees them on its next access with
**no flashing, no SD shuttle**. This is the concrete mechanism behind the
"game-changer" claim.
