# M10b: hardware HEVC decode in the shipped players (`hevc_rpivid`)

Owner GO 2026-10-01: "H.265 hardware decode within the shipped players, the best way we can."

The code is on ports branch `hevc-hwdec` (`84a8059`, `01d6cc7`, `9621a14`), merged into ports master as `2b742d6`. It is built in build 20.

## Design

**What `hevc_rpivid` is.** FFmpeg's own `hevc` decoder with an rpivid backend, registered before `hevc`, so `avcodec_find_decoder(HEVC)` picks it.
- FFmpeg parses the stream, keeps the DPB and the output order, and handles seek and flush.
- The BCM2711 rpivid block decodes each picture into SAND128 buffers.
- A NEON pass de-tiles them into the frames FFmpeg allocated, as yuv420p or yuv420p10.
- ffplay (full screen and windowed) and gtk-video use it with no code change.

**Why not a standalone decoder over our own `hevc_parse`.** That parser:
- cannot handle POC > 255, CRA/RASL, multiple slices, or emulation prevention in slice headers;
- does not check most SPS/PPS fields.

`hevc-play` therefore gives up at picture 248 of `showcase1080.265`.

**Equivalence on the host.** `hosttest/run.sh` runs both decoders against a register-level mock of the block. The new path programs it byte for byte like `hevc-play` on all 39 vectors in `tools/hevc-decode/testdata`, up to and including the 1058-picture 1080p `IMG_8331`. The result is the same with 4 frame threads.

**Fallback.**
- A stream the block cannot take never leaves the CPU, and the log says `rpivid: CPU decode: <reason>`.
- A picture the block cannot take, or a hardware error, moves the rest of the stream to the CPU.
- `FFMPEG_RPIVID=0` forces the CPU, and `2` enables every tool (see "Tool gates" below).
- `FFMPEG_RPIVID_TOOLS=-amp,+tiles` turns single coding tools off or on.
- In ffplay, `-vcodec hevc` selects the plain CPU decoder.

**What the block decodes at level 1 (the default).** The tool set proven bit-exact on the Pi: 8/10-bit 4:2:0, CTB 64, one slice segment, no tiles, WPP, SAO, TMVP, B-pyramid, multi-ref, weighted prediction. Since 2026-10-07 also the default-on tools of "Tool gates" below (pending their Pi check). Anything else falls back to the CPU.

**Licence.** The new sources are BSD-3. The FFmpeg patch is LGPL, as are the files it touches. There is still no `--enable-gpl`.

**Expected cost at 1080p.** About 4.5 ms per picture on the block plus 5–10 ms of de-tile from uncached memory: 10–16 ms per picture, ≈30–45 % of one core at 30 fps.

## Pi check (pre-registered 2026-10-01)

1. **Build proof.** `strings /usr/bin/ffplay | grep -c 'rpivid: hardware HEVC decode'` ≥ 1, and `/usr/bin/hevc-rpivid-check` is present.
2. **Correctness.**
   - Run `hevc-rpivid-check -crc -n 300 <1080p clip encoded with x265 hash=1>` and `hevc-rpivid-check /usr/share/video-demo/hevc-720p30-aac.mp4`.
   - PASS: `RPIVID-CHECK verdict=BIT-EXACT … bad=0 hw_used=1 hw_fallback=0`, plus `sei_hash_bad=0`.
3. **Players.** `video-play` with an HEVC 720p and a 1080p clip, against `-vcodec hevc`.
   - The hardware run prints `rpivid: hardware HEVC decode …` and `rpivid-stat` lines; the CPU run prints neither.
   - Compare the `ffplay-stat` fps and drops. At 1080p, expect hardware ≥ CPU fps with fewer drops.
   - gtk-video prints `GTK-VIDEO decoder hevc_rpivid`.
4. **CPU %.** `top` while playing, hardware against `-vcodec hevc`.
5. **Level-2 promotion.** Each feature clip that `-crc` reports BIT-EXACT under `FFMPEG_RPIVID=2` can move into level 1. Superseded 2026-10-07 by the per-tool gates and the `rpivid-check` stream set ("Tool gates" below).

## Result (2026-10-02, builds 20–21)

**Correctness:** bit-exact against the CPU decoder.

| Clip | Frames | Verdict | Fallbacks |
|---|---|---|---|
| 720p | 900/900 | BIT-EXACT | 0 |
| 1080p (`-crc`) | 600/600 | BIT-EXACT; SEI picture hashes 600/600 | 0 |

