# ffmpeg decode core — Phoenix-RTOS port (LGPL-2.1)

Phoenix-RTOS RPi4 (aarch64) port of the **ffmpeg decode core** —
`libavutil` + `libavcodec` + `libavformat` folded into a single **static ELF**
for software decode. Like the Quake ports, this is an **optional showcase**:
opt-in, pulls the ffmpeg source at build time, and the Phoenix core does not
depend on it.

**Pinned upstream:** ffmpeg `n6.1`
(`d4ff0020b40b524a490cf62eccbd3a318f4c0e58`). Recorded in
`build-ffmpeg-phoenix.py` (`FFMPEG_TAG` / `FFMPEG_SHA`).

## LGPL-only — no GPL

This port is built **LGPL-clean**: `./configure` is run **without**
`--enable-gpl` and **without** `--enable-nonfree`, and with a minimal
decode-only feature set (no external GPL codec libraries: no x264/x265, etc.).
The ffmpeg decode core is LGPL-2.1-or-later; the demo glue
(`e4_decode_demo.c`) carries an LGPL-2.1-or-later header (see `COPYING`). No GPL
enters the Phoenix system repositories — this port lives entirely in
`external/` (fetched, not committed) + `tools/ffmpeg-port/`.

## What this is (Phase 1)

A **running decode core, HW-VALIDATED on real Phoenix/RPi4 hardware.** The build
compiles the three static archives with **NEON asm on**, then links a real MJPEG
decode program (`e4_decode_file.c`) against them plus the **fresh** buildroot
`libphoenix.a` into one static AArch64 Phoenix ELF (zero undefined symbols).

**2026-08-06 on-Pi results (both HW-validated on the netbooted RPi4):**
- **MJPEG** (`e4-decode /usr/share/e4/test.jpg`): decoded a 96x64 baseline JPEG —
  `frame decoded 96x64`, plane-0 avg **127** (host ffmpeg 127.03), `DONE ok`, 0 faults.
- **H.264** (`e4-decode-h264 /usr/share/e4/test.h264`): decoded a 128x96 Annex-B clip —
  `frame decoded 128x96`, plane-0 avg **123** (host ffmpeg 123, **bit-exact**), `DONE ok`,
  0 faults. **NOTE:** the H.264 decode must run on a **large (8 MB) stack** — its
  DPB/deblocking/deep call chains overflow the small default main-thread stack (MJPEG does
  not); `e4_decode_h264.c` runs the decode on an 8 MB pthread. libphoenix `pthread_create`
  mmaps exactly the requested stack (no clamp).

- **Decode → HDMI** (`e4-fbshow /usr/share/e4/pattern.jpg`): decoded a 1280x720 JPEG and
  **displayed it on the HDMI output** — `/dev/fb0` is the live firmware framebuffer, so a
  YUV420→32bpp blit (byte order per pl011-tty) shows on screen. HDMI-verified: the image centered
  on black with correct colors (TL red / TR green / BL blue / BR white), 0 faults. The first
  *visible* output of the port. (`e4_fbshow.c` + `e4_fb_blit.h`; test images stage fine over NFS.)

- **Moving video** (`e4-play /usr/share/e4/clip.h264`): decoded a multi-frame color-cycling H.264
  clip in a paced loop and **played it on the HDMI screen** — HDMI-verified motion (frame 160 = cyan,
  end = magenta; different frames at different snapshots), `DONE ok (7 passes, 294 frames displayed)`,
  0 faults. Actual video playback on Phoenix. (`e4_play.c` + `gen_e4_clip.py`; runs on an 8 MB-stack
  pthread; `e4_fb_blit.h` shared.)

So the full pipeline — libphoenix file I/O + libavcodec (MJPEG **and** H.264) + NEON + the
new libm — actually decodes correctly on hardware, **puts pixels on the HDMI screen, and plays
moving video**, not just links. (`e4_decode_demo.c` remains a minimal link-only variant.)

Enabled decode-only feature set (the proven recipe):

- **decoders:** `mjpeg`, `h264`, `rawvideo`, `pcm_s16le`
- **parsers:** `h264`
- **demuxers:** `mjpeg`, `wav`
- **protocol:** `file`
- asm **on** (`--enable-asm`, NEON); static only; no programs / network / docs /
  shared libs.

## Build it

```
# once, if the fresh libphoenix.a is absent:
./scripts/rebuild-rpi4b-fast.sh --scope core

python3 tools/ffmpeg-port/build-ffmpeg-phoenix.py
# -> fetches+pins external/ffmpeg (n6.1), configures, builds the 3 archives,
#    links /tmp/e4_decode-phoenix  (PASS: static AArch64 EXEC, 0 undefined)

# to reuse an existing n6.1 clone (used AS-IS, no clone/checkout) for fast
# local testing:
FFMPEG_SRC=/path/to/ffmpeg python3 tools/ffmpeg-port/build-ffmpeg-phoenix.py
```

The upstream ffmpeg source is **not** vendored here. The script fetches a pinned
`n6.1` clone into `external/ffmpeg` (or uses `FFMPEG_SRC` as-is), configures it,
and patches only the **generated** `config.h` — never ffmpeg source.

