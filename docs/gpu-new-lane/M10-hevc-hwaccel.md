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
- `FFMPEG_RPIVID=0` forces the CPU, and `2` enables the unverified tool set.
- In ffplay, `-vcodec hevc` selects the plain CPU decoder.

**What the block decodes at level 1 (the default).** The tool set proven bit-exact on the Pi: 8/10-bit 4:2:0, CTB 64, one slice segment, no tiles, WPP, SAO, TMVP, B-pyramid, multi-ref, weighted prediction. Anything else falls back to the CPU.

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
5. **Level-2 promotion.** Each feature clip that `-crc` reports BIT-EXACT under `FFMPEG_RPIVID=2` can move into level 1.

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
