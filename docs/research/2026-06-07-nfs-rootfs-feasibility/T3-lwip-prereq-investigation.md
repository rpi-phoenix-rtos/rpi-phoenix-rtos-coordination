# T3 — Why lwip fails to bring up when launched before "/" (read-only source investigation)

**Date:** 2026-06-08. **Mode:** read-only source + archived-UART investigation (NO build, NO HW,
NO source changes). **Question:** pin down EXACTLY why, in the `nfsroot` variant, lwip launched
early (after the `devfs` named port + pl011-tty, but before mkdir / `bind devfs /dev` / posixsrv /
"/"), never lets the NFS root server open a socket — the blocker for NFS-as-rootfs (#153 T3).

## TL;DR verdict

The prior attempt's framing ("lwip never completes bring-up / never gets a DHCP lease") is **not the
mechanism**. The archived UART logs show **lwip is alive and healthy** (no crash, genet link up,
`dhcp_start: 0`), and DHCP is irrelevant to the actual failure. The real blocker is a **name-resolution
gap in the `socket()` syscall path**, plus a **second, unexplained reachability gap**:

- **Gap A (resolver, root-caused):** the `socket()` syscall resolves the socket server through the
  **kernel** `socksrvcall` → `proc_lookup("/dev/netsocket")` (`phoenix-rtos-kernel/posix/inet.c:29`,
  literal path, NO devfs fallback). A literal `/`-prefixed path **cannot resolve before "/" is
  registered** (`proc/name.c:361-369` returns `-ENOENT` when `rootRegistered==0`). The
  `devfs/netsocket` fallback that T3 change-1 added is in the **userspace** `libphoenix/sys/socket.c`
  `socksrvcall` — a **different** code path that the `socket()` syscall does **not** use. So the fix
  that was made does not touch the path that actually fails.
- **Gap B (reachability, UNEXPLAINED from static analysis):** the NFS server's own probe
  `lookup("devfs/netsocket")` returns **-2 (ENOENT)** — so netsocket is **not in the devfs tree
  either**, even though lwip's `create_dev` returned success. By the same logic the flat name
  `/dev/netsocket` is also absent. The node is registered under **no name that any resolver can
  reach pre-"/"**, and *why* is not determinable without one instrumented boot.

**Recommendation: design-A (boot a minimal RAM "/" first), because it sidesteps BOTH gaps** and
reproduces the proven-working netboot ordering. A targeted fix is presented but is
**conditional/unvalidated** — it cannot be asserted to work while Gap B is open.

---

## 1. What actually happens (raw UART evidence, not the summary doc)

Two archived nfsroot boots under `artifacts/rpi4b-uart/`:
- `...-052131-netboot-nfsroot-feature.log` (60 s deadline)
- `...-053542-netboot-nfsroot-settle.log` (10 s settle + explicit probe, 90 s deadline)

Rendered nfsroot launch order (feature log line 324):
`dummyfs;-N;devfs;-D → pl011-tty → lwip;genet → nfs;/;…;root → mkdir;/dev → bind devfs /dev →
posixsrv → … → psh`.

Key lines (feature log):
- `327 lwip: genet@fd580000: SYS_REV_CTRL=…` — lwip's genet init (in `create_netif`, i.e. AFTER
  `init_lwip_sockets` ran in `main`).
- `399 lwip: genet@fd580000: link up: 100 Mbps full-duplex`
- `400 lwip: genet@fd580000: dhcp_start: 0 (0=ok); netif waits for OFFER`
- `350…456 nfs-fs: root mount attempt N failed: … Failed to open socket` (every attempt) →
  `456 nfs-fs: FATAL root mount failed after 60s, / not registered`.
- **No `Exception` / `Data Abort` / fault anywhere.** lwip does not crash.

Settle log adds the decisive probe:
- `394 dhcp_start: 0` … `396 nfs-fs: probe devfs/netsocket rc=-2`.

**Interpretation:** lwip is up. "Failed to open socket" is libnfs's `create_socket` →
`socket()` returning -1 (`research/libnfs/lib/socket.c:1447-1449`, called from `:1430`). The settle
variant proves the 10 s idle does NOT help and that `devfs/netsocket` is unresolvable. DHCP
"never leasing" is a **non-issue**: nfs-fs can't even open a socket, and the visible "interface
bound" line in netboot comes from nfs-smoke's `/dev/ifstatus` wait (skipped in root mode), not from
lwip — so its absence here says nothing about whether DHCP completed.

