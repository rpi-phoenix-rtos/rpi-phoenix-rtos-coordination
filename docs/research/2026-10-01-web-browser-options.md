# A browser more capable than Dillo on Phoenix-RTOS (XFCE on Wayland): options and plan

**Date:** 2026-10-01. **Status:** research only. No code changed anywhere.
**Owner question:** can we ship something bigger than Dillo on XFCE/labwc: a light browser with
JavaScript, maybe video, and enough of the modern web to use popular sites?

**Short answer:** yes, but only one engine is realistic, and that is **WebKit** (WPE first, then
WebKitGTK). It is a multi-week port with real core work: signals and ucontext, a shared-memory
allocator, and malloc/mutex cost. **NetSurf** is a cheap stopgap that improves on Dillo by a modest,
honest margin. Ladybird and Servo are out today: both now need Rust, and we have no Rust target.

Legend: **[V]** = verified in our tree or on a primary web source today. **[R]** = recalled, not
checked. No browser source is cloned under `external/`, so every engine-internals claim is [R]
unless marked otherwise.

---

## 1. Candidate survey

| Candidate | JS engine | Layout / CSS | Video | TLS | Size (order of magnitude) | License | Toolchain | Process model | Verdict |
|---|---|---|---|---|---|---|---|---|---|
| **Dillo 3.2.0** (shipped) | none | CSS 2.1 subset, no flex/grid | none | mbedTLS 2.28 (TLS 1.2) [V] | 5.8 MB static [V] | GPL-3 | C++/FLTK, X11 | single | baseline |
| **NetSurf 3.11** (GTK3 frontend) | Duktape 2.7 (ES5.1 + bits of ES2015) [V]. DOM bindings are thin: no fetch, partial XHR and events [R] | CSS 2.1 + some CSS3, **flexbox since 3.11** [V], no grid [R] | none | libcurl + OpenSSL [R] | ~300k LoC with its ~12 libs, ~5 MB binary, 30–100 MB RAM [R] | GPL-2 frontend, MIT libs [R], so a port only | C99, custom make buildsystem, `nsgenbind` host tool [R] | single | **Stage 0** |
| **WPE WebKit 2.54** + MiniBrowser / own launcher | JavaScriptCore. No JIT here, so LLInt/CLoop interpreter only (see §2d) | full modern (grid, flex, container queries, web components) | GStreamer only (no FFmpeg backend) [R] | libsoup3 → glib-networking (OpenSSL backend) [R] | Source/ ~3–4 M LoC; ~80–120 MB stripped static [R]; 150–400 MB RAM per web process [R] | LGPL-2.1 / BSD-2 [R], so a port only | C++23 [R], CMake **+ Ninja (required since 2.54)** [V], ruby/perl/python/gperf on host | multi-process: UI + WebProcess + NetworkProcess [R] | **Stage 1–2** |
| **WebKitGTK 2.54** (GTK3, `webkit2gtk-4.1`) + thin shell | same JSC | same | same GStreamer | same | same + GTK glue | same; shells: badwolf BSD-3 [R]; vimb/luakit/Epiphany GPL-3 [R]; surf is X11-only [R] | same | same | **Stage 3** (desktop chrome) |
| **Ladybird** | LibJS. **Its parser and bytecode pipeline are now Rust, on by default (Feb 2026)** [V] | improving fast; GitHub, X and chatgpt.com render (July 2026) [V] | FFmpeg (LibMedia) [R] | OpenSSL + curl RequestServer [R] | ~1 M LoC + Skia, ICU and others [R] | BSD-2 [R] | C++23 **+ Rust** [V], vcpkg deps [R] | multi-process [V] | **Excluded.** Qt6 is the only frontend; the GTK frontend was removed in July 2026 [V]. We have no Qt6 and no `aarch64-phoenix` Rust target. Still pre-alpha [V] |
| **Servo** | SpiderMonkey (mozjs) [R] | good and improving [R] | GStreamer [R] | rustls [R] | large Rust crate graph [R] | MPL-2 | Rust | multi-threaded, optionally multi-process [R] | **Excluded.** Needs a Rust std port, plus libc/mio/nix crate support for Phoenix |
| **ELinks 0.17 / links2** | ELinks: QuickJS or mujs with a tiny DOM [R]. links2: none | text, or graphics on fb/X (links2) | none | OpenSSL | small | GPL-2 | C | single | no gain over Dillo for "popular sites" |
| **litehtml browsers** | none | HTML/CSS 2.1 + flex (a library, not a browser) [R] | none | n/a | small | BSD-3 | C++ | n/a | needs a browser written around it; not worth it |
| QtWebEngine / Falkon / Chromium | V8 (has jitless mode) | full | full | BoringSSL | ~30 M LoC | BSD + LGPL | needs Qt6 + Chromium's own OS layer | multi-process, sandbox mandatory | **Excluded:** porting Chromium to a new OS is the largest possible job |
| Ultralight, Sciter, Ekioh Flow | — | — | — | — | — | proprietary | — | — | **Excluded** (license) |
| Remote rendering (host Chromium streamed over VNC/RDP, or Browsh) | host | full | host | host | tiny on Pi | various | — | — | Out of scope: not a browser *on* Phoenix. Listed only so the owner can reject it explicitly |

