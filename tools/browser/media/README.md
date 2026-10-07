# Streaming-video test set (native HLS, MSE)

The host-side test set of [`docs/browser/MSE-DESIGN.md`](../../../docs/browser/MSE-DESIGN.md) §9:
media ladders, a media server, test pages, host harnesses and the Pi-side gate runner for
**stage 0** (native HLS in `MediaPlayerPrivateFFmpeg`) and **stage 1** (MSE). Everything here runs
on the build host; only `pi/b8-stream.sh` runs on the Pi. Media is generated, never committed: it
lives in the git-ignored media root `artifacts/media/` (override: `MEDIA_ROOT=` or `--root`).

| File | What |
|---|---|
| `gen-ladders.sh` | encodes the renditions once (testsrc2 + noise, burned-in `<codec> <size>` label, frame number, a white square + beep every second), packages every ladder as a stream copy, runs `ladder-manifest.py` |
| `ladder-manifest.py` | rewrites each master with RFC 6381 `CODECS` + `FRAME-RATE` (ffmpeg's hls muxer writes no `CODECS` for HEVC and never `FRAME-RATE`), writes `manifest.json` (variants, segments, fMP4 fragment timing, `expect`, sha256) |
| `mp4box.py` | the ISO-BMFF bits: codec strings from `hvcC`/`avcC`/`esds`, fragment `tfdt`/durations, `tfdt` re-stamping |
| `serve-media.py` | the server: Range (206/416), HLS/DASH MIME types, CORS + preflight, request log, live simulation, `/phx-log` |
| `serve-for-pi.sh` | `start|stop|restart|status` of the server on `10.42.0.1:8091` (the bench's is `:8090`) |
| `stage.sh` | hls.js (pinned, sha256-checked) + the pages into the media root, and `b8-stream.sh` + pages onto the NFS export |
| `pages/` | `b8-hls.html` (native HLS), `b8-hlsjs.html` (hls.js), `b8-mse.html` (hand-written MSE player), `b8-hls-memory.html` (7 idle players), `index.html` (menu), `media-common.js` |
| `check-media.py` | host harness: server protocol, every ladder URI, AES/byte ranges, fMP4 continuity, live window, and decode through ffmpeg's own HLS demuxer over HTTP; `requests` mode = the gate's host-side witness |
| `run-host-pages.sh` | the pages in Playwright's headless Chromium, Firefox and WebKit (WPE MiniBrowser) |
| `pi/b8-stream.sh` | the Pi gate runner (XFCE session, one `wpe-browser` per arm, `B8S` lines) |
| `hls.js.sha256`, `LICENSES.txt` | the hls.js pin (1.7.3, Apache-2.0) and the licences |

## Host: make, check, serve

```bash
tools/browser/media/gen-ladders.sh            # ~6 min on the build host, ~585 MB (renditions 175 MB); idempotent
tools/browser/media/stage.sh                  # hls.js + pages -> artifacts/media; b8-stream.sh + pages -> NFS export
tools/browser/media/check-media.py            # ~3 min; CHECK ... PASS|FAIL, CHECK-SUMMARY; exit 1 on a FAIL
tools/browser/media/run-host-pages.sh         # ~15 min per browser (--browsers webkit, --cases "mse-seek ...")
tools/browser/media/serve-for-pi.sh start     # http://10.42.0.1:8091/, log artifacts/media/serve.log
```

`gen-ladders.sh --secs 6 --root /tmp/media-smoke` makes a 6-second set in ~30 s for script work.
Needs the host ffmpeg with libx265, libx264, aac, libopus (Ubuntu's ffmpeg 8.0.1 has them) and
`fonts-dejavu-core`; Playwright from `tools/browser/bench/host/setup-host-tools.sh`.

### What the server serves

| URL | |
|---|---|
| `/pages/b8-hls.html?src=/ladders/<ladder>/master.m3u8` | native HLS page (see each page's header for its query) |
| `/pages/b8-hlsjs.html?src=…` | hls.js page (stage 0: must take branch `native`) |
| `/pages/b8-mse.html?mode=basic|seek|switch|evict|offset|underrun|eos&rep=…&audio=…` | MSE page |
| `/pages/b8-hls-memory.html?n=7&idle=60&play=30` | memory arm |
| `/ladders/<ladder>/master.m3u8`, `/ladders/<ladder>/manifest.json` | VOD ladders |
| `/live/<ladder>/master.m3u8[?disc=N]` | the ladder as a LIVE stream (below) |
| `/mse/manifest.json`, `/mse/<rep>/init.mp4`, `/mse/<rep>/seg-NNNNN.m4s` | MSE segment sets |
| `/phx-log?l=<line>` or `POST /phx-log` | a page line into the host log (`MEDIA-PAGE …`) |

Ladders (60 s, 30 fps, 2 s closed GOPs, 2 s segments; the variant order is deliberate, so a player
that does not choose shows it; `manifest.json` `expect` = what the §6.4 policy must pick):

| Ladder | Master order | Format | Policy must choose |
|---|---|---|---|
| `hevc-fmp4` | h264-720, hevc-480, **hevc-1080**, h264-480, hevc-720 + AAC group | fMP4 | i=2 `rule=hevc8` 1920x1080 |
| `hevc-ts` | h264-720, **hevc-1080**, AAC muxed | MPEG-TS | i=1 `rule=hevc8` |
| `hevc-main10` | hevc10-1080 (`hvc1.2.4.L120.90`), **h264-720** + AAC group | fMP4 | i=1 `rule=h264`; forced Main10 = i=0 |
| `h264-only` | h264-1080 (`avc1.640028`), h264-360, **h264-720** (`avc1.64001f`) + AAC group | fMP4 | i=2 `rule=h264` 1280x720 |
| `byterange` | h264-720 + AAC in one file, `EXT-X-BYTERANGE` | fMP4 | i=0 |
| `aes` | h264-720 + AAC, AES-128 (key `aes.key`, explicit IV) | MPEG-TS | i=0 |
| `audio-only` | aac, opus | fMP4 | informational (no video) |
| `mse/` | hevc-1080, hevc-720, h264-720, h264-480, aac, opus (one track each, DASH muxer) | fMP4 | — |

Codec strings as written into the masters: `hvc1.1.6.L120.90` (HEVC Main 1080p), `hvc1.1.6.L93.90`
(720p), `hvc1.1.6.L90.90` (480p), `avc1.640028` (H.264 High 4.0, 1080p), `avc1.64001f` (High 3.1,
720p), `avc1.4d401e` (Main 3.0, 480p/360p), `mp4a.40.2`, `opus`.

**Live simulation** (`/live/<ladder>/…`): a sliding window of 6 segments by wall clock (the clock
starts a full window in the past when the server starts), `#EXT-X-MEDIA-SEQUENCE` advancing,
`#EXT-X-PROGRAM-DATE-TIME`, no `#EXT-X-ENDLIST`; segment/init/key URIs absolute under `/ladders/`.
The 60 s content loops: **fMP4 stays continuous** (the server advances every fragment's `tfdt` by
the loop count × the track's length, `?shift=<track>:<ticks>` on the segment URI); **MPEG-TS** cannot
be re-stamped, so its loop point carries `#EXT-X-DISCONTINUITY` (a real reset). `?disc=N`: every N
segments the stream restarts at the first segment with its original timestamps behind
`#EXT-X-DISCONTINUITY` (a real reset every 2N s, like a spliced-in programme). Audio segments are
2.005 s (94 AAC frames), so audio and video playlists have their own sequence numbers; timestamps
keep them aligned (≤ 21 ms drift per 60 s loop, one AAC frame). Byte-range ladders are not offered live.

## Pi gate (MSE-DESIGN §10)

Prerequisites: an image whose `wpe-browser` carries the stage's code (stage 0: `strings
/usr/bin/wpe-browser` shows `hls choose i=`; stage 1 also `mse append`), the media made and staged
(`gen-ladders.sh`, `stage.sh`: `/usr/share/browser-media/b8-stream.sh` on the export) and the
server up (`serve-for-pi.sh start`, then `curl http://10.42.0.1:8091/phx-ping`). One Pi cycle at a
time; each preset fits one cycle (session hold ≤ 450 s):

```bash
./scripts/test-cycle-psh-interact.sh --label b8s-stage0a --idle-secs 60 --max-cmd-secs 540 \
    --hdmi-dense-on 'B8S arm=' -- "/bin/bash /usr/share/browser-media/b8-stream.sh stage0a"
```
with Bash `timeout` 600000. Presets (or a comma list of arm names):

| Preset | Arms | §10 rows |
|---|---|---|
| `stage0a` | probe, hevc-fmp4, hevc-ts, h264-only | 10.1: 1, 2, 3, 4, 5 |
| `stage0b` | main10, main10-forced, seek, byterange, aes | 10.1: 6, 7, 8 |
| `stage0c` | live, live-disc, hlsjs | 10.1: 9, 10 |
| `stage0d` | memory, audio-only | 10.1: 12 |
| `stage1a` | mse-basic, mse-seek, mse-switch, mse-underrun | 10.2: 2, 3, 4, 6 |
| `stage1b` | mse-evict, mse-eos, mse-offset | 10.2: 5, 7 |
| `stage1c` | probe, hlsjs-hevc, hlsjs-h264, mse-off | 10.2: 1, 8, 9 |

`key=value` after the arms: `base=http://…:port`, `hold=<s>`, `args=--cpu-rendering` (extra browser
words, comma-separated), `mseoff=<words that turn MSE off>` (default `--mse=off`, the launcher flag
the design plans). `main10-forced` sets `WPE_PHOENIX_HLS_VARIANT=0` (the Main10 variant's index).

**Grading.** The UART log:
`grep -a -E '^(B8S |WPEB-MEDIA |B8HLS|B8MSE|WPEB )|Exception #' <log>`; every arm ends with
`B8S arm=<a> end rc=…` and `B8S arm=<a> page=<ok|stopped|error|none> <page summary>` (summary:
`startup_ms stalls stall_ms max_stall_ms sizes total dropped rvfc_frames …`). The page lines also
reach the host log (`MEDIA-PAGE …` in `artifacts/media/serve.log`) when the Pi console is quiet.
**The host log is the witness for rows 3 and 12** — which files the player fetched, per arm:

```bash
tools/browser/media/check-media.py requests artifacts/media/serve.log --run hevc-fmp4
# REQUESTS run=hevc-fmp4 phase=start dir=/ladders/hevc-fmp4/hevc-1080 playlist=1 init=1 segment=30 bytes=…
# REQUESTS run=hevc-fmp4 phase=start dir=/ladders/hevc-fmp4/audio playlist=1 init=1 segment=31 bytes=…
```
Row 3 passes when only `master.m3u8`, the chosen variant's directory and the audio group appear;
row 12 when the memory arm shows no `segment=` in `phase=start` (before its `play-request`). On HDMI the
picture names the variant that plays (`HEVC 1080p`, `H.264 720p`, … burned in), and the white
square + beep every second show A/V offset.

## Host results (2026-10-07, build host, ffmpeg 8.0.1, Playwright 1.63)

`check-media.py`: **134/134 PASS**. Every variant of every ladder, read over HTTP by ffmpeg's own
hls demuxer (fMP4 with `EXT-X-MAP`, MPEG-TS, `EXT-X-BYTERANGE`, AES-128 through its `crypto`
protocol), decodes to frame md5s **identical** to its source rendition (1800 frames each), and so do
the MSE segment sets; live reads 10 s cleanly, and `?disc=3` resets the picture timestamps at
exactly every 6 s.

`run-host-pages.sh` (Playwright headless; muted; `stop=` shortens most cases):

| Case | Chromium 153 | Firefox 155 | WebKit (WPE MiniBrowser, GStreamer) |
|---|---|---|---|
| native HLS `b8-hls` on hevc-fmp4 | plays: its own ABR, h264-480 → h264-720 (no HEVC) | `error` (no native HLS) | `error` (no native HLS) |
| hls.js hevc-fmp4 | 2 levels kept (HEVC filtered by isTypeSupported), 720p | 5 levels, **switches to HEVC 1080p** | 5 levels, **switches to HEVC 1080p** |
| hls.js h264-only | **switches to 1080p** (no cap: why §7.5's level cap exists) | 1080p | 1080p |
| hls.js hevc-ts / main10 / byterange / aes / audio-only | — / — / ok / ok / ok | all ok | all ok (TS cases stalled once under 3-browser load; clean on rerun) |
| hls.js live / live `?disc=5` | ok (30 s) / — | ok / ok | ok / ok |
| MSE basic (hevc-1080; Chromium h264-720) | ok | ok | ok |
| MSE seek → 40 s / evict / offset / underrun / eos | pass / pass / pass / pass / — | pass ×5 | pass ×5 |
| MSE switch | — | 720p → 1080p → `changeType` H.264 720p | 720p → 1080p; **no `changeType`** (logged `changeType-missing`) |
| MSE Opus audio-only | ok | ok | ok |

Page bugs these runs found and fixed: every SourceBuffer must exist before the first init segment
is appended (Chromium and WebKit refuse a later `addSourceBuffer`); a codec change without
`changeType()` is an error by the spec; the waiting before the first frame is start-up, not a stall.

## Pi results

**stage0a, build 54 (2026-10-07, `wpe-browser` defaults: GPU raster + dma-buf, 1000x620 window).**

| Arm | Result | Player |
|---|---|---|
| probe | type answers as designed (HLS MIME types `maybe`, HEVC 8-bit / Main10 codecs `probably`); ends `page=timeout` by design (no play) | fetched 1 segment of each rendition (idle read-ahead) |
| hevc-fmp4 | `page=ok` played 60.0 s to `ended`, startup 1758 ms, 0 stalls | variant `hevc8` 1920x1080, `hw=1` (rpivid), 30.0 fps presented, 5 dropped |
| hevc-ts | `page=ok` to `ended`, startup 1406 ms, 0 stalls | `hw=1`, TS segments |
| h264-only | `page=ok` to `ended`, startup 2106 ms, 0 stalls | 1280x720: the 720p cap held |

Row 3 (host log): each arm fetched only `master.m3u8`, its chosen variant and the audio group.
Open: the compositor painted ~19 of the 30 presented frames/s (`painted=` in the stat line), and the
SAND->planar conversion took 11.5 ms per 1080p picture (rpivid-stat `sand=`); the latter is cut in
build 55 (cached picture buffers).

## Notes for the stage-0/1 implementer

- **Open a filtered master, not the whole one.** Host ffmpeg 8.0.1's hls demuxer decodes every
  variant here bit-exactly, but opening a *multi-variant* master and discarding variants logs
  `Invalid NAL unit size` / `missing picture in access unit` from the discarded streams' parsers;
  a one-variant master + its audio group (the shape §6.4 synthesizes) is clean. Same family of
  cost as §3's "opens every playlist".
- ffmpeg's hls muxer omits `CODECS` for HEVC: real-world ladders made with stock ffmpeg will
  arrive without it, so §6.4's "no CODECS: rank by RESOLUTION, assume H.264" path matters.
- The design's §9.4 C harnesses (hls.c + custom `io_open` + the 0002 hunk; the fMP4 parser against
  the mov demuxer; the policy table) need the stage-0/1 sources and are not part of this set; their
  inputs are here (`ladders/*/manifest.json` has every URI, range, key and fragment timing;
  `mse/manifest.json` every segment).
- Deviations from the design text, on purpose: the pages live in this directory and are served
  from the host (not `webkit_wpe/files/checks/`), so no image rebuild is needed to change them;
  the gate runner is `pi/b8-stream.sh` staged on the NFS export (not a port file); page lines use
  the design's tags (`B8HLS`, `B8HLSJS`, `B8MSE`, `B8HLSMEM`).
