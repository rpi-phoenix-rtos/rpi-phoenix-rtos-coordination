# Which public video sites can serve HEVC to our browser

**Question (owner, 2026-10-07, B8):** for the demo we want to upload *our* video to a public
video-sharing site, then open it in our WPE WebKit browser on the Pi 4 and watch it in high quality.
The Pi decodes **HEVC in hardware** (`hevc_rpivid`, 1080p30 proven in `<video>`). H.264 decodes
only in software, up to about 720p30. So the site has to hand our browser an **HEVC** rendition.
YouTube does not offer one and is out of scope.

**Researched 2026-10-07** with curl and ffprobe from the build host, the sites' own player
JavaScript, the PeerTube v8.3.1 source, and vendor documentation. Each claim below is marked
**[V]** (verified by a command run during this research, with the command or URL) or **[I]**
(inferred from documentation or source, not exercised end to end). Three research sub-passes ran in
parallel (PeerTube; consumer sites; video APIs and test streams). Their [V] results that were not
re-run for this document say "research pass". No accounts were created and nothing was uploaded. Nothing here
has run on the Pi yet: "plays" always means "should play, given what the site serves".

## What our browser offers a site today

| What a player may test | Our answer (build 50) | Planned |
|---|---|---|
| `canPlayType('video/mp4; codecs="hvc1.1.6.L93.B0"')` | `probably` | — |
| `canPlayType('application/vnd.apple.mpegurl')` (native HLS) | `""` (no) | stage 0: `maybe`, `<video src=….m3u8>` through FFmpeg's HLS demuxer |
| `window.MediaSource` / `ManagedMediaSource` (MSE) | undefined | stage 1 (MSE design) |
| EME / any DRM | none | none |
| `matchMedia('(dynamic-range: high)')` | false | — |
| `navigator.vendor` | `Apple Computer, Inc.` (WebKit's default, `WEBCORE_NAVIGATOR_VENDOR`) | — |
| UA | `… (X11; Phoenix-RTOS aarch64a72) AppleWebKit/605.1.15 … Version/60.5 Safari/605.1.15` | per-site override possible |

`navigator.vendor` matters more than expected. hls.js- and Shaka-based players (Bunny, Mux,
Cloudflare, PeerTube) treat an Apple vendor plus a truthy HLS `canPlayType` as Safari and hand the
`.m3u8` straight to `<video src>`. Once stage 0 answers `maybe`, those players use our native HLS
with no further work.

What the hardware path takes ([M10-hevc-hwaccel](../gpu-new-lane/M10-hevc-hwaccel.md)):
- **The decoder:** 8- and 10-bit 4:2:0, CTB 64, one slice segment per picture, no tiles, WPP
  allowed. Anything else falls back to CPU decode.
- **The WebKit frame path** ([B8-video](B8-video.md) "Frame path"): 8-bit 4:2:0 goes to the GPU as
  planes. 10-bit is first converted to 8-bit `yuv420p` by libswscale on the CPU. That costs CPU at
  1080p, and it is a bit-depth squash, **not a tone map**. HDR content (PQ or HLG) would look flat
  and grey.

So the ideal file for the demo is **HEVC Main (8-bit) 4:2:0, SDR BT.709, ≤ 1080p30, CTB 64, one
slice per picture, no tiles**. x265's defaults produce exactly that. Each platform below is judged
on whether it delivers such a stream to us unchanged and unencrypted, over something we can play
without MSE: progressive MP4 today, native HLS after stage 0.

How to check a stream's tool set against the hardware path ([V], used on every stream below):

```sh
ffmpeg -loglevel trace -i <file-or-url> -map 0:v:0 -frames:v 3 -c copy -bsf:v trace_headers -f null - 2>&1 \
  | grep -E 'general_profile_idc|bit_depth_luma_minus8|log2_(min|diff_max_min)_luma_coding_block|tiles_enabled_flag|first_slice_segment_in_pic_flag'
```

- CTB size is 2^(3 + `log2_min…` + `log2_diff…`), so `0` and `3` mean 64.
- Every `first_slice_segment_in_pic_flag = 0` line is an extra slice.
- For HLS, concatenate the `EXT-X-MAP` init segment and one media segment into a file first.

## Recommendation

### Ranking

Of the sites checked, three serve an uploaded HEVC file **unchanged**, and none of them transcodes
*to* HEVC. All three deliver it as a progressive MP4, which today's build already plays. No
mainstream site produces an 8-bit SDR HEVC rendition: Vimeo makes HEVC only from HDR uploads,
Bilibili only behind MSE, Bunny only on paid Premium encoding.

| # | Platform | HEVC to us? | Needs from our browser | Verdict |
|---|---|---|---|---|
| 1 | **Internet Archive** (archive.org) | **Yes: the `.mp4` original is a player source**, labelled "1080p HD" next to a 480p H.264 derivative [V] | nothing new: progressive MP4 via JW Player (`canPlayType('video/mp4')` only) | **Best demo host.** Free account, reputable, CC licence of our choice, stable direct URL. The viewer picks "1080p HD" once |
| 2 | **PeerTube with transcoding off** (self-hosted, or an instance configured that way) | **Yes, byte for byte**; the player picks web-video mode by itself [V on a live instance] | nothing new | Best player behaviour (no quality switch needed), but we run the site, or depend on an approval-gated small instance |
| 3 | **Odysee** (LBRY) | **Yes, unchanged** when uploaded with *Transcode: No* [V by the research pass; re-check rate-limited, see below] | nothing new (`video.src = mp4`); native HLS only if an HLS rendition is ever made | Works, but the CDN rate-limits and needs a Referer, publishing needs a crypto (LBC) deposit, and the site's reputation is mixed |
| 4 | **Bunny Stream** ("Keep original files", or Premium x265) | Original: yes [I]. Premium HLS ladder: not documented for third-party players | original: nothing new. HLS: stage 0 | A paid developer CDN (14-day trial), not a sharing site |
| 5 | **Vimeo** | **Only for HDR uploads** (Main 10 PQ). SDR uploads become AVC + AV1 [V on 56 videos] | stage 0, a UA claiming iOS or macOS 10.13, `dynamic-range: high`, real tone mapping; Vimeo-owned videos are DRM | Not for this demo |
| 6 | **Bilibili** | HEVC (`hvc1.1.6.L120.90`) only in DASH, i.e. MSE; without MSE it serves a 720p H.264 MP4 [V] | MSE; mainland phone-verified account to upload | Not before MSE |
| — | Dailymotion, Rumble, Streamable | **No**: H.264 only (a Dailymotion "4k HDR" upload too) [V] | — | Not usable |
| — | Cloudflare Stream, Mux, Wistia, api.video | **No**: H.264 delivery [V on demo media] | — | Not usable for HEVC |
| — | YouTube | No HEVC | — | Out of scope |

### The demo, concretely

**1. The file (same for every host).** Encode with x265: SDR, 1080p30, AAC-LC audio, `hvc1` tag,
`moov` up front:

```sh
ffmpeg -i master.mov -vf scale=1920:1080,format=yuv420p -r 30 \
  -c:v libx265 -preset slow -crf 22 -x265-params "keyint=60:min-keyint=30" \
  -tag:v hvc1 -c:a aac -b:a 160k -ac 2 -movflags +faststart demo-hevc-1080p30.mp4
```

- x265's defaults (CTB 64, one slice, WPP, no tiles) are the hardware path's tool set. Do not add
  `--slices` or tiles.
  - Other encoders differ. The HEVC original on Internet Archive measured below is **CTB 32**
    (`log2_diff_max_min_luma_coding_block_size = 2`), which would fall back to CPU decode. Check
    every file.
- Main (8-bit), not Main 10. Our frame path squashes 10-bit on the CPU.
- Level ≤ 4.1 at 1080p30; x265 picks it.
- [V] On a 2 s 1080p30 test source the command gives `hvc1` Main L4.0 (`level=120`), AAC-LC,
  `moov` at byte 32, CTB 64, 1 slice, no tiles.
- `-movflags +faststart` is required. None of the three hosts remuxes the original, and a `moov` at
  the end means reading the whole file before the first picture.
- `hvc1`, not `hev1`, is the tag our `canPlayType` answer was measured with. PeerTube's HLS
  helper also special-cases one tag (see its section).
- Bitrate ≤ ~8 Mbit/s stays well inside Odysee's 19.5 Mbit/s cap and leaves the Pi's HTTPS
  download headroom.
- Before uploading: run the `trace_headers` check above, and play the file on the Pi from
  `file://` in B8's page with `hw=1`.

**The demo file (prepared 2026-10-08, not published):** the showcase reel of 2026-09-30 encoded
with the recipe above (no `-c:a`: the reel has no audio track) —
`artifacts/media/demo/phoenix-rpi4-showcase-hevc-1080p30.mp4`, HEVC Main `hvc1`, 1920x1080 30 fps,
287 s, 2.6 Mbit/s, 94 MB. On the Pi: `hevc-rpivid-check -l 1` against the host's per-frame md5s,
**8610/8610 frames bit-exact** on the block with the default tool set (30.1 fps, decode 12.5 ms per
frame of one core incl. SAND); in the browser from an HTTP server (`b8.sh hevc`, as a site would
serve it) **30.0 fps presented, 97.5 % painted**, 7 frames dropped at start.

**2a. Primary host: Internet Archive** (free account, `archive.org/upload`):
- Upload the **`.mp4`** as the item's original, as mediatype "movies", with a CC licence. Upload
  `.mp4`, not `.mkv`: an MKV original is *not* offered to the player, only its H.264 derivative [V,
  `rocketrace-hevc`].
- IA keeps the original and adds a 480p H.264 `<name>.ia.mp4` derivative [V].
- **URL to open on the Pi:** `https://archive.org/details/<identifier>` or
  `https://archive.org/embed/<identifier>`.
  - The player's sources are `[{…ia.mp4, label "480p"}, {<original>.mp4, label "1080p HD"}]` [V].
  - JW Player most likely starts on the first source, 480p H.264 [I]. Choose **"1080p HD"** in the
    gear menu once; JW keeps the choice in `localStorage` (`jwplayer.qualityLabel`) [I].
  - For the demo, select it once before recording.
- **Direct file**, for B8's page or a bare `<video src>`:
  `https://archive.org/download/<identifier>/<file>.mp4`. It returns a 302 to `dn*.archive.org`,
  then 206 for Range requests, `content-type: video/mp4`, `access-control-allow-origin: *` [V].
- **Our browser needs nothing new:** JW's html5 provider checks only `canPlayType('video/mp4')`,
  with no MSE and no HLS for single-file video items [V by the research pass,
  `details-av.min.js`].
- Pi rehearsal (build 56, 2026-10-07, `demo-sites` cycle, a CC BY-SA item whose original is a
  `.MOV`, so only its 480p H.264 derivative is offered): both the **details page** (load 15.4 s)
  and the **`/embed/` page** (12.9 s) load cleanly in a 1280x960 window, and the player opens the
  file through WebKit's loader. **JW Player does not autoplay** (even with `--autoplay=allow`):
  the demo needs one click on play, and one on "1080p HD" for an `.mp4` original.
  - Choosing a quality needs a pointer click.

**Pi rehearsal of a PeerTube watch page** (build 56, 2026-10-07): `https://peertube.gravitywell.xyz/w/7tXP3FCq7b6oLDH7QnzVYM`
(third-party, test only) loads in 7.2 s, **autoplays** in the site's own player (web-video mode),
and the `hvc1` 1080x1920 59.94 fps upload decodes on the rpivid block: 60 fps presented, ~39 fps
painted, 43 dropped of 3603, played to its end — with `--autoplay=allow`. **With `/bin/browser`'s
defaults** (WebKit's policy: autoplay only without sound, as Chrome and Safari) the same page
(`demo-browser` cycle, XFCE session) shows the first picture, decoded on the block, and waits:
the demo is **one click on play** (or `WPE_BROWSER_AUTOPLAY=allow` before `browser`).

**2b. Alternative host: PeerTube with transcoding off.** Use it if we want the site's own player to
pick HEVC with no quality switch, or our own branding.
- The instance needs `transcoding.enabled: false` in `production.yaml`. The upload is then
  published as the "web video" file unchanged (PeerTube v8.3.1 `video-state.ts`,
  `local-video-creator.ts:143`; no codec check in the upload validators) [I, source]. On any
  default instance it is re-encoded to H.264.
  - **Self-hosted** (AGPL-3.0, a small VPS, our own terms) is the reliable choice.
  - Otherwise use an existing public instance with transcoding off. 41 of 1695 scanned report it;
    `peertube.gravitywell.xyz` allows sign-up with admin approval, but its terms are informal.
  - Set the video licence to CC BY or Public Domain.
- **URL:** `https://<host>/w/<shortUUID>` or `https://<host>/videos/embed/<uuid>`.
  - `?mode=web-video` forces the progressive player; PeerTube already picks it when a video has no
    HLS playlist.
  - Direct file: `https://<host>/static/web-videos/<file-uuid>-1080.mp4`.
- **Our browser needs nothing new:** web-video mode is `player.src(mp4)` with Range (`accept-ranges:
  bytes`, 206 [V]).
- Open risk: the PeerTube Angular/video.js front-end has never run in our WPE build.

**2c. Third option: Odysee.** Upload with *Transcode: No*; the original is then served from
`player.odycdn.com/v6/streams/<claim>/<id>.mp4` and the player sets `video.src` to it. See the
Odysee section below for the caveats.

**3. What our browser must support for the demo:** only what build 50 has: progressive MP4,
`hvc1` "probably", HTTP Range, a sane Referer on media requests. Native HLS (stage 0) and MSE are
not needed for the recommended path. They widen it to Bunny HLS (stage 0), Vimeo HDR (stage 0 plus
HDR) and Bilibili (MSE).

### Fallback and validation streams (no account needed)

Use these to bring up stage 0 (native HLS) and to validate the HEVC path before the demo upload
exists. All are clear (no `EXT-X-KEY`) [V]:

| Stream | What it carries [V: curl + ffprobe + trace_headers] | Use |
|---|---|---|
| `https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel-hevc.ism/.m3u8` | `hvc1.1.6.L150.90`, **Main 8-bit**, SDR, 24 fps, fMP4/CMAF, separate AAC group; 1680×750 / 2576×1150 / 3360×1500; CTB 64, 1 slice, WPP, no tiles | **First stage-0 test**: 8-bit SDR, and the hardware tool set. Pin `…/tears-of-steel-hevc-video_eng=902000.m3u8` for 1680×750 only |
| `https://devstreaming-cdn.apple.com/videos/streaming/examples/adv_dv_atmos/Job2dae5735-d6ca-48ca-91be-0ec0bead535c-107702578-hls_bundle_hevchls565/prog_index.m3u8` | 1920×1080 23.976, `hvc1.2.20000000.L123.B0`, Main 10, SDR BT.709, fMP4, video only; CTB 64, 1 slice, WPP | 1080p24 throughput, 10-bit path (swscale cost) |
| `https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_adv_example_hevc/master.m3u8` | 27 HEVC variants, all `hvc1.2.4.L123.B0` Main 10 SDR, fMP4 byte ranges in one `main.mp4`, audio as `EXT-X-MEDIA` groups (AAC, AC-3, E-AC-3) and `avc1` variants in the same master | Master-playlist variant selection. ⚠ 1080p and 720p are **60 fps**; the 30 fps ones are 768×432 (`v13/prog_index.m3u8`) and smaller |
| `https://dvdport.net/samples/hevc/bipbop/360p/index.m3u8` | Main 10, 640×360 30 fps, **MPEG-TS**, video only | HEVC in TS |
| `https://peertube.gravitywell.xyz/static/web-videos/9ebd0c01-f6f0-44fd-9f82-1ab2ccd4d924-1080.mp4` | progressive, `hvc1` Main 8-bit L5.0, 1080×1920 (portrait) **59.94 fps**, CTB 64, 1 slice, no tiles | A real user upload served unchanged by PeerTube. Third-party content: a test only, never the demo |
| An Internet Archive HEVC original (item in the [IA section](#consumer-sites)) | progressive, `hvc1` Main 8-bit 1080p **59.94 fps**, L5.1, **CTB 32** | A real IA upload served unchanged; exercises the **CPU fallback** (CTB 32 is outside the hardware tool set). Third-party content: a test only |
| `https://test-videos.co.uk/vids/bigbuckbunny/mp4/h265/1080/Big_Buck_Bunny_1080_10s_1MB.mp4` | Main 8-bit 1080p30, tagged **`hev1`**, video only | Checks that `hev1` plays as well as `hvc1` |

Stage-0 requirement this exposes: FFmpeg's `hls` demuxer exposes **every variant as its own
program**. `ffprobe` (host FFmpeg 8.0) on Apple's bipbop master lists 54 programs: the H.264
variants first, then the HEVC ones [V]. It fetches segments for every stream not set to `AVDISCARD_ALL`. So the stage-0 player must:
- pick one variant: prefer `hvc1`/`hev1`, ≤ 1080p, ≤ 30 fps, `VIDEO-RANGE=SDR` (or absent),
  8-bit before 10-bit;
- discard the others before reading packets.

Without that it downloads every rendition in parallel.

## Platform by platform

### PeerTube — the upload unchanged, when transcoding is off

- **Server** [I, PeerTube v8.3.1 source]:
  - Defaults: transcoding on, HLS on, web videos off, original deleted (`original_file.keep: false`).
  - With transcoding on, the built-in encoders are `libx264` and `aac`. The "copy instead of
    re-encode" shortcut applies only to H.264 input (`ffmpeg-default-transcoding-profile.ts:146`).
  - Plugins can add encoders (`transcodingManager.addVODProfile` / `addVODEncoderPriority`). The
    `vp9-opus-options` plugin proves the hook works, but no ready libx265 plugin exists.
  - A kept original is never public: `checkCanAccessVideoSourceFile` restricts it to the owner or
    an admin. [V] `GET /api/v1/videos/<id>/source` returns 401.
  - With `transcoding.enabled: false`, the upload is stored and served unchanged as
    `/static/web-videos/<file-uuid>-<res>.mp4`. Only `.mp4`, `.webm` and `.ogv` are accepted, there
    is no codec check, and no faststart remux is done.
  - The HLS codec-string helper (`server/core/helpers/ffmpeg/codecs.ts`) handles only the `hev1`
    tag (line 15). An `hvc1` stream reaches `baseProfileMatrix['hvc1'][profile]` at line 30, which
    throws [V, read]. HEVC over PeerTube *HLS* (a plugin encoder) is therefore untested ground.
    The helper builds HLS master `CODECS` strings; the live `hvc1` web videos above show that
    transcoding-off uploads are unaffected [V].
- **Player** [I, `client/src/standalone/player`]:
  - The mode follows the files: a video with an HLS playlist plays through `p2p-media-loader` and
    hls.js, otherwise `web-video`.
  - `?mode=web-video` forces it, on the watch page and on embeds.
  - Web-video mode is `player.src(mp4)`: no MSE.
  - In HLS mode without MSE, hls.js is not registered. If `canPlayType('application/vnd.apple.mpegurl')`
    is truthy, it sets the master `.m3u8` as a native source; otherwise it errors over to web-video
    mode.
  - PeerTube HLS is fMP4 with byte ranges in one file per resolution, which FFmpeg reads.
- **Live instances** [V, `/api/v1/config` of 1695 instances, ffprobe on public `fileUrl`s]:
  - framatube.org, tilvids.com and peertube.wtf have HLS only; peertube.tv and tube.tchncs.de
    have HLS plus web videos. All use the default H.264 profile.
  - **41 have transcoding off.** HEVC files are served as uploaded on:
    - `peertube.gravitywell.xyz` (v8.2.4, `hls`/`web_videos` disabled, sign-up with approval,
      100 GB quota): `hvc1` Main 1080×1920, re-checked here;
    - `videos.ookami.space`: Main 10 2160p, sign-up closed;
    - `tube.pompat.us`: Main 10 320p–1080p, sign-up closed.
  - Advertising an H.265 profile does not mean serving HEVC:
    - stream.andersonr.net (`hevc_nvenc`) served only H.264;
    - video.catgirl.biz (`VAAPI H265`) served one HEVC web-video file, but H.264 HLS.
- **Terms:** each video has a licence field (CC BY 1–6, Public Domain 7, All Rights Reserved 9).
  Instance terms vary. The software is AGPL-3.0: a modified self-hosted server must publish its
  source.

### Vimeo — HEVC only for HDR uploads; not for this demo

Method [V]:
- `player.vimeo.com/video/<id>/config` answers 403 "Sorry" for most videos. The same JSON is
  inlined in the embed page as `window.playerConfig = {…}` (`curl -A '<Safari UA>'
  https://player.vimeo.com/video/<id>`).
- IDs came from `https://vimeo.com/api/v2/channel/staffpicks/videos.json?page=1..3`.
- From each config: `request.file_codecs`, the HLS CDN entry's `*_url` keys, the master
  playlists' `CODECS` / `VIDEO-RANGE`, and the first media playlist's `EXT-X-KEY`.
- The server returned the same manifests to Safari, Linux Chrome and our UA.

Findings:
- **SDR uploads get no HEVC.** All 56 third-party staff picks scanned (1080p to 4K, uploaded
  2025–2026) report `file_codecs.hevc = {sdr: [], hdr: [], dvh1: []}`. They carry AVC (`avc1.64002A`
  at 1080p) and, for most, AV1 (`av01.0.08M.08`). The player code knows a `hevc_sdr_url`, but no
  config populated it [V].
- **HDR uploads get HEVC HDR.** `601558147` (Vimeo's Dolby Vision blog video) has `hevc_hdr_url`:
  `hvc1.2.4.L123.90` 1080p and up to `H153` 2160p, `VIDEO-RANGE="PQ"`, Main 10, `smpte2084`. Its
  tool set fits the hardware (CTB 64, 1 slice, WPP) [V]. Vimeo's help pages agree: HDR10 / HLG /
  Dolby Vision 8.4 uploads (≥ 10-bit, tagged transfer) yield "HDR10 and SDR renditions", and the
  SDR one is AVC [I, help.vimeo.com 12426058389649].
- **DRM:** both Vimeo-owned videos checked (`76979871`, `601558147`) are FairPlay SAMPLE-AES in
  HLS (`#EXT-X-KEY:METHOD=SAMPLE-AES,URI="skd://drm"`) and CENC in DASH. All 56 third-party videos
  are clear [V].
  - Whether a third-party *HDR* upload is clear could not be checked: the DV videos found
    (`482628714`, `481261741`) answered 401 "We couldn't verify the security of your connection"
    to curl [V].
- **Progressive MP4 is gone:** `files.progressive` is `[]` on every video [V].
- **Player without MSE** [V, `f.vimeocdn.com/p/4.46.123/js/{player,vendor}.module.js`]:
  - `HTMLScanner` plays HLS through `<video src>` when `canPlayType('application/vnd.apple.mpegurl')`
    is non-empty, so stage 0 would be used.
  - The HLS URL it gets is chosen by `G_.hevc`. With no MediaSource that is
    `SafariVersion ≥ 11 && (UA contains "mac os x 10_13" || iOS ≥ 11)`; with MSE it is
    `isTypeSupported('video/mp4; codecs="hvc1.2.4.H150.90"')`.
  - For HDR it additionally needs `G_.hdr` (`matchMedia('(dynamic-range: high)')`, or P3 plus
    `pixelDepth > 24`). Otherwise it uses `avc_url`.
  - The combined `url` master lists both `avc1` and `hvc1` variants, so a native player given it
    would choose by itself; Vimeo's code hands it out only when `avc_url` is absent.
- **Free plan:** "4K, HDR, & Dolby Vision support", 1 GB lifetime [I, help.vimeo.com 12425432518801].
- **Verdict:** HEVC from Vimeo means a 10-bit PQ upload, a spoofed iOS UA, claiming an HDR display,
  CPU 10→8-bit conversion with no tone mapping (a grey picture), and hoping the stream is not DRM'd.
  Not a "high quality" demo. It becomes interesting only after MSE plus real HDR→SDR tone mapping.

### Bunny Stream, Cloudflare Stream, Mux, Wistia, api.video

- **Bunny Stream:**
  - [I, docs] HEVC (x265) output exists only with **Premium Encoding** ($0.05/min per codec at
    1080p).
  - [I, docs] **"Keep original files"** serves the upload at `https://<zone>.b-cdn.net/<id>/original`:
    our HEVC MP4, progressive, unchanged. That URL plays with today's build.
  - [I, docs] Trial: 14 days, $20 credit without a card.
  - [V] The public demo library (`vz-cc3ec516-c62.b-cdn.net`) is all `avc1`. Its `play_720p.mp4`
    is H.264.
  - [V, `player.mediadelivery.net/assets/1.3.30/player.es.js`] The player is hls.js 1.7.3. Without
    MSE it falls back to `canPlayType(m3u8)` and native `src`; `?nativeHls=safari` forces that.
  - Unknown: whether a Premium library's master mixes `hvc1` and `avc1`, and whether "JIT
    encoding" segments work outside Bunny's player (the docs say they do not).
  - The share page `player.mediadelivery.net/play/<lib>/<id>` exists. It is a developer CDN, not a
    sharing community.
- **Cloudflare Stream:** "encodes and delivers videos using the H.264 codec"; downloads are
  "H.264/AAC" [I, docs]. The demo master is all `avc1` [V]. The Shaka player would go native here,
  but only with H.264.
- **Mux:** "high-bitrate H.264 for delivering 4K"; HEVC is accepted as input only [I, docs]. The 4K
  demo master is all `avc1`, up to `avc1.640034` [V]. The `master_access` MP4 is temporary
  (24 h).
- **Wistia:** [V] two homepage media (`fast.wistia.com/embed/medias/7sc5jnc9e0.json`) have all
  derivatives and HLS in `h264`, and the public `original` asset is the uploader's file. [I] An
  HEVC original would be exposed the same way, but the player's own path stays H.264.
- **api.video:** [I] H.264 MP4 and HLS only.

### Apple's HLS examples (test streams, not a sharing site)

Covered in the fallback table above. Apple's streams are clear and fMP4, but every HEVC variant is
**Main 10** (`hvc1.2.4…` / `hvc1.2.20000000…`) [V]. They exercise the 10-bit path and our
master-playlist handling, not the 8-bit fast path. bipbop's 1080p is 60 fps.

<a id="consumer-sites"></a>
### Internet Archive — the original MP4 is a player source

- **Upload** [I, IA help]: free account. The original is kept and stays downloadable; we choose the
  licence. IA's "derive" adds H.264 copies.
- **Verified** (`https://archive.org/metadata/<id>`, `curl -L -r 0-1`, ffprobe):
  - Item `t.-a.-d.-productions…x-265-josh`: original `…x265-Josh.mp4` (`source: original`,
    `format: MPEG4`) is `hevc Main hvc1 1920x1080 yuv420p` 59.94 fps, L5.1, CTB 32.
    `….ia.mp4` (`source: derivative`, `h.264 IA`) is 480p.
  - Item `rocketrace-hevc` (CC BY-NC-SA 4.0): the HEVC originals are `.mkv` (Matroska), and the
    player is given only their 480p H.264 `.mp4` derivatives.
  - The details page inlines the JW Player playlist. For the MP4 item, `sources` are the
    `.ia.mp4` (label `480p`) and the original (label `1080p HD`), both `type: mp4`.
  - `/download/<id>/<file>`: 302 to `dn721601.ca.archive.org/0/items/…`, then 206 with
    `content-range`, `content-type: video/mp4`, `access-control-allow-origin: *`.
  - [V by the research pass] The player is JW Player in `details-av.min.js`. Its html5 provider
    tests only `canPlayType('video/mp4')`; HLS is used only for multi-file audio items. No MSE.
- **Inferred:** the 480p source being first means playback starts in H.264 until the viewer picks
  "1080p HD". JW persists the label in `localStorage`.
- **Verdict:** usable today. Upload an `.mp4` with `hvc1`, pick "1080p HD", or link the download
  URL directly.

### Odysee (LBRY) — serves the original when "Transcode" is off

- **Verified by the research pass:**
  - `api.na-backend.odysee.com/api/v1/proxy?m=get` (`"method":"get"`) returns `streaming_url =
    https://player.odycdn.com/v6/streams/<claim_id>/<n>.mp4`.
  - A 1080p HEVC claim ffprobed as `hevc Main 10 hvc1 1920x1080`, with `content-length` equal to
    the claim's `source.size`: the upload served unchanged. A CC-BY AV1 claim was likewise served
    as uploaded.
  - Six stream URLs answered 200 `video/mp4`, none redirecting to HLS.
  - Player (`videoViewer` chunk): if the streaming URL redirects to `.m3u8` it uses hls.js, falling
    back to `video.src = m3u8` without MSE; otherwise `video.src = mp4` with no codec check.
  - Uploader: no codec restriction; recommended ≤ 9.5 Mbit/s, maximum 19.5 Mbit/s; a "Transcode:
    Yes/No" choice (the optimiser suggests transcoding above 5 Mbit/s but does not force it); a
    licence picker (CC BY 4.0 etc.).
- **My re-check** [V], same `get` call for claim `64e237d6…` with our UA:
  - The stream URL answered **401** without a Referer, and **429 Too Many Requests** with
    `Referer: https://odysee.com/`. Six tries over three minutes all got 429, after the research
    pass's many requests from this host.
  - So the CDN wants a Referer and rate-limits per IP. Inside the browser the Referer comes with
    the page (WebKit's media loader sends it); the rate limit is a demo-day risk to test.
- **Inferred:** publishing needs a small LBC (crypto) deposit. A future HLS rendition would make
  the default "adaptive" choice need native HLS ("Original" stays selectable). Much of the
  catalogue is re-uploaded commercial content, which matters for how the demo looks.
- **Verdict:** technically usable today; third choice for practical and reputational reasons.

### Dailymotion — H.264 only

- [V, research pass] `/player/metadata/video/<id>` returns one HLS `auto` URL, all `mpegts`. Under
  Safari, iPhone and our UA every format is `avc1` + `mp4a.40.2`, 380p–1080p (iPhone ≤ 720p).
- A "4k HDR" upload (`x8jkhi4`) is still `avc1.640028` at 1080p.
- The manifests are bot-filtered: 403/405 to curl.
- The player has a native-HLS path, `canPlayType("application/x-mpegURL")`. Its MSE codec check
  covers only `avc1`, `av01` and `vp09`.
- **Verdict:** no. H.264 1080p is also beyond our software decoder.

### Rumble — H.264 only

- [V, research pass] `rumble.com/embedJS/u3/?request=video&ver=2&v=v7dp942` lists `ua.tar`
  variants (360–1081) and an HLS master without `CODECS`. Segments are MPEG-TS `h264 High` + AAC.
- Progressive `ua.mp4` appears only on a 2019 video, also H.264.
- The player uses native HLS when `canPlayType("application/vnd.apple.mpegurl")` is set, otherwise
  hls.js.
- **Verdict:** no.

### Streamable — H.264 only

- [V, research pass] `api.streamable.com/videos/<code>`: `files.original` has metadata but no URL.
  `files.mp4` is `h264 High 1280x720` (206 on Range). The watch page is a plain progressive
  `og:video`.
- [I] Every upload is transcoded to H.264. Free tier: 250 MB, 10 min, deleted after 90 days.
- **Verdict:** no.

### Bilibili — HEVC only through MSE

- [V, research pass] `api.bilibili.com/x/player/playurl?…&fnval=4048`, logged out (480p cap): DASH
  with `codecid` 7 (`avc1`), 12 (`hvc1.1.6.L120.90`; the `.m4s` ffprobes as `hevc Main hvc1
  852x480`) and 13 (`av01`). Video and audio are separate tracks, so playback needs MSE.
- `fnval=0` or `1` returns a `durl` `mp4720`: H.264 720p.
- The player's `getFnVal` returns 1 (MP4) whenever MSE is unsupported, so without MSE we always get
  H.264.
- The CDN wants `Referer: https://www.bilibili.com/`. API risk control (412) blocks scripted
  clients.
- [I] Uploading needs a phone-verified (mainland) account. HEVC renditions are made by the server
  and cannot be forced.
- **Verdict:** a candidate only after MSE (stage 1). Even then, logged-out viewers get 480p.

### Native-HLS MIME strings the players test

Stage 0 must answer all of these, case-insensitively [V/I, the players above]:
- `application/vnd.apple.mpegurl` (Vimeo, Rumble, PeerTube, Bunny, Mux, Cloudflare);
- `application/vnd.apple.mpegURL` (Internet Archive);
- `application/x-mpegURL` (Dailymotion).

MIME types compare case-insensitively, so the case variants should be free; check that our
`supportsType` lowercases. What matters is registering both the `vnd.apple.mpegurl` and the
`x-mpegurl` families (plus `audio/mpegurl` and `audio/x-mpegurl`).

## Sources

- Vimeo: `https://player.vimeo.com/video/<id>` (inline `playerConfig`), `https://vimeo.com/api/v2/channel/staffpicks/videos.json`,
  player JS `https://f.vimeocdn.com/p/4.46.123/js/player.module.js` and `vendor.module.js`;
  [Watching HDR and Dolby Vision](https://help.vimeo.com/hc/en-us/articles/12425790453521-Watching-HDR-and-Dolby-Vision-Videos),
  [Upload HDR and Dolby Vision](https://help.vimeo.com/hc/en-us/articles/12426058389649-Upload-HDR-and-Dolby-Vision-videos),
  [Free plan](https://help.vimeo.com/hc/en-us/articles/12425432518801-What-s-included-with-the-Free-plan).
- PeerTube: source `github.com/Chocobozzz/PeerTube` v8.3.1; `/api/v1/config` and `/api/v1/videos/<uuid>` of public instances;
  [configuration docs](https://docs.joinpeertube.org/admin/configuration).
- Bunny `player.mediadelivery.net/assets/1.3.30/player.es.js`; Mux `@mux/playback-core`; Cloudflare Stream and Mux docs.
- Apple HLS examples: `developer.apple.com/streaming/examples/`.
- Internet Archive: `https://archive.org/metadata/<id>`, `https://archive.org/details/<id>` (inline JW playlist), `/download/<id>/<file>`.
- Odysee: `https://api.na-backend.odysee.com/api/v1/proxy?m=get`, `player.odycdn.com/v6/streams/…`.
- Dailymotion `/player/metadata/video/<id>`; Rumble `embedJS/u3`; Streamable `api.streamable.com/videos/<code>`; Bilibili `api.bilibili.com/x/player/playurl`.
- Test streams: `demo.unified-streaming.com` (tears-of-steel-hevc), `devstreaming-cdn.apple.com/videos/streaming/examples/{bipbop_adv_example_hevc,adv_dv_atmos}`, `dvdport.net/samples/hevc/bipbop`, `test-videos.co.uk`.
