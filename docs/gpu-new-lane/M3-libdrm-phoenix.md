# M3 part 1 — `libdrm-phoenix`: upstream libdrm over the Phoenix DRM servers

Milestone M3 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §4.1 (option
C), §4.4, §4.6 and §5 (M3). Builds on [E7](E7-drm-userspace-build.md) (the libdrm cross build and the
`drmIoctl()` / identity seams), [E1](E1-vm-object-export.md) (`memExport`, fd per buffer),
[E5](E5-deferred-reply.md) (IPC costs, blocking `read()` events, `poll()` quantisation),
[M1](M1-async-render-server.md) (`rpi4-v3d-async`, `v3da_proto.h`) and
[M2](M2-kms-server.md) (`rpi4-kms`, `kms_proto.h`).

**Status (2026-09-27):** code complete, **builds** (static `libdrm.a` + `drmprobe` for aarch64-phoenix,
the backend has 0 compiler warnings under libdrm's own warning set), **host-tested** (134 logic checks
+ the real `drmprobe` against fake servers, both PASS), **no Pi cycle yet** — the cycle is
pre-registered in §7. Nothing committed; no server, no old-lane file and no sibling repo touched.
Code: [`tools/gpu-lane/libdrm-phoenix/`](../../tools/gpu-lane/libdrm-phoenix/).

Evidence tags: **[read]** = read in source at the cited place; **[host]** = shown by the host harness
(§6.2); **[built]** = the cross build shows it; **[inferred]** = reasoning, not verified.

---

## 0. Decisions at a glance

