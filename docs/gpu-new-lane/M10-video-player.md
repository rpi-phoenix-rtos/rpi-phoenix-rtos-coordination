# M10 — a desktop video player

Owner goal (2026-09-28): a proper video player for demo content on the desktop (XFCE on labwc,
`/bin/xfce-session-2`, or X11 on Xorg-drm): play / pause / stop / seek, windowed and full screen with
scaling, our ffmpeg port for the codecs (CPU) and, ideally, the rpivid HEVC hardware decoder.
[PLAN](PLAN.md) row M10. Every Pi cycle below is pre-registered (PLAN rule 6).

**Status (2026-09-28):**

| Piece | State |
|---|---|
| `ffplay-drm` — ffplay on SDL KMSDRM (full screen, from psh) | ✅ built + staged, host control PASS; Pi cycle `m10a0-ffplay-drm` pending |
| `ffplay-wl` — ffplay in a window on the desktop | ✅ built + staged against M8's SDL (Wayland + KMSDRM drivers, `tools/gpu-lane/sdl2-wl/`); Pi cycle `m10a-ffplay` after M8's `m8a-quake-window` |
| `/bin/video-play` launcher (picks Wayland or KMSDRM) | ✅ staged |
| test clips (synthetic, 4 codecs) | ✅ staged in `/usr/share/m10/` |
| **`gtk-video`** — our GTK 3 player: toolbar (open, play/pause, stop, seek bar, time, fullscreen) | ✅ first version built + staged (cairo path, no GL), host test PASS on broadway (§3.3); Pi cycle `m10c-gtk-video` pending |
| rpivid HEVC in the player | 📋 designed (§4): an `hevc_rpivid` libavcodec decoder, NV12 output first |

## 1. Candidates

Our building blocks: the ffmpeg 6.1 port (static libav*; the port itself is decode-only, the player
builds add libavfilter/libswscale/libswresample from the same tarball); SDL 2.30.12 KMSDRM on Mesa
GBM/EGL (`sdl2-drm`, with the Phoenix audio driver on `/dev/audio0` and HID input); GTK 3.24.52 +
GLib 2.88 Wayland-only (`gtk3-wayland --usr`, Atril runs on it); Mesa-DRM with the EGL wayland
platform (`mesa-drm --wayland`; weston-simple-egl ran on HDMI in m6g/m6i); libepoxy; D-Bus;
harfbuzz/freetype/fribidi/fontconfig from the GTK build; the `lua` port is **5.4.7**.

### (a) ffplay on SDL2 — the quick win (✅ done for KMSDRM; Wayland = relink on M8)

* **Controls:** keyboard only — space/p pause, ←/→ ∓10 s, ↓/↑ ∓60 s, f fullscreen, s frame step,
  9/0 volume, m mute, q/Esc quit; right click = seek to that fraction; window resize = scaling,
  aspect kept. No on-screen buttons, no seek bar, no "stop" (q quits).
* **Dependency tree:** ffplay.c + cmdutils + opt_common (LGPL) → libavfilter, libavformat,
  libavcodec, libswresample, libswscale, libavutil → SDL2 (video: KMSDRM now, Wayland on M8;
  renderer: SDL's GL/GLES2 renderer, YUV textures converted in its shader; audio: SDL's Phoenix
  driver) → Mesa (GBM/EGL/GL) + libdrm-phoenix → libphoenix. No new library.
* **Effort:** done here (≈ 1 h, §3). The Wayland build is a relink against M8's `libSDL2.a` +
  Mesa's wayland EGL + libwayland-client/xkbcommon (done, §3.1).
* **Verdict:** ship it: full screen from psh (`ffplay-drm`) and in a window on the desktop
  (`ffplay-wl`) — the demo player with keyboard controls.

### (b) mpv (+ its Lua OSC)

* **Version:** mpv **0.36.0 made libplacebo mandatory**; **0.35.1 is the last release without it**
  (its `vo=gpu` renderer is mpv's own). 0.35.1 builds with meson and accepts FFmpeg 6.x (the APIs it
  uses are deprecated, not removed, in 6.1). `vo=dmabuf-wayland` first appeared in 0.36 → **not
  available** without libplacebo; 0.35 has `vo=gpu` (OpenGL/GLES via EGL: `gpu-context=wayland`
  on EGL wayland — our Mesa gives GLES 3.1, which vo_gpu supports), `vo=wlshm` (software, shm
  buffers), `vo=sdl`, `vo=drm` (KMS dumb buffers) and `vo=gpu` with `gpu-context=drm` (GBM).