**Decode cost:** measured with `hevc-rpivid-check`, build 21. Build 20's first comparison measured the check tool's own MD5 and allocation, not the decoder (fixed in `hevc-hwdec-perf`).

| Decoder | Time per 1080p frame | fps | CPU |
|---|---|---|---|
| hardware (`-hw`) | 18.9 ms | 52.8 | 60% of one core |
| CPU, 4 threads (`-cpu`) | 21.3 ms | 47.1 | 337% |

The hardware path is faster and needs about 6× less CPU. Of its 15 ms per picture, 4.5 ms is the block and 10.5 ms the uncached de-tile.

**Players:** `video-play`, KMSDRM full screen, run `m10-hevc-ab`.

| Decoder | Playback | Dropped frames |
|---|---|---|
| hardware | 30 fps | only in the first 2 s (720p: 17 early; 1080p: 26 early) |
| CPU (`-vcodec hevc`) | 30 fps | only in the first 2 s (10–19 early) |

- Both play the 720p and 1080p clips in real time and drop nothing after startup.
- The hardware path prints `rpivid: hardware HEVC decode … completion by interrupt` and `rpivid-stat` lines; the CPU path prints neither.
- At 30 fps the player cannot tell the two apart by frame rate. The gain is CPU: about 2.8 cores freed during 1080p playback.

**Robustness:** the block's known intermittent decode error appeared once in five 600-picture runs (POC 95, `CFSTATUS 71 CFNUM 264`). The stream then continued on the CPU with no visible break (`hw_fallback=1`), as designed.

**Not yet run:** gtk-video's `GTK-VIDEO decoder hevc_rpivid` line (it needs the XFCE session) and level-2 promotion.

## Tool gates (2026-10-07)

**Why.** A real PeerTube upload (HEVC Main, 1080×1920, 59.94 fps) played in the browser on the CPU at about 2 fps shown (build 51, `hevcweb2`): `rpivid: CPU decode: transform hierarchy depth 1/0 (verified: 0/0)`. Its next gate would have been `diff_cu_qp_delta_depth 0`. Level 1 took only x265's default tool set.

**The hardware supports every Main / Main 10 tool.** The Raspberry Pi Linux `hevc_dec` driver (`drivers/media/platform/raspberrypi/hevc_dec/`, GPL) limits only: 4:2:0, 8 or 10 bit, 32..4096 a side, CTB ≤ 64, TB ≤ 32. It was **read for understanding only; no code was copied** (ports `files/rpivid/` stays BSD-3, our own). `rpivid_cmd.c` already programmed each tool as the driver does:
- SPS0: the TU depths. SPS1: AMP, PCM, scaling lists, strong intra smoothing.
- PPS: CU QP delta depth, transquant bypass, transform skip, sign hiding, chroma QP offsets, constrained intra.
- The scaling factor array; CONFIG2 (merge level, PCM loop filter, constrained intra).
- The slice messages (deblocking, long-term flags); the slice / tile / WPP entry-point sequence.

So the change is in the gates, not the programming.

**One gate per tool** (`rpivid_hevc.c` `tools[]`). A tool the Pi finds wrong turns off alone, by its default in `tools[]` or at run time:

```
export FFMPEG_RPIVID_TOOLS=-amp,-slices    (+name turns one on; "none" = the proven set; "all")
```

| Tool | Default | Means | Streams enabling it (set below) |
|---|---|---|---|
| `tu_depth_intra` | on | transform tree depth > 0 in intra CUs (the PeerTube stream) | 126 |
| `tu_depth_inter` | on | the same in inter CUs | 130 |
| `cu_qp_delta` | on | CU QP delta off, or at a depth other than 1 (the PeerTube stream: 0) | 151 |
| `ctb32` / `ctb16` | on / **off** (since rpivid-fixes-2) | 32×32 / 16×16 CTBs (x265 ultrafast; NVENC, QSV) | 11 / 8 |
| `blocks` | on | min CB ≥ 16, or TB sizes other than 4..32 | 23 |
| `amp` | on | asymmetric motion partitions | 131 |
| `no_sign_hiding` | on | sign data hiding off (AMD VCN, x265 ultrafast) | 20 |
| `transform_skip` | on | transform skip | 122 |
| `no_strong_smoothing` | on | strong intra smoothing off | 18 |
| `scaling_list` | on | default or coded scaling lists | 3 |
| `deblocking` | on | deblocking off, offsets, per-slice control | 22 |
| `chroma_qp_offset` | on | PPS / slice chroma QP offsets | 7 |
| `cabac_init` | on | `cabac_init_flag` | 121 |
| `merge_level` | on | parallel merge level ≠ 2 | 6 |
| `slices` | on | several slice segments per picture (hardware encoders) | 36 |
| `long_term` | off | long-term reference pictures | 7 |
| `pcm` | off | PCM CUs | 14 |
| `transquant_bypass` | off | lossless CUs | 5 |
| `constrained_intra` | off | constrained intra prediction | 5 |
| `tiles` | off | tiles | 15 |
| `dependent_slices` | off | a picture with a dependent slice segment | 18 |

