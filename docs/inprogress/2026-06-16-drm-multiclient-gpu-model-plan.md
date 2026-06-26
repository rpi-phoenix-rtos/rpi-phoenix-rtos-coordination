# A DRM-equivalent, multi-client GPU model for Phoenix-RTOS on the Pi 4 V3D

> **STATUS (2026-06-26): forward ARCHITECTURE design, still ACTIVE / unbuilt.** No central GPU
> arbiter server exists yet; the V3D is still driven per-process by the no-DRM single-client
> synchronous winsys (which GLQuake and vkQuake each use exclusively). This design remains the
> foundation for any future multi-client GPU sharing (accelerated X, concurrent GPU apps). No
> change since 2026-06-16; kept as the forward reference.

**Date:** 2026-06-16
**Type:** RESEARCH + ARCHITECTURE DESIGN (no source changes; design only)
**Scope:** Phoenix-RTOS RPi4 port (BCM2711, Cortex-A72, V3D 4.2). The arbitration
layer that lets multiple processes share the single V3D GPU safely, plus a path to
KMS-style display ownership.
**Companions (this doc is their foundation):**
[`2026-06-16-x11-accelerated-desktop-plan.md`](2026-06-16-x11-accelerated-desktop-plan.md)
(Part 3 names this no-DRM single-client model as THE blocker for accelerated X),
[`2026-06-16-vulkan-v3dv-vkquake-port-plan.md`](2026-06-16-vulkan-v3dv-vkquake-port-plan.md)
(the synchronous-submit/sync findings reused below).

---

## TL;DR verdict (read this first)

- **The right model is unambiguous: a central GPU server** (a Phoenix userspace
  message-passing server, the same shape as `rpi4-fb`) that **exclusively owns** the
  V3D MMIO, the single MMU page table, and the single CORE0 submit queue. Clients
  never touch MMIO; they send a small `msg` describing a command-list and block until
  the server has run it and responded. This is the **Fuchsia/Magma + genode model**,
  and it is the *natural* fit for a microkernel — far more so than trying to replicate
  Linux's in-kernel DRM (Phoenix has no kernel module surface to put a DRM driver in).
- **The hardware itself endorses this and rejects the alternatives.** The Broadcom V3D
  has **one** MMU base register (`MMU_PT_PA_BASE`). The Linux v3d driver
  (`external/linux/drivers/gpu/drm/v3d/v3d_mmu.c`, GPL — read for the *hardware
  contract only*) writes that register **exactly once at init** and loads **all BOs of
  all clients into one shared 4 GB GPU address space**, because "switching between
  [page tables] is expensive" and would force a full MMU/TLB flush per job. Per-client
  isolation (the V3D GMP, 128 KB-granularity masking) is, in Linux's own words, "not
  yet implemented." **So we adopt: one global GPU address space + one global VA
  allocator, owned by the server.** Per-submit PT-base swapping is explicitly *not*
  recommended — even Linux refuses it.
- **The honest tradeoff of that choice:** one global VA space = **no inter-client GPU
  memory isolation** — any client's command list can address any other client's BO by
  its `gpuva`. This is *identical to Linux-without-GMP* and acceptable for a trusted
  desktop/game workload; it is not a hardened multi-tenant boundary. Stated loudly
  because it is the headline cost.
- **The IPC overhead is near-zero and we can prove it from the `msg` ABI.** A
  `SUBMIT_CL` descriptor is ~7 × u32 = 28 bytes and **fits in the 64-byte inline
  `msg_t.i.raw[]`** (`sources/phoenix-rtos-kernel/include/msg.h:117`). BO *contents*
  (command lists, vertices, textures, render targets) live in shared **physical**
  memory and **never transit the kernel** — they are mapped into both server and
  client by physical address (`MAP_PHYSMEM`). **This cross-process mapping of
  *kernel-allocated* DRAM is confirmed possible from the kernel source** (not merely
  inferred from the framebuffer case): `vm_objectPage` for a `VM_OBJ_PHYSMEM` mapping
  calls `page_get(pa)` and maps the raw frame even when it returns NULL — i.e. *any*
  physical address, with no owner check (`vm/object.c:219-226`, `vm/map.c:772-776`).
  So a submit costs **one `msgSend` round-trip + a context
  switch** on top of the GPU spin the winsys already does. At 42–60 submits/second
  (one per frame) this is negligible against a 16–24 ms frame. The 42fps GL-Quake /
  60fps vkQuake targets are **not** threatened by the server hop.
- **Massive reuse:** the entire GPU-side logic already exists in
  `tools/v3d-driver-port/v3d_phoenix_winsys.c` (MMU bring-up, BO alloc, VA allocator,
  `ioc_submit_cl` with binner-OOM + SLCACTL/L2T cache maintenance). The work is **not**
  writing a GPU driver — it is **relocating** the existing winsys into a server process
  and putting a thin `msg` protocol in front of it. The client side becomes a tiny
  libdrm shim that forwards to the server instead of poking MMIO.
