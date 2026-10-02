# WPE WebKit on Phoenix-RTOS (browser track D, milestones B4-B6)

**WPE WebKit 2.54.0** (`PORT=WPE`, the WPEPlatform API with its Wayland and headless backends) is
cross-built for aarch64-phoenix as **one static multi-call ELF, `wpe-browser`**. The UI process,
the WebProcess and the NetworkProcess are the same program ([PLAN](../../../docs/browser/PLAN.md)
decision 2).

Since B6 the build is the **phoenix-rtos-ports port `webkit_wpe`** (PLAN decision 3: once a stage
passes, the scratch build becomes a port; branch `webkit-wpe-port` until it is merged). Everything
the build reads lives there:

| Port path | What |
|---|---|
| `port.def.sh` | the recipe: dependencies, the patches, the staging (USE `rootfs`, `checks`) |
| `patches/webkit/0001-0011` | track C's five `OS(PHOENIX)` patches (WTF, JSC, mimalloc; the same files as `../jsc/patches/webkit/`) and WPE's six, see [Patches](#patches) |
| `files/build-wpe.sh` | **the** build: the port runs it, and so does `build.sh` here for scratch builds |
| `files/launcher/` | the program (`wpe-browser.cpp`, its CMake file, the export list) |
| `files/compat/`, `files/cmake/` | track C's libphoenix compat shims (+ `phoenix-wpe-compat.c`), the CMake platform module and toolchain template |
| `files/share/` | `/bin/browser`, the `.desktop` entry, the start page |
| `files/checks/` | the Pi checks: `b4.html`, the probe web process extension, `b6.sh` and its pages |

This directory keeps `build.sh` (the scratch-build wrapper), this README (configuration, results,
the pre-registered Pi checks) and `pi/` (development probes: `wpe-ipc-probe.c`, `b4o.sh`).

Status 2026-10-02: **B4 PASS** and **B5 PASS** on the Pi (see PLAN). **B6** (a usable browser) is
built (the new launcher and WTF's `FileSystem.cpp` with patch 0011 compiled alone, relinked
with the B5 scratch tree's other WebKit objects and the current sysroot) and its checks are
pre-registered [below](#b6-a-usable-browser).

## Build

### In the image: the port

- `ports.yaml` lists `webkit_wpe` with `use: [rootfs, checks]` (phoenix-rtos-project branch
  `webkit-wpe-port`). It depends on `gtk3_wayland webkit_deps icu harfbuzz_icu openssl libepoxy
  mesa_drm wayland_phoenix`.
- **The first build takes ~2 h** at `-j8` (8499 ninja steps) and ~15 GB. Its build directory is
  `_build/<target>/webkit_wpe-build`, deliberately **not** the port's work directory: a clean
  removes the work directory, and every recipe change in a dependency cleans its dependents.
  The inputs reach the build directory by content instead, so ninja rebuilds exactly what
  changed:
  - the patched tree is synced into `<out>/src/webkit` with `rsync -c` (an unchanged file keeps
    its mtime);
  - the dependency prefix `<out>/deps` and the compat headers the same way;
  - a hash of the static link closure (every archive of `<out>/deps`, the sysroot's
    `lib*.a`, `libstdc++.a`, the compat objects) drops `bin/wpe-browser` when it changes, so
    ninja relinks it (those archives are not ninja dependencies of the link).

  A dependency rebuilt to the same headers therefore costs a relink (~1 min). `rm -rf
  _build/<target>/webkit_wpe-build` forces a full build.
- **ccache** is used when the host has it (`PHX_CCACHE=0` turns it off). This host has none:
  `sudo apt install ccache && ccache -M 20G` makes a forced full rebuild ~10-15 min.
- **Memory and the heavy-build lock.** `rebuild-rpi4b-fast.sh` holds the heavy-build lock
  (`/tmp/phoenix-heavy-build.lock`) for its whole run, so the port's compile never overlaps a
  scratch WebKit build (those go through `scripts/heavy-build.sh`, which waits). The port runs
  plain ninja at `-j` min(8, MemAvailable / 2 GB); it must not call `heavy-build.sh` itself, which
  would wait forever for the lock its own image build holds. The image build has no `MemoryMax`
  scope of its own; to get one, run it as `scripts/heavy-build.sh -- ./scripts/rebuild-rpi4b-fast.sh
  ...` (`heavy-build.sh` exports `HEAVY_BUILD_LOCKED=1`, so the rebuild does not take the lock
  again, and `build-wpe.sh` keeps to `HEAVY_BUILD_JOBS`).

### Scratch builds (development)

```
WPE_PORT_DIR=<ports worktree>/webkit_wpe tools/browser/wpe/build.sh --out <scratch>/out --dl <cache> -j8
```

(`WPE_PORT_DIR` defaults to `sources/phoenix-rtos-ports/webkit_wpe`, i.e. after the merge.)

- The wrapper sets the tree (`PHX_TREE`, `PHX_TC`), runs the compile through
  `scripts/heavy-build.sh` (one heavy build on the host at a time, `-j` capped at
  `min(8, MemAvailable / 2 GB)`, a `MemoryMax=22G` scope; two WebKit builds side by side took the
  host to 26 GB and systemd-oomd down with them) and keeps the toolchain file of trees configured
  before the port (`PHX_CMAKE_HERE=tools/browser/jsc`: a changed toolchain file rebuilds WebKit
  from scratch). ccache is opt-in here (`PHX_CCACHE=1`): it changes every compile line.
- `--out` must be outside the repository (about 15 GB).
- Stages: `ruby` (host ruby + libyaml when the host has no ruby; this host has none), `deps`,
  `compat`, `extract`, `configure`, `build`, `plugins` (the two shared objects, against an existing
  build), `all` (the default).
- **A launcher change**: `--stage build` recompiles `wpe-browser.cpp` and relinks (~1 min).
  The existing scratch tree (`br-d`, configured from `tools/browser/wpe/launcher` before the move)
  re-runs CMake once for the new `PHOENIX_BROWSER_DIR` and should then rebuild only the launcher:
  check with `ninja -C <out>/webkit-build -n WPEBrowser | tail -1` first. Its source tree has
  patches 0001-0010 as commits but not 0011 (the B6 binary got 0011 as one object compiled by
  hand): `git -C <tree> apply <port>/patches/webkit/0011-wtf-maptofile-phoenix-write.patch` and
  commit it before rebuilding there. Note that sysroot headers newer than its objects
  (`stdint.h`, `dlfcn.h`, `malloc.h` since build 21) make ninja recompile most of WebKit (~2 h).
- `--mesa-variant gles|wayland` (default `gles`) picks the mesa_drm build that is linked, see
  [GPU](#gpu-egl-is-not-optional).
- Outputs:
  - `<out>/wpe-browser`: unstripped, for `addr2line`;
  - `<out>/wpe-browser-stripped`: the file to stage;
  - `<out>/libWPEInjectedBundle.so`: the WebProcess's injected bundle, see
    [Loaded objects](#loaded-objects-the-injected-bundle-and-web-process-extensions);
  - `<out>/phx-probe-extension.so`: the web process extension of the Pi check.
- The script reads the tree (sysroot, toolchain, installed ports) and writes only into `<out>`
  and `<dl>`.

The build needs these ports built in the tree: `gtk3_wayland` (GLib 2.88 and its views),
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

For development, environment variables take a dependency from elsewhere: `PHX_WEBKIT_DEPS=<webkit_deps
install>`, `PHX_ICU_PREFIX=<prefix with icu + harfbuzz_icu>`, `PHX_GTK`, `PHX_OPENSSL`,
`PHX_WAYLAND`, `PHX_EPOXY`, `PHX_MESA`. `WEBKIT_SRC=<tree>` builds an already-patched tree.

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
| host ruby | 3.4.7 + libyaml 0.2.5, built when the host has no ruby |
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

## The multi-call program (`files/launcher/`)

`wpe-browser.cpp` (added to the WebKit build by patch 0006 through `-DPHOENIX_BROWSER_DIR`):

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
  `abort()` (GLib finds no `/proc/self/status` and assumes a debugger). The same thread logs the
  child's memory footprint when `WPE_BROWSER_RSS_SECS` is set (below).
- **How children find the binary (patch 0007).**
  - `Shared/glib/ProcessExecutablePathGLib.cpp` normally looks for `WPEWebProcess` /
    `WPENetworkProcess` in `WEBKIT_EXEC_PATH` (developer builds only) and then in `PKGLIBEXECDIR`
    (`<libexecdir>/wpe-webkit-2.0`).
  - On Phoenix it returns `WPE_PHOENIX_EXECUTABLE`, or the compile-time
    `WPE_PHOENIX_DEFAULT_EXECUTABLE` (`/usr/bin/wpe-browser`).
  - `UIProcess/Launcher/glib/ProcessLauncherGLib.cpp` sets `WPE_PHOENIX_PROCESS_ROLE=web|network`
    on the `GSubprocessLauncher`. The GLib spawn path is otherwise unchanged.

### The UI shell (B6)

```
wpe-browser [--headless] [--snapshot=FILE.png] [--size=WxH] [--timeout=S] [--exit-after-load]
            [--ignore-tls-errors] [--cpu-rendering] [--web-extensions=DIR]
            [--ephemeral] [--data-dir=DIR] [--cache-dir=DIR] [--no-chrome] [--search=PREFIX]
            [--cycle=LIST] [--cycle-secs=S] [--rss-secs=S] [--auto=STEPS]
            [URL|FILE|WORDS]
```

A WPEPlatform view in a `GMainLoop`, one window, one view. Design decisions:

- **The chrome is an overlay inside the page, drawn by WebKit; the address is edited in the UI
  process.** WPEPlatform shows one view per toplevel (several views are tabs, not tiles), its
  Wayland seat sends input only to surfaces that are a WPE toplevel, and a second toplevel is a
  second labwc window. A toolbar of our own (a cairo subsurface) would need its own `wl_seat`,
  its own text rendering and cairo in the link. Instead:
  - a user script, injected at document start into every top-level page in the script world
    `wpe-browser`, builds a toolbar (back, forward, reload/stop, home, the address) and a
    progress line in a **closed shadow root** under one host element, styled by a constructed
    style sheet. Its JavaScript objects are invisible to the page, the page's
    Content-Security-Policy does not apply to the world, and the page's styles cannot reach
    into the shadow root;
  - it draws only what the UI process sends (`webkit_web_view_evaluate_javascript` in that
    world: `wpeBrowserChrome.update({...})` with the address text, caret, progress, back/forward
    state) and posts its buttons' actions back (`script-message-received::chrome`);
  - **it never takes the keyboard focus.** While the address is edited, the UI process consumes
    every key event in the view's `event` handler (which runs before WebKit's) and edits a
    line buffer of its own: the page never sees those keystrokes, so a site's keyboard shortcuts
    cannot fire while typing an address;
  - the toolbar shows with Ctrl+L (or a click on its address) and when the pointer touches the
    top edge of the page, and hides again; the 3 px progress line shows while a page loads.
  - Trade-off: the host element is in the page's DOM (a page walking `<html>`'s children sees
    one more `<div>`); not in SVG/XML documents' rendering (no overlay there, the keys still
    work).
- **Addresses:** a URI as is; a path as a file; a host name (it has a dot, or a port) gets
  `https://` (`http://` for `localhost` and IP addresses); anything else, one word or several, is
  a search: `--search` prefix + the escaped text, default
  `https://html.duckduckgo.com/html/?q=` (DuckDuckGo's HTML endpoint, no JavaScript: the LLInt
  is ~12x slower than the host JIT). The command-line argument goes the same way, after an
  existing file.
- **Keys** (window mode): Ctrl+L / Alt+D / F6 the address (Enter go, Escape cancel; Ctrl+A select
  all, Ctrl+U clear, Left/Right/Home/End, Backspace/Delete); Alt+Left / Alt+Right back / forward;
  Ctrl+R / F5 reload, Ctrl+Shift+R / Shift+F5 reload without the cache; Escape stop (while
  loading; otherwise the page gets it); Alt+Home the start page (headless: the first page); F11
  fullscreen; Ctrl+Q quit.
  A click into the page ends address editing.
- **The title** of the window follows the page title (`wpe-browser` while there is none).
- **New-window requests** (`target=_blank`, `window.open()`) load in the one view:
  `decide-policy` `NEW_WINDOW_ACTION` is ignored and its URI loaded; `create` does the same and
  returns no view.
- **Persistence** (window mode; `--headless` and `--ephemeral` keep the ephemeral session):
  `webkit_network_session_new(data, cache)` with
  - data `$HOME/.local/share/wpe-browser` (`--data-dir`): cookies (`cookies.sqlite`, libsoup's
    SQLite jar, policy `ACCEPT_NO_THIRD_PARTY`), local storage, IndexedDB, HSTS;
  - cache `$HOME/.cache/wpe-browser` (`--cache-dir`): the HTTP disk cache (`WebKitCache/`, cache
    model `WEB_BROWSER`).

  `$HOME`, not `XDG_DATA_HOME`/`XDG_CACHE_HOME`: the XFCE session sets those to its RAM `/tmp`.
  `xfce-desktop.sh` sets `HOME=/root`, which is on the NFS (netboot) or ext2 (SD) root. With no
  `HOME`, `/root`. A directory that cannot be created falls back to an ephemeral session
  (`session-error mkdir …`).
- **Start page:** with no argument in window mode, `/usr/share/wpe-browser/start.html` (a search
  form, links to the B6 sites, the keys); `about:blank` headless.
- **Settings:** WebGL, media, Web Audio and the page cache off; JS console messages to stdout.
- **Test knobs** (also from the environment, because an `XFCE_AUTOSTART` item takes one
  argument):
  - `--cycle=LIST` (`WPE_BROWSER_CYCLE`): every `--cycle-secs` (`WPE_BROWSER_CYCLE_SECS`,
    default 60) load the next entry of LIST, round robin; LIST is comma-separated or the path of a
    file with one entry per line (`#` comments), each entry resolved like the address field;
    without an argument the first entry is the first page;
  - `--rss-secs=S` (`WPE_BROWSER_RSS_SECS`): every process logs its memory footprint every S s
    (WTF's `memoryFootprint()`, patch 0003: the anonymous pages of its map entries from
    `meminfo()`, as `psh mem <pid>` lists them);
  - `--auto=STEPS` (`WPE_BROWSER_AUTO`): synthetic keyboard input, comma-separated
    `<seconds>:key:<keys>` (`ctrl+l`, `alt+Left`, `ctrl+shift+r`, `F5`, `Return`, `Escape`, a
    character) or `<seconds>:type:<text>`, seconds from the start. Each key is a `WPEEvent`
    sent through `wpe_view_event()`, the path real keys take from the Wayland seat: the launcher's
    handler first, then WebKit (so `type:` also types into a focused page field).
- **Exit status:** 0 OK, 1 error, 2 timeout, 3 web process died (`--snapshot` / `--exit-after-load`).
- `--snapshot`: after the first `load finished`, `webkit_web_view_get_snapshot(VISIBLE)` returns a
  `WebKitImage` (BGRA, premultiplied). The launcher writes it as an RGBA PNG through libpng and
  prints the CRC-32 of the unpremultiplied RGBA rows.

Every line the launcher prints starts with `WPEB t=<ms> ` (ms since that process started):

| Line | When |
|---|---|
| `start pid= webkit=2.54.0 mode=window\|headless uri= exe=` | UI start (`uri` = the first page) |
| `display <type>`, `view <type> <W>x<H>` | display connected, view created |
| `session ephemeral` / `session persistent data= cache= cookies= cookie-policy=no-third-party cache-model=web-browser` / `session-error …` | the network session |
| `chrome on world=wpe-browser search= home=` | window mode, the overlay installed |
| `role=web\|network pid= ppid= argc=` | a child started |
| `load started\|redirected\|committed\|finished uri=`, `progress <0..1>`, `title <t>` | page loads |
| `load-failed uri= error=`, `load-failed-tls uri= flags=`, `web-process-terminated reason=`, `timeout after <s> s` | failures |
| `chrome action=<a> source=key\|ui\|auto\|cycle …` | every chrome action: `focus-url`, `cancel`, `go input=<typed> uri=<resolved>`, `back ok=0\|1`, `forward ok=0\|1`, `reload uri=`, `reload-nocache uri=`, `stop loading=0\|1`, `home uri=`, `fullscreen`, `unfullscreen`, `quit` (`source=key`: a key from the seat; `ui`: an overlay button; `auto`: an `--auto` step; `cycle`/`pointer`: a cancel by the cycle or a click) |
| `new-window uri= opened=same-view via=policy\|create` | a new-window request, loaded in the view |
| `cycle pages=<n> secs=<s> from=<file\|list>`, `cycle n=<k> uri=` | `--cycle` |
| `auto key=<keys>`, `auto type=<text>`, `auto bad-…` | `--auto` steps |
| `mem role=ui\|web\|network pid= footprint_kb=` | `--rss-secs` |
| `snapshot file= width= height= crc32=`, `exit status=` | the end |

`/bin/browser` (the port's `files/share/browser`, run as `/bin/bash /bin/browser [URL|FILE|WORDS]`:
Phoenix execs no `#!` scripts) is the desktop launcher: `--size=1280x960 --cpu-rendering`
(`BROWSER_SIZE`, `BROWSER_GPU=1` for Skia's GPU raster), `HOME=/root` and the session's
`WAYLAND_DISPLAY` (the first `$XDG_RUNTIME_DIR/wayland-N` when unset). The XFCE menu entry "Web
Browser" (Internet; `/usr/share/applications/wpe-browser.desktop`, `Exec=/bin/bash /bin/browser
%u`) runs it, and so does a panel launcher (phoenix-rtos-ports branch `xfce-browser-launcher`).

## Loaded objects: the injected bundle and web process extensions

Every WebProcess loads WebKit's **injected bundle**, `libWPEInjectedBundle.so`, from
`/usr/lib/wpe-webkit-2.0/injected-bundle/` (`PKGLIBDIR`; `WEBKIT_INJECTED_BUNDLE_PATH` overrides
the directory). `InjectedBundle::initialize()` (`WebProcess/InjectedBundle/glib/InjectedBundleGlib.cpp`)
opens it with `g_module_open()`, which is libphoenix's `dlopen()`, and calls its one entry point,
`WKBundleInitialize`. The bundle is a single source, `WebKitInjectedBundleMain.cpp`, and the
entry point only forwards to the program: `WebProcessExtensionManager::singleton().initialize()`.
That call does the work:
- it creates the process's `WebKitWebProcessExtension` and installs it as the bundle client, so
  that every page gets its `WebKitWebPage` (the web process side of the GLib API: user messages
  between page and view, `send-request`, the form manager, context-menu and console signals);
- it loads the **web process extensions**, every `.so` in the directory the UI process set with
  `webkit_web_context_set_web_process_extensions_directory()` (launcher `--web-extensions=DIR`),
  and calls their `webkit_web_process_extension_initialize[_with_user_data]`.

Without the bundle the WebProcess still renders pages, but none of that exists: it prints
`Error loading the injected bundle (…)`, `webkit_web_view_send_message_to_page()` replies
"unhandled", and no extension is ever loaded.

What makes it load on Phoenix:
- **The bundle is a real shared object.** CMake made the `WPEInjectedBundle` MODULE library
  static (Phoenix has no shared libraries in CMake's terms, as for libWPEWebKit), and `ninja
  WPEBrowser` never built it. `build-wpe.sh` also builds that static library and links its one
  object `-shared -fPIC -nostartfiles -nostdlib -Wl,--hash-style=sysv` (stage `plugins`):
  - `-nostartfiles`: the toolchain's startfiles are a program's (crt0, with `_start`);
  - `-nostdlib`: libc, GLib and WebKit stay undefined and bind to the program's copies (a second
    libc in the object would mean a second heap);
  - a SysV hash table: libphoenix's `dlopen()` takes the symbol count from `DT_HASH`.
- **wpe-browser has an export table** (the port's `files/launcher/wpe-browser.exports`, linked by
  `files/launcher/CMakeLists.txt`). The program is static and stripped, so there was nothing to bind the
  bundle's undefined symbols to. With `-Wl,--no-dynamic-linker -Wl,--dynamic-list=<list>` ld
  gives the static program a `.dynsym` holding exactly the listed symbols:
  - libphoenix's `dlopen()` resolves against it (libphoenix branch `dl-host-exports`; before it,
    only an unstripped program's `.symtab` was read);
  - it is part of the loaded image and survives strip;
  - ld keeps every listed symbol under `--gc-sections`. `WebProcessExtensionManager::initialize`
    was collected before: only the bundle calls it.

  Both flags are needed: without `--no-dynamic-linker`, ld creates no dynamic sections for a
  program that links no shared library, and `--dynamic-list` alone does nothing.
  `--require-defined` for every listed symbol makes a WebKit rename a link error. The list
  holds 7 symbols: the bundle's 3 imports (`abort` and the two `WebProcessExtensionManager`
  methods) and the probe extension's 4. The stripped program grows by 34 KB, and its 2 PT_LOAD
  segments do not change (a PT_DYNAMIC is added; the kernel loader ignores it).
- `build-wpe.sh` checks each object: no `PT_TLS` (there is no dynamic TLS), `DT_HASH` present, no
  `DT_NEEDED`, the entry point defined, and every import exported by `wpe-browser`.
  The bundle needs nothing more from the loader. It has no TLS, no static constructors or
  destructors (so no `__dso_handle`/`__cxa_atexit`), and only 3 `JUMP_SLOT` relocations. WebKit
  is built `-fno-exceptions`, so no unwinding crosses the boundary.

A web process extension for Phoenix is built the same way and may use only what the export list
holds. Adding the public extension API (`webkit_web_*`, `jsc_*`, GLib) means adding it to the
list, at the cost of keeping those functions in the program.

## Patches

The port's `patches/webkit/` holds all eleven, applied in order (the image build: the framework's
`b_port_apply_patches`; a scratch build without `WEBKIT_SRC`: `build-wpe.sh`, one commit each in
`<out>/src/webkit`). `0001`-`0005` are track C's, byte-identical to `../jsc/patches/webkit/` (the
jsc shell keeps its copies; `build.sh` warns when they drift):

| Patch | What |
|---|---|
| 0006-wpe-phoenix-cmake | Phoenix: OpenSSL instead of libgcrypt/libtasn1 (`USE_OPENSSL`, PAL `CryptoDigestOpenSSL.cpp`, WebCore `platform/OpenSSL.cmake`; the key classes are referenced by SerializedScriptValue even with WebCrypto off); `WebKit_LIBRARY_TYPE STATIC`; with a static libWebKit the executables link its frameworks themselves and libWebKit does not `LINK_DEPENDS` on the helper executables (cycle); the `PHOENIX_BROWSER_DIR` hook; `WPE_PHOENIX_DEFAULT_EXECUTABLE` |
| 0007-wpe-phoenix-processes-shm | multi-call process lookup + role variable (above); `memfd_create()` over shmsrv (`libwlphx-compat.a`) for `WebCore::SharedMemory` and WPEPlatform's `wl_shm` pools; the `WPE_PHOENIX_SHM_LOG=1` log |
| 0008-wtf-wpe-phoenix | WTF's WPE source list on Phoenix: no `linux/` (procfs, eventfd, RealtimeKit); `phoenix/MemoryFootprintPhoenix.cpp` (track C) for `memoryFootprint()`; `MemoryPressureHandlerUnix.cpp` with `OS(PHOENIX)` (`processMemoryUsage()` = the meminfo footprint, hold-off timer) |
| 0009-xdgmime-phoenix-static | WebKit's bundled xdgmime and GLib's copy in GIO both define `_caches` and `_xdg_binary_or_text_fallback` in one static link: renamed by `-D`; `ntohl()` from `<arpa/inet.h>` on Phoenix |
| 0010-wpe-build-fixes | upstream bugs with our options: `JSHTMLMediaElementCustom.cpp` needs `#if ENABLE(VIDEO)`; `AcceleratedBackingStore.cpp` needs `DRM_FORMAT_XRGB8888` without libdrm; OpenSSL 3's `EVP_PKEY_get0_RSA()` returns `const RSA*` (WebCore's OpenSSL code targets 1.1); no `MSG_CTRUNC` in libphoenix (the kernel does not report truncated control data; with `wpe-ipc-fd-per-frame` it closes the descriptors that do not fit, as Linux does, and GLib's 256-byte control buffer holds 60) |
| 0011-wtf-maptofile-phoenix-write | B6: `FileSystem::mapToFile()` creates a file, maps it `MAP_SHARED` and copies the data into the mapping; the network cache stores every body larger than a page that way (`NetworkCacheBlobStorage`, `Blobs/`), the service worker script storage too. Phoenix has no shared file mappings (`MAP_SHARED` = `MAP_PRIVATE` = 0, no page is written back), so the file kept the zeros of its `ftruncate()`. On Phoenix the bytes go to the file with `write()` and the caller gets an anonymous read-only copy |

Compat (`build-wpe.sh` stage `compat`; the port's `files/compat/` = track C's set plus
`phoenix-wpe-compat.c`):
- **libstdc++ hides `<fenv.h>`** from C++, because the toolchain was built without
  `_GLIBCXX_HAVE_FENV_H`. With b20's real libphoenix `<fenv.h>` (and its `fesetround` &
  co. in `libphoenix.a`), the compat `fenv.h` here is a one-line include of the C header by path.
  WTF's SIMDe needs `fegetround`/`fesetround`.
- `files/compat/phoenix-wpe-compat.c`: a weak `nextafterf()`. libphoenix libm has `nextafter()` but not
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
  breaks every `WebKitEnumTypes`/`webkit_web_view_get_type` user. So `build-wpe.sh` compiles a host
  `unifdef` and passes `USE_SYSTEM_UNIFDEF=ON`.

## Results

Build host: 16 threads, 29 GiB; every heavy step through `scripts/heavy-build.sh` at `-j8`.

| | Value |
|---|---|
| WebKit steps (configure + `ninja WPEBrowser`) | 8488 (WTF, JSC, bmalloc/mimalloc, Skia, WebCore, PAL, WebKit, WPEPlatform, the launcher). The clean time was not measured in one piece: the build ran in stages while other builds held the host (a `-j8` WebKit build needs ~16 GB) |
| `wpe-browser` stripped / unstripped | B5: **121,480,352 B** / 269,711,088 B; `text` 118.2 MB, `data` 3.3 MB, `bss` 0.6 MB. B6 (the chrome, persistent session, test knobs, patch 0011): **121,572,736 B** / 269,851,528 B (+92 KB) |
| ELF | static, 2 PT_LOAD (4 KiB aligned), PT_GNU_STACK 8 MiB, 0x100-byte TLS segment, no PT_INTERP |
| allocator | `malloc` == `mi_malloc` (the mimalloc override); no `malloc_common` (libphoenix's `malloc_dl.o`) in the link |
| link contents (checked by `build-wpe.sh`) | `WebKit::WebProcessMain`, `WebKit::NetworkProcessMain`, `g_io_openssl_load`, `g_tls_backend_get_default`, `memfd_create` (shmsrv), Mesa's `eglGetProcAddress` + `dri2_initialize_surfaceless`, `epoxy_static_proc_address`, `wpe_display_wayland_new`, `wpe_display_headless_new`, ICU (`ubrk_open_78`), hb-icu, OpenSSL `SHA256_Init`, libsoup, `nextafterf`; since B6 also `webkit_user_script_new_for_world`, `webkit_cookie_manager_set_persistent_storage`, `WTF::memoryFootprint` |
| configure: public options ON | `ENABLE_PDFJS ENABLE_WPE_PLATFORM ENABLE_WPE_PLATFORM_HEADLESS ENABLE_WPE_PLATFORM_WAYLAND ENABLE_XSLT USE_SKIA_OPENTYPE_SVG USE_WOFF2` |
| build warnings | GCC 16's `-Wsfinae-incomplete` in upstream WTF/WebCore/WebKit headers (as track C); OpenSSL 3 deprecation warnings in PAL/WebCore's OpenSSL code |

Open gaps after B5:
- **libphoenix gaps** found by this link (local shims, the port's `files/compat`):
  - `nextafterf` (B1);
  - `MSG_CTRUNC` (cosmetic);
  - libstdc++'s hidden `<fenv.h>` (toolchain);
  - no POSIX shm, so `memfd_create` comes from the wayland_phoenix compat over shmsrv (see
    [Shared memory](#shared-memory-profile-and-the-b6-decision)).
- `hb_icu_get_unicode_funcs` is **not** in the binary, and that is expected: HarfBuzz uses its
  built-in UCD functions, and WebCore takes only `hb_icu_script_to_script` (linked) from hb-icu.
- **Not linked in:** WebCrypto (off, but its OpenSSL key code is compiled), WebGL, media,
  WebDriver, the inspector server.

## Shared memory: profile and the B6 decision

`WPE_PHOENIX_SHM_LOG=1` makes every process print one line per `WebCore::SharedMemory`
allocation or mapping, and per `wl_shm` pool creation or resize:

```
PHXSHM alloc pid=<pid> fd=<fd> size=<bytes> n=<count> total=<bytes>
PHXSHM map pid=<pid> fd=<fd> size=<bytes> n=<count> total=<bytes>
PHXSHM wlpool pid=<pid> fd=<fd> size=<bytes>
PHXSHM wlpool-resize pid=<pid> fd=<fd> size=<bytes>
```

`n` and `total` count since the process started (frees are not logged). Each line is one shmsrv
object: contiguous, its capacity a power of two of at least 1 MiB (`SHM_MIN_CAP`), one descriptor.

**What B4 and B5 measured** (`b24-wpe`, `b25-b5`):
- the frame buffers: 3 MiB `alloc`s in the WebProcess (2-3 per run), each `map`ped once by the
  UI process, and 2 `wlpool`s of 3 MiB in the UI process;
- Wikipedia over HTTPS (B5): **56 objects in the WebProcess and 53 in the NetworkProcess over the
  whole load**, nearly all small: resource data and IPC payloads between the network and web
  processes, 4-15 KB (about 40 per process), 16-205 KB (5) and 0.7-1.7 MB (9);
- the descriptors are reused all along (`fd=18`, `19`, `21` in the NetworkProcess, whose highest
  is 21, and `32`, `43`, `47` in the WebProcess, highest 55, come back again and again): the
  objects are freed right after use, so only a few are live at any moment.

**Decision for B6: no change** to the shm model (no non-contiguous `memExport`, no sub-allocator).
- The 1024-descriptor ceiling is not near: the highest descriptor seen is 55, and that counts
  every descriptor of the WebProcess.
- The 1 MiB floor turns a 6 KB object into a 1 MiB allocation, but only for its short life.
  ~110 objects per heavy page means ~110 shmsrv round trips and contiguous 1 MiB allocations,
  next to a 21.6 s page load.
- What would change it is the **live** count, which no log line shows yet. The B6 soak logs
  `shmsrv -s` (`SHMSRV stats rc=0 live=<objects> bytes=<bytes> ids=<created>`) every 5 minutes.
  If `live` stays in the dozens or `bytes` keeps growing over 30 minutes, the fix is one of:
  - a smaller floor for objects that never grow: `SHM_MIN_CAP` is phoenix-rtos-devices
    `misc/shmsrv/shmsrv.c` (core), and the floor exists for wl_shm pools that grow by
    `ftruncate()`, so it would become a per-object choice (`memfd_create()` flag or the first
    `ftruncate()` size);
  - a sub-allocator (many small `SharedMemory` objects in one shmsrv object), which needs an
    offset in WebKit's `SharedMemory::Handle` (fd + size today): a WebKit patch.

## Pi checks (pre-registered)

Once the `webkit_wpe` port is in an image, the image build stages everything (USE `rootfs
checks`) and `sync-netboot-tree.sh` copies it to the export. Until then, stage by hand on the
netboot NFS root (the live `fsid=0` export, `/srv/phoenix-rpi4-nfs-gcc16`):
- the program as `/usr/bin/wpe-browser` (mode 755), the bundle as
  `/usr/lib/wpe-webkit-2.0/injected-bundle/libWPEInjectedBundle.so`, the probe extension as
  `/usr/lib/wpe-browser/pi-extensions/phx-probe-extension.so` (the only file in that directory);
- the port's `files/share/browser` as `/bin/browser`, `files/share/wpe-browser.desktop` as
  `/usr/share/applications/wpe-browser.desktop`, `files/share/start.html` and
  `files/checks/{b4.html,b6.sh,b6-sites.txt,b6-newwin.html}` in `/usr/share/wpe-browser/`.

The B6 build of 2026-10-02 is assembled that way in one directory, so one command stages it:
`rsync -a --no-owner --no-group /home/houp/.claude/jobs/c8f1289c/tmp/b6/stage/ /srv/phoenix-rpi4-nfs-gcc16/`
(the unstripped program for `addr2line`: `/home/houp/.claude/jobs/c8f1289c/tmp/b6/wpe-browser.unstripped`).

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
| 2 | `/usr/bin/wpe-browser --headless --cpu-rendering --snapshot=/tmp/b4.png --timeout=600 /usr/share/wpe-browser/b4.html` | in order: `WPEB … start pid=… mode=headless uri=file:///usr/share/wpe-browser/b4.html exe=/usr/bin/wpe-browser`, `WPEB … display WPEDisplayHeadless`, `WPEB … session ephemeral` (since B6), `WPEB … view WPEViewHeadless 1024x768`, `WPEB … role=network pid=…` and `WPEB … role=web pid=…` (either order: the children print them), `PHXSHM …` lines, `WPEB … load committed`, `WPEB … title B4 WPE Phoenix <sum>`, `WPEB … load finished`, `WPEB … snapshot file=/tmp/b4.png width=1024 height=768 crc32=XXXXXXXX`, `WPEB … exit status=0`; back to the prompt |
| 3 | the same command again | the **same** `crc32=` (deterministic rendering) |
| 4 | `/usr/bin/wpe-browser --headless --snapshot=/tmp/b4gpu.png --timeout=600 /usr/share/wpe-browser/b4.html` (Skia GPU raster, Ganesh on V3D) | `exit status=0`; its crc may differ from #2. A failure here with #2 passing is a B7 finding, not a B4 failure |

Decision 5 is "Skia CPU raster first", so the primary check (#2, #3) is `--cpu-rendering`.
Compositing still goes through GLES in both cases.

**Regression gate for every new launcher:** #2 with the new binary gives the B4 checksum,
`crc32=c3e96bf3` (build 24). Headless runs have no chrome overlay and an ephemeral session, so the
B6 shell changes nothing there: a different checksum is a regression.

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

### Injected bundle: the WebProcess loads it and runs its init

Same staging; one run of B4 #2 with the loader trace and the probe extension:

| # | Command at `(psh)%` | Expected |
|---|---|---|
| 1 | `export LD_DEBUG=1 PHX_TRACE_ABORT=1` | — |
| 2 | `/usr/bin/wpe-browser --headless --cpu-rendering --web-extensions=/usr/lib/wpe-browser/pi-extensions --snapshot=/tmp/b4x.png --timeout=600 /usr/share/wpe-browser/b4.html` | `WPEB … web-extensions dir=/usr/lib/wpe-browser/pi-extensions` (UI); after `WPEB … role=web pid=P`, from the WebProcess: `dl: host <argv[0]> exports .dynsym, 7 symbols` (match on `exports .dynsym, 7 symbols`), `dl: loaded /usr/lib/wpe-webkit-2.0/injected-bundle/libWPEInjectedBundle.so base=0x… symbols=5 relocs=3 init=0`, `dl: loaded /usr/lib/wpe-browser/pi-extensions/phx-probe-extension.so base=0x… symbols=6 relocs=4 init=0`, `WPEB-EXT init extension=yes user-data=wpe-browser`, then `WPEB-EXT page-created id=<n>`; then B4's `load finished`, `snapshot … crc32=` and `exit status=0` |

**PASS:**
- the four WebProcess lines appear, in that order;
- **no** `Error loading the injected bundle` and **no** `Error loading WKBundleInitialize symbol`
  warning in any process;
- `exit status=0`, and the `crc32=` equals B4 #2's (the bundle changes no rendering);
- zero faults.

The extension's `init` line can only come from the bundle's `WKBundleInitialize`: it runs
`WebProcessExtensionManager::initialize()`, which loads the extension. So the line proves the
dlopen(), the export table and the init. `page-created` proves the `WebKitWebPage` wrapper the
bundle client creates for the page.

**Triage:**

| Symptom | Meaning |
|---|---|
| B4 #2 regresses with the bundle (a crash, a hang, another `crc32=`) | the bundle is new code in every WebProcess (`WebKitWebPage` and its loader clients). `export WEBKIT_INJECTED_BUNDLE_PATH=/nonexistent` restores the bundle-less WebProcess without restaging: the A/B |
| `dl: host … exports .symtab (file)` or `exports nothing` | the staged `wpe-browser` has no export table: linked without `files/launcher/wpe-browser.exports`, or by a libphoenix without `dl-host-exports` (`build-wpe.sh` refuses both) |
| `Error loading the injected bundle (…): dlopen: cannot open: …` | the bundle is not staged at that path |
| `… dlopen: unresolved symbol: <name>` | `<name>` is missing from the port's `files/launcher/wpe-browser.exports` |
| the bundle line but no `WPEB-EXT init` | the extension directory is wrong or holds no `.so`; a failed `dlopen()` of the extension prints `Error loading module '<path>': <dlerror>` |
| `dl: loaded …phx-probe-extension.so` but no `WPEB-EXT` line | the extension ran, but its output did not arrive: it prints with `g_printerr()` (GLib's print handler, charset conversion), not `fprintf(stderr)` like the launcher. Suspect that before the loader |

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

Scrolling and the keys were left for B6 (below).

**Record:**
- RSS per process;
- the `PHXSHM` profile, with object count per frame;
- time to first `load finished` for each page.

### B6: a usable browser

Stage the B6 build first (see the top of this section). Every check is **one psh command**,
`/bin/bash /usr/share/wpe-browser/b6.sh <mode>` (`sites`, `persist`, `persist-warm`, `soak`, `keys`,
`keys-hid`): the script exports its knobs, runs
`/bin/xfce-session` with the check as the session's autostart and ends the session itself (each
mode has its own `HOLD`; `export B6_HOLD=<s>` overrides it, a `HOLD` exported earlier is not used).
The session ends with `XFCE-SESSION done rc=0` and the script with `B6 <mode> end rc=0`. Lines of
the script start with `B6 `, the browser's with `WPEB `. No Thunar window is opened
(`THUNAR_START=0`).

**Gate before B6:** B4 #2 with the B6 binary prints `crc32=c3e96bf3` (headless = no chrome, an
ephemeral session: the B6 shell must not change a pixel there), and the injected-bundle check
still passes.

#### (a) The B6 sites (`b6.sh sites`, ~23 min, `HOLD` 1320 s)

Six autostart items, one window after another; `WPE_BROWSER_RSS_SECS=60` and
`WPE_PHOENIX_SHM_LOG=1` are exported for all of them:

| # | Item (seconds) | Expected |
|---|---|---|
| 1 | `/bin/bash=/bin/browser` (90) | `WPEB … start … mode=window uri=file:///usr/share/wpe-browser/start.html exe=/usr/bin/wpe-browser`; `WPEB … session persistent data=/root/.local/share/wpe-browser cache=/root/.cache/wpe-browser cookies=/root/.local/share/wpe-browser/cookies.sqlite cookie-policy=no-third-party cache-model=web-browser`; `WPEB … chrome on world=wpe-browser …`; `WPEB … view WPEViewWayland 1280x960`; `load finished uri=file:///usr/share/wpe-browser/start.html`; `title Phoenix-RTOS Web Browser` |
| 2 | Wikipedia (200) | `load finished uri=https://en.wikipedia.org/wiki/Phoenix-RTOS`, `title Phoenix-RTOS - Wikipedia` |
| 3 | GitHub (240) | `load finished uri=https://github.com/phoenix-rtos/phoenix-rtos-kernel`, a title with `phoenix-rtos-kernel` |
| 4 | `phoenix-rtos` (150): plain words | `start … uri=https://html.duckduckgo.com/html/?q=phoenix-rtos`, `load finished uri=https://html.duckduckgo.com/html/?q=phoenix-rtos`, a title with `phoenix-rtos` |
| 5 | Stack Overflow (240) | `load finished uri=https://stackoverflow.com/questions/tagged/rtos`, a title with `rtos` |
| 6 | BBC News (300) | `load finished uri=https://www.bbc.com/news` (or where it redirects), a title with `BBC` |

Each item also prints `mem role=ui|web|network … footprint_kb=` every 60 s and, when the
autostart closes it, `signal quit` and `exit status=0`.

**PASS (a):**
- all six `load finished` lines, with no `load-failed`, `load-failed-tls` or
  `web-process-terminated` for the page itself;
- the HDMI ticks (`artifacts/hdmi/`) show each page rendered: text, layout, images;
- zero `Exception #` / fault dumps; `XFCE-SESSION done rc=0`.

A bot check instead of the page (a Cloudflare "Just a moment..." title on Stack Overflow, a
DuckDuckGo "anomaly" page) is the site's answer to an unknown browser, not a browser failure:
record it and grade that item by its frame. **Record:** `start` → `load finished` per page, the
peak `footprint_kb` per role per page, the `PHXSHM` count per page.

#### (b) Persistence: cookies and the disk cache (`b6.sh persist`, ~6 min, `HOLD` 360 s; then `b6.sh persist-warm` after a reboot)

`persist` removes both directories, then loads Wikipedia twice (two runs of `/usr/bin/wpe-browser`,
100 s each, closed by SIGTERM), listing the files after each run. `sqlite3` reads libsoup's
`moz_cookies` table; `blobs` counts the cache's bodies larger than a page
(`WebKitCache/Version N/Blobs/`) and `blobs_nonzero` how many of (at most) 20 of them hold any
non-zero byte, read back with `read()`, i.e. what is on the disk:

```
B6 persist clear /root/.local/share/wpe-browser /root/.cache/wpe-browser
B6 persist files run=0 cookies_bytes=missing cookie_rows=- cache_files=0 cache_kb= blobs=0 blobs_nonzero=0/0 data=
B6 persist run=1 start url=https://en.wikipedia.org/wiki/Phoenix-RTOS t=…
WPEB t=… session persistent data=/root/.local/share/wpe-browser cache=/root/.cache/wpe-browser …
WPEB t=<T1> load finished uri=https://en.wikipedia.org/wiki/Phoenix-RTOS
B6 persist run=1 end rc=0 t=…
B6 persist files run=1 cookies_bytes=<B> cookie_rows=<R> cache_files=<N1> cache_kb=<K1> blobs=<L1> blobs_nonzero=<Z>/<C> data=cookies.sqlite,…
B6 persist run=2 start …
WPEB t=<T2> load finished uri=https://en.wikipedia.org/wiki/Phoenix-RTOS
B6 persist files run=2 … cache_files=<N2> …
B6 persist done
```

Then reboot (the kernel's page cache of the files is gone) and run `b6.sh persist-warm`: no
clearing, one run (`run=3`, `T3`) on what the disk kept.

**PASS (b):**
- `R >= 1` (Wikipedia sets persistent first-party cookies);
- `N1 >= 10`, `K1 >= 100`, `L1 >= 5` (Wikipedia's styles, scripts and images are blobs) and
  **`Z = C`** (every checked blob has its bytes on the disk: patch 0011);
- **`T2 < T1`** and, after the reboot, **`T3 < T1`** (the run takes `load.php` styles and scripts
  and the images from the disk cache; the HTML itself revalidates); `N2 >= N1`;
- no `session-error`; zero faults.

**Record** T1, T2, T3, N1, K1, L1.

**Discriminators:**
- `Z < C` (blobs of zeros): the bytes went through a file mapping, which Phoenix never writes
  back: a binary without patch 0011 (the B5 one, or a port build without it). `T2 < T1` can then
  still hold within one boot (the second process maps the same kernel page cache), and `T3 ≈ T1`
  after the reboot gives it away; WebKit checks each blob's SHA-1, so it is a cache miss, never
  a wrong page.
- `cookies_bytes` > 0 but `cookie_rows=0`: the jar works but stored no persistent cookie (try
  another site).
- `cache_files=0`: the NetworkProcess wrote no cache at all (look for `WebKitCache/Version …`).
- `database is locked` / `disk I/O error`: SQLite's locking on the NFS root (`fcntl` record
  locks).

#### (c) The 30-minute soak (`b6.sh soak`, ~32 min, `HOLD` 1920 s)

One browser for 30 minutes (`B6_SOAK_SECS`), the next page of `/usr/share/wpe-browser/b6-sites.txt`
(the five B6 sites) every 60 s (`--cycle`), every process's footprint and shmsrv's stats every
5 minutes:

```
B6 soak shm t=0 SHMSRV stats rc=0 live=<L0> bytes=<B0> ids=<I0>
WPEB … cycle pages=5 secs=60 from=/usr/share/wpe-browser/b6-sites.txt
WPEB … cycle n=<k> uri=<site>                      (every 60 s, then that page's load lines)
WPEB … mem role=ui|web|network pid=… footprint_kb=…  (every 300 s, one per process)
B6 soak shm t=… SHMSRV stats rc=0 live=… bytes=… ids=…   (every 300 s)
B6 soak browser alive after 18xx s
B6 soak browser rc=0
B6 soak done
```

**PASS (c):**
- at least 29 `cycle n=` lines, the browser alive at the end, `rc=0`;
- zero `web-process-terminated`, zero faults;
- no unbounded growth: per role, the last `footprint_kb` at most 1.5x the largest of the first
  10 minutes; shmsrv's `bytes` not rising sample after sample, `live` back near `L0`
  (`L0 + 10`) at the samples.

A page that needs more than 60 s on the LLInt simply gets cut by the next cycle: count it
(`load finished` per site), it is a speed finding, not a soak failure. **Record** the footprint
and shm series (the input of the shm decision above).

#### (d) Chrome and keys, scripted (`b6.sh keys`, ~7 min, `HOLD` 420 s)

Synthetic key events (`WPE_BROWSER_AUTO`, sent through `wpe_view_event()`, the seat's path) from
the start page:

| t (s) | Steps | Expected, in order |
|---|---|---|
| 20-24 | `ctrl+l`, type `/usr/share/wpe-browser/b6-newwin.html`, `Return` | `auto key=ctrl+l`, `chrome action=focus-url source=auto`, `auto type=…`, `auto key=Return`, `chrome action=go source=auto input=/usr/share/wpe-browser/b6-newwin.html uri=file:///usr/share/wpe-browser/b6-newwin.html`, `load finished uri=file:///usr/share/wpe-browser/b6-newwin.html`, the page's console line `B6-NEWWIN link focused` |
| 36 | `Return` (into the page: its focused `target=_blank` link) | `new-window uri=file:///usr/share/wpe-browser/b4.html opened=same-view via=policy` (or `via=create`), `load finished uri=file:///usr/share/wpe-browser/b4.html` |
| 50-56 | `ctrl+l`, type `en.wikipedia.org/wiki/Phoenix-RTOS`, `Return` | `chrome action=go source=auto input=en.wikipedia.org/wiki/Phoenix-RTOS uri=https://en.wikipedia.org/wiki/Phoenix-RTOS`, its `load finished` |
| 110-116 | `ctrl+l`, type `phoenix rtos microkernel`, `Return` | `chrome action=go source=auto input=phoenix rtos microkernel uri=https://html.duckduckgo.com/html/?q=phoenix%20rtos%20microkernel`, its `load finished` |
| 170 | `alt+Left` | `chrome action=back source=auto ok=1`, `load finished uri=https://en.wikipedia.org/wiki/Phoenix-RTOS` |
| 220 | `alt+Right` | `chrome action=forward source=auto ok=1`, `load finished uri=https://html.duckduckgo.com/html/?q=phoenix%20rtos%20microkernel` |
| 270-271 | `F5`, `Escape` | `chrome action=reload source=auto uri=https://html.duckduckgo.com/…`, `chrome action=stop source=auto loading=1` |
| 290-322 | `ctrl+l`, `Escape` 32 s later (the toolbar stays up: at least one HDMI tick sees it) | `chrome action=focus-url source=auto`, `chrome action=cancel source=auto` |
| 330 | `alt+Home` | `chrome action=home source=auto uri=file:///usr/share/wpe-browser/start.html`, its `load finished` |
| 360 | `ctrl+q` | `chrome action=quit source=auto`, `exit status=0`, `B6 keys browser rc=0` |

**PASS (d):** every `chrome action=` line of the table, in order, `ok=1` for back and forward; the
`new-window` line and `b4.html` loaded in the same view; an HDMI tick between 290 and 322 s
(ticks come every 25 s) shows the toolbar with the address selected, and ticks during loads show
the thin blue progress line; zero faults. (The typing windows at 20-24, 50-56 and 110-116 s are
too short for the ticks: a frame of them is a bonus.) `stop … loading=0` means the reload had already finished (timing), not a
failure: note it. A missing `new-window` line with `B6-NEWWIN link NOT focused` is the page's
focus, not the new-window path (check it by hand).

**By hand at the Pi** (USB keyboard and mouse): Ctrl+L, typing, Enter; the toolbar appearing at
the top edge, its buttons with the mouse, a click on its address; Alt+Left; scrolling (wheel,
Page_Down, Space); typing into a page field (Wikipedia's search); links; F11.

#### (d2) The same keys end to end (`b6.sh keys-hid`, ~4 min, `HOLD` 260 s)

Needs phoenix-rtos-ports branch `xfce-browser-launcher` (`xfce-desktop.sh` `INPUT_EXTRA`) in the
image. The script empties `/tmp/kbd-inject` before the session, the session adds it as a third
keyboard (`INPUT_EXTRA=/tmp/kbd-inject:keyboard`), and `b6.sh` appends 8-byte HID boot reports
to it: they go libinput-phoenix → labwc → the Wayland seat → WPE, as a USB keyboard's would.
libinput-phoenix writes its raw-mode byte into the file when it opens it, which is both the
handshake and what keeps the reports aligned (they start at offset 1).

```
XFCE start … input=/dev/kbd0:keyboard,/dev/mouse0:mouse,/tmp/kbd-inject:keyboard …
B6 keys-hid inject file opened after <n>s
B6 keys-hid key=ctrl+l t=…                 → WPEB … chrome action=focus-url source=key
B6 keys-hid type=en.wikipedia.org/wiki/Phoenix-RTOS t=…
B6 keys-hid key=Return t=…                 → WPEB … chrome action=go source=key input=en.wikipedia.org/wiki/Phoenix-RTOS uri=https://en.wikipedia.org/wiki/Phoenix-RTOS
                                             WPEB … load finished uri=https://en.wikipedia.org/wiki/Phoenix-RTOS
B6 keys-hid key=alt+Left t=…               → WPEB … chrome action=back source=key ok=1
                                             WPEB … load finished uri=file:///usr/share/wpe-browser/start.html
B6 keys-hid key=ctrl+q t=…                 → WPEB … chrome action=quit source=key, WPEB … exit status=0
```

**PASS (d2):** those lines with `source=key` and the typed `input=` exact. **Triage:** `inject
file NOT-opened`: the image's `xfce-desktop.sh` has no `INPUT_EXTRA`; keys logged by `B6` but no
`chrome` line: the keyboard focus is not on the browser window; a wrong or missing character: the
layout (`us`) or the file's alignment.