Notes that drive the choice:

- **WebKit is the only candidate that is C/C++ only, actively maintained for embedded ARM, and
  already runs on non-Linux POSIX systems.** FreeBSD is an in-tree WebKitGTK target [R], so most
  `#if OS(LINUX)` paths already have a generic UNIX fallback. Haiku keeps an out-of-tree port [R].
- **WebKitGTK 2.54 (2026-09-16) removed cairo 2D rendering; Skia is now the only renderer** [V].
  Skia is vendored under `ThirdParty/` [R], so it is not a separate port.
- **The GTK3 API still exists in 2.54.** The `ON_DEMAND` acceleration policy is deprecated, so GTK3
  builds always use hardware acceleration, though a reworked software mode remains for when no GPU
  is available [V]. The upstream CI is GTK4-first [V], so GTK3 removal in 2027 is a real risk.
- **WPE 2.54 made WPEPlatform the default and stable API.** It has built-in Wayland, DRM and
  headless backends, needs no libwpe or wpebackend-fdo, and Cog is legacy (0.18.x is the last
  stable series) [V]. A launcher is "a few lines" over WPEPlatform [V]. WPE is therefore the
  smallest WebKit to bring up on labwc.
- WebKit has been **unconditionally multi-process** for years; single-process mode was removed [R].

## 2. Phoenix gap analysis: WebKit (WPE, then GTK) and NetSurf

### (a) Cross-process shared memory: why wl_shm works despite P21, and whether WebKit can use it

- **P21 is real and unchanged:** `MAP_SHARED == MAP_PRIVATE == 0x0` in kernel
  `include/mman.h:28-29` [V]. Shared *file* mappings do not exist.
- **wl_shm works because it never uses a shared file mapping [V].** The route is:
  - `wayland_phoenix/files/compat/src/wlphx_memfd.c` implements `memfd_create()`: a devctl to the
    **shmsrv** server (`tools/gpu-lane/weston-drm/shmsrv/shmsrv.c`) returns a fresh id, and the
    client then calls `open("/shm/<id>")`.
  - On the first `ftruncate()`, shmsrv allocates **`MAP_ANONYMOUS | MAP_CONTIGUOUS`** memory and
    `memExport()`s it under the oid `{port,id}`. `memExport` is a kernel syscall; see
    `libphoenix/include/sys/mman.h`.
  - The fd crosses processes over `SCM_RIGHTS` (kernel `posix/fdpass.c`). Any `mmap()` of that oid
    maps the export window, so every process sees the same physical pages. Exported mappings also
    stay shared across `fork()`.
  - `shm_open()` is a compat over the same server (`tools/gpu-lane/labwc-drm/compat/src/lwphx_shm.c`).
    Its names are **per process**, which is fine for the usual create-then-unlink-then-pass-the-fd
    pattern.
