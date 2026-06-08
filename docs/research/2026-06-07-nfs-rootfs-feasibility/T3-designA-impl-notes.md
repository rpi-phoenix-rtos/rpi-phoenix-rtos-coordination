# T3 design-A implementation notes — NFS export takes over "/" (host build)

**Date:** 2026-06-08. **Mode:** host-only implementation + build (NO HW; orchestrator HW-tests).
**Implements:** #153 T3, Path 2 (design-A) from `T3-devfs-resolution-verdict.md`. Supersedes the
abandoned pre-"/" `root` mode (blocked by the kernel pre-"/" resolver gap, Gap B, UPDATE 3 of
`T3-attempt-1-result.md`).

## The design in one paragraph

Bring up a normal **dummyfs RAM "/"** FIRST (so posixsrv + `bind devfs /dev` + lwip all initialise
normally and sockets resolve via the real "/"), THEN the NFS server mounts the export and **takes
over "/"**. This sidesteps Gap B by construction — it never exercises the failing pre-"/"
devfs-reachability path; it reproduces the proven netboot/subtree (#T3b) order and only adds (1)
mount-at-"/" instead of /nfstest, (2) a "/dev" re-bind after takeover.

## A. NFS server takeover mode (`filesystems/nfs/srv.c` `nfs_runTakeover`)

New trailing argv token `takeover` (distinct from `root` pre-"/" mode and the default /nfstest
subtree mode): `nfs / 10.42.0.1 / v4 takeover`.

- **DHCP wait:** the NORMAL `/dev/ifstatus` wait (`wait_for_dhcp_lease`, the subtree path) — it
  works now that "/" exists. No bounded-retry hack.
- **Mount:** `nfs_makeContext` + `nfs_mount` (the subtree path). On success it prints the SAME
  marker the subtree mount does: `nfs-fs: mounted <srv>:<exp> via v4` — this is the design-A
  PREMISE check (sockets work with a dummyfs "/" up).
- **/dev re-bind (in-process):** `lookup("devfs", …)` (the named port survives the root swap — it
  is a kernel-dcache name, not under rootOid), `nfs_mkdir2("/dev")` (EEXIST ok), `nfs_node_get
  ("/dev")`, set `type=otDir` and `node->mnt = devfsOid`. Done BEFORE registering "/" and before
  starting the loop thread, so the instant NFS becomes "/", `/dev` already resolves and there is no
  race. Logs `nfs-fs: re-bound /dev (takeover, devfs port=N)`.
  - **Why in-process, not a second `bind` program:** this is exactly what `bind devfs /dev` does
    (`mtSetAttr(atDev)` → store the dev oid on the /dev node), and doing it in-process avoids a
    second `bind` launch that would (a) collide with the pre-takeover `bind` alias in the same
    rendered script and (b) require a psh-applet rename hack (psh dispatches on `basename(argv[0])`,
    so a `bind2` copy would not be recognised as the `bind` applet). Keeps the whole change in
    `filesystems`.
- **Takeover of "/": runtime decision, both paths logged.**
  1. Try the proven `mtSetAttr(atDev)` splice onto the existing "/" oid (resolved via
     `lookup("/")`), then re-resolve "/": if it now points at our port, log
     `nfs-fs: takeover via splice rc=%d` and proceed.
  2. Else fall back to `portUnregister("/")` + `portRegister(port, "/", &root)`, log
     `nfs-fs: takeover via portRegister rc=%d (unregister rc=%d)`.
  - **Expected on HW: `via portRegister`.** The kernel returns the registered `rootOid` for a bare
    `lookup("/")` WITHOUT consulting the root node's `atDev` (`name.c:239-256`), so the splice
    no-ops for "/" and the fallback fires. We still try the splice first and decide at runtime so
    the log reflects reality on any kernel.
  - On success: `nfs-fs: registered / (takeover)`.
- **Self-parent ".."** at "/" (parent = {own-port, NFS_ROOTID}); **>=64 KB stacks** kept.

### Supporting change: `node->mnt` mount-oid field (the crux)

The pre-existing `nfs_ops_setattr` `atDev` case ACCEPTED the splice oid but DISCARDED it, and
`nfs_ops_lookup` never returned a mounted-child dev oid — so a `bind` onto an NFS dir (the /dev
re-bind) would silently no-op and `/dev` on the NFS root would be empty. Added (mirrors the dummyfs
per-object `dev` field):
- `nfs_node_t.mnt` (oid; `mnt.port==0` = no mount). calloc-zeroed; root node explicit.
- `nfs_ops_setattr(atDev)` now `memcpy`s the child oid into `n->mnt`.
- `nfs_ops_lookup` returns `cn->mnt` (and stops the walk) when crossing a mountpoint mid-path, and
  returns `node->mnt` as the final `*dev` when the resolved node is itself a mountpoint — so the
  kernel redirects path resolution into the child fs (e.g. devfs at /dev).

## B. nfsroot variant boot ordering (`user.plo.yaml`)

Reworked the `nfsroot` branch to design-A. **netboot + sd renders are byte-identical to HEAD**
(verified by rendering both with jinja2 and diffing — IDENTICAL). The nfsroot render is:

```
dummyfs-root                          # RAM "/" — now INCLUDED for nfsroot (was skipped)
dummyfs -N devfs -D
pl011-tty
mkdir /dev ; bind devfs /dev          # on the dummyfs root — lets lwip/posixsrv come up
posixsrv
lwip genet:...                        # leases + registers netsocket normally ("/" exists)
nfs / 10.42.0.1 / v4 takeover         # mount + take over "/" (+ in-process /dev re-bind)
rpi4-thermal ; rpi4-hwrng ; rpi4-fb ; rpi4-gpio ; usb ; psh   # ALL after takeover
bcm2711-emmc                          # probe-only (card-out safe), same as netboot
```

- `dummyfs-root` gate flipped from `not in ['sd','nfsroot']` → `!= 'sd'` (include for nfsroot).
- thermal/hwrng/fb/gpio/usb/psh gated OUT of nfsroot at their normal positions and re-launched in
  the post-takeover block (so they see /dev on the NFS root). Duplicate `-x <name>` across
  mutually-exclusive `if:` gates is safe (established pattern).
- **No `mkdir2`/`bind2`** — the /dev re-bind is in-process (see A). This is the one place the task's
  prescription changed: I confirmed the distinct-alias approach WOULD be needed for a second `bind`
  *program* (plo dedups per-alias within a render; psh dispatches on argv[0]), but the in-process
  re-bind is cleaner and keeps the change in `filesystems`.

## C. Build

`./scripts/rebuild-rpi4b-fast.sh --scope core --variant nfsroot --build-only` — clean (libnfs port
already built; no `--with-ports` needed). loader.disk proof (`_boot/aarch64a72-generic-rpi4b/
loader.disk`):
- boot script: `app … -x nfs;/;10.42.0.1;/;v4;takeover`, `-x dummyfs-root`, NO `mkdir2`/`bind2`.
- nfs binary markers present: `mounted %s:%s via %s`, `re-bound /dev (takeover, devfs port=%u)`,
  `takeover via splice rc=%d`, `takeover via portRegister rc=%d`, `registered / (takeover)`.

## UART markers the orchestrator checks (IN ORDER)

1. `nfs-fs: mounted … via v4` — PREMISE holds (socket works with dummyfs-/ up).
2. `nfs-fs: registered / (takeover)` — takeover OK (expect `via portRegister` just above it).
3. `ls /dev` shows device nodes — the in-process /dev re-bind survived the root swap.
4. `cat /etc/hostname` at `/` = `phoenix-rpi4-nfs` — exec/read from the NFS root; 0 faults.

## Regression note

The kernel `inet.c` Gap-A fallback (`ddc3b091`) ships in ALL images and is still UNPROVEN on
netboot — regression-test `--variant netboot` (nfs-smoke mount+read + 0 faults) to confirm it is
inert post-"/".

## DIAG / cleanup debt

The `root`-mode probes (`probe devfs/netsocket`, `probe /dev/netsocket`) and lwip/dummyfs DIAG
prints (lwip `be98d19`, filesystems `6c263f6`/`09f5c90`) from the abandoned pre-"/" path remain;
they are revertable and out of scope for this host build. `nfs_runRoot` is kept for reference.