- **Display (KMS-equivalent):** the firmware owns HDMI (HVS/PixelValve). A real KMS is
  out of reach, but **double-buffered pageflip is reachable via the firmware mailbox
  pan tag** (`SET_VIRTUAL_OFFSET`, 0x00048009) over an oversized (2× height) virtual
  framebuffer — using the `mboxProp` mechanism already in `v3d_phoenix_power.c`. The
  weak spot is **vblank/tear-free vsync**, which the firmware pan may or may not honor
  (flagged VERIFY, not asserted).
- **Effort: ~6–8 person-months** to the full goal (X server + GL client sharing the GPU
  with no corruption, double-buffered). The **derisking core — server owns MMU + two
  clients submit concurrently without corruption (M0+M1) — is ~2 months**, because the
  winsys already exists and the IPC primitives are proven. **Top-3 risks:** (1)
  correctness of PT-mutation (BO alloc) *during* a concurrent submit; (2) firmware-fb
  pageflip/vsync; (3) `MAP_PHYSMEM` is unrestricted today, so "clients can't touch
  MMIO" is isolation-by-convention, not enforced.

---

## 0. Grounding — what exists today (every claim tied to a file)

| Thing | Where | State |
|---|---|---|
| The "GPU driver" = in-process userspace winsys | `tools/v3d-driver-port/v3d_phoenix_winsys.c` | **Single-client, no-DRM, synchronous.** `winsys_init()` powers on V3D, mmaps HUB/CORE0 MMIO @ `0xfec00000`, allocates ONE flat MMU PT, writes its PA to the ONE `MMU_PT_PA_BASE` reg (`:155`), submits via the ONE CORE0 queue (`ioc_submit_cl:338`) with spin-wait on FLDONE/FRDONE. State in a single global `W`. |
| V3D power-on | `tools/v3d-driver-port/v3d_phoenix_power.c` | BCM2711 mailbox + ASB bridge bring-up. Idempotent. Contains the `mboxProp` property-channel helper (reusable for the display pan tag). |
| libdrm shim (faked ioctls) | `tools/v3d-driver-port/v3d_libdrm_shim.c` | In-process `drmIoctl`→`phoenix_v3d_ioctl`; `drmSyncobj*` all trivially-signaled. |
| Display | `sources/phoenix-rtos-devices/video/rpi4-fb/rpi4-fb.c` → `/dev/fb0` | Firmware framebuffer, **no KMS/modeset**. Maps scanout DRAM by PA (`MAP_SHARED\|MAP_UNCACHED\|MAP_PHYSMEM`, `:79`). `RPI4FB_GETMODE` devctl returns `{w,h,bpp,pitch,smemlen,framebuffer}`. read()/write() = `memcpy` to/from scanout. |
| Phoenix IPC | `sources/phoenix-rtos-kernel/include/msg.h`, `sources/libphoenix/include/sys/msg.h` | `portCreate`/`msgSend`/`msgRecv`/`msgRespond`. `msg_t` carries a 64-byte inline `i.raw[]`/`o.raw[]` **plus** `i.data`/`o.data` pointers the kernel copies between address spaces. |
| Cross-process physical sharing | `sources/phoenix-rtos-kernel/include/mman.h:25-26` | `MAP_PHYSMEM` (map a known PA) + `MAP_CONTIGUOUS` + `MAP_UNCACHED`. **This is our DMA-BUF analogue.** Already used by `rpi4-fb` and the winsys. |
| Server skeleton to copy | `rpi4-fb.c:143` `fb_thread` | The canonical Phoenix device-server loop: `for(;;){ msgRecv; switch(msg.type){...}; msgRespond; }`. |
| HW contract reference (GPL — facts only) | `external/linux/drivers/gpu/drm/v3d/v3d_mmu.c` | One global 4 GB VA, PT base written once (`v3d_mmu_set_page_table:84`), isolation "not yet implemented". |

---

## 1. What "DRM" actually provides that we lack — decomposed into separable services

DRM is not one thing; it is five separable services. The "no DRM" gap is really five
gaps, each of which maps to a concrete Phoenix mechanism. (This mirrors §3.2 of the X11
companion, made concrete.)

