# T3 — `devfs/<name>` pre-"/" resolution verdict (read-only source investigation)

**Date:** 2026-06-08. **Mode:** read-only source investigation (NO build, NO HW, NO source changes).
**Decides:** the path for NFS-as-rootfs (#153 T3) after the HW result in
`T3-attempt-1-result.md` UPDATE 2 showed `lwip: netsocket create_dev rc=0 port=3 selflit=-2
selfdevfs=-2 dport=0` — i.e. the netsocket node is created (rc=0) but unreachable by EITHER
`/dev/netsocket` (selflit=-2) OR `devfs/netsocket` (selfdevfs=-2), even from lwip's own process,
pre-"/".

This doc settles three questions that the prior feasibility study got partly wrong.

---

## Q1 — Does `lookup("devfs/<name>")` resolve pre-"/" AT ALL? **YES (kernel-side).**

The kernel's `proc_portLookup` **does** forward a `devfs/<name>` lookup to the devfs server before
"/" is registered. Traced in `sources/phoenix-rtos-kernel/proc/name.c:224-434` for the literal name
`"devfs/netsocket"`:

1. Not `"/"` (name.c:239) and not the exact-string `"devfs"` fast-path
   (`name_traceDevfsLookup` is an **exact** `hal_strcmp(name,"devfs")` predicate,
   name.c:80-103,262) — so neither special case fires for `"devfs/netsocket"`.
2. Full-string dcache miss for `"devfs/netsocket"` (name.c:284, re-checked at :321).
3. **Prefix-strip walk (name.c:343-359):** scans back to the `/` at index 5, sets
   `pptr[5]='\0'`, looks up `"devfs"`. The `"devfs"` named port **is** in the general dcache —
   `proc_portRegister` inserts EVERY non-"/" name into `dcache[]` (name.c:156-162), not only the
   fast-path `devfsOid` latch (name.c:164-167). So the walk **hits**: `srv = devfsOid`, `break`,
   leaving **`i = 5` (non-zero)**.
4. **The pre-"/" bailout does NOT fire:** `name.c:361` is
   `if (rootRegistered == 0 && i == 0)`. Here `i == 5`, so the condition is false even with no
   root registered. Execution falls through to the server-query loop.
5. **Server query (name.c:386-420):** `skip=1`, `offs=6`, sends `mtLookup("netsocket")` to the
   `devfs` port (`srv`). The result comes back FROM the devfs server.

So the kernel **already** resolves `devfs/<name>` pre-"/", provided `"devfs"` is registered as a
named port (it is, via `dummyfs -N devfs`). This is **independently confirmed** by the comment on
the kernel fallback that was already added in commit `ddc3b091`
(`sources/phoenix-rtos-kernel/posix/inet.c:31-42`): it documents exactly this strip-and-forward
behaviour as the reason the fallback is expected to work.

**Sharp caveat — Q1 is two sub-questions; do not conflate them:**
- *"Does the kernel forward `devfs/<name>` pre-"/"?"* → **YES** (the trace above; the earlier
  doc's *mechanism* claim was correct).
- *"Is the netsocket node actually present in the devfs server to be found?"* → **NO** on HW
  (selfdevfs=-2). That -2 is **not** the kernel pre-"/" gate rejecting the path — the kernel
  forwards the mtLookup and the **devfs server itself returns ENOENT** (or the node never landed
  there). The HW -2 is a *server-side reachability* failure (Gap B), not a *kernel-resolver*
  failure.

The earlier doc's only real error on Q1 was **citing the Pi4 SD driver as proof** that
`devfs/<name>` resolves pre-"/". Q2 demolishes that citation — but the kernel mechanism claim
itself stands.

**Answer: YES, kernel-side. `proc_portLookup` forwards `devfs/<name>` to the devfs server pre-"/"
(name.c:343-369, esp. the `i==0` guard at :361). No kernel namespace change is required to make
multi-component named-port paths resolve pre-"/" — that capability already exists.**

---

## Q2 — How does the Pi4 SD ext2-root resolve `/dev/mmcblk0pN` pre-"/"? **(b/c) oid-direct — NOT by name.**

The Pi4 SD driver is **bcm2711-emmc**
(`sources/phoenix-rtos-devices/storage/bcm2711-emmc/`), NOT imx6ull-flash. It does **not**
re-`lookup()` the `/dev` path by name. It uses the `oid` from `create_dev` plus libstorage's own
integer-id bookkeeping:

- **Node creation + oid captured directly** (`sdstorage_dev.c:387-402`): `create_dev(oid, path)`
  returns the node's `oid`; the driver string-compares the path **it itself built** against the
  configured `rootDev` (`strcmp(path, sdcard_common.rootDev) == 0`, :399) and on match records
  `sdcard_common.rootStorageId = GET_STORAGE_ID(oid->id)` (:400). No kernel name-lookup — the id
  comes straight out of the create_dev oid.
- **Root mount is id-direct** (`sdstorage_srv.c:288-328`): polls `sdstorage_getRootStorageId`
  (:307) for that recorded `rootId`, then `storage_mountfs(storage_get(rootId), …, &oid)` (:318)
  — `storage_get` is an in-process libstorage table lookup **by integer id** — and finally
  `portRegister(oid.port, "/", &oid)` (:324).
- **The driver's own comment says it explicitly** (`sdstorage_srv.c:302-305`):
  *"We mount by the id directly -- the zynq-flash/pc-ata pattern -- rather than re-resolving the
  /dev path, which cannot be looked up here: the root mounts BEFORE "/" exists and before devfs is
  bound at /dev."* Echoed at `sdstorage_dev.c:82-85`.

**Consequence:** the SD ext2-root proves **nothing** about `devfs/<name>` name-resolution pre-"/".
The feasibility doc's precedent — `flash_oidResolve` doing `lookup("devfs/mmcblkpN")` — is from
**imx6ull-flash/flashsrv.c**, a driver the Pi4 does **not** use. As precedent for the Pi4 path it
is **invalid**.

**Answer: mechanism (b)+(c) — oid returned by `create_dev` used directly (`sdstorage_dev.c:400`)
+ libstorage integer-id bookkeeping (`storage_get(rootId)`, `sdstorage_srv.c:307-318`). No
`devfs/<name>` re-lookup. The SD ext2-root is NOT evidence that `devfs/<name>` resolves pre-"/".**

---

## Q3 — Verdict + path

### What the task hypothesized vs what is true

The task framed the fork as *"kernel-namespace-fix vs devfs-server-fix vs design-A,"* with the
kernel-namespace-fix needed *if* `devfs/<name>` does not resolve pre-"/". **Q1 disproves the need
for a kernel namespace change.** The kernel already forwards `devfs/<name>` to the devfs server
pre-"/" (name.c:343-369). The only kernel touch in play is the **one-line socksrvcall fallback**
already committed in `ddc3b091` (`posix/inet.c:43`) so that `socket()` *attempts* `devfs/netsocket`
when the literal `/dev/netsocket` misses — that is a trivial fallback, **not** a namespace change,
and it needs only a netboot regression check (UPDATE 2 notes it is untested-for-inertness). So:

**"kernel-namespace-fix" is OFF the menu.** The real fork is **devfs-server-fix vs design-A.**

### Why this cannot be closed statically

Gap B (netsocket created with rc=0 yet unreachable under any name, including `devfs/netsocket`
forwarded correctly by the kernel) is **not** resolvable by source reading:

- `create_dev("/dev/netsocket")` returned 0 → it did not take the `errout`/exit path, and lwip went
  on to print `dhcp_start` (so the node was "created" against *some* port).
- The flat-name `portRegister("/dev/netsocket")` fallback (`libphoenix/unistd/file.c:531-548`) is
  **ruled out** by `selflit=-2`: had it fired, the kernel's full-string dcache lookup of
  `"/dev/netsocket"` (name.c:284, checked **before** the rootRegistered bailout) would HIT. It
  doesn't.
- So the node landed on the `devfs` named port, the kernel forwards the relative lookup there
  correctly (Q1), yet the devfs server answers ENOENT. **Why** is a server-side
  create-vs-lookup / instance / timing question that static analysis cannot decide.

**The concrete clue is `dport=0`** in the instrumented line: that is the devfs port `create_dev`
saw / the node landed on. One instrumented boot must settle: did `create_dev` land the otDev node
in the *same* devfs instance the relative lookup queries, and does that instance serve the node
*before* "/" (dcache vs on-disk dummyfs walk; the `dummyfs -N devfs` named-port-mode lookup tree at
`dummyfs/dummyfs.c:49-139` vs create at `:702-782`)?

### RECOMMENDATION: **design-A** (minimal RAM "/" first, then NFS takeover) — primary, defensible today.

Boot a minimal **dummyfs RAM "/"** FIRST (the proven netboot order), so `bind devfs /dev`,
posixsrv, and lwip all initialise normally and `socket()` resolves `/dev/netsocket` by walking
root→/dev→netsocket. THEN the NFS server mounts and **takes over "/"**. This **sidesteps Gap B by
construction** — it never exercises the failing pre-"/" devfs-reachability path — and reproduces
byte-for-byte the ordering that the netboot regress log proves works. As a read-only analysis that
**cannot** close Gap B, design-A is the only path that can be recommended with confidence now.

**Takeover mechanism (to implement + validate on HW):**
- From the NFS server, `portUnregister("/")` then `portRegister(port, "/")`. The window is **not
  atomic**: `proc_portUnregister` for "/" just clears `rootRegistered=0`
  (`name.c:184-188`), and `proc_portRegister` for "/" sets `rootOid`/`rootRegistered=1`
  (`name.c:117-133`). libphoenix wrappers: `portUnregister`/`portRegister` in
  `libphoenix/sys/msg.c` (the prior doc cites the `:28` region). An over-mount via
  `mtSetAttr(atDev)` onto the existing "/" (the dummyfs `-m` / nfs-mountThread precedent in
  `nfs/srv.c:345-349`) is the alternative if the unregister/register swap proves racy.

**Hazards to validate on HW (the swap is the risky part):**
- **In-flight fd / cached-root hazard:** any fd or cwd/root resolved against the dummyfs "/" before
  takeover keeps pointing at the old root oid. psh isn't up yet, but posixsrv and the device
  drivers are — confirm none hold a root/cwd fd across the swap.
- **`/dev` rebind hazard:** `/dev` is bound into the dummyfs "/". After NFS owns "/", confirm the
  `/dev` mountpoint is still resolvable; re-`bind devfs /dev` against the new root if the bind does
  not survive the swap, or device nodes vanish.
- **`/dev/netsocket` continuity / cached socketsrv oid:** once a RAM "/" exists netsocket resolves
  normally (Gap B moot for the running system), but verify the swap does not invalidate the
  socketsrv oid cached by in-flight callers.

### ALTERNATIVE (cheaper, becomes preferred IF Gap B is a tractable server bug): **devfs-server-fix.**

Because Q1 shows the kernel already forwards `devfs/netsocket` correctly, IF one instrumented boot
shows Gap B is a simple devfs/dummyfs server-side landing bug — e.g. the `dummyfs -N devfs` named
instance not placing `create_dev`'d `otDev` nodes in the lookup tree it serves pre-"/", or a
cold-boot race between lwip's first `create_dev` and devfs registration — then fixing that node's
visibility in `sources/phoenix-rtos-filesystems/dummyfs` (`dummyfs.c` create `:702-782` vs lookup
`:49-139`; `srv.c` `-N` registration `:155-247`) PLUS the already-committed kernel socksrvcall
fallback (`inet.c:43`) lets `socket()` succeed with **no root-takeover swap at all**. This is the
lighter path and is *upstream-cleaner*, but it **cannot be asserted to work** from static analysis —
it is contingent on the instrumented boot that localizes Gap B. Do **not** ship it as the
recommendation while Gap B is open.

### One-line recommendation

**Go design-A** (boot RAM "/" → NFS takes over "/"); it is the only path defensible without closing
Gap B. Spend ONE instrumented boot first (log `create_dev` return + the landed `dport`, then
`lookup("devfs/netsocket")` from lwip's own process) — if it reveals Gap B as a tractable
devfs-server landing bug, switch to the cheaper devfs-server-fix; otherwise design-A stands.

---

## Evidence index (file:line)

- **Q1 kernel resolver:** `sources/phoenix-rtos-kernel/proc/name.c:224-434` — prefix-strip walk
  `:343-359`, pre-"/" bailout `:361-369` (fires only when `i==0`), exact-"devfs" fast-path
  `:80-103,262`, general dcache insert for all names `:156-162`, server query `:386-420`. Kernel
  `devfs/netsocket` fallback (already committed ddc3b091) + its mechanism comment:
  `sources/phoenix-rtos-kernel/posix/inet.c:24-55` (esp. `:31-43`). `PATH_SOCKSRV="/dev/netsocket"`:
  `phoenix-rtos-kernel/include/sockport.h:35`.
- **Q2 SD oid-direct:** `sources/phoenix-rtos-devices/storage/bcm2711-emmc/sdstorage_dev.c:387-402`
  (create_dev oid captured, rootStorageId set at :400), `:82-85` (root-by-id comment);
  `…/sdstorage_srv.c:288-328` (id-direct mount: getRootStorageId :307, storage_mountfs/storage_get
  :318, portRegister "/" :324) — explicit "mount by id, not /dev re-resolve" comment `:302-305`.
  imx6ull-flash precedent (NOT used by Pi4): `imx6ull-flash/flashsrv.c` `flash_oidResolve`.
- **Gap B (open):** create_dev path `libphoenix/unistd/file.c:512-642` (flat-name fallback
  `:531-548`, devfs strip+mtCreate `:559-560,623-637`); devfs server
  `sources/phoenix-rtos-filesystems/dummyfs/dummyfs.c:49-139` (lookup), `:702-782` (create),
  `srv.c:155-247` (`-N` registration).
- **Takeover (design-A):** `name.c:184-188` (unregister "/"), `:117-133` (register "/");
  `libphoenix/sys/msg.c` portUnregister/portRegister; `nfs/srv.c:345-349` (over-mount precedent).
- **HW evidence:** `T3-attempt-1-result.md` UPDATE 2 (the `selflit=-2 selfdevfs=-2 dport=0` line);
  `T3-lwip-prereq-investigation.md` (Gap A/Gap B split; raw nfsroot UART logs under
  `artifacts/rpi4b-uart/`).