* **Dependency tree:** mpv 0.35.1 → FFmpeg (libavcodec/format/filter/swscale/swresample, ✅) →
  **libass** (mandatory in 0.35: OSD + OSC text) → freetype ✅, fribidi ✅, harfbuzz ✅ (the GTK
  build's C-only one), fontconfig ✅ → **Lua 5.1 or 5.2 or LuaJIT** for the OSC (❌ our port is 5.4,
  which mpv does not support: a Lua 5.2 build is needed, small) → wayland-client/egl/cursor ✅,
  wayland-protocols (xdg-shell, xdg-decoration, viewporter, presentation-time) ✅ from the labwc
  build, **xkbcommon** ✅ → EGL/GLES (Mesa wayland) ✅ → audio: `ao=sdl` (SDL audio only, Phoenix
  driver) or a tiny `ao_phoenix` (as `ao_oss`) → libphoenix gaps: **`posix_spawn` is absent**
  (no `<spawn.h>`; mpv's `subprocess-posix.c` needs it — a fork/exec shim), plus the usual poll/pipe/
  `pthread_setname_np` checks.
* **Effort:** 2–4 agent days (libass + Lua 5.2 + mpv's meson cross build + the libc gaps + a GLES
  bring-up of vo_gpu on our EGL), plus Pi cycles. The OSC gives a real seek bar and buttons in
  the video window, and mpv is also libmpv for (c).
* **Verdict:** the best *player*, but the most new code under us (libass, Lua, vo_gpu's shaders
  on V3D). Second choice for the GUI.
* Re-verify: the version facts above (0.36.0 = libplacebo mandatory and the first `vo=dmabuf-wayland`;
  0.35.1 builds against FFmpeg 6.1; libass mandatory in 0.35; Lua 5.1/5.2/LuaJIT only) are from
  memory of mpv's release notes, not read from source — confirm in mpv's `meson.build`/`RELEASE_NOTES`
  first.

### (c) Celluloid (GTK frontend for libmpv)

* **Version:** Celluloid **0.24 is the last GTK 3 release** (0.25+ is GTK 4 + libadwaita). It needs
  GTK ≥ 3.22 ✅, GLib ≥ 2.66 ✅, **libmpv** (client API ≥ 1.28 → mpv ≥ 0.33, so 0.35.1 as a static
  libmpv), libepoxy ✅, and renders through **GtkGLArea** + mpv's render API.
* **Dependency tree:** everything of (b) (as libmpv) + GtkGLArea on our GTK — which today links a
  **no-EGL stand-in** (`gtk3-wayland/src/gtkphx_noegl.c`): a GL-using GTK program must link the
  Mesa wayland EGL closure instead, and **no GtkGLArea has run on this stack yet**.
* **Effort:** (b) + 1–2 days (GtkGLArea bring-up, Celluloid's meson build, MPRIS over D-Bus).
* **Verdict:** the nicest desktop result, the longest chain. Only after (b) works.
* Re-verify: "0.24 is the last GTK 3 release" and its libmpv/GLib minimums — from memory; check
  Celluloid's tags and `meson.build`.

### (d) Parole (XFCE's player, GStreamer)

* **Dependency tree:** Parole 4.18 → GTK 3 ✅, libxfce4ui/libxfce4util/xfconf ✅ (XFCE build),
  dbus-glib (❌, deprecated; Parole still uses it) → **GStreamer 1.x**: core (❌; its plugin registry
  forks `gst-plugin-scanner` and loads plugins with GModule/dlopen — a static `gst-full` build is
  needed on Phoenix) + gst-plugins-base (playbin, decodebin, videoconvert, audioconvert,
  audioresample, typefind, app) + gst-plugins-good (qtdemux, matroska, autodetect) + **gst-libav**
  (FFmpeg decoders, ✅ libs) + a video sink: Parole embeds the video through `GstVideoOverlay` on an
  X11 window id; on Wayland it needs `gtksink`/`gtkglsink`, whose support in Parole is partial →
  an audio sink for `/dev/audio0` (❌: none exists; a new GStreamer element).
* **Effort:** 5–8 agent days (four GStreamer modules, static plugin registration, a Phoenix audio
  sink, dbus-glib, Parole's Wayland video path) — the GStreamer port alone is larger than all of (e).
* **Verdict:** not worth it for a demo; revisit only if GStreamer is wanted for its own sake.
* Re-verify: Parole's current dbus-glib dependency and its Wayland video-sink state — from memory;
  check the latest Parole release's `configure.ac`/`meson.build` and `src/gst/`.

### (e) a small GTK3 player of our own (**recommended for the GUI**)

* **Design:** one static program, `gtk-video` (working name): libavformat demux thread → video
  and audio decode threads (libavcodec; `hevc_rpivid` once §4 exists) → video frames to a
  **GtkGLArea** (three R8 textures — or two for NV12 — plus a BT.709 YUV→RGB fragment shader;
  scaling is the quad's size, aspect kept) or, as the no-GL fallback, a GtkDrawingArea with a
  cairo image surface from `sws_scale` (fine at ≤ 720p); audio: libswresample → 44.1 kHz S16
  stereo → a writer thread on **`/dev/audio0`** directly (its blocking write paces the thread; the
  audio clock = samples written minus the device's buffered bytes); A/V sync with audio as the
  master clock (ffplay's rules, simplified: drop late frames, sleep to the next pts). UI: a
  GtkHeaderBar or bottom toolbar with **play/pause, stop, a seek GtkScale** (updated by a 250 ms
  timer, dragging seeks with `avformat_seek_file`), time labels, a **fullscreen** button
  (`gtk_window_fullscreen`, also F11 / double click; the toolbar hides in fullscreen), keyboard
  shortcuts as ffplay's, a file chooser (GtkFileChooserDialog) and a command-line file argument.
* **Dependency tree:** GTK 3 Wayland ✅ (the Atril link) + FFmpeg libs ✅ (this build) + for the GL
  path Mesa's wayland EGL closure (as weston-simple-egl) + libepoxy ✅. **No dependency on M8.**
* **Effort:** ~1000 lines of C, 1–2 agent days to a first Pi cycle; the cairo fallback first
  (no GL risk), then GtkGLArea. **Started 2026-09-28: the cairo version is written, builds and
  passes its host test (§3.3)**; GtkGLArea is the next step if the cairo path is too slow at 1080p.
* **Verdict:** **recommended.** It is the cheapest path to **real controls — buttons, a seek bar,
  time labels, a fullscreen button — in a window on the desktop** without porting a library stack
  (libass, Lua, GStreamer); everything under it is already Pi-proven (GTK 3 Wayland programs, the
  FFmpeg decoders, `/dev/audio0`), and it is the natural host for the zero-copy HEVC path of §4
  (GL texture from the rpivid buffer).

**Recommendation:** (a) now, full screen and windowed (both built); **(e) as the GUI player**; (b) only if a richer player is wanted later (then (c) on top of it).

## 2. Audio

* **`/dev/audio0`** (`rpi4-audio`, BCM2711 PWM audio on the 3.5 mm jack via PWM1/GPIO40-41): a
  blocking `write()` of **44100 Hz, stereo, signed 16-bit LE** — fixed format, no ioctl; the write
  drains at the playback rate, so it paces the writer.
* **The existing ffmpeg port outputs no audio.** The port is libraries only (decoders: mjpeg,
  h264, rawvideo, `pcm_s16le`; demuxers: mjpeg, wav) and the E4 demos in `tools/ffmpeg-port`
  (`e4-play` & co.) are video-only to `/dev/fb0`; nothing there opens `/dev/audio0`.
  `tools/hevc-decode/hevc-play` skips the audio track of an `.mp4` too.
* **SDL audio on Phoenix** = the `phoenix` driver of the sdl2-drm overlay
  (`tools/gpu-lane/sdl2-drm/overlay/src/audio/phoenix/SDL_phoenixaudio.c`): a pull-model backend,
  SDL's audio thread fills the mix buffer through the app's callback and `write()`s it to
  `/dev/audio0`; `OpenDevice` forces 44100/S16/2ch and SDL builds the conversion stream from the
  app's spec. ffplay asks SDL for the file's rate/channels (e.g. 48 kHz Opus) and resamples to what
  SDL grants with libswresample, so the device format is never an issue.
* A GTK player (e) writes `/dev/audio0` itself (no SDL needed), after libswresample.
* **Finding (by reading `rpi4-audio.c`, not yet heard on the Pi): the device replays stale audio.**
  With the DMA path, `/dev/audio0` is a **free-running ~0.19 s ring** (`RING_WORDS 16384`) that a
  self-chained DMA plays forever; `write()` fills it ahead of the read cursor and blocks while it
  is full. Nothing refills it with silence: `mtClose` does nothing, and a writer that falls behind
  or stops leaves the DMA looping the last ~0.19 s it wrote. So **after ffplay (or any SDL
  program) exits**, or when an SDL writer stalls, the jack keeps playing the last ~0.19 s in a
  loop until the next writer (SDL's Phoenix driver closes without writing silence; SDL itself
  keeps feeding silence while ffplay is paused, so pause is fine). Fix for the coordinator (a
  driver change, not made here): refill the ring with mid-scale on close, and when the read
  cursor passes the write index. `gtk-video` works around it (below).

## 3. What was built (2026-09-28)

`tools/gpu-lane/video-player/` (new directory; nothing existing touched):

| File | What |
|---|---|
| `build-ffplay.sh` | unpacks the **port's** `ffmpeg-6.1.tar.gz` (sha256 `938dd778…`, checked), applies `patches/`, configures with the port's line + `components.sh`, builds the libs, compiles `fftools/ffplay.c` + cmdutils + opt_common by hand (SDL is not probed: its link test would need the whole static Mesa group) and links in the **quakespasm-drm shape** against `sdl2-drm/build-out` (`sdl-prefix` + `mesa-gl`, read-only); verifies (0 undefined, KMSDRM/EGL/`/dev/audio0`/`/dev/kbd0` strings, no old-lane strings, every `pthread_create` caller routed through the glue) |
| `components.sh` | the component set: decoders h264, hevc, vp8, vp9, mpeg4, mjpeg, aac, mp3, opus, vorbis, flac, pcm; demuxers mov/mp4, matroska/webm, mpegts, avi, raw h264/hevc, ogg, mp3, aac, flac, wav; the filters ffplay's graphs need (**scale and aresample are auto-inserted** — without them a graph "cannot convert formats"), all LGPL (`CONFIG_GPL 0`, `CONFIG_NONFREE 0`) |
| `ffplay_phoenix_glue.c` | `--wrap=pthread_create`: threads created without a stack size get **8 MiB** (libphoenix's default is 256 KiB; the H.264 decoder overflowed a small stack before, ffmpeg-port README), `FFPLAY_THREAD_STACK` overrides |
| `patches/0001-ffplay-phoenix-statline-and-autokeys.patch` | two opt-in knobs: `FFPLAY_STATLINE_MS` = a newline-terminated `ffplay-stat t= clock= av= shown= fps= drop_early= drop_late= aq= vq= paused= fs= win=WxH` line (ffplay's own status line is `\r`-terminated and has no fps); `FFPLAY_AUTOKEYS="<s>:<key>,…"` = key presses pushed through ffplay's own event loop (pause fs left right up down step quit), so an unattended cycle exercises the real controls |
| `hosttest/run.sh` | host control: the same tarball + components + patch built natively against the host SDL2, every clip played headless (SDL dummy drivers), then a scripted pause/seek/fullscreen/quit run graded from the stat lines |
| `gen-clips.sh` | the test clips from lavfi sources only (testsrc2, mandelbrot, sine) |
| `pi/video-play` | the launcher (§3.2) |
| `conf/labwc-xfce-m10/autostart` | the XFCE demo autostart + one delayed player (`M10_PLAYER=ffplay` → `video-play`, `=gtk` → `gtk-video --autoexit`), for the desktop cycles |
| `gtk-video/gtk-video.c`, `gtk-video/build.sh`, `gtk-video/hosttest.sh` | the GTK 3 player (§3.3), its Phoenix build (the gtk3-wayland `--usr` stack + this directory's ffmpeg libraries) and its headless host test |
| `conf/applications/gtk-video.desktop` | "Video Player" in the app finder / Thunar's Open With (MimeType: mp4, mkv, webm, mov, avi, ts, ogg, mp3, wav, flac) |

### 3.1 Results

**`ffplay-wl`** (`build-ffplay.sh --sdl wl`, linked from `sdl2-wl/build-out/link-inputs.txt` — M8's
relink contract: gallium, sdl, mesa-gl, tail, flag lines): **`ffplay-wl.stripped`
`09a662206f6910b20e926c62381fce69d7ad9fab64358eff3ff79b090857ab4f`** (23 301 896 bytes); SDL
`libSDL2.a` `543aef39ed9a498d` (set `afda850993ea66ad` = sdl2-drm 0001–0009 + overlay + sdl2-wl
0101–0103: **Wayland and KMSDRM** drivers), Mesa `mesa-drm/build-out-wayland-gl` (opengl + wayland,
libgallium `0bc87a0e9e839bed`), libwayland-client/egl/cursor + xkbcommon + the compat layer from
labwc-drm; flags `--wrap=mmap,ioctl,close,write`. `nm -u` 0, 0 link warnings beyond libphoenix's
notes, `Wayland_CreateDevice`/`wl_display_connect`/`wl_egl_window_create`/`xkb_context_new`
present, every `pthread_create` caller routed through the glue. After this refactor the drm
variant relinks **byte-identical** (`8116cfd0…`).

* `ffplay-drm` (unstripped) and **`ffplay-drm.stripped` `8116cfd0b8770cb6d571bc33b033b4f13fb4127567f750d7936f2fdf5e1f79e9`**
  (22 830 456 bytes); ffmpeg configure set `504e01a6c405825c`; SDL `libSDL2.a` `7a1de5d1354967c2`
  (patch/overlay set `d145b0e6297ef6e4`, the stk-drm/quakespasm-drm one); Mesa `4a457a1efe6f3903` opengl=true.
  `nm -u` 0; 18 compiler warning lines, all inside ffmpeg (stringop/array-bounds/unused), none new.
* **Host control PASS** (`hosttest/run.sh`): all four clips open, decode and play 6 s with the A-V
  clock advancing and 0 errors (h264+aac 720p/1080p, hevc+aac, vp9+opus); scripted keys: pause
  holds the clock (2/2 stat lines `paused=1`, clock unchanged), → seeks +10 s (measured 11.0 s:
  keyframe-aligned, 2 s GOP), f toggles fullscreen (`fs=1`→`0`), q quits (rc 0), no `\r` in the
  output with the stat line on, fps 29.4–30.0. **Fail-first control:** the same build without the
  `scale`/`aresample` filters prints `'aresample' filter not present, cannot convert formats` and
  never advances — the host test does discriminate a broken component set.

### 3.2 Staged on the live export (`/srv/phoenix-rpi4-nfs-gcc16`, all NEW paths, checked absent, then `cmp` OK)

| Path | sha256 |
|---|---|
| `/usr/bin/ffplay-drm` | `8116cfd0b8770cb6d571bc33b033b4f13fb4127567f750d7936f2fdf5e1f79e9` |
| `/usr/bin/ffplay-wl` | `09a662206f6910b20e926c62381fce69d7ad9fab64358eff3ff79b090857ab4f` |
| `/bin/video-play` | `c62894e63cc9b5e6196916a129d2cd7799f8b718471ba5bf4ac520d0d6b8e66a` |
| `/usr/share/m10/m10-h264-720p30-aac.mp4` (45 s, 1280×720 H.264 Main + AAC 44.1k, 18.4 MB) | `68a9bd9451960432b7ebed38fabb16eb7cc7ce845d844c3cf5180efc782bb73e` |
| `/usr/share/m10/m10-h264-1080p30-aac.mp4` (30 s, 1920×1080 H.264 High + AAC, 24.7 MB) | `0c1a44e0d028b735fe3fdf4b0aa1045ff22bd2ced2e83e3cd94d0e90c10bcf08` |
| `/usr/share/m10/m10-hevc-720p30-aac.mp4` (30 s, 1280×720 HEVC Main **in the rpivid subset** + AAC, 16.2 MB) | `2ff9bc143e3ac383fc7eb53150ad0533c4064349dfca733e0ebfdf9ae8be9727` |
| `/usr/share/m10/m10-vp9-360p-opus.webm` (20 s, 640×360 VP9 + Opus 48k, 2.1 MB) | `f438af1067e7fbc559fce2a6c3fe595108ce53e5f2205240061f3ada1e504b7c` |
| `/usr/share/m10/labwc-xfce-m10/{autostart,rc.xml,menu.xml,environment}` | autostart `ab87e093a57d023f684cd85609eaf5847e80f1a27ae7e973d27bb3387a107129`; the other three copied from `/etc/xdg/labwc-xfce-demo/` |
| `/usr/bin/gtk-video` | `ddde5b61329417d187049952e076c518d0aa2b6046ce84b8964f551d696777f0` (21 876 984 bytes) |
| `/usr/share/applications/gtk-video.desktop` (root-owned dir: `sudo install`) | `41785fa2b4371f110c817e6c4385aecb0dc7f3413fef24cb7d3990b846d32edf` |

Existing HEVC streams from the rpivid
work are also on the export: `/usr/share/demo/{showcase1080,reel-motion720,hd1080b}.265` (raw
Annex-B; ffplay reads them with the `hevc` demuxer, at 25 fps nominal).

`/bin/video-play <file> [ffplay options]`: with a Wayland socket in `/tmp/xdg` it runs
`ffplay-wl` with `SDL_VIDEODRIVER=wayland` and `SDL_VIDEO_WAYLAND_WMCLASS=ffplay` (the app_id), as
M8's `/bin/game-window.sh` does (without `ffplay-wl` it refuses: `ffplay-drm` would take card0 from
labwc); otherwise it starts the render server and
`rpi4-kms` if missing (xfce-session-2's commands, without `-C`) and runs `ffplay-drm -fs`.
Knobs: `VIDEO_MODE`, `FS`, `THREADS`, `LOOP`, `AUTOEXIT`, `STATLINE_MS` (default 2000),
`FFPLAY_AUTOKEYS`. Lines: `VIDEO-PLAY start … / VIDEO-PLAY done rc=`.

Rebuild: `tools/gpu-lane/video-player/build-ffplay.sh` (≈ 1 min incremental, ≈ 2 min clean);
`gen-clips.sh`; `hosttest/run.sh`. Wayland: `build-ffplay.sh --sdl wl` (the ffmpeg libraries are
shared; the fftools objects are recompiled on a variant switch).

### 3.3 `gtk-video` — the GTK 3 player (first version)

`tools/gpu-lane/video-player/gtk-video/gtk-video.c` (~1300 lines of C): a demux thread
(libavformat, bounded packet queues, seek requests), a video thread (libavcodec, frame threads =
FFmpeg's auto) that scales each picture with libswscale **to the video area's size, aspect kept**
(BGRA, 2 swscale threads) and hands it to the GTK main loop, which paints it 1:1 with cairo; an
audio feeder thread (libavcodec → libswresample → 44.1 kHz S16 stereo → `/dev/audio0`). The master
clock is a pausable wall clock, held at the target of a seek until the first picture after it is
ready (frames before the target, decoded from the preceding keyframe, are skipped); pictures later
than 100 ms are dropped (but one is shown at least every 0.5 s). The feeder models the device ring's
fill (seconds written minus seconds elapsed since the first write) and keeps **~120 ms** queued
(`GTK_VIDEO_AUDIO_FILL_MS`; the ring holds ~190 ms); a sound frame is due when it will be *heard*
at its pts (clock + fill): later than 50 ms → dropped, earlier than 20 ms → silence in front of
it. **Silence is written while paused, held or starved, and 0.25 s of it before close** — the
ring is never left replaying stale audio (§2). UI: a toolbar with **Open** (file chooser),
**Play/Pause**, **Stop** (back to 0, paused), a **seek bar** (GtkScale, updated every 250 ms, a
drag seeks), the **time** (`m:ss / m:ss`) and **Fullscreen** (`gtk_window_fullscreen`; the
toolbar hides; also F11/f, a double click, Escape to leave); keys as ffplay's (space, s, ←/→,
↓/↑, q). `--fullscreen`, `--autoexit`; `GTK_VIDEO_STAT_MS` (a `GTK-VIDEO stat t= clock= shown=
fps= dropped= decoded= vq= aq= paused= fs= win=WxH picture=WxH sound=` line), `GTK_VIDEO_AUTOKEYS`
(`<s>:<action>`, actions pause stop fs left right up down quit), `GTK_VIDEO_AUDIO=0`,
`GTK_VIDEO_THREADS`, `GTK_VIDEO_SWS_THREADS`.

Build: `gtk-video/build.sh` after `build-ffplay.sh` (it links that build's ffmpeg libraries and
the gtk3-wayland `build-out-usr` stack, the one Atril uses, the gtk3-hello way; `--wrap=
pthread_create` with the same glue). `nm -u` 0, 0 link warnings, every `pthread_create` caller
through the glue; the Adwaita icons it names are on the export. **Host test PASS**
(`gtk-video/hosttest.sh`: host GTK 3.24 + the host-control ffmpeg, GDK's **broadway** backend,
headless): 30.0 fps; pause holds the clock (3.06 ×3); → from 4 s lands at 14.8 (keyframe-aligned);
stop → `clock=0.00 paused=1`; play resumes (0.77 s after 0.7 s); quit rc 0; all four clips play
at 29.9–30 fps with their audio resampled (AAC 44.1k, Opus 48k). Fail-first: a mutant whose pause
does nothing is caught (`paused=0`, clock moving). **Audio feeder, host check** (the "device" a
regular file, 100 ms RMS blocks): 8.47 s written over an 8.2 s run + the 0.25 s closing silence
(paced to real time by the fill model), 0.2 s of silence while the clock was held at the start,
the clip's 1 s beeps on a 1.0 s grid before and after the pause, **exactly 2.0 s of silence for
the 2 s pause**, silence at the end. Not testable on broadway: fullscreen (no such window state
there) and the real device's blocking/underrun behaviour.

The ffmpeg **port** (`sources/phoenix-rtos-ports/ffmpeg`) was unchanged at the time: it stayed the
decode-only library port. (Removed 2026-09-30: it was never enabled and had no consumer; the
`video_player` port is the one FFmpeg build, and its tarball is the one these tools read.)

### 3.4 Pi results (2026-09-28)

| Cycle | Verdict | What the log shows |
|---|---|---|
| `m10a-ffplay` (windowed, `ffplay-wl` under `/bin/xfce-session-2`) | **PASS, all 9 rows** | arm 1: `win=1280x720 fs=0` next to Thunar, **29.8–30.3 fps**, drop_late 10 of 1100 shown (0.9 %; the 21 drop_early are the first second); pause at 15 s holds `clock=14.49` over two stat lines (`paused=1`), unpause at 19 s; `key=fs` at 25 s → `fs=1 win=1920x1080` at 27.9–30.3 fps, at 35 s back to `fs=0 win=1280x720`; `key=quit` → `VIDEO-PLAY done rc=0 t=48`. Arm 2 (`-fs`): `fs=1 win=1920x1080` from the first stat line, 29.8–30.3 fps, quit → `done rc=0 t=34`. Both `XFCE-SESSION done rc=0`, 0 faults. HDMI: the decorated video window over Thunar with the panel listing both (`artifacts/hdmi/2026-09/20260928-114715-m10a-ffplay-tick.png`, paused frame `…114726…` at clip time 14.500), full screen with the panel hidden at clip times 24.9 and 30.2 s (`…114737…`, `…114742…`), Thunar alone after quit |
| `m10a0-ffplay-drm` (full screen from psh, KMSDRM) | **plays; FAIL on quit** | 720p H.264 + AAC at **29.3–30.5 fps** full screen (`win=1920x1080`, HVS scales), drop_late 3; pause 12→17 s holds the clock (11.58→11.56), `right` at 20 s seeks +10 s, `fs` toggles; **`key=quit` at 40 s never returns** (no `VIDEO-PLAY done`), so the 1080p and VP9 arms did not run. The same keypress quits cleanly on Wayland (above), so the hang is in SDL's KMSDRM teardown — the P16 path (SDL 0010). Fix + `-autoexit` in the launcher: in progress |
| `m10c-gtk-video` (the GTK 3 player under `/bin/xfce-session-2`) | **PASS except row 7 (not reached); fullscreen slow** | picture + sound (`sound=on`, `fill target 120 ms`), `decoder h264 threads=5`; windowed 720p **26–30 fps, 0 dropped**; pause holds `clock=16.41` over two stat lines, play resumes; `right` → `seek target=29.42 rc=0`, next clock 29.85; `fs` → `fullscreen=1`, `scale … -> 1920x1080`, back with `fullscreen=0`; `GTK-VIDEO done`, both `XFCE-SESSION done rc=0`, 0 faults. **Row 7 (stop/play) never ran**: the +10 s seek moved the clip's end (45 s) before the 44 s `stop` key and `--autoexit` quit first — a script-timing miss, re-run with `stop` before the seek. **Fullscreen is CPU-bound**: 720p→1080p **14–17.5 fps** with `dropped` rising 26→147, 1080p→1080p **21.5 fps** (row 9 predicted 10–20). ffplay-wl does 1080p full screen at 30 fps on the GPU, so the next step for gtk-video is a GL present (GtkGLArea, §1e), not a faster swscale. Also: arm 2's `dropped=1` against `decoded` 30/s and `fps` 21.5 = the drop counter misses frames replaced before paint (fix the counter) |
| `m10a1-hevc-cpu` (HEVC 720p, libavcodec's CPU decoder, 4 threads) | **real time; EOF stall** | 892 frames shown at **29.9–30.0 fps**, drop_late 4; at the clip's end ffplay sits at `shown=892 fps=0.0` (ffplay's default is to stay open at EOF — the launcher now needs `-autoexit`, not a decoder fault) |

## 4. rpivid HEVC in the player — design

Facts (tools/hevc-decode, README + `hevc-m2.c`): a user process maps the block's MMIO
(`0xfeb00000`, `MAP_DEVICE|MAP_PHYSMEM`), enables its clock through `/dev/vcmbox`, allocates
`MAP_UNCACHED|MAP_CONTIGUOUS` DMA buffers (bitstream, command, PU/coeff, a POC-indexed DPB pool
sized from the SPS), builds per-slice command buffers from **its own parser** (`hevc_parse.c`:
SPS/PPS/slice header → QP, data_byte_offset, ref lists, RPS, weights, WPP entry points) and waits
on SPI-98. Output is **SAND / NV12_COL128**: luma and chroma planes in 128-byte-wide columns
(`pixel(x,y) = buf[(x/128)*stride + y*128 + x%128]`, stride = column height × 128); 10-bit is
`NV12_10_COL128` (3 samples per 32-bit word). Bit-exact vs ffmpeg for the x265 default toolset to
1080p; decode ≈ **4.5 ms per 1080p frame**, but the `/dev/fb0` player reaches only 21.7 fps
because ~90 % of each frame is its SAND→RGB blit from uncached memory. Open: gotcha 8 (rare
pixel corruption under memory-fabric load). One process owns the block at a time.

**Interface options:**

1. **`hevc_rpivid` libavcodec decoder (recommended first).** A new `FFCodec` in the player's ffmpeg
   build (a patch adding `libavcodec/rpivid_hevcdec.c` + its `allcodecs.c`/Makefile lines, the
   `hevc_v4l2m2m` shape: `.p.type=VIDEO .p.id=AV_CODEC_ID_HEVC`, `.bsfs="hevc_mp4toannexb"`,
   `.receive_frame`/`.decode`, `AV_CODEC_CAP_DELAY`), wrapping hevc-m2.c's engine refactored into a
   small library (`rpivid_open(sps,pps)`, `rpivid_decode(nal, pts)`, `rpivid_get_frame()` — the
   code of `hevc-play`'s main loop: NAL iteration, POC DPB, reorder, as a library instead of
   `main`). The wrapper keeps our parser (bit-exact today) instead of mapping the register
   programming onto ffmpeg's `HEVCContext` (the hwaccel route: fewer lines in the end, but every
   descriptor field re-derived from a different parser = re-proving bit-exactness). Out-of-subset
   streams (tiles, AMP, Rext, >1 slice/frame, EPB in headers): `hevc_rpivid` returns
   `AVERROR_PATCHWELCOME` from `init` or its first packet, and the player falls back to the
   native `hevc` decoder (ffplay: `-vcodec hevc` / our player: retry with the CPU decoder).
   **Output: `AV_PIX_FMT_NV12`** (upstream 6.1 has no SAND pixel format): a CPU de-SAND = one
   128-byte memcpy per column row — ~3.1 MB per 1080p frame, read from uncached memory (est.
   4–8 ms; to be measured). With NV12, ffplay (SDL NV12 textures, converted in SDL's GL shader)
   and the GTK player need no change beyond picking the decoder. Selection: register it before
   the native `hevc` so `avcodec_find_decoder(AV_CODEC_ID_HEVC)` finds it, or name it
   (`ffplay -vcodec hevc_rpivid`). Effort: engine → library 1 day, the FFCodec 0.5 day, a
   `framemd5` bit-exact check against the native decoder on the Pi 0.5 day.
   *Speed-up if the copy is the bottleneck:* cacheable DPB buffers + an explicit invalidate before
   the de-SAND (needs a user cache-maintenance call on Phoenix — check what libphoenix/kernel offer).

2. **Zero-copy GL (for a GL player, windowed or full screen).** Mesa's v3d driver already imports
   `DRM_FORMAT_NV12` with **`DRM_FORMAT_MOD_BROADCOM_SAND128`** and converts it with its SAND blit
   (`v3d_resource.c`, `v3d_blit.c` of the Mesa we build) — the Raspberry Pi OS path. Here the
   decoder's contiguous DPB buffers would be **exported** (E1's `vm_objectExport`) and imported as
   a foreign BO by the render server + libdrm-phoenix (PRIME import, as G4/G7 do for client
   buffers), then `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, NV12, modifier SAND128(col height))` →
   a texture in the GtkGLArea. Output format of the decoder = a private `AV_PIX_FMT_DRM_PRIME`-like
   frame carrying the handle; ffplay would not use it (SDL cannot), the GTK player would. Effort:
   2–3 days (the render-server import op is the new part; the kernel export exists).

3. **Zero-copy full screen via a KMS plane.** The firmware plane API that rpi4-kms already drives
   (`SET_PLANE`) takes **`VC_IMAGE_YUV_UV`** = SAND128 NV12 with the column pitch in lines (Linux
   fkms: `vc4_firmware_kms.c`), and the HVS scales and converts in hardware. rpi4-kms would accept
   an NV12+SAND128 framebuffer on an overlay plane (ADDFB2 with the modifier) whose memory is the
   decoder's buffer (below 1 GiB: E3's scan-out limit; the DMA pool is low-PA). Effect: 1080p HEVC
   with ~0 CPU per frame and no GPU work — but only full screen / a plane rectangle, not composited
   in a Wayland window. Effort: 1–2 days in rpi4-kms + a KMS-plane output path in the player.

Order: (1) — it makes rpivid usable by every player — then (2) for the GTK player; (3) is a
demo special. The fabric-contention corruption (gotcha 8) is a prediction for every cycle of
these, not a surprise: grade frames against the CPU decoder (`framemd5`) and count bad frames.

## 5. Pre-registered Pi cycles

Common grading: `./scripts/uart-summary.sh <label>`,
`grep -a -E '^(VIDEO-PLAY|ffplay-stat|ffplay-auto|XFCE|KMS |V3DA |DEBUG|ERROR|WARN|libdrm-phoenix|MESA)|Input #|Stream #|Could not|failed|SDL' <log>`.
ffplay's own `\r` status line is off (the launcher sets `STATLINE_MS=2000`), so every stat is its
own line. fps = the `fps=` field (frames shown per second since the previous stat line); drops =
`drop_early` (decoder-side, before display) + `drop_late` (display-side). HDMI: snapshots after
`VIDEO-PLAY start` (dense). EL0 fault dumps print twice; ~1.3 % of UART lines are corrupt.

### `m10a0-ffplay-drm` — ffplay full screen from psh (runnable now)

**Question:** does ffplay decode and present H.264 + AAC through SDL KMSDRM + Mesa + rpi4-kms on
HDMI with sound, at the clip's 30 fps, and do the controls work (pause, seek, quit)?

```
./scripts/test-cycle-psh-interact.sh --label m10a0-ffplay-drm --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 200 --hdmi-dense-on 'VIDEO-PLAY start' -- \
    "export FFPLAY_AUTOKEYS=12:pause,17:pause,20:right,30:fs,34:fs,40:quit" \
    "/bin/bash /bin/video-play /usr/share/m10/m10-h264-720p30-aac.mp4" \
    "export FFPLAY_AUTOKEYS=" \
    "export THREADS=4" \
    "/bin/bash /bin/video-play /usr/share/m10/m10-h264-1080p30-aac.mp4" \
    "/bin/bash /bin/video-play /usr/share/m10/m10-vp9-360p-opus.webm"
```

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `VIDEO-PLAY server start: …` ×2 (first run only), `VIDEO-PLAY start mode=drm player=/usr/bin/ffplay-drm … video=KMSDRM audio=phoenix fs=1` | servers come up | `server is missing`: that server's lines |
| 2 | `Input #0, mov,mp4…`, `Stream #0:0: Video: h264 (Main) … 1280x720`, `Stream #0:1: Audio: aac (LC) … 44100 Hz, stereo` | the demuxer + probes | `Invalid data`/`could not find codec parameters`: the NFS read (retry once; stage to /tmp) |
| 3 | KMSDRM init: SDL `DEBUG`/no `Could not initialize SDL`; `ffplay-stat … win=1920x1080` (KMSDRM windows are the mode size) | window = the display mode | `Could not initialize SDL - …`: KMSDRM/EGL (compare with quakespasm-drm's log); `win=1280x720` with a corner picture: the fullscreen scaling did not apply (finding) |
| 4 | **audio opened**: no `SDL_OpenAudio (2 channels, 44100 Hz): …` error and `aq=` > 0 KB while playing; sound on the jack (not machine-checkable) | the Phoenix SDL audio driver | an `SDL_OpenAudio` error → ffplay plays video only (`M-V`) — the `/dev/audio0` node; a stall after the first frames with `aq` full: audio thread blocked (C5-like) → re-run with `export SDL_AUDIODRIVER=dummy` as the A/B |
| 5 | `ffplay-stat` every 2 s: 720p **fps 29–30**, `drop_early+drop_late` ≤ 2 % of `shown`, `av` within ±0.1 s | the A72 decodes 720p H.264 at 30 fps with 4 threads (bit-exact ffmpeg-port H.264 on the Pi, 2026-08-06) | fps < 25 with growing `drop_late`: present path (the vsync'd swap: SDL's renderer on KMSDRM waits for the flip) vs decode (`drop_early`): note which |
| 6 | autokeys (arm 1): `ffplay-auto t=12… key=pause`, stat lines with **`paused=1` and a constant `clock`**, `key=pause` at 17 → `paused=0`; `key=right` at 20 → `clock` +10 s (±2, keyframe); `key=fs` 30/34 → `fs=0` then `fs=1` (it started full screen); `key=quit` → `VIDEO-PLAY done rc=0` | the controls work through ffplay's event loop | a key logged but no state change: the SDL event path; windowed at `fs=0` under KMSDRM shows the same picture (KMSDRM has no windows) — finding, not a failure |
| 7 | arm 2 (1080p H.264 High): fps and drops | **CPU-bound: 15–25 fps, `drop_late`/`drop_early` rising**; audio stays in sync (audio master) | 30 fps with 0 drops: better than expected (record) |
| 8 | arm 3 (VP9 + Opus 48 kHz): `Audio: opus, 48000 Hz` → resampled; fps 30 at 360p | codec breadth + resampling | `aresample` errors: component set |
| 9 | HDMI | the testsrc2 picture full screen, **aspect kept (1280×720 → 1920×1080 exactly)**, frame counter advancing between snapshots; 1080p arm same; VP9 360p scaled up | black with stats advancing: frames not presented — ffplay asks for an accelerated vsync'd renderer and SDL takes its **opengl** (desktop GL on EGL, the quakespasm-drm path) before **opengles2**: one-line A/B `export SDL_RENDER_DRIVER=opengles2`; stretched or corner: scaling |
| 9b | sound after each `VIDEO-PLAY done` (attended) | **the last ~0.19 s of audio loops on the jack** until the next file starts (§2 finding: the ring is not silenced on close) | silence: the driver or SDL does refill (correct the §2 finding) |
| 10 | faults | 0 kernel, 0 EL0 (`Exception #`), `KMS srv`/`V3DA srv` no errors | addr2line on the unstripped `build-out/ffplay-drm` first |

### `m10a1-hevc-cpu` — HEVC on the CPU decoder (runnable now)

**Question:** how fast is ffmpeg's CPU `hevc` decoder on the Pi at 720p and 1080p (the baseline the
rpivid path must beat)?

```
./scripts/test-cycle-psh-interact.sh --label m10a1-hevc-cpu --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 200 --hdmi-dense-on 'VIDEO-PLAY start' -- \
    "export THREADS=4" \
    "/bin/bash /bin/video-play /usr/share/m10/m10-hevc-720p30-aac.mp4" \
    "export FFPLAY_AUTOKEYS=60:quit" \
    "/bin/bash /bin/video-play /usr/share/demo/showcase1080.265"
```

| Line / observation | Predicted | If instead… |
|---|---|---|
| `Stream #0:0: Video: hevc (Main) … 1280x720` + aac | yes | — |
| 720p HEVC fps | **20–30 fps** (HEVC ≈ 1.5–2× H.264's CPU cost; mandelbrot is detail-heavy); drops reported | < 15: HEVC CPU decoding is not demo-able at 720p → the rpivid path (§4) is required for HEVC content |
| 1080p `.265` (raw stream, no audio: `M-V`) | **8–15 fps**, heavy `drop_*` | — |
| HDMI | the mandelbrot zoom full screen; the showcase clip | — |
| faults | 0 | addr2line |

### `m10a-ffplay` — ffplay windowed on the XFCE desktop, then full screen (after `m8a-quake-window`)

Runnable (`ffplay-wl` is staged), but **sequence it after M8's `m8a-quake-window`**, which is the
first Pi run of SDL's Wayland driver on this stack: a failure there must not be read as an ffplay
fault.

**Question:** does ffplay run as a Wayland client next to Thunar under `/bin/xfce-session-2`
(labwc composited on V3D), windowed at the clip's size with scaling on resize, then full screen
through its own `f` key and through `-fs`?

```
./scripts/test-cycle-psh-interact.sh --label m10a-ffplay --idle-secs 60 --max-cmd-secs 420 \
    --hdmi-dense-on 'VIDEO-PLAY start' -- \
    "export HOLD=150" \
    "export CONF_DIR=/usr/share/m10/labwc-xfce-m10" \
    "export M10_DELAY=45" \
    "export FFPLAY_AUTOKEYS=15:pause,19:pause,25:fs,35:fs,44:quit" \
    "/bin/bash /bin/xfce-session-2" \
    "export FS=1" \
    "export FFPLAY_AUTOKEYS=30:quit" \
    "/bin/bash /bin/xfce-session-2"
```

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | the m7i session rows (servers, bus, `labwc … conf=/usr/share/m10/labwc-xfce-m10 files=rc.xml,menu.xml,autostart,environment`, `session up panel=registered`, Thunar) | the desktop comes up as in m7i | as m7i's table |
| 2 | ~45 s later `VIDEO-PLAY start mode=wl player=/usr/bin/ffplay-wl … sock=wayland-0 video=wayland audio=phoenix fs=0` | the autostart launches it | `FAIL mode=wl … not staged`: staging; `Could not initialize SDL - …`: the SDL Wayland init (compare with m8a's `quakespasm-wl` lines) |
| 2b | `Input #0, mov,mp4…` + both streams, the stat lines advancing | the demuxer reads through the compat layer's `--wrap=close/write` (libavformat's `file` protocol uses plain fds; weston clients and quakespasm-wl link the same wraps) = pass-through | a demux error or short read **only in mode=wl** (the same clip plays in m10a0): the wrapper, not the demuxer |
| 3 | `ffplay-stat … win=1280x720 fs=0` | ffplay creates its window `SDL_WINDOW_HIDDEN|RESIZABLE` at the stream size and shows it with the first frame: a 1280×720 xdg-toplevel **next to Thunar** (labwc places it and decorates it) | `win=` other: labwc's first configure chose the size (rc.xml window rules / placement), not an ffplay fault — record it; the picture still scales to the window, aspect kept |
| 4 | fps 29–30 windowed; `drop_*` ≤ 2 % | the compositor path is not the bottleneck (m6i: a client scanned out at 60 fps; composited 30) | ~15 with `drop_late`: labwc's GLES2 composition rate (compare `labwc` damage/present lines); record against pixman (`export RENDERER=pixman`) |
| 5 | autokeys: pause/unpause as in m10a0 row 6; `key=fs` at 25 → `fs=1 win=1920x1080` (xdg-toplevel fullscreen), at 35 → back to `fs=0 win=1280x720` | the fullscreen toggle through the compositor | `fs=1` but `win=1280x720`: labwc did not configure the fullscreen size (finding) |
| 6 | arm 2 (`FS=1` → `-fs`): the first stat line already `fs=1 win=1920x1080` | fullscreen from the start | — |
| 7 | audio | as m10a0 row 4 | — |
| 8 | HDMI | arm 1: the XFCE panel + Thunar + the video window beside it, the frame counter advancing; at t≈25–35 the video full screen (panel hidden); arm 2 full screen | the window behind Thunar: stacking (fine); black window with stats advancing: EGL wayland buffers not presented |
| 9 | `XFCE session end reason=logout held=150s`, `XFCE-SESSION done rc=0`, 0 faults | clean | addr2line on the unstripped `ffplay-wl` |

### `m10c-gtk-video` — the GTK 3 player on the XFCE desktop (runnable now)

**Question:** does `gtk-video` run as a GTK 3 Wayland client under `/bin/xfce-session-2` next to
Thunar — picture, sound, toolbar — at the clip's rate, and do its controls (pause, seek, stop,
play, fullscreen) work? No SDL, no Mesa in this binary, so it does not depend on M8.

```
./scripts/test-cycle-psh-interact.sh --label m10c-gtk-video --idle-secs 60 --max-cmd-secs 420 \
    --hdmi-dense-on 'GTK-VIDEO open' -- \
    "export HOLD=150" \
    "export CONF_DIR=/usr/share/m10/labwc-xfce-m10" \
    "export M10_PLAYER=gtk" \
    "export M10_DELAY=45" \
    "export GTK_VIDEO_AUTOKEYS=15:pause,19:pause,22:right,28:fs,38:fs,44:stop,48:pause,56:quit" \
    "/bin/bash /bin/xfce-session-2" \
    "export GTK_VIDEO_AUTOKEYS=5:fs,40:quit" \
    "export M10_CLIP=/usr/share/m10/m10-h264-1080p30-aac.mp4" \
    "/bin/bash /bin/xfce-session-2"
```

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | the m7i session rows, `labwc … conf=/usr/share/m10/labwc-xfce-m10`, `session up panel=registered`, Thunar | as m7i | as m7i's table |
| 2 | ~45 s later `GTK-VIDEO start file=/usr/share/m10/m10-h264-720p30-aac.mp4 … autoexit=1`, `GTK-VIDEO decoder h264 (h264) threads=4` (FFmpeg's auto on 4 cores), `decoder aac`, `GTK-VIDEO open … video=h264 1280x720 audio=aac sound=on` | the autostart launches it; `/dev/audio0` opens | `sound=off` + `audio device /dev/audio0: …`: the node (is another process holding it?); a GTK abort: `Gtk-WARNING`/`cannot open display` lines — the session env (compare with Thunar's) |
| 3 | `GTK-VIDEO audio fltp 44100 Hz 2 ch -> S16 44100 Hz stereo`, `GTK-VIDEO scale 1280x720 yuv420p -> WxH (threads 2)` with W×H = the video area (≈ 1280×720 minus the toolbar, aspect kept) | yes | — |
| 4 | `GTK-VIDEO stat … fps=` at 720p windowed: **20–30 fps**, `dropped` growing slowly if < 30 | CPU-bound: libswscale (NEON) ≈ 5–10 ms + cairo 1:1 paint + the wl_shm copy into labwc (GLES2 upload) per frame, on top of the 4-thread H.264 decode | < 15: the cairo/shm path is too slow → GtkGLArea (§1e) is the next step; record `dropped` vs `decoded` to split decode from display |
| 5 | autokeys: `auto … action=pause` → stat lines `paused=1`, **constant `clock`**; at 19 `paused=0`; at 22 `GTK-VIDEO seek target=<clock+10>` and the next `clock` ≈ target (keyframe, +0–2 s) | the controls work | a seek line but the clock stays: the demuxer's seek on the NFS file (rc in the seek line) |
| 6 | at 28 `GTK-VIDEO fullscreen=1`, stat `fs=1 win=1920x1080` (the toolbar hidden), a new `GTK-VIDEO scale … -> 1920x1080`; at 38 `fullscreen=0`, the window size back | labwc honours xdg-toplevel fullscreen | `fullscreen=` never printed: labwc did not configure the state (finding: compare with ffplay's in m10a) |
| 7 | at 44 `GTK-VIDEO stop`, `seek target=0.00`, stat `clock=0.00 paused=1`; at 48 `play clock=0.00`, the clock advancing again | stop = back to 0 + paused | — |
| 8 | at 56 `GTK-VIDEO done` (autokey quit) | clean exit | a hang: the threads' join (the audio thread blocked in `write()`) |
| 9 | arm 2 (1080p, fullscreen at 5 s): fps; sound (attended) | **10–20 fps** fullscreen (swscale to 1920×1080 + 8 MB per frame through cairo and wl_shm); **sound continuous**: the feeder keeps ~120 ms queued in the ring, so a scheduling gap shorter than that (4 decoder threads + 2 swscale threads on 4 cores) does not underrun; silence (no looping tone) during the pause and after exit | crackle/repeats while the picture thread is busy: gaps longer than the fill → A/B with `export GTK_VIDEO_AUDIO_FILL_MS=180` (just under the ring's 190 ms) and `GTK_VIDEO_THREADS=3`; a looping tone after exit: the closing silence did not reach the ring |
| 10 | HDMI | arm 1: the XFCE panel, Thunar and the player window with its toolbar (icons: open, play/pause, stop, the seek bar moving, `0:12 / 0:45`), the testsrc2 picture with the frame counter; t≈28–38 the picture full screen without the toolbar; after stop the first frame and `0:00` | a black area with stats advancing: the draw path (cairo surface) — compare with gtk3-demo's image demos |
| 11 | `XFCE session end reason=logout`, `XFCE-SESSION done rc=0`, 0 faults | clean | addr2line on the unstripped `build-out/gtk-video/gtk-video` |

### Why ffplay-drm hung on quit, and ffplay-drm2 / ffplay-wl2 (2026-09-28, agent; checked 2026-09-29)

Not P16. **libphoenix creates an attribute-less condition variable on `CLOCK_MONOTONIC`** — a
deliberate local revert of upstream's POSIX default (`c283f2d`: the wall clock jumps 1970 → today
during boot, and upstream's `CLOCK_REALTIME` default hung vkQuake and broke STK). SDL's pthread
`SDL_CondWaitTimeout()` builds its deadline from `CLOCK_REALTIME`, so after the clock step every
timed wait lies ~56 years ahead. The generic semaphores sit on the same call (`SDL_PTHREADS_SEM` is
off here), so their timed waits are affected too. ffplay's read thread waits 10 ms at the end of
the file, and at EOF no decoder signals it any more. So `-autoexit` never fires (m10a1's frozen
`shown=892`), and a quit after the end joins that thread forever (m10a0). A quit mid-file returns,
because the decoders still wake the reader. m10a's windowed quit came before the end of the file.

- **Fix:** `tools/gpu-lane/video-player/sdl-patches/0011-pthread-cond-timeout-on-the-monotonic-clock-on-phoenix.patch`.
  On `__phoenix__` it creates SDL's condvars with `pthread_condattr_setclock(CLOCK_MONOTONIC)` and
  takes the deadline from the same clock; other platforms are unchanged. It is applied via
  `--extra-patches` into separate SDL build-outs and is **not** in sdl2-drm's shared default set yet.
  `build-ffplay.sh --sdl-out … --tag 2` links `ffplay-drm2` / `ffplay-wl2`, which `/bin/video-play2`
  runs with `-autoexit` on by default. `/bin/video-play` and the first binaries are unchanged.
- **Host proof** (`hosttest/condclock.sh`, re-run 2026-09-29): with `phx-condclock.c` forcing
  libphoenix's clock default on the host's SDL, **both hangs reproduce** (`-autoexit` rc 124 with
  14 frozen stat lines; quit after the end rc 124). A mid-file quit returns (rc 0). With 0011 both
  return (rc 0), and stock glibc returns (rc 0).
- **Gate string in the artifact:** `SDL_CreateCond` calls `pthread_condattr_init` +
  `pthread_condattr_setclock` in `ffplay-drm2` / `ffplay-wl2` (objdump: 2 calls) and not in
  `ffplay-drm` / `ffplay-wl` (0). Staged copies `cmp`-identical to `build-out/*.stripped`
  (`ffplay-drm2.stripped` `4e02fef9…`).
- **System-wide exposure** = KNOWN-ISSUES **P17**: every port that pairs a `CLOCK_REALTIME`
  deadline with a default condvar (POSIX-correct code) waits until signalled.

### `m10a0b-ffplay2` — the m10a0 + m10a1 arms again on `video-play2` (runnable now)

```
./scripts/test-cycle-psh-interact.sh --label m10a0b-ffplay2 --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 200 --hdmi-dense-on 'VIDEO-PLAY start' -- \
    "export FFPLAY_AUTOKEYS=12:pause,17:pause,20:right,30:fs,34:fs,40:quit" \
    "/bin/bash /bin/video-play2 /usr/share/m10/m10-h264-720p30-aac.mp4" \
    "export FFPLAY_AUTOKEYS=" \
    "export THREADS=4" \
    "/bin/bash /bin/video-play2 /usr/share/m10/m10-h264-1080p30-aac.mp4" \
    "/bin/bash /bin/video-play2 /usr/share/m10/m10-vp9-360p-opus.webm" \
    "/bin/bash /bin/video-play2 /usr/share/m10/m10-hevc-720p30-aac.mp4"
```

| # | Line | Predicted | If instead… |
|---|---|---|---|
| 1 | arm 1: `key=quit` at 40 s (after the end of the file: the +10 s seek) → **`VIDEO-PLAY done rc=0`** within ~2 s | 0011 wakes the reader | no `done` line: a second wait path (record the last stat line; `SDL_SemWaitTimeout` users) |
| 2 | arms 2–4 each end on their own at EOF (`-autoexit`) with `VIDEO-PLAY done rc=0` at ≈ the clip length + ≤ 3 s | yes | a frozen `shown=` with `fps=0.0`: 0011 not in the binary (check the start line's `player=`) |
| 3 | fps: 720p H.264 29–30; **1080p H.264 15–30** (m10a0 row 7, never measured); VP9 360p 30; HEVC 720p 29–30 (m10a1) | as registered | — |
| 4 | 0 kernel, 0 EL0 dumps after every quit/EOF (P16's 0010 is in this SDL) | clean | addr2line `build-out/ffplay-drm2` |

**Result `m10a0b-ffplay2` (chain94, 2026-09-29 21:13): ✅ PASS, all 4 rows.** Every arm ends by itself at
the end of the file (`-autoexit`): 720p H.264 `done rc=0 t=42` (the +10 s seek brought EOF to ≈ 40 s, so
`-autoexit` fired before the 40 s quit key — the same EOF wake-up row 1 tests), 1080p H.264 `t=33`
(30 s clip), VP9 360p `t=23`, HEVC 720p `t=33`; 0 EL0/kernel dumps. Steady fps (first window dropped):
720p **29.4–30.3** (pause and fs windows excluded), **1080p H.264 29.9–30.5 with drop_late 0**
(row 3 predicted 15–30), VP9 29.9–30.3, HEVC 720p 29.9–30.5. Pause holds the clock (11.68 → 11.66), `right`
seeks +10 s. **The KMSDRM player is complete: controls, full screen scaled, clean exit, H.264/HEVC/VP9.**

### `m10b-hevc-rpivid` — (placeholder, after §4 option 1 is built)

Pre-register when `hevc_rpivid` exists: the same clip through `-vcodec hevc_rpivid` vs `-vcodec
hevc`; predictions: 1080p at 30 fps with `drop_*` ≈ 0 and CPU well below the CPU decoder's;
bit-exactness by `framemd5` of both decoders over the first 60 frames, with gotcha 8's rare
corrupt frames counted separately.

## 6. Log

- 2026-09-28: evaluation (§1), audio (§2), HEVC design (§4). ffplay-drm built + host-tested +
  staged with the launcher and four synthetic clips; cycles `m10a0-ffplay-drm` and `m10a1-hevc-cpu`
  runnable now. M8's SDL (Wayland + KMSDRM) landed the same morning: `ffplay-wl` linked from its
  `link-inputs.txt` and staged; `m10a-ffplay` runs after `m8a-quake-window`.
- 2026-09-28: **`gtk-video`**, the GTK 3 player (toolbar with open/play-pause/stop/seek bar/time/
  fullscreen, `/dev/audio0` sound, cairo painting) written, built for Phoenix, host-tested on
  broadway (controls + all clips PASS, a pause mutant caught), staged with a `.desktop` entry;
  cycle `m10c-gtk-video` pre-registered.
- 2026-09-28: **Pi results (§3.4).** `m10a-ffplay` PASS: ffplay-wl plays windowed next to Thunar at 30 fps, and
  pause, seek, fullscreen and quit all work through the compositor. `m10a0` plays full screen at 30 fps but hangs on
  quit (KMSDRM teardown = P16). `m10a1`: HEVC 720p decodes in real time on the CPU. `m10c-gtk-video` running.
- 2026-09-29: `m10c-gtk-video` graded (§3.4): the GTK player works windowed at 30 fps with pause/seek/fullscreen/exit; stop/play
  untested (timing), full screen CPU-bound at 14–22 fps → GL present next.
- 2026-09-29: the KMSDRM quit hang and the EOF stall are one bug: SDL's timed condvar waits on a `CLOCK_REALTIME`
  deadline against libphoenix's `CLOCK_MONOTONIC` default. SDL patch 0011 fixes it (host test discriminates;
  gate string in the binaries); `ffplay-drm2`/`-wl2` + `/bin/video-play2` staged; cycle `m10a0b-ffplay2` registered.
- 2026-09-29: `m10a0b-ffplay2` PASS — ffplay-drm2 plays 720p/1080p H.264, VP9, HEVC at 30 fps and returns at EOF; quit hang gone.
