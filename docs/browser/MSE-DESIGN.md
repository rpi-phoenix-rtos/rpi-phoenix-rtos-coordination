# Streaming video in the browser: native HLS, then Media Source Extensions (HEVC first)

PLAN row B8 follow-up ("MSE-DESIGN.md"). Written 2026-10-07 (agent). Status: **design only** —
no code, no build, no Pi cycle. Every WebKit fact below was read from the 2.54 tarball tree
(`.buildroot/_build/aarch64a72-generic-rpi4b/port-sources/webkit_wpe-2.54.0/wpewebkit-2.54.0/`,
patches 0001–0023 + `webkit-video/0030` applied; paths below are relative to its `Source/`), every
FFmpeg fact from the video_player port's FFmpeg 6.1 tree (`port-sources/video_player-6.1/ffmpeg-6.1/`).
Anything taken from memory rather than read is marked **(unverified)**.

**Owner direction (2026-10-07):** YouTube is **not** a target. The priority is the Pi 4's
**hardware HEVC decoder** (rpivid, already in patch 0030 through `hevc_rpivid`; 1080p30 HEVC
measured on the block in a page, build 35). Software H.264 is acceptable only up to ~720p30; there
is no hardware H.264. Test targets: smaller platforms (PeerTube instances, Dailymotion, Vimeo)
and **our own HEVC streams served from the host**.

## 0. Summary