- **This mechanism fits WebKit's model in principle.** WebKit's `SharedMemory` on UNIX is
  memfd_create or shm_open+unlink, then ftruncate, mmap, and fd-passing [R]. **The allocation
  model does not fit [V]:**
  - every object is at least **1 MiB**, rounded up to a power of two, and is *physically
    contiguous*;
  - an object is at most 256 MiB, and it cannot grow past its first capacity (`-EFBIG`);
  - seals fail with `EINVAL`; `msync` is a no-op.

  wl_shm uses a few large pools. WebKit creates **hundreds of small objects**: out-of-line IPC
  bodies, ShareableBitmaps, and tiles in software mode [R]. A 1 MiB contiguous floor for each one
  means fragmentation and waste. Every object is also an fd, and `MAX_FD_COUNT` is 1024
  (`posix/posix.c:32`) [V].

  **Fix shape:** let `memExport` accept non-contiguous anonymous pages (kernel), or add a
  sub-allocating shm compat. The kernel change is the right one and is close to closing P21 for
  anonymous memory. Estimate: 3–6 agent-days.

### (b) SCM_RIGHTS, socketpair, SEQPACKET, peer credentials

`SCM_RIGHTS` is implemented in `posix/fdpass.c`. `socketpair()`, `SOCK_SEQPACKET` and `SOCK_DGRAM`
are accepted for AF_UNIX (`posix/usocket.c:460,489`). `SO_PEERCRED` is defined
(`include/posix-socket.h:62`, `usocket.c:133`). All [V]. WebKit IPC uses `SOCK_SEQPACKET`
socketpairs plus fd attachments on non-Darwin systems [R]. **No gap expected**; it needs a stress
test (large attachment counts).

### (c) Helper processes

- `fork`, `vfork` and `execve` exist; **`posix_spawn` does not** (`libphoenix/include/unistd.h:128`
  lists it as unclaimed) [V].
- GLib 2.88's `g_spawn` falls back to fork+exec [R]. It already launches `xfconfd` through D-Bus
  activation in our XFCE session [V, M7 doc]. WebKit's `ProcessLauncherGLib` sits on GSubprocess
  [R], so **no gap expected**.
- The sandbox (bubblewrap/seccomp) must be built `OFF`.
- **Exec text is shared [V].** `process_load` maps PT_LOAD from the file's vm object
  (`proc/process.c:598`), and `vm_objectGet` finds an existing object by oid in a global tree
  (`vm/object.c:85`).
  - So the plan is **one static multi-call ELF** (UI + WebProcess + NetworkProcess, dispatched on
    `argv[0]` or a flag). Its text is paged in once and shared, instead of three ~100 MB
    executables.

### (d) Executable memory and JIT

- `mprotect` can never add a permission that was not in the mapping's original protection
  (`vm/map.c:1153`, `map_checkProt` → `-EACCES`) [V].
- RWX requested **at `mmap` time** is honoured: the Quake3 JIT precedent ran generated code [V,
  memory].
- **Stage plan:** `ENABLE_JIT=OFF` with the CLoop interpreter, or the asm LLInt plus
  `JSC_useJIT=0` [R]. Expect JS **≈3–10× slower** than with JSC's baseline/DFG JIT on the A72 [R,
  estimate].
- A later JIT stage would need JSC's ExecutableAllocator to use one RWX region made at mmap time
  [R], plus `ENABLE_WEBASSEMBLY=OFF` until then.
- The same rule hits **reserve-then-commit allocators**: `mmap(PROT_NONE)` followed by
  `mprotect(RW)` fails. JSC's `OSAllocator` takes that path only for `OS(LINUX)`; other UNIX
  systems reserve RW [R].
- bmalloc/libpas also want `madvise`, which libphoenix lacks [V]. So stage 1 uses
  `USE_SYSTEM_MALLOC=ON` [R]. That leads straight to (e).

### (e) Threads, locks, signals and TLS. This is the hardest part

