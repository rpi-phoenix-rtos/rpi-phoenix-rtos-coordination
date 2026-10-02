# WPE WebKit on Phoenix-RTOS (browser track D, milestones B4 and B5)

**WPE WebKit 2.54.0** (`PORT=WPE`, the WPEPlatform API with its Wayland and headless backends) is
cross-built for aarch64-phoenix as **one static multi-call ELF, `wpe-browser`**. The UI process,
the WebProcess and the NetworkProcess are the same program ([PLAN](../../../docs/browser/PLAN.md)
decision 2). Like track C (`../jsc`), it is developed outside the ports framework (decision 3):
`build.sh`, the patches and the launcher live here, and all output goes to a scratch directory.

This builds on track C and does not fork it:
- the same pinned tarball, and the host ruby built by `../jsc/build.sh --stage fetch|ruby`;
- track C's five `OS(PHOENIX)` patches (`../jsc/patches/webkit/0001-0005`), applied first;
- the same mimalloc configuration and the same libphoenix compat shims (`../jsc/compat`);
- the same CMake platform file and toolchain template (`../jsc/cmake`).

This directory adds patches `0006`+ and the launcher.

Status 2026-10-02: **builds and links** (`wpe-browser`: 121.5 MB stripped, 270 MB unstripped;
not yet run on the Pi). See [Results](#results).

## Build

```
tools/browser/wpe/build.sh --out <scratch>/out --dl <cache> -j8
```

- The compile and link run through `scripts/heavy-build.sh`: one heavy build on the host at a
  time (it waits for a running image or WebKit build), `-j` capped at
  `min(8, MemAvailable / 2 GB)`, and the build runs in a `MemoryMax=22G` scope. Two WebKit builds
  side by side took the host to 26 GB and systemd-oomd down with them. Never more than `-j8`.
- `--out` must be outside the repository (about 15 GB).
- Outputs:
  - `<out>/wpe-browser`: unstripped, for `addr2line`;
  - `<out>/wpe-browser-stripped`: the file to stage.
- Stages: `deps`, `compat`, `extract`, `configure`, `build`, `all` (the default).
- `--mesa-variant gles|wayland` (default `gles`) picks the mesa_drm build that is linked, see
  [GPU](#gpu-egl-is-not-optional).
- The script reads the tree (sysroot, toolchain, installed ports) and writes only into `<out>`
  and `<dl>`.

The script needs these ports built in the tree: `gtk3_wayland` (GLib 2.88 and its views),
`webkit_deps`, `icu`, `harfbuzz_icu`, `openssl`, `libepoxy`, `mesa_drm` and `wayland_phoenix`.

`deps` copies exactly what WPE links into one prefix, `<out>/deps`. That prefix holds:
- the libraries;
- one `.pc` file per library;
- a `pkg-config` wrapper (`--static --define-prefix`, nothing else on its path).

Why copies:
- The tree's flat `_build/<target>/include` holds every port's headers, including the old ports
  GLib 2.56. That directory must never reach WebKit's compile lines.
- Copies keep a running WebKit build stable while the tree is rebuilt.
- The view is assembled next to `deps` and synced into it by content (`rsync -c`). Refreshing it
  after an image build therefore rebuilds nothing unless a header really changed.

For development, two environment variables take a dependency from a scratch ports build:
`PHX_WEBKIT_DEPS=<webkit_deps install>` and `PHX_ICU_PREFIX=<prefix with icu + harfbuzz_icu>`.
`WEBKIT_SRC=<tree>` builds an already-patched tree.

| Pinned input | Version |
|---|---|
| WebKit | `wpewebkit-2.54.0.tar.xz`, sha256 `efa9bcc3…eb452` (as track C) |
| GLib/GIO | 2.88.3 (gtk3_wayland's private build) |
| libsoup / glib-networking | 3.6.6 / 2.90.0, OpenSSL backend (webkit_deps) |
| ICU / HarfBuzz | 78.3 (filtered data) / 14.4.0 with hb-icu |
| Wayland / wayland-protocols / xkbcommon | 1.24.0 / 1.49 / 1.13.2 (wayland_phoenix) |
| libepoxy | 1.5.10, static-EGL dispatch (port `libepoxy`) |
| Mesa | 26.2.0 `mesa_drm` (`gles` variant: EGL on GBM + surfaceless, GLES 3.1, v3d) |
| OpenSSL | 3.5.9 |
| others | libxml2 2.15.4, libxslt 1.1.45, libwebp 1.6.0, woff2 1.0.2, brotli 1.2.0, sqlite 3.53.4, freetype 26.1.20 (pkg-config version), fontconfig 2.14.2, libpng 1.6.40, libjpeg |

## Configuration (and why)

| Option | Value | Why |
|---|---|---|
| JavaScriptCore | asm LLInt, `ENABLE_JIT/DFG/FTL/WEBASSEMBLY=OFF`, `USE_MIMALLOC=ON` | exactly track C (B3) |
| WPEPlatform | `WAYLAND=ON`, `HEADLESS=ON`, `DRM=OFF`, `ENABLE_WPE_LEGACY_API=OFF` | a labwc window (B5) and a headless display for snapshots (B4); no libwpe/wpebackend-fdo; DRM needs libinput/udev/GBM |
| `USE_GBM`, `USE_LIBDRM`, `ENABLE_GPU_PROCESS` | OFF | no GPU process; the WebProcess renders through EGL **surfaceless** (see GPU) |
| media | `ENABLE_VIDEO/WEB_AUDIO/WEB_RTC/MEDIA_SOURCE/MEDIA_STREAM/MEDIA_RECORDER/MEDIA_SESSION/WEB_CODECS/ENCRYPTED_MEDIA=OFF`, `USE_GSTREAMER=OFF` | B8 later |
| `ENABLE_WEBGL`, `ENABLE_WEBXR`, `USE_VULKAN` | OFF | B7 |
| `ENABLE_XSLT` | ON | libxslt from webkit_deps works (B0 `soup-smoke` XSLTCHK) |
| `ENABLE_WEB_CRYPTO` | OFF | possible later with OpenSSL (patch 0006 already wires WebCore's OpenSSL backend for it; Ed25519/X25519 are libgcrypt-only upstream) |
| crypto library | **OpenSSL instead of libgcrypt + libtasn1** (patch 0006) | WPE hard-requires both (`find_package(... REQUIRED)`, `ENABLE_WEB_CRYPTO=OFF` does not remove them): PAL's SHA digests (WebSocket handshake, SRI, CSP hashes) are the only users then. WebKit already has `CryptoDigestOpenSSL.cpp` (PlayStation port), and OpenSSL is linked anyway (glib-networking) |
| `USE_AVIF/JPEGXL/LCMS/LIBHYPHEN`, `ENABLE_SPELLCHECK`, `ENABLE_GAMEPAD`, `ENABLE_SPEECH_SYNTHESIS`, `USE_ATK` | OFF | dependencies we do not ship |
| `USE_ATSPI` | ON (hard-wired by OptionsWPE) | GDBus only, no extra library; with no accessibility bus address it does nothing. Turning it off would be a patch across WebKit |
| `USE_LIBBACKTRACE`, `USE_SYSPROF_CAPTURE`, `ENABLE_JOURNALD_LOG` | OFF | — |
| `ENABLE_BUBBLEWRAP_SANDBOX` | OFF (also the default for a non-Linux `CMAKE_SYSTEM_NAME`) | no sandbox |
| `USE_SYSTEM_UNIFDEF` | OFF | the host has none; WebKit builds its bundled copy |
| `ENABLE_WEBDRIVER`, `ENABLE_DOCUMENTATION`, `ENABLE_INTROSPECTION`, `ENABLE_MINIBROWSER`, tests | OFF | — |
| `ENABLE_PDFJS` | ON | resources only (pdf.js runs in JSC) |
| `WebKit_LIBRARY_TYPE` | STATIC on Phoenix (patch 0006) | `TARGET_SUPPORTS_SHARED_LIBS` is false, and CMake would silently turn `SHARED` into `STATIC` while WebKit's sub-target object lists only go to a shared link |
| link | `-Wl,--gc-sections`, 8 MiB main stack, 4 KiB pages; Mesa's archives (gallium whole-archive), glib-networking's `libgioopenssl.a`, `libwlphx-compat.a` with `--wrap=close,write`, and `--wrap=mmap,ioctl` for libdrm-phoenix | static closure of what `pkg-config` does not express |

### GPU: EGL is not optional

In WPE 2.54 the WebProcess aborts without an EGL display:
`WebProcess::initializePlatformDisplayIfNeeded()` ends in `CRASH()` after trying GBM, then
surfaceless. Even "software" output composites with GL:
- `WEBKIT_SKIA_ENABLE_CPU_RENDERING=1` (launcher `--cpu-rendering`) moves only Skia's painting to
  the CPU;
- TextureMapper still composites the layers in GLES;
- `AcceleratedSurface::RenderTargetSHMImage` then `glReadPixels()`es each frame into a
  `ShareableBitmap`, which goes to the UI process as SHM.

So Mesa is linked into the binary. The `gles` variant of mesa_drm (EGL on GBM + surfaceless,
v3d/vc4 gallium, no softpipe) needs the `rpi4-v3d-async` render server (`/dev/dri/renderD128`) at
run time.

The UI process never needs EGL in this configuration:
- WPEPlatform's Wayland display tries EGL on the `wl_display` only for dma-buf.
- With the `gles` variant that fails, so the WebProcess is told to use SHM buffers, which is
  decision 5's software path.
- `--mesa-variant wayland` links the EGL Wayland platform and so enables the dma-buf path. That
  is B7 work.

epoxy resolves every EGL and GLES entry point through the statically linked `eglGetProcAddress()`
(the `libepoxy` port's `EPOXY_STATIC_EGL` patch).

## The multi-call program (`launcher/`)

`launcher/wpe-browser.cpp` (added to the WebKit build by patch 0006 through
`-DPHOENIX_BROWSER_DIR`):

- **Role dispatch.** `main()` first records its own absolute path in `WPE_PHOENIX_EXECUTABLE`
  (`realpath(argv[0])`, or a `PATH` search for a bare name). It registers glib-networking's static
  TLS backend (`g_io_openssl_load(NULL)`) in every role, then checks `WPE_PHOENIX_PROCESS_ROLE`:
  - `web`: `WebKit::WebProcessMain(argc, argv)`;
  - `network`: `WebKit::NetworkProcessMain(argc, argv)`;
  - unset: the UI shell.

  WebKit's argv (`<path> <identifier> <socket-fd>`) is passed through unchanged.
- **No orphans.** A child role starts a parent watchdog thread first: once `getppid()` changes
  (the UI process is gone and the child has been adopted by init), it waits 1.5 s for WebKit's
  own exit (the children exit when their IPC connection to the UI closes) and then `_exit(0)`s,
  printing `WPEB … role=<r> pid=<p> orphaned (UI pid <u> gone 1500 ms ago), exiting`. The
  orderly path prints `WPEB … role=<r> pid=<p> main returned <status>` instead. This is the
  Phoenix stand-in for Linux's `PR_SET_PDEATHSIG`; WebKit's own backstops are 10 s watchdogs, and
  the WebProcess's ends in `g_error()`, which on Phoenix raises SIGTRAP rather than calling
  `abort()` (GLib finds no `/proc/self/status` and assumes a debugger).
- **How children find the binary (patch 0007).**
  - `Shared/glib/ProcessExecutablePathGLib.cpp` normally looks for `WPEWebProcess` /
    `WPENetworkProcess` in `WEBKIT_EXEC_PATH` (developer builds only) and then in `PKGLIBEXECDIR`
    (`<libexecdir>/wpe-webkit-2.0`).
  - On Phoenix it returns `WPE_PHOENIX_EXECUTABLE`, or the compile-time
    `WPE_PHOENIX_DEFAULT_EXECUTABLE` (`/usr/bin/wpe-browser`).
  - `UIProcess/Launcher/glib/ProcessLauncherGLib.cpp` sets `WPE_PHOENIX_PROCESS_ROLE=web|network`
    on the `GSubprocessLauncher`. The GLib spawn path is otherwise unchanged (fork+exec, or
    posix_spawn where GLib was built with it).
- **The UI shell.** It is a WPEPlatform view in a `GMainLoop`:

  ```
  wpe-browser [--headless] [--snapshot=FILE.png] [--size=WxH] [--timeout=S]
              [--exit-after-load] [--ignore-tls-errors] [--cpu-rendering] [URL|FILE]
  ```

  - Keys: Ctrl+Q quit, Ctrl+R or F5 reload, Alt+Left / Alt+Right back / forward, Alt+Home the
    start page, F11 fullscreen.
  - Network session: ephemeral (no disk cache or cookie jar yet; B6).
  - Settings: WebGL, media and Web Audio off; JS console messages to stdout.
  - Every line the launcher prints starts with `WPEB t=<ms> `:
    - `start`, `display`, `view`, `role=web|network`;
    - `load started|committed|finished uri=`, `progress`, `title`;
    - `load-failed`, `load-failed-tls`, `web-process-terminated reason=`, `timeout`;
    - `snapshot file= width= height= crc32=`, `exit status=`.
  - Exit status: 0 OK, 1 error, 2 timeout, 3 web process died.
  - `--snapshot`: after the first `load finished`, `webkit_web_view_get_snapshot(VISIBLE)` returns a
    `WebKitImage` (BGRA, premultiplied). The launcher writes it as an RGBA PNG through libpng and
    prints the CRC-32 of the unpremultiplied RGBA rows.

## Patches

`patches/webkit/` is applied after `../jsc/patches/webkit/` by `build.sh`, one commit each in
`<out>/src/webkit`:

| Patch | What |
|---|---|
| 0006-wpe-phoenix-cmake | Phoenix: OpenSSL instead of libgcrypt/libtasn1 (`USE_OPENSSL`, PAL `CryptoDigestOpenSSL.cpp`, WebCore `platform/OpenSSL.cmake`; the key classes are referenced by SerializedScriptValue even with WebCrypto off); `WebKit_LIBRARY_TYPE STATIC`; with a static libWebKit the executables link its frameworks themselves and libWebKit does not `LINK_DEPENDS` on the helper executables (cycle); the `PHOENIX_BROWSER_DIR` hook; `WPE_PHOENIX_DEFAULT_EXECUTABLE` |
| 0007-wpe-phoenix-processes-shm | multi-call process lookup + role variable (above); `memfd_create()` over shmsrv (`libwlphx-compat.a`) for `WebCore::SharedMemory` and WPEPlatform's `wl_shm` pools; the `WPE_PHOENIX_SHM_LOG=1` log |
| 0008-wtf-wpe-phoenix | WTF's WPE source list on Phoenix: no `linux/` (procfs, eventfd, RealtimeKit); `phoenix/MemoryFootprintPhoenix.cpp` (track C) for `memoryFootprint()`; `MemoryPressureHandlerUnix.cpp` with `OS(PHOENIX)` (`processMemoryUsage()` = the meminfo footprint, hold-off timer) |
| 0009-xdgmime-phoenix-static | WebKit's bundled xdgmime and GLib's copy in GIO both define `_caches` and `_xdg_binary_or_text_fallback` in one static link: renamed by `-D`; `ntohl()` from `<arpa/inet.h>` on Phoenix |
| 0010-wpe-build-fixes | upstream bugs with our options: `JSHTMLMediaElementCustom.cpp` needs `#if ENABLE(VIDEO)`; `AcceleratedBackingStore.cpp` needs `DRM_FORMAT_XRGB8888` without libdrm; OpenSSL 3's `EVP_PKEY_get0_RSA()` returns `const RSA*` (WebCore's OpenSSL code targets 1.1); no `MSG_CTRUNC` in libphoenix (the kernel does not report truncated control data; with `wpe-ipc-fd-per-frame` it closes the descriptors that do not fit, as Linux does, and GLib's 256-byte control buffer holds 60) |

Compat (`build.sh` stage `compat`, on top of track C's probes):
- **libstdc++ hides `<fenv.h>`** from C++, because the toolchain was built without
  `_GLIBCXX_HAVE_FENV_H`. With b20's real libphoenix `<fenv.h>` (and its `fesetround` &
  co. in `libphoenix.a`), the compat `fenv.h` here is a one-line include of the C header by path.
  WTF's SIMDe needs `fegetround`/`fesetround`.
- `compat/phoenix-wpe-compat.c`: a weak `nextafterf()`. libphoenix libm has `nextafter()` but not
  the float variant, and WebCore layout/rendering needs it. **libphoenix gap (B1).**
- `msync` comes from `libwlphx-compat.a`, not from the jsc compat object.
- `UINT8_MAX`/`UINT16_MAX` keep track C's `stdint.h` until branch `stdint-int-limits` lands. The
  probe drops it by itself afterwards.
- Track C's compat object is linked as in `jsc`: the `_malloc_init` and `_malloc_fork*` hooks keep
  libphoenix's malloc out of the mimalloc link; `PHX_TRACE_ABORT=1` prints the pc/lr chain on
  SIGABRT.
- The **link** appends the whole static closure at the end of every C++ link
  (`CMAKE_CXX_STANDARD_LIBRARIES`):
  - Mesa's gallium, whole-archive;
  - one `--start-group` holding every dependency archive, Mesa's other archives and
    `libgioopenssl.a`;
  - `libwlphx-compat.a` with its wraps;
  - `libicudata.a`;
  - libphoenix's `libm.a`, **before** g++'s implicit `-lstdc++`, because `libstdc++.a`'s own
    `hypotf` collides with it.

  The order matters: CMake's find modules name only each package's main archive and put a
  target's own libraries *before* WebCore's link interface.
- **unifdef runs on the build machine.** WebKit's bundled copy would be cross-compiled.
  `generate-api-header.py` then silently installs the public API headers *unprocessed*, which
  breaks every `WebKitEnumTypes`/`webkit_web_view_get_type` user. So `build.sh` compiles a host
  `unifdef` and passes `USE_SYSTEM_UNIFDEF=ON`.

## Results

Build host: 16 threads, 29 GiB; every heavy step through `scripts/heavy-build.sh` at `-j8`.

| | Value |
|---|---|
| WebKit steps (configure + `ninja WPEBrowser`) | 8488 (WTF, JSC, bmalloc/mimalloc, Skia, WebCore, PAL, WebKit, WPEPlatform, the launcher). The clean time was not measured in one piece: the build ran in stages while other builds held the host (a `-j8` WebKit build needs ~16 GB) |
| `wpe-browser` stripped / unstripped | **121,480,352 B** / 269,711,088 B; `text` 118.2 MB, `data` 3.3 MB, `bss` 0.6 MB |
| ELF | static, 2 PT_LOAD (4 KiB aligned), PT_GNU_STACK 8 MiB, 0x100-byte TLS segment, no PT_INTERP |
| allocator | `malloc` == `mi_malloc` (the mimalloc override); no `malloc_common` (libphoenix's `malloc_dl.o`) in the link |
| link contents (checked by `build.sh`) | `WebKit::WebProcessMain`, `WebKit::NetworkProcessMain`, `g_io_openssl_load`, `g_tls_backend_get_default`, `memfd_create` (shmsrv), Mesa's `eglGetProcAddress` + `dri2_initialize_surfaceless`, `epoxy_static_proc_address`, `wpe_display_wayland_new`, `wpe_display_headless_new`, ICU (`ubrk_open_78`), hb-icu, OpenSSL `SHA256_Init`, libsoup, `nextafterf` |
| configure: public options ON | `ENABLE_PDFJS ENABLE_WPE_PLATFORM ENABLE_WPE_PLATFORM_HEADLESS ENABLE_WPE_PLATFORM_WAYLAND ENABLE_XSLT USE_SKIA_OPENTYPE_SVG USE_WOFF2` |
| build warnings | GCC 16's `-Wsfinae-incomplete` in upstream WTF/WebCore/WebKit headers (as track C); OpenSSL 3 deprecation warnings in PAL/WebCore's OpenSSL code |

Open gaps, known before the first Pi run:
- **No run anywhere yet.** The program is Phoenix-only: it links Mesa's v3d driver and
  libphoenix, and there is no host build of the same tree. So the shm profile, the RSS and the
  first-page time are Pi measurements (below).
- **GL is required in the WebProcess** (see GPU). If surfaceless EGL on `/dev/dri/renderD128`
  fails on the Pi, the WebProcess aborts before any page loads, and B4 then needs a Phoenix
  answer: either a working render node, or a softpipe/llvmpipe Mesa variant. WPE 2.54 has no
  GL-free compositing path.
- **libphoenix gaps** found by this link (local shims here):
  - `nextafterf` (B1);
  - `MSG_CTRUNC` (cosmetic);
  - libstdc++'s hidden `<fenv.h>` (toolchain);
  - no POSIX shm, so `memfd_create` comes from the wayland_phoenix compat over shmsrv (B6).
- **Spawn:** the children are started by GLib's `GSubprocess` (fork+exec or posix_spawn as GLib
  was configured) with fd inheritance (`take_fd`) of the IPC socket. That path has not run on
  Phoenix with a 120 MB static ELF yet.
- **Sizes:** each process maps the 64 MiB JSC Structure heap only when it creates a VM (the
  WebProcess). mimalloc arenas are 32 MiB steps (track C). Expect ~150-300 MB RSS for the
  WebProcess.
- `hb_icu_get_unicode_funcs` is **not** in the binary, and that is expected: HarfBuzz uses its
  built-in UCD functions, and WebCore takes only `hb_icu_script_to_script` (linked) from hb-icu.
- **Not linked in:** WebCrypto (off, but its OpenSSL key code is compiled), WebGL, media,
  WebDriver, the inspector server.

## Shared memory profile (B6 input)

`WPE_PHOENIX_SHM_LOG=1` makes every process print one line per `WebCore::SharedMemory`
allocation or mapping, and per `wl_shm` pool creation or resize:

```
PHXSHM alloc pid=<pid> fd=<fd> size=<bytes> n=<count> total=<bytes>
PHXSHM map pid=<pid> fd=<fd> size=<bytes> n=<count> total=<bytes>
PHXSHM wlpool pid=<pid> fd=<fd> size=<bytes>
PHXSHM wlpool-resize pid=<pid> fd=<fd> size=<bytes>
```

- Each line is one shmsrv object: contiguous, at least 1 MiB, and one descriptor.
- `wlpool-resize` is an `ftruncate()` growth. shmsrv objects are fixed once allocated, so a
  failure there shows as a missing cursor or buffer.
- No host run was possible: this is a Phoenix-only build. The Pi check below collects the profile.

## Pi check (pre-registered, B4 then B5)

Stage on the netboot NFS root:
- `<out>/wpe-browser-stripped` as `/usr/bin/wpe-browser` (mode 755);
- `pi/b4.html` as `/usr/share/wpe-browser/b4.html`.

The root already has what the browser needs at run time:
- `/etc/fonts/fonts.conf` with DejaVu Sans, Sans Mono and Serif in `/usr/share/fonts/truetype/dejavu`;
- `/etc/ssl/cert.pem`;
- `shmsrv` and `labwc` in `/bin`;
- `rpi4-v3d-async` and `rpi4-kms` start at boot.

psh rules apply:
- psh does **not** strip quotes and has no `;`, `|` or `&`, so every command below is one line
  without quotes;
- psh has `export`;
- every launcher option uses the `--opt=value` form.

### B4: headless render to a buffer (3 processes, no compositor)

| # | Command at `(psh)%` | Expected |
|---|---|---|
| 1 | `export WPE_PHOENIX_SHM_LOG=1 PHX_TRACE_ABORT=1` | — |
| 2 | `/usr/bin/wpe-browser --headless --cpu-rendering --snapshot=/tmp/b4.png --timeout=600 /usr/share/wpe-browser/b4.html` | in order: `WPEB … start pid=… mode=headless uri=file:///usr/share/wpe-browser/b4.html exe=/usr/bin/wpe-browser`, `WPEB … display WPEDisplayHeadless`, `WPEB … view WPEViewHeadless 1024x768`, `WPEB … role=network pid=…` and `WPEB … role=web pid=…` (either order: the children print them), `PHXSHM …` lines, `WPEB … load committed`, `WPEB … title B4 WPE Phoenix <sum>`, `WPEB … load finished`, `WPEB … snapshot file=/tmp/b4.png width=1024 height=768 crc32=XXXXXXXX`, `WPEB … exit status=0`; back to the prompt |
| 3 | the same command again | the **same** `crc32=` (deterministic rendering) |
| 4 | `/usr/bin/wpe-browser --headless --snapshot=/tmp/b4gpu.png --timeout=600 /usr/share/wpe-browser/b4.html` (Skia GPU raster, Ganesh on V3D) | `exit status=0`; its crc may differ from #2. A failure here with #2 passing is a B7 finding, not a B4 failure |

Decision 5 is "Skia CPU raster first", so the primary check (#2, #3) is `--cpu-rendering`.
Compositing still goes through GLES in both cases.

**PASS (B4):**
- #2 and #3 (CPU raster) exit 0 with equal CRCs;
- zero `Exception #` / fault dumps in the UART log;
- `/tmp/b4.png`, copied off the NFS root, shows the page correctly by eye: heading, three
  coloured boxes and a gradient, a table, the canvas square/circle/text, and `JavaScript: sum=…
  PHOENIX-RTOS-WPE-WEBKIT {"a":[1,2,3]}`.

Record:
- the time from `start` to `load finished`;
- every `PHXSHM` line (the shm profile);
- `ps` RSS of the three processes, if a second psh is available.

**Triage:**

| Symptom | Meaning |
|---|---|
| no `role=` line | the child exec failed: GLib spawn, or `WPE_PHOENIX_EXECUTABLE` |
| `web-process-terminated reason=crashed` while the WebProcess is still alive, typically after two `PHXSHM alloc` lines in a row | the UI dropped the IPC connection because a message came without its descriptor: a kernel without branch `wpe-ipc-fd-per-frame` hands every queued SCM_RIGHTS descriptor to the first message read. `wpe-ipc-probe 1 2` shows it |
| `orphaned … exiting` lines | WebKit's own exit did not finish within 1.5 s of the UI's exit; `pi/b4o.sh` (with `wpe-ipc-probe` T5-T8) narrows down which thread held it |
| `Could not create EGL display` then an abort in the web process | the surfaceless EGL path failed: no render node, `rpi4-v3d-async` down |
| `Failed to create shared memory` | shmsrv not running |
| `web-process-terminated reason=crashed` | `addr2line -e <out>/wpe-browser <pc>` on the fault dump's pc first |
| a silent abort of a process | `PHX-ABORT` lines (`PHX_TRACE_ABORT=1`, track C's compat): `addr2line -f -e <out>/wpe-browser <pc> <lr> <frames…>` |

### B5: a window on labwc (3 processes, wl_shm), local page then Wikipedia

The window runs inside the XFCE session (`/bin/xfce-session` starts labwc, the panel and the
autostart list; `XFCE_AUTOSTART` takes `/<path>=<one argument>` items):

| # | Command at `(psh)%` | Expected |
|---|---|---|
| 1 | `export WPE_PHOENIX_SHM_LOG=1 PHX_TRACE_ABORT=1 WEBKIT_SKIA_ENABLE_CPU_RENDERING=1 HOLD=480` | — |
| 2 | `export XFCE_AUTOSTART=/usr/bin/wpe-browser=/usr/share/wpe-browser/b4.html:120,/usr/bin/wpe-browser=https://en.wikipedia.org/wiki/Phoenix-RTOS:300` | — (every item **must** end in `:<seconds>`: xfce-autostart.sh takes the seconds after the LAST colon, so a URL item without them would be cut at `https`) |
| 3 | `/bin/bash /bin/xfce-session` | `XFCE-SESSION servers v3d-async=up kms=up shm=up`. Then for each item: `XFCE-AUTOSTART open /usr/bin/wpe-browser secs=…`, `WPEB … mode=window`, `WPEB … display WPEDisplayWayland`, `WPEB … view WPEViewWayland 1024x768`, `role=network`, `role=web`, `PHXSHM wlpool …` lines, `load committed/finished`, `title …`. The Wikipedia item also needs `load committed uri=https://en.wikipedia.org/…` with no `load-failed-tls`. HDMI shows the page in a labwc window |

**PASS (B5):**
- both pages reach `load finished`;
- the HDMI frame shows them rendered, by eye (HDMI ticks in `artifacts/hdmi/`);
- zero faults;
- the session ends normally (`XFCE-SESSION done rc=0`).

Scrolling and the keys (Ctrl+R, Alt+Left) are checked by hand at the Pi, or in a later cycle with
USB input.

**Record:**
- RSS per process;
- the `PHXSHM` profile, with object count per frame;
- time to first `load finished` for each page.
