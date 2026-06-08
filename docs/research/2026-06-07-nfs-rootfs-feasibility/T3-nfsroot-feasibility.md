# T3 — NFS-as-rootfs feasibility (OQ-A: sockets + lease before "/")

**Scope:** read-only source investigation. De-risks #153 T3 (make "/" itself an NFS
mount, then build/run phoenix-rtos-ports from it). No build, no HW, no source changes.
All file:line citations are against the siblings under `sources/` as of this session.

---

## TL;DR verdict

**YES — a userspace process can obtain BSD sockets and read DHCP/interface status
before "/" exists, _with two small, well-precedented changes_.** The blocker is NOT
a missing capability; it is that two *client-side resolvers* hardcode literal
`/dev/...` paths (`socksrvcall` → `lookup("/dev/netsocket")`; the DHCP-wait →
`fopen("/dev/ifstatus")`), and literal absolute paths cannot resolve before the root
fs registers "/". The **device nodes themselves already exist pre-"/"** (every lwip
node is created via `create_dev`, which targets the `devfs` *named port* and strips
`/dev/`), so the fix is to reach them the same way the SD ext2-root driver reaches
`/dev/mmcblk0pN` pre-bind: the `devfs/<name>` named-port lookup fallback
(`flashsrv.c:61` `flash_oidResolve`).

The mechanism that makes this work-or-fail is one function:

- **`proc_portLookup`** (`phoenix-rtos-kernel/proc/name.c:227`):
  - A literal `/`-prefixed path with **no root registered** strips down to `i==0`
    and returns **`-ENOENT`** (`name.c:361-369`). → literal `/dev/netsocket` **fails
    pre-"/"**.
  - A **`devfs/<name>`** name: the prefix-strip loop (`name.c:343-359`) finds the
    cached `devfs` dcache entry, sets `srv` = devfs port, and sends `mtLookup` for the
    remainder to the devfs server (`name.c:386-420`) — **no root server involved**. →
    `devfs/netsocket` / `devfs/ifstatus` **resolve pre-"/"**. ✅

So: **literal `/dev/X` = NO pre-"/"; `devfs/X` via raw `lookup()` = YES pre-"/".**

---

## 1. Sockets before "/" — definitive trace

**Path of a socket() call:**
`socket()` (`libphoenix/include/sys/socket.h`) → kernel syscall
`syscalls_sys_socket` (`syscalls.c:1713`) → `posix_socket` (`posix/posix.c:1758`) →
for AF_INET, `inet_socket` (`posix/inet.c:269`) → `socksrvcall(&msg)` (`inet.c:281`).

`socksrvcall` is the single client-side resolver. **In libphoenix** the same-named
helper lives at `libphoenix/sys/socket.c:56`; it does
`lookup(PATH_SOCKSRV, NULL, &oid)` then `msgSend(oid.port, msg)` (`socket.c:61-63`).
`PATH_SOCKSRV` is the **literal** `"/dev/netsocket"`
(`phoenix-rtos-kernel/include/sockport.h:35`). All three `socksrvcall` sites in
libphoenix funnel through that one `lookup` (`socket.c:61`, called from `:288/:367/:459`).

**Server side — the node already exists pre-"/":** lwip registers the socket server
with `create_dev(&oid, PATH_SOCKSRV)` (`phoenix-rtos-lwip/port/sockets.c:1246`).
`create_dev` (`libphoenix/unistd/file.c:512`) resolves the **`devfs` named port**
(`lookup("devfs", ...)`, `file.c:523`), strips the `/dev` prefix (`file.c:559-560`),
and `mtCreate`s the node **inside the devfs server** (`file.c:623-637`). So the node
exists in devfs as `netsocket` regardless of whether "/" or the `/dev` bind exist —
the **same** mechanism the boot comment in `user.plo.yaml:17-20` describes.

**The asymmetry:** the *server* reaches devfs by named port (works pre-"/"); the
*client* (`socksrvcall`) reaches it by literal `/dev/netsocket` (fails pre-"/", per
`proc_portLookup` above). Once "/" + the `/dev` bind are up (today the NFS server runs
at `user.plo.yaml:91`, after the bind at `:33`), the literal lookup works — which is
why sockets work for the current `/nfstest` (non-root) deployment. **Pre-"/" they do
not, until the client uses the `devfs/` fallback.**