- **Signals are the JSC blocker [V]:**
  - `SA_SIGINFO` and `SA_ONSTACK` are marked `FIXME: implement` (kernel `include/signal.h:76-82`);
    there is no `sigaltstack`;
  - `libphoenix/signal/signal.c:34` says "Phoenix delivers no ucontext to handlers";
  - there is no `ucontext_t` anywhere in the headers.

  JSC suspends mutator threads with `pthread_kill` (present [V]) and a handler that reads registers
  from `ucontext_t`, so it can scan their stacks conservatively [R]. **Workaround:**
  `JSC_useConcurrentGC=false`, which runs collection on the mutator thread with stop-the-world
  pauses [R].

  **Real fix:** deliver a `ucontext_t` (pc, sp, x0–x30) to `SA_SIGINFO` handlers. The starting
  point exists: the aarch64 trampoline already stashes the interrupted `cpu_context_t*` in a
  process-global `_dbg_signal_ctx` for libdbg [V]. This is core kernel and libphoenix work,
  estimated at 3–5 agent-days.
- **`pthread_getattr_np` is absent [V].** WTF's `StackBounds` needs the current thread's stack
  base and size. A small libphoenix addition.
- **Mutex cost (P24) [V]:** every `mutexLock` is a syscall, which is **9.1 µs per malloc+free pair
  in a multithreaded program**. WebKit runs 20–40 threads per process [R] and, with system malloc,
  performs millions of mallocs per page load. **This alone could make page loads tens of seconds.**
  - WTF's own `Lock` is a userspace CAS with ParkingLot [R], so WebKit's internal locks are fine;
    malloc and GLib are not.
  - Fix: P24's user-space mutex fast path (already an **owner decision** in KNOWN-ISSUES), or a
    per-thread-cache allocator (mimalloc) linked into the browser.
- **TLS:** static local-exec `__thread` works [V, memory]. No dynamic TLS, which is fine for a
  static build.
- **POSIX semaphores:** no `sem_*` in libphoenix [V]; labwc carries a compat (`lwphx_sem.c`).
  WebKit barely uses them [R].

### (f) Static vs dynamic linking

