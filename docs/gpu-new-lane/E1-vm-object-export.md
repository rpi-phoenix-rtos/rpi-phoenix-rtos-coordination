# E1 — kernel memory export: server-owned pages under an oid, zero-copy `mmap(fd)`

**Question** (research §4.4 / §6): can the kernel expose pages a server owns under an oid so
another process's `mmap(fd)` maps the *same* pages, refcounted (pages live until the last mapping
and the exporter's reference are gone), with one memory type per object enforced on every
mapping? And how does a client get an fd for such an oid?

**Status (2026-09-26):** prototype written, uncommitted, in the kernel and libphoenix working
trees; every touched C file `syntax-check.sh` CLEAN; probe compiles `-Werror` against the source
header. **No Pi cycle yet** — the test plan below is pre-registered.

⚠ **Read §6 first.** While building this, a **pre-existing kernel bug** turned up: freeing any
`MAP_CONTIGUOUS` object wipes the kernel's entire vm object tree. The prototype fixes it, because
the probe (and any exporter) frees contiguous objects — so the E1 kernel is **not purely additive**.

---

## 1. Interface

Two syscalls, appended after `sys_fdpath` (`include/syscalls.h:135-136`). libphoenix gets their
stubs automatically — `arch/*/syscalls.S` expands the kernel's `SYSCALLS()` list — so the only
libphoenix edit is the prototypes in `include/sys/mman.h:69-86`.

```c
int memExport(const oid_t *oid, void *vaddr, size_t size);   /* 0 or -errno */
int memUnexport(const oid_t *oid);
```

* `memExport` publishes the page-aligned range `[vaddr, vaddr+size)` of the caller's
  `MAP_ANONYMOUS | MAP_CONTIGUOUS` mapping under `oid`. `oid.port` must be a port the caller owns
  (`-EPERM` otherwise). The range may be any page-aligned **sub-range** of the mapping, so one big
  contiguous pool can be exported as many buffers (the `rpi4-kms` dumb-BO pool of §4.4).
  `-EINVAL`: not page aligned, not inside one entry, not contiguous-anonymous memory, or pending
  COW; `-EEXIST`: something already lives under `oid`.
* `memUnexport` withdraws it (`-ENOENT` if nothing is exported under `oid`; never touches a file
  object that happens to share the oid).
* Import is plain POSIX: open a descriptor whose oid is the export's, then `mmap()` it with the
  export's memory type. A descriptor passes over AF_UNIX `SCM_RIGHTS` unchanged (`posix/fdpass.c`
  is untouched — it packs the `open_file_t`).

### How a client gets the fd — decided: **server-resolved path**

The exporter serves a namespace on its port (`portRegister(port, "/gpubuf", …)`), answering
`mtLookup` (`o.lookup.fil = o.lookup.dev = {port, id}`, `o.err = strlen(name)`), `mtGetAttr
atMode`, `mtOpen`/`mtClose` (reply **0** — a positive `mtOpen` reply is taken as a multiplexer id
and rewrites the descriptor's oid, `posix/posix.c:921-928`) and **refuses `atSize`** (see
"shadow objects" in §3). `open("/gpubuf/7")` → fd → `mmap(fd)`.