## 2. The socket() syscall path — Gap A (root-caused)

`socket()` is a libphoenix `WRAP_ERRNO_DEF` (`libphoenix/sys/socket.c:44`) that traps into the
kernel POSIX layer:

```
socket() syscall
  → posix.c:1789  inet_socket(domain,type,protocol)
  → inet.c:281    socksrvcall(&msg)               (msg.type = sockmSocket)
  → inet.c:29     proc_lookup(PATH_SOCKSRV, …)     PATH_SOCKSRV = "/dev/netsocket"
  → name.c:437    proc_lookup → proc_portLookup
```

`PATH_SOCKSRV` = `"/dev/netsocket"` (`phoenix-rtos-kernel/include/sockport.h:35`).

`proc_portLookup("/dev/netsocket")` (`name.c:224`):
- not `"/"`, not the `"devfs"` fast-path (exact-string predicate, `name.c:100-103,262`);
- full-string dcache miss (`name.c:284`, `:321`);
- prefix-strip walk (`name.c:343-359`) finds no registered prefix (`"/dev"` is NOT a kernel named
  port — it is a *bound mountpoint inside* "/"); `srv` stays `rootOid`;
- **`name.c:361-369`: `rootRegistered==0 && i==0` → returns `-ENOENT`.**

So `socket()` returns the error → libnfs "Failed to open socket". **The kernel `socksrvcall` has no
`devfs/netsocket` fallback** — confirmed: the only `proc_lookup` in `phoenix-rtos-kernel/posix/` is
`inet.c:29`. The T3 change-1 fallback lives in `libphoenix/sys/socket.c:61-72` (userspace
`socksrvcall`), which serves only the message-based helpers (getnameinfo/getaddrinfo/getifaddrs); it
is never reached by the `socket()` syscall. **This is solid and is half the bug.**

## 3. The registration path — Gap B (unexplained)

`init_lwip_sockets` (`phoenix-rtos-lwip/port/sockets.c:1234-1254`) runs in `main()` (explicitly, via
the `#ifndef HAVE_WORKING_INIT_ARRAY` block at `main.c:79-119`; `HAVE_WORKING_INIT_ARRAY` is **not**
defined for this target, and `__constructor__` expands to nothing — `include/arch/cc.h:67-71`). It
does `portCreate` → `create_dev(&oid, PATH_SOCKSRV)` → spawn `socketsrv_thread`. On any non-zero
`create_dev` it calls `errout`, which **`exit(1)`s** (`port/common.c:32-43`). Because lwip later
prints `dhcp_start` (`create_netif` runs after `init_lwip_sockets`), **`create_dev` returned 0**.

`create_dev("/dev/netsocket")` (`libphoenix/unistd/file.c:512`):
- `lookup("devfs")` (`:523`) — succeeds via the kernel devfs fast-path once dummyfs `-N devfs`
  has registered the `"devfs"` named port (`dummyfs/srv.c:230-231`);
