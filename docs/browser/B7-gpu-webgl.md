# B7: GPU compositing, dma-buf frames and WebGL for the WPE browser

Browser milestone B7 ([PLAN](PLAN.md)): put the GPU to work for the shipped WPE WebKit 2.54
browser (port `webkit_wpe`), and measure it against the software mode.

Status 2026-10-02: **design and code done, no Pi cycle yet.**
- The code is on phoenix-rtos-ports branch `webkit-wpe-b7`, not merged.
- The Pi gate is pre-registered in [§7](#7-the-pi-gate-pre-registered).

Line numbers refer to the patched tree the image build compiles,
`.buildroot/_build/aarch64a72-generic-rpi4b/webkit_wpe-build/src/webkit` (patches 0001-0015), and to the
port files on master `bbf9883`.

## 0. Summary

| Part | Status today | What blocks it | Built on `webkit-wpe-b7` | Rebuild cost |
|---|---|---|---|---|
| (a) GPU Skia raster (Ganesh on GLES 3.1) | **already in the binary**: `BROWSER_GPU=1`, i.e. no `--cpu-rendering`. **Never run on the Pi**: the pre-registered B4 #4 was not run in b24 | nothing at build time. Each GPU-rastered frame is still read back for SHM | nothing new needed; gate runs B and C | none |
| (b) dma-buf frames WebProcess → UI → labwc | off: `USE_GBM=OFF` means the UI process only ever asks for SHM frames | one condition in WebKit (`WebProcessPoolGLib.cpp:198-208`), plus Phoenix's process-local sync files | **Route T**: WebKit patch 0016 (opt-in `--dmabuf`), launcher, checks | two unified sources + the launcher + a relink (minutes) |
| (c) WebGL (ANGLE on GLES 3.1) | not built (`ENABLE_WEBGL=OFF`) | ANGLE does not know Phoenix: platform, TLS key type, futex, no `RTLD_NOLOAD`, libstdc++ without `wchar_t`, and a `dlopen("libEGL.so.1")` | patch 0017 (ANGLE on Phoenix); USE flag `webgl` (default off); `--webgl` | **full WebKit rebuild + 305 ANGLE objects** (~2 h at -j8, no ccache) |

## 1. The frame path today (build 29, `--cpu-rendering`, labwc)

### 1.1 Where the WebProcess's display comes from

- **EGL is mandatory.** `WebProcess::initializePlatformDisplayIfNeeded()` (`Source/WebKit/WebProcess/glib/WebProcessGLib.cpp:142-186`):
  - tries GBM, but only `#if USE(GBM)`, and only when the UI asked for `Hardware` transport;
  - then `PlatformDisplaySurfaceless::create()` (`:172`);
  - otherwise `CRASH()` (`:184-185`).

  On the Pi this is Mesa surfaceless on `/dev/dri/renderD128` (`rpi4-v3d-async`).
- **CPU raster.** `WEBKIT_SKIA_ENABLE_CPU_RENDERING=1` (`:210-213`) turns off `ProcessCapabilities::canUseAcceleratedBuffers()`. That moves Skia's tile painting to CPU threads only (`Source/WebCore/platform/graphics/skia/SkiaPaintingEngine.cpp:68-71`).
  - The tiles are uploaded and composited by TextureMapper in GLES either way.
  - With GPU raster, Skia Ganesh paints on two GPU worker threads by default (`:318-334`). Each has its own GL context (`PlatformDisplaySkia.cpp:330-352`, `GrDirectContexts::MakeGL`).

### 1.2 Which transport the UI process allows

`WebProcessPool::platformInitializeWebProcess()` (`Source/WebKit/UIProcess/glib/WebProcessPoolGLib.cpp:192-208`), WPEPlatform branch:

```
#if USE(GBM)
        if (!parameters.drmDevice.isNull())
            parameters.rendererBufferTransportMode.add(RendererBufferTransportMode::Hardware);
#endif
        parameters.rendererBufferTransportMode.add(RendererBufferTransportMode::SharedMemory);
```

`USE_GBM=OFF`, so the WebProcess is only ever offered **SharedMemory**.

### 1.3 The swap chain picks SHM

`AcceleratedSurface::SwapChain::SwapChain()` (`Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp:590-647`):
- a Surfaceless display gets `Type::Texture` only if `EGL_MESA_image_dma_buf_export` is present **and** the transport has `Hardware` (`:603-608`);
- otherwise `Type::SharedMemory`, i.e. `RenderTargetSHMImage` (`:761-775`);
- at most 4 targets per swap chain (`AcceleratedSurface.h:406`).

### 1.4 Per frame, three CPU copies of the whole frame

1280x960 is 4.9 MB per frame (B5's 1024x768 is 3 MiB: the `PHXSHM alloc … size=3145728` lines).

| # | Where | What | File:line |
|---|---|---|---|
| 0 | WebProcess, compositing thread | TextureMapper composites the layers into the target's FBO (an RGBA8 renderbuffer) | `AcceleratedSurface.cpp:178-193`, `:211-223` |
| 1 | WebProcess | **GPU → CPU readback** into a `ShareableBitmap` (shmsrv memfd). `SkSurface::readPixels` under the Skia GL context, or `glReadPixels(GL_BGRA)` | `AcceleratedSurface.cpp:440-457` |
| — | WebProcess | no fence goes with the frame: `RenderTargetSHMImage::supportsExplicitSync()` is false, so `sync()` does `clientWait()` | `AcceleratedSurface.h:320`, `.cpp:254-262`, `:1154-1155` |
| — | IPC | `DidCreateSHMBuffer` (once per target, a read-only handle), then `Frame(id, damage, fd)` per frame | `AcceleratedSurface.cpp:435`, `:225-228` |
| — | UI process | `AcceleratedBackingStore::didCreateSHMBuffer` maps it and wraps it as `wpe_buffer_shm_new(…ARGB8888…)` | `Source/WebKit/UIProcess/wpe/AcceleratedBackingStore.cpp:158-176` |
| 2 | UI process | `wpe_view_render_buffer` → WPEPlatform Wayland: **`wlPool->write()` copies the whole buffer** into a wl_shm pool (another shmsrv object), every frame | `Source/WebKit/WPEPlatform/wpe/wayland/WPEViewWayland.cpp:336-377` (`:348`, `:356`) |
| 3 | labwc (wlroots, GLES2 renderer) | uploads the wl_shm buffer into a texture (`glTexSubImage2D`), then composites | wlroots `render/gles2` |

- **Copy 1 is the expensive one on Phoenix.** It reads the V3D's render target through the CPU: tiled-to-linear on the GPU side, then the CPU reads it from uncached or write-combined memory.
- **Copies 2 and 3** are plain cached-memory copies of 4.9 MB.
- **Nothing measures these copies today.** The gate's runs A/B against C/D separate them out (§7).

### 1.5 Whether the WPEPlatform Wayland display could take dma-bufs today

It can. `WPEDisplayWayland` binds `zwp_linux_dmabuf_v1` up to v4 (`WPEDisplayWayland.cpp:290-291`). labwc offers v4 (wlroots, GLES2 renderer). For a `WPEBufferDMABuf`, `createWaylandBufferFromDMABuf` (`WPEViewWayland.cpp:297-334`) sends fd, offset, stride and modifier through `zwp_linux_buffer_params_v1_create_immed`. **The UI process needs no EGL for that.**

Three things are off without libdrm:
- the dma-buf feedback listener (`WPEDisplayWayland.cpp:467-475`, `#if USE(LIBDRM)`). With a v4 bind the compositor sends no format/modifier events either, so `wpe_display_get_preferred_buffer_formats()` is an empty list;
- `drmMainDevice()` (`Source/WebKit/UIProcess/glib/DRMMainDevice.cpp:188-247`, all of it `USE(GBM)`);
- `WPEBufferDMABuf`'s `import_to_pixels` (`WPEBufferDMABuf.cpp:216-295`, `USE(GBM)`), which **snapshots** of dma-buf frames need.

> Correction to [tools/browser/wpe/README.md §GPU](../../tools/browser/wpe/README.md#gpu-egl-is-not-optional):
> "`--mesa-variant wayland` … enables the dma-buf path. That is B7 work" is not right.
> - The dma-buf path needs no EGL in the UI process, so the `gles` variant suffices.
> - The `wayland` variant would only add the `eglCreateWaylandBufferFromImageWL` fallback (`WPEViewWayland.cpp:265-295`, for a compositor without linux-dmabuf) and the EGL device query (`WPEDisplayWayland.cpp:395-426`).
> - The port links the `gles` variant: it depends on bare `mesa_drm` (`port.def.sh:69`) and passes no `--mesa-variant`.

## 2. Build configuration now (`webkit-build/CMakeCache.txt`, build 29)

| Option | Value | Note |
|---|---|---|
| `ENABLE_WEBGL` | OFF | `USE_ANGLE` follows it (`Source/cmake/OptionsWPE.cmake:459`); `cmakeconfig.h:141` `ENABLE_WEBGL 0`, `:195` `USE_ANGLE 0`. PRIVATE option: never in the "public options ON" line of `webkit-features.txt` |
| `USE_GBM`, `USE_LIBDRM` | OFF, OFF | `cmakeconfig.h:203,213`. `USE_GBM` depends on `USE_LIBDRM` (`OptionsWPE.cmake:165`) |
| `ENABLE_GPU_PROCESS` | OFF | depends on `USE_GBM` (`:163`) |
| `ENABLE_WPE_PLATFORM_DRM` | OFF | depends on `USE_GBM` (`:164`) |
| `USE_SKIA` | ON (private, the WPE default) | Ganesh GL backend compiled; `-DSK_ASSUME_GL_ES=1` on every compile line |
| `USE_LIBEPOXY`, `HAVE_GL_FENCE` | TRUE (hard-wired, `:449`, `:461`) | epoxy resolves through Mesa's `eglGetProcAddress` (`libepoxy` port) |
| `ENABLE_WEBXR`, `ENABLE_WEBGPU`, `USE_VULKAN` | OFF | WebGPU stays out (PLAN decision 5) |
| `ENABLE_OFFSCREEN_CANVAS(_IN_WORKERS)` | ON | 2D only without WebGL |
| `ENABLE_RELEASE_LOG` | OFF | hence the `WTFLogAlways` markers of patch 0016 |

Launcher settings (`files/launcher/wpe-browser.cpp`, master): `enable-webgl` FALSE and `enable-2d-canvas-acceleration` FALSE. `hardware-acceleration-policy` is WebKit's default, ALWAYS (`WebKitSettings.cpp:1599-1605`).

## 3. (a) GPU Skia raster

- **Status: built, not gated.** `/bin/browser` with `BROWSER_GPU=1` (or `wpe-browser` without `--cpu-rendering`) runs Ganesh on V3D 4.2. GLES 3.1, the robustness extensions, `GL_EXT_texture_format_BGRA8888` and `GL_OES_EGL_image` are all there (m3p3b/m8a extension lists).
- **What could go wrong is all at run time:**
  - shader compile time on the first pages (Mesa's disk cache is in, but the owner has ruled out shipping a cache);
  - MSAA sample count (`initializeMSAASampleCount`; V3D has 4x);
  - two GPU painting threads plus the compositor contending in one render-server client;
  - glyph atlas size.
- **No build work is needed.** The gate measures it twice: run B (GPU raster, still SHM readback) and run C (GPU raster with dma-buf).
- **Knobs if B misbehaves**, all WebKit's own environment variables:
  - `WEBKIT_SKIA_GPU_PAINTING_THREADS=1`;
  - `WEBKIT_SKIA_ENABLE_CPU_RENDERING=1`, which is the A/B.
- **Later, not in this step:** `enable-2d-canvas-acceleration` (canvas 2D on Ganesh) is a one-line launcher setting once B passes.
- **Estimate:** 0 build days; one Pi run.

## 4. (b) dma-buf frames: Route T (built) and Route G (contingency)

### 4.1 What Phoenix already has

Survey of the GPU-lane docs; each item is Pi-verified unless marked.

| Need | Phoenix | Evidence |
|---|---|---|
| `EGL_MESA_image_dma_buf_export` on the WebProcess's surfaceless display | yes. Mesa sets it from `DRM_PRIME_CAP_EXPORT` in the generic dri2 display setup (`mesa-26.2.0/src/egl/drivers/dri2/egl_dri2.c:715-717`). The render node answers `DRM_CAP_PRIME`=3 (Mesa patch 0008) | m3p3b / m8a EGL extension lists; [M3](../gpu-new-lane/M3-libdrm-phoenix.md) |
| A texture BO exportable as a dma-buf | yes. `drmPrimeHandleToFD` on the render node opens `/v3dbuf/<handle>` (G4). The server refuses only imported, scanout or cacheable BOs, and libdrm-phoenix makes only uncached ones | [M6 §15.1](../gpu-new-lane/M6-wayland.md) (lines 786-803), drmprobe 42/0 |
| The fd to another process | yes. SCM_RIGHTS carries the `/v3dbuf` fd; the receiver imports it. The fd in flight keeps the BO | M6 `prime_export_xproc`; kernel fd-per-message fix (B4) |
| labwc imports client dma-bufs | yes. zwp_linux_dmabuf_v1 v4, GLES2 renderer on V3D; `weston-simple-egl` and `quakespasm-wl` composite from dma-buf | [M8](../gpu-new-lane/M8-windowed-games.md), m8a log |
| UIF modifier (`0x0700000000000006`, what V3D renders by default) accepted for composition | expected. wlroots' GLES2 renderer imports through `EGL_EXT_image_dma_buf_import_modifiers`, and V3D samples UIF. M8's Mesa wayland clients sent UIF buffers. **Not proven for an ABGR8888 texture export** | M6 §16 row 10 (UIF client buffers) |
| Cross-process ordering | implicit. The render server treats every use of a shared BO as a write, so a later submit that names it waits for pending work (G6). labwc samples after the WebProcess's render job | [G6](../gpu-new-lane/G6-cross-process-sync.md) (g6-sync2 PASS) |
| A native fence fd across processes | **no.** Sync files are emulated per process (a dup + a table entry). Passing one is G6b and `poll()` on one is G15, both open | [M3](../gpu-new-lane/M3-libdrm-phoenix.md) §sync, G6 lines 139-140 |
| Direct scan-out of the browser's buffers | no. card0 imports LINEAR only, below 1 GiB. Not needed: labwc composites | M6 §16-18 |

### 4.2 Route T: texture export, no GBM (implemented, opt-in)

Everything below is already compiled into build 29, except the one condition in §1.2:
- `RenderTargetTexture::create()` (`AcceleratedSurface.cpp:459-500`): a GL texture, `eglCreateImage(EGL_GL_TEXTURE_2D)`, then `eglExportDMABUFImageQueryMESA` / `eglExportDMABUFImageMESA`;
- the `DidCreateDMABufBuffer` message, which is unconditional (`Source/WebKit/UIProcess/glib/AcceleratedBackingStore.messages.in`);
- `AcceleratedBackingStore::didCreateDMABufBuffer` → `wpe_buffer_dma_buf_new` (`AcceleratedBackingStore.cpp:144-156`);
- `createWaylandBufferFromDMABuf` → `zwp_linux_buffer_params_v1_create_immed` (`WPEViewWayland.cpp:297-334`).

**WebKit patch 0016** (`patches/webkit/0016-wpe-phoenix-dmabuf-transport.patch`, two files, `OS(PHOENIX)` only):
1. **The opt-in.** `WebProcessPoolGLib.cpp`: with `OS(PHOENIX) && !USE(GBM)` and `WPE_PHOENIX_DMABUF=1`, add `RendererBufferTransportMode::Hardware`. The WebProcess keeps its surfaceless display (the GBM branch of `initializePlatformDisplayIfNeeded` is compiled out), and the swap chain picks `Type::Texture` (`:603-605`).
2. **No explicit sync.** `AcceleratedSurface.cpp` `useExplicitSync()`: false on Phoenix unless `WPE_PHOENIX_EXPLICIT_SYNC=1`.
   - Mesa advertises `EGL_ANDROID_native_fence_sync`, so without this `RenderTargetTexture` (`supportsExplicitSync()` true) would export a native fence fd per frame (`:244-262`).
   - The UI process's `FenceMonitor` would then `poll()` that fd (`Source/WebKit/UIProcess/glib/FenceMonitor.cpp:113-129`, because labwc offers no `zwp_linux_explicit_synchronization_v1`). That fd is process-local on Phoenix, so the result could be a stall or a garbage wait.
   - With this change, `sync()` does `clientWait()`, and G6 orders labwc's composite after the producer.
3. **Markers**, because Release builds have no RELEASE_LOG:
   - `WPEB-WEBKIT swap-chain pid= type=texture-dmabuf|shm display=surfaceless dmabuf-export=0|1 transport-hardware=0|1 explicit-sync=0|1`, once per swap chain (and `type=shm display=none hardware-acceleration=0` when the policy is NEVER);
   - `WPEB-WEBKIT dmabuf-export pid= fourcc=AB24 modifier=0x… planes= stride= size=WxH`, for the first exported buffer.

   Without them, an fps number could come from a silent SHM fallback.

**Launcher** (`files/launcher/wpe-browser.cpp`):
- `--dmabuf` (`WPE_BROWSER_DMABUF=1`) sets `WPE_PHOENIX_DMABUF=1` before the first web process starts. It does so **only in window mode, and only when the display offers linux-dmabuf** (`wpe_display_get_preferred_buffer_formats()` non-NULL). Otherwise it prints `gpu dmabuf-refused reason=headless|no-linux-dmabuf`.
  - Headless is refused because snapshots of dma-buf frames need GBM (§1.5). The B4 checksum gate therefore keeps the SHM path.
- `gpu dmabuf-formats n= abgr8888=` logs what the compositor advertised: empty with a v4 bind without libdrm, see §1.5. The gate is safe without libdrm: WPEDisplayWayland returns a (possibly empty) format list whenever it bound linux-dmabuf (`WPEDisplayWayland.cpp:557-568`), and `wpe_buffer_formats_builder_new` takes a NULL device (`WPEBufferFormats.cpp:247-258`).
- `--cpu-rendering --dmabuf` is a valid pair: the texture swap chain needs a GL surface (`usesGL()`, `AcceleratedSurface.h:122` = composited or hardware acceleration on, WebKit's default policy ALWAYS). With the policy NEVER the swap chain is SHM before it looks at the transport (`AcceleratedSurface.cpp:594-597`).
- `gpu raster=cpu|gpu transport=shm|dmabuf webgl=on|off|unbuilt`, printed at every start.
- `--present-stats=S` (`WPE_BROWSER_PRESENT_SECS`) prints `present frames= fps= total= buffer=dma-buf|shm size=WxH` every S s.
  - It counts `WPEView::buffer-rendered`: the frames the view really handed to the compositor.
  - `buffer=` is the type of the WPEBuffer, i.e. the transport, seen from the UI process.

`/bin/browser`: `BROWSER_DMABUF=1` (and `BROWSER_WEBGL=1`) pass the options; the default is unchanged.

- **Rebuild cost:** patch 0016 touches `UnifiedSource-UIProcess-42.cpp` and `UnifiedSource-WebProcess-40.cpp`; then the launcher and a relink. Minutes. The framework applies a new patch to an already-patched work directory, and `rsync -c` hands ninja only the two changed files.
  - Both unified sources and the launcher were compiled here with the build's exact flags, into scratch: 0 errors, 0 new warnings.
- **Estimate:** done in code. One Pi run (§7 C/D) decides it.

**Route T's risks:**
1. **labwc rejects the buffer.** `create_immed` with a format/modifier it cannot import is a **fatal protocol error**: the UI process's Wayland connection dies, and the run shows `display disconnected`. The likeliest cause is the UIF modifier on an ABGR8888 texture. The fix is then Route G, or forcing LINEAR textures for render targets: a Mesa `v3d` resource flag, or a WebKit patch making the texture with `GL_EXT_EGL_image_storage` from a LINEAR import.
2. `eglExportDMABUFImageMESA` fails on a texture. WebKit logs `eglExportDMABUFImageMESA failed`, there is no fallback, and the window stays black.
3. Without explicit sync the WebProcess waits for each frame's GPU work on its compositing thread. That is the same serialisation as the SHM path's readback, minus the copies.

### 4.3 Route G: GBM (the contingency, upstream-shaped)

`USE_LIBDRM=ON` + `USE_GBM=ON` would give:
- format/modifier negotiation from the compositor's feedback: `RenderTargetEGLImage` with `gbm_bo_create_with_modifiers2` (`AcceleratedSurface.cpp:269-330`). LINEAR could then be asked for, and scan-out becomes possible;
- `import_to_pixels` for dma-buf snapshots (headless works);
- WebGL through `GraphicsContextGLTextureMapperGBM` (dma-buf WebGL layers);
- the GPU process option.

Missing pieces on Phoenix:
- **The main device.** `drmMainDevice()` takes it from `wpe_display_get_drm_device()`. WPEDisplayWayland gets that from the linux-dmabuf v4 feedback `main_device` (a `dev_t`) through `drmGetDeviceFromDevId` (`WPEDisplayWayland.cpp:347-361`).
  - libdrm-phoenix answers `drmGetDeviceFromDevId` from a static list (v3d = card1 + renderD128; `st_rdev` = the server port, M3:173-183).
  - Whether labwc's `dev_t` maps back is untested. The fallbacks are the EGL device query (needs the `wayland` Mesa variant in the UI process) or `wpeDRMDeviceCreateForDevice(nullptr)`.
- **`gbm_create_device(renderD128)` has never run on the Pi.** Every GBM user so far opens card0 through kmsro; the pieces each work (fstat `S_ISCHR`, CAP_PRIME, G4 export).
- **The build.** The deps prefix needs libdrm-phoenix's headers + `libdrm.pc` and Mesa's `gbm.h` + `gbm.pc`. Both archives are already in the link (`link-gles.txt`).
- `DMA_BUF_IOCTL_SYNC` returns ENOTTY (libdrm-phoenix). WebKit uses it for CPU-mapped dma-bufs (`MemoryMappedGPUBuffer`, the Skia DMABuf atlas, `SkiaPaintingEngine::shouldUseDMABufAtlasTextures`); those need to stay off or get the ioctl.

- **Cost:** a cmakeconfig.h change, i.e. a full WebKit rebuild. Do it in the same rebuild as `webgl` if Route T fails.
- **Estimate:** 1-2 days of code, plus 1 full build and 2-3 Pi runs.

## 5. (c) WebGL: ANGLE on GLES 3.1

### 5.1 How WebGL renders in this configuration

- `createWebProcessGraphicsContextGL()` (`Source/WebCore/platform/graphics/texmap/GraphicsContextGLTextureMapperANGLE.cpp:159-180`): without GBM, `GraphicsContextGLTextureMapperANGLE`, entirely in the WebProcess.
- **The display.** ANGLE's EGL display is `EGL_PLATFORM_ANGLE_ANGLE` with type OPENGLES, device type EGL, and native platform `EGL_PLATFORM_SURFACELESS_MESA` (`PlatformDisplayANGLE.cpp:30-66`; `PlatformDisplaySurfaceless.cpp:58-59`).
- **The context.** It shares WebKit's GL context through `EGL_EXTERNAL_CONTEXT_ANGLE` (`PlatformDisplayANGLE.cpp:70-104`) and is created surfaceless (`GraphicsContextGLTextureMapperANGLE.cpp:232-338`).
- **The frame.** Each WebGL frame is a GL texture in the shared group, handed to TextureMapper as a `CoordinatedPlatformLayerBufferRGB` with a GL fence (`:417-433`). It is composited like any layer, then leaves by SHM or dma-buf as in §1/§4.
- **No dma-buf and no second process** are needed for WebGL itself.

### 5.2 What ANGLE needs on Phoenix

Found by a scratch configure with `ENABLE_WEBGL=ON` (port worktree, `build-wpe.sh --stage configure`, out of tree, no ninja) and by compiling **every ANGLE object it generates** (305 compile lines) with their exact flags.

| # | Problem | Where | Fix (patch 0017) |
|---|---|---|---|
| 1 | `#error Unsupported platform`: `__phoenix__` is in neither the Linux nor the POSIX list | `src/common/platform.h:26-31` | add `__phoenix__` to the POSIX list. WebKit's `PlatformWPE.cmake` already defines `ANGLE_PLATFORM_LINUX` for every non-Android WPE build, which ANGLE needs to choose `DisplayEGL` (`libANGLE/Display.cpp:554-588`) |
| 2 | `std::wostream` does not exist: the toolchain's libstdc++ is built without `_GLIBCXX_USE_WCHAR_T` (`c++config.h:1903`, the known toolchain defect) | `src/common/log_utils.h:139` | the `wostream` operator only `#if !defined(__GLIBCXX__) \|\| defined(_GLIBCXX_USE_WCHAR_T)` |
| 3 | `RTLD_NOLOAD` undefined in libphoenix `<dlfcn.h>` | `src/common/system_utils_posix.cpp:213-217` | without it, the "already loaded" lookup fails cleanly. **libphoenix gap**: candidate for libphoenix itself (with a test), per the standing rule |
| 4 | `pthread_key_t` is a pointer on Phoenix: `static_cast<TLSIndex>(-1)` | `src/common/tls.h:45` | Phoenix: `reinterpret_cast` through `intptr_t` |
| 5 | `linux/futex.h`: futex mutex enabled for `ANGLE_PLATFORM_LINUX` | `src/common/SimpleMutex.h:35-38` | not on Phoenix: the portable mutex |
| 6 | `dlopen("libEGL.so.1")` for the system EGL (static program, no libEGL) | `src/libANGLE/renderer/gl/egl/FunctionsEGLDL.cpp:40-58`, path `DisplayEGL.cpp:111-121` | Phoenix: `mGetProcAddressPtr = eglGetProcAddress` (Mesa's, linked). No clash: ANGLE's entry points are `EGL_*`/`GL_*` and no ANGLE libEGL is built (`ANGLE/CMakeLists.txt:128-199`) |

**Result with patch 0017:** every ANGLE object of the `ENABLE_WEBGL=ON` configure compiles for
aarch64-phoenix with the build's exact flags: 304 C++ objects with 0 warnings, plus `xxhash.c`.
Before the patch the failures were rows 1-5; row 6 compiles either way, and fails only at run time.
ANGLE's `xxhash.c` defines `XXH*`; no archive of the current link (Mesa's included) defines them.

**What is not verified:**
- **The link.** ANGLE + Mesa + WebKit in one static program have never been linked. Possible duplicate symbols: ANGLE's bundled `xxhash.c` (Mesa's xxhash is header-inline), and ANGLE's `third_party/zlib/google` wrapper over the system zlib.
- **WebCore's WebGL objects** (`WebGLRenderingContext*`, `GraphicsContextGLANGLE.cpp`, the JS bindings). They need the generated sources of a real build. They are upstream-maintained for Linux, and the only Phoenix-specific risk there is the same OS layer as above.
- **Run time:**
  - ANGLE's GL backend needs `EGL_KHR_surfaceless_context` (present), `EGL_EXT_create_context_robustness` (present) and ES 3.0+ for WebGL 2 (3.1 present).
  - It resolves every GL function through `eglGetProcAddress`, as epoxy already does here.
  - Its feature workarounds key on `GL_VENDOR`/`GL_RENDERER` ("Broadcom", "V3D 4.2.14.0"): an unknown vendor gets the defaults.
  - `SystemInfo_linux.cpp` reads `/sys`/PCI: nothing there on Phoenix, so it is expected to fail soft.

**Cost:**
- `ENABLE_WEBGL` changes `cmakeconfig.h`: **a full WebKit rebuild** (ninja steps 8956 → 9347: the 305 ANGLE objects plus the WebGL WebCore code). That is ~2 h at -j8 without ccache, ~15 min with a warm ccache.
- It is the port's USE flag **`webgl`**, default off, like `release_log`: `PHX_WPE_WEBGL=1` → `-DENABLE_WEBGL=ON`, and `check_program` then requires `EGL_GetPlatformDisplayEXT` and `GL_BindTexture` in the link.
- `wpe-browser --webgl` turns the setting on per run. A binary without WebGL prints `webgl=unbuilt`.

**Estimate:**
- with patch 0017 as it stands: 1 full build to find link-time problems, then 1-2 Pi runs;
- with link clashes: +1 day.

## 6. The branch

phoenix-rtos-ports `webkit-wpe-b7`, worktree `/home/houp/.claude/jobs/c8f1289c/tmp/b7/ports`, based on master `bbf9883` (build 29). Not merged, not pushed.

- `a438108`: dma-buf transport (patch 0016), the launcher options, `/bin/browser`, the B7 checks.
- `0682ed6`: USE flag `webgl` (`build-wpe.sh`, `port.def.sh`) and ANGLE patch 0017.

| Path | Change |
|---|---|
| `patches/webkit/0016-wpe-phoenix-dmabuf-transport.patch` | §4.2 |
| `patches/webkit/0017-angle-phoenix.patch` | §5.2 (only compiled with USE `webgl`) |
| `files/launcher/wpe-browser.cpp` | `--dmabuf`, `--webgl`, `--present-stats`; the `gpu …` and `present …` lines |
| `files/share/browser` | `BROWSER_DMABUF=1`, `BROWSER_WEBGL=1` |
| `files/build-wpe.sh` | `PHX_WPE_WEBGL` → `ENABLE_WEBGL`; WebGL link checks |
| `port.def.sh` | USE flag `webgl`; patches 0016/0017 in the header; stages the B7 checks; the stage check requires `gpu raster=%s transport=%s webgl=%s`, `WPEB-WEBKIT swap-chain` and `WPEB-WEBKIT dmabuf-export` in the program (a stale binary fails the build) |
| `files/checks/b7.sh`, `b7-anim.html`, `b7-webgl.html` | the gate (§7) |

Patch numbers 0012-0014 are the `jit` branch's (README §Patches); 0016/0017 follow 0015.

## 7. The Pi gate (pre-registered)

### 7.1 What to build and stage

- **Build 1 (cheap):** the image build with ports branch `webkit-wpe-b7` merged, default USE (`rootfs checks`).
  - The B7 files are staged under `/usr/share/wpe-browser/`.
  - Before the cycle, check the stage: `strings <port install>/stage/usr/bin/wpe-browser | grep -c 'WPEB-WEBKIT swap-chain'` ≥ 1. The port's stage check does the same and fails the build otherwise.
- **Build 2 (WebGL, full rebuild):** the same with `use: [rootfs, checks, webgl]` for `webkit_wpe` in `ports.yaml`. Only for §7.4.

Every check is one psh command (psh rules: no quotes, no `;`/`|`/`&`; `b7.sh` builds the URLs with `?`/`&` itself).

### 7.2 Gate 0: the B4 checksum is untouched (`b7.sh headless`, no session, ~2 min)

`/bin/bash /usr/share/wpe-browser/b7.sh headless`

| Run | Expected lines |
|---|---|
| H0 (`--headless --cpu-rendering --snapshot`) | `WPEB … gpu raster=cpu transport=shm webgl=off`; `WPEB-WEBKIT swap-chain pid=<p> type=shm display=surfaceless dmabuf-export=1 transport-hardware=0 explicit-sync=0`; `WPEB … snapshot … crc32=c3e96bf3`; `B7 run=H0 end rc=0` |
| H1 (the same `+ --dmabuf`) | `WPEB … gpu dmabuf-refused reason=headless …`; `transport=shm`; the same `crc32=c3e96bf3`; `rc=0` |

**PASS:** both checksums are `c3e96bf3`. `dmabuf-export=1` is what the WebProcess display offers: it confirms §4.1 row 1 on the Pi before any dma-buf leaves the process.

### 7.3 Gate 1: compositing and transport (`b7.sh anim`, ~8 min, HOLD 480 s)

`/bin/bash /usr/share/wpe-browser/b7.sh anim`

One XFCE session runs four browsers one after another (95 s each, `--size=1280x800 --toolbar=never --ephemeral`, `WPE_BROWSER_PRESENT_SECS=5`) on `b7-anim.html?mode=both&secs=60`: 16 composited layers spinning, plus a 960x320 block of text and gradients that moves by `left` (layout and raster every frame).

| Run | Options | Must print |
|---|---|---|
| A (control: today's default) | `--cpu-rendering` | `gpu raster=cpu transport=shm`; `swap-chain … type=shm … transport-hardware=0`; `present … buffer=shm size=1280x800`; `B7-ANIM done mode=both … fps=<FA>` |
| B | GPU raster | `gpu raster=gpu transport=shm`; `type=shm`; `buffer=shm`; `B7-ANIM done … fps=<FB>` |
| C | GPU raster `--dmabuf` | `gpu dmabuf-formats n=<n> abgr8888=<…>`; `gpu raster=gpu transport=dmabuf`; **`swap-chain … type=texture-dmabuf display=surfaceless dmabuf-export=1 transport-hardware=1 explicit-sync=0`**; **`WPEB-WEBKIT dmabuf-export … fourcc=AB24 modifier=0x… planes=1 … size=1280x800`**; **`present … buffer=dma-buf size=1280x800`**; `B7-ANIM done … fps=<FC>` |
| D | `--cpu-rendering --dmabuf` | as C with `raster=cpu`; `fps=<FD>` |

Each run ends with `B7 run=<id> end rc=0`. The session ends with `B7 anim done` and `XFCE-SESSION done rc=0`.

**PASS (B7 compositing):**
1. Run C prints the three bold lines: the frames really left as dma-bufs.
2. **The HDMI ticks during C and D show the page rendered correctly**, compared by eye with A's ticks: layers in place, text not garbled or tiled, colours not swapped (R/B). Correctness first: a UIF/stride mistake shows as tiled garbage, a fourcc mistake as swapped colours.
3. No `display disconnected`, no `web-process-terminated`, zero faults.
4. **`FC ≥ FB` and `FD ≥ FA`**: dma-buf at least as fast as SHM for the same raster. The present fps of the same runs must agree within ~10 % with the page's `B7-ANIM` fps.

- **Record** FA, FB, FC, FD (page and present), the `min5`/`max5` spread, and the `modifier=` value.
- **The finding to report:** the readback cost, (FC − FB) and (FD − FA), and the raster choice, FB vs FA and FC vs FD.
- **B7's "fps vs software mode"** is FC (or FD, whichever is faster) against FA.

### 7.4 Gate 2: WebGL (`b7.sh webgl`, ~7 min, HOLD 400 s; build 2 only)

`/bin/bash /usr/share/wpe-browser/b7.sh webgl` on `b7-webgl.html?secs=60&tris=20000`: one static VBO, one draw call per frame, a known first frame read back.

| Run | Options | Must print |
|---|---|---|
| W0 (control) | GPU raster, no `--webgl` | `gpu … webgl=off`; `B7-WEBGL context=none renderer=- version=-`; `B7-WEBGL done frames=0 …` |
| W1 | `--webgl` | `gpu … webgl=on`; **`B7-WEBGL context=webgl2 renderer=…`** (`context=webgl` = WebGL 1 only, still a PASS for B7; the renderer string is informational: WebKit may mask it, and `ANGLE … V3D 4.2` is what an unmasked one looks like); **`B7-WEBGL pixel=255,128,0,255 expect=255,128,0,255 ok=1`**; `B7-WEBGL fps=…` every 5 s; `B7-WEBGL done … fps=<FW1> … triangles=20000` |
| W2 | `--webgl --dmabuf` | as W1, plus the C lines of §7.3 (`type=texture-dmabuf`, `buffer=dma-buf`); `done … fps=<FW2>` |

**PASS (B7 WebGL):**
- W1 prints `ok=1` and a `done` line with `frames > 0`;
- the HDMI ticks show the shaded sphere turning (not black, not one solid colour);
- zero faults; `rc=0` for each run.

**Record:** FW1, FW2, the renderer and version strings.

On a binary without WebGL (build 1), W1/W2 print `webgl=unbuilt` and `context=none`: that is not a WebGL failure; build 2 is missing.

### 7.5 Triage

| Symptom | Means | Next |
|---|---|---|
| C: `gpu dmabuf-refused reason=no-linux-dmabuf` | the WPE display bound no `zwp_linux_dmabuf_v1` | check labwc's globals (`WAYLAND_DEBUG=1` on one run) |
| C: `swap-chain … type=shm … transport-hardware=0` | the UI did not ask: a binary without patch 0016, or `WPE_PHOENIX_DMABUF` unset (the `gpu … transport=` line says which) | the stage check / `strings` |
| C: `type=shm … dmabuf-export=0` | Mesa offers no dma-buf export on surfaceless: CAP_PRIME (Mesa patch 0008) missing on the render node | H0's line shows the same |
| C: `eglExportDMABUFImageMESA failed` / `eglExportDMABUFImageQueryMESA failed` / `Failed to create EGL image for texture` | the texture cannot be exported (render server `BO_EXPORT` refused: run `rpi4-v3d-async -v` for `V3DA srv export FAIL … rc=`) | M6 §15 table |
| C: `WPEB … display disconnected` or the browser exits right after the first `dmabuf-export` line | **labwc rejected the buffer** (`create_immed` with a format/modifier it cannot import is fatal) | the `modifier=` value. UIF rejected → LINEAR render targets, or Route G (§4.3) |
| C: a black or frozen window, `present … frames=0` | frames are not presented: `Failed to render frame:` in the log = WPEPlatform path; nothing = a fence wait | `WPE_PHOENIX_EXPLICIT_SYNC` must be unset; the `stall` report of the web process |
| C/D: tiled garbage, a stretched image, R/B swapped | layout or fourcc mismatch between export and import (UIF padding, stride) | `modifier=`/`stride=`; compare with a weston-simple-egl client under labwc |
| B: crash or `web-process-terminated` with GPU raster, A fine | Ganesh on V3D: `addr2line` the pc; `WEBKIT_SKIA_GPU_PAINTING_THREADS=1` as the A/B | — |
| W1: `context=none` with `webgl=on` | WebGL context creation failed: ANGLE display/context. WebKit logs nothing in Release; `PHX_TRACE_ABORT` for an abort | `EGL_EXTERNAL_CONTEXT_ANGLE` / surfaceless config, §5.2 run-time list |
| W1: `pixel=` not `255,128,0,255` | ANGLE renders wrong (format/readback) | the renderer string; ANGLE workarounds for the vendor |

## 8. Open risks

1. **UIF acceptance by labwc** for the texture export (§4.2 risk 1). This decides Route T vs Route G.
2. **Per-frame `clientWait()`.** Without cross-process fences the WebProcess blocks on the GPU per frame. That is the same as today, but now the cost shows. G6b/G15 (passing and polling sync files) would make `WPE_PHOENIX_EXPLICIT_SYNC=1` usable.
3. **The WebGL link** (ANGLE + Mesa duplicate symbols) and WebCore's WebGL objects are uncompiled here (§5.2).
4. **libphoenix gaps** found on the way: `RTLD_NOLOAD` (worked around in ANGLE), and libstdc++ without `wchar_t` (the toolchain defect, worked around).
5. **Memory.** Each dma-buf target is a 4.9 MB render-server BO (4 per swap chain, per web process with a page) instead of shmsrv memory. A WebGL context adds its own textures. The `sysmem` line of `--rss-secs` shows it; run B6's soak with `--dmabuf` before making it the default.
6. **Making it the default** (`/bin/browser`, the XFCE entry) is a separate decision after the gate passes and a soak with `--dmabuf`. Until then, `BROWSER_DMABUF=1` opts in.