| # | DRM service | What it does | What we lack today | Concrete Phoenix mechanism (this doc's design) |
|---|---|---|---|---|
| **(a)** | **GEM / BO management + per-client handle namespace** | Allocate GPU memory objects; give each client its own integer-handle namespace; refcount; free | Winsys has one global `bos[]` table + `handle = slot+1` (`ioc_create_bo:316`), shared by nobody (single process) | **Server owns the BO table.** Each client connection gets its **own handle namespace** (per-connection small-int → global BO id map in the server). BO backing = `MAP_PHYSMEM`-shareable contiguous DRAM (see (d)). |
| **(b)** | **Per-process GPU virtual address spaces** | Each client sees its own GPU VA range; MMU context-switch isolates them | V3D has **one** `MMU_PT_PA_BASE`; the winsys reprograms it on `winsys_init()` → second client corrupts the first | **One global GPU address space + one global VA allocator in the server** (the Linux model). The server's `va_alloc`/`va_free` (already written, `v3d_phoenix_winsys.c:208`) becomes the single authority. **No per-client isolation** (documented tradeoff; matches Linux-without-GMP). |
| **(c)** | **Command-submission arbitration + scheduling** | Serialize/schedule jobs from many clients onto the GPU; validate command buffers | Winsys spins to completion with no cross-process lock; two submitters race CT0/CT1 | **The server's single-threaded `msg` loop IS the arbiter.** One submit runs at a time; the server `msgRespond`s only after FRDONE. Scheduling = FIFO by message arrival (priority later). |
| **(d)** | **Synchronization (fences/syncobj/timeline) + DMA-BUF sharing** | Cross-client ordering + zero-copy buffer passing (e.g. X server ↔ GL client) | No fence facility; `drmPrime*` stubbed to fail; no dma-buf analogue | **Fence = the `msgRespond` itself** (client blocks in `msgSend` until GPU idle; reuses the Vulkan doc's "synchronous = conformant" result). **DMA-BUF = `MAP_PHYSMEM`:** EXPORT returns a global BO id; IMPORT returns `{gpuva, pa}`; the importer CPU-maps `pa`. Because VA is global, the `gpuva` is *already valid* in the importer's submits — sharing is nearly free. |
| **(e)** | **KMS modeset + plane ownership + pageflip/vblank** | Own the display; flip scanout buffers; vblank events | Firmware owns HDMI; we render-to-scanout to the one `/dev/fb0` surface; no flip, no vblank | **Display-arbiter role folded into (or peered with) the GPU server.** Pageflip via firmware **pan** (`SET_VIRTUAL_OFFSET` over a 2× virtual FB). Plane ownership = the server hands one client "scanout" at a time. Vblank = best-effort (firmware-dependent, VERIFY). |

The decisive insight: **(c) and (d-fence) and (b-arbitration) all collapse into one
mechanism on a microkernel** — the server's serialized message loop. That is precisely
why the central-server model is *simpler* here than in-kernel DRM, not harder.

---

## 2. The V3D single-MMU-context problem — the crux, resolved

V3D 4.2 has exactly one MMU and one base register (`MMU_PT_PA_BASE = 0x1204`,
`v3d_phoenix_winsys.c:31`). Three ways to multiplex it:

### Option (a) — central server owns the MMU + all submits; one global VA space  ✅ RECOMMENDED
Clients never map V3D MMIO and never touch the PT. The server programs `MMU_PT_PA_BASE`
**once at startup** and inserts/removes PTEs for every client's BOs into that one flat
table. Clients address BOs by the `gpuva` the server assigned. This is the
**Fuchsia/Magma + genode** model and the **Linux v3d** model simultaneously.

**Why it wins, from the hardware contract:** `v3d_mmu.c` (read for facts only):
- `v3d_mmu_set_page_table()` writes `V3D_MMU_PT_PA_BASE` **once**, in device init
  (`:84`) — never per-job.
- The file's own DOC comment: *"switching between [page tables] is expensive, we load
  all BOs into the same 4GB address space"* and *"To protect clients from each other we
  should use the GMP … This is not yet implemented."*
So even Linux runs exactly the model we propose, and the per-job alternative is what it
deliberately avoids.

### Option (b) — per-submit MMU-base swap + global lock  ❌ REJECTED
Give each client its own PT; before each submit, write that client's PT PA to
`MMU_PT_PA_BASE` and flush the MMUC + TLB (`ioc_submit_cl` already does the flush dance,
`:349-352`). **Rejected because:** (1) it forces a *full* MMU/TLB flush per submit — the
very cost Linux cites for refusing it; (2) it provides isolation V3D's GMP would do
better and that we don't need for a trusted desktop; (3) it is *more* code and *more*
serialization than option (a), for a benefit (isolation) the workload doesn't require.

### Option (c) — single shared global VA space, no per-process isolation  ✅ ADOPTED (with (a))
This is not really an *alternative* to (a) — it is the **VA-management half** of (a).
Option (a) (server owns MMIO) + option (c) (one global VA allocator) are the same
recommendation stated from two angles. The server's existing `va_alloc`/`va_free`
(`:208`/`:230`) is already a global allocator; it simply becomes authoritative for all
clients instead of one process.

**Security/isolation tradeoff (stated plainly):** with one global VA space, a malicious
or buggy client's command list can read or write *any* BO in the system by addressing
its `gpuva`. There is **no GPU-side memory protection between clients.** This equals
Linux-without-GMP and is fine for a trusted single-user desktop + game. Hardening would
require implementing V3D GMP masking (128 KB granularity) — a future item, explicitly
out of scope.

**IPC/ABI for option (a):** see §3.

---

## 3. The Phoenix GPU-server architecture (concrete)

### 3.1 Process shape
A new userspace server, e.g. `sources/phoenix-rtos-devices/gpu/rpi4-v3d/` (sibling of
`rpi4-fb`), built around the existing `fb_thread` loop shape. At startup it:
1. `v3d_phoenix_powerOn()` (move `v3d_phoenix_power.c` in as-is).
2. Maps HUB/CORE0 MMIO, allocates the one flat MMU PT, writes `MMU_PT_PA_BASE` **once**,
   enables the MMU — i.e. **exactly today's `winsys_init()`, run once, in the server**.
3. `portCreate(&port)` + `create_dev(&dev, "v3d")` → registers `/dev/v3d` (a
   render-node analogue). Optionally also `create_dev(... "dri/renderD128")` so a future
   Mesa `drmGetDevices2` shim can `open()` it.
4. Enters the `msgRecv`/dispatch/`msgRespond` loop.

The server **is** the relocated winsys: `ioc_create_bo`, `va_alloc/va_free`,
`ioc_submit_cl`, `ioc_get_param` move in *verbatim*; only their *callers* change from a
local `drmIoctl` to a `msg` dispatch.

### 3.2 Message protocol (the GPU-server ABI)
All requests arrive as `msg.type == mtDevCtl` with a small command struct packed into
`msg.i.raw[64]`; replies pack into `msg.o.raw[64]`. None of these need `i.data`/`o.data`
(no bulk copy) — the largest, `SUBMIT_CL`, is 7 u32s. Per-connection handle namespaces
are keyed by `msg.pid` (or an explicit connect-handshake that returns a session id).

```
/* request/reply structs, all <= 64 bytes, live in msg.i.raw / msg.o.raw */

V3DG_CONNECT        -> {}                              <- { u32 session }
V3DG_GET_PARAM      -> { u32 param }                   <- { u32 value }            /* ioc_get_param */
V3DG_CREATE_BO      -> { u32 size, u32 flags }         <- { u32 handle, u32 gpuva, u64 pa, u64 phys_len }
V3DG_MMAP_BO        -> { u32 handle }                  <- { u64 pa, u64 size }     /* client MAP_PHYSMEM's pa */
V3DG_GET_BO_OFFSET  -> { u32 handle }                  <- { u32 gpuva }
V3DG_CLOSE_BO       -> { u32 handle }                  <- { int err }
V3DG_SUBMIT_CL      -> { u32 bcl_start, bcl_end,       <- { int err }   /* server spins to FRDONE,
                          rcl_start, rcl_end,                              then responds = fence signal */
                          qma, qms, qts }
V3DG_SUBMIT_TFU     -> { drm_v3d_submit_tfu fields }   <- { int err }   /* for V3DV textures, future */
V3DG_EXPORT_BO      -> { u32 handle }                  <- { u64 global_id }        /* DMA-BUF analogue */
V3DG_IMPORT_BO      -> { u64 global_id }               <- { u32 handle, u32 gpuva, u64 pa, u64 size }
V3DG_WAIT_FENCE     -> { u32 fence }                   <- { int err }   /* no-op: submit already sync */
V3DG_SET_SCANOUT    -> { u32 handle }                  <- { int err }   /* claim the display plane */
V3DG_PAGEFLIP       -> { u32 handle }                  <- { int err }   /* pan to this BO (display) */
```

**Why `mtDevCtl` and not new `msg` types:** Phoenix's `ioctl_pack`/`ioctl_unpack`
(`posix/utils.h`, as used by `rpi4-fb.c:132`) already marshal a command + small payload
through `mtDevCtl`; we reuse it rather than inventing message types, keeping the kernel
ABI untouched.

### 3.3 BO memory sharing (the DMA-BUF analogue) — `MAP_PHYSMEM`
This is the single most important mechanism. **It is confirmed to work for
kernel-allocated DRAM (not just reserved carve-outs) by reading the kernel VM source**
— a stronger claim than the framebuffer precedent, which only maps a firmware carve-out:

- **Kernel evidence (no reserved-range gate, no owner check):** a `VM_OBJ_PHYSMEM`
  mapping resolves a frame via `page_get((addr_t)offs)` (`vm/object.c:224`); the comment
  there states *"page can be NULL, when address outside of defined physical maps is
  used"* and the mapping **proceeds anyway** — `vm/map.c:772-776` `page_map`s the raw
  `paddr` into the caller's pmap whether or not a `page_t` was found. So a second process
  can `MAP_PHYSMEM` *any* physical address, including DRAM the server allocated with
  `MAP_CONTIGUOUS`. **The design works.**
- **But there is NO frame refcount.** `page_get` (`vm/page.c:188`) is a pure bsearch
  lookup — it does **not** increment a refcount. A `MAP_PHYSMEM` mapping holds no
  reference on the underlying frame. **Consequence:** if the server `munmap`s a BO's
  pages (on CLOSE) while an importer still maps them, those frames return to the
  allocator → use-after-free, with no kernel dma-buf to prevent it. **The server is the
  owner of record and must keep its own mapping of every live BO alive until all
  importers release** (a server-side refcount, see M2). This is the real lifetime layer,
  below the app-level handle bookkeeping.

The flow:

- **CREATE_BO:** server `mmap(MAP_CONTIGUOUS|MAP_UNCACHED|MAP_ANONYMOUS)` → contiguous
  uncached DRAM, `va2pa` → `pa`, inserts PTEs into the global flat PT, returns
  `{handle, gpuva, pa, len}`.
- **Client side:** the client's libdrm shim, on `DRM_V3D_MMAP_BO`, takes the returned
  `pa` and does `mmap(NULL, len, …, MAP_PHYSMEM|MAP_UNCACHED|MAP_ANONYMOUS, -1, pa)` —
  identical to how `rpi4-fb.c:79` maps the framebuffer and how the winsys maps MMIO.
  Now both server and client see the same physical pages. **Mesa writes CLs/vertices/
  textures straight into this mapping; nothing copies through the kernel.**
- **EXPORT/IMPORT between two clients (X ↔ GL):** EXPORT returns a stable `global_id`;
  the second client IMPORTs it and gets `{gpuva, pa}`. Because the VA space is **global
  and shared**, the `gpuva` the producer used is *already valid* in the consumer's
  command lists — no re-mapping of GPU VA, no PT edit. The consumer only needs the `pa`
  if it wants a CPU view. **This is why option (a)+(c) makes buffer-sharing nearly
  free** — a direct benefit of the single-address-space choice.

### 3.4 Serialization, scheduling, and the fence
- The server runs **one submit at a time**: `V3DG_SUBMIT_CL` handler calls the relocated
  `ioc_submit_cl` (which spins on FLDONE then FRDONE) and only then `msgRespond`s.
- **The respond is the fence.** The client blocked in `msgSend` wakes exactly when the
  GPU work is complete and the final L2T `FLM_CLEAN` flush has landed
  (`v3d_phoenix_winsys.c:402`). This *is* a fence with correct visibility semantics —
  and it dovetails with the Vulkan plan's verified result that synchronous,
  pre-signaled sync is *conformant*, not a hack.
- **Scheduling** is FIFO by message-arrival order initially. A later refinement can use
  `msg.priority` (`msg.h:77`) to let a compositor preempt batch GL clients between jobs
  (never mid-job — V3D submit is not preemptible here).

### 3.5 Concurrency hazard to design for (M1) — PT mutation during a submit
If the server spin-waits inside the `SUBMIT_CL` handler, a *single-threaded* loop
trivially serializes everything (no other client's `CREATE_BO` runs concurrently —
their messages just queue). That is the **safe M0/M1 default** and needs no locking.

The *optimization* (and its hazard): to let `CREATE_BO` from client B proceed while
client A's GPU job runs, the server would need a second thread, and then inserting PTEs
+ flushing the MMU **mid-job** is a correctness question (the running job may re-walk the
TLB). The fix shape — **noted, not built in M0** — is to split two locks: a *PT-mutation
lock* (BO alloc/free at currently-unused VAs, with a deferred flush folded into the next
submit's existing pre-flush `:349`) distinct from the *submit-spin*. BO allocation at VAs
not referenced by the in-flight job is safe; the next submit's mandatory MMUC/TLB flush
makes the new PTEs visible. Keep M0/M1 single-threaded; add this only if BO-alloc
latency during long frames proves to be a problem.

### 3.6 Per-submit IPC overhead vs the current in-process path
| Cost | In-process (today) | Via server (proposed) |
|---|---|---|
| BO contents copy | none (direct mmap) | **none** — `MAP_PHYSMEM` shared physical pages |
| Submit descriptor transfer | function call | **28 bytes in `msg.i.raw[]`**, no `data` buffer |
| Per-submit added cost | 0 | **1 `msgSend` round-trip + 1–2 context switches** |
| GPU spin (the dominant cost) | unchanged | unchanged (server does the same spin) |

At one submit per frame (42–60/s) the added cost is a single IPC round-trip per frame —
microseconds against a 16–24 ms frame. **The server model does not threaten the 42fps
GL-Quake / 60fps vkQuake targets.** (If a workload ever does *many* tiny submits per
frame, batching multiple CLs in one `SUBMIT` message is the mitigation.)

---

## 4. KMS / display ownership

Today the **firmware owns the HDMI pipeline** (HVS compositor + PixelValve); we
render-to-scanout into the single `/dev/fb0` surface (`v3d_phoenix_winsys.c:268` backs a
SCANOUT BO with the fb's physical pages). For multi-client + a compositor we need: a way
to (i) own/arbitrate the display plane, and (ii) flip between buffers without tearing.

### 4.1 Plane ownership / arbitration
There is exactly **one** scanout surface. The current winsys guards it with a
process-local `W.scanout_claimed` (`:120`) — useless across processes. In the server
model this becomes authoritative: `V3DG_SET_SCANOUT` lets **one client at a time** own
the display plane; the server refuses a second claimant or revokes on disconnect. A
compositor (or X) becomes the scanout owner; ordinary GL clients render to offscreen BOs
and the compositor blits/flips. This *is* the minimal "plane ownership" KMS provides.

### 4.2 Pageflip — firmware pan vs direct HVS
Two routes to double-buffering:

- **(Recommended, low-risk) Firmware pan via `SET_VIRTUAL_OFFSET` (mailbox tag
  0x00048009).** Allocate the firmware framebuffer at **2× virtual height** (set virtual
  W/H = tag 0x00048004, *confirmed present in the repo's own GPU notes*,
  `docs/knowledge/gpu-vc6-non-linux.md:155`), render alternately to the top and bottom
  halves, and "flip" by issuing a `SET_VIRTUAL_OFFSET` mailbox message that pans the
  scanout origin to the just-finished half. **We already have the exact mechanism** —
  `mboxProp()` in `v3d_phoenix_power.c:50` is a ready property-channel helper; the pan is
  one more tag. This needs **no HVS/PixelValve programming** and keeps the firmware in
  charge of the pipeline.
  - **VERIFY (not asserted):** that (1) the firmware honors a *post-boot*
    `SET_VIRTUAL_OFFSET` on the plo/firmware-allocated FB, and (2) whether the pan takes
    effect at vblank (tear-free) or immediately (tearing). Tag 0x00048009 is the
    well-known RPi firmware pan tag, but its vsync behavior on this boot path must be
    measured on HW before relying on it for tear-free flips. The repo confirms the
    sibling virtual-W/H tag (0x00048004) but not the offset tag's timing.

- **(High-effort, deferred) Direct HVS/PixelValve programming.** Take the display
  pipeline away from the firmware and program the HVS display list + PixelValve
  ourselves (true KMS). This buys real vblank IRQs and atomic plane updates but is a
  large, brittle reverse-engineering effort against undocumented VC6 display hardware
  (the repo's `docs/knowledge/gpu-vc6-non-linux.md` is the starting map). **Out of scope**
  unless tear-free vsync proves mandatory and the pan route cannot deliver it.

### 4.3 vblank
Firmware-fb gives no clean vblank IRQ. Options: (a) tolerate tearing (the current GL path
already does, and Quake is playable); (b) approximate vsync by pacing flips to the panel
refresh via a timer; (c) if the pan tag turns out to be vblank-synchronized, get it for
free. **Tear-free vsync is the honest weak spot of the firmware-fb route** and the single
best reason someone might later fund the direct-HVS path.

---

## 5. What can be adapted from permissive sources (with licenses)

The blunt summary: **reusable *code* is essentially Mesa (MIT), which we already build.
Reusable *design* is Fuchsia Magma (BSD) and genode (concepts ONLY — AGPL code).** Do
not copy Linux DRM or the BSD drm shims (Linux-derived).

| Source | License | Safe to derive **code**? | What maps to our need |
|---|---|---|---|
| **Fuchsia Magma** (`magma_system`, `MagmaConnection`, `magma_command_buffer`) | **BSD-3 / MIT-style** (Fuchsia license) | **Yes — best architectural reference.** | The *connection/session* model: a per-client connection to a central GPU "system driver" that owns the HW; client submits command buffers + semaphores over IPC; the driver multiplexes. This is **our server ↔ client design**, validated by a shipping microkernel-adjacent OS. Adapt the *shape* (connect → import/create buffer → submit command buffer → wait semaphore) into our `msg` protocol. |
| **genode `gpu_session`** (`os/include/gpu_session/`) | **AGPLv3** (with commercial dual-license) | **NO — concepts/interface design only. Do NOT copy code into published source.** This is the one real licensing landmine. | The interface *decomposition* (alloc_buffer / map_buffer / exec_buffer / completion signal / info) confirms our `msg` surface is the right granularity. Read for *what operations a microkernel GPU session needs*; write our own. |
| **Mesa `kmsro` / `renderonly`** (`src/gallium/winsys/kmsro/`, `src/gallium/auxiliary/renderonly/`) | **MIT** | **Yes.** | The render-vs-display split: render on V3D, scan out on a *separate* display device, importing buffers across. Our `EXPORT/IMPORT_BO` + "scanout owner" *is* a renderonly split — the GPU server is the render node, `rpi4-fb`/pan is the display node. Confirms the buffer-handoff pattern. |
| **Mesa `u_sync_provider`** (`src/util/u_sync_provider.c`) + `vk_drm_syncobj` | **MIT** | **Yes (already in our tree).** | The sync abstraction the Vulkan port already neutralizes (companion doc §2). Our "fence = msgRespond" plugs in here: keep advertising the provider, back every op with trivially-signaled stubs, because work is synchronous. |
| **Mesa libdrm shim surface** (our own `v3d_libdrm_shim.c`) | our code (`%LICENSE%`) | **Yes.** | The client side. Today it calls `phoenix_v3d_ioctl` in-process; in the server model it *forwards each ioctl as a `msgSend`* to `/dev/v3d`. Minimal change, same surface Mesa expects. |
| **FreeBSD `drm-kmod`, OpenBSD `drm`** | **GPL headers / Linux-derived** | **NO — avoid for code.** | As the task warns: these are largely Linux shims with GPL headers. Use neither for code. (FreeBSD's *graphics stack outside* drm-kmod, e.g. its Mesa packaging, is fine, but offers nothing we need.) |
| **Linux v3d driver** (`external/linux/.../v3d/`) | **GPL** | **NO — read for HARDWARE CONTRACT only** (register sequences = facts). | `v3d_mmu.c` (one global VA, PT base once), `v3d_sched.c`/`v3d_irq.c` (TFU/IRQ register sequences for a *future* async path). Facts, never code. |

**Net:** the architecture is Magma-shaped (BSD-safe to derive), the interface granularity
is genode-confirmed (concepts only), the code we write reuses Mesa (MIT) + our own winsys.
No GPL/AGPL code enters the published source.

---

## 6. Phased, commit-stoppable plan

Each milestone ends at a verifiable state. "Proof" = a UART line or HDMI screenshot the
`rebuild → capture → summarize` loop confirms.

| M | Goal | Deliverable | Key obstacle | What proves it | Difficulty / Risk |
|---|---|---|---|---|---|
| **M0** | **GPU server owns the MMU; ONE client submits via IPC** (single-client-via-server) | `rpi4-v3d` server (relocated winsys + `msg` loop, registers `/dev/v3d`); client libdrm shim forwards `drmIoctl`→`msgSend` | Relocating `winsys_init`/`ioc_*` into a server; defining the `msg` protocol; `MAP_PHYSMEM` BO handoff working cross-process | A boot-launched test client allocates a BO, submits a clear-CL via the server, reads back the cleared pixel == expected color. Proves the **IPC submit path** end-to-end. | **Medium.** Risk: **Medium** — the winsys exists; the new surface is IPC marshalling + the cross-process `MAP_PHYSMEM` round-trip. |
| **M1** | **TWO clients submit concurrently without corruption** | Per-connection handle namespaces; server serializes submits; (single-threaded loop = the arbiter) | Concurrency correctness; per-client handle isolation; *not* per-client VA isolation (accepted) | Two boot-launched clients each render a distinct clear to their own BO in a tight loop; both readbacks stay correct over N iterations (no MMU corruption, no cross-talk). | **Medium.** Risk: **Medium-High** — **this is the derisking gate.** The PT-mutation-during-submit hazard (§3.5) lives here; keep single-threaded to sidestep it. |
| **M2** | **DMA-BUF-style BO sharing between two clients** | `V3DG_EXPORT_BO`/`IMPORT_BO`; global-id namespace; **server-side frame refcount** | **Page-lifetime, not just app handles:** `MAP_PHYSMEM` does not refcount frames (§3.3), so the server must hold the owning mapping until all importers release, or risk use-after-free. Verifying the shared `gpuva` is valid in the consumer's CLs (it is, by global VA). | Client A renders into a BO, EXPORTs it; client B IMPORTs and samples/blits it; result correct; A's CLOSE while B holds it does **not** corrupt B. **This is the X-server ↔ GL-client handoff primitive.** | **Medium.** Risk: **Medium** — the kernel-page-ownership refcount is the real fiddly part; the VA-sharing is free by design. |
| **M3** | **Fence / sync surface complete** | `WAIT_FENCE` + the `drmSyncobj*` provider stubs wired to "respond = signal"; reuse Vulkan doc §2 | Making Mesa's `u_sync_provider`/`vk_drm_syncobj` happy with trivially-signaled, server-backed fences | Mesa GL **and** V3DV both run through the server with their sync surface satisfied (no link/assert failures); a multi-frame render is correct. | **Low-Medium.** Risk: **Low** — synchronous = conformant is already proven; this is plumbing. |
| **M4** | **Display arbiter / double-buffered pageflip** | `SET_SCANOUT` plane ownership + `PAGEFLIP` via firmware pan over a 2× virtual FB | Firmware pan post-boot + its vsync behavior (VERIFY §4.2); coordinating pan with `rpi4-fb`/fbcon | A client double-buffers: renders to back half, flips via pan, no visible tearing seam; HDMI screenshot shows a clean animated frame; only one scanout owner at a time. | **Medium-High.** Risk: **High** — firmware pan behavior is the top unknown; tear-free vsync may be unreachable without direct HVS. |
| **M5** | **X server (or compositor) as a GPU client** | kdrive/Glamor (or a tiny compositor) as the scanout owner; GL clients render to offscreen BOs the compositor composites | Everything in the X11 companion doc (EGL port, Glamor) **plus** the M0–M4 server | An X GL client renders on the GPU **concurrently** with the X server compositing, no corruption — the goal the X11 doc gated on this doc for. | **Very High / research.** Risk: **High** — ties to EGL + Glamor (X11 doc M3/M3′); this doc unblocks it but does not contain it. |

**Recommended stopping/checkpoint:** **M1** is the proof that the entire premise works
(two clients, no corruption). **M2–M4** make it useful (sharing + flip). **M5** is the
downstream payoff, owned jointly with the X11 and Vulkan docs.

---

## 7. Honest verdict

**Realistic effort: ~6–8 person-months** for the full goal (X server + GL client sharing
the V3D with no corruption, double-buffered display). Breakdown:
- **M0–M1 (the derisking core): ~2 months.** Low *architectural* risk because the GPU
  logic already exists and runs on HW — this phase is *relocating* the winsys into a
  server and proving the IPC + `MAP_PHYSMEM` path with two non-corrupting clients. If M1
  passes, the model is validated and everything after is engineering, not research.
- **M2–M3 (sharing + sync): ~1.5 months.** Mostly plumbing; sync is already understood.
- **M4 (display pageflip): ~1.5–2 months**, dominated by the firmware-pan/vsync unknown
  (could be quick if the pan tag behaves, or could push toward the large direct-HVS path
  if tear-free is mandatory).
- **M5 (X/compositor integration): not counted here** — it belongs to the X11 doc's
  budget; this doc only removes its blocker.

**Top-3 highest-risk unknowns (ranked):**
1. **PT-mutation during a concurrent submit (M1).** The single-threaded loop sidesteps it
   for free, but any future overlap of BO-alloc with GPU execution needs the two-lock
   design (§3.5) and on-HW validation that mid-job PTE inserts + deferred flush are safe.
   This is the deepest correctness question.
2. **Firmware-fb pageflip / vsync (M4).** Whether `SET_VIRTUAL_OFFSET` works post-boot and
   whether it is vblank-synchronized is unverified; the fallback (direct HVS) is a large,
   undocumented-hardware effort. Tear-free vsync is the route's weak spot.
3. **`MAP_PHYSMEM` is unrestricted today.** "Clients can't touch MMIO / can't corrupt the
   MMU" is **isolation-by-convention**, not enforced: any process can `MAP_PHYSMEM` the
   V3D MMIO at `0xfec00000` and the PT's PA just like the server does. The server model is
   *robust against accidents* (well-behaved Mesa clients go through IPC) but **not a
   security boundary** until Phoenix restricts `MAP_PHYSMEM` to privileged callers. Note
   for the security model; not a blocker for a trusted desktop.

**Is the central-GPU-server (Magma/genode-style) model clearly right for Phoenix vs.
replicating Linux's in-kernel DRM? Yes — decisively, for three concrete reasons:**
1. **Phoenix is a microkernel with no in-kernel driver home.** Every driver here
   (`rpi4-fb`, pl011-tty, USB, posixsrv) is already a userspace `msg` server. An
   in-kernel DRM has nowhere to live; the server model is the *native* pattern, not a
   compromise.
2. **The microkernel collapses three DRM services into one mechanism.** Arbitration,
   scheduling, and fencing all become "the serialized message loop responds when the GPU
   is idle." In Linux these are three subsystems (GEM scheduler, dma-fence, ww-mutex BO
   reservation); here they are one `msgRespond`. The microkernel makes the problem
   *smaller*.
3. **The hardware already mandates the server's core decisions.** One MMU base, one
   global VA, write-PT-once, isolation-via-GMP-not-yet — Linux itself runs exactly the
   model we propose. We are not inventing arbitration; we are moving the proven winsys
   behind a port and serializing it, which is what a microkernel does with any shared
   device.

**Bottom line:** This is **not** a research gamble like a from-scratch DRM would be. The
GPU driver already works single-client on real hardware; the multi-client model is a
*relocation + a thin IPC protocol + a global allocator we already have*, validated by
both the Linux hardware contract and the Magma microkernel precedent. The genuine
unknowns are narrow and late (PT-mutation overlap, firmware pageflip), not foundational.
Build M0–M1 first; it will prove the whole thesis in ~2 months.

---

## 8. Cross-references
- The crux file (relocate this into the server): `tools/v3d-driver-port/v3d_phoenix_winsys.c`
- Power-on + the reusable mailbox helper: `tools/v3d-driver-port/v3d_phoenix_power.c`
- Client libdrm surface to forward over IPC: `tools/v3d-driver-port/v3d_libdrm_shim.c`
- Server skeleton to copy: `sources/phoenix-rtos-devices/video/rpi4-fb/rpi4-fb.c`
- IPC ABI: `sources/phoenix-rtos-kernel/include/msg.h`, `sources/libphoenix/include/sys/msg.h`
- Shared-physical-memory primitive: `sources/phoenix-rtos-kernel/include/mman.h:25-26` (`MAP_PHYSMEM`/`MAP_CONTIGUOUS`)
- HW contract (GPL — facts only): `external/linux/drivers/gpu/drm/v3d/v3d_mmu.c`, `v3d_sched.c`, `v3d_irq.c`
- Display tags reference: `docs/knowledge/gpu-vc6-non-linux.md`
- Accelerated-X consumer of this design: `docs/inprogress/2026-06-16-x11-accelerated-desktop-plan.md` (Part 3, M3′)
- Vulkan consumer (sync model reused): `docs/inprogress/2026-06-16-vulkan-v3dv-vkquake-port-plan.md` (§2)
