# B10: a desktop browser under XFCE — WebKitGTK or a GTK shell around WPE

Browser milestone B10 ([PLAN](PLAN.md)): an owner-usable desktop browser on the XFCE session
(labwc, Wayland) with tabs and downloads, plus its documentation.

Status 2026-10-08: **design (this note) and phase-2 port + shell on branches; built on the
host as far as §8 says; no Pi cycle yet.**
- Coordination repo: branch `b10-webkitgtk` (this note).
- phoenix-rtos-ports: branch `b10-webkitgtk` (port `webkit_gtk`), based on master `b1334a8`
  (master moved past `e4d9a3c` while this was written; the branch takes 0033/0034 as merged).

Every number below was measured on the build host against
`webkitgtk-2.54.0.tar.xz` (sha256 `846fd19c…3f25682`, the same 2.54.0 release as
`wpewebkit-2.54.0.tar.xz`, checked against webkitgtk.org's `.sums`) and the WPE build of
image build 60 (`.buildroot/_build/aarch64a72-generic-rpi4b/webkit_wpe-build`).

## 0. Recommendation

**(a) WebKitGTK 2.54 (PORT=GTK, GTK 3, Wayland only) as a second port, `webkit_gtk`, with
WebKit's own MiniBrowser/gtk (BSD-2-Clause) as the tabbed shell.** Effort: **7–10 agent-days**
to a passing Pi gate, of which ~3 are build work done on the host (phase 2) and 4–7 are Pi
bring-up whose variance is one question: does GDK 3 get an EGL context from our Mesa (§3.1).

Why (a) and not (b):
1. **The shared code is 97.7 % of our WebKit patches.** 10 923 of 11 177 changed lines apply
   unchanged to the GTK tarball (§2); what is WPE-only is the WPEPlatform Wayland backend and
   the WPE UI-process backing store, which GTK replaces with its own.
2. **A desktop browser is mostly the UI process's dialogs**, and WebKitGTK already has every
   one of them: `<select>` popups, context menus, file chooser (uploads), JS
   alert/confirm/prompt, HTTP auth, colour/date pickers, `<datalist>`, form validation
   bubbles, clipboard, drag and drop, input methods, ATK accessibility, kinetic scrolling,
   pointer lock, fullscreen. Our WPE launcher implements none of them today (of the view's dialog/UI signals it
   connects only `decide-policy` and `create`); with (b) each is ours to write and keep.
3. **(b) has no upstream precedent for GTK.** The only toolkit embedding of WPEPlatform in
   2.54 is Qt6 (`UIProcess/API/wpe/qt6`, 1 430 lines for display + view + toplevel + item,
   without any of the dialogs above). (b)'s dma-buf path needs exactly the same GDK-GL
   bring-up as (a) (§3.1), so it does not remove (a)'s one runtime risk; its only escape from
   it, a Wayland sub-surface owned by WPE, does not work as-is (§4).
4. Its cost is build time and disk, not Pi memory (§6): +50 min–2 h host compile when WebKit
   changes, +~160 MB in the image. One browser runs at a time.

What (a) gives up: the WPE-UI-side frame pacing work (patches 0019/0020-UI/0023) does not
carry over; GTK 3 paints the page as a GL texture in its own GL-composited window
(`gdk_cairo_draw_from_gl`), which is a different, upstream-maintained pacing path. B7/B8's
numbers must be re-measured on it (gate §9, check G6).

## 1. The two candidates

| | (a) WebKitGTK + MiniBrowser shell | (b) GTK shell embedding WPE via WPEPlatform |
|---|---|---|
| Engine build | a 2nd WebKit (PORT=GTK) from `webkitgtk-2.54.0.tar.xz` | none: the shell links the existing `libWPEWebKit` (one binary, `wpe-browser`, gains a GTK role) |
| Our WebKit patches | all 24 + 0030–0034 apply with path exclusions (§2); ~120 new lines of GTK CMake | unchanged |
| New code we own | ~400 lines: a `main()` (role dispatch, defaults, data dirs) | a WPEPlatform GTK backend (`WPEDisplay`/`WPEView`/`WPEToplevel` subclasses: buffer import, input, cursor, clipboard, IME, screen) ~1 500 lines + every dialog of §0.2 ~1 500 lines + tabs/downloads UI |
| dma-buf frames | web process exports textures (patch 0016, already shared); UI imports them as EGLImages into GDK's GL context | same import into a `GtkGLArea`/GDK GL — same GDK-GL dependency |
| Fallback without GDK GL | built in: WebKitGTK switches to CPU raster + SHM (`HardwareAccelerationManager`), no WebGL | SHM buffers into a cairo surface (the shm transport B7 measured at 19 of 30 painted fps) |
| Licence of new code | ours BSD-3; MiniBrowser BSD-2 | ours BSD-3 |
| Maintenance | upstream port; patches rebase with WPE | a private WPEPlatform backend nobody else maintains |
| Effort | 7–10 agent-days | 10–14 agent-days, and a browser that still lacks the long tail (DnD, a11y, pickers) |

## 2. Our patches on the GTK tarball (measured)

The WPE tarball and the GTK tarball are the same release trimmed differently: the GTK one has
no `Source/WebKit/WPEPlatform/wpe/`, `UIProcess/wpe/`, `*/PlatformWPE.cmake` or
`OptionsWPE.cmake`; the WPE one has no `UIProcess/gtk/`. Applying our series to the GTK tree
with `git apply --exclude` on those five path patterns:

```
EX: Source/WebKit/WPEPlatform/*  Source/WebKit/UIProcess/wpe/*  Source/WebKit/UIProcess/API/wpe/*
    */PlatformWPE.cmake  Source/cmake/OptionsWPE.cmake
```

**all of 0001–0024 and 0030–0034 apply cleanly** (host, 2026-10-08, ports master `b1334a8`).
Changed lines per patch, shared vs excluded:

| Patch | shared | WPE-only (excluded) | GTK analogue needed |
|---|---:|---:|---|
| 0001–0005 OS(PHOENIX) WTF/JSC/mimalloc | 317 | 0 | — |
| 0006 cmake (OpenSSL digests, static libWebKit, multi-call hook) | 31 | 66 | **yes**: OptionsGTK + PAL/WebCore/WebKit `PlatformGTK.cmake` |
| 0007 processes + shm | 72 | 35 (`WPEWaylandSHMPool.cpp`) | no: gtk3_wayland patch 0001 already makes GDK's shm pools with `memfd_create` |
| 0008 WTF memory footprint/pressure | 9 | 17 (`wtf/PlatformWPE.cmake`) | **yes**: `wtf/PlatformGTK.cmake` |
| 0009, 0011–0017, 0021, 0024 | 331 | 0 | — |
| 0010 build fixes | 15 | 5 (`DRM_FORMAT_XRGB8888` in the WPE backing store) | no: the GTK backing store has its own `#if USE(LIBDRM)` |
| 0018, 0023 WPEPlatform Wayland event loop | 0 | 33 | no (GDK 3 has its own loop; whether it has the same class of bug is unknown until the Pi runs it) |
| 0019 frame-ahead | 0 | 58 | no (WPE UI backing store only) |
| 0020 frame watch, 0022 wait trace | 533 | 28 | optional (web-process half applies; UI half was WPE's) |
| 0030 FFmpeg player | 2 482 | 12 | **yes**: `USE_FFMPEG` option in OptionsGTK + `FFmpeg.cmake` include |
| 0031–0034 HLS, MSE, zero-copy | 7 165 | 0 | — |
| **total** | **10 923** | **254** | ~120 lines in `webkit_gtk/patches/webkit-gtk/` |

So the GTK port carries **no copy** of a shared patch: `webkit_gtk/patches/{webkit,webkit-video,webkit-mse}/`
are symlinks to `webkit_wpe`'s files (the ports framework hashes a recipe directory by file
content, so a change to a shared patch also rebuilds `webkit_gtk`), and `build-gtk.sh` fails
when `webkit_wpe` has a patch the GTK list does not name. The other agents' 0030–0034 are used,
never edited.

## 3. GTK-port-only code paths that need Phoenix work

### 3.1 GDK 3 GL — the one runtime unknown

- WebKitGTK decides hardware acceleration once, in the UI process:
  `gtkCanUseHardwareAcceleration()` (`UIProcess/gtk/AcceleratedBackingStore.cpp:115-134`)
  creates a GDK GL context on a popup window. If that fails, it logs
  **`Disabled hardware acceleration because GTK failed to initialize GL: …`** and the whole
  view runs non-accelerated: `HardwareAccelerationManager` turns compositing off,
  `WebPreferencesGtk.cpp:36-44` sets `hardwareAccelerationEnabled=false`, the web process's
  swap chain is `Type::SharedMemory` (`AcceleratedSurface.cpp:620`) with CPU raster, and
  WebGL is off. It works, slowly (the B7 CPU+shm arm: MotionMark-quick 4.96 vs 40.87).
- **GDK 3 asks for desktop GL by default.** `gdk/wayland/gdkglcontext-wayland.c:331-334`
  binds `EGL_OPENGL_API` unless `GDK_GL=gles`; our Mesa's wayland variant is GLES-only
  (`versioned-ports/mesa_drm-26.2.0/wayland/opengl.txt`: `opengl=false`). So **the shell sets
  `GDK_GL=gles`** before `gtk_init()` (GTK 3.24 has a complete GLES path: `gdkgl.c`
  `use_texture_gles_program`, `glDrawBuffers` on GLES 3).
- **GDK GL has never run on Phoenix**: `gtk3_wayland` links `gtkphx_noegl.c` as its default EGL
  (every XFCE program draws with cairo/wl_shm), and the WPE UI process has no EGL at all. The
  GTK browser links Mesa's **wayland** variant instead (its `link-gles.txt`: gallium, EGL with
  the wayland + surfaceless platforms, GBM, GLES); the static-EGL libepoxy then resolves real
  entry points.