Userspace is 100% static. Phase A `dlopen` (plugins resolved against the host's `.symtab`) exists;
Phase B (shared libs, `PT_INTERP`, dynamic TLS) does not [V, memory]. WebKit can be linked
statically (PlayStation-style) [R], and GStreamer plugins can be registered statically [R].
**No dlopen needed.** The cost is link time and RAM on the build host for a ~100 MB static link.

### (g) Dependencies (ports tree checked [V])

| Have (version) [V] | Missing for WebKit [R] | Missing for NetSurf [R] |
|---|---|---|
| GTK 3.24.52 (Wayland only, **built without EGL**: `gtkphx_noegl.c`), GLib 2.88.3, pango 1.54, cairo 1.18.4, harfbuzz 14.4 (**`HB_HAVE_ICU=OFF`**), fribidi, gdk-pixbuf, atk, libepoxy, freetype 2.13.2, fontconfig 2.14.2, libxml2 2.15.4 (inside a private labwc/atril prefix, not a shared port), sqlite 3.53.4, libjpeg(-turbo) 3.0.4, libpng 1.6.40, zlib, libffi, expat, openssl 3.5.9, Mesa 26.2 GBM/EGL/GLES 3.1 + EGL-wayland + dma-buf, libdrm_phoenix, libxkbcommon 1.13, wayland 1.24, FFmpeg 6.1 (h264/hevc/vp8/vp9/opus/aac decoders, LGPL), D-Bus | **ICU4C** (hard dependency of JSC and WebCore; ~30 MB data, filterable to ~10 MB), **libsoup3** + **glib-networking** (OpenSSL backend, which avoids gnutls) + **libpsl** + **nghttp2**, **libwebp**, **woff2 + brotli**, libxslt (or `ENABLE_XSLT=OFF`), libgcrypt/libtasn1 (or `ENABLE_WEB_CRYPTO=OFF`), harfbuzz rebuilt with ICU, unifdef/gperf (host). Media: **GStreamer** core/base/good + gst-libav (+ a Phoenix audio sink). OFF in stage 2: libmanette, libsecret, enchant, hyphen, avif/jxl, lcms2, WebRTC (disabled upstream in 2.54 anyway [V]) | the NetSurf libs (libparserutils, libwapcaplet, libhubbub, libdom, libcss, libnsutils, libnsbmp, libnsgif, libsvgtiny, libnspsl, libnslog, libutf8proc, nsgenbind) [R]. **curl is 7.64.1 on mbedTLS 2.28** [V], so it needs a modern curl on OpenSSL 3.5.9 (NetSurf wants the OpenSSL curl backend for its certificate viewer [R]) |

Precedent that large C++ ports go through here: Poppler 26.09 (atril), Mesa 26.2 and SuperTuxKart,
all built static [V].

### (h) C++ level

The toolchain is GCC 16.2 [V, memory]: full C++23 and most of C++26. libstdc++'s ~30 compiled-out
features (chrono resolution, `hardware_concurrency`, `random_device`, lstat) were fixed by
`phoenix-rtos-build` 95d9fca, which is on master [V]. WebKit requires C++23 / GCC ≥ 12-class [R],
so **no gap**.
`<fenv.h>` is still a stub [memory]. JSC uses `fesetround` in a few places [R], so this needs a
check at stage 1.

## 3. Recommendation and staged plan

**Go with WebKit, staged so the riskiest unknowns are measured before WebCore is touched. Put
NetSurf in front as a cheap, honest stopgap.** Effort is in agent-days of the kind this project has
logged. Variance grows by stage.

| Stage | What | Gate (pass criteria) | Effort |
|---|---|---|---|
| **0** | **NetSurf 3.11, GTK3 frontend** on our Wayland GTK, plus modern curl/OpenSSL, as an XFCE menu entry | Wikipedia, HN, MDN and lite.cnn.com render with CSS/flex and HTTPS (TLS 1.3) | **3–5 days** |
| **1** | **JSC alone:** ICU4C port, WTF/JSC on a Phoenix OS target (FreeBSD/UNIX fallbacks), CLoop, `USE_SYSTEM_MALLOC`, `pthread_getattr_np`, no concurrent GC; `jsc` shell on the Pi | test262 subset + SunSpider/JetStream-lite run; this measures interpreter speed and malloc/P24 cost. **Decision point:** if malloc dominates, fix P24 or link mimalloc before stage 2 | **1.5–2.5 weeks** |
| **2** | **WPE WebKit 2.54 + MiniBrowser (or our ~300-line WPEPlatform launcher)** on labwc: libsoup3/glib-networking/libpsl/nghttp2/libwebp/woff2/brotli, Skia, one multi-call static ELF, **non-contiguous `memExport`** (or a shm sub-allocator), EGL/dma-buf compositing with the software path as fallback; video, WASM, WebCrypto, XSLT and sandbox OFF | Wikipedia, GitHub, DuckDuckGo/Google search, a news site and Stack Overflow load and scroll; 3 processes, 0 faults, RSS logged | **4–8 weeks** |
| **3** | **WebKitGTK 2.54 GTK3 (4.1 API) + a thin shell** (badwolf, BSD-3, or our own GTK3 shell with tabs, URL bar, downloads). GTK3 must be rebuilt **with EGL**, or accept the software mode | it is a usable desktop browser under XFCE | **1–2 weeks** |
| **4** | **Video:** port GStreamer core/base/good + gst-libav over our FFmpeg 6.1 + a `/dev/audio0` sink; then MSE. Alternative: a custom `MediaPlayerPrivate` over FFmpeg, which is less code but off-upstream | `<video>` H.264 720p plays in a page; then YouTube via MSE | **2–4 weeks** (+MSE 1–2) |
| **5** (optional) | **JIT:** RWX ExecutableAllocator, `SA_SIGINFO`/ucontext (also enables concurrent GC and WASM) | JetStream ≥3× stage 1 | **1–3 weeks** |

**What "popular websites" will realistically do:**

- **NetSurf (stage 0):**
  - **Works:** reading-oriented and server-rendered sites: Wikipedia, HN, old-style forums, docs,
    MDN, lite news, and GitHub file views (partly).
  - **Doesn't work:** anything that is a JS app: Gmail, YouTube, X, new Reddit, Maps, most logins.
    It is better than Dillo (flex, images, modern TLS), but it is the same class of browser.
- **WebKit without JIT (stages 2–3):**
  - **Works, slowly:** almost everything that is HTML + JS without heavy computation. Wikipedia,
    GitHub (browse, issues, PRs), Stack Overflow, search, news (ads hurt), Reddit and Gmail usable
    with a lag of a few seconds.
  - **Doesn't work:** Google Maps/streets.gl (WebGL is possible on GLES 3.1 later, but too slow
    with no JIT), WASM apps (WASM is off), YouTube (until stage 4, and even then 480–720p at best
    on the A72), and DRM services (Netflix, Spotify) **never**, because Widevine is proprietary.