- The last column counts streams whose SPS/PPS (or slices) turn the tool on. HM-encoded conformance streams enable transform skip, AMP and `cabac_init` almost everywhere. CUs that really use a tool are certain only in the targeted x265 encodes and the named conformance streams (`TSKIP_A`, `AMP_*`, `SLIST_*`, `TILES_*`, `ipcm_*`, `LTRPSPS_A`, `DSLICE_*`, …).
- **Off** means level 2 only: these are rare in real uploads.
- A stream inside the limits whose tools are all enabled goes to the block.
- A PPS or picture needing a disabled tool sends the rest of the stream to the CPU (a whole picture, never half of one).
- The decoder logs `rpivid: hardware HEVC decode …, tools: <list> (not in the default set: <list>)`, and `rpivid: tools in use: …` when a later PPS adds one.

**One programming change.** In a picture of several slices, every slice now sends its own `slice_loop_filter_across_slices_enabled_flag` and its slice messages, as the driver does. One-slice pictures keep the proven form.

**Host evidence (no Pi).** Hosttest uses the register-level mock under ASan:
- `testdata`, on the proven set (`FFMPEG_RPIVID_TOOLS=none`): 39/39 programmed exactly like `hevc-play`, so the proven path is unchanged.
- At level 2, all 173 streams inside the limits attach, with no command-buffer error and no ASan report. The 4 `PICSIZE` streams are over 4096 and stay on the CPU.
- `--loop` (the mock writes the CPU decode into the SAND buffers) is bit-exact on all 32 encoded streams, with 1 and 4 threads. That covers output order, cropping, 10-bit, and multi-slice frames.
- Whether the **block** decodes the new tools bit-exactly can only be seen on the Pi.