- With GL up, GDK 3 composites the whole window in GL once any GL context exists on it
  (`gdkwindow.c:2965`, `use_gl = gl_paint_context != NULL`), and `gdk_cairo_draw_from_gl`
  draws WebKit's texture directly; cairo-drawn widgets (tab bar, toolbar) are uploaded as
  textures where damaged. The frame then leaves through `eglSwapBuffers` on Mesa's wayland
  platform — the path the M8 windowed games use.
- **Transport:** the GTK branch of `WebProcessPoolGLib.cpp:195` takes
  `AcceleratedBackingStore::rendererBufferTransportMode()` directly: `Hardware` whenever the
  EGL client extensions have `EGL_MESA_platform_surfaceless` and GDK's EGL display has
  `EGL_EXT_image_dma_buf_import` (no `WPE_PHOENIX_DMABUF` opt-in on that side, and none is
  needed: the UI process imports the dma-buf itself). Without GBM (`USE_GBM=OFF`, as WPE)
  the web process takes the same `Type::Texture` dma-buf export as WPE (patch 0016's shared
  half), and the UI process wraps each buffer in `BufferEGLImage`.
- Native fences: patch 0016 already keeps fence fds in the web process on Phoenix
  (`useExplicitSync()` false); the GTK UI process then never sees one.