- **Ladybird:** revisit in 2027 only if Phoenix gains a Rust target *and* Qt6. Neither is planned.

**Biggest risks (ordered):**

1. **The size of the OS port**: WTF, JSC and WebKit platform code with no `OS(PHOENIX)`. The
   FreeBSD paths help, but this is where the 4–8-week variance in stage 2 comes from.
2. **Signals/ucontext** for JSC's GC. Stage 1 isolates it, and a workaround exists.
3. **Malloc/mutex cost (P24)** under a 30-thread browser. Stage 1 measures it, and it may force the
   P24 owner decision.
4. **shmsrv's 1 MiB contiguous floor** under WebKit's many small objects. It is a kernel change;
   also watch the 1024-fd ceiling.
5. **Upstream drift.** GTK3 may be dropped soon, which is why WPE goes first. Each WebKit bump is a
   multi-hour host build; pin a release.
6. **No video path without GStreamer.** "Maybe video" is a separate 2–4-week project.

**Ask of the owner:**

- (1) approve stage 0 (NetSurf) now;
- (2) approve stage 1 as a time-boxed probe, ≤2.5 weeks, ending in a go/no-go on stage 2;
- (3) note that stages 1–2 touch **core**: signals/ucontext, `pthread_getattr_np`, non-contiguous
  `memExport`, and possibly the P24 mutex fast path.

Sources: [WebKitGTK 2.54.0 release](https://webkitgtk.org/2026/09/16/webkitgtk2.54.0-released.html) ·
[WebKitGTK 2.54 highlights](https://webkitgtk.org/2026/09/16/webkitgtk-2.54-highlights.html) ·
[WebKitGTK soup2 sunset](https://webkitgtk.org/2025/10/07/webkitgtk-soup2-deprecation.html) ·
[WebKitGTK 2.52 highlights](https://webkitgtk.org/2026/03/18/webkitgtk-2.52-highlights.html) ·
[WPE WebKit 2.54.0](https://wpewebkit.org/release/wpewebkit-2.54.0.html) ·
[WPE 2.54 highlights](https://wpewebkit.org/blog/2026-09-16-wpewebkit-2.54.html) ·
[Ladybird July 2026 newsletter](https://ladybird.org/newsletter/2026-07-31/) ·
[Ladybird moves to Rust (The Register, 2026-02)](https://www.theregister.com/software/2026/02/23/ladybird-indie-web-browser-flutters-toward-rust/4657323) ·
[NetSurf news / 3.11](https://www.netsurf-browser.org/about/news.html) ·
[NetSurf (Wikipedia)](https://en.wikipedia.org/wiki/NetSurf).
In-tree evidence: the file and line references above, plus `docs/KNOWN-ISSUES.md` (P21, P24),
`docs/gpu-new-lane/M7-wayland-desktop.md` and `docs/gpu-new-lane/M10-video-player.md`.
