# T3 (NFS-as-root) — attended attempt #1 result: lwip doesn't bring up pre-"/"

**Date:** 2026-06-08. **Outcome:** changes 1–3 implemented + built + boot-tested on HW; the
nfsroot boot reorders correctly and degrades safely (no brick), **but NFS-root does not yet work**
because of a wall the feasibility study did not anticipate.

## What was built (committed, regression-safe)
- **Change 1** libphoenix `socksrvcall` devfs/netsocket fallback (socket.c) — HW-regression-tested
  on a normal **netboot**: nfs-smoke mount+read still works, 0 faults → the cross-cutting libc
  change is **inert** on the existing path. (commit libphoenix d5461a9)
- **Change 2** NFS server root mode (`filesystems/nfs/srv.c` `nfs_runRoot`): accept "/", bounded-
  retry `nfs_mount`, `portRegister(port,"/")`, self-parent; LOUD FATAL on deadline. (456e3df, fb767c6)
- **Change 3** `nfsroot` plo variant + `--variant nfsroot` build plumbing (project f2ddae5,
  82d0ed3; scripts de6979f). dummyfs-root skipped; lwip early; nfs-root before mkdir/bind/posixsrv.

## The wall (HW-proven, 2 iterations)
In the `nfsroot` early slot, **lwip never completes bring-up**:
- lwip prints `dhcp_start: 0; netif waits for OFFER` then **never gets a lease** (no bound/ip line;
  every working netboot shows `interface bound, ip=10.42.0.12`).
- The NFS root server's probe `lookup("devfs/netsocket") rc=-2` (ENOENT) — **lwip never registered
  its socket node in devfs** (so the socksrvcall fallback has nothing to resolve).
- Consequently `nfs_mount` fails every attempt (first "Failed to open socket"; with 3s backoff
  "Invalid address … can not resolv" — both = no working socket layer), hits the 90s FATAL, "/"
  never registers, psh never starts. **Safe failure (bounded-retry + FATAL), recoverable (netboot).**

Adding a 10s idle settle (to rule out the NFS server's mount-storm starving lwip's tcpip thread)
did **not** help — lwip still didn't bind with the server idle. So the cause is **not** contention;
it is a genuine **ordering dependency**: lwip's full bring-up (DHCP lease + socket-server
registration) needs something that, in `nfsroot`, only comes up *after* "/" — i.e. a cycle:
**lwip → (posixsrv / "/" / the /dev bind) → "/" (NFS) → lwip.** The study's claim that lwip has
"no / dependency at startup" (main.c:73-153) is incomplete — its socket-server / tcpip path needs
more than create_dev+netif init.

