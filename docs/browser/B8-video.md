# B8 — `<video>` in pages: H.264 and HEVC, HEVC on the hardware decoder

PLAN row B8, decision 6. Written 2026-10-02 (agent). Status: **designed, option (b) coded on
ports branch `webkit-wpe-b8`, every new and every touched media TU compiled for aarch64-phoenix;
no image build, no Pi cycle yet.** The Pi gate is pre-registered in §6.

## 1. Decision

**Option (b): a WebCore media engine of our own over FFmpeg 6.1 (`MediaPlayerPrivateFFmpeg`,
CMake option `USE_FFMPEG`), behind the `webkit_wpe` USE flag `video`.** HEVC goes to
`hevc_rpivid` because `avcodec_find_decoder(AV_CODEC_ID_HEVC)` returns it, exactly as in ffplay
and gtk-video. GStreamer (option a) stays the later path if Media Source Extensions (YouTube),
Web Audio or WebRTC become goals (§3.4).

Why, in one paragraph: in WPE 2.54 nothing in WebCore needs GStreamer for `<video>`; one CMake
line forbids it (§2). With that line made conditional, the engine is ~2.2 k lines of WebKit-style
C++ next to 3 existing generic pieces (the engine registry, the compositor's buffer proxy, the
layer buffer types). It reuses the FFmpeg build that already plays these clips on the Pi
(`video_player`, M10) with the hardware HEVC decoder that is already bit-exact (M10b), and the
A/V scheme already proven in `gtk-video`. GStreamer would be 5 new meson modules built static as
`gstreamer-full`, a new audio sink element, and a decoder-ranking question for `hevc_rpivid`,
and its no-GL frame path converts every picture to BGRA on the CPU (§3.1). The one thing (a)
uniquely brings, MSE, is of little use before the JIT is in the WebProcess (B9 is jsc-only so far).

## 2. What WPE 2.54 supports (read from the tarball tree, patches 0001-0015 applied)

| Fact | Source |
|---|---|
| Media engines WebCore can register: AVFoundation (Cocoa), GStreamer + GStreamer MSE, MediaFoundation (Windows), **HolePunch (`USE_EXTERNAL_HOLEPUNCH`, no GStreamer)**, WirelessPlayback | `Source/WebCore/platform/graphics/MediaPlayer.cpp:323-364` (`buildMediaEnginesVector`) |
| **The only coupling of video to GStreamer is a CMake rule**: `WEBKIT_OPTION_DEPEND(ENABLE_VIDEO USE_GSTREAMER)` (also Web Audio, WebCodecs, speech) | `Source/cmake/GStreamerDependencies.cmake:2` (included by `OptionsWPE.cmake:169`) |
| `platform/GStreamer.cmake` adds `SourcesGStreamer.txt` and GStreamer's include dirs on `ENABLE_VIDEO OR ENABLE_WEB_AUDIO`, without a `USE_GSTREAMER` term (needs that term with another engine) | `Source/WebCore/platform/GStreamer.cmake:3`, `:82` |
| Generic code has non-GStreamer, non-Cocoa fallbacks: `VideoFrame` (`!PLATFORM(COCOA) && !USE(GSTREAMER)`), the media-capabilities factory list may be empty, `PlatformMediaSessionManager::create` for GLib without MEDIA_SESSION, MIME/ImageDecoder hooks guarded `USE(GSTREAMER) && ENABLE(VIDEO)` | `platform/VideoFrame.cpp:35,81`; `platform/mediacapabilities/PlatformMediaEngineConfigurationFactory.cpp:61-69`; `platform/audio/PlatformMediaSessionManager.cpp:57`; `platform/MIMETypeRegistry.cpp:461`; `platform/graphics/ImageDecoder.cpp:109` |
| The WebKit layer only passes GStreamer options when `USE(GSTREAMER)` | `WebKit/WebProcess/glib/WebProcessGLib.cpp:244`, `WebKit/UIProcess/glib/WebProcessPoolGLib.cpp:228` |
| A static GStreamer is supported upstream: `USE_GSTREAMER_FULL` (`gstreamer-full-1.0`) | `Source/cmake/GStreamerChecks.cmake:2-9`, `WebKit/PlatformWPE.cmake:499` |
| GStreamer minimum 1.18.4; with video: app, pbutils, video, tag (+ mpegts, gl optional); Web Audio: audio, fft | `GStreamerChecks.cmake:10-45` |
| `ENABLE_MEDIA_SOURCE` is a WebKit feature (default ON for GStreamer ports) whose only GLib backend is `MediaPlayerPrivateGStreamerMSE` | `GStreamerDefinitions.cmake:4`; `MediaPlayer.cpp:345-347` |
| Frame path into compositing: `CoordinatedPlatformLayerBufferProxy::setDisplayBuffer()` is thread-safe and requests a composition (`CompositionReason::VideoFrame`); the layer buffer types RGB, YUV (planar/semi-planar GL textures), DMABuf, ExternalOES, HolePunch exist **without** GStreamer (`USE(COORDINATED_GRAPHICS)` only) | `texmap/coordinated/CoordinatedPlatformLayerBufferProxy.cpp:84-103`; `platform/TextureMapper.cmake:76-82` |
| **WPE 2.54 composites with Skia by default** (`UseSkiaForComposition`, WebKit default `true`; `WEBKIT_USE_SKIA_FOR_COMPOSITION=0` switches to TextureMapper): a video layer buffer is drawn through its `skiaImage()` | `WTF/Scripts/Preferences/UnifiedWebPreferences.yaml:9342-9352`; `skia/SkiaCompositingLayer.cpp:795`; `WebPage/CoordinatedGraphics/DrawingAreaCoordinatedGraphicsGLib.cpp:144-147` |
| GStreamer's non-GL video path: the sink caps are `BGRx`/`BGRA` (a CPU `videoconvert`), mapped and uploaded as one RGB texture in the compositor | `gstreamer/VideoSinkGStreamer.cpp:46-51`; `CoordinatedPlatformLayerBufferVideo.cpp` (`createBufferFromMappedFrameIfNeeded`) |
| Autoplay: the WPE API's default policy is `WEBKIT_AUTOPLAY_ALLOW_WITHOUT_SOUND` | `WebKit/UIProcess/API/glib/WebKitWebsitePolicies.cpp:134-139` |

