# M3 part 1 — `libdrm-phoenix`: upstream libdrm over the Phoenix DRM servers

Milestone M3 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §4.1 (option
C), §4.4, §4.6 and §5 (M3). Builds on [E7](E7-drm-userspace-build.md) (the libdrm cross build and the
`drmIoctl()` / identity seams), [E1](E1-vm-object-export.md) (`memExport`, fd per buffer),
[E5](E5-deferred-reply.md) (IPC costs, blocking `read()` events, `poll()` quantisation),
[M1](M1-async-render-server.md) (`rpi4-v3d-async`, `v3da_proto.h`) and
[M2](M2-kms-server.md) (`rpi4-kms`, `kms_proto.h`).

**Status (2026-09-27, latest):** the first kmscube run (`m3p3-kmscube`) crashed at `eglMakeCurrent`. The cause is a
Mesa OS gate (`caps.dmabuf` never queried on Phoenix), fixed in Mesa patch 0008. See
[M3 part 3 — first kmscube run: analysis](#m3-part-3--first-kmscube-run-analysis-2026-09-27); the next cycle,
`m3p3b-kmscube`, is pre-registered there.
**Earlier:** part 2 closed server gaps G2/G1/G3/G10 and library G13 — see
[M3 part 2](#m3-part-2--server-gaps-closed-2026-09-27) (host-tested, Pi cycle pre-registered). Part 1: code complete, **builds** (static `libdrm.a` + `drmprobe` for aarch64-phoenix,
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
  buffer → `-ENOSYS` (`KMS_OP_PRIME_IMPORT`, G7). *Later:* G7 implemented — a `/v3dbuf` export imports
  (kms proto 2), see [M6 §16](M6-wayland.md).
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
| `PRIME_FD_TO_HANDLE` | ✅ card0 self-import · ✅ card0 foreign `/v3dbuf` (G7, implemented, pending Pi `m6h-g7`) · ✅→S render (`BO_IMPORT` sent, server `-ENOSYS`, **G1**) | renderonly, v3d, v3dv, wsi |
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
| **G2** ✅ closed — part 2 | **Neither server answered `mtGetAttrAll`**, so `fstat()` on a DRM descriptor fails (`posix_fstat` sends it to the device server for `ftRegular` files and returns its error, `kernel posix/posix.c:1737-1817` [read]). **Mesa's `gbm_create_device()` refuses a descriptor whose `fstat` fails or is not `S_ISCHR` (`gbm/main/gbm.c:133`) and v3dv's device init fails on `fstat(primary/render)` (`v3dv_device.c:1465-1477`)** [read] | GBM (kmscube), v3dv — **the next step** | ~20 lines per server: answer `mtGetAttrAll` with `mode = S_IFCHR | 0666`, all other fields `err = 0` (size 0) | this doc |
| **G1** ✅ closed — part 2 | `V3DA_OP_BO_IMPORT` (was reserved → `-ENOSYS`): the render server opens `/kmsbuf/<id>`, maps it (UNCACHED), resolves the pages, maps them into the V3D MMU, returns a handle | Mesa kmsro/GBM scan-out (renderonly allocates on card0 and imports into v3d), v3dv WSI swapchains (`device_alloc_for_wsi`), `drmprobe prime_import_render` | ~150 lines server | `drm_phoenix_ext.h` `v3da_bo_import_req_t` |
| **G3** ✅ closed — part 2 | `/kmsbuf` refused `atSize` (E1's shadow-object rule), so `lseek(dmabuf_fd, 0, SEEK_END)` fails; **Mesa reads a dma-buf's size exactly that way** (`v3d_bufmgr.c:454`, `v3dv_device.c:2313,2533`) [read] | Mesa's dma-buf import on top of G1 | ~10 lines: answer `atSize` only while the buffer is exported, and clear the "exported" flag **before** `memUnexport` under the server lock (then a tree miss can never meet a positive answer — the shadow-object race stays closed) | this doc |
| **G13** ✅ closed (in-process) — part 2 | **implicit sync for flips of GPU-rendered buffers that carry no `IN_FENCE_FD`**. This is not only Xorg's problem: **kmscube's default (legacy) path is `eglSwapBuffers` → `drmModePageFlip` with no fence** — on Linux the flip waits on the buffer's dma-resv; here `rpi4-kms` would scan out a buffer the GPU is still rendering. **In-process fix, library only, once G1 exists:** record on render `PRIME_FD_TO_HANDLE` the mapping "kms export id → (render connection, render handle)"; on `PAGE_FLIP`/`ATOMIC` of a framebuffer whose dumb handle has such a record, attach that BO's mirrored last-use fence (already kept by the submit path) as the plane's `in_fence` (kms `-G` checks it on the fence page, no IPC). Cross-process producers (Xorg, compositors) need the server op `BO_LAST_FENCE` (M2 §8) | kmscube / SDL KMSDRM / single-process GBM apps (tearing, partial frames); Xorg flips (M4) | ~60 lines library (+ ~40 server for the cross-process op) | `drm_phoenix_ext.h` (23) |
| G10 ✅ closed — part 2 | `/dev/dri/{card0,card1,renderD128}` names; one port per node for distinct `dev_t` (today card1 and renderD128 share the v3d port, so `VK_EXT_physical_device_drm` reports equal primary/render ids) | programs that hard-code `/dev/dri/card0` (SDL2 KMSDRM scans `card%d`, kmscube's default); exact dev_t identity | `create_dev(&dev, "dri/card0")` etc. beside the old names (+ optional extra ports) | this doc |
| **G4** implemented, pending Pi (`m6g-g4`, [M6 §15](M6-wayland.md)) | `V3DA_OP_BO_EXPORT` (22) + a `/v3dbuf` namespace (M1a BOs are one contiguous block each, so `memExport` works); import `ns=v3dbuf` shares the exported BO itself (same handle, GPU VA and last-use record) and holds a reference; every open `/v3dbuf/<id>` descriptor holds one too. Protocol 3; the server accepts HELLO 2..3, so proto-2 binaries are unaffected, and libdrm-phoenix falls back to 2 against an old server (export → `ENOSYS` as before). Host-tested: `hosttest/run.sh` (G4 tests pass against the fake G4 server and fail with `errno=38` against a fake proto-2 one). **G4a** — render-node export of an *imported* BO (reopen the exporter's name; the v3dv WSI's `vkGetMemoryFdKHR`) — closed in the library by [M5](M5-vulkan.md) §4.1 (with **G17**, `SET_CLIENT_CAP(ATOMIC)` ⇒ universal planes, §4.2) | DRI3 (M4), Wayland dmabuf (M6), v3dv external memory, `drmprobe prime_export_render` / `prime_import_render2` / `prime_export_xproc` | ~350 lines server + ~80 library (+ probe and fake) | `v3da_proto.h` (`V3DA_OP_BO_EXPORT`, `V3DA_BUF_NS`, `V3DA_PROTO_BASE`) |
| G5 | `SUBMIT_CPU` (v3dv queries, indirect CSD) and perfmons | Vulkan queries (the server already advertises `SUPPORTS_CPU_QUEUE = 1`, research §3.7) | M1 part 3 | reserved opcodes |
| G6 | cross-process syncobj / sync-file descriptors (`/v3dsync/<id>`, `atPollStatus`) | vkGetSemaphoreFd, DRI3/Present, Wayland explicit sync | M4/M6 | `drm_phoenix_ext.h` (55/56) |
| **G7** implemented, pending Pi (`m6h-g7`, [M6 §16](M6-wayland.md)) | `KMS_OP_PRIME_IMPORT` (38, kms proto 2; the server accepts HELLO 1..2, libdrm-phoenix falls back to 1 → `ENOSYS` as before): rpi4-kms opens `/v3dbuf/<id>` itself, maps it (UNCACHED), checks contiguity, and **keeps the descriptor open** (the render BO's reference) until the handle is closed and no framebuffer can be on a plane; ADDFB2 refuses a buffer at/above 1 GiB (`why=above_1g`), non-LINEAR, pitch/offset not 64-aligned (`EINVAL`). Host-tested (fake server + negative controls; `kms/hosttest` rule checks). Another client's `/kmsbuf` export: `EINVAL` (follow-up). Cross-process flips carry no fence (`BO_LAST_FENCE`) | Weston direct scan-out, Xorg Present flips of client pixmaps, HEVC frames | ~470 lines server + a 70-line rules header, ~45 library (+ probe, fake) | `kms_proto.h` (`KMS_OP_PRIME_IMPORT`, `kms_prime_import_req_t`, `KMS_PROTO_BASE`) |
| G8 | kms out-fence (`OUT_FENCE_PTR`: a sync file signalled at flip) | kmscube `-A`, EGL `ANDROID_native_fence_sync` presentation | needs G6's descriptor kind | — |
| G9 | `GET_CRTC` exposes only the primary's pending fb; a "commit pending" flag would make blocking commits exact | blocking commits touching only overlays/cursor | 2 lines (+ a field in `kms_crtc_t`'s padding) | — |
| G12 | `poll()` on the card fd rides the kernel's 20 ms cycle (E5 §5 items 1–2, a kernel change) | 60 Hz event loops that `poll()` (Xorg, Weston, SDL) | kernel | E5 |
| G15 (merge/info ✅ in-process, [M5](M5-vulkan.md) §9.3: `-Wl,--wrap=ioctl`; **v3dv needs it on every signalling `vkQueueSubmit`** — the m5-vkcube hang) | `SYNC_IOC_MERGE`/`FILE_INFO` raw ioctls and `poll()` on the in-process sync files (a `dup()` of the render descriptor: `poll` becomes `atPollStatus` to rpi4-v3d-async, which answers only `atMode`), `DMA_BUF_IOCTL_*_SYNC_FILE` | EGL native-fence merging (`sync_accumulate`, `v3d_fence.c:159`, `dri2.c:179`), Vulkan WSI implicit-sync bridging. **Not** GL fencing: gallium v3d's `fence_finish` imports the sync file into a syncobj and uses `drmSyncobjWait` (`v3d_fence.c:89-107`) [read], which the emulation serves; libsync `sync_wait()` (poll) is used only by `platform_android` on our Mesa | with G6 (or `-Wl,--wrap=poll` resolving emulated sync fds on the fence page, in-process only) | — |

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

1. ✅ **Done in part 2 (2026-09-27, Pi cycle pending):** ~~Server work first (blocking):~~ G2 (`mtGetAttrAll` in both servers), G1 (`BO_IMPORT`), G3
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

## M3 part 2 — server gaps closed (2026-09-27)

**Status:** code complete for G2, G1, G3, G10 (servers) and G13 (library); both servers and
libdrm-phoenix **build** (`-Wall -Wextra -Werror`, 0 backend warnings under libdrm's set);
**host-tested** (logic 134/134 + the real `drmprobe` against the extended fake servers, both modes
PASS, with a negative control for G13); **no Pi cycle yet** — pre-registered below. No protocol
version changed; nothing committed, nothing staged. Also in this pass: the two `m2-kms-a` defects
(empty flip events, exit crash) — see the fix under M2's Result section.

### What changed, per gap

| Gap | Where | What |
|---|---|---|
| **G2** fstat | `v3da_main.c` `attr_all()`, `kms_main.c` + `kms_bo.c` `kms_attr_all()` | Both node servers answer `mtGetAttrAll` with a full `struct _attrAll` (`mode = S_IFCHR\|0666`, every `err = 0` among the ten fields `posix_fstat` checks, `links 1`, `ioblock 4096`). `st_rdev` is set by the kernel from the descriptor's port (`posix.c:1735`), never from `attrs.dev` — which is why G10 needs a port per node. `/kmsbuf` answers it too (`S_IFCHR`, size while exported), so `fstat()` of a dma-buf descriptor and `open(O_RDWR)` of a buffer name (libphoenix `stat()`s it) now work. First answer logs `V3DA srv fstat answered …` / `KMS srv fstat answered …`. |
| **G1** BO_IMPORT | `v3da_proto.h` (request moved here from `drm_phoenix_ext.h`, union member `bo_import`), `v3da_bo.c` `v3da_bo_import()`, `v3da_main.c` `bo_import_request()` | Served **without `srv.lock`** (like `DBG_IRQ_SELFTEST`): `lookup("/kmsbuf/<id>")` must resolve to the request's `{port, id}`; `open(O_RDONLY)`; size = request size or, if 0, `lseek(SEEK_END)` (G3); `mmap(PROT_READ, MAP_UNCACHED iff the export is)`; each page touched then `va2pa` (present pages only; refuses a PA the 32-bit PTE cannot hold). Then locked: re-check the client is the same open (slot generation), dedupe, slot + GPU VA, one PTE per page, a normal generation-tagged handle, `refs = 1`. **Same client + same buffer → same handle, no extra reference** (DRM: one `GEM_CLOSE` releases it; Mesa's BO tables rely on it). `BO_MMAP` of an import answers the exporter's OID memref, never a PA. Release = the ordinary quarantine (PTEs cleared, TLB, every queue past the release point), then **`munmap`, never the BO pool** (`block_get` zeroes pooled blocks — it would wipe the exporter's buffer). Client death drops it like any owned BO. The mapping is the E1 window reference, so the pages outlive a kms-side destroy until the GPU is provably done. Lines: `V3DA srv import handle=… ns=kmsbuf id=… pages=… pa0=… contiguous=… gpuva=…`, `V3DA srv import released …`, `V3DA srv import FAIL … rc=`. `ns=V3DBUF` → `-ENOSYS` on a proto-2 server; since G4 (proto 3) it imports one of this server's own exports, locked, without opening anything: the importer shares the BO and holds a reference ([M6 §15](M6-wayland.md)). |
| **G3** atSize | `kms_bo.c` `kms_bufns_thread`, `kms_bo_unref`, `kms_pool_fini` | `/kmsbuf` answers `atSize` for id ≠ 0 **only while `exported`**, under `srv.lock`; `exported` is cleared **before** `memUnexport`, under the same lock (unref and pool teardown). The kernel asks `atSize` (`proc_size`) from `vm_objectGet` only after a tree miss, i.e. after the window was withdrawn — then the answer is `-ENOENT` and no shadow object can be made. Residual (documented in the code): a positive reply given just before an unref whose `memUnexport` lands before the kernel's re-lookup inserts a file object under an id that is **never exported again** (kms handles are never reused) — that one `mmap` gets unbacked pages, nobody else's memory. First answer logs `KMS srv kmsbuf atSize id=… (first; G3)`. |
| **G10** names | `v3da_proto.h` (`V3DA_DRI_*_NAME`), `v3da_main.c` `dri_name()`, `kms_proto.h` (`KMS_DRI_NAME`), `kms_main.c` `claim_names()`/`names_release()` | kms: `/dev/dri/card0` on the **same** port as `/dev/kms` (one node, one dev_t). v3d: `/dev/dri/renderD128` on the main port (alias of `/dev/v3d-async`), `/dev/dri/card1` on a **second port** with its own receiving thread — full protocol, shared client table. Rids are per port, so a parked wait now remembers its arrival port (`v3da_wait_t.port`, set from `srv.rx_port` under the lock) and is answered there. All dri names are best effort (the legacy name stays the single-owner guard; a stale dri name from a dead server is reclaimed, a live one left alone) and are removed on a clean quit. Library: `srv_of_devid()` maps card1's dev_t to the v3d primary node. Lines: `V3DA srv dri name=/dev/dri/… port=… registered=1`, `KMS srv dri name=/dev/dri/card0 … registered=1`. |
| **G13** implicit flip sync | `drm_phoenix_v3d.c` (process-wide import table `IMP`, `drmphx_v3d_implicit_fence/_wait`), `drm_phoenix_kms.c` (`fb_note/fb_drop`, `implicit_attach`, `implicit_fallback`) | A render-node `PRIME_FD_TO_HANDLE` records `{ns port, id} → (render connection, handle)`; `ADDFB2` records `fb → dumb handle`. On `PAGE_FLIP` and on non-TEST_ONLY `ATOMIC`, a plane whose `IN_FENCE_FD` is unset and whose fb's dumb buffer was imported gets that BO's mirrored last-use fence — **only if it has not signalled** (so idle buffers flip exactly as before), no IPC. `rpi4-kms -G` then gates the flip on the render fence page. If kms answers `-ENODEV` (no `-G`), the library latches that per connection, prints one stderr line, waits on the CPU and retries without the fence. Entries die on `GEM_CLOSE` and with the connection (lock order `G.lock → IMP.lock → conn->lock`). Cross-process producers still need `BO_LAST_FENCE` (M4). |

**A pre-existing library bug found by the new host test:** `sync_wait()`'s single-handle path turned
`drmSyncobjWait(…, INT64_MAX)` (`glFinish`, drmprobe's `cl_clear`) into a **zero-timeout poll**
(`FOREVER_NS` is negative and was clamped to 0), answering `-ETIME` whenever the job was still
running. The old fake GPU completed jobs at submit, so it never showed; the M3 part-1 Pi cycle would
have (a 64×64 clear usually finishes within the IPC round trip, so possibly intermittently). Fixed
in `drm_phoenix_v3d.c`; the fake GPU now completes jobs lazily (at the next wait or fence-gated
flip), which is what exposed it.

### Compatibility

* **No protocol version changed** (`V3DA_PROTO_VERSION` 2, `KMS_PROTO_VERSION` 1); both HELLO
  structs untouched (their size is encoded in the ioctl number — growing one would turn every old
  client's HELLO into `-ENOTTY`). Everything is additive: a reserved opcode now implemented, a new
  union member of the same size, new message types answered, new names beside the old ones.
* `quakespasm-v3da` / `stk-v3da` / `v3dasync-ping` (libv3da-client, proto 2 on `/dev/v3d-async`):
  same port, same HELLO, same opcodes; the only dispatch changes are the unlocked `BO_IMPORT` branch
  and answering on the arrival port (identical for the main port). `kmstest` (proto 1 on `/dev/kms`):
  unchanged, and its flip events now arrive (the M2 fix is server-side, so the **old** staged kmstest
  benefits too; the `read_dump` lines need the new one).
* libdrm-phoenix still requires exact versions — unchanged versions keep that correct. It sends
  `BO_IMPORT` unconditionally: a part-2 (pre-M3p2) server answers `-ENOSYS`, the part-1 gap
  behaviour. drmprobe grades every new check as a **gap** (not a failure) when it meets servers
  from before this pass: fstat all `ENOSYS`, `atSize` `ENOENT`, import `ENOSYS`, no `card1`.
* `drm_phoenix_ext.h` now `#error`s against a `v3da_proto.h` without `V3DA_HAVE_BO_IMPORT`.
* `hosttest/run.sh` takes `DRMPHX_OUT` (the build dir; default `build-out/`) and now expects the
  part-2 keys, so it grades the current sources only. **Until `build-out/` is rebuilt, run it as
  `DRMPHX_OUT=tools/gpu-lane/libdrm-phoenix/build-out-m3p2 …/run.sh`** — a bare run against the
  part-1 tree fails by design (its `src/phoenix` copy predates the checks), not a regression.
* Not exercised by the host test: G13's `-ENODEV` fallback (the fake kms always gates on fences,
  so `implicit_fallback()` and the per-connection latch never ran); `BO_IMPORT` with `size = 0` (a
  foreign process's export — drmprobe exports and imports in one process, so the library always
  knows the size; the server's `lseek` cross-check still runs); `drmphx_v3d_implicit_wait()` holds
  `IMP.lock` across bounded 2 s IPC slices, so a connection teardown can stall behind it (fallback
  path only).

### Builds (new output dirs only)

| Binary | Path | sha256 (first 16) |
|---|---|---|
| rpi4-v3d-async | `tools/gpu-lane/v3d-async/out-m3p2/rpi4-v3d-async` | `ea13136832989089` |
| v3dasync-ping | `tools/gpu-lane/v3d-async/out-m3p2/v3dasync-ping` | `b32d499cb12974b0` |
| rpi4-kms | `tools/gpu-lane/kms/out-m3p2/rpi4-kms` | `689304f31e9f1161` |
| kmstest | `tools/gpu-lane/kms/out-m3p2/kmstest` | `fe373d2562a89314` |
| drmprobe (+ `prefix/lib/libdrm.a`) | `tools/gpu-lane/libdrm-phoenix/build-out-m3p2/drmprobe` | `8f03feeccd1e2918` |

`strings -a` shows the new tagged lines in each server (`V3DA srv import …`, `V3DA srv import
released …`, `V3DA srv fstat answered …`, `V3DA srv dri name=…`, `/dev/dri/card1`,
`/dev/dri/renderD128`; `KMS srv read_dump …`, `srv fstat answered …`, `srv dri name=/dev/%s …`,
`srv kmsbuf atSize …`, `/dev/dri/card0`; kmstest `read_dump n=…`), and `objdump` shows
`kms_vblank_thread` and v3d `dispatch_thread` ending in `bl <endthread>`.

### Host tests

`DRMPHX_OUT=tools/gpu-lane/libdrm-phoenix/build-out-m3p2 tools/gpu-lane/libdrm-phoenix/hosttest/run.sh`:

```
HOSTTEST libdrm-phoenix checks=134 fails=0 verdict=PASS
HOSTE2E m3p2 mode=legacy fstats=2 atsizes=1 imports=1 imports_closed=1 deferred_flips=1
HOSTE2E legacy verdict=PASS (only the fake-GPU pixel checks failed, as expected)
HOSTE2E m3p2 mode=dri fstats=3 atsizes=1 imports=1 imports_closed=1 deferred_flips=1
HOSTE2E dri verdict=PASS (only the fake-GPU pixel checks failed, as expected)
```

The fakes model the new server behaviour (mtGetAttrAll on every port incl. card1's own, atSize on
`/kmsbuf`, BO_IMPORT with dedupe and OID memrefs, a flip that arrives with an unsignalled fence is
counted as `deferred_flips`), and the mocks model the kernel's `posix_fstat` (st_rdev = port, first
negative err wins) and `lseek(SEEK_END)` = `atSize`. Expected failures are exactly the four pixel
checks the fake GPU cannot draw (`cl_clear, cl_clear_dep, import_clear, implicit_flip`); run.sh
additionally requires by name: `fstat_nodes … ok=1` (n=3 in dri mode), `dmabuf_size ok=1`,
`prime_import_render rc=0 ok=1`, `prime_reimport ok=1`, `implicit_flip submit=0 flip=0 events=1`,
`identity node=card1 … node_type=0 ok=1` (dri), and `imports=1 imports_closed=1 deferred_flips≥1`.
**Negative control:** with `implicit_attach()` disabled in the build copy, both modes FAIL on
`deferred_flips=0` — the counter is G13's, not an artefact.

### Pre-registered Pi cycle `m3p2-drmprobe` (one netboot cycle)

**Question:** on hardware, do (a) `fstat()` on all three DRM nodes answer S_ISCHR with a distinct
dev_t per node, (b) `lseek(SEEK_END)` size a dma-buf, (c) a kms dumb buffer imported on the render
node take a GPU clear that the CPU reads back through the kms mapping, released cleanly, (d) a
fence-less flip of a just-rendered buffer complete with the pixels done — and do (e) the flip
events now carry data and (f) the proto-2 / proto-1 clients still work against the new servers?

**Preconditions:** netboot image ≥ build 9 (E1 export, port-death, vcmbox XL — as §7); no GPU app,
X, SDL program or `rpi4-v3d` in the boot; the old staged `/bin/kmstest` and `/bin/v3dasync-ping`
(proto 1 / proto 2 clients) left in place — they are the compatibility probes.

**Stage (coordinator)** — new names, so nothing already staged changes:

| Source | Export path |
|---|---|
| `tools/gpu-lane/v3d-async/out-m3p2/rpi4-v3d-async` | `<export>/bin/rpi4-v3d-async-m3p2` |
| `tools/gpu-lane/kms/out-m3p2/rpi4-kms` | `<export>/bin/rpi4-kms-m3p2` |
| `tools/gpu-lane/kms/out-m3p2/kmstest` | `<export>/bin/kmstest-m3p2` |
| `tools/gpu-lane/libdrm-phoenix/build-out-m3p2/drmprobe` | `<export>/bin/drmprobe-m3p2` |
| `tools/gpu-lane/v3d-async/out-m3p2/v3dasync-ping` (optional; the cycle uses the old one) | `<export>/bin/v3dasync-ping-m3p2` |

(`<export>` = the live fsid=0 export: `awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports`.)
For a later swap to the canonical names: the m3p2 server is a drop-in for the part-2 one
(compatibility above), built from the current tree incl. the EINVAL-draw fix `05141ff7d`.

**One cycle** (Bash `timeout: 600000`; psh has no `&` — both servers detach themselves):

```
./scripts/test-cycle-psh-interact.sh --label m3p2-drmprobe --idle-secs 8 --max-cmd-secs 120 \
    --hdmi-dense-on 'DRMPROBE kms_flip start' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1" \
    "/bin/rpi4-kms-m3p2 -G" \
    "/bin/drmprobe-m3p2 -n 120" \
    "/bin/drmprobe-m3p2 -n 30" \
    "/bin/kmstest info" \
    "/bin/kmstest-m3p2 -n 120 flip" \
    "/bin/v3dasync-ping cl-smoke" \
    "/bin/kmstest-m3p2 stats" \
    "/bin/kmstest-m3p2 quit" \
    "/bin/v3dasync-ping stats" \
    "/bin/v3dasync-ping quit"
```

Order: the render server first (`rpi4-kms -G` opens `/dev/v3d-async` for the fence page at start).
Wall clock ≈ netboot + 11 × (8 s idle + a few s) + 2 × ~6 s probe + ~3 s flips ≈ 5 min. Grade from
tagged lines only (~1.3 % UART line corruption: re-read, don't count; EL0 dumps print twice):

```
grep -a -E '^(DRMPROBE|KMS|KMSTEST|V3DA|V3DAPING) ' artifacts/rpi4b-uart/rpi4b-uart-*-m3p2-drmprobe.log
./scripts/uart-summary.sh m3p2-drmprobe
```

**Predictions** (§7's table still holds for every line it lists, except where noted):

| Line | Predicted | If instead… |
|---|---|---|
| `V3DA srv dri name=/dev/dri/renderD128 port=<P> rc=0 registered=1`, `… /dev/dri/card1 port=<Q≠P> … registered=1`; `KMS srv dri name=/dev/dri/card0 rc=0 registered=1` | once each, before `ready`/after `detached` | `rc<0 registered=0`: devfs refused the `dri` directory (`create_dev` makes it) — the probe then shows legacy names and the card1 gap; read the rc. |
| `V3DA srv dri name=/dev/dri/renderD128 … registered=1` but `/dev/dri/card1` and/or kms's `/dev/dri/card0` `rc<0 registered=0` | — | the second and later names under `dri/` take `create_dev`'s `-EEXIST → mtLookup → lookup.dev` branch for the directory, never exercised on this system before (dummyfs answers `o->dev`, which for a plain directory is its own oid [read], so it is predicted to work). Best effort, not a regression: the probe then shows mixed names; follow-up = create the directory once with `mkdir` in devfs. |
| `DRMPROBE device i=0 … primary=/dev/dri/card0 render=-`, `device i=1 … primary=/dev/dri/card1 render=/dev/dri/renderD128` | the canonical names now resolve | legacy names: G10 names missing (line above). |
| `open node=card1 path=/dev/dri/card1 rdwr=1 … ok=1`, `identity node=card1 version=v3d 1.0.0 … node_type=0 is_kms=0 … ok=1` | the v3d primary node answers on its own port | `open` fails: the card1 thread is not receiving (`V3DA srv card1 …` failure line); `identity` ok=0 with `version=-`: HELLO over the card1 port failed. |
| `fstat node=card0 … rc=0 mode=020666 chr=1 rdev=<K> devid_type=0 devid_dev=0`, `node=card1 … rdev=<Q> devid_type=0`, `node=render … rdev=<P> devid_type=2`; `fstat_nodes n=3 answered=3 chr_all=1 distinct=1 devid_ok=1 ok=1`; `V3DA srv fstat answered …`, `KMS srv fstat answered …` | **G2+G10 on hardware**: K = kms port, P = main v3d port, Q = card1 port | `rc=-1 errno=38` on all: old servers staged (gap=1) — a staging error, not a result; `errno=22`: the reply's `o.data` too small (kernel `sizeof(struct _attrAll)` differs); `distinct=0`: card1 shares a port — G10 regressed; `devid_ok=0`: library `srv_of_devid`. |
| `dmabuf_size end=8294400 errno=0 want=8294400 ok=1 gap=0` + `KMS srv kmsbuf atSize id=<h> size=8294400 (first; G3)` | **G3** | `end=-1 errno=2`: atSize refused (old kms or the flag cleared early). |
| `prime_import_render rc=0 errno=0 handle=<r> ok=1 gap=0` + `V3DA srv import handle=… client=… ns=kmsbuf id=<h> pages=2025 pa0=<inside the KMS pool of this boot> contiguous=1 gpuva=… cache=uncached live=1` | **G1**: 2025 pages = 8294400 B, contiguous (the pool), `pa0` = the `KMS pool pa=` line + the BO offset, below `0x40000000` | `rc=-1 errno=2`: lookup/open of `/kmsbuf/<h>` from the render server failed (pid rule on `/kmsbuf`?); `errno=22`: port mismatch or `size`; `errno=14` (EFAULT): `va2pa` of an unfaulted page — the touch did not fault it in; `V3DA srv import FAIL … rc=` names it. |
| `prime_reimport rc=0 handle=<same> same=1 ok=1` | DRM dedupe, no second `V3DA srv import` line | a second import line: dedupe broken (a leak per re-import). |
| `import_clear handle=… gpuva=… rc=0 pixels_ok=1 px0=0xff2080ff wait_bo=0 ok=1` + HDMI: an azure band ≈2 rows high at the top of the teal frame (then lime after `implicit_flip`) | **the V3D writes a kms scan-out buffer**, CPU reads it back through the kms mapping | `px0=0xdeadbeef`: the job did not write these pages (PTEs wrong: compare `pa0` with kms's pool PA) or it wrote elsewhere; `rc=-62`: the wait path (check `cl_clear` first). |
| `implicit_flip submit=0 flip=0 events=1 pixels_ok=1 px0=0xff80ff20 flip_us=8000–35000 ok=1` | the flip event arrives with the second clear complete | `pixels_ok=0` with `events=1`: the flip completed before the GPU (G13 did not attach and the clear was slower than a vblank — on hardware a 64×64 clear usually wins the race, so PASS proves "no regression", the host test proves the attach). `flip=-19`: kms without `-G` and the fallback failed. |
| `V3DA srv import released handle=… id=<h> pages=2025 live=0` | once, right after `import_clear`/`implicit_flip` (GEM_CLOSE → quarantine → munmap) | missing: the import leaks (quarantine never passed, or refs held). |
| `DRMPROBE RESULT pass=36 fail=0 gap=1 failed=- secs≈4–8 verdict=PASS` (gap = `prime_export_render`, G4) | first and second run | any `failed=` key: its row says what it means. |
| `KMSTEST connect …`, `KMSTEST info … result fails=0 verdict=PASS` (the **old** staged kmstest) | proto-1 client unchanged | HELLO `-EPROTO`/`-ENOTTY`: the KMS HELLO changed — blocker. |
| `KMSTEST flip … result flips=120/120 … errors=0 … verdict=PASS`, 0 `event_bad`, `KMSTEST read_dump n=1..3 bytes=32 raw=00000002 00000020 …` and `KMS srv read_dump n=1..3 path=parked packed=1 bytes=32 first16=00000002 00000020 …` with the same words | the M2 event fix | see the M2 fix subsection. |
| `V3DAPING cl-smoke … wait_rc=0 fence_err=0 …` (the **old** staged ping = quakespasm-v3da's client library) | proto-2 client unchanged | HELLO fails: blocker for the staged clones. |
| `KMSTEST stats … bos=0 exports=0`, `V3DAPING stats … parked=0 … pages_to_kernel=0`, `V3DAPING quit rc=0`, `KMSTEST quit rc=0` | no leaks after two probes (client death cleans up; the import released) | `bos>0`/`exports>0`: kms leak; `pages_to_kernel>0`: an import went to the BO pool path (must not). |
| after `KMS srv exit … restored=1` and `V3DA srv exit …` | **no** Exception dump | a dump at pc `0x1e1e…`: a thread entry still returns. |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | any: `addr2line` the PC (binaries unstripped). |

**What the cycle decides:** all PASS = the servers provide everything kmscube's GBM/EGL path needs
from them (§8 step 1 done); the next step is the `mesa-drm` build + kmscube (§8 steps 2–3). A
failure confined to `implicit_flip` pixels is a timing question, not a blocker (host test proves
the attach); any `fstat`/`import`/`dmabuf_size` failure blocks Mesa and is fixed first.

### Remaining gaps after part 2

G4 (`BO_EXPORT` + `/v3dbuf`), G5 (`SUBMIT_CPU`, perfmons), G6 (cross-process syncobj/sync-file
fds), G7 (`KMS_OP_PRIME_IMPORT`), G8 (kms out-fence), G9 (a commit-pending flag in `GET_CRTC`), G12
(`poll()` 20 ms quantum, kernel), G15 (`SYNC_IOC_*`, `DMA_BUF_IOCTL_*_SYNC_FILE`), cross-process
implicit sync (`BO_LAST_FENCE`), and access control on `/kmsbuf` / client ids (R3). `F2`'s
misleading `kms_proto.h` comment is fixed.

## M3 part 3 — Mesa DRM build + kmscube (2026-09-27)

**Status:** **builds and links.** Upstream Mesa 26.2.0 on the DRM path (gallium v3d + vc4/kmsro,
GBM with the dri backend linked in, EGL platforms drm + surfaceless, GLES 2/3) cross-built
**static** for aarch64-phoenix against libdrm-phoenix, and upstream kmscube linked against it:
`kmscube` 16.0 MB text (87.8 MB with debug info, 16.5 MB stripped), **0 undefined symbols**, no
old-lane string, no `dlopen` linked. **No Pi cycle yet** — pre-registered below. Nothing committed,
nothing staged; no server, no libdrm-phoenix source, no old-lane file, no sibling repo touched.
Code: [`tools/gpu-lane/mesa-drm/`](../../tools/gpu-lane/mesa-drm/).

### Build

```
tools/gpu-lane/mesa-drm/build.sh                     # Mesa + kmscube (≈100 s of ninja at -j16 on this host)
tools/gpu-lane/mesa-drm/build.sh --relink \
    --libdrm-prefix tools/gpu-lane/libdrm-phoenix/build-out-m3p2/prefix   # kmscube only, ≈20 s
tools/gpu-lane/mesa-drm/build.sh --clean
```

Writes only `tools/gpu-lane/mesa-drm/build-out/` (gitignored, ≈1.5 GB): `mesa-src/` (a `git clone -s`
of `external/mesa` detached at **`mesa-26.2.0` = `9f0a761020b`** + `patches/mesa/*.patch`; the old
lane's fork checkout is never touched), `mesa-build/`, `prefix/` (`ninja install`: headers +
`libEGL.a`, `libgbm.a`, `lib/gbm/dri_gbm.a`, `libGLESv2.a`, `libgallium-26.2.0.a`),
`libdrm-prefix/` (a **snapshot** of libdrm-phoenix's prefix, with its own `libdrm.pc`; provenance in
`libdrm-snapshot.txt`), `zlib-prefix/` (see below), `compat/libmesadrm-compat.a`, `kmscube-src/`
(upstream `f60e50e`, MIT), `kmscube` (unstripped, for `addr2line`), `kmscube-stripped` (stage this),
`kmscube.map`, `mesa-drm-full.patch` (all Mesa patches as one diff), logs.

* **Why not the fork E7 built from.** E7 compiled the fork HEAD (`mesa-26.2.0-22-g51c5ee977ba`), but
  E7 §3.3 shows its `__phoenix__` old-lane hooks activate in a DRM build (fake v3dv fd, `MMAP_BO`
  offset treated as a CPU VA, `v3d_phoenix_peek_next_scanout`). So the base is the fork's own base
  tag, with the genuine driver fixes cherry-picked (E7 §3.3 option (a), §8 step 2) — same object
  store, same compiler, same cross recipe.
* **Options:** `-Dgallium-drivers=v3d,vc4 -Dvulkan-drivers= -Dplatforms= -Degl=enabled
  -Dgbm=enabled -Dglx=disabled -Dopengl=false -Dgles1=disabled -Dgles2=enabled -Dllvm=disabled
  -Dspirv-tools=disabled -Dvideo-codecs= -Dgallium-va=disabled -Dshader-cache=disabled
  -Dxmlconfig=disabled -Dexpat=disabled -Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled
  -Dlmsensors=disabled -Dperfetto=false -Dbuild-tests=false -Dtools=`, `debugoptimized` +
  `b_ndebug=true`, `--wrap-mode=nodownload`, `default_library=static`. EGL summary: drivers
  `builtin:egl_dri2`, platforms `surfaceless drm` (native platform `surfaceless`: kmscube uses
  `eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR)`, so that is not load-bearing; `26.2` has no `drm`
  choice for `egl-native-platform`). Desktop GL (`opengl=false`) and Vulkan are left out for this
  step — the games (desktop GL) and v3dv (M5) are one option each when their turn comes.
* **Cross file** (generated): E7's `phx-gcc`/`phx-g++` (drop `-pthread`), the tree sysroot,
  `compat/include` on `-I` (never `-include`: E7's probe-flip trap), and
  `has_function_posix_memalign = false` in `[properties]` — meson's documented cross override. It
  fixes E7's false YES (a gcc builtin) **without a Mesa patch**: `os_memory_aligned.h` then takes
  its over-allocating fallback.
* **zlib** is Mesa's one hard port dependency. The ports prefix ships `libz.a` + `zlib.h` but **no
  `zlib.pc`**, and meson — finding nothing — **silently downloaded zlib 1.3.1 from wrapdb and built it
  as a subproject** on the first attempt (caught in `mesa-setup.log`). Fixed: a private
  `zlib-prefix/` with only `zlib.h`/`zconf.h` and a `zlib.pc` pointing at the ports' `libz.a`
  (the ports `include/` also holds other ports' `GL/`, `X11/` headers — never on Mesa's include
  path), plus `--wrap-mode=nodownload` so it can never happen again.
* **kmscube** is compiled directly (its meson source list with GLES3/shadertoy, no libpng/GStreamer)
  and linked in E7's `link-kmscube.sh` shape: C++ driver, `-static`, `--gc-sections`,
  `-z max-page-size=0x1000`, **`-Wl,--wrap=mmap`**, `libgallium-26.2.0.a` **whole-archive**, then one
  `--start-group` of `libEGL.a libgbm.a dri_gbm.a libGLESv2.a` + the small per-version archives +
  `libdrm.a` + `libmesadrm-compat.a` + the ports' `libz.a`. meson turns internal static libraries into
  *thin* archives and bundles their objects into each installed target's archive, so
  `libgallium-26.2.0.a` (dri_target + the DRI frontend via `link_whole` + libmesa/NIR/GLSL/drivers/
  winsyses/util) is the equivalent of E7's "target objects + `libdri` whole"; EGL/GBM's bundled copies
  of loader/util objects are never pulled twice (their symbols are already defined).
* **`--relink`** re-snapshots libdrm-phoenix and relinks kmscube only. kmscube **embeds** libdrm.a,
  so every library-side libdrm-phoenix fix (G13, name handling, a proto bump) needs a relink. The
  delivered binary is linked against **`libdrm-phoenix/build-out-m3p2`** (the part-2 library: G1
  import, G2/G3 users, `/dev/dri` names incl. card1, G13 implicit sync), sha256
  `3889af57eaf5bbb0…`; Mesa's objects are unaffected (the two prefixes' headers are identical).

### Mesa patches (`tools/gpu-lane/mesa-drm/patches/mesa/`, `git format-patch` over `mesa-26.2.0`)

| # | Patch | Lines | Seam / rationale |
|---|---|---|---|
| 0001 | `util, meson: recognise Phoenix-RTOS as a POSIX KMS/DRM system` | +13/−5 | E7's OS patch minus its blake3 hunk (that hunk *reverted* a fork change that does not exist on the tag). `'phoenix'` in `system_has_kms_drm` (else GBM refuses); `DETECT_OS_PHOENIX` + `DETECT_OS_POSIX` (else `os_time.c`/`os_misc.c` `#error`); `os_misc.c` `<unistd.h>` branch; `u_thread.[ch]`: Mesa's own mutex+condvar barrier (libphoenix has no `pthread_barrier_*`, as Apple/Haiku) and no `pthread_getcpuclockid` (as managarm). |
| 0002 | `meson: build the EGL/GBM/GLES/gallium libraries as archives on Phoenix` | +11/−6 | No `dlopen` of DRI drivers — **static megadriver** (E7 §3.4: non-PIC libphoenix, no TLS relocations in `dl.c`, initial-exec TLS in glapi/EGL). The six `shared_library()` targets (libgallium, libEGL, libgbm, dri_gbm, libGLESv2, libGLESv1_CM) become `library()`, which follows `default_library` (unchanged for `shared`); `libname_suffix = 'a'` on Phoenix so libgallium's `name_suffix` yields an archive name. |
| 0003 | `gbm: use the linked-in dri backend on Phoenix instead of dlopen()` | +29 | The one `dlopen` left on the GBM/EGL/KMS path: GBM's backend loader. It **links** (libphoenix has `dlopen`) and would fail only at runtime (`gbm_create_device` → NULL). With `GBM_BUILTIN_DRI_BACKEND` (meson, host `phoenix`) `_gbm_create_device()` calls the linked `gbmint_get_backend()` through a static descriptor with `lib == NULL`, which `_gbm_device_destroy()` already skips. Verified: `gbmint_get_backend` in the binary, `dlopen`/`loader_open_driver_lib` not linked. |
| 0004 | `v3d: decline HW mipmap-gen for NPOT textures` (fork `e4be1163240`) | +16 | genuine driver fix (Q2 NPOT skins), unconditional |
| 0005 | `u_vbuf: NULL-check the translate object` (fork `f342ce50282`) | +12 | genuine fix (aarch64 has only `translate_generic`) |
| 0006 | `u_vbuf: do not silently drop draws on the index-unrolling path` (fork `aa916f2f060`) | +42/−1 | genuine fix, `__phoenix__`-gated (Q3 world black) |
| 0007 | `v3d: force EZ off on Phoenix 26.2` (fork `2728620c216`) | +15 | **droppable**: the old lane's proven wedge-avoidance config, kept for parity; E2b questions it (render-phase slowness). Delete the file to measure without it. |
| 0008 | `gallium/u_screen: query DRM_CAP_PRIME on Phoenix-RTOS too` | +2/−1 | **added after the first Pi run** — the fix for the m3p3 crash ([analysis](#m3-part-3--first-kmscube-run-analysis-2026-09-27)) |
| 0009 | `egl/drm: treat a GBM back buffer without a DRI image as an allocation failure` | +12 | defensive (upstream error path), same analysis |

The cherry-picks carry `(cherry picked from commit …)`. Not taken from the fork: every old-lane hook
(E7 §3.3), the RASTER-scanout pair (`4363822955b`/`34a448d6a29` — on the DRM path the scan-out
resource is `PIPE_BIND_SCANOUT` → linear by upstream's own rule, `v3d_resource.c:847`), the C1
ralloc instruments, and the v3dv commits (Vulkan not built). **No Mesa `mmap` patch**: the program
links with `-Wl,--wrap=mmap`; `objdump` shows `v3d_bo_map_unsynchronized`, `vc4_bo_map_unsynchronized`,
`gbm_dri_bo_create`, the sw winsys maps (and libphoenix's own `malloc`/`fopen`/`pthread_create`
mappings, which pass straight through) calling `__wrap_mmap`.

### Compat shim (libphoenix gaps — `tools/gpu-lane/mesa-drm/compat/`)

`#include_next` wrapper headers on `-I` (never in the shared sysroot) + `mesadrm_compat.c`, whose
stand-ins are compiled **only while `libphoenix.a` lacks the symbol** (`build.sh` checks with `nm`),
so the shim can never duplicate a real implementation. Delete each piece when libphoenix gains it:

| Gap (today's tree sysroot) | Used by | Shim | Real fix |
|---|---|---|---|
| `<assert.h>` lacks C11 `static_assert` | Mesa C (hundreds of TUs) | `compat/include/assert.h` | branch `gpu-lane/libc-gaps` `8551094` |
| `<inttypes.h>` lacks `SCNxPTR`/`SCNuPTR` | `nir_opt_varyings.c` (`-Werror=format`) | `compat/include/inttypes.h` | `c1c2af2` |
| `<sys/file.h>` has `flock()` but no `LOCK_*` | `fossilize_db.c` | `compat/include/sys/file.h` | `1f5c7db` |
| no `_SC_PHYS_PAGES` | `os_get_total_physical_memory` | `compat/include/unistd.h` defines **103** (the branch's value), `sysconf` answers −1 today → fail soft | `3162bd4` |
| no `open_memstream` | `util/memstream.c` (NIR/SPIR-V debug text only; all callers handle NULL) | ENOSYS stub | `7cc5628` |
| no `posix_memalign` (but a gcc builtin → probe false YES) | `os_memory_aligned.h` | cross-file `has_function_posix_memalign = false` | `c69d829` |
| no `pthread_barrier_*`, no `pthread_getcpuclockid` | `u_thread` | Mesa patch 0001 (Mesa's own fallback, correct) | barriers `3da702b` (the Mesa fallback stays correct after it lands) |
| no `getopt_long_only` | kmscube | → `getopt_long` (single-dash long options like `-count=10` not recognised; `-c 600` / `--count=600` fine) | **new gap**, not on the branch |
| no GNU `sincos` | kmscube `cube-gears.c` | `sin`+`cos` (`compat/app-include/math.h`, applications only) | **new gap** |
| `<sys/ioccom.h>` absent (Mesa's own `include/drm-uapi/drm.h` takes the BSD branch, as libdrm's) | every DRM uapi header in Mesa | alias → `<sys/ioctl.h>` (Phoenix = BSD `_IOC` layout). Verified: `vc4_drm_screen_create`'s raw `ioctl` uses `0xc0106447` (`_IOWR('d', 0x47, 16)`, Phoenix layout), so libphoenix copies the right 16 bytes | — (a Phoenix port of libdrm-uapi headers) |
| `pthread_exit` not declared `noreturn`; no `pthread_setname_np` | `threads_posix.c:289` warning; `u_thread.c:118` `#warning` (threads unnamed) | none (cosmetic) | libphoenix header attribute / new function |

Build warnings otherwise: 55 × upstream `-Wsign-compare` from `u_math.h:892` in C++ TUs, 2 ×
`-Warray-bounds` in upstream `blake3.c` (gcc 16) — none in patched code; kmscube 0 warnings.

### Verification (the delivered binary)

| Check | Result |
|---|---|
| static link | OK (`kmscube-link.log` empty) |
| `aarch64-phoenix-nm -u kmscube` | **0** symbols |
| `size` | text 15 982 118, data 515 992, bss 298 012; file 87 813 320 B, stripped 16 504 008 B |
| sha256 (first 16) | `kmscube` `7eadd92a74b24ff7`, `kmscube-stripped` `8569c7eb00c4b9bf` |
| libdrm-phoenix present | symbols `drm_phoenix_ioctl`, `drmPhoenixMmap`, `__wrap_mmap`, 15 `*implicit*` (G13); strings `/dev/kms`, `/dev/dri/card0`, `/dev/v3d-async`, `/dev/dri/renderD128`, `/dev/dri/card1`, `/kmsbuf`, `libdrm-phoenix: rpi4-kms runs without -G: …` |
| drivers present | `gbmint_get_backend`, `vc4_drm_screen_create`, `kmsro_drm_screen_create`, `v3d_drm_screen_create_renderonly`; strings `kmsro`, `v3d` (31), `vc4` (6), `V3D 4.2`, `EGL_KHR_platform_gbm` |
| old lane absent | `v3d-winsys:` 0, `phoenix_v3d_ioctl` 0, `peek_next_scanout` 0, `v3d-srv` 0 (checked on the stripped binary; `build.sh` fails if any appears) |
| `dlopen`, `loader_open_driver_lib` | not linked |

### Runtime path and the risks only the Pi can show

What `kmscube -D /dev/dri/card0 -N -c 600` does, in order, and where each step can fail:

1. `open(O_RDWR)` + `drmModeGetResources`/connector/encoder/CRTC → kms (M3 part 1 marshalling).
2. `gbm_create_device(fd)`: **`fstat` → `S_ISCHR`** (`gbm.c:133`, **G2**), then the built-in dri
   backend (patch 0003) → `loader_get_driver_for_fd` → `drmGetVersion` = `vc4` →
   `vc4_drm_screen_create` → raw `DRM_IOCTL_VC4_GET_PARAM` must **fail** (kms answers every non-HELLO
   ioctl `-ENOTTY`) → `kmsro_drm_screen_create` → `drmGetDevices2` (libdrm-phoenix's static list) →
   render node `open(O_RDWR)` → `drmGetVersion` = `v3d` → `v3d_drm_screen_create_renderonly` →
   `V3D_GET_PARAM`s.
3. `u_pipe_screen_lookup_or_create`: Phoenix has no `kcmp`/`F_DUPFD_QUERY`, so
   `os_same_file_description` returns −1 → **one expected stderr line** `os_same_file_description
   couldn't determine if two DRM fds reference the same file description…`, then it compares
   `fstat` `(st_dev, st_ino, st_rdev)` — needs G2 answering consistently (dup'ed descriptors share the
   oid → equal → same screen, as intended). `os_dupfd_cloexec` uses `F_DUPFD_CLOEXEC`, which the
   kernel implements (`posix.c:2340`).
4. EGL on GBM (`dri2_initialize_drm`): `get_fd_render_gpu_drm` → the render node again; prints the
   EGL/GLES info block.
5. First `eglSwapBuffers` allocates the scan-out buffers: `renderonly_create_kms_dumb_buffer_for_resource`
   → `CREATE_DUMB` **1024 px × N rows** (one page per row, N = pages of the linear 1080p colour
   buffer + 64 B TFU read-ahead ≈ 2026 → ~7.9 MiB each) in the **kms pool** → `PRIME_HANDLE_TO_FD`
   (`/kmsbuf/<id>`) → render `PRIME_FD_TO_HANDLE` (**G1** `BO_IMPORT`) → `lseek(SEEK_END)` (**G3**) →
   `GET_BO_OFFSET` → `MMAP_BO` token → `__wrap_mmap` → OID memref → `/kmsbuf` uncached map. The
   platform keeps up to 4 colour buffers; kmscube's legacy loop needs 3 (displayed + pending +
   rendering) = ~24 MiB of the default 32 MiB pool. **A 4th fits only barely** (4 × 8 298 496 B ≤
   32 MiB with no fragmentation): a `DRM_IOCTL_MODE_CREATE_DUMB failed` / `Failed to create scanout
   resource` line means pool exhaustion → re-run with `rpi4-kms-m3p2 -G -p 48` (48 MiB below 1 GiB is
   untested, E3 proved 32).
6. `drmModeAddFB2(1920×1080, XR24, handle = the dumb handle, pitch 7680)` — the dumb buffer's own
   pitch is 4096 (1024 px); kms validates `offset + pitch × height ≤ size` (`kms_bo.c:443`), so it is
   accepted. kmscube passes no modifier (`LINEAR` = 0 → no `DRM_MODE_FB_MODIFIERS`).
7. `drmModeSetCrtc` (blocking, current mode only), then per frame: draw → `eglSwapBuffers`
   (`SUBMIT_CL` on the render server writing kms pool pages below 1 GiB) →
   `gbm_surface_lock_front_buffer` → `drmModePageFlip(EVENT)` → **G13** attaches the imported BO's
   unsignalled last-use fence → `rpi4-kms -G` gates the flip on the render fence page → `select()` on
   stdin + the card fd → `drmHandleEvent` (`read`).
8. At `-c` frames: `Rendered N frames in S sec (F fps)` and exit (no GL/GBM teardown; process death
   releases both servers' clients; kms restores the console on client death, M2).

Only the Pi can show: whether (a) the dri screen comes up at all through the kmsro pairing (every step
of 2 runs for the first time on hardware); (b) the fps: `select()` on the card fd rides the kernel's
**20 ms poll cycle (G12)** — ~30 fps with a correct picture is G12, not a render problem; ≥ 55 fps
means the event read happened to align; the V3D render phase at 1080p (E2b) and EZ-off (patch 0007)
cap it further; (c) whether G13's fence actually gates (tearing / half-drawn cube = the attach did not
happen or `-G` is missing — the library then prints its one `libdrm-phoenix: rpi4-kms runs without
-G` line); (d) the GLSL/NIR/v3d compile time of the first frame (excluded from kmscube's fps, but it
extends the silence before the first `Rendered` line); (e) any `mmap of bo … failed` (Mesa) = the
token/`--wrap` path; (f) memory: Mesa's shader compiler + a static 16 MB text binary.

### Dependencies on server gaps (status after M3 part 2)

| Gap | Needed for | Part 2 status |
|---|---|---|
| **G2** `mtGetAttrAll` | `gbm_create_device` (`S_ISCHR`), screen dedupe (step 3) | implemented in `out-m3p2` servers, **not yet Pi-verified** (`m3p2-drmprobe`) |
| **G1** `BO_IMPORT` | every scan-out colour buffer (step 5) — without it `Failed to get v3d handle for dmabuf` and no frame | implemented, not Pi-verified |
| **G3** `atSize` | `lseek(SEEK_END)` in `v3d_bo_open_dmabuf` — without it `Couldn't get size of dmabuf fd` | implemented, not Pi-verified |
| **G10** `/dev/dri` names | `-D /dev/dri/card0`, kmscube's default `drmGetDevices2` scan works either way (libdrm-phoenix falls back to `/dev/kms`) | implemented (`/dev/dri/card0`, `card1`, `renderD128`) |
| **G13** implicit flip sync | tear-free legacy flips (`drmModePageFlip` carries no fence) | library side in `build-out-m3p2` (linked here); needs `rpi4-kms -G` |
| G8 kms out-fence, G6 | `kmscube -A` (atomic + `EGL_ANDROID_native_fence_sync`) | open — **do not run `-A` yet** |
| G12 poll quantum | fps (above) | open (kernel) |

**Gate:** run this cycle only **after `m3p2-drmprobe` passes** (or in the same boot right after it —
it needs the same staged servers). If G2/G1/G3 fail there, kmscube fails at steps 2/5 for the same
reason and adds nothing.

### Pre-registered Pi cycle `m3p3-kmscube` (one netboot cycle)

**Question:** does an unmodified upstream GBM/EGL/GLES2 program — Mesa's kmsro pairing of the vc4
display node with the v3d render node — render and page-flip on HDMI through libdrm-phoenix and the
two new-lane servers, and at what frame rate?

**Preconditions:** netboot image ≥ build 9 (as §7); no GPU app, X, SDL program or `rpi4-v3d` in the
boot; the `m3p2` binaries staged (M3 part 2 table above) — this cycle uses the same server binaries;
the old staged `/bin/kmstest` and `/bin/v3dasync-ping` stay (used for stats/quit only).

**Build + stage (coordinator):**

```
tools/gpu-lane/mesa-drm/build.sh --relink \
    --libdrm-prefix tools/gpu-lane/libdrm-phoenix/build-out-m3p2/prefix   # already done; re-run after any libdrm-phoenix change
EXPORT=$(awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports)
```

| Source | Export path |
|---|---|
| `tools/gpu-lane/mesa-drm/build-out/kmscube-stripped` | `$EXPORT/bin/kmscube` |
| `tools/gpu-lane/v3d-async/out-m3p2/rpi4-v3d-async` | `$EXPORT/bin/rpi4-v3d-async-m3p2` (if not staged by m3p2) |
| `tools/gpu-lane/kms/out-m3p2/rpi4-kms` | `$EXPORT/bin/rpi4-kms-m3p2` (if not staged by m3p2) |
| `tools/gpu-lane/kms/out-m3p2/kmstest` | `$EXPORT/bin/kmstest-m3p2` (if not staged by m3p2) |

(`sudo install -m 755 <source> <path>`; `cmp` afterwards. Keep the unstripped `build-out/kmscube` on
the host for `addr2line`.)

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label m3p3-kmscube --idle-secs 30 --max-cmd-secs 150 \
    --hdmi-dense-on 'Using display' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-m3p2 -G" \
    "/bin/kmscube -D /dev/dri/card0 -N -c 600" \
    "/bin/kmscube -D /dev/kms -N -c 300" \
    "/bin/kmscube -D /dev/dri/card0 -N -M rgba -c 300" \
    "/bin/kmstest-m3p2 stats" \
    "/bin/kmstest-m3p2 quit" \
    "/bin/v3dasync-ping stats" \
    "/bin/v3dasync-ping quit"
```

Order: the render server first (`rpi4-kms -G` opens the render fence page at start). Both servers
detach (psh has no `&`). `-N`: kmscube's legacy loop `select()`s stdin too and would end at the first
stray UART byte ("user interrupted!"). `-c`: frame count (upstream option). `--idle-secs 30`: the
first frame compiles shaders (several seconds of silence possible before the first `Rendered` line;
after that kmscube prints every 2 s). 600 frames ≈ 10–20 s at 30–60 fps; `--max-cmd-secs 150` covers
a 4–5 fps worst case. The second run uses the legacy name and a fresh process (servers must have
released the first process's clients, pool BOs and imports); the third adds texture upload and
sampling (`-M rgba`: a 512×512 RGBA texture, TFU/TMU path). Wall clock ≈ netboot 60–150 s + 9 ×
(30 s idle + run) ≈ 7–9 min — **if it exceeds the 10-min cap, split after the first kmscube** (drop
runs 2–3 into a second cycle with the same two server lines first). Grade:

```
grep -a -E '^(KMS|KMSTEST|V3DA|V3DAPING|Rendered|Using display|  (version|renderer|vendor):|failed|Failed|MESA|DRI2|libdrm-phoenix|os_same_file)' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m3p3-kmscube.log
./scripts/uart-summary.sh m3p3-kmscube
```

kmscube's lines are untagged upstream text; allow for ~1.3 % UART line corruption (re-read, don't
count), EL0 dumps print twice.

**Predictions (first kmscube run) and what each alternative means:**

| Line / observation | Predicted | If instead… |
|---|---|---|
| `KMS srv ready …`, `V3DA srv …` ready lines + the part-2 `dri name=… registered=1` lines | as in `m3p2-drmprobe` | a server missing: stop, it is a staging/boot problem. |
| `V3DA srv fstat answered …`, `KMS srv fstat answered …` (first answers) | once each during kmscube's start | no `fstat` line and `failed to initialize GBM`: **G2** not served (old binary staged). |
| `os_same_file_description couldn't determine if two DRM fds …` (stderr) | **expected once** (no `kcmp` on Phoenix) | absent: fine too (only printed when a second screen lookup happens). |
| `Using display 0x… with EGL version 1.5`, `EGL information:` `version: "1.5"`, `vendor: "Mesa Project"`, client extensions incl. `EGL_KHR_platform_gbm` | as listed | `failed to initialize GBM`: gbm_create_device NULL — read the preceding `MESA`/`kmsro`/`DRI2` lines: no render device (`drmGetDevices2`/open of renderD128), `vc4_drm_screen_create` took the "3D present" branch (the raw `VC4_GET_PARAM` did **not** fail — kms answered it), or the v3d screen failed on a `GET_PARAM`. `failed to initialize EGL` + `DRI2: failed to get compatible render device`: `get_fd_render_gpu_drm`. |
| `OpenGL ES 2.x information:` `version: "OpenGL ES 3.1 Mesa 26.2.0"` (or `2.0`/`3.0`), `renderer: "V3D 4.2.…"` | a V3D 4.2 renderer string | `renderer: "llvmpipe"`/`softpipe`: impossible here (not built) — if any sw name appears, kmsro fell back to the `kms_swrast` path (`libswkmsdri`): the render node was not found. |
| import lines: `V3DA srv import handle=… ns=kmsbuf … pages=2026 … contiguous=1` × 3 (maybe 4) around the first frame | **the kmsro scan-out buffers imported on the render node (G1)**; `KMS srv kmsbuf atSize id=… (first; G3)` once | `MESA: error: Failed to get v3d handle for dmabuf …`: G1; `Couldn't get size of dmabuf fd`: G3; `DRM_IOCTL_MODE_CREATE_DUMB failed` / `Failed to create scanout resource`: kms pool exhausted (→ `-p 48`, step 5); `mmap of bo … failed`: the token/`__wrap_mmap` path. |
| HDMI (dense snapshots from `Using display`) | **a rotating smooth-shaded cube** (red/green/blue/… faces) on black, full screen 1920×1080, no console text over it | console still visible: `SETCRTC` did not reach the display (`KMS apply` lines); black screen with `Rendered` lines advancing: frames flip but the GPU wrote elsewhere (compare `V3DA srv import pa0` with `KMS pool pa`); a frozen cube: flips stopped (`failed to queue page flip`); torn / half-drawn cube: G13 did not gate (look for `libdrm-phoenix: rpi4-kms runs without -G`); garbage stripes: a tiled buffer was scanned out (impossible per `v3d_resource.c:847` — report it). |
| `Rendered N frames in 2.0x sec (F fps)` every 2 s, then a final line after 600 frames, prompt returns | **F = 25–60**: ~30 = the G12 20 ms poll quantum on the flip-event `select()`; 55–60 = event-aligned. Record F and the `KMSTEST stats` flip counters | F < 20: the V3D render phase (E2b) or per-frame IPC — compare `V3DAPING stats` job times; no `Rendered` line within `--idle-secs`: first-frame compile hang or a wait that never completes (`V3DA` wedge lines). |
| `failed to queue page flip: Device or resource busy` | absent | the flip was issued while one was pending: event delivered before completion (kms) or kmscube's wait loop broke out early. |
| `select err: …` / `select timeout!` | absent | `-N` only stops kmscube from *acting* on stdin: `legacy_run` still `FD_SET(0)`s the psh tty in every `select()` — the first new-lane client to `select()` the console. `select err` = `select()` on fd 0 (or on the card fd's `atPollStatus`) failed, **not** a DRM failure; remedy = a 2-line kmscube patch (`patches/kmscube/`, skip `FD_SET(0)` when `nonblocking`; psh has no `<` redirection). A tty that reports permanently readable is harmless (the loop falls through to the blocking `drmHandleEvent` read). |
| second run (`-D /dev/kms`, fresh process) | same lines, same fps; `import` lines for new buffers; `import released` lines for the first run's buffers **before** it (client death) | failure only on the second run: pool BOs or imports leaked by the first process (`KMSTEST stats bos`/`exports`). |
| third run (`-M rgba`) | a textured cube (the RGBA test image on each face) | texture black/garbled with the smooth cube fine: TFU/TMU path (patch 0004 is POT-neutral; the 512×512 texture is POT). |
| `KMSTEST stats … bos=0 exports=0 apply_errors=0`, `V3DAPING stats … parked=0 … pages_to_kernel=0`, both `quit rc=0` | no leaks after three kmscube processes | `bos>0`/`exports>0`: kms leak on client death; `pages_to_kernel>0`: an import went to the BO pool path (must not). |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | any EL0 fault in kmscube: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/mesa-drm/build-out/kmscube <pc>` (unstripped, same link as the staged stripped copy). |

**What the cycle decides:** a rotating cube on HDMI with `Rendered` lines = **M3's GBM/EGL/KMS path
works end to end** (the first unmodified Linux DRM client on Phoenix); next are SDL2 KMSDRM, a desktop-GL
Mesa (`-Dopengl=true`) for the game clones, and `kmscube -A` once G8 exists. A failure before
`Using display` is an integration bug in steps 2–4 (fix in libdrm-phoenix or the servers, relink,
re-run); a failure at the first frame is the import/scan-out chain (G1/G3/pool); a correct but slow or
torn cube is performance (G12/E2b) or G13, not a blocker for the M3 verdict.

## Result — `m3p2-drmprobe` (queue17, 2026-09-27 04:20): **PASS, exactly as pre-registered**

`DRMPROBE RESULT pass=36 fail=0 gap=1 failed=- verdict=PASS` on both probe runs (the gap is render-node
export, G4); 0 exceptions, 0 faults. Log `artifacts/rpi4b-uart/*-m3p2-drmprobe.log`.

- **G10** `V3DA srv dri name=/dev/dri/renderD128 … registered=1`, `/dev/dri/card1` (own port 23),
  `KMS srv dri name=/dev/dri/card0 … alias of /dev/kms` — the directory-exists branch of `create_dev`
  worked.
- **G2** `fstat_nodes n=3 answered=3 chr_all=1 distinct=1 devid_ok=1 ok=1`.
- **G3** `dmabuf_size end=8294400 want=8294400 ok=1`.
- **G1 — the first zero-copy cross-server buffer:** `V3DA srv import … ns=kmsbuf id=1 pages=2025
  pa0=0x06000000 contiguous=1 gpuva=0x02110000 cache=uncached` → a GPU clear job into it →
  `import_clear … pixels_ok=1 px0=0xff2080ff` read back through the KMS mapping → `import released …
  live=0`. A buffer owned by rpi4-kms, written by the V3D via rpi4-v3d-async, read by a client.
- **G13** `implicit_flip … events=1 pixels_ok=1 flip_us=33191` — a fence-less flip of a GPU-written
  buffer waited for the GPU and showed the right pixels.
- **Compatibility:** the old staged proto-1 `kmstest info` and proto-2 `v3dasync-ping cl-smoke` ran
  against the new servers unchanged; `kmstest-m3p2 -n 120 flip` **120/120 at 60.00 fps, 0 missed**.
- **M2 event fix proved:** server `read_dump … bytes=32 first16=00000002 00000020 …` and client
  `read_dump … raw=00000002 00000020 4b4d5300 …` agree (type 2 = flip complete, length 32, the flip's
  user data `0x4b4d5300 + i`).

Next: `m3p3-kmscube` (queued, queue18) — Mesa GBM/EGL on this stack.

## M3 part 3 — first kmscube run: analysis (2026-09-27)

**Status:** root cause found by static analysis of the log and the binary. It is a **one-line Mesa OS gate**:
nothing in libdrm-phoenix or either server was wrong on this path. Fixed (Mesa patch 0008), plus a
defensive error-path patch (0009) and the opt-in `DRMPHX_TRACE` in libdrm-phoenix. Rebuilt and
host-tested. **No Pi cycle yet**: pre-registered below. **No server changed.** Nothing committed or staged.

### The failure (cycle `m3p3-kmscube`, `artifacts/rpi4b-uart/rpi4b-uart-20260927-042056-m3p3-kmscube.log`)

All three kmscube runs got through `gbm_create_device` (`KMS srv fstat answered`, `V3DA srv fstat answered
… port=render`), `eglInitialize` on the GBM platform and the kmsro screen (`Using display … EGL version
1.5`, both extension lists). They then died with `Data Abort (EL0)` at `pc=0x9279c8`, `far=0`, before the
`OpenGL ES 2.x information:` block, which kmscube prints after `eglMakeCurrent`. addr2line on the
unstripped binary (now `build-out/kmscube-m3p3a`) plus the stack words:

```
0x9279c8 dri2_allocate_textures       frontends/dri/dri2.c:280   texture = images.back->texture
0x927718 dri2_allocate_textures       dri2.c:207                 (return from dri_image_drawable_get_buffers)
0x9276a4 dri_image_drawable_get_buffers dri2.c:157               (getBuffers = dri2_drm_image_get_buffers)
0x923340 dri_st_framebuffer_validate  dri_drawable.c:79          (st validate at the first eglMakeCurrent)
```

The disassembly shows `ldr x0,[sp,#128]` (= `images.back`) then `ldr x28,[x0]` with `x0 = 0`. So
**`images.back` itself was NULL**, not its texture (`texture` is at offset 0 of `struct dri_image`, hence `far=0`).
There were no `V3DA srv import` lines, and `kmstest stats` afterwards showed `bos=0 exports=0 applied=0`.

### The path, and where it went wrong

`eglMakeCurrent` → st framebuffer validate → `dri2_drm_image_get_buffers` (`platform_drm.c:327`) →
`get_back_bo` → the surface came from `gbm_surface_create_with_modifiers(&LINEAR, 1)` (kmscube
`common.c:166`, flags = `GBM_BO_USE_SCANOUT` only) → `gbm_bo_create_with_modifiers2` →
**`gbm_dri_bo_create` (`gbm_dri.c:902`): `if (usage & GBM_BO_USE_WRITE || !dri->has_dmabuf_export) return
create_dumb(…)`**.

`has_dmabuf_export` comes from `pscreen->caps.dmabuf & DRM_PRIME_CAP_EXPORT` (`gbm_dri.c:1246`).
`caps.dmabuf` is filled in only by `u_init_pipe_screen_caps` (`gallium/auxiliary/util/u_screen.c:135`):

```c
#if defined(HAVE_LIBDRM) && (DETECT_OS_LINUX || DETECT_OS_BSD || DETECT_OS_MANAGARM)
   if (pscreen->get_screen_fd) { … drmGetCap(fd, DRM_CAP_PRIME, &cap) … caps->dmabuf = cap; }
#endif
```

Phoenix is `DETECT_OS_PHOENIX` (our patch 0001), so the query was compiled out and **every screen
reported `caps.dmabuf = 0`**. v3d does not override it. The query would have worked: libdrm-phoenix
answers `DRM_CAP_PRIME = IMPORT|EXPORT` on the render node (`drm_phoenix_v3d.c:1225`).

What happened next:

* `create_dumb()` → `DRM_IOCTL_MODE_CREATE_DUMB 1920×1080×32` on card0 **succeeded**. Then
  `gbm_dri_bo_map_dumb` → `MAP_DUMB` token → `__wrap_mmap` → `/kmsbuf` mapping **succeeded** too (had it
  failed, `create_dumb` would have returned NULL and `get_back_bo` would have failed cleanly). This is a
  **positive first result for Mesa's GBM dumb path on our stack**. The `bos=0` afterwards is only the
  cleanup when the client died. The render server was simply never asked for anything, so `imports=0`.
* A dumb `gbm_bo` has `bo->image == NULL`. `dri2_drm_image_get_buffers` still returned success with
  `image_mask = BACK`, `back = bo->image = NULL`, which is the NULL dereference above.

**Independent evidence already in the log:** the display extension list has **no
`EGL_EXT_image_dma_buf_import`, `…_modifiers` or `EGL_MESA_image_dma_buf_export`**. `dri2_setup_screen`
(`egl_dri2.c:628-630`) gates these on the same `caps.dmabuf` bits. **Static evidence in the binary:**
`objdump` of `u_init_pipe_screen_caps` in the m3p3a `kmscube` contains **0** calls to `drmGetCap`.

**Mesa error handling (the second, upstream bug):** GBM's dumb fallback creates a buffer that the EGL GBM
platform can never render into, and `dri2_drm_image_get_buffers` does not check for it. On Linux this is
latent: only a driver without PRIME export reaches it. That is worth a tiny defensive patch that **reports
the problem** instead of crashing (0009). It does not hide the root cause: 0008 is the fix.

Every other link of the intended path (below) was checked against libdrm-phoenix and found wired, and
most of it was proven on hardware by `m3p2-drmprobe`: render-node `PRIME_FD_TO_HANDLE` → `V3DA_OP_BO_IMPORT`
(`drm_phoenix_v3d.c:1173`, G1, sends the known size), `lseek(SEEK_END)` (G3), `GET_BO_OFFSET` from the
import table, `MMAP_BO` of an import → OID memref, `GEM_CLOSE` → quarantine, the card0 export with
`DRM_CLOEXEC|DRM_RDWR` (opened `O_RDONLY|O_CLOEXEC` by design, E1), and kms `ADDFB2` validating only
`offset + pitch×height ≤ size` (`kms_bo.c:443`), so a 1024-px dumb BO can back a 1920-wide fb.

The intended path once `caps.dmabuf = 3`:
`gbm_dri_bo_create` → `dri_create_image_with_modifiers(LINEAR, SCANOUT|SHARE)` →
`v3d_resource_create_with_modifiers` (SCANOUT ⇒ linear, `v3d_resource.c:847`) → `screen->ro` ⇒
`renderonly_scanout_for_resource` → `renderonly_create_kms_dumb_buffer_for_resource`:
`CREATE_DUMB 1024 × 2026 × 32` on card0 → `PRIME_HANDLE_TO_FD` (`/kmsbuf/<h>`) →
`v3d_bo_open_dmabuf`: render `PRIME_FD_TO_HANDLE` (BO_IMPORT) → `lseek` → `V3D_GET_BO_OFFSET` → `close(fd)` →
`dri2_query_image(HANDLE)` = the kms handle through `renderonly_get_handle` → kmscube `ADDFB2
1920×1080 XR24 pitch 7680 mod 0` (no `DRM_MODE_FB_MODIFIERS`: LINEAR = 0).

### Fix

| Where | Change |
|---|---|
| `tools/gpu-lane/mesa-drm/patches/mesa/0008-gallium-u_screen-query-DRM_CAP_PRIME-on-Phoenix-RTOS.patch` (+2/−1) | `DETECT_OS_PHOENIX` added to the `u_screen.c` `drmGetCap(DRM_CAP_PRIME)` gate. One gate, three consumers: gbm_dri `has_dmabuf_*`, `dri_screen` `dmabuf_import`/`has_dmabuf`, EGL `has_dmabuf_*` (+ the dma-buf EGL extensions). |
| `…/0009-egl-drm-treat-a-GBM-back-buffer-without-a-DRI-image-as-failure.patch` (+12) | `get_back_bo`: a freshly created back BO with `image == NULL` → `libEGL warning: DRI2: GBM surface buffer has no DRI image (dumb-buffer fallback: the driver reports no dma-buf export)`, the BO is destroyed and `get_back_bo` fails, as for any other back-buffer allocation failure. Then getBuffers fails, `eglSwapBuffers` returns `EGL_BAD_ALLOC` and `gbm_surface_lock_front_buffer` returns NULL: a well-behaved client exits with an error. (Upstream kmscube does not check the locked BO: `drm-legacy.c:58-62` passes NULL to `drm_fb_get_from_bo`, which dereferences it, so kmscube would still fault, but in its own code and after the warning line has named the cause.) The check sits in `get_back_bo`, not `image_get_buffers`: there, swap would still hand out the dumb BO and the frontend would draw with no colour buffer. Defensive only: never taken when 0008 works. (A first variant in `image_get_buffers` was replaced before any cycle. Out dirs built with it, such as sdl2-drm's `mesa-gl`, are harmless with 0008 but should pick up the revision at their next full build.) |
| `tools/gpu-lane/libdrm-phoenix/src/xf86drm_phoenix.c`, `drm_phoenix_priv.h`, `drm_phoenix_wrap.c` (additive) | **`DRMPHX_TRACE`** (below). |

Mesa patch 0001's row above is unchanged. The OS gate is its own patch so each fix can be traced.

### `DRMPHX_TRACE` (libdrm-phoenix, opt-in)

Any value except empty or `0` turns it on. The environment is read once per process, at the first DRM call,
so `export DRMPHX_TRACE=0` in psh turns it off for later processes. When off, the cost is one load and a
branch per ioctl. Output goes to stderr, one `write()` per line:

```
DRMPHX conn  fd=<n> path=<fdpath> node=<card0|card1|render> port=<p> client=<id> rc=<rc>       (each new identification)
DRMPHX ioctl node=<card0|card1|render|?> fd=<n> nr=0x.. name=DRM_IOCTL_<…> rc=<0|-1> errno=<e> n=<count> <key args>
DRMPHX mmap  kind=<token|fd0|dmabuf> fd=<n> offset=0x.. handle=<h> len=<bytes> ptr=<p> errno=<e>
```

The trace is rate-limited per request number: the first 16 calls of each, then every 256th (`n=` counts
all calls). Key arguments: `CREATE_DUMB w h bpp flags → handle pitch size`, `MAP_DUMB`/`DESTROY_DUMB`,
`PRIME_* handle flags fd fdpath`, `GEM_CLOSE`, `GET_CAP`/`SET_CLIENT_CAP cap value`, `ADDFB2
WxH fmt flags handle pitch offset mod → fb`, `ADDFB`, `RMFB`, `SETCRTC`/`GETCRTC`, `PAGE_FLIP crtc fb flags`,
`ATOMIC`, `GETRESOURCES`, `GETCONNECTOR`, `WAIT_VBLANK`, `SYNCOBJ_*`, `V3D_CREATE_BO size flags → handle
offset`, `MMAP_BO`, `GET_BO_OFFSET`, `GET_PARAM`, `WAIT_BO`, `SUBMIT_CL bcl rcl bos flags syncs`,
`SUBMIT_TFU`/`CSD`. Names come from the `DRM_IOCTL_*` macros; the driver range decodes as V3D only on the
v3d server. `drmPhoenixMmap` and `__wrap_mmap`'s dma-buf branch log the token/dma-buf maps and keep `errno`.

### Builds (new outputs; the old binary kept)

| Artifact | Path | sha256 (first 16) |
|---|---|---|
| libdrm-phoenix (`DRMPHX_TRACE`) | `tools/gpu-lane/libdrm-phoenix/build-out-m3p3/prefix/lib/libdrm.a` | `b23358170ceea578` |
| drmprobe (same library, optional) | `tools/gpu-lane/libdrm-phoenix/build-out-m3p3/drmprobe` | `09d701d9a82958a1` |
| **kmscube, fixed** (Mesa 0001–0009 + libdrm `build-out-m3p3`) | `tools/gpu-lane/mesa-drm/build-out/kmscube-m3p3b-stripped` (= `kmscube-stripped`); unstripped `kmscube-m3p3b` (= `kmscube`) | `17977dafbc863f74` / `b29ac0131891f665` |
| kmscube, the failing m3p3a build (reference) | `tools/gpu-lane/mesa-drm/build-out/kmscube-m3p3a{,-stripped,.map}` | `7eadd92a74b24ff7` / `8569c7eb00c4b9bf` |

Checks on the new binary: `objdump` of `u_init_pipe_screen_caps` now has **1** `bl drmGetCap` (it was 0);
`strings` shows the 0009 warning and the three `DRMPHX` formats; `nm -u` = 0; old-lane strings 0; libdrm
backend 0 warnings; Mesa 56 warning lines in this (partial, stamp-triggered) rebuild: 55 × upstream `u_math.h:892`
`-Wsign-compare` in C++ TUs + 1 × `u_thread.c` `#warning` (no `pthread_setname_np`). Both are known; there are none in
`u_screen.c` or `platform_drm.c`.

**Host tests** (`DRMPHX_OUT=tools/gpu-lane/libdrm-phoenix/build-out-m3p3 tools/gpu-lane/libdrm-phoenix/hosttest/run.sh`):
`HOSTTEST checks=134 fails=0 verdict=PASS`, `HOSTE2E legacy verdict=PASS`, `HOSTE2E dri verdict=PASS`.
These are identical to part 2: the expected four fake-GPU pixel failures, `unaligned_ends=0`, and
`imports=1 imports_closed=1 deferred_flips=1`. **Plus trace runs** under ASan/UBSan (`DRMPHX_TRACE=1 e2e
legacy|dri`): 177/181 `DRMPHX` lines, 0 sanitizer reports, the same `DRMPROBE RESULT`. `DRMPHX_TRACE=0` → 0
lines. Sample: `DRMPHX ioctl node=render fd=4 nr=0x2e name=DRM_IOCTL_PRIME_FD_TO_HANDLE rc=0 errno=0 n=1
handle=57345 flags=0x0 fd=6 fdpath=/kmsbuf/1`. The Mesa side (0008/0009) has no host test: Mesa is not
built for the host here. The objdump check above is the static proof.

### Pre-registered next cycles

**Servers: the staged `-m3p2` binaries, unchanged** (no server change in this pass).

**Stage (coordinator)**, under new names so the failing binary is not silently replaced:

| Source | Export path |
|---|---|
| `tools/gpu-lane/mesa-drm/build-out/kmscube-m3p3b-stripped` | `<export>/bin/kmscube-m3p3b` |
| (already staged by m3p2) `rpi4-v3d-async-m3p2`, `rpi4-kms-m3p2`, `kmstest-m3p2` | — |

(`<export>` = `awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports`; `sudo install -m 755`, then `cmp`.)

**Cycle A `m3p3b-kmscube`** (Bash `timeout: 600000`). psh-interact waits `--idle-secs` after
every command, including each `export`, so the list is kept to 8 commands. Estimate: netboot 60–150 s +
~300 s.

```
./scripts/test-cycle-psh-interact.sh --label m3p3b-kmscube --idle-secs 30 --max-cmd-secs 150 \
    --hdmi-dense-on 'Using display' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-m3p2 -G" \
    "export DRMPHX_TRACE=1" \
    "export EGL_LOG_LEVEL=debug" \
    "/bin/kmscube-m3p3b -D /dev/dri/card0 -N -c 30" \
    "export DRMPHX_TRACE=0" \
    "/bin/kmscube-m3p3b -D /dev/dri/card0 -N -c 600" \
    "/bin/kmstest-m3p2 stats"
```

**Cycle B `m3p3b-kmscube2`** (only after A shows a cube): `-D /dev/kms` and `-M rgba` in fresh processes
plus the leak checks. Same two server lines, then `"/bin/kmscube-m3p3b -D /dev/kms -N -c 300"`,
`"/bin/kmscube-m3p3b -D /dev/dri/card0 -N -M rgba -c 300"`, `"/bin/kmstest-m3p2 stats"`, `"/bin/kmstest-m3p2
quit"`, `"/bin/v3dasync-ping stats"`, `"/bin/v3dasync-ping quit"`. Its predictions are the m3p3 table's rows
for runs 2–3 and for the stats lines.

Grade A:

```
grep -a -E '^(DRMPHX|KMS|KMSTEST|V3DA|libEGL|MESA|Rendered|Using display|  (version|renderer|vendor|display extensions):|DRM_IOCTL|failed|Failed|os_same)' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m3p3b-kmscube.log
./scripts/uart-summary.sh m3p3b-kmscube
```

(~1.3 % of UART lines are corrupted: re-read, don't count. EL0 dumps print twice. The trace makes run 1
chatty: ~150–300 lines in the first second.)

| Line / observation (traced run, `-c 30`) | Means |
|---|---|
| `display extensions:` **now contains `EGL_EXT_image_dma_buf_import` and `EGL_MESA_image_dma_buf_export`** | **0008 took**: `caps.dmabuf` ≠ 0. Absent → a stale binary staged (check sha `17977daf…`). |
| `DRMPHX ioctl node=render … name=DRM_IOCTL_GET_CAP … cap=0x5 value=0x3` | the `u_screen.c` query itself (cap 5 = `DRM_CAP_PRIME`). |
| `DRMPHX ioctl node=card0 … MODE_CREATE_DUMB … w=1024 h=2026 bpp=32 … pitch=4096 size=8298496` | the **kmsro scan-out path** (0008 working). `w=1920 h=1080` = GBM's dumb fallback = the old binary. `rc=-1 errno=12/28`: kms pool exhausted (re-run kms with `-p 48`). |
| `… PRIME_HANDLE_TO_FD rc=0 … fd=<n> fdpath=/kmsbuf/<h>` then `node=render … PRIME_FD_TO_HANDLE rc=0 … handle=<r> fdpath=/kmsbuf/<h>` + `V3DA srv import handle=… ns=kmsbuf id=<h> pages=2026 … contiguous=1` + `KMS srv kmsbuf atSize …` (first time only) | **G1/G3 from Mesa**. `PRIME_FD_TO_HANDLE rc=-1`: the errno names it (2 lookup/open, 22 port/size, 14 va2pa). The `V3DA srv import FAIL` line gives the server side. |
| `node=render … V3D_GET_BO_OFFSET rc=0 handle=<r> offset=0x…` (≠ 0) | `v3d_bo_open_handle` succeeded. `rc=-1` → `MESA: error: Failed to get BO offset`. |
| 2–4 such CREATE_DUMB/import groups over the run (one per colour buffer as the swap chain fills) | normal. More than 4 → leak/re-allocation. |
| `libEGL warning: DRI2: GBM surface buffer has no DRI image …` | 0009's report: the dumb fallback happened anyway, so `caps.dmabuf` is still 0 or the render node lacks EXPORT. The next line may be a kmscube fault in `drm_fb_get_from_bo` (kmscube passes the NULL locked BO on unchecked, `drm-legacy.c:62`). That is kmscube's own bug; the warning is the diagnosis. |
| `OpenGL ES 2.x information:` `version: "OpenGL ES 3.1 Mesa 26.2.0"`, `renderer: "V3D 4.2…"` | `eglMakeCurrent` passed the old crash point. |
| `node=render … V3D_SUBMIT_CL rc=0 … bos=<n> … out=<s>` | first frame submitted (after the shader compile). |
| `node=card0 … MODE_ADDFB2 rc=0 1920x1080 fmt=0x34325258 flags=0x0 handle=<h> pitch=7680 offset=0 mod=0x0 fb=<f>` | the scan-out fb on the imported dumb BO. `rc=-1 errno=22`: pitch/size (should not happen, `kms_bo.c:443`). `errno=2`: the handle is not this client's. |
| `… MODE_SETCRTC rc=0 crtc=0x40 fb=<f> … mode_valid=1 mode=1920x1080`, then `… MODE_PAGE_FLIP rc=0 … flags=0x1` (logged for n ≤ 16, then every 256th) | legacy loop running. `PAGE_FLIP errno=16`: flip while pending. |
| `DRMPHX mmap kind=token …` lines | only for CPU maps (shader BOs, uniforms, texture uploads); `ptr=(nil)`/`0x0` with `errno≠0` = the token path failed (Mesa then says `mmap of bo … failed`). |
| HDMI (dense after `Using display`): rotating smooth-shaded cube | **M3's GBM/EGL/KMS path works end to end**. A black screen with `Rendered` lines advancing → compare the `V3DA srv import pa0` with `KMS pool pa`. |
| `Rendered 30 frames …` / untraced run `Rendered N frames in 2.0x sec (F fps)` … final 600 | fps as the m3p3 prediction (≈30 = G12 poll quantum, 55–60 = aligned). The traced run's fps is **not** comparable. |
| `KMSTEST stats … bos=0 exports=0 apply_errors=0` | no kms leak after two processes. |
| any EL0 fault | `aarch64-phoenix-addr2line -f -i -C -e tools/gpu-lane/mesa-drm/build-out/kmscube-m3p3b <pc>`. |

**What A decides:** a cube with the kmsro lines = M3 part 3 PASS (then run B). A fault or error *after*
the `CREATE_DUMB 1024×2026` line is the next link of the chain, named by the first `rc=-1` DRMPHX line.
If `CREATE_DUMB 1024×2026` is missing, 0008 did not take.

## M3 part 4 — SDL2 KMSDRM + quakespasm-drm (2026-09-27)

**Status:** **builds and links; no Pi cycle yet** (pre-registered below). SDL 2.30.12 — the version
`ports/sdl2` ships — built **static** with its **stock KMSDRM video driver** on Mesa's GBM + EGL
(desktop GL) and libdrm-phoenix, and **`quakespasm-drm`**: a clone of the quakespasm port rebuilt
against it. 0 undefined symbols, no old-lane string, no dynamic loading. Nothing committed, nothing
staged; no server, no libdrm-phoenix source, no old-lane file (`ports/sdl2`, the Mesa fork,
`tools/.gpu-libs`, the shipped `/usr/bin/quakespasm`) and no sibling repo touched.
Code: [`tools/gpu-lane/sdl2-drm/`](../../tools/gpu-lane/sdl2-drm/).

### Build

```
tools/gpu-lane/sdl2-drm/build.sh                 # Mesa-GL (via mesa-drm) + SDL + quakespasm-drm, ≈5 min cold
tools/gpu-lane/sdl2-drm/build.sh --skip-mesa     # SDL + quakespasm-drm only, ≈1.5 min
tools/gpu-lane/sdl2-drm/build.sh --libdrm-prefix tools/gpu-lane/libdrm-phoenix/build-out-<x>/prefix   # relink on another libdrm-phoenix (default build-out-m3p3)
tools/gpu-lane/sdl2-drm/build.sh --clean
```

Writes only `tools/gpu-lane/sdl2-drm/build-out/` (gitignored, `.gitignore` added):

| Step | What |
|---|---|
| 1. Mesa with desktop GL | `mesa-drm/build.sh --opengl --out build-out/mesa-gl --libdrm-prefix …/build-out-m3p3/prefix` (m3p3 = the part-2 library + the opt-in `DRMPHX_TRACE`; b233581…). quakespasm is a **desktop-GL** program (`glBegin`, fixed function + GLSL) and the committed mesa-drm build is GLES-only, so **`--opengl` is a new opt-in option of `mesa-drm/build.sh`** (default unchanged, still `-Dopengl=false`; a build dir configured the other way refuses to be reused — `mesa-opengl.txt`, seeded from the dir's own meson summary for dirs older than the label). It adds `-Dopengl=true` (GLES2 stays on) and builds `src/mesa/glapi/glapi/libglapi_bridge.a` — the static `gl*` entry points libGL would export, which meson does not build with `glx=disabled` (`build_by_default: false`); `nm` shows `T glBegin`, `glVertex3f`, `glClear`. The same run relinks a GL-enabled kmscube there (unused). The delivered build is a **full** Mesa build (fresh `mesa-gl/`) with mesa-drm's committed patches 0001–**0009** (patch set `278cdef4539b4a27`): 0008 (`u_screen` queries DRM_CAP_PRIME on Phoenix — the kmscube first-run fix) matters here exactly as for kmscube, because SDL creates its GBM surface the same way; checked in the binary: `u_init_pipe_screen_caps` has 1 `bl drmGetCap`. `libglapi_bridge.a` is a meson **thin** archive (it points into `mesa-gl/mesa-build/`): a `--clean` of `mesa-gl` means a full Mesa rebuild (≈5 min), not a relink. |
| 2. SDL source | `ports/sdl2`'s tarball (read-only) + `patches/000{1..8}` + `overlay/` → `build-out/sdl-src` (re-extracted when the tarball/patch/overlay stamp changes). |
| 3. SDL configure | cmake as the port (`CMAKE_SYSTEM_NAME=Generic`, `-DPHOENIX=ON`, static only) with `-DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF -DSDL_HIDAPI=OFF`, every host backend off. Flags = mesa-drm's target flags + the tree sysroot — **not** the port's `-I<ports prefix>/include` (it holds other ports' `GL/`/`X11/` headers). `PKG_CONFIG` is a wrapper restricted to `mesa-gl/prefix` (egl, gbm), mesa-drm's libdrm-phoenix snapshot (libdrm) and its private zlib prefix; it strips the `-pthread` Mesa's `.pc` files carry (aarch64-phoenix-gcc rejects it). The build **fails** unless `SDL_config.h` has `SDL_VIDEO_DRIVER_KMSDRM`, `SDL_VIDEO_OPENGL_EGL`, `SDL_VIDEO_OPENGL`, `SDL_INPUT_PHOENIX`, `SDL_AUDIO_DRIVER_PHOENIX`, pthreads + unix timer, and none of `SDL_VIDEO_DRIVER_KMSDRM_DYNAMIC`, `SDL_VIDEO_DRIVER_PHOENIX` (old lane), X11/Wayland, `SDL_INPUT_LINUXEV`, `SDL_LOADSO_DLOPEN`, Vulkan. |
| 4. quakespasm-drm | the port's pinned commit `f5fe178` + the port's single patch `0001-quakespasm-phoenix-v3d-single-elf.patch` + the port's three Phoenix glue files (all read-only from `sources/phoenix-rtos-ports/quakespasm/`, GPL-2.0+, an application — as the shipped port), the port's exact TU list (`gl_vidsdl`/`in_sdl`/`snd_sdl` + `pl_phoenix_{sys,main,stubs}`) and flags (`-DUSE_SDL2 -DNO_SDL_CONFIG -ffreestanding -O2 …`), GL headers from the clone's own Mesa source. **Not built:** the old lane's GL-context glue `ports/sdl2/glue/sdl_phoenix_glctx.c` (SDL's KMSDRM + EGL own the context now). Added: `qsdrm/qsdrm_banner.c`, a constructor that writes one line (`quakespasm-drm: new GPU lane -- SDL 2.30.12 KMSDRM + Mesa 26.2 GBM/EGL (desktop GL) + libdrm-phoenix -> …`, `write(1)`, no stdio before `main()`'s `setvbuf`) so a UART log names the lane, and sets SDL's VIDEO + INPUT log categories to DEBUG so KMSDRM reports its init steps (a dozen `DEBUG:` lines at start, nothing per frame) without an `export` in the cycle. |
| 5. link | mesa-drm's kmscube shape: C++ driver, `-static`, `--gc-sections`, `-z max-page-size=0x1000`, **`-Wl,--wrap=mmap`**, `libgallium-26.2.0.a` whole-archive, one group of `libSDL2.a` + **`libglapi_bridge.a` (instead of `libGLESv2.a`, whose `gl*` would clash)** + libEGL/libgbm/dri_gbm/libglapi/v3d/broadcom/winsys/util archives + `libdrm.a` + the compat shim + the ports' `libz.a`; plus the port's `-z stack-size=33554432`. Link log empty. |

### SDL patches (`tools/gpu-lane/sdl2-drm/patches/`)

| # | Patch | Lines | Origin / rationale |
|---|---|---|---|
| 0001 | `cmake-phoenix-pthread-detection` | +5/−2 | **verbatim** from `ports/sdl2` (pthreads live in libphoenix; gcc rejects `-pthread`) |
| 0002 | `cmake-phoenix-platform-branch` | +21/−0 | **verbatim** (the PHOENIX cmake branch that skips the host probes; unix timer + pthreads) |
| 0003 | `dynapi-disable-on-phoenix` | +2/−0 | **verbatim** (static only) |
| 0004 | `systhread-priority-noop-on-phoenix` | +5/−2 | **verbatim** (`pthread_{get,set}schedparam` unimplemented) |
| 0005 | `cmake-phoenix-audio-driver` | +15/−0 | **adapted** from ports/sdl2 0006: same hunks, rebased onto a tree **without** the old lane's video patch 0005. The driver (`overlay/src/audio/phoenix/`, `/dev/audio0` pull model) is copied **verbatim** from the ports/sdl2 overlay. |
| 0006 | `kmsdrm-static-egl-on-phoenix` | +39/−2 | **new.** The PHOENIX branch calls exactly `CheckEGL()` + `CheckKMSDRM()` (pkg-config on the cross prefixes), refuses `SDL_KMSDRM_SHARED=ON` (then `SDL_kmsdrmdyn.c` binds `KMSDRM_drm*`/`gbm_*` directly), sets GL/GLES2 attribute plumbing + renderers (all via `SDL_GL_GetProcAddress`, no link dependency). `SDL_egl.c`: Phoenix takes the branch the static ANGLE/Vita builds take — `LOAD_FUNC` references the EGL functions directly and the `SDL_LoadObject("libGL…"/"libEGL…")` block is skipped; GL entry points come from `eglGetProcAddress` (EGL 1.5). |
| 0007 | `kmsdrm-phoenix-hid-input` | +17/−0 | **new.** `SDL_INPUT_PHOENIX`, a third KMSDRM input source beside evdev/wscons (Init/PumpEvents/Quit). The code, `overlay/src/core/phoenix/SDL_phoenixhid.c` (280 lines, zlib), is the old phoenix video driver's HID report handling (8-byte keyboard reports diffed into key events + US-QWERTY `SDL_TEXTINPUT`, 4-byte relative mouse packets, bounded non-blocking drains, bounded lazy open), detached from that driver: events go to the focused window. |
| 0008 | `kmsdrm-xrgb8888-scanout-on-phoenix` | +9/−0 | **new.** KMSDRM hard-codes an ARGB8888 GBM surface; rpi4-kms maps AR24 to the firmware's `VC_IMAGE_ARGB8888` on a primary plane stacked **above the console framebuffer**, so a game leaving alpha < 1 (glClear alpha 0, blended passes) could show console text through the picture. XRGB8888 (kmscube's default; the EGLConfig follows through `SDL_EGL_SetRequiredVisualId`). |

**Not taken from ports/sdl2:** its 0005 + `overlay/src/video/phoenix/` (the old lane's `/dev/fb0` video
driver) and `glue/` (the in-process GL context).

### Verification (the delivered binary)

| Check | Result |
|---|---|
| static link | OK, link log empty; no `PT_INTERP` |
| `aarch64-phoenix-nm -u quakespasm-drm` | **0** symbols |
| `size` | text 17 365 166, data 565 476, bss 6 175 276; file 101 500 632 B, **stripped 17 938 024 B** (shipped `/usr/bin/quakespasm` 18 578 808 B, `quakespasm-v3da.stripped` 18 562 600 B); `libSDL2.a` 9 615 722 B |
| sha256 (first 16) | `quakespasm-drm` `ac29ad23653cc553`, `quakespasm-drm.stripped` `51526dfcb18edda8` (`build-out/BUILD-INFO.txt`); inputs: SDL set `2f79883be1753ceb`, Mesa set `278cdef4539b4a27`, libdrm.a `b23358170ceea578` (m3p3) |
| SDL KMSDRM, static | symbols `KMSDRM_CreateDevice`, `KMSDRM_GLES_SwapWindow`, `SDL_EGL_LoadLibrary`, `SDL_PHOENIX_HID_Poll`; strings `KMS/DRM Video Driver`, `/dev/dri/` (4), `/dev/kbd0`, `/dev/audio0`; `eglChooseConfig`/`eglGetPlatformDisplay` referenced directly by `SDL_EGL_LoadLibraryInternal`; SDL's loadso is the dummy one |
| Mesa + libdrm-phoenix | `gbmint_get_backend`, `kmsro_drm_screen_create`, `v3d_drm_screen_create_renderonly`, `glBegin` (bridge), `eglGetPlatformDisplayEXT`, `drm_phoenix_ioctl`, `drmPhoenixMmap`, `__wrap_mmap`; strings `libdrm-phoenix:`, `DRMPHX_TRACE`, `EGL_KHR_platform_gbm`, `V3D 4.2`, `kmsro`, `quakespasm-drm:`. `objdump`: `gbm_dri_bo_create`, `v3d_bo_map_unsynchronized`, `vc4_bo_map_unsynchronized`, `dri_sw/kms_sw_displaytarget_map` and libphoenix's `malloc`/`fopen` call `__wrap_mmap`; the only direct `bl mmap` are inside `__wrap_mmap` |
| old lane absent | strings `v3d-winsys:` 0, `v3da-winsys:` 0, `phxgl` 0, `/dev/fb0` 0, `RPI4FB_GETMODE` 0, `phoenix_v3d_ioctl` 0, `peek_next_scanout` 0, `v3d-srv` 0; symbols `PHOENIX_bootstrap`, `PHOENIX_PumpEvents`, `phxgl_init`, `winsys_init`, `v3da_connect` absent (`build.sh` fails on any) |
| warnings | SDL: 20 × upstream `__ieee754_sqrt redefined` (`src/libm`), 5 × upstream `-Wundef` in the always-compiled hidapi stub; 0 in patched code. quakespasm 0. Mesa 56 (mesa-drm's known upstream set). |

### Runtime path (what the Pi will exercise, in order)

1. `SDL_Init(VIDEO)` → `KMSDRM_Available`: `opendir("/dev/dri/")`, open each `card*` `O_RDWR`,
   `drmModeGetResources` (card1 = v3d primary → `-EOPNOTSUPP` → skipped; card0 → 1 connector),
   `drmSetMaster` + `drmAuthMagic(fd, 0)` (both accepted no-ops on card0, so not `-EACCES`).
2. `KMSDRM_VideoInit`: re-open card0, connector/encoder/CRTC, the single 1920×1080 mode;
   `DRM_CAP_ASYNC_PAGE_FLIP` = 0; `drmDropMaster` (no-op) keeps the fd; HID `/dev/kbd0` + `/dev/mouse0`
   open attempts.
3. `SDL_CreateWindow(800×600, OPENGL)` → `gbm_create_device` (G2 fstat, kmsro pairing as kmscube) →
   `eglGetPlatformDisplay(GBM)` → cursor BO (64×64 dumb, `GBM_BO_USE_CURSOR|WRITE`; soft on failure) →
   closest mode = **1920×1080** (the only one; any window size gets it) → `gbm_surface_create(1920×1080,
   XRGB8888, SCANOUT|RENDERING)` → EGL window surface → `RESIZED` to 1920×1080, which quakespasm reads
   back (`VID_GetCurrentWidth` = `SDL_GetWindowSize`).
4. `SDL_GL_CreateContext`: `eglBindAPI(EGL_OPENGL_API)`, a desktop GL 2.1 (compat) context — the first
   desktop-GL context on the new lane.
5. Per frame `SDL_GL_SwapWindow` → `KMSDRM_WaitPageflip` (`poll()` on card0 + `drmHandleEvent`) →
   `eglSwapBuffers` → `gbm_surface_lock_front_buffer` → `drmModeAddFB2` (once per BO) → first frame
   `drmModeSetCrtc`, then `drmModePageFlip(EVENT)` (G13 attaches the imported BO's fence; `rpi4-kms -G`
   gates the flip).

### Gaps

| Gap | Effect | Remedy |
|---|---|---|
| **Keyboard probably needs the console handed over** | per the old driver's notes pl011-tty's console bridge holds `/dev/kbd0` while fbcon is in text mode; on the new lane only `rpi4-kms -C` disables it. With `-G` alone (the cycle below) either the open keeps failing (bounded retries; no `phoenix-hid: /dev/kbd0 open` DEBUG line) or it succeeds and the RAW-mode switch competes with the console bridge for the reports [inferred, not read] — harmless for a timedemo, which needs no input. | a follow-up cycle with `rpi4-kms-m3p2 -G -C` (then type in the Quake console); it grades both outcomes |
| HID code not run on hardware in this form | the report handling is the old driver's proven code; the detached version is host-compiled only | the `-G -C` cycle |
| No hardware cursor | `MODE_CURSOR` is a library stub (`-ENOSYS`, §3.2); SDL treats it as soft (debug-level log). quakespasm hides the cursor anyway | cursor plane mapping in libdrm-phoenix (§3.2 follow-up) |
| Mode list = the current mode | Stage A: every window is 1920×1080 (SDL picks the closest mode) — as the old lane's always-native `/dev/fb0` window [inferred] | kms Stage B modesets |
| `SetWindowGammaRamp` | `drmModeCrtcSetGamma` is a stub → SDL gamma fails; quakespasm uses its GLSL gamma path when available | — |
| G12 `poll()` quantum | `KMSDRM_WaitPageflip` `poll(-1)`s the card fd: when the previous flip has not completed at swap time, the wake rides the kernel's 20 ms cycle | kernel (E5) |
| no joystick / filesystem / power / sensor backends | SDL dummy drivers, as the shipped port | — |
| libdrm-phoenix is embedded | every library-side libdrm-phoenix fix needs a relink (the binary embeds `libdrm.a`, now m3p3) | `build.sh --libdrm-prefix …/build-out-<x>/prefix` (≈2 min, Mesa objects unaffected) |

### Pre-registered Pi cycle `m3p4-qsdrm` (one netboot cycle)

**Question:** does an unmodified SDL2 game — SDL's stock KMSDRM driver + Mesa GBM/EGL desktop GL —
run on the full DRM-shaped stack (libdrm-phoenix → `rpi4-kms` + `rpi4-v3d-async`), render on HDMI,
and at what `timedemo demo1` rate compared with the old lane and `quakespasm-v3da`?

**Gate:** after `m3p3b-kmscube` (the re-run with Mesa 0008/0009 + libdrm m3p3, the same Mesa patch set
and libdrm this binary embeds) shows a rotating cube — same GBM/EGL/kmsro/import chain; a kmscube
failure would fail here for the same reason and add nothing.

**Preconditions:** netboot image ≥ build 9 with `core_freq=500` (as the 40.4 fps reference); no GPU
app, X or `rpi4-v3d` in the boot; the m3p2 servers staged; `/usr/share/quake/id1/pak0.pak` present
(as for the shipped quakespasm).

**Stage (coordinator)** (`sudo install -m 755 <source> <path>`, `cmp` afterwards; `<export>` = the live
fsid=0 export, `awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports`):

| Source | Export path |
|---|---|
| `tools/gpu-lane/sdl2-drm/build-out/quakespasm-drm.stripped` | `<export>/usr/bin/quakespasm-drm` |
| `tools/gpu-lane/v3d-async/out-m3p2/rpi4-v3d-async` | `<export>/bin/rpi4-v3d-async-m3p2` (if not staged by m3p2) |
| `tools/gpu-lane/kms/out-m3p2/rpi4-kms` | `<export>/bin/rpi4-kms-m3p2` (if not staged by m3p2) |

Keep the unstripped `build-out/quakespasm-drm` on the host for `addr2line`.

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label m3p4-qsdrm --idle-secs 30 --max-cmd-secs 240 \
    --ready-line 'V3DA srv detached|KMS srv detached|frames .* seconds .* fps' --ready-extra-secs 15 \
    --hdmi-dense-on 'quakespasm-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-m3p2 -G" \
    "/usr/bin/quakespasm-drm +timedemo demo1"
```

* `--idle-secs` is inert here: with `--ready-line` set, psh-interact ends a command only at the
  ready-line (+ `--ready-extra-secs`) or at `--max-cmd-secs`.
* **No environment is needed and none is set:** KMSDRM is the only real video driver in this SDL
  (the dummy driver answers only `SDL_VIDEODRIVER=dummy`), and an `export` line would print nothing that
  matches `--ready-line`, so it would burn the whole `--max-cmd-secs` (psh-interact applies the
  ready-line to every command, M1 §16 correction). The same holds for `DRMPHX_TRACE=1`: keep it for a
  diagnostic re-run, not the first cycle. Fallback only if the log shows
  `Failed to open directory '/dev/dri/'`: a second cycle with `"export SDL_KMSDRM_DEVICE_INDEX=0"` first.
* The game is **last**: quakespasm returns to its console after a timedemo and never exits, so
  nothing after it would run (no `stats`/`quit` lines).
* Budget: netboot 60–150 s + 2 × (detach + 15 s) + game ≤ 240 s (pak load over NFS + first-frame
  shader compile, Mesa shader cache off + 969 frames ≈ 25–40 s) + 15 s ≈ 5–8 min.

Grade (tagged lines; ~1.3 % UART line corruption — re-read, don't count; EL0 dumps print twice):

```
grep -a -E '^(quakespasm|KMS |V3DA |GL_|Video mode|[0-9]+ frames|libdrm-phoenix|libEGL|os_same_file|MESA|DRI2|DEBUG|ERROR|WARN|Quake Error)' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m3p4-qsdrm.log
./scripts/uart-summary.sh m3p4-qsdrm
```

**HDMI grading rule:** only snapshots **after** the `(psh)% /usr/bin/quakespasm-drm +timedemo demo1`
echo / the `quakespasm-drm: new GPU lane` line count (dense 5 s ticks from the banner); every earlier tick
shows the console during the two server commands and says nothing about the game (the M1 P2-B grading
error).

**Predictions** and what each alternative means:

| Line / observation | Predicted | If instead… |
|---|---|---|
| `V3DA srv detached …`, `KMS srv detached …`, the part-2 `dri name=… registered=1` lines | as in `m3p2-drmprobe` | a server missing: staging/boot problem, stop. |
| `quakespasm-drm: new GPU lane -- SDL 2.30.12 KMSDRM …` then `quakespasm: main() entered (argc=3)` | once | a different banner / none: the wrong binary is staged (`cmp`). |
| `DEBUG: Opening device /dev/dri/card0`, `DEBUG: Opened DRM FD (n)`, `DEBUG: /dev/dri/card0 connector, encoder and CRTC counts are: 1 1 1` (SDL KMSDRM) | at video init | no KMSDRM `DEBUG:` line at all: the log priority did not take (then read the `Quake Error` line); `Failed to open KMSDRM device /dev/dri/cardN, errno: …`: an open failed (card1 failing is harmless, card0 is not). |
| `KMS srv fstat answered …`, `V3DA srv fstat answered …` (first answers) | during video init | `Quake Error: Couldn't create window` (quakespasm prints no SDL detail here): GBM/EGL init failed inside `KMSDRM_CreateWindow` — G2 or the kmsro pairing; compare the server lines with the kmscube cycle's. `Quake Error: Couldn't init SDL video: No available video device`: `KMSDRM_Available` found no usable `card*` — `opendir("/dev/dri/")` failed (→ the index fallback above) or open/GetResources of `/dev/dri/card0` failed (read the `KMS` lines). |
| `os_same_file_description couldn't determine …` | at most once (no `kcmp`) | — |
| `V3DA srv import handle=… ns=kmsbuf … pages=2026 … contiguous=1` × 3 (maybe 4) + `KMS srv kmsbuf atSize id=… (first; G3)`, and one `DEBUG: New DRM FB (n): 1920x1080, from BO 0x…` per buffer | **the GBM surface's scan-out buffers imported on the render node** (the kmscube prediction, same sizes: 1920×1080 linear) | `libEGL warning: DRI2: GBM surface buffer has no DRI image …`: Mesa 0008 missing (stale Mesa); `Failed to create scanout resource` / `DRM_IOCTL_MODE_CREATE_DUMB failed`: kms pool exhausted (4 × 7.9 MiB + the 16 KiB cursor BO in 32 MiB) → re-run with `rpi4-kms-m3p2 -G -p 48`; `Failed to get v3d handle for dmabuf`: G1; `mmap of bo … failed`: the `--wrap=mmap` path. |
| `GL_VENDOR: Broadcom`, `GL_RENDERER: V3D 4.2…`, `GL_VERSION: 2.1 Mesa 26.2.0` (or a higher compat version) | a **desktop GL** V3D context through EGL | `Quake Error: Couldn't create GL context`: the EGLConfig has no `EGL_OPENGL_BIT` (desktop GL not in this Mesa → `mesa-opengl.txt`) or `eglBindAPI(EGL_OPENGL_API)` failed; a renderer `llvmpipe`/`softpipe` is impossible (not built) — a `kms_swrast` name means the render node was not found. |
| `Video mode 1920x1080x32 60Hz (24-bit z-buffer, 0x FSAA) initialized` (quakespasm) | 1920×1080 (the only mode) | 800×600: SDL did not resize the window (`RESIZED` path) — the picture would occupy a corner. |
| HDMI after the echo | **demo1 playing full screen** (E1M3 walkthrough: "You got the nails" etc.), no console text over it, no tearing; at the end the Quake console with the `… fps` line | console text bleeding through the picture: an alpha format reached scan-out (patch 0008 not in the build); black with `frames` advancing: rendering lands elsewhere (compare `V3DA srv import pa0` with `KMS pool pa`); frozen first frame: flips stopped (`ERROR: Could not queue pageflip` / `Wait for previous pageflip failed`); torn/half-drawn frames: G13 did not gate (`libdrm-phoenix: rpi4-kms runs without -G` line). |
| `969 frames X seconds Y fps` | **Y = 30–45**. Reference: `quakespasm-v3da` 40.4 fps (core 500, same server flags), old lane 30.4 fps (core 250; a core-500 old-lane figure is still open). Y ≥ 36 = parity with the v3da clone through a real DRM client stack; 25–36 with the rest clean = present-path cost, most likely **G12** (a swap that finds the previous flip pending waits for the next 20 ms poll cycle) — not a render regression; flips are vsynced by construction (`ASYNC_PAGE_FLIP` = 0), so Y ≤ 60 | no `frames` line within `--max-cmd-secs`: a wait that never returns (V3DA wedge lines, or `poll()` on card0 never waking — `KMS` event lines) or a load stall (last quakespasm line tells which). |
| `ERROR:`/`WARN:` lines from SDL | none, except possibly cursor-related ones (`drmModeSetCursor` is a stub; SDL reports those at DEBUG, which is on here) | anything else: read it — SDL's KMSDRM errors are specific. |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | an EL0 fault in quakespasm-drm: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/sdl2-drm/build-out/quakespasm-drm <pc>` (unstripped, same link). |

**What the cycle decides:** demo1 on HDMI with a `frames … fps` line = **M3's "SDL2 KMSDRM" item
done — the first unmodified SDL2 game on the DRM-shaped stack**; the fps says whether the present path
(G12, G13 gating) costs anything against `quakespasm-v3da`. Next: the keyboard cycle (`-G -C`, type in
the console), the old-lane quakespasm at core 500 for a same-clock three-way comparison, then the other
SDL2 games as clones.

## Result — `m3p3b-kmscube` (queue20, 2026-09-27 05:06–05:12): **PASS — a spinning cube on HDMI through the whole new stack**

Mesa 26.2 GBM/EGL/GLES (static, patches 0001–0009) → libdrm-phoenix (`DRMPHX_TRACE`) → rpi4-kms
(dumb BOs, firmware planes, flip events) + rpi4-v3d-async (zero-copy PRIME import, V3D jobs). 0 exceptions.

- The trace shows the pre-registered path: `DRM_IOCTL_MODE_CREATE_DUMB … w=1024 h=2026` on card0 (the
  fixed binary), then on the render node `DRM_IOCTL_PRIME_FD_TO_HANDLE rc=0 … fdpath=/kmsbuf/1` and
  `V3DA srv import … pages=2026 contiguous=1 gpuva=0x02102000` — two scanout buffers per run, both
  released at exit (`import released … live=0`). 8 imports across the runs.
- `OpenGL ES 3.1 Mesa 26.2.0 … renderer: "V3D 4.2.14.0"`; EGL now lists `EGL_EXT_image_dma_buf_import`,
  `…_modifiers` and `EGL_MESA_image_dma_buf_export` (absent in the failing run — the patch 0008 tell).
- `-c 600`: **599 frames in 20.06 s = 29.85 fps**, steady (28.6 → 29.9 over the run); server
  `err=0 wedges=0 rej=0`.
- **HDMI** (graded on the snapshots taken during the run, 05:10:59–05:11:22, every one different): a
  full-screen shaded rotating cube on grey (`artifacts/hdmi/20260927-051111-m3p3b-kmscube-tick.png`);
  the console returns after exit (`planes_off=1`).

**Why ~30 fps, not 60** ↩ *corrected 2026-09-27*: not the 20 ms poll quantum as first written here. rpi4-kms
deferred every fence-carrying flip and never woke its vblank thread, so each flip took two vblanks;
fixed in `efa4c1f9f` → **60.00 fps** (poll-wake.md, queue26). The original reading was:
kmscube's legacy loop waits for each page-flip event with `select()` on the card
fd, and on Phoenix a poll of a device-server fd is quantised to 20 ms (P9 / G12) — so it catches every
other vblank. The GPU is idle most of the time (render 1.2 s busy over 20 s). This is the known kernel
gap, now with a real client to prove the fix against: a `block_ms` path for device-server fds in
`posix_poll` (E5 §(b)) should take kmscube to 60.

## Result — `m3p4-qsdrm` (queue21, 2026-09-27 05:12–05:15): **PASS — Quake on the full DRM stack, 24.4 fps (poll-bound)**

`quakespasm-drm` (SDL 2.30.12 KMSDRM + Mesa 26.2 GBM/EGL desktop GL + libdrm-phoenix → rpi4-kms +
rpi4-v3d-async): `969 frames 39.8 seconds 24.4 fps`, 0 exceptions. Banner line present; three scanout
BOs imported (`V3DA srv import … pages=2026`), released and re-imported cleanly on the mode set.
HDMI (snapshots during the timedemo, each different): demo1 renders correctly — lit textured level,
models, particles, HUD, console messages, in-game counter "26 FPS"
(`artifacts/hdmi/20260927-051442-m3p4-qsdrm-tick.png`).

As pre-registered: "25–36 with everything else clean points at the 20 ms poll quantum rather than
rendering" — SDL's KMSDRM waits for every page flip with `poll()` on the card fd (P9/G12); 24.4 is
just under that band, and quakespasm-v3da (same game, same GPU server, no page-flip poll) does 40.4.
The kernel readiness-wakeup work (agent, `docs/gpu-new-lane/poll-wake.md`) is the fix to measure next.

## STK on the full DRM stack (stk-drm) — build (2026-09-27)

**Status:** **builds, links, all static checks pass; no Pi cycle yet** (pre-registered below).
`supertuxkart-drm` is SuperTuxKart 1.4, relinked from the port's own build tree, on the **full
standard stack**: SDL 2.30.12's stock **KMSDRM** driver (`tools/gpu-lane/sdl2-drm`) + Mesa 26.2
**GBM/EGL/GLES** + **libdrm-phoenix m5b** (the G15 `--wrap=ioctl` interposer), talking to `rpi4-kms`
(card0) and `rpi4-v3d-async` (renderD128). The launcher is `stk-drm`. Nothing committed or staged. No
old-lane file was touched (shipped `/usr/bin/supertuxkart` and `/bin/stk`, `ports/sdl2`,
`tools/.gpu-libs`, the Mesa fork), and neither was any sibling repo. `sdl2-drm/build.sh` and
`mesa-drm/build.sh` were **not** run: `libSDL2.a` and `quakespasm-drm` are byte-identical to before.
Code: [`tools/gpu-lane/sdl2-drm/build-stk-drm.sh`](../../tools/gpu-lane/sdl2-drm/build-stk-drm.sh),
[`tools/gpu-lane/sdl2-drm/stkdrm/stkdrm_hooks.c`](../../tools/gpu-lane/sdl2-drm/stkdrm/stkdrm_hooks.c)
(both BSD-3; STK itself stays a GPL application built in its port tree, as for `stk-v3da`).

### Build

```
tools/gpu-lane/sdl2-drm/build-stk-drm.sh              # control relink + drm relink + objdump proofs
tools/gpu-lane/sdl2-drm/build-stk-drm.sh --no-control # skip the control relink
tools/gpu-lane/sdl2-drm/build-stk-drm.sh --libdrm-prefix tools/gpu-lane/libdrm-phoenix/build-out-<x>/prefix
```

Needs the STK port built in `.buildroot`, a finished `sdl2-drm/build.sh` (`build-out/sdl-prefix` +
`build-out/mesa-gl`) and `libdrm-phoenix/build-out-m5b`. It writes only
`tools/gpu-lane/sdl2-drm/build-out/stk-drm/` (gitignored).

| Step | What |
|---|---|
| Renderer | STK's port is configured `-DUSE_GLES2=ON`. Irrlicht's SDL device asks for an **ES 3.0** context (`SDL_GL_CONTEXT_PROFILE_ES`, 8/8/8 + depth 24, double-buffered, no multisample attribute), falling back to ES 2.0. It then loads **every** `gl*` through glad + `SDL_GL_GetProcAddress`, which is `eglGetProcAddress` → shared-glapi's stub table. So STK references **no** `gl*` symbol directly, and no desktop-GL entry points are needed. The Mesa used is **`sdl2-drm/build-out/mesa-gl`**, patch set `278cdef4539b4a27` (0001–0009, every GL-relevant one; 0010/0011 are Vulkan-only). That build has desktop GL **and** GLES2, and it is the Mesa `libSDL2.a` was configured against. The same Mesa + SDL passed on the Pi with quakespasm-drm. |
| libdrm-phoenix | `build-out-m5b` (`libdrm.a` `a508e207…`) is snapshotted into `stk-drm/libdrm-prefix`. mesa-gl's objects were compiled against its m3p3 snapshot, whose headers the script checks are **byte-identical** to m5b's (`diff -r`), so linking the m5b archive is exact. The script fails if the archive has no `__wrap_ioctl`, which would mean a pre-m5b library. |
| Link | `link.txt` from the STK port build tree (the port's stage 4) with: `-o` redirected; the ports-prefix `libSDL2.a` (old `/dev/fb0` video driver) replaced by the KMSDRM `libSDL2.a` (the script requires exactly one occurrence and fails if the final command still names any old-lane input); the old GL glue objects `sdl_phoenix_glctx.o`/`sdl_phoenix_glstubs.o` and `libGL-phoenix.a`/`libv3d-phoenix.a` **not linked**. Added: `stkdrm_hooks.o`, `-static -Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,--wrap=SDL_GL_SwapWindow -Wl,-Map`, and the kmscube/quakespasm-drm Mesa shape: `libgallium-26.2.0.a` whole-archive, then one group of `libSDL2.a` + libEGL/libgbm/dri_gbm/**libGLESv2**/libglapi/v3d/broadcom/winsys/util archives + `libdrm.a` + the compat shim + the port's zlib/ogg/vorbis/mbedtls. The 8 MiB main stack is unchanged. Link log empty. |
| Control | build-stk-v3da.sh's relink, verbatim with the shipped inputs: **byte-identical to the shipped `prog/supertuxkart`** (`30c69197…`). So the engine objects in the clone are exactly the shipped ones. |
| Launcher | `tools/supertuxkart-port/stk-launcher.c` with exactly **3 lines** rewritten: exec path `/usr/bin/supertuxkart-drm`, the `stk-drm: exec` error, and the `stk-drm: DATADIR=` banner. It keeps the same default args (`--screensize=1920x1080 --fullscreen --disable-texture-compression --disable-addon-karts --disable-addon-tracks`) and seeds the same `players.xml`/`config.xml` (`show_fps="true" scale_rtts_factor="0.75"`, enable_internet=2), so its workload is the one `stk`/`stk-v3da` render. |

### The frame counter (`stkdrm_hooks.c`, linked into this clone only)

KMSDRM prints nothing per frame, so the clone carries its own flipstat. Irrlicht's
`COGLES2Driver::endScene` calls `SDL_GL_SwapWindow` once per rendered frame. The clone is linked with
`-Wl,--wrap=SDL_GL_SwapWindow`, so that call goes through `__wrap_SDL_GL_SwapWindow`, which times the
real call (KMSDRM: wait for the previous page flip + `eglSwapBuffers` + lock the front BO +
`drmModePageFlip`). Every 5 s it prints:

```
stk-drm flipstat <N> frames in <T> ms = <X.XX> fps (total <M>)
stk-drm swapstat t=<ms since first swap> fr=<N> swap_us_avg=<a> swap_us_max=<m>
```

The first line has the old winsys' `flipstat … (total …)` shape, so `scripts/flipstat-summary.sh`
reads it unchanged. It counts the same thing as `v3d-winsys: flipstat` (one per presented frame).
The second line separates present-path cost from render cost. `V3D_FLIPSTAT=0` / `V3D_FLIPSTAT_MS`
work as on the old lane. The same file also prints:

- a banner at start (`stk-drm: new GPU lane -- SDL 2.30.12 KMSDRM + Mesa 26.2 GBM/EGL (GLES) + libdrm-phoenix -> …`, `write(1)`);
- a `stk-drm: first swap <ms> after start: window WxH drawable WxH swap_interval N flipstat on` line;
- a `stk-drm: exit after N swaps …` line at `exit()`.

It sets SDL's VIDEO/INPUT log categories to DEBUG, as quakespasm-drm does (a dozen KMSDRM init
lines, nothing per frame).

**Why a link-time wrap and not an SDL edit:** it is gated to this clone by construction.
`libSDL2.a` and quakespasm-drm stay byte-identical, and it needs no `sdl2-drm/build.sh` run, which
would also invoke the currently dirty `mesa-drm/build.sh`. objdump proves the wrap sits in the path:
the only caller of `__wrap_SDL_GL_SwapWindow` is `irr::video::COGLES2Driver::endScene`, and the only
caller of the real `SDL_GL_SwapWindow` is the wrapper.

### SDL KMSDRM vs STK — what was checked (no SDL change needed)

| Item | Finding |
|---|---|
| `SDL_GetWindowWMInfo` | Irrlicht **returns from its device constructor** if this fails (`CIrrDeviceSDL.cpp:132`). KMSDRM implements it (`KMSDRM_GetWindowWMInfo`, needs version ≥ 2.0.15; STK passes 2.30.12). STK's objects were compiled against the old port's `SDL_config.h`, which has no `SDL_VIDEO_DRIVER_KMSDRM`, so their `SDL_SysWMinfo` union lacks the `kmsdrm` member. The union is padded to `dummy[64]`, so the size is identical, and KMSDRM's 16-byte write fits. This is the only public struct whose layout depends on the config. Every other public API is the same SDL 2.30.12. |
| `SDL_config.h` diff (old port vs KMSDRM build) | KMSDRM/EGL/GLES2 vs PHOENIX/OFFSCREEN video. No `SDL_JOYSTICK_VIRTUAL`: STK's `SDL_InitSubSystem(GAMECONTROLLER/HAPTIC/SENSOR)` failures are logged and non-fatal, and the dummy joystick/haptic/sensor drivers are present. No `SDL_USE_LIBICONV`: SDL's built-in iconv covers UTF-8/UCS. |
| GLES context version | ES 3.0 requested → Mesa returns ES 3.1 (`versionCorrect(3,0)` passes), the same as the old lane (`OpenGL ES 3.1 Mesa 26.2.0`, same Mesa version ⇒ the same `graphical_restrictions.xml` rules). |
| Mode | `--screensize=1920x1080 --fullscreen` → `SDL_WINDOW_FULLSCREEN` → the closest mode = the only one, 1920×1080. |
| Swap interval | STK tries `SDL_GL_SetSwapInterval(-1)` (adaptive). KMSDRM rejects it, and STK falls back to its configured value. With `DRM_CAP_ASYNC_PAGE_FLIP`=0, intervals 0 and 1 behave identically: every swap first waits for the previous flip. At ~12 fps (≈83 ms frames) that flip normally completed long before (the `swapstat` line shows it). |
| Multisample / alpha | STK sets no `SDL_GL_MULTISAMPLE*`. The surface is XRGB8888 (sdl2-drm patch 0008), so there is no console bleed-through. |
| Input | `--profile-laps=2` drives all karts by AI; no input is needed. Phoenix HID opens `/dev/kbd0` lazily and bounded (it may fail while the console holds it; harmless). |
| Y orientation | The old fork forced `Y_0_TOP` only for FBOs ≥ 1024×768 (the reason for 0.75 and not 0.5). On this lane the window-system framebuffer is a real EGL/GBM surface with upstream orientation logic. An upside-down 3D scene here would be a **finding**, not the known quirk. |
| Shader cache | The new-lane Mesa has no shader disk cache: STK compiles every shader at load, and lazy variants early in the race may compile then too. Load will be slower than a warm `stk-v3da`; gameplay fps is unaffected apart from the first windows. The old lane's `Mesa shader disk cache KEPT/cleared` arm assertion does not apply to this clone. |

### What STK exercises that quakespasm-drm / kmscube did not (read, not changed)

1. **`glFenceSync` / `glClientWaitSync` every frame** (`src/graphics/draw_calls.cpp`,
   `shader_based_renderer.cpp`; M1 §"What STK uses that quakespasm did not"). STK fences its
   instance-data draws and polls the fence (timeout 0, then 1 ms steps) before re-uploading. Here that
   path is Mesa's own `v3d_fence_finish` → `drmSyncobjCreate` + `drmSyncobjImportSyncFile` +
   `drmSyncobjWait` + `drmSyncobjDestroy` through **libdrm-phoenix**. On hardware those ioctls have so
   far been driven only by `drmprobe-m5b` and v3dv (vkcube). **No GL client on this stack has used
   them yet**, and STK can issue them up to ~90 times a frame while it polls. Failure shape
   (pre-registered): `stk-drm flipstat` stops advancing (or the first swap never comes) while
   `V3DA srv qstat` shows the GPU idle. That is a wait that never returns. Next step: a
   `DRMPHX_TRACE=1` re-run (`export DRMPHX_TRACE=1` as an extra psh line), not the first cycle.
2. **Many CL submits per frame, chained BCLs, TFU mipmap generation**: server-side, on the same
   `rpi4-v3d-async` (EINVAL fix included) that ran `stk-v3da` clean.
3. **BO churn at load** (uncompressed textures): one `CREATE_BO` round trip plus a server memset each
   (R17), the same as `stk-v3da`; slower load only.

### Verification (the delivered binaries)

| Check | Result |
|---|---|
| static | no `PT_INTERP`; `aarch64-phoenix-nm -u supertuxkart-drm` = **0** symbols; the launcher is also 0 |
| sizes | `supertuxkart-drm.stripped` **38 167 456 B** (shipped `usr/bin/supertuxkart` 38 836 320 B; `supertuxkart-v3da.stripped` 38 820 768 B); unstripped 123 835 136 B (for addr2line; `supertuxkart-drm.map`); `size`: text 37 192 750, data 969 144, bss 1 014 720; `stk-drm` 898 448 B (shipped `bin/stk` 898 456 B) |
| sha256 | `supertuxkart-drm.stripped` `d4642e25df7e94cd…`, `supertuxkart-drm` `567f12b542bc6579…`, `stk-drm` `0620ae41883190f6…` (`build-out/stk-drm/BUILD-INFO.txt`); inputs: libSDL2.a `4abf34e0…` (set `2f79883be1753ceb`), Mesa set `278cdef4539b4a27`, libdrm.a `a508e207…` (m5b), link.txt `a423262b…` |
| new stack, symbols | `KMSDRM_CreateDevice`, `KMSDRM_GLES_SwapWindow`, `KMSDRM_GetWindowWMInfo`, `SDL_EGL_LoadLibrary`, `SDL_PHOENIX_HID_Poll`, `gbmint_get_backend`, `kmsro_drm_screen_create`, `v3d_drm_screen_create_renderonly`, `eglGetPlatformDisplayEXT`, `eglGetProcAddress`, `_mesa_glapi_get_proc_address`, `drm_phoenix_ioctl`, `drmPhoenixMmap`, `__wrap_mmap`, **`__wrap_ioctl`** (m5b), `__wrap_SDL_GL_SwapWindow` |
| new stack, strings | `KMS/DRM Video Driver`, `/dev/dri/`, `libdrm-phoenix:`, `DRMPHX_TRACE`, `DRMPHX sync` (m5b), `EGL_KHR_platform_gbm`, `kmsro`, `/dev/kbd0`, `/dev/audio0`, `stk-drm: new GPU lane`, `stk-drm flipstat`, `stk-drm swapstat` |
| old lane absent | symbols `PHOENIX_bootstrap`, `PHOENIX_PumpEvents`, `PHOENIX_GL_*`, `phxgl_*`, `phoenix_v3d_ioctl`, `winsys_init`, `boPool_take`, `mboxProp`, `v3da_connect`, `v3d_phoenix_flip`: none. Strings `v3d-winsys:`, `v3da-winsys:`, `phxgl`, `PHOENIX: GL_CreateContext`, `/dev/fb0`, `RPI4FB_GETMODE`, `phoenix_v3d_ioctl`, `peek_next_scanout`, `v3d-srv`, `v3d-pool:`: all 0. Inverse control: the shipped binary has none of the new strings and does have `v3d-winsys: RT scanout` |
| call sites (objdump) | real `ioctl` called only from `__wrap_ioctl`, real `mmap` only from `__wrap_mmap` (3 sites); `COGLES2Driver::endScene` → `__wrap_SDL_GL_SwapWindow` → `SDL_GL_SwapWindow` (`call-sites.txt`) |
| silent duplicates | no global symbol is defined both by STK's own link inputs (objects + bundled/ports archives) and by the new stack's archives, apart from the `DW.ref.__gxx_personality_v0` comdat |
| guarded inputs | the shipped prog/bin/launcher, link.txt, glue objects, old libSDL2/libGL/libv3d, KMSDRM libSDL2.a, libgallium and m5b libdrm.a are unchanged by the run |

### Gaps

| Gap | Effect | Remedy |
|---|---|---|
| No shader disk cache in the new-lane Mesa | longer load (60–150 s predicted, vs ~60 s warm on the old lane); possible compile hitches in the first race windows | first/last windows are dropped anyway; a disk-cache backend for mesa-drm later |
| Present path waits for the previous flip before `eglSwapBuffers` (stock KMSDRM) | at most one vblank of GPU bubble for the final pass when the render outlasts the flip; small at 83 ms frames | measured by `swapstat`; if it matters: `SDL_VIDEO_DOUBLE_BUFFER` semantics / a 3-deep flip queue (later) |
| `GL_TIME_ELAPSED` / CPU-queue jobs | STK's GPU profiler only (off in these runs), as on the other lanes | — |
| Hardware cursor | `drmModeSetCursor` is a library stub → SDL logs a DEBUG/error line when STK hides the cursor; soft cursor | cursor plane in libdrm-phoenix (§3.2) |
| libdrm-phoenix and Mesa are embedded | a library fix needs a relink | `build-stk-drm.sh --libdrm-prefix …` |

### Pre-registered Pi cycle `stkdrm-1` (one netboot cycle)

**Question:** does an unmodified SuperTuxKart run on the full standard stack (SDL KMSDRM + Mesa
GBM/EGL/GLES + libdrm-phoenix → rpi4-kms + rpi4-v3d-async), render the race correctly on HDMI, and at
what fps compared with `stk-v3da` (12.12 fps, M1 STK A/B) and Raspberry Pi OS (11.7 fps, E2c; same
track, karts, settings)?

**Preconditions:** the same netboot image family as the M1 STK A/B (core_freq=500); no GPU app, X or
`rpi4-v3d` in the boot. **Single-owner rule:** never in the same boot as `stk`, `stk-v3da`, any
old-lane GPU app or X. `rpi4-v3d-async-m3p2` and `rpi4-kms-gate` (the deferred-flip wake fix;
kmscube 60.00 fps, vkcube 60.15) are already staged from earlier cycles, as are the STK data and assets
(`/usr/share/supertuxkart`, used by `stk`).

**Stage (coordinator)** (`sudo install -m 755 <source> <path>`, then `cmp`; `<export>` = the live
fsid=0 export, `awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports`):

| Source | Export path |
|---|---|
| `tools/gpu-lane/sdl2-drm/build-out/stk-drm/supertuxkart-drm.stripped` | `<export>/usr/bin/supertuxkart-drm` |
| `tools/gpu-lane/sdl2-drm/build-out/stk-drm/stk-drm` | `<export>/bin/stk-drm` |
| `tools/gpu-lane/v3d-async/out-m3p2/rpi4-v3d-async` | `<export>/bin/rpi4-v3d-async-m3p2` (if not already staged) |
| `tools/gpu-lane/kms/out-gate/rpi4-kms` | `<export>/bin/rpi4-kms-gate` (if not already staged) |

Check afterwards: `grep -a -c 'stk-drm: new GPU lane' <export>/usr/bin/supertuxkart-drm` = 1, and the
same grep on `<export>/usr/bin/supertuxkart` = 0. Keep the unstripped `supertuxkart-drm` on the host.

**One cycle.** ⚠ Wall clock: netboot 60–150 s + two server windows (~20 s each) + up to 440 s of game
+ 30 s is **more than the 600 s Bash-tool cap**. Run it detached (`setsid`, as the E2b/M1 STK queues
did) or from the coordinator's queue script, never under a foreground Bash `timeout`. A cycle the
harness kills is void.

```
./scripts/test-cycle-psh-interact.sh --label stkdrm-1 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 440 \
    --ready-line 'V3DA srv detached|KMS srv detached|profile: Number of frames' --ready-extra-secs 30 \
    --hdmi-dense-on 'stk-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/stk-drm --track=hacienda --numkarts=4 --profile-laps=2"
```

* `--idle-secs` is inert with a ready line (psh-interact ends a command at the ready line +
  `--ready-extra-secs`, or at `--max-cmd-secs`).
* In profile mode STK **exits** after the two laps and prints `profile: Number of frames …`. That
  closes the game window 30 s later (STK's teardown frees its textures and BOs after that line). By then the client has closed, so `rpi4-kms-gate` has printed its
  `KMS srv flipstat client=…` line and the clone its `stk-drm: exit after N swaps` line. If the race
  does not finish in 440 s, psh-interact's `max-cmd-secs … WITHOUT matching` message is expected and
  grading uses the windows present (≥ 10 required).
* No environment is set: KMSDRM is the only real video driver in this SDL; `V3D_FLIPSTAT` defaults on.

**Grade** (~1.3 % UART line corruption: re-read rather than count; EL0 dumps print twice):

```
grep -a -E '^(stk-drm|KMS |V3DA |DEBUG|ERROR|WARN|libdrm-phoenix|libEGL|MESA)|IrrDriver|GLDriver|profile:|Draw call returned' \
    artifacts/rpi4b-uart/rpi4b-uart-*-stkdrm-1.log
./scripts/flipstat-summary.sh --seq stkdrm-1
./scripts/uart-summary.sh stkdrm-1
```

**Metric** (the M1 STK A/B rule, unchanged): the `stk-drm flipstat … = X fps` lines over **gameplay
windows only**. Keep windows with fps > 3 in the contiguous race run, drop the first and last race
window, and drop any window that contains a wedge / `TIMEOUT` / reject line. Trial fps = the **mean
of the per-window fps**; ≥ 10 windows or the trial is void. Report the frame-weighted Σframes/Σms
beside it (not graded) and the mean `swap_us_avg` of the same windows. STK's own `profile: … Average
FPS` counts simulation frames, not rendered ones (the M1 run printed 70.2 at a real 12 fps). **Never
grade by it.**

**HDMI grading rule:** only snapshots **after** the `(psh)% /bin/stk-drm …` echo and the `stk-drm:
new GPU lane` line (dense ticks from the banner). Earlier ticks show the console during the server
commands.

**Predictions** and what each alternative means:

| Line / observation | Predicted | If instead… |
|---|---|---|
| `V3DA srv detached …`, `KMS srv detached …` | as in the qsdrm/kmscube cycles | a server missing: staging/boot, stop |
| `stk-drm: DATADIR=/usr/share/supertuxkart …`, then `stk-drm: new GPU lane -- SDL 2.30.12 KMSDRM …` | once each | `stk: DATADIR=` / no banner: the wrong launcher/engine staged (`cmp`) |
| KMSDRM `DEBUG: Opening device /dev/dri/card0`, `Opened DRM FD`, connector/encoder/CRTC `1 1 1`; `[IrrDriver Logger]: SDL Version 2.30.12` | at video init | `Unable to initialize SDL!` / `Could not initialize display!`: KMSDRM or EGL init failed; read the SDL `ERROR:` line and the `KMS`/`V3DA` lines, as the qsdrm table |
| `Using renderer: OpenGL ES 3.1 Mesa 26.2.0`, `OpenGL renderer: V3D 4.2.14.0`, vendor `Broadcom` | the same strings as the old-lane log (different git suffix) | a lower ES version / `kms_swrast`: the render node was not paired (kmsro); `Could not initialize display!` right after `SDL_GL_CreateContext`: no ES3-capable EGLConfig |
| `DEBUG: New DRM FB (n): 1920x1080 …` ×2–3 and `V3DA srv import … pages=2026 contiguous=1` per scan-out BO; possibly released and re-imported once on the fullscreen mode set (as quakespasm) | yes | `Failed to create scanout resource` / `CREATE_DUMB failed`: kms pool exhausted → re-run with `rpi4-kms-gate -G -p 48` |
| `stk-drm: first swap … window 1920x1080 drawable 1920x1080 swap_interval 0\|1 flipstat on` | one line, 30–150 s after start | 800×600-type size: the mode set did not resize the window (picture in a corner) |
| load until the race starts | 60–150 s after the first swap (cold shaders, NFS assets) | race not started within ~300 s: shader compile or asset load stall; read the last STK line |
| **gameplay fps** (`stk-drm flipstat`) | **11–13 fps = parity** with `stk-v3da` 12.12 and Pi OS 11.7. Same GPU server, same engine objects (control relink byte-identical), same workload; the difference is Mesa's own DRM winsys + GBM/EGL present instead of the v3da adapter + in-app FBO blit. `swapstat swap_us_avg` ≪ 5 ms | **< 10.5 with a clean gate:** a present-path cost. Read `swap_us_avg`: ≫ 5 ms means KMSDRM's wait-for-previous-flip is eating frame time; compare with `KMS srv flipstat` `vbl1/vbl2/q2a_us_avg` (gated flips that take 2 vblanks = the deferred-flip bug again) and the server `qstat busy` (GPU idle ⇒ present-bound). **> 14:** not a win until explained. The log must show `stk-drm: DATADIR=` (the seeded 0.75 RTT factor); check `rej=0`, 0 `Draw call returned`, and HDMI (frames actually rendered, full 3D scene) |
| `KMS srv flipstat client=… flips≈<total swaps> … deferred≈flips applied_gate≈deferred` | flips gated by the render fence (G13, `-G`), mostly `vbl1` | `deferred=0`: no fence attached to the imported BO (the flips are not render-gated; look for tearing) |
| `V3DA srv qstat` | `err=0 wedges=0 rej=0` | any wedge/err/rej: FAIL whatever the fps (the M1 gate) |
| `profile: Number of frames …` then `stk-drm: exit after N swaps …` | race finishes within 440 s at ~12 fps (M1: ~52 windows) | no `profile:` line and fps ≥ 10: the load took longer (count the windows); a fault: addr2line the PC first with the unstripped `build-out/stk-drm/supertuxkart-drm` |
| **HDMI** after the echo | the lit hacienda race, 4 karts, HUD and STK's own FPS counter (`stk-v3da` showed 9/13/15 min/avg/max), upright 3D scene, no console text through the picture, no tearing | upside-down 3D: an orientation difference on this lane (a finding: the window FB is a real EGL surface now); console bleed: alpha scan-out (patch 0008 absent); black with flipstat advancing: rendering lands elsewhere (compare the import `pa0` with the KMS pool); frozen: flips stopped (`Could not queue pageflip` / `Wait for previous pageflip failed`) |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | EL0 fault in supertuxkart-drm: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/sdl2-drm/build-out/stk-drm/supertuxkart-drm <pc>`. This is a different layout: never fold it into the C1 statistics, but record a C1 signature (`0x8000000x` high word), since no in-process winsys exists on this lane |

**What the cycle decides:** a lit race on HDMI with ≥ 10 gameplay windows and a clean gate means
**SuperTuxKart runs unmodified on the standard Linux-shaped graphics stack**, the stack Raspberry Pi
OS uses (SDL KMSDRM → Mesa GBM/EGL → DRM KMS + V3D). The fps band then says whether the stock
present path costs anything against the v3da clone and Pi OS. Next: 3 interleaved trials against
`stk-v3da` for a graded A/B (the M1 protocol), then an input cycle (`rpi4-kms-gate -G -C`, drive the
menu by keyboard).

## Result — `stkdrm-1` (queue32, 2026-09-27 10:27–10:35): **11.89 fps = Raspberry Pi OS parity on the full standard stack**; one EL0 fault at exit

`stk-drm` (SDL2 KMSDRM + Mesa GBM/EGL/GLES + libdrm-phoenix m5b → rpi4-kms-gate + rpi4-v3d-async):
**53 gameplay windows, mean 11.89 fps** (`stk-drm flipstat`, fps > 3, first/last dropped) — against
`stk-v3da` 12.12 (async server, in-process adapter) and Raspberry Pi OS 11.7 (E2c). The whole standard
graphics stack costs nothing measurable over the direct adapter. Loading ran at < 1 fps for a while (no
shader disk cache on the new-lane Mesa — every shader compiles cold).

⚠ **One `Data Abort (EL0)` at game exit** (after the race and its flipstat windows): `pc` =
`file_rawSeek` (`stdio/file.c:193`), `far=0xba`, called from `__fflush_one` ← `fflush` ←
`_atexit_finalize` (`stdlib/atexit.c:243`) — libc flushing all streams at exit found a corrupt `FILE`
in its list. n = 1. Old-lane STK on the same libphoenix (build 13 showcase gate) exited with 0 faults, so
this is specific to the new clone's shutdown path (SDL KMSDRM / Mesa-DRM teardown, or a stream closed
twice), not yet attributed. Next: re-run `stk-drm` with `C1_HEAP_TRACE_ALL`-style guards off and check
whether it reproduces; inspect SDL/Mesa `fclose`/`fdopen` users on exit.

## stk-drm exit fault — analysis (2026-09-27)

**Cause: SuperTuxKart's own `fclose(stdout)` + libstdc++'s exit-time `cout.flush()` = a use-after-free
in libphoenix, not a corrupt `FILE` in the exit list.** The note above ("libc flushing all streams at
exit found a corrupt `FILE` in its list", "specific to the new clone's shutdown path") was wrong on
both counts.

Evidence (all static; artifact `rpi4b-uart-20260927-102841-stkdrm-1.log`, line 617, binary
`tools/gpu-lane/sdl2-drm/build-out/stk-drm/supertuxkart-drm`):

1. **The call chain is not `fflush(NULL)`.** `addr2line` on the return addresses in the stack dump:
   `0x91b860` `fflush` (`file.c:1011`, the *single-stream* branch: `fflush` tests `cbz x0` first, and
   this address follows `bl __fflush_one` on the non-NULL side) ← `0x1a467b4`
   `__gnu_cxx::stdio_sync_filebuf<char>::sync()` ← `0x1a614e8` `basic_streambuf::pubsync()` ←
   `0x1a5de8c` `ostream::flush()` ← `0x1a3a13c` `std::ios_base::Init::~Init()` ← `0x91cdbc`
   `_atexit_finalize` (`atexit.c:243`, the line after a destructor call). This is `std::cout`'s flush
   in libstdc++'s exit-time destructor. It flushes the `FILE *` that `stdout` held **at startup**,
   cached in `buf_cout_sync`.
2. **STK closes that stream itself.** `src/main.cpp:2635-2636`, at the end of `main()`:
   `fclose(stderr); fclose(stdout);` (`#ifndef ANDROID`). libphoenix's `fclose()` → `file_release()` →
   `free(stream)`: the standard streams were ordinary heap `FILE`s. Checked in the binaries, not only
   the source: `main()` loads `stderr` then `stdout` and calls `fclose` on each, in the stk-drm ELF
   (`0x41f7e8`/`0x41f7f4`) and in the old-lane `prog/supertuxkart` (`0x41f7c8`/`0x41f7d4`). So when `~Init()` runs, the cached
   `stdout` points at freed memory, which the rest of the exit has recycled.
3. **The registers fit a recycled block.** `x0 = x19 = 0x20d0` is the stream (user VA starts at 0 on
   aarch64 and `_file_init()` makes the first allocations, so a low heap address is consistent with the
   startup `stdout` — "consistent with", not proven). `file_rawSeek`: `ldr w3,[x0,#4]` read `flags` with
   bit 5 (`F_OPS`, added by the `open_memstream`/`fmemopen` work, 7cc5628) set and `F_WRITING` clear;
   `ldr x3,[x0,#72]` read the `ops` pointer **past the end** of the 72-byte `FILE` → `0xaa`;
   `ldr x3,[x3,#16]` (`ops->seek`) → `far = 0xaa + 0x10 = 0xba`. `x1 = 8` is `bufpos - bufeof` of the
   stale words.
4. **Corroboration: the missing exit line.** `stkdrm_exit()` (registered at the first swap, so it runs
   *before* `~Init()`) writes `stk-drm: exit after N swaps …` to fd 2. It is absent from the log because
   `fclose(stderr)` had already closed fd 2.
5. **The atexit rewrite is exonerated.** `exit()` is `__cxa_finalize(NULL); fflush(NULL); _exit()`
   before and after the upstream merge (b1a37b4); `~Init()` has always been a `__cxa_atexit`
   destructor. The final `fflush(NULL)` is innocent too: `fclose()` had already taken `stdout` off the
   list.

**Why the old lane "exits clean": luck of heap layout.** Same `main.cpp`, same libstdc++, same bug. With
bit 5 clear in the stale `flags` word, `__fflush_one` does an `lseek`/`write` on a stale descriptor
(`write` from the buffer `fclose` had already `munmap`ed) and then `stream->flags |= F_ERROR` — a
**silent write into freed heap memory** on every STK exit. Before 7cc5628 bit 5 meant nothing, so it
could never fault. Hypothesis only, not tested: this exit-time write is a candidate contributor to the
allocator "heap guards fire silently" item. It runs at exit, so it cannot explain mid-game corruption.

The suggested sweep of exit-time stdio users in SDL KMSDRM, Mesa-DRM, libdrm-phoenix and the hooks
was not done. The chain above is fully resolved (libstdc++ `~Init()` → the cached startup `stdout` →
freed by STK's own `fclose`), so nothing in the new stack is implicated.

**Fix (libphoenix `bf35aaf`, merged 2026-09-27 in build 14):** `_file_init()`
records the three stream objects it creates. `file_release()` (reached from `fclose()` and from a failed
`freopen()`) **empties** one of those instead of freeing it, as glibc and musl do: buffer freed and
NULL (so `fflush` is a no-op), `fd = -1` (I/O fails with EBADF), off the list (a second `fclose` fails
with EBADF). The lock stays, because `fflush` takes it. A stream the program assigns to `stdout` itself
(e.g. `stdin = fdopen(0, "r")` in `libc/stdio/file.c`) is freed as before. Known follow-up, out of
scope: `freopen()` on an already-closed standard stream reopens it unbuffered and off the list.

**Tests:**

| check | before (master 156422a) | after (bf35aaf) |
|---|---|---|
| `tools/libstdio-hosttest` `make stdclose` (file.c + ASan: `fclose(stdout)` → `fflush`/`fileno`/write/2nd `fclose` through the saved pointer; failed `freopen(stdin)`; `fflush(NULL)`) | **heap-use-after-free** in `fflush` (`file.c:1010`), freed by `fclose` → `file_release` | **11/11 pass** |
| `phoenix-rtos-tests` `stdlib_exit.closed_std_streams` (branch `fix/stdstream-fclose-uaf`, c4b23a8), run on the host against libphoenix's `file.c` | FAIL: child killed in the atexit flush (no sanitizer); ASan: heap-use-after-free | **PASS** (same with ASan) |
| same test on glibc (the reference) | — | PASS |
| `make run` differential (file/memstream/fmemopen) | digest `957daf050379ec27`, 0 diffs | **identical** digest, 0 diffs |
| `make unity` (stdio_memstream + stdio_fmemopen) | — | 17/17 |
| target flags `-Werror` syntax check: `stdio/file.c`, `libc/exit/exit.c`, `stkdrm_hooks.c` | — | clean |

**tools (uncommitted):** `stkdrm/stkdrm_hooks.c` keeps a private close-on-exec copy of fd 2
(`fcntl(2, F_DUPFD_CLOEXEC, 3)`) for everything `out()` prints, so the exit line survives STK's
`fclose(stderr)`. `tools/libstdio-hosttest`: new `stdclose.c` + `make stdclose` target, README line.
No STK source patch is needed. Skipping the two `fclose`s on Phoenix would hide the bug for STK only.

**PRE-REGISTERED Pi check (`stkdrm-2`)**, after merging `fix/stdstream-fclose-uaf` into libphoenix
master, building `--scope core`, and re-running `build-stk-drm.sh` with the modified hooks. Gate: the new
`BUILD-INFO.txt` `libphoenix.a` hash ≠ `77c4dbf8…`, and `stkdrm_hooks.c` hash ≠ `8e40681f…`. One
`stkdrm` cycle, same recipe as `stkdrm-1`:

- (a) **0 `Exception #` lines** from race start through exit, and the `(psh)%` prompt returns;
- (b) the line **`stk-drm: exit after <N> swaps in <M> ms since the first swap`** is present, printed
  after the profile block. Its absence with (a) true would mean the hooks fix, not the libc fix, is
  wrong;
- (c) `test-libc-exit` on target: `stdlib_exit.closed_std_streams` **PASS**, with every other
  `unistd_exit`/`unistd_Exit`/`stdlib_exit` case unchanged from its pre-change verdict.
- Refutation: a fault at exit whose chain still runs through `~Init()` → `fflush` means the
  standard-stream objects are still being freed somewhere (check the relink really took the new
  `libphoenix.a`). A fault with a *different* chain is a second, independent defect.

The old-lane `prog/supertuxkart` is statically linked and makes the same two `fclose` calls. It keeps
writing freed heap at every exit until it is relinked (ports rebuild) against the fixed libphoenix. The
check above covers only stk-drm.

n = 1 exit per cycle, and the pre-fix fault depends on heap layout (the old lane never showed it), so
one clean exit is weak evidence alone. The deterministic evidence is (c) and the host checks above.

**RESULT `stkdrm-2` (build 14, queue35, 2026-09-27 11:57): PASS on all three.** Gate: `BUILD-INFO.txt`
`libphoenix.a e69b216a…` (≠ `77c4dbf8…`), `stkdrm_hooks.c 97112c2e…` (≠ `8e40681f…`). Log
`artifacts/rpi4b-uart/rpi4b-uart-20260927-115705-stkdrm-2.log`: (a) **0 `Exception #`**, prompt back;
(b) `stk-drm: exit after 3259 swaps in 339302 ms since the first swap`; (c) `b14-exit`:
`TEST(stdlib_exit, closed_std_streams) PASS`, `32 Tests 0 Failures 4 Ignored` (exit), `112 Tests 0
Failures 1 Ignored` (stdio). Rate: **11.87 fps** mean over 51 windows (stkdrm-1: 11.89). Merged to libphoenix
master `bf35aaf`, tests `c4b23a8`; the build-14 ports rebuild relinked the old-lane `supertuxkart` against it
too (showcase gate 6/6, 0 faults). Manifest `manifests/2026-09-27-build14-stdstream-uaf.md`.