Why not a "wrap this oid into an fd" call: it needs new kernel code in the posix layer; without a
round-trip to the owner it is a capability-forging primitive (ports and ids are small integers,
trivially guessed); with the round-trip (`mtOpen`) it saves only the lookup — and the exporter must
still serve `mtClose` for every close. The path costs **zero kernel code**, keeps authorisation
where every Phoenix device keeps it (the server's `mtLookup`/`mtOpen`), and is what libdrm's
`drmPrimeHandleToFD` becomes: an export ioctl to the server + `open()` of the returned name.

**Namespace caveat for M3** (from reading libphoenix, not yet measured): `_resolve_abspath`
(`unistd/dir.c`) issues `mtGetAttr(atMode)` on **every intermediate component**, so
`/dev/dri/buf/7` fails at `/dev/dri` unless devfs knows `dri`. Register a top-level name
(`/gpubuf`) or give devfs the directory. `open(O_RDWR)` additionally `stat()`s (`mtGetAttrAll`);
the probe opens `O_RDONLY` (the kernel's `mmap` does not check the descriptor's access mode —
pre-existing).

## 2. Design as implemented

**Export window.** `vm_objectExport()` (`vm/object.c:574`) creates a new `vm_object_t` that
*borrows* the pages of the source object: `pages[i] = parent->pages[offs/PAGE + i]`, holds one
reference on the parent, records `memtype = entry flags & (MAP_UNCACHED|MAP_DEVICE)`, and is
inserted into the **existing** object tree under `oid` (`lib_rbInsert` → `-EEXIST` on collision)
and onto a list of published exports. Flags live in a padding hole of `vm_object_t`; the struct
grows by three pointers (`parent`, `next`, `prev`) — `vm/object.h:25-45` (struct at 25-39).

The source range is found by `vm_mapObjectRange()` (`vm/map.c:721`, modelled on `vm_mapFlags`):
one entry must cover it, map object pages directly (`amap == NULL`, no `MAP_NEEDSCOPY`), and the
object must be anonymous-contiguous (`oid == {-1,-1}`): all pages present, owned by that object
alone, never refetched or replaced. File objects (a refetchable cache), `VM_OBJ_PHYSMEM` (no page
ownership) and amap memory (`page_t` has no refcount) are refused.

**Import / fault path — unchanged code.** `sys_mmap` → `posix_getOid()` → `vm_objectGet()` finds
the window in the tree (`vm/object.c:85`) and takes a reference → `vm_mmap` → `_map_map` stores it
in the entry → `_map_force`/fault → `vm_objectPage()` returns `o->pages[offs/PAGE]`, which is
never NULL, so `object_fetchCluster()`/`proc_read` is never reached. The fault path is
byte-for-byte unchanged (copying the page pointers into the window is what buys that).

**Memory type.** `vm_objectMapCheck()` (`vm/object.c:697`) requires `flags & (MAP_UNCACHED|
MAP_DEVICE) == memtype` and `offs + size <= window size`. It is enforced in `_vm_mmap()`
(`vm/map.c:622`, before the `MAP_FIXED` unmap so a refused call destroys nothing — covers every
kernel caller, e.g. exec) and checked earlier in `sys_mmap` (`syscalls.c:115`) only to return a
precise `-EINVAL` instead of `-ENOMEM`. Every other path that creates a mapping of an object copies
an existing entry's flags (fork, split, merge), and `mprotect` does not change cache attributes.
So a page of an export is never cached in one mapping and uncached in another (the stale-dirty-line
lesson, `done/2026-09-04-uncached-page-stale-cache-rootcause.md`); the uncached-map clean+invalidate
in `_pmap_cacheOpAfterChange` still runs per mapping and is harmless on pages that only ever had
NC mappings. Supported types: **cached** and **`MAP_UNCACHED`** (aarch64 Normal-NC = write-combine,
what V3D BOs use today). A device-type (`MAP_DEVICE`) export is recorded but unreachable:
`sys_mmap` refuses `MAP_DEVICE` without `MAP_PHYSMEM` up front (`syscalls.c:84`); RAM buffers do
not need Device-nGnRE.

**Lifetime.**

| holder | reference on |
|---|---|
| the export (published) | window (1) |
| every mapping (any process) | window |
| the window | parent (1) |
| the exporter's own mapping | parent |

Pages are freed only when the export is withdrawn **and** the last mapping is gone
(`vm_objectPut`, `vm/object.c:158`: window → put parent → parent frees its buddy block). Recursion
depth is 1 (a window's parent is never a window). Withdrawal (`_object_unpublish`,
`vm/object.c:42`) removes the window from the tree **immediately**, so a withdrawn or dead
exporter's oid can never resolve to stale memory; existing mappings are unaffected.

**Exporter death / port release.** `port_put()` calls `vm_objectUnexportPort()` when the port's
last reference goes, *before* `lib_idtreeRemove` makes the id reusable (`proc/ports.c:104`;
`vm/object.c:660`). No export can be in flight then: `memExport` holds a port reference across the
insert (`syscalls.c:152`). With nothing exported the hook is one lock/unlock. When it does withdraw
something it prints the one distinctive kernel line —
`vm: port <n> released with <k> memory export(s) still published, withdrawn` — which is also the
`strings loader.disk` stale-core marker.

**Fork.** Mappings of a window, and of a parent that has been exported from (sticky
`VM_OBJ_SHARED`), are **shared across fork, not COW** (`vm/map.c:1418`, `vm_objectShared()`): a
COW copy would silently detach a process from pages other processes and devices keep using. Every
other entry keeps today's behaviour.

## 3. Traps designed around

* **Shadow objects.** If an importer mmaps `{port,id}` before the export exists, `vm_objectGet`
  misses and asks the server `proc_size()`. If the server answered with a size, a file-backed object
  with NULL pages would be inserted: the export then fails `-EEXIST` and the importer silently gets
  *copies* via `proc_read`. Rule: an exporter refuses `atSize` for its buffer names (the probe
  does), and exports before announcing an id.
* **Contiguous-object free path.** `vm_objectPut` identifies contiguous objects by their oid
  sentinel and frees only `pages[0]` (the whole block). Windows keep their parent's sentinel
  untouched, so a window never falls into the per-page free branch.
* **Never-inserted nodes** — §6.

## 4. Rejected alternatives

| alternative | why not |
|---|---|
| new `mmap` flag (`MAP_EXPORT`) | `vm_flags_t` is `u8` and all 8 bits are taken (`include/mman.h`); `sys_mmap` truncates `sflags` to it; `mmap` has no way to carry "under this oid" |
| `platformctl` | per-arch HAL dispatch — wrong layer for a generic vm facility |
| message to a kernel port (`mtCreate`-like) | the kernel serves no port; adding one is far larger than two syscalls |
| kernel allocates the pages ("create", not "export") | no pool sub-allocation (every BO power-of-two `MAP_CONTIGUOUS` again), and the exporter then needs an fd for its own object (self-send deadlock if it opens its own path from its serving thread) or a map-into-caller variant of `mmap` |
| re-key the exporter's contiguous object itself | exports the whole power-of-two object only, no sub-ranges, and mutates the identity (oid) of an object the exporter's entries already point at; a window is one pointer and a page array more |
| export amap (plain anonymous) memory | `page_t` has no refcount; would mean stealing anons out of amaps — invasive, not additive |
| oid→fd syscall | §1 |
| page-array window over the parent without copying pointers | adds a branch to `vm_objectPage`, the hottest vm path; the copy costs 8 B/page |

## 5. Pre-registered Pi test plan (coordinator)

0. **Order.** E1 depends on the §6 fix, so gate them in order: the one-hunk §6 fix alone → stock
   gate (and, if wanted, the C1 A/B) → E1 on top → gate → probe.
1. **Build** `--scope core` (a core change: `auto` would ship a stale image). Verify it is in:
   `strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep "memory export(s) still published"`
   and `aarch64-phoenix-nm $S/lib/libphoenix.a | grep -w memExport` (a `T` line). If the kernel
   string is there but the stub is not, libphoenix reused a cached `syscalls.o` (the stubs come from
   the *installed* `<phoenix/syscalls.h>`, a dependency the build may not track): touch
   `sources/libphoenix/arch/aarch64/syscalls.S` and rebuild `--scope core`.
   **Stale-binary census:** `scripts/check-stale-binaries.sh` will report every prebuilt port as
   older than the new `libphoenix.a`. That is expected and benign here — no existing syscall changed
   number or arity, only two were appended — so it does **not** call for `--scope full-clean`.
2. **Stock regression gate** on that image: the five-game + X desktop gate, unchanged. Existing
   callers never reach the new code — *except* the §6 fix and the struct growth (kernel-heap
   layout). A red gate here is attributed to those two, in that order.
3. **Probe**: build `tools/gpu-lane/exportprobe` per its README against the tree sysroot, stage it,
   run `exportprobe` once at the psh prompt (rpi4-run recipe, ~180 s capture is plenty). Then run
   it a **second time in the same boot** (port-id reuse, leftovers from run 1).
4. **Grade** by the tagged lines only: `EXPORTPROBE RESULT … verdict=PASS`, exactly **one**
   `vm: port … withdrawn` line per run, `free_delta_a`/`_c` ≈ 1048576, `free_delta_b` ≈ 0, and no
   kernel or EL0 fault dump. `kernel_has_export=0` means a stale image — not a result.

| outcome | meaning | consequence for M3 |
|---|---|---|
| all keys 1, both runs | E1 answered **yes** | productise (§7) |
| `import=0` | namespace/lookup (probe server) or window not found | debug the probe first, then `vm_objectGet` |
| `same_pa=0` | importer mapped a different object (shadow object, or the tree lost the window — §6) | blocker |
| `xwrite_*_cached=0` only | cache attributes / shareability wrong on one side | blocker for cached BOs only |
| `xwrite_*_uncached=0` | NC attribute not applied, or the map-time flush corrupts | blocker |
| `memtype_refused_*=0`, `refuse_*=0` | enforcement bypassed | must fix before any use |
| `partial_map_same_pa=0` | sub-range mapping of an export broken | pool sub-allocation (kms dumb BOs) blocked |
| `scm_rights_same_pa=0`, all else 1 | the facility works; fd passing over a `SOCK_STREAM` socketpair is in question (never exercised here before) | not an E1 failure: M3 verifies fdpass on stream sockets or uses `SOCK_DGRAM` |
| `fork_shared=0` | `vm_mapCopy` rule ineffective | clients must not fork; fix |
| `survives_unexport=0`, `survives_exporter_exit=0`, fault at final put | refcount / free ordering (window→parent) | blocker |
| `reimport_refused=0` | withdrawn window still reachable, or shadow object created | blocker (stale-memory exposure) |
| `held_b=0` | pages freed while the exporter still maps them | **stop** — use-after-free class |
| `released_a=0` / `released_c=0` / no "withdrawn" line | leak: window/parent not freed, or port-release hook not firing | exporters leak on crash; fix before M3 |
| any kernel fault | stop, `addr2line` the PC | — |

## 6. Pre-existing kernel bug found: freeing a `MAP_CONTIGUOUS` object empties the object tree

`vm_objectContiguous()` zeroes the object and **never inserts it** into the object tree.
`vm_objectPut()` (upstream too, unchanged since 2021) calls `lib_rbRemove(&object_common.tree, …)`
unconditionally at refs==0. On a never-inserted, zeroed node `lib_rbRemove` takes the
`z->left == NULL` branch → `rb_transplant(tree, z, NULL)` → `z->parent == NULL` →
**`tree->root = NULL`**; the node's colour is 0 = `RB_RED`, so no rebalance follows.
**Confirmed on the host** by compiling the kernel's own `lib/rb.c`: 16 inserted nodes, remove one
zeroed never-inserted node → root NULL, 0 nodes reachable, lookups fail.

So every last `munmap` of a `MAP_CONTIGUOUS` buffer (every GPU BO free, and any failed contiguous
`mmap`) drops **every** vm object — the page caches of all mapped binaries and libraries — from
the tree. Consequences by reading the code (not yet measured on the Pi): later opens create
duplicate objects for the same file (re-read over NFS/SD, extra memory); when an orphaned object is
later removed, `lib_rbRemove`/`lib_rbRemoveBalance` run inside a structure that is no longer the
tree, can re-point `tree->root` into the orphans (orphaning the live objects in turn), and can
dereference a NULL parent at the orphan root (an EL1 fault near `far=0x8`).

The prototype's fix (`vm/object.c:173-187`): remove a node only if it is in the tree — not for
contiguous objects, and for export windows only while published. **This changes behaviour for an
existing caller**, so:

* it should land as **its own commit**, separately gated, not folded into E1. Minimal standalone
  form (what the prototype's larger hunk reduces to without export windows):

  ```c
  /* vm/object.c, vm_objectPut(): a contiguous object was never inserted into the tree */
  if (!((o->oid.port == (u32)(-1)) && (o->oid.id == (id_t)(-1)))) {
  	lib_rbRemove(&object_common.tree, &o->linkage);
  }
  ```

* E1 depends on it (the probe frees three contiguous objects; without the fix each free empties the
  tree, possibly while windows are published);
* whether it bears on **C1** is *untested speculation*: GPU BO churn is exactly what empties the
  tree, and a cold shader cache means more BO churn. It is cheap to test: A/B the one-hunk fix alone
  against stock on the C1 bench, graded by the cycle's own `cleared`/`KEPT` line. Note the §7 layout
  caveat — measure the fix *without* the rest of E1.

## 7. Residual risks

* **Kernel-heap layout.** `vm_object_t` grows by 24 B, shifting kernel kmalloc layout. C1 is
  layout-sensitive — do not compare C1 rates between an E1 kernel and stock.
* **Device writes after free (C1 rule).** The kernel frees pages at the last reference; it cannot
  see DMA. A server must keep the export (or a mapping) until the device is idle. If the server
  **crashes** with a V3D job or a scanout in flight, the port-release hook withdraws its exports and
  the pages can be freed while the device still writes (V3D) or reads (HVS) them. Today's lane has
  the same hazard with `MAP_CONTIGUOUS` BOs. M3 needs a policy (quiesce the device on server exit;
  or a device-owner reference that outlives the process) — not solvable in the vm layer.
* **Fork around export.** An entry forked *before* export carries `MAP_NEEDSCOPY` → export refused
  (explicit `-EINVAL`, fine). A multi-threaded exporter forking *concurrently* with `memExport` can
  leave the exporter's entry COW (the shared flag is set after the map lock is dropped) — exporters
  should not fork while exporting, or map pools `MAP_NOINHERIT`. `VM_OBJ_SHARED` is sticky on the
  parent after its windows are gone.
* **`MAP_PHYSMEM` hole.** Any process can still map an export's physical address with any memory
  type. The facility makes it possible to retire `MAP_PHYSMEM` for non-drivers; it does not do so.
* **Lock order.** The port-release hook runs under `port_common.port_lock` and may free pages and
  print: port lock → object lock → page/kmalloc locks. No path taking the object lock calls into
  ports (`vm_objectGet`/`vm_objectPage` drop it before `proc_size`/fetch); audited by reading only.
* **Contiguous only.** Non-contiguous export needs a kernel-allocated page-array object (a follow-up
  call; not a new `MAP_*` bit — none are free). Windows of windows are refused (a client passes the
  fd on instead of re-exporting).
* **Syscall numbering.** Appended after our own `sys_fdpath`. If an upstream sweep appends syscalls,
  ours must move after theirs (TD-21 policy) — that renumbers `memExport`/`memUnexport` and breaks
  only binaries that call them (new-lane servers/probes), which must then be rebuilt. No arity
  change to any existing syscall. Standalone tools linking the toolchain's `libphoenix.a` fail to
  link these symbols (loud, not silent) until that copy is re-synced.
* **NOMMU:** both syscalls return `-ENOSYS`.
* **Tests:** the probe is the test for now; a `phoenix-rtos-tests` case should follow when M3
  productises the interface (owner rule: libphoenix tests).

## 8. Files (uncommitted)

| file | change |
|---|---|
| `sources/phoenix-rtos-kernel/include/syscalls.h` | `ID(memExport) ID(memUnexport)` appended |
| `…/vm/object.h` | `flags`/`memtype` (padding hole), `parent`/`next`/`prev`; `VM_OBJ_*`; 5 prototypes |
| `…/vm/object.c` | `object_isContiguous`, `_object_unpublish`, `vm_objectExport`, `vm_objectUnexport`, `vm_objectUnexportPort`, `vm_objectMapCheck`, `vm_objectShared`; `vm_objectGet` field init; `vm_objectPut` tree fix (§6) + window release |
| `…/vm/map.c`, `vm/map.h` | `vm_mapObjectRange`; memtype check in `_vm_mmap`; shared-on-fork in `vm_mapCopy` |
| `…/syscalls.c` | `syscalls_memExport`, `syscalls_memUnexport`, `syscalls_portOwned`; precise `-EINVAL` in `sys_mmap` |
| `…/proc/ports.c` | withdraw a port's exports before its id is released |
| `sources/libphoenix/include/sys/mman.h` | prototypes + contract |
| `tools/gpu-lane/exportprobe/` | the probe + README |

## Result

*(to be filled in after the Pi cycle)*