### 3.2 The rest

| Area | GTK 2.54 needs | Phoenix answer |
|---|---|---|
| libgcrypt + libtasn1 (required by OptionsGTK) | PAL digests, WebCore crypto | OpenSSL, as 0006 did for WPE (new patch `webkit-gtk/0101`) |
| Library types | JSC `SHARED`, WebKit `SHARED` | `OBJECT` / `STATIC` under `CMAKE_SYSTEM_NAME MATCHES "Phoenix"`, as WPE (0101) |
| WTF sources | `linux/` memory status, RealTimeThreads | `phoenix/MemoryFootprintPhoenix.cpp` + `MemoryPressureHandlerUnix.cpp` (0101) |
| GStreamer | `include(GStreamerDependencies)`, media off without it | `USE_GSTREAMER=OFF` + our `USE_FFMPEG` (0030's player, HLS, MSE, zero-copy run in the web process and are port-independent) — option added in `webkit-gtk-video/0130` |
| Accessibility | `USE_ATK` (GTK 3, forced on) + `USE_ATSPI` (forced on) | ATK is in gtk3_wayland (2.38, no at-spi bridge); the ATSPI side is GDBus code generated by `gdbus-codegen`, already compiled in WPE. With no a11y bus the web process simply has no client |
| Gamepad (`ENABLE_GAMEPAD` default ON, libmanette REQUIRED) | — | OFF |
| Spellcheck (Enchant), hyphenation, libsecret, Flite/Spiel speech, AVIF/JPEG-XL/LCMS, journald, bubblewrap, WebDriver, introspection, docs | optional deps | OFF (as WPE) |
| X11/Quartz targets | default ON | `ENABLE_X11_TARGET=OFF` (gtk3_wayland has no X11 backend), `ENABLE_QUARTZ_TARGET=OFF` |
| `USE_GTK4` | default ON | OFF: XFCE 4.20 and our gtk3_wayland are GTK 3 (API `webkit2gtk-4.1`) |
| GPU process (`ENABLE_GPU_PROCESS`, depends on GBM) | default ON | OFF (as WPE) |
| Printing | `gtk+-unix-print-3.0` optional | present (no backends: GTK built with `print_backends=none`); Print shows an empty printer list |
| Process launching | `ProcessLauncherGLib` (shared) | patch 0007's shared half: the same multi-call `WPE_PHOENIX_EXECUTABLE` / `WPE_PHOENIX_PROCESS_ROLE` mechanism, executable default `/usr/bin/webkit-browser` |
| Injected bundle | `libwebkit2gtkinjectedbundle.so` (MODULE) | made a real dlopen()ed `-fPIC` object exactly as `libWPEInjectedBundle.so`, under `/usr/lib/webkit2gtk-4.1/injected-bundle/` |
| GResource bundles | Inspector, media controls, resources, PDF.js | `--require-defined` per bundle as WPE (the inspector bundle is dropped: no developer extras by default) |
| Static GTK closure | GTK, GDK, pango(+cairo/ft2), cairo(-gobject), gdk-pixbuf, atk, fribidi, epoxy, xkbcommon, wayland-cursor | gtk3_wayland's archives; harfbuzz comes from harfbuzz_icu (same 14.4.0 release as gtk3_wayland's, plus hb-icu), so there is one harfbuzz in the link |
| GLib/GIO | 2.70+ | gtk3_wayland's 2.88 (same as WPE) |