| Stage | What | Why first | New WebKit build? | Effort (agent-days) |
|---|---|---|---|---|
| **0** | **Native HLS** in `MediaPlayerPrivateFFmpeg`: `<video src=….m3u8>` through FFmpeg's `hls` demuxer, every playlist/segment/key fetched through WebKit's loader (not FFmpeg's network), **our own variant choice** (HEVC 8-bit ≤1080p, else H.264 ≤720p), live + VOD, `canPlayType` HLS answers, a MediaCapabilities factory; 0030's idle-player memory budget kept (lazy decoders: an unplayed player never takes rpivid) | The way Safari plays HLS; every byte stays on the proven 0030 path (demux thread → decoders → rpivid → compositor); no `ENABLE_MEDIA_SOURCE`; it is what hls.js/video.js-based sites fall back to while MSE is absent | **No** full rebuild: a video_player rebuild (FFmpeg + one `hls.c` hunk, ~2 min) + 3–4 WebCore TUs + relink | **4.5–6.5** |
| **1** | **MSE** (`ENABLE_MEDIA_SOURCE=ON`): `MediaPlayerPrivateFFmpegMSE` + `MediaSourcePrivateFFmpeg` + `SourceBufferPrivateFFmpeg` + `MediaSampleFFmpeg`, a small **fMP4 parser** of our own; samples go to 0030's decode/present/audio threads; `isTypeSupported`/`decodingInfo` steer to **hvc1/hev1** (rpivid) and **avc1 ≤720p**, AAC/Opus/AC-3 | hls.js, dash.js, Shaka, video.js-VHS and the platforms built on them need MSE | **Yes**, once (`cmakeconfig.h` changes: ~2 h) | **9–12** |
| 2 | WebM/VP9 + Opus in MSE, quality-switch and eviction polish, rendition switching in native HLS | breadth | no | 5–7 |

**Recommended path:** stage 0 now (small, isolated, exercises rpivid on real streaming
manifests), then stage 1 behind its own USE flag `mse` (default off) so the default image does not
pay the rebuild. Stage 1 keeps native HLS available: the launcher can turn MSE off per run
(`enable-mediasource`), which sends hls.js/video.js sites back to the native path where *we* choose
the variant.

**One consequence to keep in mind (§5):** the moment stage 1 ships, hls.js-based sites stop using
native HLS (their `Hls.isSupported()` becomes true) and hls.js's own ABR picks renditions, so in
stage 1 our `isTypeSupported` + `decodingInfo` answers *are* the codec steering.

## 1. Where we are

**Base:** the design builds on 0030 as on ports master after merge `3af6b08` (commit `9c0b6dc`,
2026-10-07, "FFmpeg player memory"): `preload="none"` honoured (nothing fetched until `play()`,
`prepareToPlay()` or a higher preload), read-ahead `WPE_PHOENIX_MEDIA_IDLE_AHEAD_MB` (2) until
the first play and `WPE_PHOENIX_MEDIA_AHEAD_MB` (16) after it (stream bytes + packet queues), the
stream buffer allocated once for its window, player threads at `WPE_PHOENIX_MEDIA_STACK_KB`
(2048), stat fields `ahead_kb buf_kb q_kb preload played players`. Reason: a page with seven
paused players reached 1.2 GB with the old player (NYT, build 50 survey). §6.10 and §7.8 keep that
budget for HLS and MSE.

- 0030 (`webkit_wpe/patches/webkit-video/0030-wpe-phoenix-ffmpeg-media-player.patch`):
  `MediaPlayerPrivateFFmpeg` = demux thread (libavformat over `FFmpegMediaStream`, an AVIOContext
  whose bytes come from WebKit's media resource loader, Range requests on seek) → two packet
  queues (serials for flush) → video thread (libavcodec; `hevc_rpivid` for HEVC; each picture
  waits for the clock, late ones dropped; `CoordinatedPlatformLayerBufferFFmpeg` uploads 8-bit
  4:2:0 planes, GPU YUV→RGB through Skia or TextureMapper) and audio thread (libswresample →
  44.1 kHz S16 stereo → `/dev/audio0`, ~120 ms ring fill model). Master clock = pausable wall
  clock, started at the first picture; audio follows it. End = both decoders drained a serial →
  `timeChanged()`; seek = flush serial + `avformat_seek_file` + skip pictures before the target.
- It answers `supportsType` only for `PlatformMediaDecodingType::FileOrHLS` and only for
  progressive containers (mp4/webm/mkv/ts/ogg/…): **no HLS MIME types**, so
  `canPlayType('application/vnd.apple.mpegurl')` is `""` today.
- Build: `-DENABLE_MEDIA_SOURCE=OFF -DENABLE_WEB_AUDIO=OFF -DENABLE_WEB_CODECS=OFF
  -DENABLE_ENCRYPTED_MEDIA=OFF -DUSE_GSTREAMER=OFF`, `USE_FFMPEG=ON` with USE `video`
  (`webkit_wpe/files/build-wpe.sh:504-555`).
- FFmpeg (video_player, `files/components.sh`): decoders h264, hevc (+`hevc_rpivid`), vp8, vp9,
  mpeg4, mpeg1/2, mjpeg, aac, aac_latm, mp3, opus, vorbis, flac, ac3, eac3, dca, alac, pcm;
  demuxers mov, matroska, mpegts, mpegps, avi, flv, ivf, raw h264/hevc, m4v, mjpeg, wav, ogg, mp3,
  aac, flac; parsers incl. h264/hevc/aac/opus/ac3; protocols **file, pipe only**;
  `--disable-network`. **No `hls` demuxer** (`config_components.h: CONFIG_HLS_DEMUXER 0`), no
  `dash` demuxer (needs libxml2), **no AV1 software decoder** (no libdav1d; FFmpeg 6.1's native
  `av1` decoder only drives hwaccels).

### 1.1 What the Pi decodes (measured unless marked)

| Codec | Measured | Source | Policy |
|---|---|---|---|
| HEVC 8-bit, rpivid | 1080p30 at 30 fps, `hw=1`, in a page (build 35) and in ffplay | B8-video.md, M10b | **≤1920×1080, ≤30 fps** first; 1080p60 = try and measure; 4K = no (the CPU SAND→planar de-tile + 12 MB/frame upload, not the block, is the limit) |
| HEVC 10-bit (Main10), rpivid | the block decodes 8 **or** 10 bit (`rpivid_hevc.c:221`) — not measured in a page | — | **gap:** 0030's layer buffer takes only 8-bit 4:2:0 (`supportsPixelFormat`), so 10-bit pictures go through `sws_scale` on the CPU (~1080p per frame). Prefer 8-bit renditions; Main10 only when no 8-bit HEVC or ≤720p H.264 exists; a 16-bit-plane upload path is a stage-2 item (needs `GL_R16`/`EXT_texture_norm16` on V3D — **unverified**) |
| HEVC CPU (`FFMPEG_RPIVID=0`) | 720p30 real time (m10a1, 4 threads) | M10 §m10a1 | fallback only |
| H.264 CPU | 720p30: 30 fps, 100 % painted over dma-buf in a page (build 42); 1080p30 synthetic testsrc2 clip: 29.9–30.5 fps in ffplay full screen | B8, M10 `m10a0b` | **≤1280×720, ≤30 fps** (owner); 1080p only when nothing else exists |
| VP9 CPU | 360p30 only (ffplay) | M10 | stage 2; measure 480p/720p before answering yes above 480p |
| AV1 | not built | — | answer **no** everywhere |
| AAC, Opus, AC-3/E-AC-3, MP3 | AAC + Opus played (B8, M10) | — | yes |

## 2. WebKit 2.54 facts that shape the design

| Fact | Where |
|---|---|
| `ENABLE_MEDIA_SOURCE` is defined OFF in WebKitFeatures, **defaulted ON for WPE** by `GStreamerDefinitions.cmake:4` (included unconditionally); our OFF is the explicit `-D` in build-wpe.sh. Its **only** dependency: `WEBKIT_OPTION_DEPEND(ENABLE_MEDIA_SOURCE ENABLE_VIDEO)`. Not tied to GStreamer. `ENABLE_MEDIA_SOURCE_IN_WORKERS` stays OFF (iOS only) | `cmake/WebKitFeatures.cmake:249-250,350`; `cmake/GStreamerDependencies.cmake:1-15` |
| GStreamer's MSE sources are only added with `USE_GSTREAMER` (0030 already gates `GStreamer.cmake`); the generic MSE code (`Modules/mediasource/*`, `MediaSourcePrivate.cpp`, `SourceBufferPrivate.cpp`, `TrackBuffer.cpp`, `MediaSourceTypeSupportedCache.cpp`, the mock backend) is in `Sources.txt` under `ENABLE(MEDIA_SOURCE)`; grep finds no GStreamer assumption in it | `WebCore/Sources.txt:237-249,2672-2717,2854-2857`; `WebCore/platform/GStreamer.cmake:3,18-20` |
| The Cocoa pieces (`SourceBufferParserWebM` over libwebm, `SourceBufferParserAVFObjC`, `MediaSampleAVFObjC`) are **not in the WPE tarball** (no `platform/graphics/cocoa`, no `ThirdParty/libwebm`), and they are CoreMedia-shaped anyway: **nothing to reuse** for parsing | `WebCore/SourcesCocoa.txt:380-497` lists them; directory absent |
| The GPU-process MSE proxies are not in the tarball; `MediaPlayerPrivateRemote`'s MSE stub is under `ENABLE(GPU_PROCESS)` (OFF here) | agent survey |
| Runtime prefs: `MediaSourceEnabled` default **true** when compiled in; `ManagedMediaSourceEnabled` default **true on WPE** (`defaultManagedMediaSourceEnabled()`); `SourceBufferChangeTypeEnabled` true without GStreamer; GLib setting `enable-mediasource` | `WTF/Scripts/Preferences/UnifiedWebPreferences.yaml:5773,5320,8287`; `WebKit/Shared/WebPreferencesDefaultValues.cpp:152-160`; `WebKit/UIProcess/API/glib/WebKitSettings.cpp:1472-1479` |
| `MediaSource.isTypeSupported` → `MediaSourceTypeSupportedCache` → `MediaPlayer::supportsType` with `platformType = MediaSource`; **with codecs present the engine must answer `IsSupported`** (`MayBeSupported` counts as no) | `WebCore/Modules/mediasource/MediaSource.cpp:996-1053` |
| An MSE load picks the engine whose `supportsTypeAndCodecs(platformType=MediaSource)` answers; GStreamer registers a **separate** MSE engine; `load(URL, LoadOptions, MediaSourcePrivateClient&)` is pure virtual for every engine | `WebCore/platform/graphics/MediaPlayer.cpp:327-370,442-475,575-716`; `MediaPlayerPrivate.h:66-70` |
| **`MediaCapabilities.decodingInfo()` answers `supported:false` for everything today**: the factory list is empty without Cocoa/GStreamer and the fallback is a default-constructed info (all false) | `WebCore/platform/mediacapabilities/PlatformMediaEngineConfigurationFactory.cpp:58-68,109-111`; model: `gstreamer/PlatformMediaEngineConfigurationFactoryGStreamer.cpp:46-71` |
| `SourceBufferPrivate` pure virtuals: `platformType()`, `appendInternal(Ref<SharedBuffer>&&) -> Ref<MediaPromise>`, `resetParserStateInternal()` (+ the two logger methods when release logging is on). Results go back through `didReceiveInitializationSegment(InitializationSegment&&)` and `didReceiveSample(Ref<MediaSample>&&)` **on the dispatcher** (main thread by default, `MediaSourcePrivate.cpp:124`); the append ends when the returned promise settles (`appendCompleted` is vestigial) | `WebCore/platform/graphics/SourceBufferPrivate.h:87,192-224`; `SourceBufferPrivate.cpp:1012-1041` |
| Playback hooks a backend overrides: `flush(TrackID)`, `enqueueSample(Ref<MediaSample>&&, TrackID)`, `allSamplesInTrackEnqueued(TrackID)`, `isReadyForMoreSamples(TrackID)`, `notifyClientWhenReadyForMoreSamples(TrackID)` (→ call `provideMediaData(trackID)` later), `canSwitchToType`, `platformMaximumBufferSize` | `SourceBufferPrivate.h:98,158,199-206` |
| The generic layer already does the MSE "coded frame processing": `TrackBuffer` (per-track sample maps, sync-sample search, append windows, timestamp offset, sequence mode, eviction, buffered ranges, gap policy), seek → `MediaSourcePrivate::waitForTarget(target)` (resolves once buffered) → `reenqueueMediaForTime(time)` → per track `flush` + `enqueueSample` from the preceding sync sample (earlier samples as non-displaying copies) | `TrackBuffer.cpp`; `MediaSourcePrivate.cpp:149-228,407`; `SourceBufferPrivate.cpp:445-493` |
| `MediaSourcePrivate` pure virtuals: `player()`, `setPlayer()`, `platformType()`, `addSourceBuffer(...)`, `notifyActiveSourceBuffersChanged()`; `currentTime()`/`timeIsProgressing()` call the player from **any thread** | `MediaSourcePrivate.h:86-98`; `.cpp:603-620` |
| readyState for MSE is computed by WebCore (`MediaSource::monitorSourceBuffers`) and pushed via `MediaSourcePrivate::setMediaPlayerReadyState` → `player->readyStateFromMediaSourceChanged()`; duration/buffered/seekable come from the MediaSource, not the engine | `Modules/mediasource/MediaSource.cpp:416-487,557`; `MediaSourcePrivate.cpp:485-500`; `HTMLMediaElement.cpp:4316,6430,8900` |
| `MediaSample` pure virtuals: pts, dts, duration, trackID, sizeInBytes, presentationSize (non-empty only for video: B-frame tracking keys on it), offsetTimestampsBy, setTimestamps, createNonDisplayingCopy, flags, platformSample, type; also implement `createCopyWithAdjustedStartTime`. **No generic subclass exists**; `PlatformSample`'s variant holds only `MockSampleBox*`/`CMSampleBuffer`/`GstSample*`; `MediaPlatformType` has only `Mock, AVFObjC, GStreamer, Remote` | `WebCore/platform/MediaSample.h:52-133`; `MediaPlayer.h:161-166`; `SourceBufferPrivate.cpp:1004` |
| The mock backend is the smallest complete template: player 337 lines, MediaSourcePrivate 123, SourceBufferPrivate 277; `MockMediaPlayerMediaSource::seekToTarget` = `waitForTarget` → `reenqueueMediaForTime` → resolve | `WebCore/platform/mock/mediasource/*` (cpp:251-286) |
| WPE's default user agent claims Safari (`… Version/60.5 Safari/605.1.15`) | `WebCore/platform/glib/UserAgentGLib.cpp:103-116` |

## 3. FFmpeg 6.1 HLS facts

| Fact | Where (`libavformat/hls.c` unless noted) |
|---|---|
| `hls_demuxer_select="adts_header ac3_parser mov_demuxer mpegts_demuxer"` — all already in our component set except the demuxer itself; **no network dependency** | `configure:3499` |
| Every playlist, segment, init section (`EXT-X-MAP`) and key is opened through `open_url()` → `s->io_open(s, pb, url, AVIO_FLAG_READ, &opts)`; the per-playlist sub-demuxers open nested files through the parent's `io_open` (`nested_io_open`) | `:641-726`, `:772`, `:1801`, `:2138` |
| **But** `open_url()` first requires `avio_find_protocol_name(url)` to name `http*`, `file` or `data`, else `AVERROR_INVALIDDATA` — with `--disable-network` there is no `http`/`https` protocol, so every remote open fails before reaching `io_open` | `:650-687` |
| Byte ranges (`EXT-X-BYTERANGE`) for http reach `io_open` as options `offset` / `end_offset`; AES-128 segments are opened as `crypto+<url>` with options `key`/`iv` (hex) — i.e. through FFmpeg's `crypto` protocol, which `io_open` would have to emulate; SAMPLE-AES is a separate (FairPlay-style) scheme | `:1289-1290`, `:1314-1324` |
| `hls_read_header` parses **every** media playlist of a master and opens a demuxer for **every** playlist (probing reads its first segment) before the caller can discard streams; it makes one AVProgram per variant (`variant_bitrate` metadata). Choosing a variant afterwards with `AVStream.discard` still costs one segment download per rendition at open | `:1954-1965`, `:1996-2005`, `:2018-2140` |
| VOD: `#EXT-X-ENDLIST`/`PLAYLIST-TYPE:VOD` → `finished=1`, `s->duration` = sum of segment durations, seekable (`hls_read_seek`). Live: no end list → reload loop (`max_reload`, `m3u8_hold_counters`), start `live_start_index` (default −3) segments from the end, `ff_check_interrupt` polled while waiting | `:852-915`, `:1477-1560`, `:1743-1760`, `:1977-1982` |
| fMP4 segments (`EXT-X-MAP` init section) go through the mov demuxer, TS through mpegts; HEVC in either needs nothing beyond what we have (hevc parser + decoder are in) | `:417` (`new_init_section`), `:860-878` (`#EXT-X-MAP`); configure select |

## 4. Who serves HEVC on the web (honest)

| Source | HEVC? | Confidence |
|---|---|---|
| Our own host streams (§9) | yes, by construction | certain |
| Apple's public HLS examples ("advanced" bipbop streams, Apple-oriented demos) | HEVC (fMP4, `hvc1`) variants next to H.264 in some masters | likely — **verify** by fetching the master and reading `CODECS` |
| Apple-ecosystem / broadcaster HLS (trailers, some HDR/4K services) | often HEVC for HDR/4K tiers (frequently Main10 → our 10-bit gap), H.264 for SD/HD | likely, varies; DRM services (FairPlay/Widevine) are out of scope anyway |
| Vimeo | H.264 for SDR; HEVC reported only for HDR (Dolby Vision/HDR10) uploads; AV1 on some | **unverified** |
| PeerTube (default transcoding profiles) | **H.264 + AAC** (libx264); HEVC only via third-party transcoding plugins on some instances | fairly confident for defaults; per-instance **unverified** |
| Dailymotion | H.264 | **unverified** |
| Twitch | H.264; HEVC/AV1 only in "enhanced broadcasting" trials for some channels | **unverified** |
| YouTube | no HEVC (VP9/AV1/H.264) — and not a target | confident |

**Verification method (no guessing):** for any candidate, fetch the master playlist with `curl`
on the host and read `#EXT-X-STREAM-INF:…CODECS="hvc1.…"` / `"hev1.…"`; on the Pi the stage-0
player logs every variant it saw (§6.8). Realistic expectation: **real-world HEVC is mostly
Apple-style HLS; the small platforms will play H.264 ≤720p on the CPU.** Our host-served
ladders (§9) are what proves the rpivid path end to end.

## 5. How sites choose between native HLS and MSE

| Player | Logic | Confidence |
|---|---|---|
| **hls.js** (recommended integration) | `if (Hls.isSupported()) hls.js; else if (video.canPlayType('application/vnd.apple.mpegurl')) video.src = url;` — `isSupported()` = a MediaSource (since 1.5 it **prefers `ManagedMediaSource`**, option `preferManagedMediaSource`) + `isTypeSupported('video/mp4; codecs="avc1.42E01E,mp4a.40.2"')` | from memory, **unverified** for the exact version a site ships |
| **video.js / VHS** (`@videojs/http-streaming`) | `overrideNative` defaults to `!videojs.browser.IS_ANY_SAFARI`; our UA claims Safari (§2) → **native HLS is preferred whenever `canPlayType` says yes**, even with MSE present | **unverified**; check `navigator.userAgent` + VHS version on a real site |
| **Shaka / dash.js** | MSE only (HLS in Shaka via MSE too); without MSE they report "unsupported browser" | fairly confident |
| **PeerTube** | own player on hls.js (+ p2p-media-loader); instances usually also keep "web video" progressive MP4 files, which 0030 already plays — whether the player falls back to them without MSE is **unverified** | — |
| Dailymotion, Vimeo | proprietary players on MSE; Safari gets native HLS (UA/feature checks) | **unverified** |

**Consequences for the design:**
1. **Stage 0 (MSE absent):** hls.js- and video.js-based players fall back to `video.src = m3u8` once
   `canPlayType` answers `"maybe"`. That is our path, with our variant choice.
2. **Stage 1 (MSE present):** hls.js takes over (it will prefer `ManagedMediaSource` because WPE
   exposes it by default). hls.js then transmuxes TS→fMP4 itself and its ABR picks renditions
   among those whose `CODECS` pass `MediaSource.isTypeSupported` — so our MSE answers decide
   whether HEVC renditions are even candidates and whether 1080p H.264 is offered. Decisions:
   - ship stage 1 with `ManagedMediaSourceEnabled` **off** at first (launcher, through the WPE
     feature API `webkit_settings_set_feature_enabled` — **verify the identifier** in the 2.54
     feature list) to keep one surface; turn it on once plain MSE passes;
   - the launcher gets `--mse=on|off` (WebKitSettings `enable-mediasource`), default **on** once
     stage 1 passes its gate, so a site that misbehaves on hls.js can be retried on native HLS;
   - video.js/Safari-sniffing sites keep using native HLS even then (consequence 1 stays live).

## 6. Stage 0 — native HLS in `MediaPlayerPrivateFFmpeg`

### 6.1 Approach: FFmpeg's demuxer, WebKit's network (decided)

Two ways to get bytes to the `hls` demuxer:

- (A) `--enable-network` + FFmpeg's `http/https/tls` protocols (OpenSSL backend). Rejected: a
  second HTTP+TLS stack inside the WebProcess that bypasses the NetworkProcess (no cookies, no
  CORS/mixed-content checks, no proxy, no cache), and lwIP sockets from a sandbox-less WebProcess.
- **(B) chosen:** keep `--disable-network`; set `AVFormatContext::io_open`/`io_close2` on the
  player's context so **every URL the demuxer opens becomes an `FFmpegMediaStream`** (0030's
  loader-backed AVIO, one per URL), plus one small FFmpeg hunk so `hls.c` accepts http(s) URLs
  when the caller supplies custom I/O. Bytes, cookies, TLS (glib-networking/OpenSSL in the
  NetworkProcess), CORS and the HTTP cache are WebKit's, exactly as for progressive `<video>`.

### 6.2 FFmpeg changes (video_player port)

1. `files/components.sh`: add `hls` to `FF_DEMUXERS` (its selects are already in). Optional:
   `webvtt` demuxer for `EXT-X-MEDIA:TYPE=SUBTITLES` (later). Verify in the recipe's
   `config_components.h` check list: `CONFIG_HLS_DEMUXER 1`, `CONFIG_HTTP_PROTOCOL 0`.
2. New hunk `files/rpivid/patches/`-style but separate: **`video_player/patches/0002-hls-custom-io.patch`**
   (LGPL, like the file it changes), ~15 lines in `open_url()`: when the context has
   `AVFMT_FLAG_CUSTOM_IO` and `proto_name` is NULL, classify by scheme prefix (`http://`,
   `https://` → `is_http = 1`; `crypto+http…`/`crypto:` → keep the `crypto+` prefix and let
   `io_open` handle it) instead of returning `AVERROR_INVALIDDATA`. Keep `file` handling as is.
   Host-testable: the hunk + a tiny program that opens a master playlist with a fake `io_open`
   serving files from a directory (§9.4).
3. Nothing else in FFmpeg: `http_persistent`/`http_multiple` only change *how many* `io_open`
   calls happen (our loader keeps connections alive in the NetworkProcess anyway); set
   `http_persistent=0`, `http_multiple=0` in the demuxer options to keep the call pattern simple.

### 6.3 WebCore changes (patch 0030 → a follow-up patch `webkit-video/0031-ffmpeg-hls.patch`)

Kept as a separate patch file so 0030 stays reviewable; both apply only with USE `video`.

- **`FFmpegMediaStream`**: allow creation from the demux thread (drop the main-thread assertion;
  `open()` of `file://` is thread-safe, remote requests are already posted to the main thread);
  accept a byte range `[offset, end_offset)` (Range header `bytes=a-b`, size = range length,
  reads relative to the range); expose `contentType()` (from the response, for sniffing).
- **`io_open` / `io_close2`** on `m_format` (the context's `opaque` = the player): create an
  `FFmpegMediaStream` for the URL (resolved by hls.c already), honour options `offset`/`end_offset`;
  for `crypto+<url>` / `crypto:<url>` wrap the stream's AVIO in an **AES-128-CBC decrypting
  AVIOContext** (libavutil `av_aes`, key/iv from the options dict, PKCS#7 padding stripped at
  the stream's end; ~80 lines). Keys themselves arrive through a plain `io_open` of the key URI.
  SAMPLE-AES / anything DRM: refuse (log `error hls-encryption=SAMPLE-AES unsupported`).
- **`interrupt_callback`** on `m_format` returning `m_quit`, so `stop()` aborts a blocked live
  reload or segment fetch (hls.c polls `ff_check_interrupt`), plus `FFmpegMediaStream::cancel()`
  for every open stream (the player keeps a list).
- **HLS detection** in `load()`: MIME type from the element (`application/vnd.apple.mpegurl`,
  `application/x-mpegurl`, `audio/mpegurl`, `audio/x-mpegurl`), or the URL path ending `.m3u8`, or
  the first bytes `#EXTM3U` (sniffed on the demux thread). HLS → §6.4; anything else → 0030 as today.

### 6.4 Variant choice is ours, not FFmpeg's

Because `hls_read_header` would open (and fetch a segment of) **every** rendition (§3), the player
reads the master playlist itself and hands the demuxer a **filtered master**:

1. Fetch the URL completely (playlists are small; cap 1 MiB). If it has `#EXTINF` lines it is a
   media playlist → give it to the demuxer unchanged (no choice to make).
2. Parse `#EXT-X-STREAM-INF` (`BANDWIDTH`, `AVERAGE-BANDWIDTH`, `CODECS`, `RESOLUTION`,
   `FRAME-RATE`, `AUDIO`, `SUBTITLES`, `VIDEO-RANGE`) and `#EXT-X-MEDIA` (`TYPE`, `GROUP-ID`,
   `NAME`, `LANGUAGE`, `DEFAULT`, `AUTOSELECT`, `URI`, `CODECS` when present). Ignore
   `#EXT-X-I-FRAME-STREAM-INF`. ~250 lines, host-tested (§9.4).
3. Score each variant (`FFmpegHLSPolicy`, one function, logged):
   - **eligible video:** `hvc1.1.*`/`hev1.1.*` (Main, 8-bit) with `width×height ≤ 1920×1080` and
     `FRAME-RATE ≤ 30` (≤ 60 behind `WPE_PHOENIX_HLS_HEVC_MAX_FPS`); `avc1`/`avc3` with
     `width×height ≤ 1280×720`, `FRAME-RATE ≤ 30`; `hvc1.2.*` (Main10) eligible but ranked below
     8-bit HEVC and below H.264 720p (the CPU conversion, §1.1); `dvh1`/`dvhe` (Dolby Vision),
     `av01`, `vp09` not eligible. `VIDEO-RANGE=PQ/HLG` → not eligible (no HDR output).
   - **rank:** eligible HEVC 8-bit by resolution (highest ≤ cap) > eligible H.264 by resolution >
     Main10 HEVC > the lowest-bandwidth variant whatever it is (last resort, logged
     `hls choose=fallback`). Ties: lower `BANDWIDTH`.
   - **audio group:** `DEFAULT=YES` rendition, else first; codecs `mp4a.40.2/5/29`, `opus`,
     `ac-3`, `ec-3`, `mp3` accepted; `ec-3` with JOC (Atmos) still decodes as 5.1 → downmixed by
     swresample.
   - **no `CODECS` attribute** (allowed by the spec): rank by `RESOLUTION` only and assume H.264;
     if the decoder then reports HEVC, fine (rpivid takes it).
   - Knobs: `WPE_PHOENIX_HLS_VARIANT=<index>` forces one; `WPE_PHOENIX_HLS_POLICY=lowest|highest`
     for tests; `WPE_PHOENIX_HLS_H264_MAX=1280x720`, `WPE_PHOENIX_HLS_HEVC_MAX=1920x1080`.
4. Synthesize a master with **one** `#EXT-X-STREAM-INF` (the chosen variant, its attributes
   copied) and only its `#EXT-X-MEDIA` groups, every `URI` made absolute against the real master
   URL, and pass it to `avformat_open_input` (format forced to `av_find_input_format("hls")`)
   through a memory AVIO, with `url` = the real master URL so relative URLs inside the media
   playlists still resolve (hls.c resolves media-playlist entries against the media playlist's
   own URL — **check in the host harness** that nothing resolves against the synthesized
   master's text).
5. After `avformat_open_input`: one video and one audio `AVStream` remain → 0030's
   `av_find_best_stream` + `openDecoder`; HEVC → `hevc_rpivid`. In HLS mode the decoders open at
   the first `play()`, not at metadata (§6.9a).
6. No rendition switching in stage 0 (a fixed variant). Stage 2 adds a **down-switch** when the
   `stat` line shows sustained drops (`dropped/presented > 5 %` over 10 s): re-run §6.4 with the
   next lower variant and reopen at `currentTime` (a seek-shaped restart, ~1 segment of stall).

### 6.5 Live and VOD

- **Startup:** 0030's `openContainer()` runs `avformat_find_stream_info` with default
  `probesize` (5 MB) / `analyzeduration`; on an HLS context that can read several segments before
  the first frame. The filtered master already fixes the stream set, so HLS mode caps both
  (e.g. 1 MB / 2 s) — a startup-latency knob, logged.
- **VOD** (`finished=1`): `duration` = `s->duration` (sum of `#EXTINF`), `maxTimeSeekable` =
  duration (0030 returns 0 without a byte size: HLS mode returns the duration), seek through the
  existing `avformat_seek_file` path (hls.c maps it to a segment + in-segment skip).
- **Live** (no end list): `duration` = +∞ (`MediaTime::positiveInfiniteTime()`; the controls show
  live), `movieLoadType()` = `LiveStream`, `currentTime` = clock − first pts (as now), seekable
  range = the playlist window (from `pls->segments`; exposed through a `seekable()` override,
  **check** which of `seekable()`/`maxTimeSeekable()`/`liveUpdateInterval()` HTMLMediaElement
  reads for a non-MSE live stream), seeks outside it refused. Start position: hls.c's default
  3 segments from the live edge. The demux thread's queue limit already throttles; the reload
  loop waits inside `av_read_frame`, aborted by the interrupt callback.
- **`EXT-X-DISCONTINUITY`** (ad insertion, TS timestamp resets): the mpegts timestamps jump; hls.c
  does not rebase them. Risk for A/V sync (the clock follows pts): stage 0 detects a pts jump >
  1 s backwards/forwards on the video thread, logs `discontinuity`, and re-anchors the clock at
  the next picture (the seek path's "first picture" logic). Our own test ladders have none; a
  live test with one is in §9.2.

### 6.6 Type answers (stage 0)

`mimeTypeCache()` gains `application/vnd.apple.mpegurl`, `application/x-mpegurl`,
`audio/mpegurl`, `audio/x-mpegurl`. `supportsType(FileOrHLS)` for those types:

| Query | Answer | Why |
|---|---|---|
| `canPlayType('application/vnd.apple.mpegurl')` (no codecs) | `"maybe"` | spec: no "probably" without codecs (Safari answers "maybe" here too) |
| `…; codecs="hvc1.1.6.L120.90,mp4a.40.2"` / `avc1.…` / `mp4a`, `ec-3`, `opus` | `"probably"` | codec decodable; the *resolution* cap is applied in the variant choice, not here (a master lists many) |
| `…; codecs="av01…"` / `"vp09…"` / `"dvh1…"` alone | `""` | not decodable / no HDR |
| `video/mp4; codecs="hvc1…"` (progressive) | `"probably"` (unchanged) | 0030 |

Also add `hev1`, `dvh1` handling to `codecIsSupported` (0030 accepts `hvc1/hev1`; add an explicit
reject list `av01, dvh1, dvhe, vp09.02` (10-bit VP9)).

### 6.7 MediaCapabilities (stage 0, cheap)

Add `platform/graphics/ffmpeg/PlatformMediaEngineConfigurationFactoryFFmpeg.cpp` (~120 lines)
and append it in `PlatformMediaEngineConfigurationFactory.cpp`'s `defaultFactories()` under
`USE(FFMPEG)` (an edited TU, no `cmakeconfig.h` change). Answers (same table drives stage 1):

| Configuration | supported | smooth | powerEfficient |
|---|---|---|---|
| HEVC 8-bit ≤1920×1080 ≤30 fps | true | true | **true** (rpivid) |
| HEVC 8-bit ≤1080p60, ≤4096² | true | false | true |
| HEVC Main10 ≤1080p30 | true | false | false |
| H.264 ≤1280×720 ≤30 fps | true | true | false |
| H.264 ≤1920×1080 ≤30 fps | true | false | false |
| H.264 above that | true | false | false |
| VP9 profile 0 ≤640×360 (stage 2: ≤ measured cap) | true | true | false |
| VP9 above / VP9 profile 2 / AV1 | false | false | false |
| AAC, Opus, AC-3/E-AC-3, MP3, FLAC, Vorbis | true | true | true |

Players that consult `decodingInfo` (Shaka does whenever the API exists; hls.js ≥ 1.5 filters
levels through it, option `useMediaCapabilities` — **unverified**) then avoid >720p H.264 as "not
smooth" and prefer the power-efficient HEVC tier. **This factory is a hard prerequisite of
stage 1:** with MSE on and today's empty factory list, every `decodingInfo` answer is
`supported:false`, so such players would see zero playable levels and fail before the first
`appendBuffer`, however good the `isTypeSupported` answers are. It ships in stage 0 so it is
already proven when stage 1 lands.

### 6.8 Logging (new `WPEB-MEDIA` lines)

`hls master url=<u> variants=<n> audio-groups=<n>`, one `hls variant i=<k> bw=<b> res=<w>x<h>
fps=<f> codecs=<c> eligible=<0|1> reason=<r>` per variant, `hls choose i=<k> rule=<hevc8|h264|
main10|fallback|forced> audio=<name>`, `hls live=<0|1> segments=<n> target=<s>`, `hls open
url=<u> range=<a>-<b> crypto=<0|1>` per `io_open` (rate-limited after 20), `hls reload`,
`discontinuity pts=<a>-><b>`, `canplaytype type=<t> answer=<a>` (all three engines' answers,
also for `isTypeSupported` in stage 1), `capabilities type=<t> codec=<c> <w>x<h>@<f> supported=…
smooth=… efficient=…`.

### 6.9a Memory per HLS player (idle/paused players stay small)

Rules on top of 0030's master budget (§1):
- **One budget per player, not per URL.** The idle/active read-ahead (2 / 16 MiB) is shared by
  all of a player's `FFmpegMediaStream`s (playlist, init section, current video and audio
  segment): each stream's window is the player's remaining budget, allocated once, released at
  `io_close2` (hls.c closes a segment when it is consumed). `http_multiple=0` keeps hls.c from
  opening the next segment early (one in flight per playlist). Playlists are read whole but capped
  at 1 MiB; the master text is freed after the synthesized master is built.
- **`preload`:** `none` → nothing fetched (as 0030); `metadata` → master + chosen media
  playlist(s) + the init section only (enough for duration/size/tracks: fMP4 `moov`; for TS the
  first segment's first 188-byte packets up to the PMT + first video access unit), then the demux
  thread parks; `auto` → as `metadata` plus up to the idle budget of segment data.
- **Decoders are opened lazily** in HLS mode: at the first `play()`/`prepareToPlay()`, not at
  metadata. A paused, never-played HLS player therefore holds no decoder, no frame threads, no
  decoded pictures and — important for the owner's goal — **does not take the rpivid block**
  (`/tmp/.rpivid.lock`, one owner system-wide) away from the player the user actually starts.
  Cost: the first frame (poster-less preview) appears only after play; pages normally set a
  `poster`. (The same lazy open is worth back-porting to progressive mode; separate change.)
- Decoder threads: H.264 frame threads capped at 3 in HLS mode (`WPE_PHOENIX_MEDIA_THREADS`
  overrides) — 720p30 needs < 3 cores; `hevc_rpivid` runs one thread when the block decodes.
- Live: the reload loop holds only the playlist text; a paused live player stops reloading after
  its idle budget is full (resumes at the live edge on play, like Safari's "jump to live").
- Stat line additions: `hls_streams=<open> hls_kb=<bytes in their windows> variant=<i>`.

### 6.9 Build and rebuild cost

- video_player: components + one hunk → its FFmpeg rebuild (~2 min); `webkit_wpe` relinks
  (link-closure hash) and recompiles the edited TUs (`MediaPlayerPrivateFFmpeg.cpp`,
  `FFmpegMediaStream.cpp`, the new playlist/policy/AES files, the factory list). **No full
  WebKit rebuild** (no CMake option changes). Link check additions in `build-wpe.sh`:
  `ff_hls_demuxer`, the `hls choose` format string.
- Size: the hls demuxer is ~3 k lines of C; negligible.

## 7. Stage 1 — Media Source Extensions over FFmpeg

### 7.1 Shape (mirrors GStreamer's split, sized like the mock backend)

```
platform/graphics/ffmpeg/
  FFmpegPlaybackEngine.{h,cpp}        extracted from 0030: the clock, the video thread + presentFrame,
                                      the audio thread + /dev/audio0, counters; input = two
                                      FFmpegPacketQueues (serial, end, abort) of FFmpegPacket
                                      {AVPacket ref, Ref<FFmpegCodecConfig>, displaying flag}
  MediaPlayerPrivateFFmpeg            progressive + native HLS: demux thread -> engine queues (as now)
  MediaPlayerPrivateFFmpegMSE         MSE engine: registered separately (identifier FFmpegMSE),
                                      answers only platformType == MediaSource
  MediaSourcePrivateFFmpeg            addSourceBuffer (type check, creates SourceBufferPrivateFFmpeg),
                                      notifyActiveSourceBuffersChanged, durationChanged -> player
  SourceBufferPrivateFFmpeg           appendInternal -> parser thread; enqueue/flush/readiness -> engine
  MediaSampleFFmpeg                   one coded sample (AVPacket ref + MediaTimes + flags + config)
  FFmpegFMP4Parser                    ISO-BMFF init/media segment parser, append-boundary safe
  (stage 2) FFmpegWebMParser          EBML/Matroska Tracks + Cluster/SimpleBlock/BlockGroup
```

Enum/plumbing hunks (stage 1 patch `webkit-mse/0032-ffmpeg-mse.patch`, applied with USE `mse`):
`MediaPlatformType::FFmpeg` (`MediaPlayer.h`), `MediaPlayerMediaEngineIdentifier::FFmpegMSE` +
`MediaPlayerType::FFmpegMSE` (+ the IPC serializer and the remote player switch, as 0030 did for
`FFmpeg`), `MediaSample::Type::FFmpegSample` and a `USE(FFMPEG)` alternative `const AVPacket*` in
`PlatformSample`'s variant, registration in `buildMediaEnginesVector()` under
`USE(FFMPEG) && ENABLE(MEDIA_SOURCE)`, `platform/FFmpeg.cmake` source list.

### 7.2 `MediaSampleFFmpeg`

Holds: `AVPacket*` (a ref: the payload buffer is shared, never copied), `MediaTime pts, dts,
duration` (exact rationals: `MediaTime(value * tb.num, tb.den)` from the track timescale),
`TrackID`, `SampleFlags` (`IsSync` from the trun/sample flags), `FloatSize` (video only, from the
config), `Ref<FFmpegCodecConfig>`. `offsetTimestampsBy`/`setTimestamps` change only the MediaTimes;
`createNonDisplayingCopy` copies with `IsNonDisplaying`; `createCopyWithAdjustedStartTime` shifts
pts/dts and shortens duration (audio trimming at append-window edges); `isDivisable()` false at
first (WebKit then drops partial audio frames at window edges — acceptable). `platformSample()`
returns the packet; `type()` = `FFmpegSample`. When enqueued, the engine converts the MediaTimes to
the decoder's `pkt_timebase` (we set 1/90000 for video, 1/sample_rate for audio).

`FFmpegCodecConfig` (ThreadSafeRefCounted): `AVCodecParameters` copy (codec id, extradata =
`avcC`/`hvcC`/AudioSpecificConfig/OpusHead, width, height, sample rate, channels), the RFC 6381
codec string, a generation number. It is also the `MediaDescription` subclass the init segment
carries (`codec()`, `isVideo/isAudio/isText`).

### 7.3 Parsing appends: a small fMP4 parser of our own (decided)

Why not libavformat's mov demuxer on an in-memory AVIO: MSE appends arrive split at arbitrary
byte boundaries, an append must end with exactly the complete samples parsed (the promise settles
then), a second `moov` (quality switch) must re-configure (mov ignores a duplicate `moov` in a
fragmented stream — **from memory, check**), `resetParserState()`/`abort()` must discard a half
box, and the demuxer's open would block for the first `mdat` before reporting the init segment.
Chromium and WebKit-Cocoa both use dedicated segment parsers for the same reasons. The box layer
needed for MSE fMP4 is small:

- **Top level:** `ftyp`/`styp` (skip), `moov` (init segment), `moof`+`mdat` (media segment),
  `sidx`, `emsg`, `prft`, `free`, `skip`, `uuid` (skip). A box is processed only when complete
  (pending buffer across appends; `mdat` of a 2–6 s segment is 0.5–4 MB, fine).
- **`moov`:** `mvhd` (timescale), `mvex/trex` (per-track defaults), `mehd` (fragment duration →
  init segment duration, else +∞); per `trak`: `tkhd` (track ID, width/height), `mdia/mdhd`
  (timescale, language), `hdlr` (`vide`/`soun`/`text`), `stsd` sample entry: `avc1/avc3` (`avcC`),
  `hvc1/hev1` (`hvcC`), `mp4a` (`esds` → AudioSpecificConfig; object type 0x40/0x67), `Opus`
  (`dOps` → OpusHead: 19 bytes + channel mapping), `ac-3`/`ec-3` (`dac3`/`dec3`, no extradata
  needed), `fLaC` (`dfLa`), `vp09` (`vpcC`, stage 2); `encv`/`enca`/`sinf` → refuse (no EME:
  append fails with a decode error and a log line); `edts/elst` → audio priming offset only
  (applied as a negative pts shift + trimming, **verify** what Chrome/Safari do for AAC priming in
  fMP4; YouTube-style streams have none).
- **`moof`:** `mfhd`, per `traf`: `tfhd` (track, base-data-offset / default-base-is-moof,
  defaults), `tfdt` (baseMediaDecodeTime, v0/v1), each `trun` (sample count, data offset, first
  sample flags, per-sample duration/size/flags/composition offset v0 unsigned / v1 signed) →
  samples: `dts = base + Σ durations`, `pts = dts + cto`, sync = `!sample_is_non_sync_sample`
  (and `sample_depends_on == 2` as a hint), payload = slice of `mdat` (the AVPacket references
  the append's buffer; one `av_buffer_create` per append wrapping the `SharedBuffer`, no copies).
  `saio/saiz/senc` → refuse (encrypted).
- **Payload format:** with the `avcC`/`hvcC` in `extradata`, libavcodec's h264/hevc decoders take
  the length-prefixed NAL units directly (no bitstream filter); `hevc_rpivid` is FFmpeg's HEVC
  decoder plus the hwaccel, so it sees exactly what it sees from the mov demuxer today.
- **Threads:** `appendInternal` queues the `SharedBuffer` to a per-SourceBuffer parser
  `WorkQueue`; the parser emits `InitializationSegment` / samples by `ensureOnDispatcher` posts
  in order, then resolves the append promise from the dispatcher after the last sample was posted
  (ordering guaranteed by the serial dispatcher). Parse error → reject with `ParsingError`
  (WebCore runs the append-error algorithm). `resetParserStateInternal` drops the pending bytes
  and any half box; the last init segment's configs are kept.
- **Init segments mid-stream:** a new `moov` produces a new `InitializationSegment` (same track
  count/types, new `FFmpegCodecConfig`s, generation+1); WebCore validates it against the first one
  (MSE §init-segment). Samples after it carry the new config → §7.4 reopens the decoder.
  `canSwitchToType()` → true for any type `supportsType(MediaSource)` accepts (`changeType()`
  avc1↔hvc1 included).
- **Size:** ~900 lines + a host harness (§9.4) that compares its samples (pts/dts/duration/flags/
  size/md5) against FFmpeg's mov demuxer on whole files for every fMP4 the test set produces,
  and fuzzes append boundaries (every split point of a 3-segment stream, and random splits).

### 7.4 Decode and present: 0030's threads fed by samples

- **Refactor** 0030 into `FFmpegPlaybackEngine` (no behaviour change for the progressive/HLS
  player; gated by B8's existing Pi check before stage 1 builds on it).
- `SourceBufferPrivateFFmpeg::enqueueSample(sample, trackID)` → the track's engine queue as an
  `FFmpegPacket` (displaying = `!isNonDisplaying()`). `flush(trackID)` → `queueFlush` (new
  serial: the decode thread calls `avcodec_flush_buffers`). `allSamplesInTrackEnqueued` →
  `queueSetEnd` (drain → 0030's end path). `isReadyForMoreSamples` → queue below **2 s of media
  or 120 packets (video) / 2 s (audio)**; `notifyClientWhenReadyForMoreSamples` → the decode
  thread posts `provideMediaData(trackID)` to the dispatcher once below 1 s.
- **Codec switching:** the decode thread compares the packet's `FFmpegCodecConfig` generation with
  its decoder's; on change (always at a sync sample) it drains the old decoder (keeps its pictures),
  frees it and opens one for the new config — the same `openDecoder()`, so HEVC lands on rpivid
  again (the block's lock is held by the process, `/tmp/.rpivid.lock`). Resolution changes then
  reach `presentFrame` (a new layer buffer size) and `sizeChanged()` is posted.
- **Non-displaying samples** (decode-only, from seeks and append-window trimming): decoded, not
  presented, not counted as dropped — replaces 0030's "skip pts < target" heuristic in MSE mode.
- **Clock:** the same pausable wall clock, but **running only while `!paused && readyState ≥
  HaveFutureData && !seeking`**: when WebCore drops readyState on an underrun
  (`readyStateFromMediaSourceChanged()`), the clock holds, video keeps the last picture, audio
  writes silence; when data returns the clock resumes at the next picture's pts (no jump).
  `timeIsProgressing()` = clock running. `currentTime()` and `currentOrPendingSeekTime()` are
  lock-protected (called from the dispatcher by `MediaSourcePrivate`).
- **Seek** (`MockMediaPlayerMediaSource` pattern): hold the clock at the target → `msp->waitForTarget(target)` →
  `msp->reenqueueMediaForTime(time)` (WebCore flushes and re-enqueues from the preceding sync
  sample, earlier ones non-displaying) → the first displaying picture resolves the seek promise
  (0030's `didShowFirstFrame`) → `timeChanged()`. A newer seek rejects the older promise (as now).
- **Duration / buffered / seekable:** from the MediaSource (`msp->duration()` on
  `durationChanged`); `buffered()` not used for MSE. **End:** `markEndOfStream` →
  `mediaSourceHasRetrievedAllData()` → NetworkState Loaded; the drains of all tracks → `didEnd`.
- **Audio:** unchanged path (swresample → 44.1 kHz S16 → `/dev/audio0`, one writer system-wide).
  Audio-only SourceBuffers (podcasts, music sites) release the clock at the first sound (0030
  already handles no-video).
- **`videoPlaybackQualityMetrics()`** (total = presented + dropped + late, dropped = late drops,
  corrupted 0): ABR players read `getVideoPlaybackQuality()` to down-switch when the Pi drops
  frames — the main *runtime* steering besides `isTypeSupported`.
- **Buffer limits:** see §7.8.

### 7.5 Type answers (the steering)

`MediaPlayerPrivateFFmpegMSE::supportsType` (platformType `MediaSource`) — must answer
`IsSupported` when codecs are present (§2):

| Type | Answer |
|---|---|
| `video/mp4` + `hvc1.1.*`/`hev1.1.*` (Main 8-bit) | yes |
| `video/mp4` + `hvc1.2.*`/`hev1.2.*` (Main10) | yes (decodes; CPU conversion), `decodingInfo` smooth=false |
| `video/mp4` + `avc1.*`/`avc3.*` | yes — **level-capped**: level ≤ 3.1 (`avc1.xx001f`, 720p30) yes; level 3.2–4.2 yes only with `WPE_PHOENIX_MSE_H264_1080=1`, else **no** (removes 1080p H.264 from hls.js/Shaka candidate lists; many ladders tag 720p as level 3.1 and 1080p as 4.0/4.1/4.2). Extended parameters `width=`/`height=`/`framerate=` (if a page passes them, Cobalt-style) are honoured with the §6.4 caps |
| `audio/mp4` + `mp4a.40.2/5/29`, `opus`, `ac-3`, `ec-3`, `fLaC`, `mp3`/`mp4a.69/6B` | yes |
| `video/mp4` + `av01`, `vp09`, `dvh1/dvhe`; any `encv` | no |
| `video/webm`, `audio/webm` | **no** in stage 1; stage 2: `vp9`/`vp09.00.*` (≤ the measured cap) + `opus`/`vorbis` yes, `vp8` yes |
| `video/mp2t` | no (hls.js transmuxes TS to fMP4 before appending) |

Stage 1 requires the §6.7 MediaCapabilities factory (same table). Without the level cap, hls.js would see 1080p H.264 as playable and its bandwidth ABR on a LAN
would pick it; the drop-driven down-switch then oscillates. The cap is the owner's ≤720p rule in
codec-string form. (**Unverified:** whether every target ladder labels its levels honestly; the
`decodingInfo` smooth=false answer is the second line of defence, `droppedVideoFrames` the third.)

### 7.6 CMake / feature flags

| Change | Pulls in |
|---|---|
| `-DENABLE_MEDIA_SOURCE=ON` (USE `mse`, requires USE `video`) | `Modules/mediasource/*` + JS bindings (`JSMediaSource`, `JSSourceBuffer`, `JSManagedMediaSource`, …), `MediaSourcePrivate`, `SourceBufferPrivate`, `TrackBuffer`, `SampleMap`, `MediaSourceTypeSupportedCache`, the **mock MSE backend** (compiled, used only by tests), `ManagedMediaSource` (runtime-on by default on WPE). No new library. `cmakeconfig.h` changes → **full WebKit rebuild (~2 h at -j8)**, and again when toggled off |
| keep `ENABLE_MEDIA_SOURCE_IN_WORKERS=OFF` (default) | — (hls.js/Shaka in workers fall back to main-thread MSE) |
| keep `ENABLE_ENCRYPTED_MEDIA=OFF`, `ENABLE_WEB_AUDIO=OFF`, `ENABLE_WEB_CODECS=OFF` | — (§8) |
| `USE_FFMPEG` unchanged; `platform/FFmpeg.cmake` adds the MSE files under `ENABLE_MEDIA_SOURCE` | — |
| launcher: `--mse=on|off` (`enable-mediasource`), ManagedMediaSource off at first (feature API) | — |

The patch is `patches/webkit-mse/0032-…` applied only with USE `mse`, for the same reason 0030 is
apart: a default build keeps the same tree and no rebuild.

### 7.8 Memory per MSE player

With MSE the bytes live in WebCore's `TrackBuffer`s (compressed samples), the page's own JS
buffers (hls.js keeps fetched fragments) and our engine queues; the rules keep a page with several
players (hover previews, muted autoplay teasers) bounded:
- **SourceBuffer caps:** `platformMaximumBufferSize()` 40 MB per video SourceBuffer, 8 MB audio
  (`WPE_PHOENIX_MSE_MAX_MB`) for a player that has played; **8 MB / 2 MB for one that has not**
  (the same played/idle split as 0030's read-ahead; WebCore re-reads the size through
  `setMaximumBufferSize` → eviction on the next append). Compressed data only: 720p H.264 ≈
  0.3–0.5 MB/s, 1080p HEVC ≈ 0.6–1 MB/s → ~40–80 s of video at the full cap.
- **No copies of payload:** a `MediaSampleFFmpeg`'s AVPacket references the append's
  `SharedBuffer` (one `av_buffer_create` per append). Consequence: an append's buffer is freed only
  when every sample from it is evicted — segment granularity, acceptable; but if an append's
  buffer is > 2× the bytes of the samples it yields (junk/`free` boxes, partial appends), copy
  the samples out instead (logged).
- **Engine queues** hold ≤ 2 s per track (§7.4), not bytes-capped by 0030's 16 MiB alone.
- **Decoders** open at the first `play()` (as §6.9a): a never-played MSE player holds parsed
  samples only, no decoder and no rpivid claim. Pause keeps the decoder (resume must be instant).
- **Threads:** the player's threads at `WPE_PHOENIX_MEDIA_STACK_KB` (2 MiB); **one** parser
  `WorkQueue` shared by every SourceBuffer in the process (parsing is cheap; a WorkQueue per
  SourceBuffer would add a thread + stack each).
- Stat line additions: `mse_sb=<n> mse_kb=<TrackBuffer bytes> mse_samples=<n> played=`.

## 8. Web Audio, WebCodecs, EME

- **Web Audio:** not needed for playback by hls.js, dash.js, Shaka, video.js or the PeerTube player
  (they feature-detect `AudioContext` for extras: volume boost, visualisers). Some players create
  an `AudioContext` to "unlock" autoplay on iOS — harmless when absent. Keep OFF; it would need a
  WebCore audio destination over `/dev/audio0` + a mixer with the media player's writer (one
  writer system-wide today) — a separate item.
- **WebCodecs:** not used by these players for playback. OFF.
- **EME:** out of scope: no Widevine/FairPlay/PlayReady CDM exists for Phoenix (proprietary,
  licensed). ClearKey would be implementable (EME + `encv` parsing + AES-CTR/CBCS) but no target
  site uses it. Encrypted content is refused with a logged reason (§7.3), and `requestMediaKeySystemAccess`
  stays absent so players show their own "unsupported" message.

## 9. Host-side test set

### 9.1 Server: `tools/browser/media/serve-media.py` (new, BSD-3, stdlib only)

`serve.py` (bench) is `SimpleHTTPRequestHandler`-based: **no Range support** and no `.m3u8`/
`.m4s`/`.ts` MIME map. The media server reuses its shape plus:
- `Range: bytes=a-b` → 206 with `Content-Range` (progressive seek, `EXT-X-BYTERANGE`);
- MIME: `.m3u8` `application/vnd.apple.mpegurl`, `.m4s`/`.mp4` `video/mp4`, `.ts` `video/mp2t`,
  `.aac` `audio/aac`, `.key` `application/octet-stream`, `.mpd` `application/dash+xml`;
- `Access-Control-Allow-Origin: *` (hls.js pages may be served from elsewhere);
- **live simulation**: `GET /live/<ladder>/<variant>.m3u8` serves a sliding window over a VOD
  ladder's segments by wall clock (window 6 segments, `#EXT-X-MEDIA-SEQUENCE` advancing, no
  `#EXT-X-ENDLIST`), optionally with an `#EXT-X-DISCONTINUITY` every N segments (`?disc=N`);
- `GET /phx-log?…` → one stdout line (`MEDIA-PAGE …`), so page events also reach the host log;
- address/port as the bench: `10.42.0.1:8091` (`MEDIA_SERVE_ADDRESS`, `MEDIA_SERVE_PORT`), started
  by `tools/browser/media/serve-for-pi.sh` (same pattern as `bench/host/serve-for-pi.sh`).

### 9.2 Ladders: `tools/browser/media/gen-ladders.sh` (host ffmpeg; same encoders video_player's USE demo already requires: libx264, libx265, aac, libopus)

Source: `testsrc2` + `sine` (a frame counter on screen and a tone: A/V offset is visible and
audible), 60 s, 30 fps, keyframe every 2 s (`-g 60 -keyint_min 60 -sc_threshold 0`), 2 s segments.

| Ladder | Variants | Format | Purpose |
|---|---|---|---|
| `hevc-fmp4` | HEVC Main 8-bit 1080p30 (4.5 Mb/s), 720p30, 480p30 + H.264 720p30, 480p30 + AAC 128k audio group | `-f hls -hls_segment_type fmp4 -hls_playlist_type vod -master_pl_name master.m3u8 -var_stream_map …`, `-tag:v hvc1` | **the main case**: policy must choose HEVC 1080p8 → `hw=1` |
| `hevc-ts` | HEVC 1080p30 + H.264 720p in **MPEG-TS** segments, muxed AAC | `-hls_segment_type mpegts` | TS path + hevc parser |
| `hevc-main10` | HEVC Main10 1080p + H.264 720p | fmp4 | policy must choose H.264 720p (Main10 ranked below); `WPE_PHOENIX_HLS_VARIANT` forces Main10 to measure the CPU-conversion gap |
| `h264-only` | H.264 1080p30, 720p30, 360p30 (+ AAC) | fmp4 | the PeerTube-like case: policy must cap at 720p |
| `byterange` | H.264 720p as one file with `EXT-X-BYTERANGE` (`-hls_flags single_file`) | fmp4 | Range path (`offset/end_offset`) |
| `aes` | H.264 720p TS with AES-128 (`-hls_key_info_file`) | ts | the crypto wrapper |
| `audio-only` | AAC and Opus (fMP4) | fmp4 | no-video clock path |
| live (`serve-media.py /live/…`) | `hevc-fmp4` 1080p + `h264-only` 720p, sliding window; one with `?disc=10` | — | live + discontinuities |
| `mse-segments` (stage 1) | per representation: `init.mp4` + `seg-00001.m4s …` (HEVC 1080p/720p, H.264 720p/480p, AAC, Opus-in-mp4) via `-f dash -dash_segment_type mp4 -seg_duration 2 -use_template 1` | fmp4 | the hand-written MSE page |

Every ladder writes a `manifest.json` (variants, codecs strings as ffmpeg wrote them, segment
counts, sha256) that the gate scripts read; the ladders are data (regenerated when the script or
the host ffmpeg version changes, like `gen-clips.sh`). Approximate size: ~250 MB on the host's
export (not in the image).

### 9.3 Pages (in `webkit_wpe/files/checks/`, staged with USE `checks`; also served by the host server)

- **`b8-hls.html`** (stage 0): `<video controls>` with `src=<?src=…m3u8>`; logs (`B8HLS t=… …`)
  `canPlayType` for the 4 HLS MIME types and a codec matrix, `loadedmetadata` (size, duration,
  `Infinity` for live), `playing`, `timeupdate` every 5 s, `getVideoPlaybackQuality()` every 5 s,
  `seeking`/`seeked`, `ended`, `error` (code + message); actions by query: `seek=<t>`,
  `pause=<a>-<b>`, `stop=<t>`.
- **`b8-hlsjs.html`** (stage 1, also a stage-0 negative control): a vendored **hls.js** release
  (Apache-2.0 — check its LICENSE at vendoring time; a pinned version + sha256, served from the host,
  never staged in the image), the
  canonical `isSupported() ? hls.js : native` snippet, logs which branch ran, every
  `MANIFEST_PARSED` level (codecs, resolution), `LEVEL_SWITCHED`, `FRAG_LOADED` timing, errors.
  Stage 0 expectation: branch `native`; stage 1: branch `hls.js`, levels filtered (no 1080p
  H.264 unless forced), HEVC level chosen when `isTypeSupported(hvc1)` is yes.
- **`b8-mse.html`** (stage 1): hand-written MSE player (~250 lines, no library): fetch
  `init.mp4` + N segments of one representation per SourceBuffer, `appendBuffer` with
  `updateend` chaining; modes by query: `basic`, `seek` (seek to an unbuffered time → fetch the
  segment, append → `seeked`), `switch` (720p→1080p HEVC mid-stream: new init segment on the
  same SourceBuffer, then `changeType('video/mp4; codecs="avc1…"')` HEVC→H.264), `evict` (append
  60 s, `remove(0, 40)`, check `buffered`), `offset` (`timestampOffset` + sequence mode),
  `underrun` (stop appending for 5 s: `waiting` then resume), `eos` (`endOfStream()` → `ended`).
  Logs `B8MSE t=… …` with `buffered` ranges after every `updateend`; at start it logs the
  `isTypeSupported` matrix and the `navigator.mediaCapabilities.decodingInfo` matrix (§6.7).

### 9.4 Host harnesses (no Pi; the project's host-harness pattern)

`tools/browser/media/hosttest/` (built against the host's own FFmpeg 6.1 headers/libs or the
port's tree compiled for x86_64):
1. **HLS demuxer + custom io_open + the 0002 hunk:** a C program opens each ladder's master
   (filtered by a C port of the §6.4 policy, the same source file the WebCore code includes) with a
   directory-backed `io_open` that also logs every URL/range, reads all packets, and checks:
   only the chosen variant's playlists/segments were opened; packet pts monotonic per stream;
   total duration; the AES and byte-range ladders decode identically to their clear/whole-file
   twins (frame md5s).
2. **fMP4 parser** (stage 1): samples vs FFmpeg's mov demuxer on the same files (pts, dts,
   duration, flags, size, payload md5), for every split point of the first 3 segments and 10 000
   random split sequences; ASan + UBSan; a fuzz corpus from the ladders.
3. **Policy table test:** synthetic masters (CODECS/RESOLUTION/FRAME-RATE/VIDEO-RANGE
   combinations, missing CODECS, Main10-only, Dolby-Vision-only) → expected choice.

## 10. Pi verification per stage (B8 §6 pattern; one cycle at a time)

Prerequisites per stage: the image built with the stage's USE flags (`webkit_wpe:
use: [rootfs, checks, video]` (+ `mse` for stage 1)), the host media server up
(`tools/browser/media/serve-for-pi.sh`), build proof by `strings /usr/bin/wpe-browser` for the
stage's format strings (`hls choose i=%d`, `mse append`), and B8's existing `b8.sh` arms still
passing (the refactor guard). Script `b8-stream.sh` (staged with USE `checks`) runs arms, each a
fresh `wpe-browser --autoplay=allow --size=1000x620 <url>` killed after its time, under
`./scripts/test-cycle-psh-interact.sh --label <l> --idle-secs 60 --max-cmd-secs 540
--hdmi-dense-on 'B8S arm=' -- "/bin/bash /usr/share/wpe-browser/b8-stream.sh <arms>"` with Bash
`timeout` 600000. Grading: `grep -a -E '^(B8S |WPEB-MEDIA |B8HLS|B8MSE|WPEB )|Exception #'`.

### 10.1 Stage 0 gate (native HLS)

| # | Arm / line | PASS |
|---|---|---|
| 1 | page: `canplaytype type=application/vnd.apple.mpegurl answer=maybe`, `…codecs="hvc1.1.6.L120.90,mp4a.40.2" answer=probably`, `…av01… answer=` (empty) | HLS answers as §6.6 |
| 2 | `hevc-fmp4` VOD: `hls master … variants=5`, five `hls variant` lines, **`hls choose i=<1080p HEVC> rule=hevc8`**, `decoder video=hevc_rpivid`, `rpivid: hardware HEVC decode 1920x1080 8-bit`, `stat … fps=29–31 hw=1`, dropped ≤ 2 %, painted ≥ 95 % (dma-buf) | **HEVC from an HLS ladder on the block** |
| 3 | `hls open` lines name only the chosen media playlist, its init section and its segments (+ the audio group) | the policy, not FFmpeg, selected |
| 4 | `hevc-ts`: `rule=hevc8`, `hw=1`, 30 fps | TS segments |
| 5 | `h264-only`: `rule=h264` at **1280x720**, fps 29–31 | the 720p cap |
| 6 | `hevc-main10`: `rule=h264` 720p (default); forced Main10 arm records fps + `convert <10-bit format> -> yuv420p (CPU)` | the policy + the measured gap |
| 7 | seek arm (`hevc-fmp4`, `seek=40`): `seek target=40.000`, `seek done pts=` 40 ± 2, `timeupdate` advancing | VOD seek |
| 8 | `byterange` and `aes` arms play (`first-frame`, fps ≈ 30) | Range + AES-128 |
| 9 | live arm (host `/live/…`, 90 s): `hls live=1`, `loadedmetadata duration=Infinity`, `hls reload` lines, playback continuous ≥ 80 s (fps ≈ 30, no `stall` gaps > 2 s); `?disc=10` arm: `discontinuity` lines, A/V `av_ms` within ±80 after each | live + discontinuity |
| 10 | `b8-hlsjs.html` (stage 0): page logs branch `native` | the fallback sites take |
| 11 | real-world, attended or opportunistic (network via host NAT): Apple's HEVC example master (if `CODECS` shows `hvc1`), one PeerTube video page, Dailymotion/Vimeo embed: record what each page did (branch, `hls choose`, or "no MSE" message) | **recorded, not graded** (sites change) |
| 12 | **memory arm:** a page with 7 `<video preload=metadata controls src=<hevc-fmp4 master>>`, none played, 60 s: `players=7`, every player `hls_streams` 0 after metadata, **no `decoder` line and no `rpivid` line**, WebProcess footprint < +60 MB vs the empty page; then `play()` on one: it alone opens `hevc_rpivid` (`hw=1`) | idle players stay small and leave the block free |
| 13 | 0 `Exception #`, every arm `B8S arm=<a> end`, B8 progressive arms unchanged (incl. 0030's preload/idle checks) | clean |

### 10.2 Stage 1 gate (MSE)

| # | Arm / line | PASS |
|---|---|---|
| 1 | page: `MediaSource` present; `isTypeSupported` matrix as §7.5 (hvc1 yes, avc1.64001f yes, avc1.640028 **no**, av01 no, webm no); `decodingInfo({type:'media-source'})` probes: `capabilities … codec=hvc1… 1920x1080@30 supported=1 smooth=1 efficient=1`, `… codec=avc1.640028 1920x1080@30 supported=1 smooth=0 efficient=0` | steering answers |
| 2 | `b8-mse.html?mode=basic&rep=hevc1080`: `mse addsourcebuffer type=video/mp4;codecs="hvc1…"`, `mse init tracks=1 video=hevc 1920x1080`, `mse append bytes=… samples=…` per segment, `decoder video=hevc_rpivid`, `stat … hw=1 fps=29–31`; page `buffered` grows contiguously | MSE + rpivid |
| 3 | `mode=seek`: `seeking` → page appends the target segment → `seek done pts=` ± 2 → `seeked` | waitForTarget/reenqueue |
| 4 | `mode=switch`: `mse init … generation=2 1280x720 → 1920x1080`, `decoder reopen hevc_rpivid`, no `error`; `changeType` to H.264: `decoder video=h264` | codec/quality switching |
| 5 | `mode=evict`: `buffered` after `remove` starts at 40; 120 s append with a 40 MB cap: eviction lines, footprint flat | eviction |
| 6 | `mode=underrun`: `waiting` fired, `readyState` ≤ 2 during the gap, clock holds (`stat clock` constant), resumes without A/V offset > 80 ms | stall handling |
| 7 | `mode=eos`: `ended` at the last sample's end | EOS |
| 8 | `b8-hlsjs.html` on `hevc-fmp4`: branch `hls.js`, `MANIFEST_PARSED` lists HEVC levels, the level playing is HEVC (`hw=1`); on `h264-only`: 1080p absent from the levels, 720p playing | hls.js steered |
| 9 | `--mse=off` arm on the same page: branch `native`, stage-0 behaviour | both paths live |
| 10 | real-world recorded as 10.1 #11 (PeerTube now via hls.js; Vimeo/Dailymotion players) | recorded |
| 11 | memory: a page with 4 MSE players each appended 30 s, none played: `mse_kb` ≤ 4 × 10 MB, no decoder lines; then 5 min of playback on one: footprint < +150 MB vs start, `mse_kb` flat once the 40 MB cap is reached | bounded |
| 12 | 0 `Exception #` | clean |

## 11. Risks

1. **Owner's HEVC goal vs the real web:** few open platforms serve HEVC (§4); the HEVC payoff is
   mostly our own streams and Apple-style HLS. Small platforms will run H.264 ≤720p on the CPU,
   sharing 4 cores with JS and the compositor (B8 risk 2 unchanged).
2. **Main10 is common in HEVC ladders** (HDR tiers, some SDR): until a 16-bit upload path exists,
   such content converts on the CPU at 1080p (unmeasured; likely < 30 fps). The policy avoids it
   where an alternative exists.
3. **hls.c with custom I/O** is unusual territory: http-specific branches (`is_http`,
   keep-alive, `http_multiple`) may assume FFmpeg's http protocol options exist on the AVIO
   (`av_opt_get(*pb, "cookies")` is skipped under custom I/O — read in `open_url`); the host
   harness exercises them before the Pi.
4. **Live HLS timing:** playlist reload waits happen inside `av_read_frame` on the demux thread;
   the demux queue thresholds and the interrupt callback must keep teardown prompt (P17-style
   wait bugs have bitten us: patch 0021).
5. **TS discontinuities and timestamp wrap** (33-bit PTS) on long live streams: handled by
   re-anchoring; untested on real ad-inserted streams.
6. **MSE coverage:** WebCore's generic MSE code is exercised upstream only with GStreamer/Cocoa
   backends on Linux/Apple; our backend's threading (dispatcher = main thread, a parser
   WorkQueue, decode threads) must respect every `WTF_GUARDED_BY_CAPABILITY` in
   `SourceBufferPrivate` — release builds do not check them; a debug-ish run of the mock-style
   unit flows on the host is not available (WebKit is not built for the host).
7. **hls.js ABR on a LAN** picks the highest eligible level; if a site mislabels levels the 720p
   cap leaks → drops → down-switch oscillation. Mitigation: level cap + `decodingInfo` + accurate
   `getVideoPlaybackQuality()`.
8. **Memory:** MSE buffers + hls.js's own buffers (it keeps fetched fragments in JS) in a
   WebProcess already seen at 1.2 GB on a page with seven paused progressive players (NYT, fixed
   in 0030 `9c0b6dc`). Budgets in §6.9a/§7.8 (idle vs played, lazy decoders, shared parser
   queue); memory rows in 10.1/10.2. hls.js's JS-side buffer is outside our control
   (`maxBufferLength`/`maxMaxBufferLength` are the page's).
9. **One audio writer, one rpivid owner** system-wide (B8 risk 6): a second video element or tab
   plays muted / on the CPU. Pages with a muted preview `<video>` plus the main player (common)
   can take the block first — stage 1 should prefer the larger/visible element (**open**).
10. **Rebuild cost:** stage 1's `ENABLE_MEDIA_SOURCE` toggle = ~2 h full builds; keep USE `mse` off
    in the default image until its gate passes.
11. **Site breakage when MSE appears:** sites that worked through native HLS in stage 0 switch
    to hls.js in stage 1 and may regress; `--mse=off` is the escape hatch and the A/B tool.

## 12. Effort (agent-days, to a Pi gate pass)

| Part | Days |
|---|---|
| Stage 0: components + `hls.c` hunk + host harness 1 | 1 |
| Stage 0: per-URL `FFmpegMediaStream`, `io_open`/`io_close2`, interrupt, byte ranges, AES wrapper | 1 |
| Stage 0: playlist parser + policy + synthesized master (+ policy table test) | 1 |
| Stage 0: live/VOD/discontinuity handling, type answers, MediaCapabilities factory | 0.5–1 |
| Stage 0: memory budget (shared per-player window, preload levels, lazy decoders) + memory arm | 0.5 |
| Stage 0: ladders, media server, pages, `b8-stream.sh`, 1–2 Pi iterations | 1–2 |
| **Stage 0 total** | **4.5–6.5** |
| Stage 1: refactor 0030 into `FFmpegPlaybackEngine` (+ B8 regression check) | 1–1.5 |
| Stage 1: `MediaSampleFFmpeg`, `MediaSourcePrivateFFmpeg`, `SourceBufferPrivateFFmpeg`, enums/IPC/registration | 2 |
| Stage 1: fMP4 parser + host harness 2 | 2 |
| Stage 1: MSE player (seek, readyState gating, EOS, codec switch, quality metrics, limits) | 2 |
| Stage 1: CMake + one full build (~2 h) + MSE/hls.js pages + 2–3 Pi iterations | 2–3 |
| **Stage 1 total** | **9–12** |
| Stage 2: WebM parser (EBML Tracks/Cluster/SimpleBlock/BlockGroup, lacing, missing durations) + VP9/Opus answers after a VP9 480p/720p measurement | 3–4 |
| Stage 2: native-HLS down-switch, MSE eviction/quality polish, 10-bit upload path study | 2–3 |

## 13. Open items to verify (before or during stage 0)

1. hls.js `isSupported()` / `preferManagedMediaSource` behaviour and its HEVC handling in the
   version current sites ship (WebFetch the hls.js API docs/changelog; record the version).
2. video.js VHS `overrideNative` on our Safari-claiming UA (`navigator.userAgent` on the Pi).
3. The WPE 2.54 feature identifier for ManagedMediaSource in `webkit_settings_get_all_features()`.
4. Which `HTMLMediaElement` getter reads a non-MSE live stream's seekable window
   (`seekable()` vs `maxTimeSeekable()`), for §6.5.
5. Whether FFmpeg 6.1's mov demuxer really ignores a second `moov` in a fragmented stream (only
   matters for the "why not mov" argument; the decision stands on the other reasons).
6. Real platforms' HEVC (§4): fetch masters and read `CODECS`; record in this file.
7. VP9 decode rate at 480p/720p on the Pi (ffplay on generated clips) before stage 2 answers.
8. hls.js's `useMediaCapabilities` default and whether it filters levels on `supported` only or
   also on `smooth` (decides whether 1080p H.264 needs `supported=false` rather than `smooth=false`).

## 14. Log

- 2026-10-07: rebased on 0030 master (`9c0b6dc`: preload/idle read-ahead/2 MiB stacks); memory
  rules for HLS (§6.9a) and MSE (§7.8), memory gate rows.
- 2026-10-07: first draft (YouTube-oriented MSE); restructured the same day on the owner's
  direction: native HLS first (stage 0), MSE steered to HEVC/rpivid and H.264 ≤720p (stage 1),
  YouTube dropped as a target.