## The one build fix: `config.h` HAVE_{ERF,EXP2,EXP2F,LOG2F}

ffmpeg's `./configure` link-probes `erf`, `exp2`, `exp2f`, `log2f` against the
**stale toolchain sysroot** libphoenix, which *declares* them in `math.h` but
does not *define* them. The probe fails → configure sets `HAVE_*=0` → ffmpeg
emits its own `static inline` fallbacks in `libavutil/libm.h`, which then clash
with libphoenix's non-static prototypes (a hard compile error that reads
confusingly like a toolchain bug).

The **fresh** buildroot `libphoenix.a` we actually link against genuinely
defines all four (plus `scalbn`, `erfc/erff/erfcf`), so the build driver
flips those four `HAVE_*` flags `0 → 1` in the generated `config.h`
**after** configure. This is both the whole compile fix and link-honest. The
driver **asserts exactly four substitutions land** and fails loud otherwise, so
a future configure change can't silently reintroduce the clash.

## Verified status

`build-ffmpeg-phoenix.py` was run end-to-end (configure → patch → **clean**
archive build → link → verify) and produces a **static AArch64 Phoenix ELF with
0 undefined externals**:

- `readelf -h`: `ELF64`, `Machine AArch64`, `Type EXEC`; no dynamic section
  (fully static).
- `nm | grep ' U '` → **0** undefined externals; **0** weak-undefined (`w`).
- Projected closure lands as defined (`T`): `exp2`, `scalbn` (fresh
  libphoenix), `__addtf3`, `__aarch64_ldadd4_acq_rel` (libgcc, auto-linked),
  `main`, `_start`.
- ELF size ≈ **3.06 MB** as built (loadable footprint ≈ 0.59 MB
  `text+data+bss` via `size`). Of the 3.06 MB, ~2.4 MB is retained
  debug/symbol sections: `aarch64-phoenix-strip` reduces the ELF to **≈ 0.63
  MB** (verified). The original feasibility probe reported a 1.31 MB ELF; the
  on-disk size varies only with retained debug/symbol content — the
  link-complete / 0-undefined milestone is identical.

The archives build clean (no compile-fail TUs): `libavformat.a`, `libavcodec.a`,
`libavutil.a`.

## The link line (proven)

```
aarch64-phoenix-gcc -o e4_decode-phoenix e4_decode_demo.o \
  -Wl,--start-group \
    external/ffmpeg/libavformat/libavformat.a \
    external/ffmpeg/libavcodec/libavcodec.a \
    external/ffmpeg/libavutil/libavutil.a \
    .buildroot/_build/aarch64a72-generic-rpi4b/lib/libphoenix.a \
  -Wl,--end-group -lm -lgcc
```

The fresh `libphoenix.a` sits **inside** the group and is searched before the
gcc driver's implicit trailing (stale) `-lphoenix`, so the new libm symbols win
and no stale copy is reached. libgcc supplies the compiler-runtime symbols
(outline-atomics, TFmode soft-float for 128-bit `long double`) automatically, as
for every Phoenix ELF.

## Scope: running decode core (HW-validated), not yet a media player

The decode core is **proven end-to-end on hardware** (a 96x64 JPEG decodes
correctly on the Pi — see above). `e4_decode_file.c` is that real demo;
`e4_decode_demo.c` is a minimal link-only variant kept for reference. What
remains is turning a working decoder into a usable feature (a player), which is
runtime/integration work, not a port/link problem.

## Remaining runtime/integration work

None are toolchain/link blockers; they are runtime + infra:

1. **Larger media over NFS.** The 1.4 KB test JPEG reads fine, but multi-MB
   video hits the netboot NFS speed/reliability limit — stage such clips on
   **SD or tmpfs**, **not** the NFS root. File delivery over the ~100 Mbps flaky
   netboot NFS is the headline runtime risk (the same limit that gated large
   game assets), not a decode bug.
2. **Frame sink + moving playback — DONE (HW-validated 2026-08-06).** `e4_fbshow.c`
   blits one decoded frame to `/dev/fb0` (the live firmware HDMI framebuffer); `e4_play.c`
   loops+paces a multi-frame clip = **moving video on screen** (see the Decode → HDMI and
   Moving-video results above). Remaining toward a real *media player*: real content
   (not synthetic clips), audio (/dev/audio0 exists), container demux, seeking.
3. **Threading.** ffmpeg's pthread frame/slice threading API satisfies configure
   but is unproven under load on Phoenix — run `-threads 1` for first bring-up.
4. **h264 — DONE (HW-validated 2026-08-06).** `--enable-decoder=h264
   --enable-parser=h264` (NEON asm on) links 0-undefined with no new libc/libm
   gaps beyond mjpeg, and `e4_decode_h264.c` decoded a 128x96 clip bit-exactly on
   the Pi (see above) — with the decode on an 8 MB stack (see the H.264 note in
   the status section). Software H.264 on Cortex-A72 is realistic for SD/~720p;
   1080p is marginal. **VideoCore HW decode is out of scope** (Phoenix has no
   V4L2/MMAL codec path — a separate large driver project).

See `docs/misc/2026-08-06-ffmpeg-port-feasibility.md` for the full
feasibility assessment this driver productionizes.
