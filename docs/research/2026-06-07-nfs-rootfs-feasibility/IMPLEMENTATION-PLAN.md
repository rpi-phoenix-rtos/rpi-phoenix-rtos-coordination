# NFS client + NFS-rootfs for Phoenix-RTOS — Implementation Plan (runbook)

**Date:** 2026-06-07
**Status:** PLAN ONLY — no source changes, no builds, no HW cycles authorised by this doc.
**Scope:** Port libnfs to Phoenix-RTOS, build a generic userspace NFS fs-server speaking
the `mt*` VFS protocol, prove it on HW (Pi 4) and on QEMU (other arches), then optionally
serve `/` over NFS. Read this top-to-bottom; it is a checklist-driven engineering runbook.

**Builds on** the feasibility study in this directory (`README.md` + `01..06`). Where that
study nailed a fact with a file:line anchor, this doc cites it and goes deeper into the
implementation. **Do not re-derive** the kernel exec/mmap-from-fs path — it is cited as a
risk, not re-investigated.

---

## Table of contents

1. [Conventions, crosswalk, and genericity ground rules](#1-conventions-crosswalk-and-genericity-ground-rules)
2. [Architecture overview](#2-architecture-overview)
3. [Stage T0 — Cross-compile libnfs for the Phoenix target](#3-stage-t0--cross-compile-libnfs-for-the-phoenix-target)
4. [Stage T1 — HW socket/RPC smoke test](#4-stage-t1--hw-socketrpc-smoke-test) (incl. §4.8 `nfscli` psh CLI)
5. [Stage T2 — Wrap libnfs in an `mt*` userspace fs-server](#5-stage-t2--wrap-libnfs-in-an-mt-userspace-fs-server)
6. [Stage T3b — Mount at `/nfstest`: read/write/exec acceptance](#6-stage-t3b--mount-at-nfstest-readwriteexec-acceptance)
7. [Stage T3 — NFS as the actual rootfs (`/`)](#7-stage-t3--nfs-as-the-actual-rootfs-)
8. [libnfs multithreading — Phoenix-native port](#8-libnfs-multithreading--phoenix-native-port)
9. [Cross-arch QEMU validation matrix](#9-cross-arch-qemu-validation-matrix)
10. [Consolidated risk register](#10-consolidated-risk-register)
11. [Rollback / abort procedure per stage](#11-rollback--abort-procedure-per-stage)
12. [Open questions / decisions needed before T3](#12-open-questions--decisions-needed-before-t3)
13. [Host preparation — Linux NFS server (prerequisite for T1+)](#13-host-preparation--linux-nfs-server-prerequisite-for-t1)
14. [Testing strategy — scenarios per stage, in order](#14-testing-strategy--scenarios-per-stage-in-order)

> **Read order for the implementer:** §13 (host prep) is a **prerequisite gate before T1**
> — do it right after T0. §14 consolidates every test scenario by stage in execution order;
> it cross-references the per-stage acceptance checklists (§3.7/§4.7/§5.8/§6.5/§7.4/§8.8/§9.5).

---

## 1. Conventions, crosswalk, and genericity ground rules

### 1.1 Stage label crosswalk (this doc ↔ feasibility study)

The user's staging differs from the study's `T0..T4`. **This doc uses the user's labels.**

| This doc | Feasibility study | Meaning |
|----------|-------------------|---------|
| T0 | T0 | libnfs cross-compiles for the target |
| T1 | T1 | userspace smoke app: mount + read one host file |
| T2 | T2 | `mt*` fs-server wrapping libnfs |
| **T3b** | T3 / "option (b)" | server registers at **`/nfstest`** (NOT root); read/write/exec acceptance |
| **T3** | T4 / "option (c)" | NFS server owns **`/`** (the invasive boot-order change) |

### 1.2 Arch-neutral vs platform-specific (genericity contract)

**Arch-neutral (the whole NFS stack):** the libnfs port, the `mt*` fs-server, the
DHCP-wait, the host export, and the socket path (`/dev/netsocket`, study note 02). These
are shared across every Phoenix target that runs lwIP + the socketsrv.

**Platform-specific (exactly two seams):**
1. **Build triple** — `aarch64-phoenix` for Pi 4 (study note 05, line 32); `i386-pc-phoenix`
   for ia32; `riscv64-phoenix`, etc. Passed as `${HOST}` by the ports system.
2. **NIC bring-up + launch syntax** — Pi 4: `lwip;genet:...` via plo `-x`
   (`user.plo.yaml:77`). ia32-qemu: `X /sbin/lwip rtl:0x18:11` via `rc.psh`
   (`_projects/ia32-generic-qemu/rootfs-overlay/etc/rc.psh:7`). The NFS server binary and
   its arguments are identical across both — only the *launcher line* differs.

**Rule for every file this plan creates:** no `#ifdef __aarch64__`, no GENET/BCM constants,
no Pi-only addresses in the fs-server or the libnfs port. Platform conditioning lives only
in the per-project `*.plo.yaml` / `rc.psh` launcher lines and the port's `${HOST}`.

### 1.3 Shell discipline (for the implementer, when this becomes active work)

Per `CLAUDE.md`: use `./scripts/git-siblings.sh` for cross-repo git reads, the Read tool for
file slices, `grep -r`/`rg` for searches. After any committed *core* change rebuild with
`--scope core`. The fs-server lives in `phoenix-rtos-filesystems/` which is a **core** repo
→ the stale-core hazard applies.

---

## 2. Architecture overview

```
   Linux dev host                         Phoenix-RTOS target (Pi4 / QEMU)
 ┌─────────────────┐                    ┌──────────────────────────────────────┐
 │ nfs-kernel-srv  │   TCP :2049 (v4)   │  nfs-srv  (new process)                │
 │ /srv/...-nfs    │◀──────────────────▶│   ├─ libnfs.a (sync or MT API)         │
 │ exports w/      │   NFSv4.1, no      │   ├─ id↔path/nfsfh idtree              │
 │ no_root_squash, │   portmap/mountd   │   └─ msgRecv loop on its own port      │
 │ insecure, fsid=0│                    │         registered at /nfstest or "/"  │
 └─────────────────┘                    │              ▲ mt* (mtRead/mtWrite/…)  │
        ▲                               │   socket fd ──┼──────────────┐         │
        │ DHCP+TFTP (dnsmasq)           │   via /dev/netsocket          │         │
        │ 10.42.0.1/24                  │  lwip;genet  ◀────────────────┘         │
        └───────────────────────────────  (socketsrv = lwIP process)             │
                                        └──────────────────────────────────────┘
```

- The NFS server is **dummyfs-shaped** (study note 04): a standalone process, NOT a
  `libstorage` block-device consumer. Its "backing store" is a TCP socket to the host.
- libnfs's socket goes through Phoenix's cross-process BSD socket surface
  (`/dev/netsocket`, study note 02): any process can open sockets, so the NFS server does
  not have to be the lwIP process.
- Protocol = **NFSv4.1** (single port 2049, no portmapper/mountd round-trips). v3-over-TCP
  is the fallback. Rationale: study `README.md:74-84`, note 02 §"TCP vs UDP, v3 vs v4".

### 2.1 Deliverable components (all link the one `libnfs.a` from T0)

| Component | Path | Stage | Role |
|-----------|------|-------|------|
| libnfs port | `phoenix-rtos-ports/libnfs/` | T0 | the cross-built `libnfs.a` + headers |
| `nfs-smoke` | `phoenix-rtos-utils/nfs-smoke/` | T1 | fixed boot-line feasibility probe (mount+read+write one file) |
| `nfscli` | `phoenix-rtos-utils/nfscli/` | T1 (§4.8) | **interactive psh CLI** for ad-hoc NFS triage (ping/ls/cat/stat/put/mkdir/rm) — direct libnfs, no VFS mount |
| `nfs` (fs-server) | `phoenix-rtos-filesystems/nfs/` | T2+ | the `mt*` VFS server; mounts at `/nfstest` (T3b) or `/` (T3) |
| MT backend | libnfs `patches/` (§8) | before T3 | `HAVE_PHOENIX_THREADS` native shim |
| host helpers | `scripts/nfs-export-up.sh`, `scripts/nfs-stage.sh` | §13 | manage the host export + staging |

---

## 3. Stage T0 — Cross-compile libnfs for the Phoenix target

**Proves:** the OS seam ports; a static `libnfs.a` links with the target toolchain.
**Effort:** S. **Deps:** Phoenix toolchain (present at
`.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc`, GCC 14.2.0). **Validation: host-only**
— not netboot-testable.

### 3.1 Files to create

| Path | Purpose |
|------|---------|
| `sources/phoenix-rtos-ports/libnfs/port.def.sh` | the port recipe (model on `curl/port.def.sh`) |
| `sources/phoenix-rtos-ports/libnfs/files/config.h` | static `config.h` (fallback if cross-`configure` misdetects; see 3.4) |
| `sources/phoenix-rtos-ports/libnfs/patches/*.patch` | OS-seam patches (expected: 0–2; see 3.5) |

### 3.2 `port.def.sh` skeleton (model: `phoenix-rtos-ports/curl/port.def.sh`)

```sh
#!/usr/bin/env bash
:
#shellcheck disable=2034
{
    ports_api=1
    name="libnfs"
    version="6.0.2"                       # pin to a release TARBALL (ships ./configure)
    desc="Client library for accessing NFS shares over the network"
    source="https://github.com/sahlberg/libnfs/releases/download/libnfs-${version}/"
    archive_filename="${name}-${version}.tar.gz"
    src_path="${name}-${version}/"
    size="<fill from tarball>"
    sha256="<fill from tarball>"
    license="LGPL-2.1"                    # study note 05 §License
    license_file="COPYING"
    iuse=""
    depends=""
    conflicts=""
    supports="phoenix>=3.3"
}

p_common() {
    export PREFIX_NFS_INSTALL="$PREFIX_PORT_BUILD/install"
}

p_prepare() {
    b_port_apply_patches "${PREFIX_PORT_WORKDIR}"
    CONFIGURE_PARAMS=(--host="${HOST}"
        --prefix="$PREFIX_NFS_INSTALL"
        --disable-shared --enable-static
        --disable-pthread                 # T0/T1/T2: sync API only (flip in T2-MT, §8)
        --disable-werror
        --without-libkrb5
        --disable-examples --disable-utils --disable-tests)
    if [ ! -f "$PREFIX_PORT_WORKDIR/config.status" ]; then
        (cd "$PREFIX_PORT_WORKDIR" && "$PREFIX_PORT_WORKDIR/configure" \
            CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" "${CONFIGURE_PARAMS[@]}")
    fi
}

p_build() {
    make -C "$PREFIX_PORT_WORKDIR"
    make -C "$PREFIX_PORT_WORKDIR" install
    # stage headers + static lib into the Phoenix sysroot staging dirs (curl pattern)
    cp -a "$PREFIX_NFS_INSTALL/include/nfsc" "$PREFIX_H"
    cp -a "$PREFIX_NFS_INSTALL/lib/libnfs.a" "$PREFIX_A"
}
```

Notes:
- **Use the release tarball, not the git clone.** The git tree ships no pre-generated
  `configure` and the dev host lacks autotools (`research/libnfs-EXPERIMENTS.md:9-12`). The
  release tarball ships `configure`, exactly like curl/openssl ports.
- `--without-libkrb5`, `--disable-examples/utils/tests` keep the GPL-3 ancillary programs
  out of the link (study note 05 §License): only LGPL-2.1 `libnfs.a` is shipped.
- The fs-server is **not** a port — it is Phoenix code (§5) that links `libnfs.a`.

### 3.3 libnfs subset that must compile/link (study note 01)

Must build: `lib/` (core: `libnfs.c`, `libnfs-sync.c`, `socket.c`, `pdu.c`, `init.c`,
`nfs_v3.c`, `nfs_v4.c`, `libnfs-zdr.c`), `nfs/` (v3 raw, pre-generated), `nfs4/` (v4 raw,
pre-generated), `mount/` + `portmap/` (v3 fallback path), `include/`. **Excluded:** `nlm/`,
`nsm/`, `rquota/` (locking/quota — not needed), `win32/aros/ps2/ps3` (other-OS seams),
`examples/utils/tests`. RPC/XDR is self-contained — no rpcgen, no libtirpc (study note 05
§RPC/XDR; the `.x` IDL is present but pre-generated `libnfs-raw-*.{c,h}` are committed).

### 3.4 `config.h` contents (if cross-`configure` misdetects)

The study already proved the only quirks hit are `HAVE_SOCKADDR_STORAGE` and the
`arpa/inet.h` `htonl` path (`libnfs-EXPERIMENTS.md:14-23`; study note 05 §Compile-test). The
POSIX header set Phoenix ships satisfies the rest. Minimal static `config.h` if needed:

```c
#define HAVE_SOCKADDR_STORAGE 1     /* Phoenix sys/socket.h:33 already defines it; libnfs
                                       guards its own def with !HAVE_SOCKADDR_STORAGE
                                       (include/libnfs-private.h:105) → set to avoid redef */
#define HAVE_ARPA_INET_H      1     /* gives htonl→htobe32 / ntohl→be32toh */
#define HAVE_NETINET_IN_H     1
#define HAVE_NETINET_TCP_H    1
#define HAVE_POLL_H           1
#define HAVE_SYS_SOCKET_H     1
#define HAVE_SYS_UIO_H        1
#define HAVE_SYS_TIME_H       1
#define HAVE_UNISTD_H         1
#define HAVE_FCNTL_H          1
#define HAVE_STRUCT_SOCKADDR_IN6 1  /* only if --enable-ipv6; default off → omit */
/* DO NOT set HAVE_SYS_SYSMACROS_H, HAVE_TALLOC_TEVENT, HAVE_LIBKRB5 */
/* DO NOT set HAVE_FIONBIO/sys/filio.h → libnfs falls back to fcntl(O_NONBLOCK)
   (study note 01: FIONBIO absent → fcntl path used, which Phoenix supports) */
```

**Preference:** let cross-`configure` generate `config.h` first; only ship the static one if
the autoconf feature-probe binaries can't *run* under the cross host (they can't be executed
— they're target binaries). In practice cross-configure caches/guesses many `HAVE_*` from
the triple; supply a `config.cache` or the static `config.h` for the handful it gets wrong.
This is the single most likely T0 friction point — budget for it.

### 3.5 Anticipated OS-seam patches (expect 0–2 one-liners)

1. **`writev` chattiness (optional, defer):** Phoenix `writev` is a userspace loop over
   `write()` that stops on a short write (study note 02 §writev nuance, `uio.c:95-96`).
   libnfs handles partial writes correctly (`rpc_write_to_socket` advances `out.pos`), so
   this is *correct, just chattier*. **No patch for T0–T3b.** If profiling at T3 shows it
   matters, a one-line libnfs patch coalesces the iov into one `send()`. Track as a perf TODO.
2. **Reserved-port bind:** libnfs prefers a <1024 source port (`rpc_bind_reserved`,
   socket.c ~1480/1539). Phoenix lwIP has no privileged-port check (study note 02 §reserved
   port). **No patch needed** — mitigate host-side with `insecure` export (§3.6). If lwIP
   ever rejects it, set libnfs's "don't use reserved port" knob (a runtime call, not a patch).

### 3.6 Host setup (do once, before T1)

Per study note 03. Add a managed helper `scripts/nfs-export-up.sh` (mirrors how
`scripts/netboot-server.sh` manages dnsmasq — **do not hand-edit `/etc/exports`**):

```sh
sudo apt-get install -y nfs-kernel-server
sudo mkdir -p /srv/phoenix-rpi4-nfs
# /etc/exports line the helper writes:
/srv/phoenix-rpi4-nfs  10.42.0.0/24(rw,sync,no_subtree_check,no_root_squash,insecure,fsid=0)
sudo exportfs -ra
sudo systemctl enable --now nfs-kernel-server
showmount -e 10.42.0.1     # v3 sanity
```
Option rationale: study note 03 table (`rw`, `no_root_squash` because Phoenix runs uid 0,
`insecure` for >1023 source ports, `fsid=0` makes this the **NFSv4 pseudo-root** so the v4
client mounts `host:/`).

### 3.7 T0 acceptance checklist

- [ ] `port.def.sh` builds without error against the target toolchain.
- [ ] `${CROSS}-nm libnfs.a | grep -E 'nfs_mount|nfs_open|nfs_pread|nfs_pwrite|nfs_stat64|nfs_opendir'`
      shows all symbols **defined** (`T`), not undefined (`U`).
- [ ] `${CROSS}-ar t libnfs.a` lists `nfs_v4.o`, `libnfs-raw-nfs4.o` (v4 path present).
- [ ] No reference to `pthread_*` symbols (sync build) — `${CROSS}-nm libnfs.a | grep pthread`
      is empty. (Changes in §8.)
- [ ] A trivial host link `int main(){nfs_init_context();return 0;}` links clean.

---

## 4. Stage T1 — HW socket/RPC smoke test

**Proves:** sockets + RPC + XDR work end-to-end over the real NIC. Resolves OQ-1 (poll
timeout fidelity), OQ-2 (reserved-port bind), OQ-3 (payload caps), OQ-4 (DHCP-wait).
**Effort:** M. **Deps:** T0; host export up (§3.6); lwIP/NIC up (Pi4 GENET working, ping
RTT ~0.9 ms per project memory). **Validation:** netboot cycle; UART shows file bytes.

### 4.1 File to create

`sources/phoenix-rtos-utils/nfs-smoke/` — a ~150-line standalone program (a util, not a
port). Add to `phoenix-rtos-utils` build like any other util, link `-lnfs`. It is
**arch-neutral**; only the launcher line differs per platform.

### 4.2 Program structure (exact API sequence)

```c
/* nfs-smoke.c — T1 feasibility gate. Arch-neutral. */
#include <nfsc/libnfs.h>
/* args: <server-ip> <export-path> <file-to-read>  e.g. 10.42.0.1 / /etc/hostname  */

int main(int argc, char **argv)
{
    /* STEP 1: wait for a DHCP lease before any socket op (OQ-4, §4.3). */
    char ipbuf[64];
    if (wait_for_dhcp_lease(ipbuf, sizeof ipbuf, /*timeout_ms*/ 30000) != 0) {
        fprintf(stderr, "nfs-smoke: no DHCP lease in 30s, aborting\n");
        return 2;
    }
    printf("nfs-smoke: interface bound, ip=%s\n", ipbuf);

    /* STEP 2: context + v4 + mount (bounded). */
    struct nfs_context *nfs = nfs_init_context();
    if (nfs == NULL) { fprintf(stderr, "init_context failed\n"); return 3; }
    nfs_set_version(nfs, NFS_V4);                 /* libnfs.h:361 — recommend v4 */
    nfs_set_timeout(nfs, 5000);                   /* per-RPC timeout, ms (see §5.5) */
    /* Optionally tune rsize/wsize below the kernel msg payload cap (OQ-3, §4.4) */
    nfs_set_readmax(nfs,  32 * 1024);
    nfs_set_writemax(nfs, 32 * 1024);
    if (nfs_mount(nfs, argv[1], argv[2]) != 0) {  /* v4: export = "/" (fsid=0) */
        fprintf(stderr, "mount failed: %s\n", nfs_get_error(nfs));
        /* FALLBACK: retry v3-over-TCP (see §4.5) */
        return 4;
    }
    printf("nfs-smoke: mounted %s:%s\n", argv[1], argv[2]);

    /* STEP 3: stat + open + pread one file, print bytes + timing. */
    struct nfs_stat_64 st;
    if (nfs_stat64(nfs, argv[3], &st) != 0) { ...; return 5; }
    struct nfsfh *fh;
    if (nfs_open(nfs, argv[3], O_RDONLY, &fh) != 0) { ...; return 6; }
    char buf[512];
    uint64_t t0 = now_ms();
    int n = nfs_pread(nfs, fh, buf, sizeof buf, 0);   /* libnfs.h:740 */
    uint64_t t1 = now_ms();
    if (n < 0) { ...; return 7; }
    printf("nfs-smoke: read %d bytes in %llu ms:\n%.*s\n", n, t1 - t0, n, buf);

    /* STEP 4 (write half of OQ): write a marker file, read it back, compare. */
    struct nfsfh *wfh;
    if (nfs_creat(nfs, "/nfs-smoke-marker.txt", 0644, &wfh) == 0) {
        const char *m = "phoenix-nfs-smoke-OK\n";
        nfs_pwrite(nfs, wfh, m, strlen(m), 0);
        nfs_close(nfs, wfh);
        printf("nfs-smoke: wrote marker (verify on host: cat /srv/.../nfs-smoke-marker.txt)\n");
    }
    nfs_close(nfs, fh);
    nfs_destroy_context(nfs);
    return 0;
}
```

### 4.3 DHCP-wait helper (OQ-4 — the first component in the tree that must wait)

**Nothing in the tree waits for DHCP today** (study `README.md:63-72`, note 02 OQ-4). The
mechanism: read `/dev/ifstatus` (registered by the lwIP socketsrv,
`phoenix-rtos-lwip/port/devs.c:606`), which prints per-interface lines including
`<ifname>_up=1`, `<ifname>_dhcp=1`, `<ifname>_ip=<addr>` (`devs.c:230-245`).

```c
/* Arch- and interface-name-agnostic: scan ALL non-lo interfaces for a bound IPv4.
   Pi4 is en0, ia32-qemu is en1 (rc.conf.d/network:5) — DO NOT hardcode the name. */
static int wait_for_dhcp_lease(char *ip_out, size_t cap, int timeout_ms)
{
    int waited = 0;
    for (;;) {
        FILE *f = fopen("/dev/ifstatus", "r");        /* path requires /dev bind (see note) */
        if (f != NULL) {
            char line[128], cur_if[16] = "", cur_ip[64] = "";
            int up = 0;
            while (fgets(line, sizeof line, f)) {
                /* parse "<if>_up=1", "<if>_ip=A.B.C.D"; skip lo*; accept first
                   interface whose _up=1 AND _ip != 0.0.0.0 (and, if present, _dhcp=1) */
                ...
                if (up && valid_ipv4(cur_ip) && strncmp(cur_if, "lo", 2) != 0) {
                    strncpy(ip_out, cur_ip, cap); fclose(f); return 0;
                }
            }
            fclose(f);
        }
        if (waited >= timeout_ms) return -1;
        usleep(250000); waited += 250;                /* poll every 250 ms */
    }
}
```

**Genericity note:** scanning for *any* non-`lo` bound interface (not a fixed `en0`) is what
makes the DHCP-wait portable across Pi4/ia32/etc. **T3 caveat:** `/dev/ifstatus` is reachable
only after the `/dev` bind, which does not exist before `/` — so the **root** case needs a
different lease check (open the socket port directly, or rely on a bounded mount retry).
Flagged as **OQ-A** (§12).

### 4.4 OQ-3 — single-message payload cap (rsize/wsize tuning)

Phoenix `mtRead`/`mtWrite` carry data via `msg.o.data`/`msg.i.data` (a separate buffer, not
the 64-byte `raw` union — see `msg.h:117-147`), so the practical cap is the
socketsrv/transport limit, not 64 bytes. Determine the effective single-`recv`/`send`
payload empirically in T1 (read a large file, watch for truncation). Set libnfs
`rsize`/`wsize` at or below it (`nfs_set_readmax`/`nfs_set_writemax`); libnfs chunks larger
reads automatically. Safe default: **32 KB** (well under typical caps, fine for 4 KB exec
pages). This is **throughput tuning, not feasibility** (study note 02 OQ-3).

### 4.5 v3 fallback (decisive gate)

If v4 misbehaves (libnfs's v4 path is newer/less battle-tested — study `README.md:80-84`),
fall back here **before** building T2 on top of it:
```c
nfs_set_version(nfs, NFS_V3);
nfs_mount(nfs, argv[1], "/srv/phoenix-rpi4-nfs");  /* v3: real export path; libnfs does
                                                      portmap(111)→mountd→nfs(2049) over TCP */
```
T2/T3 are protocol-agnostic (they call the same sync API). Record which protocol passed T1.

### 4.6 Launcher (platform-specific)

- **Pi4 (plo `-x`):** add **after** the `lwip;genet` line in
  `_projects/aarch64a72-generic-rpi4b/user.plo.yaml:77`:
  `- app {{ env.BOOT_DEVICE }} -x nfs-smoke;10.42.0.1;/;/etc/hostname ddr ddr`
- **ia32-qemu (`rc.psh`):** add after `X /sbin/lwip ...` in
  `_projects/ia32-generic-qemu/rootfs-overlay/etc/rc.psh:7`:
  `X /usr/bin/nfs-smoke 10.0.2.2 / /etc/hostname` (host IP differs per QEMU netdev, §9).

### 4.7 T1 acceptance checklist

- [ ] UART shows `interface bound, ip=10.42.0.x` (DHCP-wait works).
- [ ] UART prints the host file's exact bytes (RPC + XDR + read path works).
- [ ] Host shows `nfs-smoke-marker.txt` with the expected content (write path works).
- [ ] Round-trip `pread` time is logged and sane (< a few ms on the lab link) — OQ-1.
- [ ] Records which protocol (v4 preferred, v3 fallback) passed → carried into T2/T3.

### 4.8 `nfscli` — a Phoenix-native NFS connectivity CLI (psh-invocable)

> Requested by the user: a native Phoenix-RTOS command-line tool to test NFS connectivity —
> "either a command line to mount NFS, or a test client callable from psh for basic
> connectivity testing." **Verdict: very doable, low effort, high value as a triage tool.**

`nfs-smoke` (§4.1) is a fixed, fire-and-forget boot-line program. `nfscli` is the
**interactive, argument-driven** companion: a single binary you run from the **psh prompt**
to do ad-hoc NFS operations against an arbitrary server — the NFS analogue of the project's
existing `diag-udp` triage tools. It does **not** register a VFS mount; it talks libnfs
directly and prints to stdout, so it works **before T2 exists** and stays useful afterward
for debugging.

**Placement:** `sources/phoenix-rtos-utils/nfscli/` (a util, links `-lnfs`). Arch-neutral;
installed to `/bin/nfscli` so psh can exec it. Build it right after `nfs-smoke` (same T1
dependency: just `libnfs.a`). It is the model libnfs ships as `utils/nfs-ls.c` /
`nfs-cp.c` — but we write a small self-contained multi-command one rather than porting the
GPL-licensed `utils/` (license hygiene, study note 05).

**Invocation (subcommand style), each opens its own context + does the DHCP-wait:**
```
nfscli ping   <server>                         # mount + immediate unmount: prove reachability
nfscli ls     <server> <export> <path> [v3|v4] # nfs_opendir/readdir → print names + sizes
nfscli cat    <server> <export> <path>         # nfs_open + loop nfs_pread → stdout
nfscli stat   <server> <export> <path>         # nfs_stat64 → print size/mode/mtime/type
nfscli put    <server> <export> <path>         # read stdin → nfs_creat + nfs_pwrite
nfscli mkdir  <server> <export> <path>         # nfs_mkdir2
nfscli rm     <server> <export> <path>         # nfs_unlink / nfs_rmdir
```
Example from psh: `nfscli ls 10.42.0.1 / /bin v4` → lists the host export's `/bin`.

**Skeleton (one libnfs context per invocation; reuses the §4.3 DHCP-wait + §4.5 protocol):**
```c
int main(int argc, char **argv) {
    /* argv[1]=subcmd, argv[2]=server, argv[3]=export, argv[4]=path, optional argv[5]=ver */
    char ip[64];
    if (wait_for_dhcp_lease(ip, sizeof ip, 15000) != 0) { fprintf(stderr,"no lease\n"); return 2; }
    struct nfs_context *nfs = nfs_init_context();
    nfs_set_version(nfs, parse_ver(argv[5]));          /* default NFS_V4 */
    nfs_set_timeout(nfs, 5000);
    if (nfs_mount(nfs, argv[2], argv[3]) != 0) { fprintf(stderr,"%s\n",nfs_get_error(nfs)); return 4; }
    int rc = dispatch_subcommand(nfs, argv[1], argv[4], /*stdin for put*/);
    nfs_destroy_context(nfs);
    return rc;
}
```
- `ls` → `nfs_opendir`(1302)/`nfs_readdir`(1338)/`nfs_closedir`(1379); print `nfsdirent`
  name + size + type.
- `cat` → `nfs_open`(675) + `nfs_pread`(740) loop to EOF → `write(1, ...)`.
- `stat` → `nfs_stat64`(520) → print fields.
- `put` → `nfs_creat`(1208) + `nfs_pwrite`(862) from stdin.
- `mkdir`/`rm` → `nfs_mkdir2`(1148)/`nfs_unlink`(1268)/`nfs_rmdir`(1177).

**Stack:** exec'd as a normal psh program; if its primary stack is the 8 KB default, run the
libnfs work on an explicitly-sized ≥64 KB thread (§5.6) — same rule as the server.

**Why it earns its place:**
- **T1 triage:** `nfscli ping`/`ls`/`cat` reproduce the T1 gate interactively without a
  reboot per change — far faster iteration than editing the boot line.
- **T2/T3b triage:** when `ls /nfstest` (via the VFS server) misbehaves, `nfscli ls`
  (direct libnfs, bypassing the `mt*` layer) **isolates whether the bug is in libnfs/network
  or in the `mt*` mapping** — the single most useful split when debugging the server.
- **T3 triage:** `nfscli ping <server>` from a booted (non-NFS-root) image confirms the host
  export is reachable before flipping `RPI4B_ROOT=nfs`.

**Acceptance:** `nfscli ping`/`ls`/`cat`/`stat`/`put` each succeed from the psh prompt
against the host export, output matching the host (`ls` names, `cat` bytes, `put` round-trip
host-verified). Add these as scenarios T1.10–T1.14 in §14.3.

---

## 5. Stage T2 — Wrap libnfs in an `mt*` userspace fs-server

**Proves:** libnfs is usable as a Phoenix userspace filesystem speaking the VFS protocol.
**Effort:** M–L. **Deps:** T1 green. **Validation:** register at a test mountpoint, `cat`/`ls`
host files via psh, byte-compare against the host tree.

### 5.1 Files to create (`sources/phoenix-rtos-filesystems/nfs/`)

| File | Contents |
|------|----------|
| `srv.c` | `main()`: arg parse, DHCP-wait, `nfs_mount`, `portCreate`/`portRegister`, the `msgRecv` dispatch loop (model: `dummyfs/srv.c:278-357`) |
| `nfs_node.c` / `nfs_node.h` | the `id ↔ path / struct nfsfh*` bidirectional map (idtree, model: dummyfs `object.c`) |
| `nfs_ops.c` / `nfs_ops.h` | the `mt*`→libnfs handlers |
| `Makefile` | builds a `libnfsfs` static lib + an `nfs` binary, links `libnfs.a` (model: `dummyfs/Makefile`) |

`Makefile` (model `dummyfs/Makefile`):
```make
LOCAL_PATH := $(call my-dir)
NAME := libnfsfs
SRCS := $(filter-out $(LOCAL_PATH)srv.c, $(wildcard $(LOCAL_PATH)*.c))
include $(static-lib.mk)

NAME := nfs
SRCS := $(LOCAL_PATH)srv.c
LIBS := libnfsfs
LDLIBS := -lnfs          # the libnfs.a staged by the T0 port
include $(binary.mk)
```
Add `nfs` to the target's `DEFAULT_COMPONENTS` (via `_targets/Makefile.*`) only for targets
that have networking; do **not** add it unconditionally (a no-network target would ship a
dead binary). Guard behind the project's lwIP presence.

### 5.2 The node table (oid scheme) — go deeper than the study

Phoenix is **handle-based** (`oid_t {port, id}`); NFS sync API is **path-based**. The server
keeps a **bidirectional** map so a repeated `mtLookup` of the same path returns the **same**
id (else you leak ids and break inode identity):

```c
typedef struct nfs_node {
    rbnode_t   linkage;        /* keyed by id, for id→node */
    rbnode_t   path_linkage;   /* keyed by hashed absolute path, for path→node */
    id_t       id;             /* the oid.id we hand back to the VFS */
    char      *path;           /* canonical absolute path within the export */
    struct nfsfh *fh;          /* non-NULL while open (mtOpen..mtClose); else NULL */
    int        type;           /* otDir/otFile/otSymlink/otDev — cached from nfs_LSTAT64
                                  (don't-follow), so symlinks keep type otSymlink */
    unsigned   refs;           /* open-count; drop node when 0 AND unlinked */
} nfs_node_t;
```

- **Root id convention:** id `0` = the export root (mirrors dummyfs `DUMMYFS_ROOTID`,
  `dummyfs.c:59`). `portRegister(..., &root)` passes `root = {port, 0}` (dummyfs `srv.c:223`).
- **Id allocation:** monotone counter (or idtree allocator). On `mtLookup`, hash the resolved
  absolute path; if present, reuse its id; else allocate. On `mtDestroy`/last-close-after-
  unlink, free the node.
- **Path canonicalisation:** join parent path + name, collapse `.`/`..`/`//`. Keep paths
  relative to the libnfs mount root (libnfs paths are export-relative).

### 5.3 The `mt*` → libnfs mapping (every message) — extends study note 04

Dispatch loop is **dummyfs-shaped** (`dummyfs/srv.c:278-357`): `for(;;){ msgRecv; switch;
msgRespond; }`. Set `msg.o.err` to a **negative errno** (Phoenix convention) on failure.
Translate libnfs's return codes (it returns negative errno-ish on failure; `nfs_get_error`
for text).

| `mt*` (msg.h enum) | Handler in | libnfs call(s) (`libnfs.h` line) | Notes / contract |
|--------------------|-----------|----------------------------------|------------------|
| `mtLookup` | `nfs_ops_lookup` | **`nfs_lstat64`(553)** on `parent.path + "/" + name` | **Use `nfs_lstat64` (don't-follow), NOT `nfs_stat64`** — `nfs_stat64` follows symlinks (libnfs.h:546-547) so the server would never see `otSymlink` and the symlink-read branch + symlink-aware unlink would be dead code (busybox applet-symlinks would fail to exec). `nfs_lstat64` reports the link itself. **Return value = number of name chars consumed** (NOT 0). Mirror `dummyfs_lookup` (`dummyfs.c:49-118`): resolve as far as possible, set `o.lookup.fil`/`o.lookup.dev` to the child oid, return `len`. Allocate/reuse node id (§5.2). On miss return `-ENOENT`. **`..` at the NFS mount root must return the PARENT fs's oid** (mirror the mountpoint-crossing case `dummyfs.c:85-95`: `*res = *dev = fs->parent`), or `cd /nfstest/..` wedges. Store the parent oid handed to the server at mount/splice time. |
| `mtOpen` | `nfs_ops_open` | `nfs_open(path, flags, &fh)`(675) | cache `fh` in node; bump `refs`. |
| `mtClose` | `nfs_ops_close` | `nfs_close(fh)`(709) | clear `fh`; drop `refs`; if 0 and unlinked, free node. |
| `mtRead` | `nfs_ops_read` | **regular file:** `nfs_pread(fh, o.data, o.size, i.io.offs)`(740); **symlink:** `nfs_readlink(path, o.data, o.size)`(1528) | **MUST branch on node type.** There is NO `mtReadlink` enum — symlink *target* resolution is a `mtRead` on an `otSymlink` object (confirmed: `dummyfs_read` accepts `S_ISLNK`, `dummyfs.c:964`). If `node->type == otSymlink` → `nfs_readlink` (return the target string); else `nfs_pread` (open-on-demand by path if no cached `fh`). **This is load-bearing for exec-via-symlink** — e.g. busybox applet symlinks (project memory) and any real rootfs path-walk through a symlink. Return byte count in `o.err` (dummyfs returns count in `o.err`, `srv.c:293`). |
| `mtWrite` | `nfs_ops_write` | `nfs_pwrite(fh, i.data, i.size, i.io.offs)`(862) | same fh handling; return count. |
| `mtTruncate` | `nfs_ops_truncate` | `nfs_ftruncate(fh, i.io.len)`(1091) if open, else `nfs_truncate(path, len)`(1061) | |
| `mtGetAttr` | `nfs_ops_getattr` | one **`nfs_lstat64`(553)** | **don't-follow** (so `atType` reports `otSymlink` correctly). Map per `i.attr.type` (atSize/atMode/atMTime/atType/atLinks/…) exactly as `dummyfs_getattr` (`dummyfs.c:370-457`); `atPollStatus` → always-ready like dummyfs (`:439-441`). |
| `mtGetAttrAll` | `nfs_ops_getattrAll` | one **`nfs_lstat64`(553)** | don't-follow. Fill `struct _attrAll` (msg.h:55-71) like `dummyfs_getattrAll` (`dummyfs.c:460+`); init with `_phoenix_initAttrsStruct(attrs, -ENOSYS)` then set known fields. |
| `mtSetAttr` | `nfs_ops_setattr` | `nfs_chmod`(1569)/`nfs_chown`(1658)/`nfs_utimes`(1752)/`nfs_truncate`(1061) by `i.attr.type` | **`atDev` = inbound mount-splice** (a child fs mounting onto a dir *inside* the NFS tree): record the child oid on that node and forward future ops there, like dummyfs does — for T3b this is rare; return success. (The server *emits* its own outbound `atDev` to its parent at startup — see §5.7a, separate code path.) |
| `mtCreate` | `nfs_ops_create` | by `i.create.type`: `nfs_creat`(1208)/`nfs_mkdir2`(1148)/`nfs_mknod`(1237)/`nfs_symlink`(1911) | return new child oid in `o.create.oid`; allocate node. |
| `mtDestroy` | `nfs_ops_destroy` | (none) | drop node id; refcount for unlink-on-last-close. |
| `mtUnlink` | `nfs_ops_unlink` | `nfs_unlink`(1268) file/symlink / `nfs_rmdir`(1177) dir | choose by the `nfs_lstat64`-cached `type`: a **symlink-to-a-dir is `otSymlink` → `nfs_unlink`** (remove the link, not the target). Using follow-stat here would mis-route it to `nfs_rmdir`. |
| `mtLink` | `nfs_ops_link` | `nfs_link`(1962) | hardlink target oid → name under parent. |
| `mtReaddir` | `nfs_ops_readdir` | `nfs_opendir`(1302)/`nfs_readdir`(1338)/`nfs_closedir`(1379) | serialize **one** `struct dirent` per call into `o.data`, keyed by `i.readdir.offs` cookie; fill `d_ino`/`d_type`/`d_namlen`/`d_name`, return 0; `-ENOENT` past the end. Mirror `dummyfs_readdir` (`dummyfs.c:825-900`). See §5.4. |
| `mtStat` (0xf53) | `nfs_ops_statfs` | `nfs_statvfs64`(1498) | fill the statfs blob into `o.data`. |
| `mtDevCtl` | — | (none) | `-EINVAL`, same as dummyfs (`srv.c:304-306`). |

### 5.4 Readdir directory-cookie strategy

dummyfs returns one dirent per `mtReaddir` and uses `i.readdir.offs` as a resumable cookie
(`dummyfs.c:847-895`). libnfs's `nfs_readdir` walks a `struct nfsdir`. Two options:
- **(A) Snapshot:** on `offs==0`, `nfs_opendir` + drain all entries into a per-dir cached
  array; serve the entry at the cookie per call; free on end/timeout. Simple, correct,
  matches the one-dirent-per-call VFS contract. **Recommended.**
- **(B) Streaming:** keep the open `nfsdir` and a cursor keyed by offs. Lower memory, but the
  VFS may re-issue arbitrary offsets — riskier. Defer.

**Cookie semantics — match dummyfs exactly.** dummyfs's `offs` is **cumulative `d_reclen`**,
not an array index: it advances `diroffs += ei->len` per entry and resumes by walking until
`diroffs >= offs` (`dummyfs.c:863-873`, `887-895`), setting `dent->d_reclen = ei->len`. The
NFS server MUST set `d_reclen` per entry and interpret the incoming `offs` as the cumulative
byte offset (sum of prior `d_reclen`s), then map that to its snapshot index. If you instead
treat `offs` as a raw array index, a sequential `ls` of a large directory breaks mid-listing
when the kernel passes back a cumulative offset. (Compute each entry's `len` the same way
dummyfs does so the cumulative arithmetic lines up.)

### 5.5 Head-of-line blocking + per-RPC timeout (CRITICAL for exec)

The sync-API server processes **one message at a time** (dummyfs too). Each `mtRead` blocks
the whole loop inside libnfs's `poll` until the RPC completes. **Demand-paging an exec'd ELF
from NFS = one `mtRead`→`nfs_pread` per page fault**, each of which stalls every other fs op
until it returns. Consequences the implementer MUST encode:

1. **Bound every RPC:** `nfs_set_timeout(nfs, 5000)` (ms) so one dropped packet cannot wedge
   exec — and the whole server — forever. On timeout, return `-ETIMEDOUT`/`-EIO` to the
   caller rather than blocking indefinitely.
2. **This is why §8 (multithreading) exists** — the MT service thread + per-thread pool lets
   concurrent `mtRead`s overlap, removing the head-of-line stall. Single-threaded sync is
   fine for T1/T2/T3b *correctness*; MT is the *robustness/throughput* upgrade.

### 5.6 Worker-thread stack size — the #120 lesson, applied

**Hard requirement.** The SD-root saga (`docs/inprogress/2026-06-07-sd-exec-data-path.md`
§"CORRECTED ROOT CAUSE") proved the default **8 KB pool-thread stack overflows** on this
port's deep fs call chains, surfacing as mystery list-corruption Data Aborts; the fix was
`16*_PAGE_SIZE` (64 KB). The NFS call chain (msgRecv → handler → libnfs sync → XDR
marshal/unmarshal → socket message-passing → another process) is **deeper than ext2-over-SD**.

- Any thread that runs a handler (the main loop thread, or §8 worker pool threads) MUST get
  a **≥ 64 KB stack** (`16 * _PAGE_SIZE`). Set explicitly via `beginthread(..., stacksz)` or
  the pthread attr stacksize in §8.
- The main loop runs on the process's primary stack (sized by the loader); if that is the
  8 KB default, run the loop body on an explicitly-sized worker even in the single-threaded
  build. **Treat 8 KB as the prime suspect for any NFS-server corruption** (memory note).

### 5.7 mount-point registration — TWO DIFFERENT MECHANISMS (do not conflate)

Subtree-under-`/` and owning-`/` use **structurally different** Phoenix mechanisms. Getting
this wrong fails T3b at "step one" (`ls /nfstest` returns nothing) with no useful diagnostic,
so it is spelled out exactly here.

#### (a) `/nfstest` subtree under an existing `/` (T3b) — the `-m` / `atDev`-splice path

This is **NOT** the devfs `-N` named-port branch (`srv.c:229-237`). That `-N` path is a
*special namespace shortcut* for devfs (`user.plo.yaml:17-20`: create_dev strips the `/dev`
prefix, reachable before `/` exists) — it does `portRegister(port, mountpt, ...)` and is not
how a normal filesystem splices a subtree.

The correct precedent for mounting a real fs under an existing `/` is the dummyfs **`-m`**
path (the `else` branch `srv.c:238-240` → `dummyfs_mount_async` → `dummyfs_mount_sync` →
`dummyfs_do_mount`, `srv.c:59-123`). Live example in the ia32 profile:
`rc.psh:5` `W /sbin/dummyfs -m /tmp -D`. The mechanism (`dummyfs_do_mount`, `srv.c:59-85`):

```c
/* The NFS server, after portCreate + nfs_mount, splices itself into the parent fs: */
1. lookup(mountpt, &parent_oid, NULL);          /* find /nfstest in the existing "/"  */
2. stat(mountpt, &buf); assert S_ISDIR(buf);    /* it must be an existing directory   */
3. msg.type = mtSetAttr;                         /* tell the PARENT fs to splice us in */
   msg.oid  = parent_oid;
   msg.i.attr.type = atDev;
   msg.i.data = &my_root_oid;   msg.i.size = sizeof(oid_t);   /* {my_port, 0}          */
   msgSend(parent_oid.port, &msg);
```

So the NFS `srv.c` `main()` for the subtree case does: `portCreate(&port)` (NOT
`portRegister`), `nfs_mount`, then replicate `dummyfs_do_mount` to send `mtSetAttr(atDev)`
to the parent. **`/nfstest` must already exist as a directory under `/`** (`mkdir /nfstest`
first) — that's why the splice can `lookup`+`stat` it. (The dummyfs `-m` mount runs async via
a helper thread, `srv.c:117-123`, because the parent `/` may not be registered yet at launch;
the NFS server should do the same: spawn the splice on a ≥64 KB-stack thread that waits for
`/` then sends `atDev`.)

> Note: the `mtSetAttr`/`atDev` *row* in §5.3 is the **receiving** side — what an fs does
> when *it* is a parent and a child mounts onto one of its dirs. The subtree mount above is
> the NFS server acting as the **child** that *initiates* the splice. The NFS server must do
> both: handle inbound `atDev` (a child mounting under the NFS tree — rare, return success)
> and emit the outbound `atDev` to its parent at startup.

#### (b) owning `/` (T3) — `portRegister("/")`

`portCreate(&port); portRegister(port, "/", &root);` (the `mountpt == NULL` branch,
`srv.c:219-227`). Only when no other server owns `/` (else `-EEXIST`). **Root does NOT use
the `atDev` splice** — it claims the namespace root directly. This asymmetry (root =
`portRegister`, subtree = `atDev`-splice) is the trap; both are covered.

### 5.8 T2 acceptance checklist

- [ ] Server registers at a test mountpoint; psh `ls /nfstest` lists host dir entries.
- [ ] `cat /nfstest/<file>` returns bytes byte-identical to the host copy.
- [ ] `stat`/`ls -l` shows correct size/mtime/mode (getattr mapping correct).
- [ ] Each worker/loop thread stack is ≥ 64 KB (grep the `beginthread`/attr call).
- [ ] No Data Abort / list-corruption over a sustained `ls`/`cat` loop.

---

## 6. Stage T3b — Mount at `/nfstest`: read/write/exec acceptance

**This is the headline dev-velocity deliverable** and the crux gate before touching the boot
order. Mount as `/nfstest` (NOT root), then prove read, write, and **exec-from-NFS**
*deterministically*. **Effort:** S (after T2). **Deps:** T2 green; host export populated.

### 6.1 Launcher (additive, per platform)

- **Pi4:** after `lwip;genet` (`user.plo.yaml:77`), additive like thermal/hwrng/gpio:
  ```yaml
  - app {{ env.BOOT_DEVICE }} -x mkdir;/nfstest ddr ddr
  - app {{ env.BOOT_DEVICE }} -x nfs;/nfstest;10.42.0.1;/;v4 ddr ddr
  ```
  (args: mountpoint ; server ; export ; version. A **non-`/` mountpoint arg selects the
  `atDev`-splice path** of §5.7a — the server `portCreate`s, `nfs_mount`s, then splices itself
  under `/nfstest` via `mtSetAttr(atDev)` to the parent; it does the DHCP-wait internally.)
- **ia32-qemu:** in `rc.psh` after `X /sbin/lwip ...`:
  `X /sbin/nfs /nfstest 10.0.2.2 / v4` (preceded by a `mkdir /nfstest`).

### 6.2 Populate the export so a rebuild drops binaries (the dev-velocity mechanism)

Point the rootfs/overlay staging at `/srv/phoenix-rpi4-nfs` (study note 03 §Populating) so a
rebuild drops fresh binaries straight into the export tree — the Pi sees them on next access,
**no flashing, no SD shuttle**. The implementer adds a `scripts/nfs-stage.sh` that copies the
freshly built test binary into the export (and is allowlisted, per shell discipline).

### 6.3 Attribute-cache posture (so a rebuilt binary isn't shadowed)

The headline use case is "rebuild → run the new binary." If libnfs caches attributes/handles,
a stale cached size/handle could shadow the new file. For T3b: **disable or minimise attr
caching** so each access re-stats — `nfs_set_*` cache knobs, or simply do not cache `nfsfh`
across opens for files under active redeploy. Re-`stat` on every `mtOpen`. Revisit for
throughput only after determinism is proven. (Flag as **OQ-B**, §12.)

### 6.4 Deterministic acceptance tests (quantitative — this is the gate)

All three must pass with the stated determinism before T3 is even considered.

1. **Read fidelity (byte-for-byte).** Host: create files of sizes that straddle the
   `rsize` boundary — e.g. **1 B, 4 KB-1, 4 KB, 4 KB+1, 32 KB, 32 KB+1, 1 MB** of known
   content (e.g. `head -c <n> /dev/urandom > f<n>`; record sha256 on host). Pi: `cat
   /nfstest/f<n>` piped to a hash, OR read via a small verifier. **Pass = sha256 matches host
   for all 7 sizes, 5/5 consecutive reads each** (catches chunking/offset bugs at boundaries).
2. **Write fidelity (round-trip).** Pi: write each of the same 7 sizes to `/nfstest/w<n>`.
   Host: `sha256sum` and diff against the bytes the Pi wrote. **Pass = host sha256 == the
   intended content for all 7 sizes, 5/5 each.**
3. **Exec stability (the real test).** Place a known, small, self-contained test binary on
   the export (e.g. a "hello + exit 42" built for the target). Pi: exec it from `/nfstest`
   **M = 10 times**. **Pass = 10/10 runs produce the exact expected stdout AND exit code 42,
   with 0 faults (no Data Abort, no silent no-output).** Then exec a *larger* binary (one
   whose text spans many `rsize`-sized demand-page reads, e.g. a busybox or psh) **10 times**;
   **10/10 must run.** This directly exercises §5.5 (HOL blocking) and §5.6 (stack).

**"Passes deterministically" means:** every criterion above hits its full count (5/5, 10/10)
with **zero** intermittent failures across at least **3 separate boots**. One intermittent
exec fault = NOT deterministic → do not proceed to T3; root-cause first (suspect stack §5.6,
then HOL/timeout §5.5, then attr cache §6.3). This mirrors the #120 bench rigor (the SD doc's
"100% repro → 0/10 after fix" standard).

### 6.5 T3b acceptance checklist

- [ ] Test 1 (read) passes for all 7 sizes, 5/5, across 3 boots.
- [ ] Test 2 (write) passes for all 7 sizes, 5/5, host-verified.
- [ ] Test 3 (exec) small binary 10/10 AND large binary 10/10, 0 faults, across 3 boots.
- [ ] A rebuilt binary copied to the export runs **without a reflash/SD swap** (the win).
- [ ] No fault attributable to stack/HOL/cache observed in any boot.

---

## 7. Stage T3 — NFS as the actual rootfs (`/`)

**ONLY after T3b passes deterministically.** This is the invasive, one-way-door boot-order
change. **Effort:** L; **attended only.** **Deps:** T3b deterministic + advisor/user sign-off
on the OQs in §12.

### 7.1 The hard constraint (no runtime fallback exists)

Phoenix has **no `pivot_root`, no umount of `/`, no re-`portRegister("/")`** — a second
`portRegister("/")` fails `-EEXIST` (study note 04 §B; `user.plo.yaml:13-14` documents the
#120 lesson verbatim). Therefore **"mount NFS, fall back to dummyfs if it fails" cannot be
done by remounting.** The only safe design decides root ownership **before anyone registers
`/`**.

### 7.2 Safe design — a single root launcher that decides ownership first

Introduce one process (or a tiny `root-init` launcher) that runs **before** any `/`-owner and:

```
1. bring up network (lwip;genet) — moved to the FRONT of user.plo.yaml
2. wait for DHCP lease, bounded (e.g. 30 s)            [§4.3; but /dev/ifstatus
                                                         unavailable pre-/ → OQ-A]
3. attempt nfs_mount(host, "/", v4) with a bounded timeout (e.g. 10 s)
4a. SUCCESS → the NFS server portRegister("/") and owns root  (srv.c:219-227 branch)
4b. FAILURE (no lease / mount timeout / mount error)
         → exec dummyfs-root (or SD ext2), which portRegister("/")  ← the fallback
```

Because exactly one of 4a/4b ever calls `portRegister("/")`, there is **no `-EEXIST` race**
and **network-down still boots** (to the dummyfs/SD root). This sequencing IS the
"safe/reversible" deliverable. The sd-variant's ext2-at-`/` flow (`user.plo.yaml:27-28`,
dummyfs-root suppressed) is the existence proof that a non-dummyfs server can own `/`.

### 7.3 Boot-order changes (`user.plo.yaml`) — gated behind a new env flag

- New env flag **`RPI4B_ROOT`** (default `dummyfs`; values `dummyfs` | `sd` | `nfs`),
  parallel to the existing `RPI4B_VARIANT`. **Default keeps today's behaviour** — the change
  is inert unless explicitly selected. This is the reversibility guarantee.
- When `RPI4B_ROOT == nfs`, the **exact** required order is:
  **`dummyfs;-N;devfs` (named port, line 21) → `lwip;genet` → root-launcher (§7.2) → `mkdir
  /dev` / `bind devfs /dev` / `posixsrv` (lines 29-34) → the rest.**
  - **NOT "lwip to the very front."** lwIP's `create_dev("/dev/netsocket")` + `/dev/ifstatus`
    depend on the **devfs named port (line 21)**, and the root-launcher's lease check reaches
    them via `devfs/ifstatus`/`devfs/netsocket` (OQ-A). So devfs-named-port MUST precede
    `lwip;genet`, which MUST precede the root decision.
  - Suppress `dummyfs-root` (line 16) as a *primary* owner (it becomes the 4b fallback).
  - Everything `/`-dependent (devfs bind, posixsrv, thermal, hwrng, fb, gpio, psh) stays in
    its current relative order, just after root is established.
- **Why this is feasible at all:** `nfs`, `lwip`, `genet`, `dummyfs` are all **`-x` syspage
  programs loaded from `loader.disk`**, so the boot-critical binaries themselves have **no
  `/` dependency** — they exec before any filesystem root exists. That is precisely why
  network-before-root works.
- **Ordering hazard:** anything that must run *before* `/` exists (devfs named port,
  `pl011-tty`) already does so today (study note 04 §"encouraging part"). Verify nothing
  newly-moved-earlier needs `/` — concretely, confirm the root-launcher reaches the lease
  status via `devfs/ifstatus` (named port), not the post-bind `/dev/ifstatus` path.

### 7.4 T3 acceptance checklist

- [ ] `RPI4B_ROOT=nfs` boots to psh with `/` served from the host; `ls /`, `cat /etc/...`,
      and exec-from-`/` all work (reuse the T3b exec test against `/`).
- [ ] Writes to `/` persist on the host (verify host-side).
- [ ] **Network-down boot falls back** to dummyfs/SD root and still reaches psh (pull the
      host export / unplug; confirm graceful 4b path).
- [ ] `RPI4B_ROOT` unset / `=dummyfs` boots **exactly** as today (no regression).
- [ ] Captured a known-good manifest (`scripts/snapshot-integration-state.sh`) before/after.

---

## 8. libnfs multithreading — Phoenix-native port

> Requested by the user: follow `research/libnfs/README.multithreading` and provide a
> **Phoenix-RTOS-native multithreaded** libnfs if feasible. **Verdict: feasible and
> recommended for T2-onward robustness**, via a small native shim — NOT via Phoenix's
> partial pthread.

### 8.1 Why MT matters here (not just throughput)

The sync API serializes all fs ops behind one in-flight RPC (§5.5). For exec-from-NFS, every
demand-page fault is a blocking `nfs_pread`; under the single-thread model they cannot
overlap and a slow/lost RPC stalls the entire filesystem. libnfs's MT mode runs a **dedicated
service thread** owning all socket I/O, letting multiple application threads issue concurrent
**sync-API** calls safely (`README.multithreading:20-30`). Mapped onto the fs-server: a
**pool of msg-handler threads** + libnfs's service thread = concurrent `mtRead`s, killing the
head-of-line stall.

### 8.2 The libnfs MT seam (exactly two files)

`README.multithreading:42-89` confirms MT is abstracted into **two files**:
- `include/libnfs-multithreading.h` — typedefs for thread/mutex/sem + the wrapper API
  prototypes (the second half is generic, do not touch).
- `lib/multithreading.c` — implementations of: `nfs_mt_service_thread[_start/_stop]`,
  `nfs_mt_mutex_{init,destroy,lock,unlock}`, `nfs_mt_sem_{init,destroy,post,wait}`,
  `nfs_mt_get_tid`.

Two defines gate it: **`HAVE_MULTITHREADING`** (global on) + a backend selector
(`HAVE_PTHREAD` today). We add a third backend: **`HAVE_PHOENIX_THREADS`**.

### 8.3 Why a native shim, not Phoenix pthread

Phoenix ships `pthread.h` with `pthread_create/join/mutex*` and `PTHREAD_MUTEX_ERRORCHECK`
(verified: `libphoenix/include/pthread.h:51,64,67,154,157,175`), **but**:
- **No POSIX `<semaphore.h>`** in libphoenix (verified: only `sys/threads.h` exists; no
  `sem_init/sem_post`). libnfs's MT path needs `sem_*` (`multithreading.c:338-356`).
- **No `SYS_gettid`** syscall macro; libnfs's pthread `nfs_mt_get_tid` `#error`s without one
  (`multithreading.c:199-204`). Phoenix has a native `gettid()` (`sys/threads.h:52`) instead.

So the pthread backend would need patching anyway. The **native** Phoenix threads API is a
cleaner, complete match:

| libnfs MT primitive | Phoenix native (`sys/threads.h`) |
|---------------------|----------------------------------|
| thread create/join  | `beginthreadex(...)` / join via a join-semaphore or `threadJoin` (verify name) — `:64` |
| mutex               | `mutexCreate`/`mutexLock`/`mutexUnlock`/`resourceDestroy` — `:86,98,107` |
| semaphore           | `semaphore_t` + `semaphoreCreate`/`semaphoreDown`/`semaphoreUp`/`semaphoreDone` — `:36,110-119` (native — no POSIX sem needed) |
| tid                 | `gettid()` — `:52` |

### 8.4 The Phoenix MT backend (new code, isolated in the two seam files)

A patch (in the libnfs port's `patches/`) adds a `HAVE_PHOENIX_THREADS` branch:

`include/libnfs-multithreading.h` (new typedef block, parallel to the WIN32/pthread blocks):
```c
#elif defined(HAVE_PHOENIX_THREADS)
#include <sys/threads.h>
typedef handle_t      libnfs_thread_t;   /* beginthreadex id */
typedef handle_t      libnfs_mutex_t;    /* mutexCreate handle */
typedef semaphore_t   libnfs_sem_t;      /* native semaphore_t */
typedef int           nfs_tid_t;         /* gettid() */
#endif
```

`lib/multithreading.c` (new `#elif defined(HAVE_PHOENIX_THREADS)` block implementing the 11
functions):
```c
nfs_tid_t nfs_mt_get_tid(void)            { return gettid(); }
int nfs_mt_mutex_init(libnfs_mutex_t *m)  { return mutexCreate(m); }
int nfs_mt_mutex_destroy(libnfs_mutex_t *m){ return resourceDestroy(*m); }
int nfs_mt_mutex_lock(libnfs_mutex_t *m)  { return mutexLock(*m); }
int nfs_mt_mutex_unlock(libnfs_mutex_t *m){ return mutexUnlock(*m); }
int nfs_mt_sem_init(libnfs_sem_t *s, int v){ return semaphoreCreate(s, v); }
int nfs_mt_sem_destroy(libnfs_sem_t *s)   { return semaphoreDone(s); }
int nfs_mt_sem_post(libnfs_sem_t *s)      { return semaphoreUp(s); }
int nfs_mt_sem_wait(libnfs_sem_t *s)      { return semaphoreDown(s, 0 /*block*/); }
/* service thread: identical poll loop to the pthread version (multithreading.c:207-234),
   spawned via beginthreadex with a >=64KB stack (§5.6). Sets
   nfs->rpc->multithreading_enabled and loops poll()→nfs_service(). */
static void nfs_mt_service_thread(void *arg) { ... endthread(); }
int  nfs_mt_service_thread_start(struct nfs_context *nfs) { /* beginthreadex(64KB stack) +
        spin until multithreading_enabled */ }
void nfs_mt_service_thread_stop(struct nfs_context *nfs)  { /* clear flag + join */ }
```

**`config.h`** for the MT build adds `#define HAVE_MULTITHREADING 1` and
`#define HAVE_PHOENIX_THREADS 1` (and the port's `port.def.sh` drops `--disable-pthread`,
instead passing whatever enables `HAVE_MULTITHREADING` without selecting pthread — likely a
`CFLAGS=-DHAVE_PHOENIX_THREADS` plus a configure tweak; verify the build wires
`multithreading.c` into the lib when `HAVE_MULTITHREADING` is set).

### 8.5 Service-thread stack + the `nfs->nfsi->service_thread` field

`nfs_mt_service_thread_start` stores the thread handle in `nfs->nfsi->service_thread`
(`multithreading.c:108,238`). For the pthread typedef that field is a `pthread_t`; for the
Phoenix typedef it becomes `handle_t`. Confirm the field's type follows `libnfs_thread_t`
(it should, since it's declared via the typedef) — if it is hardcoded `pthread_t` in
`libnfs-private.h`, that's a one-line patch in the same seam.

### 8.6 Server MT model (how the fs-server uses it)

Per `README.multithreading:15-30`: `nfs_init_context` → `nfs_mount` → (if mounted)
`nfs_mt_service_thread_start(nfs)` → only the **sync API** from worker threads thereafter.
Fs-server change:
- After mount, start the service thread.
- Run a **pool of N msg-handler threads** (e.g. 4), each `beginthread`ed with a **≥64 KB
  stack** (§5.6), each looping `msgRecv`→handler→`msgRespond` on the same port. Phoenix
  `msgRecv` is safe to call from multiple threads on one port (verify against an existing
  multi-threaded server, e.g. the storage `storage_run(N, ...)` pattern referenced in the SD
  doc). Concurrent handlers issue concurrent sync `nfs_*` calls; libnfs's service thread +
  mutexes serialize the actual socket I/O. **This removes the §5.5 head-of-line stall.**

### 8.7 Phasing (do NOT do MT first)

- **T0/T1/T2/T3b correctness:** single-threaded sync (`--disable-pthread`,
  `HAVE_MULTITHREADING` off). Simplest; proves the protocol mapping.
- **T2-MT / before T3:** flip on `HAVE_PHOENIX_THREADS` + the handler pool, re-run the T3b
  exec bench. Rationale: MT is the robustness fix for exec-from-NFS HOL blocking, and T3
  (root over NFS, where *every* page fault on *every* binary goes through this) is exactly
  where it pays off. Land MT **before** T3, after T3b proves the single-threaded mapping is
  correct.

### 8.8 MT acceptance checklist

- [ ] libnfs builds with `HAVE_MULTITHREADING`+`HAVE_PHOENIX_THREADS`; `nm libnfs.a` shows
      `nfs_mt_service_thread_start` defined, no `pthread_*`/`sem_*` undefined.
- [ ] Service thread + handler-pool threads each have ≥64 KB stacks.
- [ ] Concurrent `cat` of two large files from two psh sessions both complete correctly.
- [ ] T3b exec bench (10/10 small + 10/10 large) still passes under MT — and a *concurrent*
      exec + read does not deadlock or corrupt (new MT-specific test).
- [ ] `DEBUG_PTHREAD_LOCKING_VIOLATIONS`-equivalent: the ERRORCHECK mutex posture is
      preserved (Phoenix `mutexCreateWithAttr` + `PH_LOCK_ERRORCHECK`) during bring-up.

---

## 9. Cross-arch QEMU validation matrix

**Goal:** prove genericity — the NFS stack works on a non-Pi arch with no Pi-specific code.
Only the build triple + NIC/launcher differ (§1.2).

### 9.1 Target survey (what was verified in this study)

| Project | NIC + lwIP wired? | Evidence | Use in matrix |
|---------|-------------------|----------|---------------|
| `ia32-generic-qemu` | **YES** — rtl8139 + lwIP, launched `X /sbin/lwip rtl:0x18:11` | `rc.psh:7`; `lwip/lwipopts.h`; `scripts/ia32-generic-qemu-net.sh` (virbr0 bridge + rtl8139) | **PRIMARY non-Pi proof (x86)** |
| `aarch64a72-generic-rpi4b` | YES — GENET | `user.plo.yaml:77` | HW reference (Pi4) |
| `aarch64a53-generic-qemu` | networking present in build.project | `build.project` matched `lwip/netdev` grep | **Secondary (aarch64, no HW dependency)** |
| `riscv64-generic-qemu` | **NOT confirmed** — no NIC/lwIP match found | grep found nothing | **contingent** — only if a NIC is added |
| `armv7a9-zynq7000-qemu` | **NOT confirmed** as a NIC (only busybox_config matched) | grep | **contingent** |

**Honest genericity claim:** present **ia32 (x86) + aarch64 (Pi4 HW and/or a53-qemu)** as the
cross-arch proof. List riscv64/armv7 as "contingent on NIC availability" — do **not** promise
them until a NIC + lwIP is confirmed wired in those projects.

### 9.2 Host NFS export for QEMU (same server, different reachability)

Reuse the §3.6 `nfs-kernel-server` export. For QEMU user-mode networking the guest reaches
the host at the SLIRP gateway **`10.0.2.2`** (no bridge needed); for bridged
(`ia32-generic-qemu-net.sh` uses `virbr0`) the guest gets a libvirt-pool DHCP address and the
host is the bridge IP (e.g. `192.168.122.1`). Two export options:
- **SLIRP/user-mode (simplest):** export to the guest's apparent source net. With
  `qemu ... -netdev user,id=n0` the guest's packets are NAT'd from the host, so export to
  `127.0.0.1`/`10.0.2.0/24` as appropriate, `insecure`, `fsid=0`. Host = `10.0.2.2`.
- **Bridged (virbr0):** export to `192.168.122.0/24` (the libvirt default pool), host =
  `192.168.122.1`. Matches `ia32-generic-qemu-net.sh:15`.

### 9.3 QEMU invocation shapes (exact)

**ia32-generic-qemu, user-mode net (recommended for CI-style determinism):**
```sh
qemu-system-i386 -cpu pentium3 -smp 1 -serial stdio -vga cirrus \
  -drive file=_boot/ia32-generic-qemu/hd0.disk,format=raw,media=disk,index=0 \
  -netdev user,id=net0 -device rtl8139,netdev=net0,id=nic0,addr=03.0
  # guest reaches host NFS at 10.0.2.2; libnfs: nfs_mount("10.0.2.2","/",v4)
```
(This is the stock `ia32-generic-qemu-net.sh` with `-netdev bridge,br=virbr0` swapped for
`-netdev user` so no host bridge/root is required. Keep the bridge variant for parity with
the existing script.)

**aarch64a53-generic-qemu:** use the existing `scripts/aarch64a53-generic-qemu.sh` as the
base; add a `-netdev user,id=n0 -device <virtio-net|the NIC its lwip expects>` (confirm which
NIC model its lwip driver binds — read its lwip launch args before committing the device).

### 9.4 In-guest test commands (T1/T2/T3b on QEMU)

Mirror the HW tests, substituting host IP and the `rc.psh` launcher:
1. **T1:** `rc.psh` line `X /usr/bin/nfs-smoke 10.0.2.2 / /etc/hostname` → serial shows the
   host file bytes.
2. **T2:** `X /sbin/nfs /nfstest 10.0.2.2 / v4` then at psh: `ls /nfstest`, `cat
   /nfstest/<file>`, compare to host.
3. **T3b:** the §6.4 deterministic suite — read 7 sizes (sha compare), write 7 sizes
   (host verify), exec a known target binary 10×. **Exec-from-NFS on a 2nd arch is the
   strongest genericity signal.**

### 9.5 QEMU matrix acceptance checklist

- [ ] ia32-qemu: T1 reads a host file over rtl8139 + lwIP (proves the stack is not
      Pi/GENET-specific).
- [ ] ia32-qemu: T2 `ls`/`cat` of host export via the `mt*` server.
- [ ] ia32-qemu: T3b read/write/exec deterministic suite passes (or document the gap).
- [ ] (If available) aarch64a53-qemu: at least T1 passes (2nd aarch64 path, no HW).
- [ ] Genericity statement written: "same `nfs`/`nfs-smoke` binary + same libnfs.a recipe,
      only `${HOST}` triple and the launcher line changed."

---

## 10. Consolidated risk register

| # | Risk | Stage | Likelihood | Impact | Mitigation |
|---|------|-------|-----------|--------|------------|
| R1 | **Exec-from-NFS fails / faults** (the #120 exec-from-mounted-fs class) | T3b/T3 | Med | High | §5.6 ≥64 KB stacks; §5.5 bounded RPC + MT (§8); §6.4 deterministic exec bench *before* T3; cite SD-exec doc as prior art |
| R2 | **Worker-thread 8 KB stack overflow → silent corruption** | T2+ | Med-High | High | Hard rule §5.6: every handler/worker thread ≥ `16*_PAGE_SIZE` (the #120 fix) |
| R3 | **Head-of-line blocking wedges fs on a slow/lost RPC** | T2+/T3 | Med | High | §5.5 `nfs_set_timeout`; §8 MT service-thread + handler pool |
| R4 | **No runtime root fallback** (no pivot_root; 2nd `portRegister("/")`=-EEXIST) | T3 | Certain (design fact) | High | §7.2 decide-before-register launcher; network-down → dummyfs/SD 4b path; `RPI4B_ROOT` flag defaults off |
| R5 | **DHCP not bound before mount** (nothing waits today) | T1/T3 | High if unhandled | High | §4.3 DHCP-wait via `/dev/ifstatus`, interface-name-agnostic, bounded timeout |
| R6 | **`/dev/ifstatus` unavailable pre-`/`** (lives behind `/dev` bind) | T3 | Certain | Med | OQ-A: alternate lease check for root case (direct socket port, or bounded mount retry) |
| R7 | **Stale attr/handle shadows a rebuilt binary** | T3b | Med | Med | §6.3 disable/minimise attr cache; re-stat on open; OQ-B |
| R8 | **libnfs v4 path less battle-tested than v3** | T1 | Med | Med | §4.5 v3-over-TCP fallback decided AT T1, before building T2 on it |
| R9 | **Cross-`configure` misdetects `HAVE_*`** | T0 | Med | Low-Med | §3.4 static `config.h`; study already found the only quirks (sockaddr_storage, arpa/inet) |
| R10 | **rsize/wsize exceeds msg payload cap → truncation** | T1 | Low | Med | §4.4 measure cap in T1, set rsize/wsize ≤ it (default 32 KB); libnfs chunks |
| R11 | **MT seam: `service_thread` field type / sem absence / no SYS_gettid** | T2-MT | Med | Med | §8.3-8.5 native `HAVE_PHOENIX_THREADS` shim (avoids pthread sem/gettid gaps); one-line field-type patch if needed |
| R12 | **`writev` chattiness → IPC overhead** | T1+ | Low | Low | correct as-is (study note 02); one-line coalesce patch only if profiling demands |
| R13 | **Reserved-port bind rejected** | T1 | Low | Low | `insecure` export (§3.6); libnfs no-reserved-port knob |
| R14 | **Non-x86 QEMU targets lack a NIC** → genericity overclaim | T9 | Med | Low | §9.1 only claim confirmed targets (ia32 + aarch64); mark riscv/arm contingent |
| R15 | **Stale-core image** (committed fs-server change not in image) | all | Med | Med | `rebuild --scope core` after committed core changes; `strings loader.disk \| grep nfs` |

---

## 11. Rollback / abort procedure per stage

**General:** every validated step ends with `scripts/snapshot-integration-state.sh` →
`manifests/*.md`. To revert all siblings: `scripts/restore-integration-state.sh <manifest>`.
Sibling commits are small and reviewable; the coordination repo records the integration SHA.

- **T0:** Pure addition under `phoenix-rtos-ports/libnfs/`. Abort = `git -C
  sources/phoenix-rtos-ports rm -r libnfs/` (or `git checkout`); nothing else references it.
  No boot impact (nothing links libnfs yet).
- **T1:** `nfs-smoke` util + one launcher line. Abort = delete the util dir + remove the
  `-x nfs-smoke`/`rc.psh` line. Boot unaffected once the line is gone (it ran late, additive).
- **T2:** New `phoenix-rtos-filesystems/nfs/` + its `DEFAULT_COMPONENTS` entry. Abort = remove
  the dir + the component entry. Not launched at boot yet (no plo line) → zero boot risk.
- **T3b:** Additive `mkdir /nfstest` + `-x nfs;/nfstest;...` after lwIP. Abort = remove those
  two lines → boot returns to exactly today's behaviour. **Fully reversible, additive.**
- **T3:** **The only invasive stage.** Reversibility is built in: gated behind `RPI4B_ROOT`
  (default `dummyfs` = today). Abort levels:
  1. Unset/`=dummyfs` `RPI4B_ROOT` → boot is byte-for-byte today's flow (the primary rollback).
  2. Network-down at runtime → the §7.2 launcher's 4b path falls back to dummyfs/SD root
     automatically (boot still reaches psh).
  3. Full revert → `restore-integration-state.sh` to the pre-T3 manifest.
  **Capture a known-good manifest immediately before enabling `RPI4B_ROOT=nfs`.**
- **MT (§8):** libnfs port patch + fs-server pool. Abort = drop the patch (rebuild port with
  `--disable-pthread`/MT off) + revert the server to the single-threaded loop. T3b's
  single-threaded acceptance is the fallback baseline.

---

## 12. Open questions / decisions needed from the user before T3

These need a human/advisor decision **before** the invasive T3 (NFS-as-root):

- **OQ-A — root-case lease check (near-answered; needs one verification).** `/dev/ifstatus`
  (and `/dev/netsocket`) are `create_dev`'d by lwIP (`devs.c:606`); the *literal* `/dev/X`
  path fails before the `/dev` bind exists, **but the SD-root flow proves the
  `devfs/<name>` named-port form resolves pre-bind** — that is the exact trick the SD driver
  uses for `/dev/mmcblk0pN` (`user.plo.yaml:24-26`: "literal path fails pre-bind → falls back
  to `devfs/<name>`"). So the root-case DHCP-wait should open **`devfs/ifstatus`** (and the
  socket layer reaches **`devfs/netsocket`**), mirroring the SD driver. **This sinks the
  earlier candidate "lookup(`/dev/netsocket`)"** — same pre-bind problem, same `devfs/` fix.
  **Verify (not re-investigate):** that `devfs/ifstatus` resolves via the devfs named port
  before the `/dev` bind, exactly as `devfs/mmcblk0pN` does. Fallback if not: a bounded
  `nfs_mount` retry loop (mount fails fast until routable). **Decision needed:** confirm the
  `devfs/` path resolves pre-`/`; if yes OQ-A is closed.

- **OQ-B — attribute-cache policy.** For the "rebuild → run new binary" workflow, do we
  disable libnfs attr caching entirely (simplest, slower) or set a short TTL + re-stat on
  open? Affects whether a freshly built binary is ever shadowed by a stale handle. **Decision
  needed:** caching posture for the redeploy use case (T3b sets it; T3 inherits).

- **OQ-C — protocol lock-in.** If T1 shows libnfs v4 is flaky on Phoenix, do we ship v3-over-
  TCP as the *primary* (it pulls portmap/mount RPCs, still bundled, but adds round-trips), or
  invest in fixing the v4 path? **Decision needed at T1**, before T2 is built on a protocol.

- **OQ-D — MT before T3?** Confirm the §8.7 phasing: land the Phoenix-native MT backend +
  handler pool **before** T3 (because root-over-NFS makes every page fault an RPC and HOL
  blocking would be acute), accepting the added complexity. **Decision needed:** MT is a hard
  pre-req for T3, or T3 ships single-threaded first and MT follows.

- **OQ-E — T3 is a one-way-door boot change.** Per project policy (memory: "unattended vs
  attended scoping"), T3 must be an **attended** session. Confirm: T3 only proceeds with the
  user present, after T3b is deterministic (§6.4), with a captured rollback manifest.

- **OQ-F — write semantics / persistence expectations.** Confirm the host export stays `rw`
  + `no_root_squash` and that writes from the Pi (uid 0) landing as root on the host is
  acceptable for the lab. (Security posture is fine on the private crossover link; confirm
  for any shared host.)

- **OQ-G — licensing sign-off (LGPL-2.1 static link).** Study note 05 §License: static-
  linking LGPL-2.1 libnfs into publicly-published Phoenix code is permissible *with the
  relink path* (libnfs source + the port recipe are public). Confirm with whoever owns
  licensing before shipping (factual note, not legal advice).

### Open-question resolutions (user, 2026-06-07)

- **OQ-A → VERIFY (mine).** User: "no idea, verify it yourself." Action: during T1 prep I
  confirm whether `devfs/ifstatus` (or `devfs/netsocket`) resolves *before* `/` is owned;
  not a user decision. Closes when measured.
- **OQ-B → start cache-OFF, add TTL later.** T3b runs with attribute cache disabled (easiest
  to reason about / test). Once NFS is stable, implement proper attr/handle caching with a
  reasonable, controllable TTL (do NOT ship cache-off as the final design).
- **OQ-C → protocol-agnostic; "something that works well."** Prefer NFSv4 (single port, no
  portmapper); if v4 proves flaky on Phoenix at T1, fall back to v3-over-TCP without hesitation.
- **OQ-D → MT required in the final version; timing is mine.** Land single-threaded first if
  that's faster to working; the Phoenix-native MT backend (§8) MUST be in before this is
  considered done.
- **OQ-E → proceed unattended where safe.** User not needed for normal progress; will assist
  on explicit request. T3 (one-way-door root change) may be attempted without a human in the
  loop **if** it carries a safe automatic fallback (decide-before-register), else ask.
- **OQ-F → uid-0 writes acceptable; ignore security.** Export a narrow, dedicated, disposable
  directory; nothing critical; `rw` + `no_root_squash`, no squashing needed. Security is out
  of scope for this PoC.
- **OQ-G → ignore licensing for now.** Internal, unpublished proof-of-concept prototype.
  LGPL-2.1 static-link concerns deferred; revisit before any eventual publication.

---

## 13. Host preparation — Linux NFS server (prerequisite for T1+)

Phoenix-RTOS is the **NFS client**; the Linux dev host runs the **NFS server**. This must be
stood up, populated, and **proven reachable from the lab subnet before T1** (T0 is host-only
and needs none of it). §3.6 introduced it; this section is the full runbook. **Do not
hand-edit `/etc/exports`** — add a managed helper `scripts/nfs-export-up.sh` (allowlisted),
mirroring how `scripts/netboot-server.sh` manages dnsmasq.

### 13.1 Network context (reuse the existing netboot subnet)

From `scripts/netboot-server.sh` (study note 03): host `10.42.0.1/24` on the netboot NIC
(`RPI4B_NETBOOT_IFACE`, default `eth1`); the Pi gets `10.42.0.10..20` via dnsmasq DHCP
(12 h lease); dnsmasq is DHCP+TFTP only (`port=0` disables its DNS) and does **not** serve
NFS. NFS is a separate daemon on the same host/subnet. (QEMU reachability differs — §9.2.)

### 13.2 Install + enable the server

```sh
sudo apt-get install -y nfs-kernel-server nfs-common
sudo systemctl enable --now nfs-server          # (alias: nfs-kernel-server)
```

### 13.3 Create + populate the export tree

```sh
sudo mkdir -p /srv/phoenix-rpi4-nfs
sudo chown "$(id -u)":"$(id -g)" /srv/phoenix-rpi4-nfs   # so rebuilds can stage without sudo
```
Populate it (the dev-velocity mechanism, §6.2): point the build's rootfs/overlay staging at
`/srv/phoenix-rpi4-nfs` so a rebuild drops fresh target binaries straight in. Minimum content
for the test stages:
- `/srv/phoenix-rpi4-nfs/etc/hostname` — a known small text file for the **T1** read.
- `/srv/phoenix-rpi4-nfs/test/f{1,4095,4096,4097,32768,32769,1048576}` — the 7 boundary-size
  files for the **T3b** read-fidelity test (`head -c <n> /dev/urandom > f<n>`; record
  `sha256sum` on the host into a manifest).
- `/srv/phoenix-rpi4-nfs/bin/hello` and a larger `/srv/phoenix-rpi4-nfs/bin/busybox` (or
  `psh`), **built for the target arch**, for the **T3b** exec test.
- (T3 only) a full target rootfs tree staged here.

### 13.4 The export line + options (each is load-bearing)

The helper writes to `/etc/exports.d/phoenix-rpi4.exports` (a drop-in, not the main file):
```
/srv/phoenix-rpi4-nfs  10.42.0.0/24(rw,sync,no_subtree_check,no_root_squash,insecure,fsid=0)
```
| Option | Why (study note 03) |
|--------|---------------------|
| `rw` | the point — drop binaries without SD swaps |
| `no_root_squash` | Phoenix runs uid 0; without it root-owned writes squash to `nobody` and fail |
| `insecure` | accept NFS from source ports **>1023** (removes any dependency on libnfs binding a reserved port — risk R13) |
| `no_subtree_check` | modern default; avoids subtree-check overhead/bugs |
| `sync` | host data integrity (safe default) |
| `fsid=0` | makes this the **NFSv4 pseudo-root** → the v4 client mounts `host:/`; harmless for v3 |

Apply + verify:
```sh
sudo exportfs -ra
sudo exportfs -v                 # shows the active export + options
showmount -e 10.42.0.1           # v3 path sanity: lists /srv/phoenix-rpi4-nfs
```

### 13.5 NFSv4 specifics (the recommended protocol)

- With `fsid=0`, `/srv/phoenix-rpi4-nfs` IS the v4 pseudo-root → the client mounts export
  path **`/`** (`nfs_mount(nfs, "10.42.0.1", "/")`), not the on-disk path.
- v4 needs only **TCP 2049** open — no portmapper(111)/mountd/statd. Pin nothing extra.
- Confirm v4 is enabled (default on modern distros): `cat /proc/fs/nfsd/versions` should show
  `+4 +4.1` (or set `RPCNFSDOPTS`/`/etc/nfs.conf [nfsd] vers4=y`). If v4.1 is needed for
  session/trunking, ensure `vers4.1=y`.

### 13.6 Firewall / reachability

On the private crossover/point-to-point netboot link no extra firewalling is needed. If a
host firewall is active: allow **TCP 2049** (v4). For the **v3 fallback** also allow **111**
(portmap) + the mountd/statd ports (pin them in `/etc/nfs.conf` and open those). Verify the
server listens:
```sh
sudo ss -tlnp | grep -E ':2049|:111'      # 2049 (nfsd) [+ 111 for v3]
sudo rpcinfo -p 10.42.0.1                  # v3: lists nfs/mountd/portmap programs
```

### 13.7 Host-side prove-reachable BEFORE booting Phoenix (decisive prerequisite)

Mount the export **from another Linux host on the same subnet** (or the dev host itself) to
prove the server works independently of any Phoenix bug — so T1 failures are unambiguously
*client* failures, not host misconfig:
```sh
# v4:
sudo mkdir -p /mnt/nfs-selftest
sudo mount -t nfs4 -o vers=4.1 10.42.0.1:/ /mnt/nfs-selftest
ls /mnt/nfs-selftest && cat /mnt/nfs-selftest/etc/hostname
echo probe | sudo tee /mnt/nfs-selftest/host-write-probe   # write works (no_root_squash)
sudo umount /mnt/nfs-selftest
# v3 fallback:
sudo mount -t nfs -o vers=3,tcp 10.42.0.1:/srv/phoenix-rpi4-nfs /mnt/nfs-selftest
```

### 13.8 Host preparation checklist (gate before T1)

- [ ] `nfs-kernel-server` installed + enabled; `systemctl is-active nfs-server` = active.
- [ ] `/srv/phoenix-rpi4-nfs` exists, owned writable, populated with the T1/T3b fixtures.
- [ ] Export line present with `rw,no_root_squash,insecure,no_subtree_check,fsid=0`;
      `exportfs -v` confirms.
- [ ] `showmount -e 10.42.0.1` lists the export; `ss -tlnp` shows 2049 listening.
- [ ] v4 enabled (`/proc/fs/nfsd/versions` shows `+4`/`+4.1`).
- [ ] **Host self-mount (§13.7) succeeds** for the chosen protocol — read AND write — proving
      the server is correct before any Phoenix client runs.
- [ ] Fixture sha256 manifest recorded on the host (for T3b byte-for-byte comparison).
- [ ] (QEMU) the §9.2 reachability variant (SLIRP `10.0.2.2` or bridge `192.168.122.1`)
      chosen and self-mount-verified from the QEMU host's perspective.

---

## 14. Testing strategy — scenarios per stage, in order

This section is the single ordered list of **what to test, when, and what "pass" means**. It
gates progression: **do not advance to the next stage until the current stage's tests pass.**
Each scenario references its stage's acceptance checklist for the bullet form.

### 14.0 Test environments

| Env | Use | How driven |
|-----|-----|-----------|
| **Host-only** | T0 link/symbol checks; §13.7 host self-mount | shell on the dev host |
| **Pi4 netboot HW** | T1–T3, primary | `scripts/test-cycle-netboot.sh`; `scripts/uart-summary.sh <label>`; HDMI snapshots in `artifacts/hdmi/` |
| **ia32-generic-qemu** | genericity proof (T1–T3b) | §9.3 qemu invocation; serial stdio |
| **aarch64a53-qemu** | 2nd-aarch64 genericity (≥T1) | `scripts/aarch64a53-generic-qemu.sh` + netdev |

Capture discipline: `--capture-secs >= 180` to see lwIP startup + NFS prints, `timeout >=
(capture_secs+80)*1000` (CLAUDE.md). Use a `Monitor` armed on the test-cycle output with a
`grep` filter for the expected NFS log line rather than polling.

### 14.1 Stage T0 tests — does libnfs build for the target? (host-only)

| # | Scenario | Command | Pass |
|---|----------|---------|------|
| T0.1 | Static lib builds | run the port build | `libnfs.a` produced, no errors |
| T0.2 | Public symbols present | `${CROSS}-nm libnfs.a \| grep -E 'nfs_mount\|nfs_open\|nfs_pread\|nfs_pwrite\|nfs_stat64\|nfs_opendir\|nfs_creat'` | all shown as `T` (defined) |
| T0.3 | v4 path linked | `${CROSS}-ar t libnfs.a \| grep -E 'nfs_v4\|libnfs-raw-nfs4'` | present |
| T0.4 | No pthread in sync build | `${CROSS}-nm libnfs.a \| grep pthread` | empty |
| T0.5 | Trivial link | link `int main(){nfs_init_context();}` | links clean |
→ **Gate:** all pass → do §13 host prep → proceed to T1.

### 14.2 Host-prep tests — §13.8 (gate before T1)

The §13.7 host self-mount (read + write, chosen protocol) is the decisive scenario: it
isolates host correctness from client bugs. **Must pass before any Phoenix client boots.**

### 14.3 Stage T1 tests — sockets + RPC end-to-end (Pi4 HW first, then ia32-qemu)

| # | Scenario | Pass | Resolves |
|---|----------|------|----------|
| T1.1 | DHCP-wait binds | UART: `interface bound, ip=10.42.0.x` within timeout | OQ-4 |
| T1.2 | Mount succeeds (v4) | UART: `mounted 10.42.0.1:/` ; no error | core RPC |
| T1.3 | Read one file | UART prints `etc/hostname` bytes **== host content** | RPC+XDR+read |
| T1.4 | Write-back marker | host shows `nfs-smoke-marker.txt` with expected text | write path |
| T1.5 | Poll/RTT sane | logged `pread` RTT < a few ms on lab link | OQ-1 |
| T1.6 | Large read (no truncation) | read a 1 MB file, byte count == size | OQ-3 (set rsize) |
| T1.7 | Reserved-port bind | mount works with `insecure` export (or bind<1024 ok) | OQ-2 |
| T1.8 | **Protocol decision** | record v4 pass; if v4 fails, T1.2–T1.6 pass under v3-TCP | OQ-C |
| T1.9 | ia32-qemu repeat | T1.1–T1.4 pass on ia32 (host `10.0.2.2`) | genericity |
| T1.10 | `nfscli ping <server>` (§4.8) | reports reachable from psh prompt | CLI triage |
| T1.11 | `nfscli ls <srv> / <dir>` | lists host export entries, matches host `ls` | CLI |
| T1.12 | `nfscli cat <srv> / <file>` | bytes == host content | CLI |
| T1.13 | `nfscli stat <srv> / <file>` | size/mode/mtime match host | CLI |
| T1.14 | `nfscli put <srv> / <file>` | round-trip; host-verified content | CLI write |
→ **Gate:** T1.1–T1.6 pass on Pi4 (protocol locked via T1.8); T1.9 demonstrates non-Pi;
T1.10–T1.14 give the interactive triage tool. **This is the decisive feasibility gate** — if
v4 misbehaves, fall to v3 HERE before T2.

### 14.4 Stage T2 tests — the `mt*` fs-server (Pi4 HW)

| # | Scenario | Pass |
|---|----------|------|
| T2.1 | Server registers + splices `/nfstest` (§5.7a) | psh `ls /nfstest` lists host dir entries |
| T2.2 | Readdir correctness | `ls /nfstest` matches host `ls`; entry count + names exact |
| T2.3 | Read fidelity (single file) | `cat /nfstest/<file>` == host bytes |
| T2.4 | Getattr | `ls -l` / `stat` shows correct size, mtime, mode, type |
| T2.5 | Lookup multi-component | `cat /nfstest/a/b/c` resolves (lookup returns chars-consumed) |
| T2.6 | Write + create | `echo x > /nfstest/new` then host sees `new` with `x` |
| T2.7 | Stack safety | sustained `ls`/`cat` loop (e.g. 100×) → **0** Data Abort / list corruption |
| T2.8 | Timeout behavior | kill the host export mid-op → server returns error, does **not** hang forever |
→ **Gate:** T2.1–T2.7 pass; T2.8 confirms bounded-RPC (§5.5). Proceed to T3b.

### 14.5 Stage T3b tests — read/write/EXEC determinism (the crux gate) (Pi4 HW)

The full quantitative suite from §6.4. **This is the hardest bar and the gate before T3.**

| # | Scenario | Pass (quantitative) |
|---|----------|---------------------|
| T3b.1 | **Read fidelity at boundaries** | for sizes {1, 4095, 4096, 4097, 32768, 32769, 1048576}: sha256(Pi read) == host sha256, **5/5 reads each** |
| T3b.2 | **Write fidelity at boundaries** | write same 7 sizes; host sha256 == intended, **5/5 each** |
| T3b.3 | **Exec small binary** | run `/nfstest/bin/hello` **10×** → exact stdout + exit 42, **10/10**, 0 faults |
| T3b.4 | **Exec large binary** | run `/nfstest/bin/busybox`(or psh) **10×** (text spans many demand-page `mtRead`s) → **10/10**, 0 faults |
| T3b.5 | **Rebuild→run (the headline)** | rebuild a binary, stage to export, run from `/nfstest` with **no reflash/SD swap** → new behavior observed |
| T3b.6 | **Determinism across boots** | T3b.1–T3b.4 repeated across **3 separate boots** → **zero** intermittent failures |
| T3b.7 | ia32-qemu cross-arch | T3b.1–T3b.3 pass on ia32-qemu (exec-from-NFS on a 2nd arch) |
→ **Gate (strict):** every count hit in full (5/5, 10/10) with **0** intermittent failures
across 3 boots. **One intermittent exec fault = NOT deterministic** → root-cause first
(stack §5.6 → HOL/timeout §5.5 → attr cache §6.3), do **not** proceed to T3.

### 14.6 MT tests — Phoenix-native multithreading (before T3) — §8.8

| # | Scenario | Pass |
|---|----------|------|
| MT.1 | MT lib builds | `nm` shows `nfs_mt_service_thread_start` defined; no undefined `pthread_*`/`sem_*` |
| MT.2 | Stacks | service + pool threads each ≥ 64 KB |
| MT.3 | Concurrent reads | two psh sessions `cat` two large files simultaneously → both correct |
| MT.4 | Exec under MT | T3b.3 + T3b.4 still 10/10 with the handler pool enabled |
| MT.5 | Concurrent exec+read | exec a binary while another thread reads a large file → no deadlock, no corruption |
| MT.6 | No HOL stall | a slow/blocked RPC on one file does not stall ops on another (the §5.5 fix, observed) |
→ **Gate (if OQ-D = MT-before-T3):** MT.1–MT.6 pass → T3 may proceed.

### 14.7 Stage T3 tests — NFS as `/` (attended only) — §7.4

| # | Scenario | Pass |
|---|----------|------|
| T3.1 | Boot to psh with NFS `/` | `RPI4B_ROOT=nfs` → reaches `(psh)%`; `ls /`, `cat /etc/...`, exec-from-`/` all work |
| T3.2 | Exec-from-root determinism | T3b.3/T3b.4 exec suite re-run against `/` → 10/10, 0 faults |
| T3.3 | Write persistence | writes to `/` persist + visible on host |
| T3.4 | **Network-down fallback** | host export down / link pulled at boot → §7.2 4b path falls back to dummyfs/SD root, **still reaches psh** |
| T3.5 | **No-regression** | `RPI4B_ROOT` unset/`=dummyfs` → boots **exactly** as today (diff UART stages vs a baseline manifest) |
| T3.6 | Rollback proven | `restore-integration-state.sh <pre-T3 manifest>` returns to known-good |
→ **Gate:** T3.1–T3.6 pass with the user present; known-good manifest captured before enabling.

### 14.8 Regression / soak (continuous, all stages once reached)

| # | Scenario | Pass |
|---|----------|------|
| RG.1 | Multi-boot pass-rate | `scripts/test-cycle-bench.sh <N> nfs-<stage>` → record pass rate; no new faults |
| RG.2 | Soak | leave NFS mounted, run periodic read/write/exec for a sustained window → 0 faults, no leak (node table bounded) |
| RG.3 | Host-restart resilience | restart `nfs-server` mid-session → client recovers or fails cleanly (no hang/corruption) |
| RG.4 | Cross-arch parity | the ia32-qemu T1/T2/T3b suite stays green when the Pi4 suite does |

### 14.9 Test-execution order (one line)

`T0 (host-only) → §13 host prep + self-mount → T1 (Pi4 then ia32) → T2 → T3b (strict
determinism, 3 boots) → MT (if OQ-D) → T3 (attended) → RG soak`. **Never skip the T3b
determinism gate before T3.**

---

*End of plan. Implementation begins at T0 only after the user accepts this plan and the OQs
above (at minimum OQ-C is needed at T1; OQ-A/B/D/E before T3). Host prep (§13) is a hard
prerequisite before T1; the T3b determinism gate (§14.5) is a hard prerequisite before T3.*