**Cross-process is not a concern** (confirmed in `02-phoenix-socket-api.md`):
`inet_socket` returns the lwip server's **port** (`inet.c:286`), stored in the
caller's fd; all subsequent ops are generic IPC to that port. Any process that can
*resolve the socket server's oid* can open sockets — there is no "must be the lwip
process" restriction. The only gate is the name resolution, which is what this doc
fixes.

### Required change (sockets) — additive, mirrors `create_dev`

`socksrvcall` (`libphoenix/sys/socket.c:56-66`) must fall back to the devfs named port
when the literal lookup fails, exactly like `flash_oidResolve` (`flashsrv.c:61-86`):

```c
static int socksrvcall(msg_t *msg) {
    oid_t oid;
    int err;
    if (lookup(PATH_SOCKSRV, NULL, &oid) < 0) {
        /* "/" not up yet (NFS-root early boot): reach the node via the
         * devfs named port, stripping the /dev/ prefix — same as create_dev
         * and flashsrv_oidResolve. */
        if ((err = lookup("devfs/netsocket", NULL, &oid)) < 0)
            return SET_ERRNO(err);
    }
    if ((err = msgSend(oid.port, msg)) < 0)
        return SET_ERRNO(err);
    return 0;
}
```

This is **harmless in every existing configuration**: it only fires when the literal
lookup fails (i.e. pre-"/"). It is a shared-libphoenix change but a small, defensible,
upstreamable one (the literal/named-port duality is already an established Phoenix
pattern). libnfs opens its socket *during `nfs_mount`* via this exact path, so with
this fallback in place libnfs gets its TCP socket to `host:2049` before "/" exists.

---

## 2. DHCP-wait before "/" — `/dev/ifstatus` is NOT reachable via `fopen`

The current DHCP-wait (`nfs/srv.c:98-164` `wait_for_dhcp_lease`, and the identical
`nfs-smoke.c:88`) uses **`fopen("/dev/ifstatus", "r")`** (`srv.c:102`). Unlike the raw
`lookup()` syscall, **`fopen`/`open` route through `resolve_path`**
(`libphoenix/unistd/file.c:369,391`), and `resolve_path` (`unistd/dir.c:282`):
- for a non-`/`-prefixed path calls `getcwd` (`dir.c:304-313`) — needs "/";
- for a `/`-prefixed path produces a canonical path that is then looked up through the
  root chain — needs "/" too.

So **`fopen("/dev/ifstatus")` cannot work pre-"/", and you cannot fix it by rewriting
the string to `"devfs/ifstatus"`** — `fopen` would still canonicalize via cwd/root.
Only the raw `lookup("devfs/ifstatus", ...)` + manual `mtRead` message I/O reaches the
node pre-"/". (This is precisely why `flashsrv.c:89` comments "resolve_path would fail"
and does raw `lookup` + msg I/O, never `fopen`.)

**Decision for root mode (recommended): drop the ifstatus read entirely; use a
bounded `nfs_mount` retry.** The server already sets `nfs_set_timeout(5000)`
(`srv.c:339`), so each `nfs_mount` attempt is bounded. In root mode, loop
`nfs_mount` (re-init context on failure) until it succeeds or a deadline (e.g. 60 s)
expires — DHCP completing is observed indirectly by the mount succeeding. This avoids
the `fopen`-pre-"/" problem completely and is the plan's "bounded mount retry" note.

(Alternative, heavier: port `wait_for_dhcp_lease` to raw `lookup("devfs/ifstatus")` +
manual `mtRead` against the returned oid, parsing the same `%s%d_ip=` buffer that
`devs.c:286 ifstatus_read` serves. Only do this if the bounded-retry mount proves too
slow/noisy; it duplicates ifstatus parsing against the named port.)

---

## 3. Boot-reorder recipe — the `nfsroot` variant

Today's order (`_projects/aarch64a72-generic-rpi4b/user.plo.yaml`):
`dummyfs-root` owns "/" (`:16`, netboot) → `dummyfs;-N;devfs;-D` named port (`:21`) →
`pl011-tty` (`:22`) → [sd: `bcm2711-emmc -r ... :ext2` owns "/" (`:28`)] →
`mkdir /dev /nfstest` (`:32`) → `bind devfs /dev` (`:33`) → `posixsrv` (`:37`) →
thermal/hwrng/fb/gpio/usb (`:48-77`) → **`lwip;genet` (`:80`) ← network up here** →
nfs-smoke (`:84`) → `nfs;/nfstest;...` splice (`:91`) → psh (`:92`) → `go!`.