**The stream set** is `tools/hevc-decode/rpivid-check/gen-set.sh`, 177 streams, 108 MB, staged at `/usr/share/video-demo/rpivid-check/` on the NFS root:
- 32 host encodes, one tool each: libx265 (including 10-bit, 1080p, ultrafast, veryslow), AMD VCN via VA-API (including 4 slices) and via Vulkan video.
- 144 HEVC v1 conformance streams (FFmpeg FATE's Main and Main 10 list).
- The PeerTube upload, `real-peertube-1080.mp4`, 3634 frames.
- Each stream has a `<stream>.md5`: per-frame md5s of the **FFmpeg 6.1** CPU decode, the Pi's own decoder. They match the host ffmpeg 8.0 `framemd5` except `CONFWIN_A`, `NUT_A` and `RPS_D`, where the two versions output different frames (`MANIFEST`: `ref8=DIFF`).
- `MANIFEST` lists the streams in check order, real-world first.
- End-to-end check of the staged copy on the host: `hevc-rpivid-check -hw -l 0 <dir>` (the CPU decoder in the hardware pass) gives 177/177 streams with `mismatches=0`.

**Pi check.** After building `video_player` (below), one command:

```
hevc-rpivid-check -l 2 /usr/share/video-demo/rpivid-check
```

- One line per stream: `RPIVID-CHECK stream=<name> frames=<n> mismatches=<m> fallback=<0|1> fps=<x> result=PASS|FAIL|CPU|ERROR ref=md5 ref_frames= first_bad= sei_checked= sei_bad= tools=<list> nondefault=<list> [cpu_why=…]`.
- Then `RPIVID-CHECK summary streams= pass= fail= cpu= error=`.
- A FAIL names the tools in its `tools=`. A tool that fails across streams, while streams without it pass, goes off: in `tools[]` (rebuild), or at once with `FFMPEG_RPIVID_TOOLS=-<tool>`.
- Add `-crc` for an independent oracle. The conformance streams and every x265 encode carry MD5 picture-hash SEI, which gives `sei_bad=` with no reference at all. It slows the run.
- **Expected CPU results:** the 4 `PICSIZE_*` streams (over 4096 a side).
- **Wedge:** a block timeout makes every later stream report `cpu_why=the block stopped responding earlier in this process`. Resume with `-from <next stream name>`: the run starts at that `MANIFEST` entry.
- The block's known intermittent error (see above) shows as `fallback=1` part way through a stream. Rerun that stream alone: `hevc-rpivid-check -l 2 /usr/share/video-demo/rpivid-check/<stream>`.
- Level 1 (no `-l`) shows what the browser and players do by default.

**Builds.**
- **`video_player`** (decoder + `hevc-rpivid-check`): rebuild it, then make sure the NFS root gets the new `/usr/bin/hevc-rpivid-check`. The old binary cannot read a directory or `.md5` references; it prints its usage and exits 2.
- **`webkit_wpe`:** relink it. It links `video_player`'s private `ffmpeg/` static libraries, and the PeerTube fallback was seen in the browser.
- The WebKit patch's comment on `FFMPEG_RPIVID` still says "verified tool set". It was left alone, because changing it would rebuild WebKit for a comment.

### Pi result, build 54, and the fix (rpivid-fixes-2)

**Result.** `hevc-rpivid-check -l 2`: 163 pass, 10 fail, 4 CPU (the `PICSIZE_*` streams).
- `real-peertube-1080.mp4` passed: 3634 frames, 0 mismatches.

**Cause of 9 of the 10 failures: stale slice-message state.**
- The block keeps the deblocking and QP-offset state of the last slice messages it received.
- An I picture whose messages were all defaults sent none (`hevc-play`'s proven form). It was therefore filtered with the previous picture's state.
- Every one of the 9 failures started at an I picture decoded right after a picture with deblocking off or offset, from the same stream or the previous stream in the run:
  - `x265-nosignhide` after `x265-nodeblock`;
  - `RPLM_A` after `RAP_B`;
  - `TMVP_A` after `TILES_B`;
  - `IPRED_A` after `ipcm_E`, then `IPRED_B` and `IPRED_C` (all-intra, so nothing resets the state);
  - `ipcm_C` and `ipcm_D` after `ipcm_B`;
  - `RAP_B` at picture 25: its IDR with deblocking on, after 25 pictures with deblocking off.
- The failing conformance streams have the same SPS/PPS as passing ones (`SAO_A`, `TSKIP_A`), so neither transform skip nor sign hiding is implicated.
- **Fix:** every slice sends its messages, as the driver does. `hevc-play` was changed the same way.
- This also affected the proven tool set: in the browser, the decoder process lives on from one video to the next.

**The one unexplained failure: `ctb16`.** `x265-ctu16` failed from its CRA (frames 55–90). `ctb16` is out of the default set until a re-check passes.

**Re-check** (failures after the stream or picture that provoked them, plus controls; the MANIFEST points into the full set):

```
hevc-rpivid-check -l 2 -crc /usr/share/video-demo/rpivid-check-rerun
```

**SAND → planar cost (browser, PeerTube 1080×1920 at 59.94 fps): 7.95 ms per picture on the CPU**, against 2.35 ms on the block.
- The SAND buffers are mapped uncached (`MAP_UNCACHED`). The de-tile reads 3.1 MB at about 400 MB/s, which is the uncached-read limit of one A72.
- **1. Cached mapping (recommended next):**
  - Map the output pool cacheable and `dc civac` it once after allocation (the kernel zero-fill leaves dirty lines).
  - Then `dc civac` each picture's range after phase 2, before the de-tile reads it. EL0 may do this: `SCTLR_EL1.UCI` is set, and lwip's genet RX does the same.
  - The CPU never writes SAND buffers, so its lines are always clean, and the block's reference reads stay correct.
  - Estimate: ≈0.5–1 ms of `civac` plus ≈1.5 ms of cached NEON copy, about **2–2.5 ms per 1080p picture**.
- **2. Threads:** split the columns over 2–3 threads. Uncached reads scale with cores until DRAM saturates: about 3–4 ms on its own, or about 1.2–1.5 ms together with option 1. Total with the block: about 4–5 ms per picture, inside the 16.7 ms of 60 fps.
- **3. Zero copy:** give the GPU the SAND buffer as a dma-buf with `DRM_FORMAT_MOD_BROADCOM_SAND128`. Mesa's v3d already imports it and de-tiles with a blit shader (`v3d_blit.c` `sand8_blit`). This costs no CPU, but needs our winsys / EGL dma-buf import with modifiers and a WebKit video sink that passes dma-bufs. The HVS can also scan SAND128 out directly as a plane (full-screen players). It is the larger project.