So: **a non-GStreamer backend is buildable** — proven below by compiling, with
`ENABLE_VIDEO=ON USE_GSTREAMER=OFF USE_FFMPEG=ON`, the 3 new TUs and every WebCore unified
bundle that contains media code (§5.3). One latent upstream bug surfaced on the way
(`VideoFrame.cpp`'s fallback returns `RefPtr<NativeImage>` without including `NativeImage.h`).

## 3. The two options

### 3.1 (a) GStreamer + gst-libav + a `/dev/audio0` sink

- **Ports (GLib 2.88 is gtk3_wayland's private one, as for webkit_deps; never the old glib2 2.56):**
  GStreamer core, gst-plugins-base (playbin/uridecodebin/decodebin, typefind, app, audioconvert,
  audioresample, videoconvert(scale), volume; libs app, pbutils, video, audio, tag), gst-plugins-good
  (isomp4 = qtdemux, matroska, audioparsers, autodetect), gst-plugins-bad (videoparsersbad:
  h264parse/h265parse, which decodebin3/parsebin and the MSE player want), gst-libav (FFmpeg's
  decoders, linked against video_player's libraries), optionally orc (without it videoconvert and
  audioconvert run plain C). **5–6 tarballs**, best as one `gstreamer` port built the monorepo way
  into a static `gstreamer-full-1.0` (Phoenix has no shared objects: plugins registered statically,
  no registry, no `gst-plugin-scanner`), plus a **new audio sink element** for `/dev/audio0`
  (`GstAudioSink` subclass, ~300 lines, ranked for `autoaudiosink`, `GStreamerCommon.cpp:1198`).
- **Frame path:** without GstGL, every picture is converted to BGRA by `videoconvert` on the CPU and
  uploaded as 4 bytes/pixel (8.3 MB per 1080p frame). Zero-copy needs `USE_GSTREAMER_GL` = the
  gst-gl library on our EGL/GLES 3.1 (surfaceless; another porting front) or dma-buf (`USE_GBM`,
  off here; B7).
- **A/V sync:** GStreamer's own (pipeline clock, the audio sink as the clock provider) — the
  upstream-quality part of this option.
- **HEVC on hardware:** gst-libav wraps each libavcodec decoder as `avdec_<name>`; whether
  `avdec_hevc_rpivid` is created and outranks `avdec_h265` is **unverified** (gst-libav's skip
  lists and rank table; `GST_PLUGIN_FEATURE_RANK` can force it). Re-verify in gst-libav's
  `ext/libav/gstavviddec.c` before relying on it.
- **Licence:** core/base/good/bad/libav are LGPL-2.1-or-later; the GPL plugins (gst-plugins-ugly's
  x264, -bad's faad/dts/mpeg2enc/resindvd/x265…) stay off (`-Dgpl=disabled`, the default). All of
  it lives in ports, statically linked into wpe-browser like WebKit (LGPL) and GLib already are:
  no new kind of obligation, nothing GPL, nothing in a core repo.
- **Brings:** MSE (YouTube and most adaptive players), Web Audio, WebCodecs, MediaRecorder,
  WebRTC later — all upstream-shaped.
- **Effort:** 6–10 agent-days to a first Pi pass (the research note said 2–4 weeks incl. MSE),
  then a WebKit rebuild (~2 h). Risk: GStreamer's thread/clock/poll machinery on Phoenix
  (GstPoll, GstSystemClock, many streaming threads), meson cross builds of 5 modules.
- Re-verify: GStreamer's current stable series (1.26 in 2026-10?), `gst-full` meson options, and
  gst-libav's FFmpeg 6.1 support — from memory, not read.

### 3.2 (b) `MediaPlayerPrivateFFmpeg` over FFmpeg 6.1 (chosen; code on `webkit-wpe-b8`)

- **Engine:** `Source/WebCore/platform/graphics/ffmpeg/` (WebKit patch 0030 in `patches/webkit-video/`, BSD-2-Clause):
  - `MediaPlayerPrivateFFmpeg` — registered by `MediaPlayer.cpp` under `USE(FFMPEG)`; a demux
    thread (libavformat) → two packet queues → a video thread and an audio thread; the
    `MediaPlayerPrivateInterface` state machine (HaveNothing → HaveMetadata at the container's
    metadata → HaveEnoughData at the first picture/sound; seek promises resolved at the first
    picture after the target; end → `timeChanged()`, looping left to HTMLMediaElement).
  - `FFmpegMediaStream` — the bytes: `file://` read directly (`pread`), everything else through
    WebKit's media resource loader (the NetworkProcess; Range requests on seek, the request
    stopped while 16 MiB wait unread), the way GStreamer's `webkitwebsrc` works. FFmpeg itself
    stays `--disable-network`.
  - `CoordinatedPlatformLayerBufferFFmpeg` — one decoded picture (an `AVFrame` reference) handed
    to `CoordinatedPlatformLayerBufferProxy::setDisplayBuffer()` from the video thread.
- **Frame path:** decoder (8-bit 4:2:0 planes in system memory; `hevc_rpivid` de-tiles SAND128
  into them) → the compositing thread uploads the three planes and converts on the GPU:
  - Skia composition (default): `SkImages::TextureFromYUVAPixmaps()` over the planes, with the
    frame's colour space (BT.601/709/2020, limited/full range);
  - TextureMapper (`WEBKIT_USE_SKIA_FOR_COMPOSITION=0`): three `R8` (or `R8`+`RG8` for NV12)
    textures and TextureMapper's planar YUV shader.

  1.5 bytes/pixel go to the GPU (3.1 MB per 1080p frame), no CPU colour conversion; other pixel
  formats (10-bit, 4:2:2, 4:4:4) are converted to yuv420p by libswscale first. Then, as for every
  page in this configuration, the WebProcess's composited frame is read back (`glReadPixels`,
  `RenderTargetSHMImage`) and sent to the UI process as SHM (README "GPU"); B7's dma-buf path
  removes that copy for video and everything else alike.
- **A/V sync (gtk-video's proven scheme, M10 §3.3):** the master clock is a pausable wall clock,
  started at the first picture; audio follows it: 44.1 kHz S16 stereo (libswresample) written to
  `/dev/audio0` with ~120 ms kept in the ring (fill = seconds written − seconds elapsed), a sound
  frame heard > 50 ms late dropped, > 20 ms early preceded by silence; pictures wait for their pts
  and are dropped when > 100 ms late (one at least every 0.5 s). The reported `av_ms` is the last
  written frame's pts − its hearing time. Silence while paused/held and 0.25 s before close (the
  driver now also silences played words itself, `rpi4-audio.c:22`). One writer system-wide: an
  `fcntl` record lock on `/tmp/.wpe-audio0.lock` (released at process exit) — a second player
  plays muted.
- **HEVC on hardware:** nothing to do in WebKit: `hevc_rpivid` is FFmpeg's HEVC decoder with the
  rpivid hwaccel attached (M10b), CPU fallback per stream/picture inside it, one process owns the
  block (`/tmp/.rpivid.lock`). `FFMPEG_RPIVID=0` forces the CPU (the control arm).
- **Threads and stacks:** our three threads get 8 MiB stacks (`StackAllocationSpecification`);
  FFmpeg's frame/slice workers get 8 MiB through `phx_ffmpeg_pthread_create` — `build-wpe.sh`
  renames `pthread_create` in its copies of the FFmpeg archives (`objcopy --redefine-sym`), so
  WebKit's and GLib's threads are unchanged (a global `--wrap` would widen every thread).
- **Not supported (by design, stated):** MSE (no YouTube), Web Audio, WebCodecs, encrypted media,
  playback rate ≠ 1, `paint()` into a canvas (`drawImage(video)` draws nothing yet), in-band text
  tracks (WebVTT `<track>` is WebCore's own and works).
- **Licence:** the new WebKit files BSD-2-Clause (WebKit's preferred licence for new files), the
  hunks in existing files as those files; FFmpeg LGPL as configured (no `--enable-gpl`);
  `files/compat/phoenix-ffmpeg-compat.c` BSD-3-Clause. Everything in the ports repo.
- **Effort:** coded (2.2 k lines); remaining to a Pi pass: one full WebKit build with
  `USE video` (~2 h at -j8), then 1–3 Pi iterations (each a few TUs + ~1 min relink). Total
  ≈ 1–2 agent-days; polish (canvas paint, http seeking robustness, rate) 2–3 more.

### 3.3 Side by side

| | (a) GStreamer | (b) FFmpeg engine |
|---|---|---|
| New ports | 1 bundle of 5–6 tarballs (`gstreamer-full`) + audio sink | none (video_player now installs its FFmpeg libraries) |
| New code of ours | sink element, port recipes | 2.2 k lines WebCore + 70 lines compat (done) |
| WebKit patch | small (USE_GSTREAMER_FULL already upstream) | 0030: CMake (4 files), 2 enums, 1 serializer, registration, 6 new files |
| HEVC on rpivid | unverified (gst-libav naming/rank) | yes, same decoder as the shipped players |
| Frame path | CPU BGRA convert + 4 B/px upload (GL path needs gst-gl) | 1.5 B/px upload, GPU YUV→RGB (Skia or TextureMapper) |
| A/V sync | GStreamer clock (best) | wall clock + audio follower (gtk-video's, ±50 ms window) |
| MSE / Web Audio / WebRTC | yes / yes / later | no / no / no |
| Upstream shape | upstream path | off-upstream (fork-only policy: acceptable) |
| Licence | LGPL, ports | LGPL FFmpeg + BSD, ports |
| To first Pi pass | 6–10 agent-days | ~1–2 agent-days |

### 3.4 When to revisit (a)

When YouTube/MSE is wanted **and** the JIT is in the WebProcess (B9 into the port): YouTube's
player JS on the LLInt is not usable regardless of the media backend. (b) and (a) can coexist —
the engine registry takes both — but `USE_FFMPEG` conflicts with `USE_GSTREAMER` in patch 0030 to
keep one engine per build until that is needed.

## 4. What was built (ports branch `webkit-wpe-b8` = `842e5e6` video_player + `214d1a4` webkit_wpe, on master `bbf9883`; not merged, not pushed)

| Change | Where |
|---|---|
| WebKit patch `patches/webkit-video/0030-wpe-phoenix-ffmpeg-media-player.patch` (applied only with USE `video`): option `USE_FFMPEG` (WPE, private, OFF; depends on `ENABLE_VIDEO`, conflicts with `USE_GSTREAMER`); `ENABLE_VIDEO` needs GStreamer only without `USE_FFMPEG`; `GStreamer.cmake` gated on `USE_GSTREAMER`; `platform/FFmpeg.cmake` (pkg-config `libavformat libavcodec libswresample libswscale libavutil`); `MediaPlayerEnums::MediaEngineIdentifier::FFmpeg` + `MediaPlayerType::FFmpeg` (+ `WebCoreArgumentCoders.serialization.in`, the remote player's switch); the engine; the `VideoFrame.cpp` include fix | `webkit_wpe/patches/webkit-video/` |
| USE flag **`video`** (default off): `depends … video? ( video_player )`, `PHX_WPE_VIDEO=1 PHX_FFMPEG=<video_player>/ffmpeg` to `build-wpe.sh`; stage checks for the media strings; `b8.html`/`b8.sh` staged with USE `checks` | `webkit_wpe/port.def.sh` |
| `build-wpe.sh`: `PHX_WPE_VIDEO` → `-DENABLE_VIDEO=ON -DUSE_FFMPEG=ON` (else `-DUSE_FFMPEG=OFF`, the old configuration); the FFmpeg archives + headers + `.pc` into the private deps view, `pthread_create` renamed in them (checked: no direct caller left); `phoenix-ffmpeg-compat.o` on the link line; link checks (`MediaPlayerPrivateFFmpeg`, `ff_hevc_rpivid_decoder`, `phx_ffmpeg_pthread_create`, the `WPEB-MEDIA` format; and the engine **absent** without video) | `webkit_wpe/files/build-wpe.sh` |
| `phx_ffmpeg_pthread_create` (8 MiB default for FFmpeg's threads, `WPE_PHOENIX_FFMPEG_THREAD_STACK`) | `webkit_wpe/files/compat/phoenix-ffmpeg-compat.c` |
| Launcher: in a video build `enable-media` on and `--autoplay=muted|allow|deny` (`WPE_BROWSER_AUTOPLAY`; default muted = WebKit's), `WPEB … media autoplay=<p>`; a build without video is unchanged (all under `#if ENABLE_VIDEO`) | `webkit_wpe/files/launcher/wpe-browser.cpp` |
| The B8 check page and script | `webkit_wpe/files/checks/b8.html`, `b8.sh` |
| `video_player` installs its FFmpeg build as a library prefix `ffmpeg/` (headers, `.a`, prefix-relative `.pc`; checked: `ff_hevc_rpivid_decoder` in libavcodec) — a private prefix, not the shared one (header poisoning) | `video_player/port.def.sh` |

### 4.1 Rebuild cost

- **Default image (USE `video` off): no WebKit rebuild.** Patch 0030 lives in its own directory
  (`patches/webkit-video/`, applied only with USE `video`), so the tree WebKit compiles is the
  same; its new option would otherwise add `#define USE_FFMPEG 0` to `cmakeconfig.h` and
  recompile everything. What does change: **video_player** rebuilds (recipe changed: one FFmpeg
  build, ~2 min), and **webkit_wpe** re-runs its recipe: `rsync -c` keeps every unchanged file's
  mtime, so ninja recompiles only the launcher (`wpe-browser.cpp`; its non-video code is the same,
  every media line is under `#if ENABLE_VIDEO`) and relinks (~1–2 min).
- **USE `video` on:** a full WebKit build (~2 h at -j8, ~8500 steps; `ENABLE_VIDEO` changes
  `cmakeconfig.h`), and the same again when it is turned off. Later engine changes: 1–3 TUs +
  relink (~2–5 min); a video_player rebuild with unchanged headers: a relink (the link-closure hash).

## 5. Verification done here (no Pi, no image build)

1. **Configure:** `build-wpe.sh --stage configure` in a scratch `--out` with the patched tree,
   `PHX_WPE_VIDEO=1`, `PHX_FFMPEG` = a copy of the built video_player FFmpeg: CMake succeeds;
   public options ON now include `ENABLE_VIDEO`; `cmakeconfig.h` has `ENABLE_VIDEO 1`,
   `USE_FFMPEG 1`, `USE_GSTREAMER 0`; the deps view reports FFmpeg 60.16/60.31/4.12/7.5/58.29
   and no `U pthread_create` in libavcodec/libavutil. Warnings: the two pre-existing ones
   (MODULE→STATIC, unused `ENABLE_STATIC_JSC`/`ENABLE_WEB_CRYPTO`).
2. **The new TUs** (`MediaPlayerPrivateFFmpeg.cpp`, `FFmpegMediaStream.cpp`,
   `CoordinatedPlatformLayerBufferFFmpeg.cpp`) compile with the real WebCore flags (`-Werror`
   classes as the build; only upstream's `-Wsfinae-incomplete` notes from WTF headers).
3. **Media-bearing WebCore bundles: 106/106 compile** — every `UnifiedSource-*.cpp` of the scratch
   configure that includes a file mentioning `ENABLE(VIDEO)`, `MediaPlayer` or `HTMLMediaElement`
   (HTMLMediaElement, HTMLVideoElement, RenderVideo, the JS bindings, the media controls host,
   CoordinatedPlatformLayer, SkiaCompositingLayer…), compiled with the real flags minus the PCH,
   0 errors, 0 warnings beyond upstream's `-Wsfinae-incomplete`. One real gap found and fixed in
   0030 (`VideoFrame.cpp`'s missing `NativeImage.h`, standalone compile).
4. **The launcher** (`wpe-browser.cpp`) compiles against both configurations (video: has the
   `media autoplay=` line; no video: has not).
5. **The patch series** (0001–0011, 0015 + webkit-video/0030) applied with `patch -p1` (the framework's way) to
   a fresh tarball gives exactly the tree that was compiled.
6. **Link-time facts checked by hand:** `pread`, `clock_gettime`, `fcntl` are in the sysroot's
   `libphoenix.a`; Skia's `SkImage_GaneshFactories.cpp` (`TextureFromYUVAPixmaps`) and
   `SkImage_GaneshYUVA.cpp` are in WebKit's Skia source list; the NetworkProcess opens `file:` URLs
   with `g_file_new_for_path(url.fileSystemPath())` (`NetworkDataTaskSoup.cpp:140`), so the gate
   page's `?src=` query does not reach the file name.
7. **Not done:** the `Source/WebKit` bundles with media code (their generated message headers
   need most of the build first; reviewed by grep instead: every GStreamer use there is under
   `USE(GSTREAMER)`), the link, any run.


## 6. Pi gate (pre-registered; run after an image with `webkit_wpe` USE `video checks`)

**Prerequisite:** an image built from ports `webkit-wpe-b8` with `webkit_wpe` at
`use: [rootfs, checks, video]` in the project's `ports.yaml` (one full WebKit build), staged by
`sync-netboot-tree.sh`; build proof: `strings /usr/bin/wpe-browser | grep -c 'WPEB-MEDIA mono='` ≥ 1
and the port's stage check (`wpe-browser (USE video) lacks …` absent from the build log).

**Clips (present on the live export `/srv/phoenix-rpi4-nfs-gcc16`):**
- `/usr/share/video-demo/h264-720p30-aac.mp4` — 45 s, 1280×720 H.264 Main + AAC 44.1 k (video_player USE demo);
- `/root/hevc-1080p30-hash.mp4` — 20 s, 1920×1080 HEVC Main (`hvc1`, yuv420p, x265 with SEI hashes: the M10b correctness clip) + AAC 44.1 k; hand-placed in root's home, not part of USE demo (adding a 1080p HEVC clip to `gen-clips.sh` is a follow-up);
- (spare) `/usr/share/video-demo/hevc-720p30-aac.mp4` — 30 s, 1280×720 HEVC in the rpivid subset.

**Command (one psh line; XFCE session, `HOLD` 400 s; ~7.5 min; Bash `timeout` 600000):**
```
./scripts/test-cycle-psh-interact.sh --label b8-video --idle-secs 60 --max-cmd-secs 480 \
    --hdmi-dense-on 'B8 arm=' -- "/bin/bash /usr/share/wpe-browser/b8.sh"
```
(The arms take 250 s + 20 s of pauses after the ~45 s desktop start; `export B8_HOLD=<s>`
overrides the hold, `/bin/bash /usr/share/wpe-browser/b8.sh hevc,hevc-cpu` runs a subset.)
Arms in one session (`b8.sh`, default `h264,hevc,hevc-cpu,seek`), each a fresh
`wpe-browser --cpu-rendering --autoplay=allow --size=1000x620 file:///usr/share/wpe-browser/b8.html?src=<clip>`
killed after 75/50/50/55 s; the element is 960×540 with native controls.

**Grading lines** (`grep -a -E '^(B8 |WPEB |WPEB-MEDIA )|B8PAGE|Exception #|XFCE-SESSION'`):

| # | Line | PASS | If instead… |
|---|---|---|---|
| 1 | `WPEB … media autoplay=allow`; page `B8PAGE … canplaytype type=video/mp4;codecs="avc1.4d401f,mp4a.40.2" answer=probably`, `…hvc1… answer=probably`, `video/mp4 answer=maybe`, `application/x-mpegURL answer=no` | the engine is registered and answers | `answer=no` everywhere: no engine (a build without USE video, or registration) |
| 2 | `WPEB-MEDIA mono=… id=N load uri=file:///… local=1 mime=video/mp4`, `… open container=mov,mp4,m4a,3gp,3g2,mj2 duration=45.00 video=h264 1280x720 fps=30.00 audio=aac 44100 Hz 2 ch`, `… decoder video=h264 codec=h264 threads=<N ≥ 1> FFMPEG_RPIVID=unset` (avcodec_open2 resolves the requested 0 = auto to the count it starts), `… decoder audio=aac codec=aac threads=<N> …` | demux + decoders open in the WebProcess | `error open-failed`/`container-open`: the path or AVIO; `no-decoder`: component set |
| 3 | `WPEB-MEDIA … first-frame pts=0.000 1280x720 format=yuv420p`, page `B8PAGE … loadedmetadata size=1280x720 duration=45.00`, `canplay`, `playing` | readyState progression, autoplay | metadata but no `first-frame`: decoder; `first-frame` but no `playing`: autoplay policy |
| 4 | `WPEB-MEDIA … audio sound=on device=/dev/audio0 fill_ms=120`, `… audio fltp 44100 Hz 2 ch -> S16 44100 Hz stereo` | sound opened (heard on the jack: attended only) | `sound=off reason=busy`: another writer holds the lock; `reason=open`: the node |
| 5 | **H.264 720p** `stat` lines every 2 s after the first 4 s: `fps=` 29–31, `dropped` growth ≤ 2 % of `presented` growth, `|av_ms|` ≤ 60, `painted` within 2 % of `presented` | real time, in sync, every frame drawn | `presented≈30/s` but `painted` lower: the compositor (WebProcess composite + readback + labwc) drops — record `upload_ms`; `fps` < 25 with `dropped` rising: decode-bound (CPU, 4 threads + JS); `av_ms` drifting past ±60: the clock/fill model |
| 6 | **HEVC 1080p hw**: `decoder video=hevc_rpivid codec=hevc … FFMPEG_RPIVID=1`, `WPEB-MEDIA … ffmpeg [hevc_rpivid @ …] rpivid: hardware HEVC decode 1920x1080 8-bit (verified tool set), HEVC clock … MHz, completion by interrupt`, `stat … hw=1`, `ffmpeg … rpivid-stat pictures=…` at the end; `fps=` ≥ 25 with drops ≤ 5 % after 4 s | **HEVC decoded on the block in a page** | `rpivid: CPU decode: <reason>` / `continuing on the CPU decoder`: the block refused/failed (record reason; `the block is in use` = another process holds it); hw but fps < 20: upload/composite bound at 1080p (`upload_ms`, `painted` vs `presented`) — predicted risk, not a decode fault |
| 7 | **HEVC 1080p CPU control** (`FFMPEG_RPIVID=0`): `decoder video=hevc_rpivid … FFMPEG_RPIVID=0`, **no** `rpivid: hardware` line in that arm, `stat … hw=-` | the control decodes on the CPU | a `rpivid: hardware` line: the environment did not reach the WebProcess |
| 8 | hw vs CPU: record `fps`, `dropped`, and `top`-free proxy = the arms' `dropped`/`fps`; M10b predicts hw ≈ CPU fps at 30 fps with ~2.8 cores freed | recorded | — |
| 9 | **seek arm**: page `seek-request from=10.xx to=30`, `WPEB-MEDIA … seek target=30.000 rc=0`, `… seek done pts=` 30.0 ± 2 (keyframe), page `seeked current=` ≈ 30, then `timeupdate current=` advancing past 35 | seeking works | `seek done` missing: the first-picture-after-seek path |
| 10 | end of H.264 arm (45 s < 75 s): `WPEB-MEDIA … end clock=45.0… decoded=… presented=… painted=… dropped=…`, page `ended current=45.00` | end detection | no `end`: the drain path |
| 11 | every arm `B8 arm=<a> end rc=…`, `B8 done`, `XFCE-SESSION done rc=0`; **0** `Exception #`, 0 kernel faults | clean | addr2line the PC on the unstripped `bin/wpe-browser` first |
| 12 | HDMI (dense on `B8 arm=`): the labwc browser window with the toolbar, the 960×540 video with its controls, the testsrc2 frame counter / the 1080p picture advancing between two ticks; colours right (no green/magenta = plane order/colour space) | frames reach the screen | black element with `painted` rising: the YUV upload/colour path — A/B `WEBKIT_USE_SKIA_FOR_COMPOSITION=0` (TextureMapper path) |

**Record:** per arm the steady `fps`, `dropped/presented`, `painted/presented`, `upload_ms`,
`av_ms` range, and the WebProcess `mem … footprint_kb` if `WPE_BROWSER_RSS_SECS` is set.

## 7. Risks

1. **Compositor throughput at 1080p.** Each picture is a 3.1 MB upload, then the whole page is
   composited and read back (`glReadPixels` → SHM → wl_shm → labwc) at the video rate. 720p is
   likely fine; 1080p30 may top out below 30 fps until B7's dma-buf path. The `painted` vs
   `presented` counters separate this from decoding.
2. **The WebProcess's CPU budget:** H.264's 4–5 decoder threads + swscale (non-4:2:0 only) + JS
   on the LLInt + Skia CPU raster share 4 cores; HEVC on the block frees ~2.8 cores (M10b).
3. **Untested in WebKit's full build:** the `Source/WebKit` bundles with media code and the link
   were not compiled here (only WebCore); the first USE `video` build may surface more
   VIDEO-without-GStreamer gaps like the `VideoFrame.cpp` one.
4. **Teardown:** the engine's threads are joined in its destructor (DestructionThread::Main);
   nothing they wait for needs the main thread. A WebProcess that exits abruptly leaves the
   device to the driver's own silencing and the locks to process exit (both fine).
5. **HTTP media** goes through `FFmpegMediaStream`'s loader path, which the gate exercises only
   for `file://`; a remote MP4 (`https://…/x.mp4`) is the next check.
6. **One audio writer / one rpivid owner** system-wide: a second video, or gtk-video running,
   plays muted / on the CPU — by design, logged.
7. **Merge with `webkit-wpe-b7`** (B7, another agent's branch): both edit `build-wpe.sh`,
   `port.def.sh` and `wpe-browser.cpp` (textual conflicts expected; this branch's hunks are
   additive and `#if ENABLE_VIDEO`-guarded). No WebKit source overlap: B7's patches are
   `patches/webkit/0016-0017` (dma-buf transport, ANGLE), this one is `patches/webkit-video/0030`,
   applied after them.
8. **Colour:** BT.709 vs 601 is chosen from the stream (unspecified: 709 at ≥ 720 lines); full-range
   streams are exact on the Skia path, approximate (limited-range matrix) on TextureMapper.

## 8. Log

- 2026-10-02: design (§1–3); option (b) coded on ports `webkit-wpe-b8`; configure + new TUs +
  media WebCore bundles compiled in scratch; gate §6 pre-registered.