The **SD precedent** (`:23-28` + `name.c` + `sdstorage_srv.c:288-328`) proves a server
can mount its real root and `portRegister("/")` *before* mkdir/bind/posixsrv, with
**dummyfs-root skipped** so there is no -EEXIST conflict. The `nfsroot` variant is the
network analogue. Verified lwip has **no "/" dependency** at startup: `main`
(`lwip/port/main.c:73-153`) only does `create_dev`-via-devfs (`devs.c:598-635`) +
netif init — no `fopen` of config, no posixsrv need.

**`nfsroot` ordering (new `RPI4B_VARIANT == 'nfsroot'`):**

```
1. kernel + dtb                                         (unchanged, :6-8)
2. dummyfs;-N;devfs;-D       → devfs NAMED PORT          (MOVE earlier; :21)
3. pl011-tty                                            (:22)
4. lwip;genet:...            → NETWORK up (DHCP async)   (MOVE here, BEFORE root)
5. nfs (ROOT MODE)           → bounded nfs_mount retry,  (NEW: portRegister("/"))
                               then portRegister("/")
   --- fallback: dummyfs-root here if NFS failed (see §4) ---
6. mkdir /dev /nfstest                                   (:32, now after "/")
7. bind devfs /dev                                       (:33)
8. posixsrv                                              (:37)
9. thermal/hwrng/fb/gpio/usb ...                         (:48-77)
10. psh                                                  (:92)
11. go!
```

Key points:
- **dummyfs-root is SKIPPED** in `nfsroot` (same gate as the sd variant,
  `user.plo.yaml:15`), so the NFS server's `portRegister("/")` (`name.c:117-133`)
  succeeds with no -EEXIST.
- **devfs named port and lwip both come up before the NFS root server**, so the NFS
  server can (a) open sockets via the `devfs/netsocket` fallback (§1) and (b) bounded-
  retry the mount until DHCP lands (§2) — all pre-"/".
