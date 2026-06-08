# T3 — Gap B dummyfs investigation (server-side source trace + DIAG add)

**Date:** 2026-06-08. **Mode:** host-only source investigation; contained DIAG add to the
dummyfs server (no behavioral change); build verified. NO hardware (orchestrator HW-tests).
**Decides:** whether Gap B (netsocket `create_dev` rc=0 but unreachable pre-"/") is a small
contained dummyfs fix, or whether design-A is the path.

## Verdict: NO contained fix exists. create and lookup are provably symmetric in dummyfs source.

The pinned hypothesis was that the `dummyfs -N devfs` (named-port) instance attaches
`create_dev`'d nodes into one root object but `mtLookup` searches from a *different* (or unset)
root — i.e. an asymmetry specific to `-N` mode pre-"/". **Source disproves this.** Both the create
and the lookup resolve to the **same** root object (`id = DUMMYFS_ROOTID = 0`), in the **same**
server instance, via the **same** directory rbtree keyed the **same** way.

### The full symmetric trace (file:line)

**`-N devfs` always builds a real root object (id=0), same as root mode.**
`dummyfs -N devfs -D` → `srv.c:229-237` `portRegister(port, "devfs", &root)` with `root = {0}`.
Then `dummyfs_mount` → `dummyfs_alloc` (`dummyfs.c:1166-1234`) unconditionally creates the root
directory object with `o->oid.id = DUMMYFS_ROOTID (0)` (`dummyfs.c:1184-1185`), `dummyfs_dir_init`,
and adds `.`/`..`. `-N` mode does **not** skip or alter this — the root object exists identically
to `dummyfs-root`. (The only `-N` difference is the named-port `portRegister("devfs")` vs root
`portRegister("/")`, plus `mountpt=NULL` and no `fetch_modules`/mount.)

**create_dev lands netsocket in root id=0.**
`create_dev("/dev/netsocket")` (`libphoenix/unistd/file.c:512-642`): `lookup("devfs", NULL, &odev)`
(:523) hits the kernel devfs fast-path (`name.c:262-275`) returning
`devfsOid = {port, id=0}` (set at `proc_portRegister` `name.c:165` from the dcache entry whose
`id = oid->id = 0`, `name.c:157`). `/dev` prefix stripped → `netsocket`; `splitname` yields
`base="netsocket", dir="/"`, and the intermediate-dir loop breaks immediately on the leading `/`
(`file.c:569-574`), so **no** intermediate dir is created. Final mtCreate (`file.c:623-637`) is sent
with `msg.oid = odev = {devfs, id=0}`, name `"netsocket"`, type `otDev`. Server
`_dummyfs_create` (`dummyfs.c:702-774`) gets `dir = {id=0}` → root object, allocates the node
(`dummyfs_object_create`, idtree id=1), and `_dummyfs_link` → `dummyfs_dir_add` inserts
`"netsocket"` into **root's** dir rbtree (`dir.c:110-178`, key = `dummyfs_dir_hash("netsocket", 9)`).

**lookup("devfs/netsocket") searches root id=0 of the same instance.**
Kernel (`name.c:224-434`): full-string dcache miss, prefix-strip walk hits the `"devfs"` dcache
entry at `i=5` → `srv = {devfs, id=0}` (`name.c:354-356`); pre-"/" bailout does NOT fire (`i==5`,
`name.c:361`); server query sends `mtLookup("netsocket")` with `msg.oid = srv = {devfs, id=0}`
(`name.c:387-404`). Server `dummyfs_lookup` (`dummyfs.c:49-139`): `dir = {id=0}` →
`dummyfs_object_get({id=0})` → **root object** (`dummyfs.c:67-73`), then `dummyfs_dir_find`
(`dir.c:78-91`) → `dummyfs_dir_get` hashes `"netsocket"` with len from `strchrnul` = 9 → **same key
as the add**, same rbtree, finds the node, returns it as a device (`dummyfs.c:118`).

**Conclusion:** create attaches to root id=0; lookup searches root id=0; same instance, same key.
There is no source-level path by which the create succeeds (rc=0) and the immediate lookup returns
ENOENT. The `selflit=-2` in the HW log additionally rules out the flat-name `portRegister`
fallback (`file.c:531-548`) — had that fired, the kernel full-string dcache lookup of
`"/dev/netsocket"` (`name.c:284`) would HIT, not return -2. So the node definitively reached the
devfs named port and the lookup definitively reached the same server, yet HW says ENOENT.

