# E7 — building the DRM-shaped userspace stack for Phoenix (libdrm, Mesa GBM/EGL, first look at Xorg)

**Question** (research §4.6, §6 E7/E8): what does it take to build upstream libdrm and a Mesa with
the DRM path (v3d + vc4 + kmsro, loader, GBM dri backend, EGL surfaceless + drm/gbm, v3dv) for
Phoenix, statically? Where does a Phoenix backend plug into libdrm? Static megadriver or `dlopen`?
And (E8) what blocks Xorg `hw/xfree86` + modesetting + glamor + DRI3/Present?

**Method:** host-side builds only, in a private directory (`/home/houp/.claude/jobs/c8f1289c/tmp/e7/`):
a meson cross file for `aarch64-phoenix` (the project had none), upstream libdrm 2.4.134-16
(`external/libdrm`, cloned for this experiment, MIT), and a `git clone -s` of our Mesa fork
(26.2.0 + 22 Phoenix commits) so the fork itself was not touched. Read against the real sysroot
(`.buildroot/_build/aarch64a72-generic-rpi4b/sysroot`), port libs from `.buildroot/_build/.../lib`.
No Pi cycle, no shared build output written. Every `[inferred]` is a reading, not a build result.

## 1. Result in one table

| Step | Outcome |
|---|---|
| meson cross file for Phoenix | works; one wart: the Phoenix gcc rejects `-pthread`, which meson's generic `dependency('threads')` adds for an unknown OS (1060 of 1066 first-round Mesa errors). Worked around with a 3-line compiler wrapper that drops it. |
| **libdrm core, static** | **builds** (`libdrm.a`, 520 KB) with **one** header alias (`sys/ioccom.h` → `<sys/ioctl.h>`) and **one** missing libc symbol (`open_memstream`). A static test program calling `drmGetDevices2`, `drmGetVersion`, `drmModeGetResources`, atomic, `AddFB2`, PRIME, syncobj, `drmHandleEvent` **links**. |
| Mesa configure, unpatched fork | **fails**: `meson.build:553: Feature gbm cannot be enabled: GBM only supports DRM/KMS platforms` — `'phoenix'` is not in `system_has_kms_drm` (`meson.build:162`). |
| Mesa configure + 1-line patch | **configures**: gallium v3d+vc4, kmsro, EGL `builtin:egl_dri2` platforms `surfaceless drm`, GBM (dri backend), Vulkan broadcom with WSI `surfaceless drm`. expat 2.5.0 + zlib found from ports; libudev, libelf, zstd, libunwind absent (all optional). |
| **Mesa full build** (1224 steps, `-j4`) | **every C/C++ object compiles** after a 15-line Mesa OS patch + 6 simulated libphoenix header/symbol fixes (§3.2). The **shared** targets cannot link: `libgallium-26.2.0.so`, `libgbm.so` and `libvulkan_broadcom.so` **fail** because `libphoenix` is not PIC (`relocation against 'environ' … recompile with -fPIC`, from `libm.a(env.o)`). `libEGL`, `dri_gbm.so` and `libGLESv2` were never attempted, since they depend on `libgallium`. (The fork's host tool `v3d_shader_dump` also fails; that is irrelevant here.) |
| **kmscube-class static link** (GBM + EGL on GBM + GLES2 + KMS page flip) | **links** (`kmscube-lite`, 19.0 MB text) from the build's archives + the objects of the shared targets, with two stubs: `open_memstream` and the old-lane hook `v3d_phoenix_peek_next_scanout` (see §3.3). Not run — no DRM server exists yet. |
| Xorg 21.1.24 `hw/xfree86` configure | reaches dependency resolution; stops at missing ports: `libxcvt` (hard), then `epoxy`, `gbm` (ours, once installed), `xshmfence` (DRI3). No OS-level blocker in configure itself. |

## 2. libdrm

### 2.1 What was needed to build

```
-Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled -Dvmwgfx=disabled
-Domap=disabled -Dexynos=disabled -Dtegra=disabled -Dvc4=enabled -Detnaviv=disabled
-Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled -Dudev=false -Dtests=false
```

libdrm has no v3d library; `vc4` installs only headers. Mesa uses core libdrm + its own
`include/drm-uapi` copies. Probes against the real sysroot: `clock_gettime`, `dlsym`, Intel
(`__sync`) atomics, `sys/sysmacros.h major/minor/makedev` YES; `open_memstream`, `secure_getenv`,
`sys/sysctl.h` NO.

* `include/drm/drm.h:47` — every non-Linux OS takes the BSD branch `#include <sys/ioccom.h>`.
  Phoenix has no such header, but its `_IO/_IOR/_IOW/_IOWR` (`<sys/ioctl.h>`, `phoenix/ioctl.h`)
  **are the BSD layout**: `IOCPARM_MASK 0x1fff` (13-bit size), `IOC_OUT 0x40000000`, `IOC_IN
  0x80000000`. Differences from BSD: `IOC_VOID` is 0 and `0x20000000` means `IOC_NESTED`. A
  one-line alias header was enough. **Encoding hazard:** the old lane's
  `mesa/shim-include/sys/ioccom.h` uses the *Linux* layout (READ=2/WRITE=1 at bit 30, 14-bit
  size). Both are fine as long as every server keys on `_IOC_NR`/the low 16 bits (identical in both
  layouts), which `libv3d-client.c:303,318` already does. But libphoenix's `ioctl()` copies
  `IOCPARM_LEN(request)` bytes to the server (research §4.1), so a **raw** `ioctl()` built with
  the Linux-layout shim sends the wrong length. The new lane must use the Phoenix layout
  everywhere. Ship the alias inside the libdrm port's own include dir, or better, patch `drm.h`
  with `#elif defined(__phoenix__) #include <sys/ioctl.h>`. **Never** put it into the shared
  sysroot (header-poisoning rule).
* `xf86drm.c:289,395` — `open_memstream`, used only to print ARM AFBC/AFRC and AMD
  format-modifier *names*. Both callers return NULL on failure, so a stub is harmless, but it is a
  POSIX.1-2008 gap in libphoenix that Mesa also hits (`util/memstream.c:67`). Implement it there.
* Everything else libdrm calls exists in libphoenix. Its undefined list is plain libc:
  `ioctl`, `open`, `stat/fstat`, `mmap/munmap`, `opendir/readdir`, `major/minor/makedev`, `mknod`,
  `chown`, `sysconf`, …. **Core libdrm uses no `pthread_*` and no `poll`.** Events are a plain
  `read()` in `drmHandleEvent` (`xf86drmMode.c:~1027`), and `poll()` is the caller's business.

### 2.2 What compiles but is wrong at runtime (the OS dependencies)

Phoenix `fstat()` reports `st_rdev = oid.port` (`kernel/posix/posix.c:1735`,
`libphoenix/sys/stat.c:48`), and `major()`/`minor()` are glibc-style bit-unpacking of that port
number (`libphoenix/posix/stubs.c:145-215`). libdrm identifies DRM devices by char-device
major/minor everywhere, so on Phoenix **every identity function fails** even though it links:

| Site (xf86drm.c) | Phoenix behaviour |
|---|---|
| `drmNodeIsDRM` :3305 (`#else` → `maj == DRM_MAJOR`, 226) | false for every Phoenix fd, so everything below fails |
| `drmGetNodeTypeFromFd` :3329, `drmGetMinorType` :1012 (probes `access("/dev/dri/card<minor>")`) | `-EINVAL`/`-ENODEV` |
| `drmGetMinorNameForFD` :3393 (non-Linux branch builds the name from `minor(st_rdev)`) → `drmGetRenderDeviceNameFromFd` / `…PrimaryDeviceName…` | NULL (used by EGL `egl_dri2.c:2713`, `platform_device.c:241`) |
| `process_device` :4506 → `drmGetDevices2` :4824, `drmGetDeviceFromDevId` :4626, `drmGetDevice2` :4778 | `readdir("/dev/dri")` + `stat` + the checks above → **0 devices** |
| `drmParseSubsystemType` :3602 and `drmParse{Pci,Usb,OF,Faux}{Bus,Device}Info` :3758, :3970, :4199, :4230, :4304, :4364, :4470 | `#warning "Missing implementation"` → `-EINVAL`; there is no sysfs |
| `drmOpenDevice` :824 (legacy `drmOpen*` only) | `mknod`/`chown` of `/dev/dri/*`, which is meaningless on Phoenix. Only Xorg's PCI `drmOpen(NULL, BusID)` path uses it, and we disable that path. |
| `S_ISCHR(st_mode)` checks (:3340, process_device, :4787) | OK **iff** the server answers `mtGetAttr atMode` with `S_IFCHR`. `video/rpi4-fb/rpi4-fb.c:183` already does; **`gpu/rpi4-v3d` currently answers no `mtGetAttr` at all** (grep), so this is an M3 server work item |
| `DRM_DIR_NAME "/dev/dri"` (`xf86drm.h:65`) | E1 found that `_resolve_abspath` stats every path component, so devfs must know `dri` (E1 §"Namespace caveat") |

Who consumes this: Mesa's loader, kmsro and v3dv all find the render node through
`drmGetDevices2` (`loader.c:249`, `pipe_loader_drm.c:366` →
`loader_open_render_node_platform_devices`, `v3dv_device.c:1747`). They require
`bustype == DRM_BUS_PLATFORM`, a node with `DRM_NODE_RENDER`, and `drmGetVersion()->name == "v3d"`
(kmsro's list, `pipe_loader_drm.c:394`). v3dv also needs a separate platform device with only a
primary node for display (`vc4`). **The OF `compatible` strings are not matched by any path we
use.** `drmDevicesEqual`/`drmGetDevice2` compare devices in the Vulkan WSI
(`wsi_common_drm.c:507-532`).

### 2.3 The proposed seam — two files plus a few `#elif` branches

1. **`drmIoctl()` (xf86drm.c:672) is the single choke point.** All 52 KMS entry points in
   `xf86drmMode.c` and every `drm*` helper go through it, and so do Mesa's v3d/v3dv drivers
   (`v3d_ioctl` → `drmIoctl`). Patch: `#ifdef __phoenix__ return drm_phoenix_ioctl(fd, request,
   arg); #endif` in its body. `drm_phoenix_ioctl` lives in a **new `xf86drm_phoenix.c`**: a table
   keyed by `DRM_IOCTL_NR(request)` (plus the driver-private range per node type). Flat structs go
   through as `mtDevCtl`. Nested-pointer ioctls (GETRESOURCES, GETCONNECTOR, GETPLANE*,
   OBJ_GETPROPERTIES, GETPROPERTY, GETPROPBLOB, CREATEPROPBLOB, ATOMIC, SYNCOBJ_WAIT/RESET/SIGNAL/
   TIMELINE_*, V3D SUBMIT_CL/TFU/CSD/CPU + extension chains, PERFMON_*, **and VERSION**, which
   carries name/date/desc buffers) get a flattener into `i.data`/`o.data`. **Fd-producing ioctls**
   (PRIME_HANDLE_TO_FD, SYNCOBJ_HANDLE_TO_FD, SYNCOBJ_EXPORT_SYNC_FILE) are completed client-side:
   the server returns a name and libdrm `open()`s it (E1's decision). Their inverses send the
   fd's identity (`fstat` → port/ino, or a server-query ioctl on the fd) [inferred].
2. **Device identity**: `#elif defined(__phoenix__)` branches in `drmNodeIsDRM`,
   `drmGetMinorType`, `drmGetMinorNameForFD`, `drmParseSubsystemType` (→ `DRM_BUS_PLATFORM`),
   `drmParseOFBusInfo`/`drmParseOFDeviceInfo`, and `drmGetDeviceNameFromFd[2]`. Each calls one
   helper in `xf86drm_phoenix.c` that asks the node itself: a private
   `DRM_IOCTL_PHOENIX_NODE_INFO` (node type, minor index, driver name, OF fullname/compatible), or
   VERSION + GET_UNIQUE. `drmGetDevices2` then keeps its `readdir("/dev/dri")` loop unchanged.
   **Server-side requirement** [inferred]: give each node (card0, card1, renderD128) its **own
   port**, so `st_rdev` is unique and stable per node. Then `drm_device_has_rdev`,
   `drmGetDeviceFromDevId` and `VK_EXT_physical_device_drm` major/minor stay meaningful without
   further patches, and `mtGetAttr atMode` must return `S_IFCHR`.
3. **`drmOpen*`/`drmOpenDevice`**: return `-ENODEV` on Phoenix (no `mknod`). No consumer in our
   stack needs them.
4. `drm.h`: the `__phoenix__` include branch (3 lines). `meson.build`: add `xf86drm_phoenix.c`
   when `host_machine.system() == 'phoenix'` (≈5 lines).

For the link, `drm_phoenix_ioctl` can be a stub returning `-ENOSYS`. The size is the marshalling:
research §4.6 estimates 3–5 k lines [inferred]; the identity part is ≈120 lines of `#elif` in
`xf86drm.c` + ≈250 in the new file [inferred]. **Precedent:** Mesa's own `system_has_kms_drm` list
already contains `managarm`, a microkernel OS whose DRM drivers are userspace servers, and Mesa
carries `__managarm__` exclusions in exactly the places we hit (`u_thread.c:179`). Their libdrm
port is worth reading before writing ours [inferred: not examined here].

## 3. Mesa with the DRM path

### 3.1 Configuration used vs the current Phoenix port

| | current port (old lane) | E7 DRM build |
|---|---|---|
| how | **host** (x86) `meson setup` for `compile_commands.json`, then `build-{v3d,gl,v3dv}-phoenix.py` re-issue each compile with the Phoenix gcc + curated source lists (`v3d-core-sources.txt`, aux lists) + a link-drive loop | real **meson cross** build: probes run against the Phoenix sysroot, and meson builds and links the targets |
| options | `-Dgallium-drivers=v3d -Dvulkan-drivers= -Dplatforms= -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dspirv-tools=disabled -Dvideo-codecs=` | `-Dgallium-drivers=v3d,vc4 -Dvulkan-drivers=broadcom -Dplatforms= -Dgbm=enabled -Degl=enabled -Dglx=disabled -Dllvm=disabled -Dspirv-tools=disabled -Dvideo-codecs= -Dshader-cache=disabled -Dxmlconfig=disabled` |
| libdrm | `shim-include/xf86drm.h` (inline `drmIoctl` → `phoenix_v3d_ioctl`), `drm.h`, `xf86drmMode.h`, `libsync.h`, `dlfcn.h`, Linux-layout `sys/ioccom.h`; syncobj/PRIME stubbed (`v3d_libdrm_shim.c`, `v3dv_libdrm_shim.c`) | real libdrm (§2) |
| libc gaps | `phoenix_mesa_compat.h` force-included (math, `static_assert`, `SCNxPTR`, `CPU_*`, `_SC_PHYS_PAGES`, **stubbed `pthread_barrier_*`**), `gl_stubs.c` | §3.2: fix in libphoenix; the patched build uses Mesa's own mutex+condvar barrier |
| probe answers | host-native: every probe returns the x86 Linux answer (mirror risk `HAVE_MEMFD_CREATE` etc., memory note "silent cross-probe failures" class 4) | real Phoenix answers (memfd, `dl_iterate_phdr`, `qsort_r`, `secure_getenv`, `sched_getaffinity`, … correctly NO), with one false YES (below) |

`-Dplatforms` has no `drm`/`surfaceless` values. Both come implicitly with `-Degl=enabled
-Dgbm=enabled` (EGL summary: "Platforms: surfaceless drm"). `-Dshared-glapi` is deprecated and a
no-op in 26.2 (`meson.options:329`). glapi is always inside `libgallium`.

### 3.2 What broke, and the fix for each

| Failure (first seen) | Cause | Fix / size |
|---|---|---|
| `meson.build:553` GBM refuses | `'phoenix'` not in `system_has_kms_drm` (`meson.build:162`) | +1 word |
| 1060 × `unrecognized command-line option '-pthread'` | meson adds `-pthread` for gcc on an unknown OS, and the Phoenix gcc driver has no `%{pthread:}` spec | toolchain spec (accept and ignore `-pthread`) — benefits every meson/autotools port; E7 used a wrapper |
| 657+193 × `implicit declaration of 'static_assert'` (+ ~1000 follow-on parse errors) | libphoenix `<assert.h>` lacks the C11 `static_assert` macro | libphoenix, 3 lines |
| 596 × `unknown type name 'pthread_barrier_t'` | libphoenix has no barriers; `util/u_thread.h:119` uses them for any `HAVE_PTHREAD` except Apple/Haiku | Mesa: add `!defined(__phoenix__)` (u_thread.h:119, u_thread.c:199), which selects Mesa's mutex+condvar barrier. **Correct**, unlike the old port's stubbed barriers, which never block [inferred from `phoenix_mesa_compat.h:137` "impls stubbed"]. Alternative: implement POSIX barriers in libphoenix. |
| `os_time.c:52 #error Unsupported OS`, `os_misc.c:85 #error unexpected platform` | `util/detect_os.h` does not know `__phoenix__`, so `DETECT_OS_POSIX` is 0 | Mesa: `DETECT_OS_PHOENIX` + `DETECT_OS_POSIX` (8 lines) and `os_misc.c:63` (+1 term) |
| `u_thread.c:183 pthread_getcpuclockid` | absent in libphoenix | Mesa: `!defined(__phoenix__)` beside the existing `__managarm__` (1 line) |
| `nir_opt_varyings.c:3191 spurious trailing '%'` (`-Werror=format`) | `SCNxPTR`/`SCNuPTR` missing from libphoenix `<inttypes.h>` | libphoenix, 2 lines |
| `fossilize_db.c:264 LOCK_UN undeclared` | libphoenix declares `flock()` in `<sys/file.h>` but the `LOCK_*` constants only in `<fcntl.h>` | libphoenix, 1 line |
| `os_misc.c:363 _SC_PHYS_PAGES` | not in libphoenix `sysconf` | libphoenix (the kernel reports memory; the old compat header faked it) |
| `memstream.c:67`, `xf86drm.c:289` `open_memstream` | POSIX.1-2008 gap | libphoenix (~80 lines on a growing buffer) |
| `wsi_common_display.c:2112 pthread_setcanceltype` | only `setcancelstate` exists | libphoenix (the VK_KHR_display hotplug thread uses async cancel, so a stub is **not** correct there [inferred]) |
| `xmlconfig.c:1197 scandir` | absent | libphoenix, or `-Dxmlconfig=disabled` (driconf hard-coded). expat itself is available. |
| `v3d_disk_cache.c:56` `build_id_*` → needs `dl_iterate_phdr` | absent | `-Dshader-cache=disabled` for now; the old lane stubs `disk_cache_*` (memory: "Mesa V3D shader disk cache") |
| `blake3_neon.c:341 no previous prototype` | **our fork's** `blake3_impl.h` hunk forces NEON off on `__phoenix__`, but meson still compiles `blake3_neon.c` on aarch64 | drop the fork hunk in the new lane (the NEON path is correct when it is compiled in) |
| **false YES**: `Checking for function "posix_memalign": YES` | it is a gcc **builtin**, so meson's `__has_builtin` probe passes. libphoenix has no `posix_memalign` (implicit declaration in a direct test). | libphoenix must implement it. Until then the fork's `#undef HAVE_POSIX_MEMALIGN` (`os_memory_aligned.h`) is load-bearing: a clean upstream tree would fail to link. |
| shared-library link failures (3 attempted, 3 dependants skipped) | `-shared` pulls non-PIC `libphoenix.a` into the `.so` | none. Static is the only model (§3.4). |

The E7 Mesa OS patch is **13 added lines** over the fork (`meson.build`, `detect_os.h`, `u_thread.[ch]`,
`os_misc.c`), plus reverting the fork's blake3 hunk (`git diff --stat`: 15 insertions, 14 deletions in total); saved as
`/home/houp/.claude/jobs/c8f1289c/tmp/e7/e7-mesa-os.patch`. The libphoenix gaps were simulated
with `#include_next` wrapper headers (`compat-libc/`), not a force-included header. A first
attempt with `-include compat.h` pulled `<time.h>` into every meson probe and flipped
`clock_gettime` to **NO** (`ERROR: C shared or static library 'rt' not found`). This is class 1 of
the memory note "silent cross-probe failures", reproduced live.

### 3.3 The old lane's `__phoenix__` hooks leak into a DRM build

The fork guards its in-process-winsys changes with `__phoenix__`, which the compiler always
defines. In a DRM build they activate and are wrong:

* `v3dv_device.c:~1735` bypasses `drmGetDevices2` and creates the physical device on a **fake fd**
  with `display_fd = -1`: no KMS, no WSI display.
* `v3d_bufmgr.c:~600` and `v3dv_bo.c:298-304` treat `DRM_IOCTL_V3D_MMAP_BO`'s `offset` as an
  **already-mapped CPU VA** instead of `mmap(fd, offset)`.
* `v3d_resource.c:863,935` calls `v3d_phoenix_peek_next_scanout()` from the winsys; this was the
  only undefined symbol left in the kmscube link apart from `open_memstream`.
* `blake3_impl.h` (above), `os_memory_aligned.h`, `st_atom_framebuffer.c` (Y-flip heuristic),
  `v3dv_queue.c` (forced `is_shim`), `v3d_bufmgr.[ch]` daemon-handle import.

So `mesa-drm` must **not** build from the old-lane branch as is. Either (a) a separate branch
`phoenix-drm` = `mesa-26.2.0` + the genuine driver fixes (NPOT mip decline, u_vbuf draw-drop +
NULL check, TFU/zero-tile guards, EZ-off if still needed) + the §3.2 OS patch, or (b) rename the
old-lane guards to `PHOENIX_INPROC_WINSYS` and have the old lane's `build-*-phoenix.py` define it.
(b) touches the old lane's build and needs its gate. (a) does not, so **recommend (a) for M3**,
converging at migration when the old lane is deleted.

### 3.4 Static megadriver vs `dlopen` — decided by three facts

1. **TLS.** GL dispatch and EGL current-thread state are `initial-exec` TLS:
   `_mesa_glapi_tls_Dispatch`/`_Context` (`glapi/glapi.h:72-73`, `shared-glapi/core.c:151-153`),
   `_egl_TLS` (`egl/main/eglcurrent.c:41`), plus `util/u_thread.h:52-57` users. libphoenix `dl.c`
   handles only RELATIVE/GLOB_DAT/JUMP_SLOT/ABS64 and **no TLS relocations**, the known Phase A
   limit. Linked statically, initial-exec relaxes to local-exec, which Phoenix supports. **So
   `libgallium` cannot be a Phase A plugin.**
2. **No PIC libc.** The `.so` links fail outright (§3.2). A plugin must leave libc undefined and
   resolve it against the host `.symtab`: possible (the T-DYNLINK recipe) but not what Mesa's meson
   produces, and it brings the gc-sections-prunes-the-API trap with it.
3. **Mesa 26 barely uses `dlopen` any more.** libEGL, libGLESv2 and `dri_gbm` link `libgallium`
   **directly** (`egl/meson.build:12`, `gbm/backends/dri/meson.build:21`,
   `targets/dri/meson.build:70`), and the pipe loader is `pipe_loader_static`. The only `dlopen`
   left on our path is **GBM's backend loader**: `gbm/main/backend.c:110`
   `loader_open_driver_lib("dri")` → `loader.c:898 dlopen` → `dlsym(GBM_GET_BACKEND_PROC_NAME)`.
   The link succeeds, but at runtime `gbm_create_device()` would fail.

**Recommendation: static megadriver.** Patches:

* `gbm/main/backend.c`: under a `GBM_BUILTIN_DRI_BACKEND` define, call the linked
  `gbmint_get_backend()` (`gbm_dri.c:1269`) instead of `load_backend_by_name`. ≈20 lines.
* meson: build `libgallium`, `libEGL`, `libgbm`, `dri_gbm`, `libGLESv2`/`libGLESv1_CM` and
  `libvulkan_broadcom` as `static_library` when `host_machine.system() == 'phoenix'`, keeping
  `link_whole: libdri` (`targets/dri/meson.build:57`). ≈40–80 lines across 6 meson files
  [inferred]. E7 did the equivalent by hand (`link-kmscube.sh`: the targets' `.p/*.o` + the
  archives in one `--start-group`, `libdri.a` whole-archive). It links, so no symbol conflicts
  exist between EGL, GBM, the dri frontend and the loader.
* A static binary carries **both** v3d and vc4 plus the GLSL compiler: 19.0 MB text for
  kmscube-lite with `--gc-sections`. Acceptable (STK/Quake binaries are already this class).

### 3.5 Remaining OS surfaces Mesa's DRM path touches (build-clean, runtime design items)

| Item | Where | Phoenix answer |
|---|---|---|
| raw `ioctl()` bypassing libdrm | `vc4_drm_winsys.c:47` (`DRM_IOCTL_VC4_GET_PARAM`, decides "no 3D → kmsro"); `util/libsync.h:160,176` (`SYNC_IOC_MERGE`, `SYNC_IOC_FILE_INFO`); `util/os_drm.h:27` (used only by drivers we do not build) | flat structs, so they reach the fd's server via libphoenix `ioctl()` → `mtDevCtl` unchanged. `rpi4-kms` must answer (reject) `VC4_GET_PARAM`; sync_file fds need a server that answers MERGE, and merging a v3d fence with a kms fence is a cross-server problem [inferred] |
| BO CPU mapping `mmap(drm_fd, offset)` | upstream `v3d_bufmgr.c:518`, `v3dv_bo.c:305`, `vc4_bufmgr.c:646`, `gbm/backends/dri/gbm_driint.h:153` (dumb map) | E1 decided per-buffer fds (`open("/gpubuf/N")` → `mmap(fd)`), so one `mmap` per BO on the *node* fd does not map distinct buffers. Options [inferred]: (i) the node's oid is an **arena window** and MMAP_BO/MAP_DUMB hand out offsets into it (zero Mesa patches, but one memory type per window, and E1 enforces memtype per window); (ii) patch these 4 sites to call a libdrm-phoenix `drmPhoenixMapBo()` that opens the per-BO name (≈40 lines). Decide in M3 with E1's data. |
| sync_file / dma-buf sync | `wsi_common_drm.c:54,76` (`DMA_BUF_IOCTL_EXPORT/IMPORT_SYNC_FILE` on the dmabuf fd), `v3d_fence.c:159`, `dri2.c:179` (`sync_accumulate`), `vk_drm_syncobj.c` | the dmabuf fd's server (the BO exporter) must answer the two DMA_BUF ioctls; `poll()` on a sync_file fd needs `atPollStatus` in the fence server |
| `memfd` | `util/anon_file.c:41-95` | falls back to `mkostemp` in `$XDG_RUNTIME_DIR` (must be set); only for `os_memory_fd`/Wayland |
| `eventfd`, inotify | `util/os_file_notify.c` | compiled; not on the GBM/EGL/KMS path |
| libudev | not required (EGL/GBM/loader/kmsro all use libdrm enumeration) | — |
| threads | `c11/threads` → pthread (`thrd_create` NO, correctly) | works; barriers via Mesa fallback |

### 3.6 Patch-set estimate to get kmscube-class code **linking** statically

| Piece | Size | Measured? |
|---|---|---|
| Mesa OS patch (meson + detect_os + u_thread + os_misc) | 13 lines (+ 1 fork hunk reverted) | **measured** |
| Mesa static-library meson + GBM builtin backend | 60–100 lines | [inferred] |
| Mesa: drop/gate old-lane `__phoenix__` hooks (branch choice §3.3) | branch work, ≈0 new lines | — |
| libphoenix: `static_assert`, `SCNxPTR/SCNuPTR`, `LOCK_*` in `sys/file.h`, `_SC_PHYS_PAGES`, `open_memstream`, `posix_memalign`, `pthread_setcanceltype` (+ optional `scandir`, POSIX barriers, `dl_iterate_phdr`) | ≈300–400 lines + host-harness tests | [inferred] |
| toolchain: accept `-pthread` | 1 spec line | [inferred] |
| libdrm: `drm.h` branch + `xf86drm.c` `#elif`s + meson + stub backend | ≈150 lines | the link is **measured**; the lines are [inferred] |
| meson cross-file generator for the ports framework | ≈40 lines | the cross file itself is **measured** |

**Linking** kmscube-class code is ≈1 day of patch work on top of what E7 proved. **Running** it is
the libdrm-phoenix backend (3–5 k lines of marshalling) plus the servers (M1/M2); that is where
the effort is, not the build.

## 4. E8 first look — Xorg `hw/xfree86` + modesetting + glamor + DRI3/Present

Read from the xorg-server 21.1.24 tarball the kdrive port already ships
(`sources/phoenix-rtos-ports/xorg_server/`), plus one meson configure attempt.

* **os-support needs no `phoenix/` directory for a first build.** For an unknown
  `host_machine.system()`, `hw/xfree86/os-support/meson.build`'s `else` branch selects the
  `stub/` + `shared/*_noop.c` set (no VT switching, no APM, no AGP). A real `phoenix/` dir would
  later hold only console handover with `rpi4-kms`/fbcon (≈100–200 lines, modelled on the 220-line
  `hurd/`) [inferred].
* **No libpciaccess, no udev needed.** With `-Dpciaccess=false -Dudev=false -Dudev_kms=false`,
  `XSERVER_LIBPCIACCESS` and `XSERVER_PLATFORM_BUS` are both off (`include/meson.build:326-327`).
  modesetting then uses its legacy `Probe` → `open_hw()` (`drivers/modesetting/driver.c:223`):
  `Option "kmsdev"` (:138), `$KMSDEVICE`, or `/dev/dri/card0`. `check_outputs()` needs only
  `drmModeGetResources` and `drmGetCap(PRIME)`.
* **Blocker 1 — module loading.** Every DDX, input driver, `glamoregl`, `fb`, `shadow`, `exa` is a
  `shared_module` (`hw/xfree86/*/meson.build`), loaded by `loader/loader.c:106`
  (`dlopen(…, RTLD_LAZY|RTLD_GLOBAL)`), `:122` (`dlsym(RTLD_DEFAULT, …)`) and `:127`
  (`dlopen(NULL)`). Modules also resolve **each other's** symbols through `RTLD_GLOBAL`
  (modesetting → glamor_egl). libphoenix `dl.c` resolves a plugin only against itself and the host
  `.symtab`: no inter-plugin resolution, no `RTLD_DEFAULT`, no TLS. **Fix: a built-in module table**
  in `loader/loadmod.c` (look up `<name>ModuleData` in a static array before `dlopen`, ≈150 lines)
  + meson to build the needed modules as static libs linked into `Xorg` [inferred].
* **Blocker 2 — `libxcvt`**: a hard dependency (`meson.build:196`), no port. Small MIT meson
  library (≈300 lines), trivial port [inferred].
* **Blocker 3 — epoxy.** `glamor/meson.build:41` requires `epoxy`, with EGL for `glamor_egl`.
  Upstream libepoxy resolves GL/EGL through `dlopen`/`dlsym` of `libEGL.so`/`libGL.so`, which is
  wrong for a static binary [inferred: libepoxy source not in the tree]. The project's `tools/x11-port/glamor-shim/` (3 helpers, GL only) is the
  precedent: extend it with the EGL/GBM helpers `glamor_egl.c` uses (≈100–200 lines), or patch
  libepoxy to bind statically [inferred].
* **Blocker 4 — DRI3 needs `xshmfence`** (`meson.build:96`, `:427`); so does Mesa's X11 DRI3
  client. xshmfence's non-futex backend puts a `PTHREAD_PROCESS_SHARED` mutex+cond in shared
  memory [inferred: xshmfence source not in the tree]. libphoenix's `pthread_mutex_t` is a **kernel handle** (`sys/types.h:55-58`,
  `handle_t mutexh`), meaningless in another process. So it cannot work as is. It needs a Phoenix
  xshmfence backend over a cross-process primitive (a fence server, or a futex-like syscall) and
  SCM_RIGHTS fd passing on the X socket (E1 notes stream-socket fdpass as unverified).
  Present-only (no DRI3) can come first [inferred].
* **Input**: no `xf86-input-*` for Phoenix. Port the `/dev/kbd0`/`/dev/mouse0` + `hid_evdev_map.h`
  code from the kdrive DDX (`files/ddx/fbdev.c`, 1020 lines total) into a small xf86 input driver
  (≈400–600 lines). XKB: no `xkbcomp` port. Reuse the kdrive port's compiled-in keymap
  `files/ddx/ddxLoad.c`.
* Also needed: `-Ddefault_font_path=…` (no `fontutil.pc`), `-Dsha1=libmd` (as kdrive), GBM from
  `mesa-drm`, `drmCrtcQueueSequence`/vblank events from `rpi4-kms` (modesetting's `vblank.c`).
* **Comparison**: the kdrive `Xphoenix` port has none of these problems because it hand-links one
  static DDX from the core archives, with no loader, no GBM and no DRI. Xorg-with-modesetting is a
  different binary (`Xorg-newlane`) built **alongside**, per the ground rules.

**E8 size** [inferred]: loader built-in table ≈150, input driver ≈500, epoxy EGL shim ≈150,
libxcvt port ≈50 (recipe), xshmfence Phoenix backend ≈300 + a kernel/server primitive, meson glue
≈100, os-support/console ≈150. That is ≈1.5 k lines plus the xshmfence primitive: **~1–2 weeks**
once M3's libdrm-phoenix + GBM exist. Without DRI3 (glamor + Present flips only) it drops to ≈1 week.

## 5. Recommended M3 build approach

1. **`sources/phoenix-rtos-ports/libdrm-phoenix/`** — framework port, upstream libdrm tarball +
   patches (§2.3), meson cross. **Install into its own subprefix**
   (`$PREFIX/libdrm-phoenix/{include,lib,lib/pkgconfig}`), not `$PREFIX/include`. The old lane's
   Mesa compiles with its shim `xf86drm.h`/`drm.h` first on the include path. A real `xf86drm.h`
   in the shared prefix would make which one wins order-dependent (header-poisoning rule).
2. **A shared meson cross file generated by the ports framework** (`phoenix-rtos-build`, from the
   same CFLAGS/SYSROOT_OPTS as `target/aarch64.mk` + `setup-sysroot.mk`). E7's
   `phoenix-aarch64.cross` + `pkg-config-phoenix` wrapper are the template. xserver, libxcvt,
   libepoxy and Weston all need it.
3. **`sources/phoenix-rtos-ports/mesa-drm/`** — builds from a **`phoenix-drm` branch** of the Mesa
   fork (26.2.0 + genuine fixes + §3.2 OS patch + static-library/GBM-builtin patch), with
   `PKG_CONFIG_LIBDIR` pointing at libdrm-phoenix's subprefix. Outputs static archives into its own
   subprefix (`libEGL.a`, `libgbm.a`, `libGLESv2.a`, `libgallium.a`, `libvulkan_broadcom.a`) and
   **never** writes `tools/.gpu-libs` or `/tmp/mesa-v3d-build`. The old lane keeps building exactly
   as today.
4. **libphoenix first**: land the §3.2 gaps as real implementations with host-harness tests (owner
   rule), plus the `-pthread` spec. That removes every compat header E7 needed, and gives the old
   lane correct barriers/`posix_memalign`/`open_memstream` for free.
5. First client: a `kmscube`-class probe under `tools/gpu-lane/` (E7's `kmscube-lite.c` is the
   skeleton). It gates M3 once libdrm-phoenix's backend and `rpi4-kms`/`rpi4-v3d` answer the calls.

## 6. Artefacts

* **Recipe files (small, kept in the coordination repo, uncommitted):**
  `tools/gpu-lane/e7-drm-build/`: cross files, `pkg-config-phoenix`, `bin/phx-gcc` / `phx-g++`
  (drop `-pthread`), `compat/` (ioccom alias, stubs, and the *abandoned* force-include header
  `phoenix-e7-mesa-compat.h`, kept as the record of the probe-flipping approach), `compat-libc/`
  (simulated libphoenix fixes), `e7-mesa-os.patch`, `linktest-libdrm.c`, `kmscube-lite.c`,
  `link-kmscube.sh`. The cross files and scripts still hold absolute paths into the scratch dir
  below; a port must generate them.
* `external/libdrm` — upstream clone (gitignored, `external/`).
* **Large trees and binaries stay in the scratch dir only**
  (`/home/houp/.claude/jobs/c8f1289c/tmp/e7/`, per-job, may be cleaned): `mesa-src/` (shared clone
  of the fork, patched), `mesa-build/` (full cross build), `libdrm-build/`, `prefix/lib/libdrm.a`,
  `linktest-libdrm` and `kmscube-lite` (aarch64 static, 19.0 MB text), `mesa-ninja-{1..4}.log`,
  `link-kmscube-{1,2}.log`, `xorg-server-21.1.24/`, `xserver-build/`.