## Two ways forward (decision needed)
1. **Design-A (NFS-root for real):** bring up a minimal **dummyfs RAM "/" FIRST** (as normal
   netboot) so posixsrv + the /dev bind + lwip all initialise normally and lwip gets its lease +
   registers netsocket; THEN the NFS server mounts and **takes over "/"** (portUnregister/register
   or over-mount). Hazards (advisor): the unregister→register window isn't atomic (in-flight fds,
   /dev rebind). Substantial + needs careful HW validation. This is the real NFS-as-root.
   (Prereq worth doing first: a focused read of WHY lwip stalls pre-"/" — is it posixsrv, "/", or
   the /dev bind? — so design-A doesn't just move the cycle.)
2. **Off-ramp (deliver the value now):** the user's actual goal is a *rich Phoenix setup with the
   ported programs, runnable*. That does NOT require root-on-NFS: NFS at **/nfstest works today**
   (read+write+exec HW-proven). Build phoenix-rtos-ports, stage them on the host export
   `/srv/phoenix-rpi4-nfs`, and run them from `/nfstest` on the Pi. Delivers the goal without the
   root-takeover hazard.

**Recommendation:** off-ramp (2) for value now; design-A (1) as a deeper follow-up if root-on-NFS
is a hard requirement. Either is a real chunk of work — hence the decision point.

Tree state: nfsroot variant committed but non-working; netboot+sd variants unaffected
(regression-proven); libphoenix change inert. No revert needed.

---

## UPDATE 2 (2026-06-08): lwip works; the real blocker is a devfs-pre-"/" node-visibility bug

Two corrections to the above (the "lwip never leases" read was WRONG):
- **lwip is fine pre-"/"** — it's alive, link up, `dhcp_start:0`; its bring-up uses only
  kernel-native primitives (no posixsrv/"/" dependency). So there is NO ordering cycle. The
  "design-A required" verdict above is **downgraded to fallback**, not the primary path.
- The blocker is purely **socket() name resolution pre-"/"**, split into two gaps:
  - **Gap A (fixed):** libnfs's `socket()` resolves via the KERNEL `inet_socket`→`socksrvcall`
    →`proc_lookup("/dev/netsocket")` (posix/inet.c), NOT libphoenix socket.c. Added a kernel
    `devfs/netsocket` fallback there (commit kernel ddc3b091). **UNTESTED on netboot regression —
    next session must confirm this kernel change is inert post-"/" before trusting it.**
  - **Gap B (ROOT-CAUSED, the real wall):** instrumented boot shows
    `lwip: netsocket create_dev rc=0 port=3 selflit=-2 selfdevfs=-2 dport=0` — i.e. `create_dev`
    mtCreates "netsocket" in the devfs server and returns 0, but the node is **unreachable by
    ANY name pre-"/" — even from lwip's own process** (literal `/dev/netsocket` AND `devfs/
    netsocket` both ENOENT). nfs-fs's cross-process probes agree (both -2).

### The decisive clue: the SD ext2-root proves this IS solvable
The **sd variant create_dev's `/dev/mmcblk0pN` pre-"/" via the SAME `create_dev` code AND its node
IS reachable** (flash_oidResolve `lookup("devfs/mmcblk0pN")`, and the ext2 mount + boot succeed,
#120). So `create_dev` + `devfs/<name>` resolution DOES work pre-"/" for the SD driver but NOT for
lwip's netsocket. Same create_dev path (devfs found at file.c:523 → strip /dev → mtCreate otDev in
devfs, file.c:623-641). The difference must be devfs/dummyfs-server-side or a subtle param/timing.

### RECOMMENDED NEXT (direct fix, cleaner than design-A):
Compare the **netsocket vs mmcblk node lifecycle in the devfs/dummyfs server** (phoenix-rtos-
filesystems/dummyfs, run as `dummyfs -N devfs -D`): why is one `mtCreate`d-otDev node resolvable
pre-"/" and the other not? Suspects: (1) otDev vs the dir-walk; (2) the SD driver resolves within
its OWN process while netsocket is created by lwip and queried cross-process — does the devfs
server actually persist/serve the node before "/" (dcache vs on-disk-dummyfs)? (3) a `dummyfs -N`
(named-port mode) quirk where created nodes aren't in the lookup tree pre-"/". Instrument the
devfs server's mtCreate+mtLookup for netsocket. If found + fixed → Gap A's kernel fallback then
lets `socket()` succeed → NFS root mounts. **design-A (RAM "/" → takeover) remains the fallback if
the devfs bug proves intractable.**

### Tree state (commits, no push): kernel ddc3b091 (Gap A fallback — REGRESSION-TEST on netboot),
lwip be98d19 (DIAG prints — revertable), filesystems 6c263f6 (DIAG probe — revertable), plus the
earlier changes 1-3. nfsroot still non-working (FATAL → no "/"); netboot/sd presumed unaffected but
the NEW kernel change needs a netboot regression check.

---

## UPDATE 3 (2026-06-08): Gap B is a KERNEL resolver limitation, not dummyfs — DECISIVE

Server-side dummyfs DIAG (commit filesystems 09f5c90) on an instrumented boot
(`nfsroot-gapb2`):
```
dummyfs: DIAG create netsocket: pid=4 port=1 dir.id=0 rc=0 -> node.port=1 node.id=3
lwip: netsocket create_dev rc=0 port=3 selflit=-2 selfdevfs=-2 dport=0
nfs-fs: probe devfs/netsocket rc=-2     (cross-process)
```
The dummyfs server logs the **mtCreate** of netsocket (dir.id=0, rc=0) — the node IS created
correctly in the devfs root. But there is **NO `DIAG lookup netsocket` line** despite THREE
lookups attempted (lwip self literal + self devfs, nfs cross-process) — i.e. **not one `mtLookup`
ever reached the dummyfs server.** So the failure is entirely in the **kernel name resolver**:
`lookup("devfs/<sub>")` pre-"/" (no root registered) does NOT forward an `mtLookup` to the `devfs`
named port — it returns ENOENT in-kernel. (Bare `lookup("devfs")` DOES work — create_dev used it —
so the *multi-component* named-port path is the gap.) This **contradicts the verdict doc's Q1**
static trace of name.c (which concluded it forwards); the HW disproves it. The dummyfs server +
the kernel inet.c Gap-A fallback are both fine; the wall is the kernel resolver not forwarding
`<namedport>/<sub>` lookups pre-"/".

### Implication / recommendation (CHECKPOINT)
Direct fix now = a **core kernel namespace-resolver change** (make `proc_portLookup` forward the
remainder via `mtLookup` to a resolved named port even when no root "/" is registered — phoenix-
rtos-kernel/proc/name.c). That is delicate + affects all path resolution → must be done carefully,
attended, with regression tests, NOT at the tail of a long session. **design-A** (boot a minimal
dummyfs RAM "/" first so ALL names resolve normally, bring up posixsrv+lwip, then NFS takes over
"/") remains the alternative that sidesteps this entirely.

**Decision for next session (fresh context):**
- **Path 1 (kernel resolver fix):** small, targeted change to name.c so `devfs/netsocket` forwards
  mtLookup pre-"/". If it's truly a missing-forward (not a deliberate gate), this is the CLEANEST
  outcome (real NFS-root, no takeover) and the inet.c fallback already in place would then work.
  Verify it doesn't break normal path resolution (full regression). HIGHER VALUE if low-risk.
- **Path 2 (design-A):** RAM "/" → NFS takeover. Avoids the kernel change; has the non-atomic
  unregister/register + in-flight-fd + /dev-rebind hazards.
- **Off-ramp:** ports via /nfstest works TODAY (delivers the rich-ported-toolset goal without root).

DIAG prints (lwip be98d19, filesystems 6c263f6, the nfs probes) should be REVERTED once Gap B is
fixed. The kernel inet.c Gap-A fallback (ddc3b091) is correct + needed for path 1; regression-test
it on netboot regardless.