The failure is therefore **dynamic**, not a static source defect: a runtime divergence (separate
instance / pid / port, a timing/ordering window, or a kernel-resolution detail) that source reading
cannot decide. **Writing a speculative dummyfs change would be a blind, self-unverifiable edit to
the root+devfs server shared by every variant — exactly the cross-cutting regression to avoid.**
This matches the prior verdict (`T3-devfs-resolution-verdict.md`): "Gap B is not resolvable by
source reading; one instrumented boot must settle it."

## What was changed (contained, additive, revertable): server-side DIAG only

The existing DIAG is all **client-side** (lwip's self-probe in `sockets.c:1247-1258`; nfs probes).
Nothing instrumented the **server**, which is the exact unknown. Added two prints in
`dummyfs/srv.c`, filtered to the name `"netsocket"` so normal boots stay quiet:

- **mtCreate** (`srv.c:308` block): `dummyfs: DIAG create netsocket: pid=%d port=%u dir.id=%llu
  rc=%d -> node.port=%u node.id=%llu`
- **mtLookup** (`srv.c:335` block): `dummyfs: DIAG lookup netsocket: pid=%d port=%u dir.id=%llu
  rc=%d`

These add **no** behavioral change (pure logging after the handler returns) and fire only for the
`netsocket` name. On the next boot they localize Gap B definitively:
- **Same** `pid`+`port`+`dir.id` on create and lookup, but lookup `rc<0` → same-instance
  create-vs-find bug (then a real, locatable dummyfs fix becomes possible).
- **Different** `pid`/`port` between create and lookup → separate-instance / ordering problem
  (lwip's create reached a different devfs than the kernel forwards the lookup to) → design-A.
- **No** server-side lookup print at all (but a create print) → the kernel never forwarded the
  lookup to this server (kernel-resolution issue, not dummyfs).

## RECOMMENDATION: design-A (RAM "/" first → NFS takeover) as the primary path.

Per `T3-devfs-resolution-verdict.md` Q3, design-A sidesteps Gap B by construction (it never
exercises the failing pre-"/" devfs-reachability path) and reproduces the proven netboot ordering.
It remains the only path defensible today. IF the next instrumented boot shows Gap B is a
same-instance dummyfs bug (per the DIAG decision table above), the cheaper devfs-server-fix becomes
viable — but that cannot be asserted now.

## Build status
`./scripts/rebuild-rpi4b-fast.sh --scope core --variant nfsroot` — clean. DIAG strings confirmed in
the shipped image (`strings loader.disk | grep "DIAG .* netsocket"` → both present), so the build is
not stale.

## Regression note (dummyfs is cross-cutting)
dummyfs is the root fs AND devfs for **every** variant. The orchestrator MUST regression-test a
normal `--variant netboot` boot in addition to `nfsroot`. The DIAG is name-filtered to `netsocket`,
which IS create_dev'd on netboot too (by lwip), so expect the two prints to also appear on a normal
netboot boot — confirm they are harmless there (boot still reaches `(psh)%`, 0 faults).

## Next-boot UART markers the orchestrator checks
- **nfsroot:** `dummyfs: DIAG create netsocket: ...` and `dummyfs: DIAG lookup netsocket: ...`
  (compare pid/port/dir.id/rc per the decision table) → `lwip: netsocket create_dev rc=0 ...
  selfdevfs=0` would mean fixed → then `nfs-fs: registered / (root mode)` → psh.
- **netboot regression:** boot reaches `(psh)%`, 0 faults; the two DIAG lines are inert.

## Evidence index
- dummyfs root object (id=0) built in `-N` mode: `dummyfs/dummyfs.c:1166-1234` (esp. :1184-1185),
  `dummyfs/srv.c:219-247`.
- create path: `libphoenix/unistd/file.c:512-642`; server `dummyfs/dummyfs.c:702-774`,
  `dir.c:110-178`.
- lookup path: kernel `phoenix-rtos-kernel/proc/name.c:224-434` (devfs fast-path :262-275, walk
  :343-359, bailout :361); server `dummyfs/dummyfs.c:49-139`, `dir.c:78-91`.
- DIAG added: `dummyfs/srv.c` mtCreate `:308`-block, mtLookup `:335`-block.
- HW evidence: `T3-attempt-1-result.md` UPDATE 2; prior verdict `T3-devfs-resolution-verdict.md`.