## 4. Why not (b)

- A GTK-widget backend (custom `WPEDisplay` drawing into a `GtkGLArea`) needs GDK GL for
  dma-bufs exactly as (a) does, and writes the WebKitGTK UI process a second time, badly.
- A **Wayland sub-surface** backend (WPE's own `WPEViewWayland` placed under the GTK window)
  would keep the WPE frame path and every B7/B8 number unchanged — but a sub-surface must be
  created on the **same `wl_display` connection** as its parent. WPEPlatform's Wayland display
  opens its own connection (`wpe_display_wayland_connect`), and GDK owns its own; sharing one
  means two event loops reading one display (the exact area of patches 0018/0023) and both
  binding `wl_seat`, so every pointer/keyboard event reaches two listeners. That is a new
  WPEPlatform backend plus a GDK patch, with no upstream precedent: higher risk than (a)'s
  GDK-GL question, for a result that still lacks the dialogs.
- Cheapest of all, and not asked for: **(c) tabs and downloads inside `wpe-browser`'s HTML
  overlay** (~3 agent-days; `WebKitDownload` is GLib API in WPE too). It stays a kiosk-style
  browser without native popups/dialogs; useful only if (a) fails at §3.1 *and* the CPU
  fallback is too slow.

## 5. The shell: candidates and licences

| Shell | Licence | Toolkit / API | Tabs | Downloads | Fit |
|---|---|---|---|---|---|
| **MiniBrowser/gtk** (in the WebKitGTK tarball, `Tools/MiniBrowser/gtk`) | **BSD-2-Clause** (Igalia/Apple headers) | GTK 3 + GTK 4 branches, webkit2gtk-4.1 / 6.0, same 2.54 API | yes (GtkNotebook) | yes (downloads bar) | **chosen**: written against exactly this API, compiled from the tarball (no copy in our tree), no new deps, 5 263 lines; plus find bar, zoom, settings dialog, fullscreen |
| badwolf | BSD-3-Clause + CC-BY-SA-4.0 (docs/icons) | GTK 3, webkit2gtk-4.1 | yes | yes | privacy defaults (JavaScript off, per-tab ephemeral sessions) are wrong for "owner-usable"; separate tarball |
| lariza | MIT | GTK 3, WebKit2GTK | yes (since 20.05) | yes | unmaintained upstream; FIFO-based instance protocol |
| surf (suckless) | MIT | GTK 3 + **X11** properties/XEmbed | no (needs `tabbed`, X11) | no | X11-bound |
| Epiphany, vimb, luakit, Ephemeral, Eolie | GPL-2/3 | — | — | — | excluded: no GPL in Phoenix repos (ports may carry LGPL/BSD only) |

Our part is `files/launcher/webkit-browser.c` (BSD-3): the multi-call role dispatch (as
`wpe-browser.cpp:2534-2560`), `GDK_GL=gles`, persistent data/cache under `$HOME`
(`~/.local/share/webkit-browser`, `~/.cache/webkit-browser`), the process model defaults of
patch 0015 (process cache 2, no prewarm), a start page, plain words → DuckDuckGo, one window
whose tabs/downloads/find/zoom are MiniBrowser's `BrowserWindow`/`BrowserTab`/
`BrowserDownloadsBar`, and `WKGB `-prefixed log lines for the gates.

## 6. Cost

| | WPE (measured, build 60) | GTK (estimate) |
|---|---|---|
| Compile steps | 2 801 objects (unified sources) | ~2 900 (+ `UIProcess/gtk`, `WebKitGTK` API, MiniBrowser) |
| Cold compile, -j8 | 49 min (build 59, mostly ccache misses); "~2 h" without ccache | the same: **ccache gives ~nothing across ports** (PORT changes `cmakeconfig.h`, which every object includes) |
| Incremental (one shared patch) | ~2 min + relink | the same, **paid twice** per image build while both ports ship |
| Build directory | 4.2 GB (`webkit-build`) | +~4.5 GB; ccache must hold both (`ccache -M 40G`, today 20G) |
| Program | `wpe-browser` 158 MB stripped / 338 MB unstripped | `webkit-browser` ~165 MB stripped (+GTK/pango/cairo closure ~8 MB) |
| Image / NFS root | — | **+~165 MB** while both ports ship |
| Pi RAM | one browser at a time | unchanged: text is paged in per program; the page cache (P30) keeps whichever ran last |

Two WebKits in one image are a transition state: once B10's gate passes, the owner can choose
to keep `webkit_wpe` only for the headless/kiosk checks (B4, bench) or drop it.

## 7. Phase-2 implementation (ports branch `b10-webkitgtk`)

```
webkit_gtk/
  port.def.sh                 USE: rootfs checks jit webgl video mse (as webkit_wpe; jit/webgl/video/mse
                              default as the image's webkit_wpe line); depends as webkit_wpe + mesa_drm[wayland]
  patches/webkit/*.patch      -> ../../webkit_wpe/patches/webkit/*  (symlinks, shared)
  patches/webkit-video/*      -> webkit_wpe's 0030 0031 0033
  patches/webkit-mse/*        -> webkit_wpe's 0032 0034
  patches/webkit-gtk/0101-gtk-phoenix-cmake.patch        OptionsGTK + PlatformGTK (WTF, PAL, WebCore, WebKit)
  patches/webkit-gtk-video/0130-gtk-phoenix-ffmpeg.patch USE_FFMPEG for PORT=GTK
  files/build-gtk.sh          build-wpe.sh's stages for PORT=GTK (deps view + GTK closure, compat,
                              extract with the exclusions, configure, build, checks, plugins)
  files/launcher/             webkit-browser.c (+ MiniBrowser sources from the tree), CMakeLists.txt, exports
  files/share/                webkit-browser.desktop (XFCE menu: Internet), start page
```

`webkit_wpe` is **not touched** (no recipe change, so the integrator's next image build does
not rebuild WPE because of B10). `build-gtk.sh` duplicates the generic half of `build-wpe.sh`
(dependency view, compat objects, toolchain file); folding both into one script is a
follow-up once the GTK gate passes.

## 8. What was built on the host

See the branch's commit messages and §10 (filled in as phase 2 ran).

## 9. The Pi gate (pre-registered)

Run under the XFCE session (labwc), image with `webkit_gtk` (USE as the image's `webkit_wpe`
line), `webkit-browser` started from the XFCE menu entry or `webkit-browser URL`.

| # | Check | Pass |
|---|---|---|
| G0 | GL decision | the UART/log has **no** `Disabled hardware acceleration because GTK failed to initialize GL`; the web process logs `WPEB-WEBKIT swap-chain … type=texture-dmabuf` (patch 0016's shared log line). If G0 fails, G1–G5 still run (CPU fallback) and the run is graded "fallback" |
| G1 | the shell opens | window with tab bar + toolbar on HDMI within 20 s of launch, 0 faults |
| G2 | tabs | Ctrl+T opens a 2nd tab; both tabs load (start page, Wikipedia); switching repaints the right page; Ctrl+W closes one |
| G3 | download | a link to a ~1 MB file (`tools/browser/pi-pages` media server) downloads into `~/Downloads` with the downloads bar showing progress, file size equal to the source |
| G4 | Wikipedia | `https://en.wikipedia.org/wiki/Raspberry_Pi` loads, title right, scrolls with the wheel, `<select>`-free page; then a page with a `<select>` opens its popup |
| G5 | video page | `b8.html` H.264 720p and the HEVC 1080p30 HLS ladder play (`hw=1` for HEVC), controls work |
| G6 | numbers | painted fps of G5's 1080p30 HEVC and MotionMark-quick, next to `wpe-browser` on the same image |
| G7 | HDMI shot | one frame with two tabs and the downloads bar, kept as `docs/browser/b10-webkitgtk.png` |

## 10. Risks

1. **GDK GL on our Mesa (§3.1)**: GLES-only + `GDK_GL=gles`, never run on Phoenix. Fallback
   exists (CPU raster + SHM), so the browser works either way; the risk is speed.
2. **GTK 3 GL-composited window pacing**: frames are presented by GDK's frame clock and
   `eglSwapBuffers`; WPE needed three Phoenix-specific pacing patches. Expect similar work.
3. **Static link of a second GTK-based WebKit**: GResource bundles, `--gc-sections` dropping
   constructors (the B6 `g_bytes_get_data` lesson), one harfbuzz in the closure.
4. **Two WebKit builds per image build** while both ports ship (§6); ccache size.
5. **Shared patches drift**: a WPE-only fix added to a shared patch file can break the GTK
   build; `build-gtk.sh` applies with the exclusions and fails on a missing symlink, but a
   compile break in GTK-only code will only show in a GTK build.