- **No `/dev` bind before the NFS root server**, exactly mirroring the sd flow (the sd
  driver also create_dev's via the named port before any bind). mkdir/bind/posixsrv
  move *after* the root is registered.
- The `/nfstest` splice line (`:91`) and nfs-smoke (`:84`) are dropped in `nfsroot`
  (the root IS the NFS export; no secondary mount).

### NFS-server code changes for root mode

The server (`phoenix-rtos-filesystems/nfs/srv.c`) currently *refuses* "/"
(`srv.c:321-324`). Add a root mode (e.g. argv `nfs / <server> <export> v4 root`, or
a `-R` flag):

1. **Accept "/" as mountpoint** — gate the current refusal (`srv.c:321-324`) behind
   "not root mode".
2. **Socket fallback** — covered by the libphoenix `socksrvcall` change (§1); no
   per-server code needed for sockets, but the server must be built/linked so that the
   patched libphoenix is in effect (it is the standard libc).
3. **DHCP wait** — in root mode skip `wait_for_dhcp_lease` (the `fopen` path, unusable
   pre-"/"); instead bounded-retry `nfs_init_context`+`nfs_mount` (§2) until success or
   deadline. Keep `nfs_set_timeout(5000)` (`srv.c:339`).
4. **Register "/" instead of splicing** — in root mode, after a successful mount,
   `portCreate(&port)` then `portRegister(port, "/", &root)` directly (the
   `mountpt == NULL` branch the dummyfs root uses, `dummyfs/srv.c:219-227`;
   `sdstorage_srv.c:324`). Do **not** start `nfs_mountThread` (the `mtSetAttr(atDev)`
   splice at `srv.c:170-212` is for the subtree case only — it waits for an *existing*
   "/", which would deadlock when we ARE "/").
5. **Keep the loop on its >=64 KB stack** (`srv.c:372`, #120 stack lesson) — unchanged.
6. **Self-root caveat:** the splice records `common.fs.parent` so ".." crosses back to
   the owning fs (`srv.c:193-201`). In root mode, "/" has no parent — set
   `common.fs.parent = {port, NFS_ROOTID}` (self), so ".." at "/" stays at "/" (POSIX
   root semantics). This replaces the splice's parent bookkeeping.

No new `mt*` handlers are required — the loop already implements every op (proven by
the `/nfstest` read+write+exec HW tests). The loader's `object_fetch` page-reads
(read-based, not mmap — `T3b-exec-fix-notes.md`) already work, so exec-from-root works.

---

## 4. Fallback — decide-before-register so a network/server outage can't brick boot

This is the **top risk** and the part with the least precedent. plo `app -x` launches
are **unconditional and sequential** (no "if previous failed" branch), and a process
cannot spawn dummyfs-root from "/" before "/" exists. Two viable designs; recommend (A).

### (A) NFS server owns the fallback (recommended — single decision point)

The NFS root server, on **mount failure within the deadline**, registers a **minimal
emergency root itself** so boot proceeds and the system is reachable (UART/psh) for
recovery — rather than leaving "/" unregistered (which bricks every "/"-dependent
service). Cleanest implementation: the NFS server, in root mode, **links the dummyfs
object store** (it is already dummyfs-shaped per `04-rootfs-ordering.md`) and, on mount
failure, calls the dummyfs root path (`portRegister("/")` + an empty RAM root) instead
of the NFS path. Single binary, single decision, no plo branching, deterministic.
- Pro: one decision point; "/" is *always* registered (NFS if reachable, RAM if not);
  netboot-recoverable and clean.
- Con: the NFS server gains a dummyfs dependency (acceptable — they are structurally
  the same server).

### (B) Two launches + portUnregister takeover (more moving parts)

`portUnregister("/")` is exposed to userspace (`libphoenix/sys/msg.c:28`) and resets
`rootRegistered` (`name.c:184-188`). Sequence: launch **dummyfs-root first** (safe RAM
"/" default, always succeeds), then launch the NFS root server which, on a *successful*
mount, does `portUnregister("/")` then `portRegister(port, "/", ...)` to take over.
- **Risk (must validate on HW):** the unregister→register window is not atomic; any
  lookup of "/" in between transiently `-ENOENT`s, and oids/fds already resolved
  against the old dummyfs root keep pointing at it. Also `/dev` (bound to the *old*
  root's namespace via `bind`) may need re-binding after takeover. **Do not ship (B)
  without confirming the takeover doesn't strand the `/dev` bind or in-flight fds.**
- dummyfs-root on a *late* `portRegister("/")` failure (-EEXIST) logs "can't mount as
  rootfs" and exits cleanly (`dummyfs/srv.c:223-226`) — so the *reverse* ordering
  (NFS-first, dummyfs-fallback-second) would have dummyfs exit harmlessly if NFS won.
  But that reverse ordering reintroduces the "is NFS up yet?" race that (A) avoids.

**Recommendation:** ship **(A)**. It makes the network a *soft* boot dependency
(NFS-down → RAM root + psh for recovery, never a brick) with a single deterministic
decision and no namespace-takeover hazard.

---

## Top risks (ranked)

1. **Fallback correctness (§4).** The one genuinely new mechanism. (A) is low-risk but
   adds a dummyfs link; (B) has an unvalidated unregister/re-bind hazard. Pick (A).
2. **libphoenix `socksrvcall` change is shared.** Small and additive (fires only on
   literal-lookup failure), but it ships to *every* Phoenix target. Frame as the
   established literal/named-port duality; guard so it is a pure fallback.
3. **Bounded-mount-retry timing.** DHCP discover→ack takes seconds; the retry deadline
   must exceed worst-case lease time (60 s suggested) or a slow DHCP server fails the
   boot into fallback unnecessarily.
4. **Exec-from-root throughput.** Per-page open/read/close RPCs over a single-threaded
   server are slow (noted in night doc for the 228 KB ELF). Tolerable for PoC; MT
   server (plan §8) is the eventual fix. Booting *userland* (psh + ports) entirely from
   NFS magnifies this — expect a slow first boot.
5. **`/dev` bind ordering.** mkdir/bind/posixsrv must move strictly *after* the NFS
   root registers "/", or they fail. The recipe (§3) places them correctly; a careless
   edit that leaves them early would brick the variant.

---

## Bottom line

- **Sockets + lease before "/": YES**, via the `devfs`-named-port lookup fallback that
  the SD ext2-root precedent already uses for `/dev/mmcblk0pN`. The capability exists;
  only two client-side literal-path resolvers need the fallback.
- **Changes:** (1) libphoenix `socksrvcall` devfs fallback (shared, additive); (2) NFS
  server root mode (accept "/", bounded-retry mount instead of `fopen` ifstatus,
  `portRegister("/")` instead of splice, self-parent); (3) `nfsroot` plo variant
  (devfs+lwip+nfs-root before mkdir/bind/posixsrv, dummyfs-root skipped); (4) fallback
  design (A): NFS server registers a minimal RAM root if mount fails.
- **Do (b) /nfstest first (done, HW-proven) → then this (c) NFS-as-root** as an
  attended boot-sequence change with the (A) fallback, since it makes the network a
  boot dependency.
