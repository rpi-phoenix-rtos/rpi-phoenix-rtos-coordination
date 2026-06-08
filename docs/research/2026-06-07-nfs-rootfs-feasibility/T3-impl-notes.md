# T3 — NFS-as-rootfs: implementation notes (changes 1–3)

Implements changes 1–3 of `T3-nfsroot-feasibility.md` §1/§3 (the §4 fallback is
deferred to a later step). NFS already works read-write+exec at `/nfstest`
(T0–T3b, HW-proven); this makes "/" itself the NFS mount. Host-only build; all
HW tests run by the orchestrator.

## Root-mode arg form chosen

A **trailing `root` token** on the existing positional argv:

```
nfs / 10.42.0.1 / v4 root
       ^server   ^export ^ver ^root-mode
```

`argv[1]` mountpoint (ignored in root mode — it is always "/"), `argv[2]` server,
`argv[3]` export, `argv[4]` version, `argv[5] == "root"` selects root mode. Chosen
over a `-R` flag because it slots into the server's existing positional parsing
(`srv.c:main`) with no getopt machinery, and reads naturally in the plo line.

## Change 1 — libphoenix `socksrvcall` devfs fallback (CORE/all-arch, additive)

`sources/libphoenix/sys/socket.c` `socksrvcall()`. Pure fallback: only fires when
the literal `lookup(PATH_SOCKSRV == "/dev/netsocket")` returns < 0 (i.e. before
"/" is registered). Then it does `lookup("devfs/netsocket", ...)` — the named-port
path that resolves pre-"/" (the socket node already exists in devfs because lwip
`create_dev`'s it via the "devfs" named port, stripping `/dev/`). Mirrors
`create_dev` and `flashsrv.c` `flash_oidResolve`. Once "/" exists the literal
lookup succeeds and behaviour is byte-identical to before. SET_ERRNO only on the
fallback's failure, never on the literal failure (it must fall through).

```c
if (lookup(PATH_SOCKSRV, NULL, &oid) < 0) {
    /* pre-"/" NFS-root fallback: reach the node via the devfs named port */
    if ((err = lookup("devfs/netsocket", NULL, &oid)) < 0)
        return SET_ERRNO(err);
}
if ((err = msgSend(oid.port, msg)) < 0)
    return SET_ERRNO(err);
```

Verified compiled into the built `libphoenix.a` (string `devfs/netsocket` present).

## Change 2 — NFS server root mode (`sources/phoenix-rtos-filesystems/nfs/srv.c`)

- New `nfs_makeContext(version)` helper: `nfs_init_context` + `set_version` +
  `set_timeout(5000)` + `set_readmax/writemax(32K)`. Used by both the non-root
  path (refactor, identical behaviour) and the root retry loop (rebuild per attempt).
- New `nfs_runRoot(server, export, verstr, version)`:
  - (a) Accepts "/" — the old refusal at the top of `main` is now gated behind
    "not root mode"; root mode never reaches it.
  - (b) **Skips `wait_for_dhcp_lease`** (the `fopen("/dev/ifstatus")` path, unusable
    pre-"/"). Instead bounded-retries: each iteration `nfs_makeContext` (fresh
    context) + `nfs_mount`; on failure `nfs_destroy_context` and `usleep(1s)` so a
    fast connect-refused can't hot-loop. Deadline is **wall-clock** `time(NULL)` +
    60 s (not iteration counting — each `nfs_mount` can take 5–15 s). DHCP completing
    is observed indirectly by the mount succeeding. On deadline expiry prints the LOUD
    line `nfs-fs: FATAL root mount failed after 60s, / not registered` and returns
    non-zero (no silent hang). `nfs_set_timeout(5000)` kept.
  - (c) **Registers "/" directly** (no splice): `nfs_node_init` → `portCreate(&port)`
    → `portRegister(port, "/", &root)` (mirrors dummyfs/srv.c:219-227 +
    sdstorage_srv.c). Does NOT start `nfs_mountThread` (its `mtSetAttr(atDev)` splice
    waits for an existing "/" and would deadlock when we ARE "/"). Prints the success
    line `nfs-fs: registered / (root mode)` right after `portRegister`.
  - (d) **Self-parent**: `common.fs.parent = {own-port, NFS_ROOTID}` so ".." at "/"
    stays at "/" (POSIX root).
  - Runs the msg loop via `beginthread(nfs_loopThread, ... common.loopStack, ...)` on
    the existing >=64 KB stack (#120 lesson). `mountStack` is unused in root mode.

## Change 3 — `nfsroot` plo variant

`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`.
The templater is **per-item `if:`** only (no spanning `{% %}` blocks). Duplicate
`app -x <name>` aliases across mutually-exclusive gates are safe (only the rendered
survivor counts — same as the two existing `bcm2711-emmc` lines). Netboot and sd
render byte-identically to before (verified).

### Every gate's behaviour

| line (orig) | gate before | gate after | nfsroot effect |
|---|---|---|---|
| dummyfs-root | `!= 'sd'` | `not in ['sd','nfsroot']` | **skipped** (NFS owns "/", no -EEXIST) |
| dummyfs;-N;devfs;-D | (none) | (none) | runs — devfs named port up early |
| pl011-tty | (none) | (none) | runs |
| **NEW** lwip;genet | — | `== 'nfsroot'` | **runs here** (network up before NFS root) |
| **NEW** nfs;/;...;v4;root | — | `== 'nfsroot'` | **runs here** (root mount + register "/") |
| **NEW** mkdir;/dev | — | `== 'nfsroot'` | **runs here** (after "/" exists; /dev only) |
| **NEW** bind;devfs;/dev | — | `== 'nfsroot'` | **runs here** |
| **NEW** posixsrv | — | `== 'nfsroot'` | **runs here** |
| sd ext2 `bcm2711-emmc -r ...:ext2` | `== 'sd'` | `== 'sd'` (unchanged) | not run |
| mkdir;/dev;/nfstest | (none) | `!= 'nfsroot'` | skipped (nfsroot did its own mkdir /dev) |
| bind;devfs;/dev | (none) | `!= 'nfsroot'` | skipped (done in block) |
| posixsrv | (none) | `!= 'nfsroot'` | skipped (done in block) |
| bcm2711-emmc (probe) | `!= 'sd'` | `!= 'sd'` (unchanged) | **runs** for nfsroot — card-out-safe probe; harmless. Left untouched to avoid widening the gate. |
| rpi4-thermal | (none) | (none) | runs (after "/" — renders after the block) |
| rpi4-hwrng | (none) | (none) | runs |
| rpi4-fb / rpi4-gpio | `!= 'sd'` | unchanged | run for nfsroot |
| usb | (none) | (none) | runs |
| lwip;genet (normal pos) | (none) | `!= 'nfsroot'` | skipped (launched early) |
| nfs-smoke | (none) | `!= 'nfsroot'` | skipped (root IS the export) |
| nfs;/nfstest;...;v4 | (none) | `!= 'nfsroot'` | skipped (root IS the export) |
| psh | (none) | (none) | runs |

Because mkdir/bind/posixsrv at their normal positions are dropped for nfsroot, the
device-driver lines (thermal/hwrng/fb/gpio/usb) were **not moved** — they
automatically render after the nfsroot block's posixsrv, giving the correct
post-root order with zero extra edits.

### Rendered nfsroot order (verified from the built `user.plo`)

```
dummyfs;-N;devfs;-D -> pl011-tty -> lwip;genet -> nfs;/;10.42.0.1;/;v4;root
-> mkdir;/dev -> bind;devfs;/dev -> posixsrv -> bcm2711-emmc (probe)
-> rpi4-thermal -> rpi4-hwrng -> rpi4-fb -> rpi4-gpio -> usb -> psh -> go!
```

No `dummyfs-root`, no `nfs-smoke`, no `nfs;/nfstest`, no `/nfstest` in any mkdir.

## Build plumbing

`scripts/rebuild-rpi4b-fast.sh` `--variant` now accepts `netboot|sd|nfsroot`
(extended the case + error message + the variant comment). nfsroot only changes
the rendered `user.plo` (set via `RPI4B_VARIANT=nfsroot`); the image staging is
variant-agnostic (only `build-rpi4b-rootfs-ext2.sh` assumes an ext2 partition, and
that is sd-only). nfsroot is **netboot-delivered** like the netboot variant — no
ext2 partition, no SD card. `libnfs.a` was already staged in `.buildroot` from
prior NFS builds; if a fresh tree fails on `-lnfs`, run one `--with-ports` first.

## Exact build commands (host-only, NO HW)

```
# 1. Regression image (proves Change 1 didn't break the literal-path socket flow)
./scripts/rebuild-rpi4b-fast.sh --scope core --variant netboot --build-only

# 2. Feature image
./scripts/rebuild-rpi4b-fast.sh --scope core --variant nfsroot --build-only
```

Both built clean. The authoritative verification is the rendered
`.buildroot/_build/aarch64a72-generic-rpi4b/plo-scripts/user.plo` and the plo
program-image table (not `grep -a` on `loader.disk` — the embedded user.plo block
has slack bytes from the larger netboot render that can show stale strings).

## Orchestrator HW test plan + markers

Regression (netboot):
- `./scripts/test-cycle-netboot.sh --label nfsroot-regress-netboot --capture-secs 240` (Bash timeout >= 320000)
- Expect unchanged behaviour: genet link + DHCP/IP, **nfs-smoke READ ok**
  (`nfs-smoke: ... read ...` success), the `/nfstest` splice mounts, boot reaches
  `(psh)%`, 0 faults. This proves the libphoenix socksrvcall fallback is inert when
  "/" exists.

Feature (nfsroot) — requires the NFS server on the host exporting the rootfs at
`10.42.0.1:/`:
- `./scripts/test-cycle-netboot.sh --label nfsroot-feature --capture-secs 240` (Bash timeout >= 320000)
- Success markers (in order):
  - `nfs-fs: root mode: mounting 10.42.0.1:/ as / (bounded retry, 60s deadline)`
  - `nfs-fs: root mode: mounted 10.42.0.1:/ via v4 after N retr*`
  - **`nfs-fs: registered / (root mode)`**  <- headline success line
  - then `/`-dependent services: bind devfs /dev, posixsrv, drivers, and boot
    reaches `(psh)%`.
- Failure markers to watch:
  - `nfs-fs: FATAL root mount failed after 60s, / not registered` — NFS unreachable
    within the deadline (network/DHCP/host-export problem; the §4 fallback that would
    make this non-fatal is deliberately NOT implemented yet, so "/" stays unregistered
    and the boot stalls — expected, not a code bug).
  - `nfs-fs: FATAL portRegister(/) failed` — something else already owns "/".

## Runtime watch-item for the orchestrator (NOT a build blocker)

`mkdir /dev` in the nfsroot block now runs against the **NFS export**, which may
already contain a `/dev` directory -> mkdir could return `-EEXIST`. If `mkdir`
aborts the boot on EEXIST, you would see "/" register (the success line above) but
then no `/dev` bind / no psh. If that happens, the host export's rootfs should omit
a pre-existing `/dev`, or mkdir needs an idempotent (-EEXIST-tolerant) path. Flagged
because it is the most likely first-boot surprise of the new ordering.
