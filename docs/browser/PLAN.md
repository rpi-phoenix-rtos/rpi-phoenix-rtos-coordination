# A WebKit browser on Phoenix-RTOS — plan and status

**Owner directive, 2026-10-01:** skip NetSurf and take the WebKit path. The goal is a browser on the XFCE/labwc desktop with:
- JavaScript, eventually with a JIT;
- video playback, with H.265 routed to the hardware decoder from the players work;
- as a bonus, GPU-accelerated rendering and WebGL through our Mesa.

Deliver step by step: a simple variant first (JS interpreter, no video, minimal shell), then iterate. Execute autonomously up to a working browser.

Background and gap analysis: [web-browser-options study](../research/2026-10-01-web-browser-options.md).

## Decisions (fixed here, not left to individual tracks)

1. **Engine and order.**
   - First **WPE WebKit 2.54** (WPEPlatform's Wayland backend, a window under labwc), with a small launcher of our own.
   - Then **WebKitGTK 2.54** (GTK3) plus a thin GTK shell, for the full desktop browser. Optional once WPE works.
   - Pin one release (2.54.x) and never chase upstream mid-stage.
2. **One static multi-call ELF.**
   - The UI, WebProcess and NetworkProcess are one binary, dispatched on `argv[0]`/flag.
   - Exec text is shared across processes on Phoenix (`proc/process.c:598`, `vm/object.c:85`), so ~100 MB of text is paged in once.
   - No `dlopen` is needed.
3. **Develop outside the ports framework until a stage passes.** A recipe change to a WebKit port would rebuild everything downstream for hours, and a build copies `sources/` at start.
   - Like the GPU lane: standalone scratch builds against the sysroot, under `tools/browser/<component>/` (scripts + patches) and an agent's scratch dir.
   - Hand-staged onto the NFS export for Pi gates.
   - Wrapped as a `phoenix-rtos-ports` port (`webkit_wpe`, …) only when its stage passes.
   - The *dependencies* (ICU, libsoup3, …) are ordinary ports from the start, since they are small.
4. **Malloc / P24.** **Owner approved P24 on 2026-10-01**: the user-space mutex fast path is being implemented as its own track (session task #99).
   - The browser links a per-thread-cache allocator, **mimalloc**, into its own binary.
   - WTF's own locks are already user-space (ParkingLot).
   - Stage 1 measures malloc cost in the browser workload with and without P24.
5. **GPU.**
   - Stage-2 fallback: software rendering (Skia CPU + wl_shm).
   - Target: **EGL / dma-buf** via Mesa's EGL-wayland platform (works since M8). Skia Ganesh on GLES 3.1 gives accelerated compositing.
   - **WebGL** comes through ANGLE on GLES 3.1.
   - **WebGPU: no.** WebKit's implementation targets Metal/Dawn, and V3D 4.2 has no path to it.
6. **Video, stage 4.** Choose then between two options:
   - (a) GStreamer + gst-libav + a `/dev/audio0` sink;
   - (b) a custom `MediaPlayerPrivate` over our FFmpeg 6.1, reusing the **`hevc_rpivid` hardware decoder** from the players work.

   Option (b) is less code and reuses the HEVC hardware path directly; (a) is upstream-shaped and brings MSE (YouTube).
7. **JIT, stage 5.** Needs:
   - one RWX `ExecutableAllocator` region made at `mmap` time; `mprotect` cannot add PROT_EXEC later (`vm/map.c:1153`), but RWX at `mmap` works, per the Quake3 JIT precedent;
   - `SA_SIGINFO` + `ucontext` delivery. That is the B4 track, which also enables JSC's concurrent GC and later WASM.

## Milestones

| # | What | Gate (pre-registered in `docs/browser/<id>.md` before its first Pi cycle) | Status |
|---|---|---|---|
| **B0** | Dependencies as ports: **ICU4C** (filtered data), **libsoup3 + glib-networking** (OpenSSL backend) **+ libpsl + nghttp2**, **libwebp**, **woff2 + brotli**, harfbuzz rebuilt **with ICU**, libxslt (or OFF), gperf/unifdef on the host | Each port builds in the image build; a small per-port Pi smoke (e.g. `uconv`/ICU collation sample, a libsoup3 HTTPS GET) | ▶ tracks A1, A2 started 2026-10-01 |
| **B1** | libphoenix gaps: `pthread_getattr_np`, `madvise` (ENOSYS-safe/no-op semantics as documented), `<fenv.h>` `fesetround`/`fegetround` check, `posix_memalign`/`aligned_alloc` audit; **libphoenix tests for each** | libc suite green on the Pi; new tests can fail on the old code | ▶ track B started |
| **B2** | Kernel + libphoenix **`SA_SIGINFO` + `ucontext_t`** (pc, sp, x0–x30) delivered to handlers; `sigaltstack` if cheap | `--scope core`, full libc suite incl. new signal tests, showcase 7/7; branch until JSC consumes it | ▶ track B4 started |
| **B3** | **JSC alone**: WTF + JavaScriptCore on an `OS(PHOENIX)` target (FreeBSD fallbacks), CLoop, mimalloc, `JSC_useConcurrentGC=false`, JIT/WASM off; `jsc` shell | **Go/no-go for everything after.** On the Pi: `jsc -e 'print(1+1)'`, a test262 subset pass count, a SunSpider/JetStream-lite score, RSS, and a malloc-rate micro-measurement in the same boot | ▶ track C started (needs ICU) |
| **B4** | Headless WebCore: WPE WebKit renders a page to a buffer, software mode, single process | page checksum vs the same build on the host / screenshot by eye | — |
| **B5** | **First page in a labwc window** (WPEPlatform Wayland, wl_shm), 3 processes (multi-call ELF), launcher `/bin/browser` | Wikipedia + a news page load and scroll; HDMI frame by eye; 0 faults; RSS logged | — |
| **B6** | Usable browser: libsoup3 HTTPS + cookies + disk cache; shm model fixed for WebKit's many small objects (non-contiguous `memExport`, or an shm sub-allocator — decided after B3/B4 numbers); XFCE menu entry; URL bar/back/reload in the launcher | Wikipedia, GitHub, DuckDuckGo, Stack Overflow, a news site; 30-min soak, 0 faults | — |
| **B7** | **GPU**: EGL/dma-buf compositing (Skia Ganesh on GLES 3.1) + **WebGL** (ANGLE on GLES) | a WebGL sample renders; fps vs software mode | — |
| **B8** | **Video**: H.264/HEVC `<video>`, HEVC on the hardware (`hevc_rpivid`) | a 720p H.264 and an HEVC clip play in a page | — |
| **B9** | **JIT** (Baseline/DFG; needs B2) + concurrent GC on | JetStream-lite ≥ 3× B3 | — |
| **B10** | WebKitGTK + GTK shell (tabs, downloads) under XFCE; docs (USER-GUIDE browser section, CHANGES, README) | owner-usable desktop browser | — |

## Tracks (subagents write code on worktrees and scratch builds; only the coordinator builds images and runs Pi cycles)

| Track | Scope | Started |
|---|---|---|
| A1 | ICU4C 7x port (+ filtered data), harfbuzz with ICU | 2026-10-01 |
| A2 | libsoup3 + glib-networking (OpenSSL) + libpsl + nghttp2 + libwebp + woff2 + brotli (+ libxslt) ports | 2026-10-01 |
| B | libphoenix gaps (B1) with tests | 2026-10-01 |
| B4 | SA_SIGINFO / ucontext (B2) | 2026-10-01 |
| C | WTF + JSC standalone port → `jsc` shell (B3) | 2026-10-01 |

## Risks (from the study, ordered)

1. **Porting WebKit's platform layer** to a new OS (no `OS(PHOENIX)`). The FreeBSD paths help. This is where the variance in stages B4–B6 comes from.
2. **Signals/ucontext for JSC's GC.** B2 fixes it; `JSC_useConcurrentGC=false` is the workaround.
3. **Malloc cost under a 30-thread browser.** P24 (approved) plus mimalloc; B3 measures it.
4. **shmsrv's 1 MiB contiguous floor and the 1024-fd ceiling** against WebKit's many small shared objects (B6).
5. **Upstream drift.** Pin 2.54.x.
6. **Host build time and RAM.** The build host has 29 GiB of RAM and 16 threads, which is enough for a ~100 MB static link.

## Log

- 2026-10-01: plan written; tracks A1, A2, B, B4, C started. Owner: P24 approved (track started), new syscalls may be appended, RWX for the JIT allowed, P21 later.