| Question | Decision |
|---|---|
| Where is the seam? | `drmIoctl()` → `drm_phoenix_ioctl()` (one `#ifdef __phoenix__`), plus one `__phoenix__` branch in each identity function of `xf86drm.c`. Upstream libdrm stays pristine: **3 patches, +93/−6 lines** over `b97cbde`, and new files in `phoenix/`. |
| How does a request reach a server? | The DRM descriptor is identified **by the path it was opened under** (`sys_fdpath()`: the kernel keeps it on the `open_file_t`, so it survives `dup()` and `SCM_RIGHTS`), then `HELLO` on it gives the per-open client id. Every DRM ioctl becomes one raw `msgSend` to `{server port, client id}` — the M1/M2 transport, **no server protocol change**. Both servers answer every ioctl except `HELLO` with `-ENOTTY`, so nothing passes through "flat". |
| Payloads | Arrays go through a per-connection **page-aligned bounce buffer**; the per-frame ones (atomic plane states, submit buffers) are sent **page-rounded** so the window has no unaligned end (E5: +30 µs per end). Host harness: `unaligned_ends=0` over a full `drmprobe` run [host]. |
| `mmap(drm_fd, offset)` | **Token + `drmPhoenixMmap()`**: `MMAP_BO`/`MAP_DUMB` answer an opaque page-aligned token (`1<<52 | handle<<12`); `drmPhoenixMmap()` resolves it to the buffer's memref (PHYS → `MAP_PHYSMEM`; OID → `open("/kmsbuf/<id>")` + `mmap(MAP_UNCACHED)`). Unmodified programs get it by linking with **`-Wl,--wrap=mmap`** (libdrm-phoenix ships `__wrap_mmap`); Mesa either links the same way or patches its 4 call sites. The arena-window alternative is impossible (§2.6). |
| PRIME | Export = server export op + `open()` of the server-resolved name (E1's decision). `card0` export works today (`KMS_OP_PRIME_EXPORT` → `/kmsbuf/<id>`); self-import returns the original handle. Import on the render node = **`V3DA_OP_BO_IMPORT`**, reserved in the protocol → `-ENOSYS` today (**server gap G1**, request layout specified in `drm_phoenix_ext.h`). |
| Sync files / syncobj fds | Sync files emulated **in-process** (a `dup()` of the render descriptor + a fence snapshot, as the M1 adapter proved); `IN_FENCE_FD` resolves to a `v3da_fence_t` that `rpi4-kms -G` checks on the fence page. Opaque syncobj fds and cross-process sync files: `-ENOSYS` (gap G6). |
| Device identity | A static list of **two platform devices, three nodes** — `vc4` (`card0` → rpi4-kms) and `v3d` (`card1` + `renderD128` → rpi4-v3d-async): Linux's Pi 4 topology. v3dv opens the v3d device's primary node unconditionally, so the render server needs a primary name too (§2.9). |
| `/dev/dri` names | libdrm-phoenix reports `/dev/dri/{card0,card1,renderD128}` when they resolve and falls back to `/dev/kms`, `/dev/v3d-async` otherwise; both are recognised when a descriptor is identified. **Proposal (server work G10):** each server additionally `create_dev()`s its `dri/…` names (`create_dev` makes the `dri` directory itself); the old names stay, so nothing of the old lane or the M1/M2 tools changes. |
| Protocol additions | **None made.** Every server gap is a request layout for an already reserved opcode or a new appended opcode, written down in [`include/drm_phoenix_ext.h`](../../tools/gpu-lane/libdrm-phoenix/include/drm_phoenix_ext.h), spoken only once the server's protocol version says it exists (a `#error` fires when the version bumps without the library catching up). |
| Build | Static only (E7 §3.4). meson cross build from a generated cross file, the E7 `phx-gcc` wrapper, the tree sysroot. `build.sh` never touches `external/libdrm`. |

## 1. What was built

| File (under `tools/gpu-lane/libdrm-phoenix/`) | Lines | What |
|---|---|---|
| `patches/0001-drm-headers-phoenix-ioctl-layout.patch` | +22/−2 (drm.h +5/−1, xf86drm.h +17/−1) | `drm.h`/`xf86drm.h`: `#if __phoenix__ #include <sys/ioctl.h>` (the BSD `_IOC` layout, E7 §2.1); `drmPhoenixMmap()`/`drmPhoenixMunmap()` declarations |
| `patches/0002-xf86drm-phoenix-backend-hooks.patch` | +58/−3 | `drmIoctl` hook; `drmWaitVBlank` via `drmIoctl` (§5 F1); identity branches in `drmGetDevices2`, `drmGetDevice2`, `drmGetDeviceFromDevId`, `drmGetNodeTypeFromDevId`, `drmGetNodeTypeFromFd`, `drmGetDeviceNameFromFd[2]`, `drmGetMinorNameForFD`, `drmOpenMinor`; `open_memstream` fallback (meson computed `HAVE_OPEN_MEMSTREAM`, nothing used it) |
| `patches/0003-meson-phoenix-backend.patch` | +13/−1 | build `phoenix/*.c` when `host_machine.system() == 'phoenix'` |
| `src/xf86drm_phoenix.c` | ~900 | descriptor → connection table, identification, transport, `drm_phoenix_ioctl`, identity, `drmPhoenixMmap`, PRIME fd table, sync-file table |
| `src/drm_phoenix_kms.c` | ~1100 | KMS marshalling (card0 → rpi4-kms) |
| `src/drm_phoenix_v3d.c` | ~1180 | V3D + GEM + PRIME + syncobj marshalling (renderD128/card1 → rpi4-v3d-async) |
| `src/drm_phoenix_logic.[ch]` | ~540 | **pure** logic (no IPC, no Phoenix headers): atomic flattening, GETPROPERTY/VERSION fill, `drmDevice` layout, ioctl/token decoding — host-testable |
| `src/drm_phoenix_wrap.c` | 56 | `__wrap_mmap` (own archive member: pulled in only by `--wrap=mmap`) |
| `src/drm_phoenix_priv.h`, `src/xf86drm_phoenix.h` | | internal interfaces; the hook API the patches call |
| `include/drm_phoenix_ext.h` | | **proposed** wire additions (the server work list, §4) |
| `include/v3d_drm.h` | | vendored Mesa `include/drm-uapi/v3d_drm.h` (MIT, unchanged; libdrm ships no v3d header) |
| `drmprobe/drmprobe.c` | ~940 | the Pi probe (§7): libdrm API only, plain `mmap()` |
| `hosttest/` | | `hosttest.c` (logic), `mock/` (fake servers + Phoenix stand-ins), `e2e_main.c`, `run.sh` |
| `build.sh` | | clone → patch → meson → `libdrm.a` → `drmprobe` |

New files carry the Phoenix header with `%LICENSE%` (as the M1/M2 files); the patches touch MIT files
and stay MIT. No GPL source was read or copied.

## 2. Design

### 2.1 The seam

`drmIoctl()` is libdrm's choke point (E7 §2.3): all `drmMode*` entry points, `drmSyncobj*`,
`drmPrime*`, `drmGetVersion`/`GetCap`/`SetClientCap`, and Mesa's v3d/v3dv drivers go through it. The
dispatcher trusts only the low 16 bits of the request (number + `'d'` group), which are identical in
the Phoenix/BSD and the Linux `_IOC` layouts — so a Mesa built with any `drm.h` variant decodes
correctly (E7's encoding hazard does not apply behind `drmIoctl`). Requests of another group
(`DMA_BUF_IOCTL_*`, `SYNC_IOC_*`) get `ENOTTY`.

### 2.2 Descriptor → connection

* First use of a descriptor: `sys_fdpath(fd)` → a known node path (or any `/dev/…` name, then the
  server is asked with each `HELLO`; each server answers the other's with `-ENOTTY`) → `lookup()` →
  port → `HELLO` ioctl on the descriptor → client id. The render connection also maps the fence page
  (read-only, cached — the only memory type any mapping of it may use, E5 §1(d)).
* `dup()`ed descriptors share one connection (same `{server, port, client id}`); `HELLO` is idempotent
  in both servers [read: `v3da_main.c client_hello`, `kms_main.c handle_ioctl`].
* A descriptor number recycled for a **different** path is re-identified (one `sys_fdpath` syscall
  per call, no IPC). A number recycled for the **same** node: render — detected by one load (the
  fence-page slot generation changes when the server reassigns the slot); display — the server
  answers `-EBADF` for a dead client id and the call is retried once after re-identification. The
  residual is a kms client id reused by the server for the new open (then only local mirrors are
  stale) — see R2.
* libdrm has no close hook, so a connection lives until its descriptor number is reused (4 KiB fence
  mapping + tables per stale render connection). Harmless for Mesa's usage [inferred].

### 2.3 Transport and costs

One DRM ioctl = one `msgSend` round trip (E5: ~31 µs), except the local ones (`VERSION`,
`GET_UNIQUE`, `GET_MAGIC`, render-node `GET_CAP`/`SET_CLIENT_CAP`/master ops) and the fast paths
(`WAIT_BO`/`SYNCOBJ_WAIT` on passed fences: one fence-page load; `GET_PARAM`, `GET_BO_OFFSET`,
`MMAP_BO`/`MAP_DUMB` on known handles: tables). Waits are raw-only and never park longer than
`V3DA_WAIT_MAX_MS`/`KMS_READ_MAX_MS` (2 s); longer waits loop in the library, as E5 requires.

### 2.4 KMS specifics

* **Atomic flattening** — DRM requests are *partial*, `kms_atomic_plane_t` is *complete*: each
  plane's last committed state is mirrored per connection (seeded from `GET_PROPERTIES`, updated on
  every successful commit, dropped by `SETCRTC`/`PAGE_FLIP`/`RMFB`), and the request is overlaid on
  it. Connector `CRTC_ID`, CRTC `MODE_ID`/`ACTIVE` map to the header. A CRTC-only commit carries the
  primary unchanged so the server validates the mode and can send the event. Immutable properties →
  `-EINVAL`, unknown → `-ENOENT`, >8 planes → `-E2BIG`, planes on two CRTCs → `-EINVAL` (Stage A).
* **Blocking commits** (`SETCRTC`, `SETPLANE`, `OBJ_SETPROPERTY`, `ATOMIC` without `NONBLOCK`): the
  library waits for the vblank after acceptance, then until `GET_CRTC` shows no pending primary flip
  (Stage A exposes the primary's pending fb only — G9).
* **`WAIT_VBLANK`**: the library always sends an absolute 64-bit target it computed itself (32-bit DRM
  sequences widened against the current count; `NEXTONMISS` handled here), because the server's
  `KMS_VBL_ABSOLUTE` is bit 0 — the opposite of DRM's `_DRM_VBLANK_RELATIVE = 1` (§5 F2).
* **Legacy**: `ADDFB` → `ADDFB2` (32/24 → XRGB8888, 32/32 → ARGB8888); `SETPLANE`,
  `OBJ_SETPROPERTY`, connector `SETPROPERTY` → a one-plane atomic commit (DPMS/link-status accepted
  as no-ops); `DIRTYFB` → 0; `CLOSEFB` = `RMFB`.
* **Events** need no library code: `drmHandleEvent()` `read()`s the card descriptor and the server
  answers in the DRM event wire layout (M2 §7); `poll()` works, 20 ms-quantised (E5, G12).

### 2.5 Render specifics

The M1 adapter's hard-won Mesa semantics (`v3da_winsys.c`, proven on the Pi with quakespasm-v3da)
moved behind `drmIoctl()`: `WAIT_BO` busy → `ETIME` (Mesa's `v3d_bo_wait` decodes only that) and a
real wait; relative `WAIT_BO` vs absolute `SYNCOBJ_WAIT` deadlines; MULTI_SYNC flattening (wait
stage → `V3DA_SEM_RENDER`); BO last-use and out-syncobj mirrors for no-IPC fast paths; submit buffers
page-rounded. Differences from the adapter: no old-lane hooks (scan-out BOs, firmware pan) — those
belong to KMS now; `MMAP_BO` returns a token instead of a CPU pointer; CPU-job extensions and perfmon
ids answer `-ENOSYS` instead of being silently ignored; timelines with non-zero points → `-EINVAL`
(`DRM_CAP_SYNCOBJ_TIMELINE = 0`); `SYNCOBJ_TRANSFER` = query + import; `SYNCOBJ_QUERY` reports point 0
for binary syncobjs.

### 2.6 Buffer mapping — why token + `drmPhoenixMmap`

| Option | Verdict |
|---|---|
| **Arena window**: the node descriptor's object *is* the buffer space, `MMAP_BO` offsets index it | **impossible without kernel/server redesign.** After `mtOpen` the descriptor's oid is `{port, client id}`; `mmap()` of it misses the object tree → `proc_size` → the server would have to answer `atSize` → a file-backed shadow object (E1 §3). And the render server's BOs are separate contiguous blocks (M1a), not one exportable range; E1 enforces one memory type per window while v3d BOs may be cached or not. |
| **Patch Mesa's 4 sites** (`v3d_bufmgr.c:518`, `v3dv_bo.c:305`, `vc4_bufmgr.c:646`, `gbm_driint.h:153`) to call `drmPhoenixMmap()` | works, ≈12 lines; but every other DRM program (Xorg modesetting's dumb BOs, Weston's pixman renderer, SDL KMSDRM dumb path) maps dumb buffers with plain `mmap()` too |
| **`-Wl,--wrap=mmap`** at the final static link → `__wrap_mmap` resolves tokens, forces `MAP_UNCACHED` on dma-buf descriptors, passes everything else to the real `mmap()` | **chosen as the default**: zero patches in any program; one branch per `mmap()`, plus one `sys_fdpath` syscall for `mmap(fd ≥ 0, offset 0)`. Verified in the cross build: `drmprobe`'s own `mmap()` calls and libdrm's internal ones both land in `__wrap_mmap` [built: objdump]. Forgetting the flag fails loudly (the servers refuse `atSize` → `mmap` fails). |

Tokens are page-aligned and `1<<52 | handle<<12`, so they can never collide with a real file offset
in practice. `munmap()` needs nothing: every mapping is an ordinary one once made. A dma-buf
descriptor mapped at offset 0 gets the export's memory type automatically (both namespaces export
Normal-NC memory).

### 2.7 PRIME

* `card0` `HANDLE_TO_FD`: `KMS_OP_PRIME_EXPORT` → memref `{OID, /kmsbuf port, handle}` →
  `open("/kmsbuf/<id>", O_RDONLY [|O_CLOEXEC])` (O_RDONLY: O_RDWR would `stat()` the name, E1 §1). The
  descriptor is recorded with its size (sizes are otherwise unknowable: `lseek(SEEK_END)` on a buffer
  name fails today, G3). Pan-backend slots (`KMS_MEM_PHYS`) cannot be exported (`memExport` refuses
  firmware memory) → `-ENOSYS`.
* `card0` `FD_TO_HANDLE`: this client's own export → the original handle (DRM semantics); a foreign
  buffer → `-ENOSYS` (`KMS_OP_PRIME_IMPORT`, G7).
* `renderD128` `FD_TO_HANDLE`: `sys_fdpath` → `{namespace port, id}` (+ size when this process exported
  it) → `V3DA_OP_BO_IMPORT` with the `v3da_bo_import_req_t` layout of `drm_phoenix_ext.h` → the
  server answers `-ENOSYS` today (**G1**). `HANDLE_TO_FD` → `-ENOSYS` locally (**G4**).
* `DRM_CAP_PRIME` reports `IMPORT | EXPORT` on both nodes (the ioctls exist; Mesa then takes its
  normal dma-buf paths and hits the gaps visibly instead of silently choosing another path).

### 2.8 Sync files

`SYNCOBJ_HANDLE_TO_FD(EXPORT_SYNC_FILE)` → a `dup()` of the render descriptor + the syncobj's fence
snapshot in a process table; `FD_TO_HANDLE(IMPORT_SYNC_FILE)` attaches it (`SYNCOBJ_IMPORT`, or
`SIGNAL` for a signalled snapshot); `IN_FENCE_FD` in an atomic request resolves through the same
table to a `kms_fence_t`, which `rpi4-kms -G` checks against the render server's fence page — **the
first cross-server fence path**, exercised by `drmprobe`'s `in_fence` line. Limits: process-local;
`SYNC_IOC_MERGE`/`SYNC_IOC_FILE_INFO` raw ioctls on these descriptors reach the render server as
unknown ioctls (`-ENOTTY`, G15); `OUT_FENCE_PTR` → `-ENOSYS` (G8).

### 2.9 Device identity

`drmGetDevices2` builds two `DRM_BUS_PLATFORM` devices laid out exactly like libdrm's
`drmDeviceAlloc` (one block + a separately allocated `compatible` list, so the unmodified
`drmFreeDevice` frees them — checked under ASan [host]): `vc4` (`/gpu`, `brcm,bcm2711-vc5`, primary
only) and `v3d` (`/v3dbus/v3d@7ec04000`, `brcm,2711-v3d`, primary + render). What consumes it:
Mesa's loader/kmsro/EGL (`bustype == PLATFORM`, a render node, `drmGetVersion()->name == "v3d"`,
the display node's `"vc4"`) and v3dv's `enumerate_devices()`, which calls
`try_device(nodes[DRM_NODE_PRIMARY], …, "v3d")` on the render device without checking the node exists
(`v3dv_device.c:1779-1780`) [read] — hence `card1`. `drmGetNodeTypeFromFd`, `drmGetDevice2`,
`drmGet{Primary,Render}DeviceNameFromFd`, `drmGetDeviceNameFromFd[2]` answer from the descriptor's
connection; `drmGetDeviceFromDevId` maps a `dev_t` (= `st_rdev` = the server port on Phoenix) back to
its device; `drmOpenMinor` (and so `drmOpen("vc4", NULL)`, `drmAvailable`) opens by minor.

## 3. Coverage

`✅` marshalled to the server · `L` answered in the library · `S` stub `-ENOSYS`/`-EOPNOTSUPP` (gap id)
· Mesa column: who calls it on our path [read, `external/mesa` greps of `drivers/v3d`,
`winsys/*/drm`, `broadcom/vulkan`, `renderonly`, `loader`, `gbm`, `egl/drivers/dri2`, `vulkan/wsi`,
`util/u_sync_provider.c`].

### 3.1 Generic and identity

| Request / function | Status | Mesa |
|---|---|---|
| `VERSION` | L (`vc4` 0.0.0 / `v3d` 1.0.0) | loader, kmsro, gbm, egl, v3dv |
| `GET_UNIQUE`, `GET_MAGIC`, `SET_VERSION` | L | egl (magic) |
| `GET_CAP` | ✅ card0 (`KMS_OP_GET_CAP`) · L render (`SYNCOBJ` 1, `TIMELINE` 0, `PRIME` 3, `TIMESTAMP_MONOTONIC` 1, `DUMB_BUFFER` 0) | u_sync_provider, wsi |
| `SET_CLIENT_CAP` | ✅ card0 · S render (`-EOPNOTSUPP`, as Linux v3d) | wsi display |
| `SET_MASTER`/`DROP_MASTER`/`AUTH_MAGIC` | ✅ card0 (accepted no-ops) · L card1 · `-EACCES` render | wsi display, egl |
| `GEM_CLOSE` | ✅ (render `BO_CLOSE`, card0 `DESTROY_DUMB`) | v3d, v3dv, renderonly |
| `GEM_FLINK`/`GEM_OPEN` | S (global names; PRIME is the sharing path) | v3d `resource_from_handle(SHARED)` only |
| `PRIME_HANDLE_TO_FD` | ✅ card0 · S render (**G4**) | renderonly, v3d, v3dv |
| `PRIME_FD_TO_HANDLE` | ✅ card0 self-import · S card0 foreign (G7) · ✅→S render (`BO_IMPORT` sent, server `-ENOSYS`, **G1**) | renderonly, v3d, v3dv, wsi |
| `drmGetDevices2`/`drmGetDevice2`/`drmGetDeviceFromDevId`/`drmGetNodeTypeFrom{Fd,DevId}`/`drmGetDeviceNameFromFd[2]`/`drmGet{Primary,Render}DeviceNameFromFd`/`drmOpen*`/`drmAvailable` | L (§2.9) | loader, egl, v3dv, wsi |

### 3.2 Display node (card0 → rpi4-kms)

| Request | Status | Mesa / notes |
|---|---|---|
| `MODE_GETRESOURCES`, `GETCONNECTOR`, `GETENCODER`, `GETCRTC` | ✅ | kmscube, wsi display, v3dv |
| `MODE_SETCRTC` | ✅ blocking (current mode only, Stage A) | kmscube legacy, SDL KMSDRM |
| `MODE_GETPLANERESOURCES`, `GETPLANE` | ✅ | wsi display |
| `MODE_OBJ_GETPROPERTIES`, `GETPROPERTY`, `GETPROPBLOB`, `CREATEPROPBLOB`, `DESTROYPROPBLOB` | ✅ | wsi display |
| `MODE_OBJ_SETPROPERTY`, `MODE_SETPROPERTY` | ✅ → one-plane atomic / DPMS no-op | wsi display (DPMS) |
| `MODE_ATOMIC` (TEST_ONLY, NONBLOCK, ALLOW_MODESET, PAGE_FLIP_EVENT) | ✅ flattened (§2.4); `IN_FENCE_FD` ✅ (in-process sync files, needs kms `-G`); `OUT_FENCE_PTR` S (**G8**) | wsi display, kmscube `-A` |
| `MODE_PAGE_FLIP` (+ EVENT) | ✅ (`TARGET_*` → `-EINVAL`: `PAGE_FLIP_TARGET` cap 0) | kmscube, SDL KMSDRM |
| `MODE_SETPLANE` | ✅ → one-plane atomic, blocking | SDL, Xorg |
| `MODE_ADDFB2` (+modifiers: LINEAR only), `MODE_ADDFB`, `MODE_RMFB`, `MODE_CLOSEFB`, `MODE_DIRTYFB` | ✅ / L (ADDFB → ADDFB2, DIRTYFB no-op) | wsi display, kmscube |
| `MODE_CREATE_DUMB`, `MAP_DUMB` (token), `DESTROY_DUMB` | ✅ | renderonly (kmsro), gbm, v3dv WSI |
| `WAIT_VBLANK` (blocking + event, high-crtc, NEXTONMISS), `CRTC_GET_SEQUENCE`, `CRTC_QUEUE_SEQUENCE` | ✅ | wsi display, Xorg |
| events (`drmHandleEvent`) | served by the server's `read()` | all KMS clients |
| `MODE_CURSOR`/`CURSOR2`, `GETGAMMA`/`SETGAMMA`, `GETFB`/`GETFB2` | S (library follow-up: cursor → cursor plane; `gamma_size` 0) | SDL, Xorg |
| `SYNCOBJ_*` on card0 | S `-EOPNOTSUPP` (`DRM_CAP_SYNCOBJ` 0 on the display node, as Linux vc4 without DRIVER_SYNCOBJ) | — |
| driver range (`DRM_IOCTL_VC4_*`) | `-EINVAL` (no 3D: what vc4's winsys probe expects) | vc4 winsys probe |

### 3.3 Render node (renderD128/card1 → rpi4-v3d-async)

| Request | Status | Mesa |
|---|---|---|
| `V3D_GET_PARAM` | ✅ (cached per connection) | v3d, v3dv, broadcom/common |
| `V3D_CREATE_BO`, `MMAP_BO` (token), `GET_BO_OFFSET`, `WAIT_BO` | ✅ (fast paths §2.3) | v3d, v3dv |
| `V3D_SUBMIT_CL`, `SUBMIT_TFU`, `SUBMIT_CSD` (legacy syncs + MULTI_SYNC) | ✅ | v3d, v3dv |
| `V3D_SUBMIT_CPU`, CPU-job extensions 0x02–0x07 | S (**G5**) | v3dv (timestamp/perf queries, indirect CSD) |
| `V3D_PERFMON_*` (and `perfmon_id ≠ 0`) | S (G5; server reserved) — Mesa does not call them while `SUPPORTS_PERFMON` = 0 | v3d, v3dv |
| `SYNCOBJ_CREATE`/`DESTROY`/`RESET`/`SIGNAL`/`WAIT` | ✅ (WAIT >8 handles: chunked / polled) | v3d, v3dv, vk_drm_syncobj |
| `SYNCOBJ_HANDLE_TO_FD`/`FD_TO_HANDLE` with `*_SYNC_FILE` | L (in-process sync files) | v3d, v3dv |
| … without the sync-file flag (opaque syncobj fds) | S (**G6**) | vk_drm_syncobj, wsi display |
| `SYNCOBJ_TRANSFER` (binary) | ✅ query + import | vk_drm_syncobj |
| `SYNCOBJ_QUERY`, `TIMELINE_WAIT`/`TIMELINE_SIGNAL` (points 0) | L / ✅ (non-zero points `-EINVAL`) | u_sync_provider |
| `SYNCOBJ_EVENTFD` | S | — |
| `MODE_*` | `-EOPNOTSUPP` (not a KMS device: `drmIsKMS` false) | — |

Raw `ioctl()` calls that bypass libdrm: `vc4_drm_winsys.c:47` (`VC4_GET_PARAM` → `-ENOTTY`, the answer
kmsro expects), `util/libsync.h` `SYNC_IOC_MERGE`/`FILE_INFO` (G15), `wsi_common_drm.c`
`DMA_BUF_IOCTL_EXPORT/IMPORT_SYNC_FILE` through `drmIoctl` on dma-buf descriptors (`ENOTTY`; G15).

## 4. Server gaps found (the M3 server work list)

Ordered by what they block. "Spec" = where the request layout is written down.

| # | Gap | Blocks | Size | Spec |
|---|---|---|---|---|
| **G2** | **Neither server answers `mtGetAttrAll`**, so `fstat()` on a DRM descriptor fails (`posix_fstat` sends it to the device server for `ftRegular` files and returns its error, `kernel posix/posix.c:1737-1817` [read]). **Mesa's `gbm_create_device()` refuses a descriptor whose `fstat` fails or is not `S_ISCHR` (`gbm/main/gbm.c:133`) and v3dv's device init fails on `fstat(primary/render)` (`v3dv_device.c:1465-1477`)** [read] | GBM (kmscube), v3dv — **the next step** | ~20 lines per server: answer `mtGetAttrAll` with `mode = S_IFCHR | 0666`, all other fields `err = 0` (size 0) | this doc |
| **G1** | `V3DA_OP_BO_IMPORT` (reserved → `-ENOSYS`): the render server opens `/kmsbuf/<id>`, maps it (UNCACHED), resolves the pages, maps them into the V3D MMU, returns a handle | Mesa kmsro/GBM scan-out (renderonly allocates on card0 and imports into v3d), v3dv WSI swapchains (`device_alloc_for_wsi`), `drmprobe prime_import_render` | ~150 lines server | `drm_phoenix_ext.h` `v3da_bo_import_req_t` |
| **G3** | `/kmsbuf` refuses `atSize` (E1's shadow-object rule), so `lseek(dmabuf_fd, 0, SEEK_END)` fails; **Mesa reads a dma-buf's size exactly that way** (`v3d_bufmgr.c:454`, `v3dv_device.c:2313,2533`) [read] | Mesa's dma-buf import on top of G1 | ~10 lines: answer `atSize` only while the buffer is exported, and clear the "exported" flag **before** `memUnexport` under the server lock (then a tree miss can never meet a positive answer — the shadow-object race stays closed) | this doc |
| **G13** | **implicit sync for flips of GPU-rendered buffers that carry no `IN_FENCE_FD`**. This is not only Xorg's problem: **kmscube's default (legacy) path is `eglSwapBuffers` → `drmModePageFlip` with no fence** — on Linux the flip waits on the buffer's dma-resv; here `rpi4-kms` would scan out a buffer the GPU is still rendering. **In-process fix, library only, once G1 exists:** record on render `PRIME_FD_TO_HANDLE` the mapping "kms export id → (render connection, render handle)"; on `PAGE_FLIP`/`ATOMIC` of a framebuffer whose dumb handle has such a record, attach that BO's mirrored last-use fence (already kept by the submit path) as the plane's `in_fence` (kms `-G` checks it on the fence page, no IPC). Cross-process producers (Xorg, compositors) need the server op `BO_LAST_FENCE` (M2 §8) | kmscube / SDL KMSDRM / single-process GBM apps (tearing, partial frames); Xorg flips (M4) | ~60 lines library (+ ~40 server for the cross-process op) | `drm_phoenix_ext.h` (23) |
| G10 | `/dev/dri/{card0,card1,renderD128}` names; one port per node for distinct `dev_t` (today card1 and renderD128 share the v3d port, so `VK_EXT_physical_device_drm` reports equal primary/render ids) | programs that hard-code `/dev/dri/card0` (SDL2 KMSDRM scans `card%d`, kmscube's default); exact dev_t identity | `create_dev(&dev, "dri/card0")` etc. beside the old names (+ optional extra ports) | this doc |
| G4 | `V3DA_OP_BO_EXPORT` + a `/v3dbuf` namespace (M1a BOs are one contiguous block each, so `memExport` works) | DRI3 (M4), Wayland dmabuf (M6), v3dv external memory, `drmprobe prime_export_render` | ~120 lines | `drm_phoenix_ext.h` (opcode 22, needs proto 3) |
| G5 | `SUBMIT_CPU` (v3dv queries, indirect CSD) and perfmons | Vulkan queries (the server already advertises `SUPPORTS_CPU_QUEUE = 1`, research §3.7) | M1 part 3 | reserved opcodes |
| G6 | cross-process syncobj / sync-file descriptors (`/v3dsync/<id>`, `atPollStatus`) | vkGetSemaphoreFd, DRI3/Present, Wayland explicit sync | M4/M6 | `drm_phoenix_ext.h` (55/56) |
| G7 | `KMS_OP_PRIME_IMPORT` (scan out a buffer another server allocated, below 1 GiB) | compositors that allocate scan-out on the render node; HEVC frames | ~100 lines | `drm_phoenix_ext.h` (38, proto 2) |
| G8 | kms out-fence (`OUT_FENCE_PTR`: a sync file signalled at flip) | kmscube `-A`, EGL `ANDROID_native_fence_sync` presentation | needs G6's descriptor kind | — |
| G9 | `GET_CRTC` exposes only the primary's pending fb; a "commit pending" flag would make blocking commits exact | blocking commits touching only overlays/cursor | 2 lines (+ a field in `kms_crtc_t`'s padding) | — |
| G12 | `poll()` on the card fd rides the kernel's 20 ms cycle (E5 §5 items 1–2, a kernel change) | 60 Hz event loops that `poll()` (Xorg, Weston, SDL) | kernel | E5 |
| G15 | `SYNC_IOC_MERGE`/`FILE_INFO` raw ioctls and `poll()` on the in-process sync files (a `dup()` of the render descriptor: `poll` becomes `atPollStatus` to rpi4-v3d-async, which answers only `atMode`), `DMA_BUF_IOCTL_*_SYNC_FILE` | EGL native-fence merging (`sync_accumulate`, `v3d_fence.c:159`, `dri2.c:179`), Vulkan WSI implicit-sync bridging. **Not** GL fencing: gallium v3d's `fence_finish` imports the sync file into a syncobj and uses `drmSyncobjWait` (`v3d_fence.c:89-107`) [read], which the emulation serves; libsync `sync_wait()` (poll) is used only by `platform_android` on our Mesa | with G6 (or `-Wl,--wrap=poll` resolving emulated sync fds on the fence page, in-process only) | — |

Not gaps but server hygiene worth fixing with G2: both `mtGetAttr` handlers already answer `atMode`
correctly (`S_IFCHR`), and `/kmsbuf` answers `atMode`/`atType` — so path resolution and `open(O_RDWR)`
of the nodes work (libphoenix `open` `stat()`s the **dummyfs** entry, not the server) [read].

## 5. Findings along the way

* **F1 — `drmWaitVBlank()` bypasses `drmIoctl()`** (`xf86drm.c:2666`, the only public entry that calls
  `ioctl()` directly besides legacy `drmDMA`). On Phoenix the raw `ioctl` reaches `rpi4-kms` as an
  ioctl-packed message and gets `-ENOTTY`. Found by the host harness (`vblank wait_rc=-1`), fixed in
  patch 0002 (`drmIoctl` under `__phoenix__`).
* **F2 — `kms_proto.h` pairs `KMS_VBL_RELATIVE`/`KMS_VBL_ABSOLUTE` with the wrong DRM names** in its
  comments (DRM: `_DRM_VBLANK_ABSOLUTE = 0`, `_DRM_VBLANK_RELATIVE = 1`; the server treats bit 0 as
  *absolute*, `kms_main.c:1071`). The server is self-consistent; any client that takes the comments at
  face value passes DRM `type` values through and waits on the wrong target. libdrm-phoenix sends only
  absolute targets it computed itself. Fix the comment when `kms_proto.h` is next touched.
* **F3 — `fstat()` and `lseek(SEEK_END)` on server descriptors** fail today (G2, G3); both are on
  Mesa's GBM/v3dv/dma-buf paths (§4).
* **F4 — `open_memstream`**: meson has computed `HAVE_OPEN_MEMSTREAM` all along but `xf86drm.c` never
  checked it; the patch makes the two modifier-name printers fail soft (they already return NULL on
  any `open_memstream` failure). E7's link stub is no longer needed; libphoenix gaining the function
  (libc-gaps branch) turns the real code back on automatically.
* **F5 — v3dv needs a primary node on the render device** (§2.9) — Linux's topology (card1) is the
  answer, not a v3dv patch.
* **F6 — `drmHandleEvent` reads into a 1 KiB stack buffer**: every event `read()` therefore pays one
  unaligned-window shadow page (E5, ~30 µs per event). Irrelevant at 60 Hz; noted for high-rate event
  loops (a patch could read into an aligned static buffer).

## 6. Build and host tests

### 6.1 Build

```
tools/gpu-lane/libdrm-phoenix/build.sh            # libdrm.a + drmprobe  (≈1 min first time)
tools/gpu-lane/libdrm-phoenix/build.sh --lib-only # libdrm.a only
tools/gpu-lane/libdrm-phoenix/build.sh --clean
```

Writes only `tools/gpu-lane/libdrm-phoenix/build-out/` (gitignored): `src/` (a `git clone -s` of
`external/libdrm` at `b97cbde` + the patches + the backend in `src/phoenix/`), `build/` (meson),
`prefix/{lib/libdrm.a, include/{xf86drm.h,xf86drmMode.h,libdrm/}}`, `drmprobe`, the logs, and
`libdrm-phoenix-full.patch` (everything as one diff, for porting into the ports framework later).
Inputs: the tree sysroot (it checks for `sys_fdpath` — the toolchain's own `libphoenix.a` lags),
the toolchain, `tools/gpu-lane/e7-drm-build/bin/phx-gcc` (drops `-pthread`), and read-only the two
servers' wire headers and `v3da_clgen.c`. Options as E7 (vc4 headers on, every other driver off, no
tests/man/udev/valgrind), `default_library=static`, `--buildtype=debugoptimized`.

Result [built]: `libdrm.a` 902 KB (with debug info), **0 warnings in the backend** under libdrm's
warning set; the 9 remaining warnings are upstream's (8 × `#warning "Missing implementation of
drmParse…"` for non-Linux, and `drmGetMinorName` unused on any non-Linux/non-FreeBSD target).
`drmprobe` 1.2 MB static, linked with `-Wl,--wrap=mmap`. Linking against libdrm-phoenix: add
`prefix/include` + `prefix/include/libdrm` to the include path, `prefix/lib/libdrm.a` and
`-Wl,--wrap=mmap` to the link.

### 6.2 Host tests (no Pi, seconds)

```
tools/gpu-lane/libdrm-phoenix/hosttest/run.sh
```

1. **Logic** (`hosttest.c`, gcc + ASan/UBSan, pristine libdrm headers): 134 checks — atomic flattening
   (kmscube's first commit, partial flips over a baseline, stale fences never carried over, every error
   class), plane-state from properties, `GETPROPERTY` fill incl. capacity limits, `VERSION`
   truncation, `drmDevice` layout freed by libdrm's own free logic, ioctl-number decoding in both
   `_IOC` layouts, token encoding, and byte-layout identity of every DRM struct/constant `kms_proto.h`
   copies. **PASS.**
2. **End to end** (`e2e`): the real `drmprobe.c` + the **patched** libdrm (`xf86drm.c`,
   `xf86drmMode.c`, …) + the backend, built natively with Phoenix stand-ins (`mock/`: `msgSend`,
   `lookup`, `sys_fdpath`, HELLO ioctls, `MAP_PHYSMEM`, `/kmsbuf` descriptors that map only with
   `MAP_UNCACHED`, event `read()`) and fake servers modelled on `kms_main.c`/`v3da_main.c`. Run twice:
   with today's names (`/dev/kms`, `/dev/v3d-async`) and with `/dev/dri` names registered. **PASS**
   both: every line ok except the two pixel checks of the CL clear (the fake GPU runs nothing — the
   expected result), `prime_import_render`/`prime_export_render` answered as the predicted gaps,
   `unaligned_ends=0`. It found F1.

## 7. Pre-registered Pi test (one netboot cycle)

**Question:** does upstream libdrm, through libdrm-phoenix, drive both servers the way a Linux DRM
program does — identity, KMS enumeration, dumb buffers mapped by plain `mmap()`, vsynced page flips
with events, atomic commits, a V3D clear job with syncobjs, a GPU→display in-fence across the two
servers, PRIME export with zero-copy mapping — and do the gaps answer as predicted?

**Preconditions:** current netboot image (build ≥ 9: E1 export, port-death, vcmbox XL); **no GPU app,
no X, no SDL program, no `rpi4-v3d`**; `rpi4-v3d-async` from a proto-2 build (the current M1 part-2
server; `drmprobe` sends `V3DA_PROTO_VERSION` 2 and `KMS_PROTO_VERSION` 1, and both servers refuse a
mismatching HELLO with `-EPROTO`); `rpi4-kms` from `tools/gpu-lane/kms/out/`.

**Build + stage (coordinator):**

```
tools/gpu-lane/libdrm-phoenix/build.sh
EXPORT=$(awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports)
sudo cp tools/gpu-lane/libdrm-phoenix/build-out/drmprobe "$EXPORT/bin/"
# plus rpi4-v3d-async, v3dasync-ping, rpi4-kms, kmstest if not staged already
```

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label m3-drmprobe --idle-secs 8 --max-cmd-secs 120 \
    --hdmi-dense-on 'DRMPROBE kms_flip start' -- \
    "/bin/rpi4-v3d-async -r 1" \
    "/bin/rpi4-kms -G" \
    "/bin/drmprobe -n 120" \
    "/bin/drmprobe -n 30" \
    "/bin/kmstest stats" \
    "/bin/kmstest quit" \
    "/bin/v3dasync-ping stats" \
    "/bin/v3dasync-ping quit"
```

Order matters: the render server first (`rpi4-kms -G` connects to it at start for the fence page).
Both servers detach (psh has no `&`). The second `drmprobe` checks that a fresh process gets fresh
clients and that nothing leaked in the servers. Wall clock ≈ netboot + 8 × (8 s + few s) + ~10 s of
probe ≈ 4–5 min. Grade from tagged lines only:

```
grep -a -E '^(DRMPROBE|KMS|KMSTEST|V3DA|V3DAPING) ' artifacts/rpi4b-uart/rpi4b-uart-*-m3-drmprobe.log
./scripts/uart-summary.sh m3-drmprobe
```

Allow for ~1.3 % UART line corruption (re-read, don't count); EL0 dumps print twice.

**Predictions (per line, first `drmprobe`):**

| Line | Predicted | If instead… |
|---|---|---|
| `devices count_null=2 n=2 ok=1`, `device i=0 … fullname=/gpu compat=brcm,bcm2711-vc5 primary=/dev/kms render=-`, `device i=1 … primary=/dev/v3d-async render=/dev/v3d-async` | as listed (no `/dev/dri` names yet: G10) | `n<2`: a server not running / name not registered — read the `V3DA srv`/`KMS srv` lines. `/dev/dri/…` shown: someone registered them. |
| `open node=card … rdwr=1 … ok=1`, `open node=render … rdwr=1 … ok=1` | **first `O_RDWR` open of either server** (all earlier clients used `O_RDONLY`); predicted to work because libphoenix's `open` `stat()`s the dummyfs entry, not the server [read] | `rdwr=0 rdwr_errno=…`: `O_RDWR` open fails — **blocker for Mesa** (loader/v3dv open `O_RDWR`); the probe continues on the `O_RDONLY` fallback. |
| `identity node=card version=vc4 0.0.0 … node_type=0 is_kms=1 name2=/dev/kms primary_name=/dev/kms render_name=- get_device=0 … ok=1` | as listed | `is_kms=0`: GETRESOURCES marshalling; `version=-`: the connection could not be identified (HELLO failed: read errno in the next lines). |
| `identity node=render version=v3d 1.0.0 … node_type=2 is_kms=0 … primary_name=/dev/v3d-async render_name=/dev/v3d-async … ok=1` | as listed | — |
| `caps universal=0 atomic=0 dumb=1 prime=3 monotonic=1 crtc_in_vblank=1 async_flip=0 ok=1` | as listed (M2 §3.2 caps) | — |
| `resources crtcs=1 connectors=1 encoders=1 fbs=0 min=1x1 max=4096x4096 ok=1` | as listed | — |
| `connector id=0x20 type=11 type_id=1 connection=1 mm=…x… modes=1 mode0=1920x1080 1920x1080@60 clock=1485xx encoder=0x30 props=5 ok=1` | as listed (synthesized mode; `clock` 148500 or the 60.01 Hz-corrected 1485xx) | `modes=0`/`connection≠1`: see `KMS mode` line. |
| `encoder … crtc=0x40 possible_crtcs=0x1 ok=1`, `crtc id=0x40 fb=0 mode_valid=1 mode=1920x1080@60 gamma=0 ok=1` | as listed | — |
| `planes n=2 ok=1`, `plane id=0x50 type=1 … formats=2 fmt0=0x34325258 props=16`, `plane id=0x57 type=2 …`, `crtc_prop ACTIVE=1 MODE_ID=… OUT_FENCE_PTR=0 VRR_ENABLED=0`, `properties primary=0x50 total=32 enum_ok=1 fmt=0x34325258 ok=1` | as listed (plane backend: primary + cursor, XRGB8888) | `n=0`: universal-planes cap not stored; `enum_ok=0`: GETPROPERTY enum fill. |
| `dumb i=0 create=0 handle=… pitch=7680 size=8294400 map=0 token=0x1000000000…000 mmap=ok readback=1 fb=4096 ok=1`, same for `i=1` | as listed — **plain `mmap()` of a token through `__wrap_mmap`** | `mmap=Invalid argument`: memtype not forced / wrap not linked; `create≠0`: pool exhausted (M2 32 MiB holds 3 × 1080p). |
| `setcrtc rc=0 … blocking_ms=17–35 ok=1` + HDMI: teal background, white square left | as listed | `blocking_ms` 0: the blocking wait returned early (G9 hypothesis); console still visible: see `KMS apply FAIL`. |
| `flip_event rc=0 events=1 seq=… latency_us=8000–35000 ok=1` | one event, ≤ 2 vblanks | `rc=-110`/timeout: event not delivered to this client's queue or `read()` path broken. |
| `flips n=120 done=120 rc=0 ms≈2000 fps_x100≈5990–6010 vblanks=120 missed_vblanks=0 ok=1` + HDMI: magenta/teal alternating with the square moving right | 60 fps, ≤ 2 missed (M2 target rule: commit right after the event → next vblank) | `fps≈30`: every flip lands two vblanks late → IPC/draw after the event exceeds the 2 ms guard window (look at `KMSTEST stats`); `rc=-16`: a flip issued while one was pending (event delivered before completion). |
| `vblank wait_rc=0 seq=… waited_us=0–17000 … get_seq_rc=0 seq64=… ok=1` | as listed (drmWaitVBlank through the F1 patch) | `wait_rc=-1`: the patch 0002 hunk is missing (stale build). |
| `poll flip_rc=0 poll=1 revents=0x1 wake_us=0–36000 ok=1` | POLLIN within ~2 frames (20 ms poll cycle, E5) | `poll=0`: `atPollStatus` readiness broken. |
| `atomic fb_prop=0x101 test_only=0 commit=0 events=1 ok=1` | as listed (partial request: FB_ID only → flattened over the mirror) | `commit=-22`: flattening produced an invalid state (compare the `KMS` validation line if logged). |
| `v3d_params ident0=0x04443356 hub_ident1=0x000e1124 tfu=1 csd=1 cache_flush=1 multisync=1 cpu_queue=1 perfmon=0 ok=1` | as listed (live registers) | `0xdead…`: GET_PARAM marshalling. |
| `render_caps syncobj=1 timeline=0 prime=3 ok=1` | as listed | — |
| `v3d_bo create=0 handle=0x…2001 gpuva=0x… cpu=0x… readback=1 wait_idle=0 ok=1` | as listed | `readback=0`: token → PHYS mapping. |
| `cl_clear rc=0 pixels_ok=1 rt0=0xff3366cc wait_bo=0 us=… sizes=276/396/12288/256 ok=1` | the M1-proven clear, now through `DRM_IOCTL_V3D_SUBMIT_CL` + `drmSyncobjWait` | `rt0=0xdeadbeef`: job not run/not waited (fence fast path wrong); `rc=-22`: submit buffer layout. |
| `cl_clear_dep rc=0 pixels_ok=1 rt0=0xff00ff00 ok=1` | second job with `in_sync_bcl` = the first's syncobj | `rc=-22`: in-sync resolution. |
| `syncobj export=0 sfd=… import=0 wait=0 first=0 transfer=0 wait_t=0 wait_empty=-22 signal=0 wait_s=0 ok=1` | as listed (sync-file emulation, transfer, reset/signal semantics) | — |
| `in_fence fence_prop=0x10b commit=0 errno=0 events=1 ok=1` | **first cross-server fence**: `rpi4-kms -G` checks the render fence page and flips | `commit=-1 errno=19` (ENODEV): kms started without `-G` or its v3d connect failed (`KMS v3d connect=0`); `errno=22`: the sync file did not resolve. |
| `prime_export rc=0 fd=… path=/kmsbuf/<h> mmap_same_pages=1 self_import=0 handle=h/h ok=1` | zero-copy dma-buf mapping of a dumb buffer | `mmap_same_pages=0`: different pages (shadow object) — blocker. |
| `prime_import_render rc=-1 errno=… handle=0x0 gap=1` | `errno` = ENOSYS (**G1**) | `gap=0` with rc 0: G1 was implemented meanwhile — PASS. |
| `prime_export_render rc=-1 errno=… gap=1` | ENOSYS (**G4**, library-local) | — |
| `RESULT pass=28 fail=0 gap=2 failed=- secs≈4–8 verdict=PASS` | as listed | any `failed=` key: the row above says what it means. |
| second `drmprobe -n 30` | same lines, `n=30`, `RESULT … verdict=PASS` (fresh clients: the first process's were released by the kernel's `mtClose` at exit) | failure only on the second run: server-side client/slot reuse or a leak (`KMSTEST stats` `bos`/`exports`). |
| `KMSTEST stats … apply_errors=0 dropped=0 bos=0 exports=0` | no leaks after both probes exited (client death cleans up) | `bos>0`: dumb BOs outlive their client. |
| `V3DAPING stats … parked=0 … pages_to_kernel=0` and `quit rc=0` lines | clean | parked > 0: a wait left parked. |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | any fault: `addr2line` the PC (`build-out/drmprobe` is unstripped). |

**What the cycle decides:** PASS = libdrm-phoenix works end to end for everything the servers
implement; the next step (§8) can start on G2 + G1. A failure in identity/transport lines is a
library bug (fix, re-run); a failure in `in_fence`/`prime_export` with the rest green is an
integration question between the servers, not a library one.

## 8. Next step: `mesa-drm` static build + kmscube

1. **Server work first (blocking):** G2 (`mtGetAttrAll` in both servers), G1 (`BO_IMPORT`), G3
   (`/kmsbuf` `atSize` for live exports), G10 (`dri/` names) — all additive, together ≈ 300 lines.
   **Library work with it:** G13's in-process implicit sync (export id → render BO → last-use fence
   as the flip's `in_fence`), so kmscube's fence-less legacy flips never scan out a half-rendered
   buffer; `rpi4-kms` then always runs with `-G`.
2. **Mesa** from a `phoenix-drm` branch of the fork (E7 §3.3 option (a): 26.2.0 + genuine driver fixes
   + the 13-line OS patch + static-library/GBM-builtin-backend meson changes), configured with
   `PKG_CONFIG_LIBDIR` pointing at `build-out/prefix` (add a `libdrm.pc` there, or pass the
   dependency by hand) — **never** the shared sysroot (header-poisoning rule). Link every Mesa
   program with `-Wl,--wrap=mmap` (then no Mesa mmap patch is needed); the old lane's `__phoenix__`
   hooks must not be in that branch (E7 §3.3).
3. **kmscube** (legacy mode first: `drmModeSetCrtc` + `drmModePageFlip` + GBM + EGL on GBM + GLES2),
   then `-A` atomic once G8 exists. Probe tags from E7's `kmscube-lite.c`. Pre-register as its own
   cycle; it is also the first test of kmsro's renderonly import (G1) and of the v3d render targets
   in kms pool memory (below 1 GiB by construction).
4. Then SDL2 KMSDRM (needs G10's `/dev/dri/card%d` scan), glmark2-es2-drm, the games.

## 9. Risks

| # | Risk | Handling |
|---|---|---|
| R1 | `--wrap=mmap` is a link-time contract: a program linked without it maps DRM buffers with `mmap(fd, token)` and fails | fails loudly (`mmap` → EINVAL/ENOMEM, Mesa aborts "map failure"), never silently; document in the ports recipe; the Mesa-patch alternative stays available (§2.6) |
| R2 | Descriptor-number reuse: libdrm has no close hook. Render: detected (slot generation); display: detected when the server rejects the client id; undetected only when the kms server hands the same id to the new open of the same node in the same process — then only local mirrors (plane states, dumb memrefs) are stale | the plane mirror is re-seeded after any failed commit path would need it; worst case one wrong-baseline atomic commit. A kms "client generation" in HELLO would close it (server, 4 lines). |
| R3 | Cross-process access control: any process that learns a client id could talk to `{port, id}` (both servers log `pid ≠ client pid` but do not enforce, M1/M2) | server-side enforcement at the M1/M2 gates; unchanged by this library |
| R4 | In-process sync files: a sync-file number recycled by `close()`/reopen resolves to the newest snapshot of that number | as the M1 adapter; real sync files are G6 |
| R5 | `drmIoctl` latency: every KMS call is ~31 µs of IPC; the atomic path adds one `GET_PROPERTIES` per plane on first use (mirror seeding) | per-frame cost ≈ one commit round trip (page-rounded payload) — within the E5 budget |
| R6 | Protocol drift: the library compiles the servers' headers; a bumped `*_PROTO_VERSION` makes HELLO fail with `-EPROTO` (loud); the `#error` guards force the `_EXT` requests to be reconciled | rebuild libdrm-phoenix with every server protocol bump |
| R7 | The fake servers are models: the host e2e proves the library's marshalling against my reading of the servers, not the servers themselves | the Pi cycle (§7) is the ground truth; the fakes follow `kms_main.c`/`v3da_main.c` handler by handler |

## Result

*(to be filled after the §7 cycle: log path, snapshot paths, the tagged lines, the rows that applied)*