- strips the `/dev` prefix (`:559-560`), `mtCreate(otDev,"netsocket")` to the devfs port (`:623-637`);
- this is exactly how `/dev/route`, `/dev/ifstatus` (`devs.c:599-635`) and the SD variant's
  `/dev/mmcblk0pN` are created — and the SD variant **proves** an `otDev` node so created is
  resolvable pre-"/" via `lookup("devfs/<name>")` (dummyfs_lookup with `dir==NULL` walks the root
  object's children, `dummyfs/dummyfs.c:49-139`).

**The contradiction:** `create_dev` returned 0, the create+lookup mechanism is sound and proven by
the SD precedent, yet the probe `lookup("devfs/netsocket")` = -2 says the node is **not in devfs**.
And it is not under the flat name either: if `create_dev` had taken the `portRegister("/dev/netsocket")`
fallback (`file.c:531-548`, only when `lookup("devfs")` AND `lookup("/dev")` both fail), the kernel's
full-string dcache lookup of `"/dev/netsocket"` (`name.c:284`, checked **before** the `rootRegistered==0`
bailout) would HIT and `socket()` would succeed. It doesn't. **So netsocket is registered under no
name any resolver can reach pre-"/", despite create_dev success — and static analysis cannot say why.**

Prime suspects for Gap B (to be settled with ONE instrumented boot, see §5):
1. **Cold-boot race between lwip's first `create_dev` and devfs registration.** lwip launches with
   only dummyfs(`-N devfs -D`) + pl011-tty ahead of it; plo launches are sequenced but daemon
   *registration* is async (dummyfs forks a child that registers and signals via SIGUSR1;
   `srv.c:189-217,230-231,276`). If lwip's `init_lwip_sockets` create_dev raced ahead of the
   `"devfs"` registration, the static `nodevfs` latch in `create_dev` (`file.c:518,526-527`) would
   divert to the `portRegister` fallback — but that predicts the flat name, which is ruled out
   above. So a simple race does not fully fit; instrument to confirm/deny.
2. **devfs `-N` namespace-mode quirk for `otDev` create-vs-lookup** at the root object pre-bind
   (the `d->oid.port != d->dev.port` mountpoint branch at `dummyfs.c:104-109`, or object-id
   handling). Less likely given the SD precedent, but the SD node is created by a driver that may
   differ in timing.
3. **Node created into a *different* devfs instance / port** than the one the probe resolves
   (would require two registrants of `"devfs"`; not seen, but the fast-path caches the first).

## 4. Working vs broken slot — what lwip actually consumes

| resource | netboot (works) | nfsroot (broken) | does lwip's bring-up need it? |
|---|---|---|---|
| dummyfs-root **"/"** | present (before lwip) | **absent** (NFS will own "/") | **YES — indirectly:** the kernel `socket()` syscall resolves `/dev/netsocket` only by walking from "/". No "/" → `socket()` = -ENOENT (§2). |
| `bind devfs /dev` | present (before lwip) | absent | YES — it is what makes the literal `/dev/netsocket` walkable from "/". |
| posixsrv | present (before lwip) | absent | **NO.** lwip's tcpip/DHCP/socketsrv use only kernel-native primitives — `mutexCreate/condCreate/condWait/gettime/usleep/beginthread(ex)` (`port/mbox.c`, `port/threads.c`, `drivers/bcm-genet.c`). No `pipe()/select()/poll()/eventfd`, no posixsrv RPC, no path `open()/fopen()` in the tcpip or genet path. (posixsrv is the advisor's earlier prime suspect — **ruled out** for lwip's own bring-up.) |
| `devfs` named port | present | present (early) | YES for `create_dev` to land netsocket — and it IS present, yet Gap B shows the node still isn't reachable. |
| DHCP lease | completes | unknown/irrelevant | DHCP runs entirely in lwip's tcpip thread via the in-process genet driver (raw UDP, not the socket API). It does **not** need "/", posixsrv, or `socket()`. Its apparent "non-completion" is a non-event — see §1. |

**Working-slot proof** (`...-051807-netboot-nfsroot-regress.log`, the netboot baseline): lwip launches
AFTER dummyfs-root + bind + posixsrv; `399 nfs-smoke: interface bound, ip=10.42.0.12`,
`403 mounted 10.42.0.1:/ via NFSv4`, READ ok. The ONLY relevant difference vs nfsroot is **"/" (and
the /dev bind under it) exists before any `socket()` call**, which is exactly what lets the kernel
`proc_lookup("/dev/netsocket")` succeed.

So the **minimal set lwip's *consumers* need** is: a registered **"/"** with **`/dev`** under it such
that `/dev/netsocket` resolves. lwip itself needs only the `devfs` named port (present) + kernel
primitives (always present). The dependency is the **root namespace**, of which posixsrv is merely
another consumer — not lwip's own requirement.

## 5. Verdict

### Recommended: design-A (minimal RAM "/" first, then NFS takeover)

Boot a minimal **dummyfs RAM "/"** FIRST (as the working netboot does) so `bind devfs /dev`, posixsrv,
and lwip all initialise in the proven order and `socket()` resolves `/dev/netsocket` by walking
root→/dev→netsocket. THEN the NFS server mounts and **takes over "/"**. This **sidesteps both Gap A
and Gap B by construction** — it never exercises the pre-"/" socket-resolution path that fails, and it
is byte-for-byte the ordering the regress log proves works.

Takeover mechanism + hazards to validate on HW:
- Either `portUnregister("/")` then `portRegister(port,"/")` from the NFS server (the
  unregister→register window is **not atomic** — `name.c:177-188,112-133`), or an over-mount via
  `mtSetAttr(atDev)` onto the existing "/" (the dummyfs `-m`/nfs-mountThread precedent,
  `nfs/srv.c` notes at `:345-349`).
- **In-flight fd hazard:** any fd opened against the dummyfs "/" (or nodes resolved through it)
  before takeover keeps pointing at the old root oid; validate that nothing holds a cwd/root fd
  across the swap (psh isn't up yet, but posixsrv and the device drivers are).
- **`/dev` rebind hazard:** `/dev` is bound into the dummyfs "/". After NFS owns "/", confirm the
  `/dev` mountpoint is still resolvable (re-`bind devfs /dev` against the new root if the bind does
  not survive the root swap), or device nodes vanish.
- **`/dev/netsocket` continuity:** once a RAM "/" exists, netsocket resolves normally and Gap B is
  moot for the running system — but verify the swap doesn't invalidate the cached socketsrv oid held
  by in-flight callers.

### Targeted fix — CONDITIONAL / UNVALIDATED (do NOT ship as the recommendation)

A narrow fix would have to close **both** gaps; only Gap A's fix is known, and Gap B is unexplained:
- **Gap A:** add a `devfs/netsocket` fallback to the **kernel** `socksrvcall`
  (`phoenix-rtos-kernel/posix/inet.c:24-40`) — `if (proc_lookup(PATH_SOCKSRV…)<0)
  proc_lookup("devfs/netsocket"…)` — mirroring the userspace change. Viable **in principle** (the SD
  variant proves `devfs/<name>` resolves pre-"/"), but it is a **kernel POSIX-layer change** (heavier,
  upstream-sensitive) and it **resolves nothing while Gap B holds** — the probe shows netsocket is not
  in devfs, so a devfs fallback would also return -2.
- **Gap B:** must be root-caused first. **First experiment (one instrumented boot):** in lwip's
  `init_lwip_sockets`, log the `create_dev(PATH_SOCKSRV)` return value AND, immediately after,
  `lookup("devfs/netsocket")` and `lookup("/dev/netsocket")` from lwip's *own* process. That tells you
  in one boot which branch `create_dev` took and where (if anywhere) the node landed — disambiguating
  the race (suspect 1) from the devfs-mode quirk (suspect 2) from a dual-instance (suspect 3).

Because Gap B is open and a targeted fix is therefore unvalidated, **design-A is the robust path.** If
Gap B turns out to be a simple cold-boot race that a small lwip-side retry/ordering tweak closes, the
targeted fix (Gap A kernel fallback + Gap B retry) becomes a lighter alternative — but that is a
follow-up contingent on the instrumented boot, not a present recommendation.

## Evidence index (file:line)

- Kernel socket resolver (Gap A): `phoenix-rtos-kernel/posix/inet.c:24-40` (`socksrvcall`,
  `proc_lookup(PATH_SOCKSRV)`), `:269-287` (`inet_socket`); `posix/posix.c:1789`.
- `PATH_SOCKSRV` = `/dev/netsocket`: `phoenix-rtos-kernel/include/sockport.h:35`.
- Literal-path pre-"/" ENOENT: `phoenix-rtos-kernel/proc/name.c:224-369` (esp. `:361-369`);
  devfs fast-path `:100-103,258-277`; full-string dcache before bailout `:279-299,318-336`.
- Userspace fallback (wrong path): `libphoenix/sys/socket.c:56-76`.
- lwip socket registration: `phoenix-rtos-lwip/port/sockets.c:1234-1254`; `main.c:73-153,79-119`;
  `errout`→exit `port/common.c:32-43`; cc.h `__constructor__`/`HAVE_WORKING_INIT_ARRAY`
  `include/arch/cc.h:67-71`.
- `create_dev` + portRegister fallback: `libphoenix/unistd/file.c:512-642` (`:518` nodevfs latch,
  `:523` devfs lookup, `:531-548` fallback, `:559-560` /dev strip, `:623-637` mtCreate).
- devfs `-N` registration + lookup: `phoenix-rtos-filesystems/dummyfs/srv.c:155-247,276-309`;
  `dummyfs/dummyfs.c:49-139` (lookup), `:702-782` (create).
- lwip posixsrv-independence: `phoenix-rtos-lwip/port/mbox.c`, `port/threads.c`, `port/tcpip.c`,
  `drivers/bcm-genet.c` (kernel-native sync only); DHCP in-process.
- libnfs socket-open failure: `research/libnfs/lib/socket.c:1430,1447-1449`.
- nfs-fs root-mode + probe: `phoenix-rtos-filesystems/nfs/srv.c:350-379` (`:368` probe).
- Raw logs: `artifacts/rpi4b-uart/rpi4b-uart-20260608-052131-netboot-nfsroot-feature.log`,
  `...-053542-netboot-nfsroot-settle.log`, `...-051807-netboot-nfsroot-regress.log`.
