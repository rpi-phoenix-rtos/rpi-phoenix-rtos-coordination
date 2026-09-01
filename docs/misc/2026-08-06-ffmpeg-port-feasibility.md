# E4 — ffmpeg / libavcodec port feasibility (Phoenix-RTOS, Pi 4 aarch64)

Date: 2026-08-06
Scope: **bounded feasibility assessment**, host-side only. No source repo modified, nothing
committed, Pi/netboot untouched. ffmpeg **n6.1** shallow-cloned to
`/home/houp/.claude/jobs/c8f1289c/tmp/ffmpeg` (temp, not added to `external/`).
Toolchain probed: `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc` (GCC 14.2.0).

---

## Verdict (two-tier — pick the question)

- **Core library port (build libavutil/libavcodec.a for a small decoder set): TRACTABLE.**
  Every cheap build probe came back green. This is the question the probes actually answer.
- **End-to-end "video decode on the Pi", unattended, HW-validated: HARD-BUT-POSSIBLE.**
  Gated on things *not* probed here: sw-decode perf at resolution, getting video files onto
  the Pi over the flaky/slow NFS root, and the absence of any HW-decode path. None is a
  toolchain blocker; together they make an unattended end-to-end demo the hard part, not the port.

**INFEASIBLE-UNATTENDED** applies only to Pi **hardware** video decode (VideoCore) — see §5.

---

## What was actually observed (evidence)

### 1. `./configure` works for this cross target — first pass, exit 0
```
./configure --enable-cross-compile --arch=aarch64 --target-os=none \
  --cc=aarch64-phoenix-gcc --cross-prefix=aarch64-phoenix- \
  --disable-everything --enable-decoder=mjpeg,rawvideo --disable-asm \
  --disable-doc --disable-programs --disable-network --disable-pthreads \
  --disable-shared --enable-static
  → CONFIGEXIT=0
```
`--target-os=none` was accepted directly; no fallback to `linux` needed, no autotools involved
(ffmpeg uses its own hand-rolled `configure` shell script — no libtool/automake/hosted-POSIX
assumptions to fight). The only warning is a benign `pkg-config not found` (irrelevant: we
`--disable-everything` for external libs). `config.log` probe failures are **all expected
optional-feature probes** that fail harmlessly: `windows.h`, `linux/videodev2.h`, `X11/Xlib.h`,
`vdpau`, Objective-C, `_mingw.h`, `dlfcn.h`, etc. — none gate the requested build.

### 2. NEON / aarch64 hand-written asm ASSEMBLES — keep asm ON
Second configure with `--enable-asm --enable-neon` → `HAVE_NEON=1`, `HAVE_ARMV8=1`, exit 0.
Assembled three representative `.S` files with the GNU `aarch64-phoenix-as`, all produced `.o`:
```
AS libavutil/aarch64/float_dsp_neon.o
AS libavcodec/aarch64/h264idct_neon.o
AS libavcodec/aarch64/hpeldsp_neon.o   → MAKEEXIT=0
```
Blocker (d) does **not** materialize: the hand-written aarch64 SIMD builds with this toolchain.
`--disable-asm` is *not* required (and would badly hurt decode perf — keep it on).

### 3. Threading — libphoenix pthreads satisfy the configure probe
With `--enable-pthreads`: `HAVE_PTHREADS=1`, `HAVE_THREADS=1`, `HAVE_PTHREAD_CANCEL=1`.
So ffmpeg's frame-/slice-threading API layer is available at build time.
**Caveat:** this proves the *API* satisfies configure; it does **not** prove frame/slice
threading is robust under load on Phoenix (detected, unproven-at-runtime). A conservative
first bring-up can run single-threaded (`-threads 1`) and add threads later.

### 4. Compile surface — one real blocker, and it is the known libm gap
Sampled ~14 TUs across libavutil + the libavcodec decode core. Cleanly compiled:
`mem`, `mathematics`, `buffer`, `log`, `avstring`, `time`, and (after the fix below)
`eval`, `rational`, `avpacket`, `codec_desc`, `decode`.

The **only** compile blocker hit is a declaration clash in `libavutil/libm.h`:
```
libavutil/libm.h:121: error: static declaration of 'erf' follows non-static declaration
  .toolchain/.../include/math.h:100: note: previous declaration of 'erf'
```
Root cause = the documented libphoenix pattern (MEMORY: *libphoenix math.h declares full C99
but only a subset is defined*). configure's **link** probe for `erf/exp2/exp2f/log2f` failed
(declared-but-undefined → link error) → set `HAVE_{ERF,EXP2,EXP2F,LOG2F}=0` → ffmpeg emits its
own `static inline` fallback → clashes with libphoenix's non-static prototype. Exactly 4
functions clash; every other math fn probed present (`cbrt, copysign, hypot, log2, lrint, rint,
round, trunc, isnan, isinf` all `HAVE_*=1`), so libphoenix libm is otherwise sufficient.

**Fix is two steps (a compile fix is NOT the whole fix):**
1. Compile: set `HAVE_{ERF,EXP2,EXP2F,LOG2F}=1` in `config.h` (or via a compat header /
   config patch) so ffmpeg stops emitting its inline fallback. **Verified:** doing this made
   `decode.o`, `avpacket.o`, `codec_desc.o`, `eval.o`, `rational.o` all compile (MAKEEXIT=0).
2. Link: because those symbols are now deferred to libphoenix and libphoenix *doesn't define
   them*, any enabled decoder that actually calls `erf/exp2/exp2f/log2f` will be an **undefined
   reference at link**. Supply the 4 definitions — trivially, ffmpeg's own `static inline`
   bodies already live in `libavutil/libm.h`; lift them into a shim `.c`, or implement in
   libphoenix per the standing "implement missing libc" rule. Simple decoders (mjpeg/rawvideo/
   h264 video path) are unlikely to pull these in at all, so the real closure is tiny.

### 5. Other libc gaps (projected from config.log link-probes, not a measured link)
No final link was attempted (per task). Projected closure from probes + compile sampling:
- `HAVE_MEMALIGN=0`, `HAVE_POSIX_MEMALIGN=0` — **benign**: `av_malloc` falls back to
  `malloc` + manual over-allocation/alignment. No action needed.
- `sysctl`, `sched_getaffinity`, `gethrtime` link-probes failed — **gated off** on this target
  (used only for CPU-count / hrtime paths that `--target-os=none` doesn't enable).
- The 4 libm symbols in §4 are the only *real* projected undefined refs.
Treat this as a **projected** surface, not a link-verified one; a minimal `libavutil.a` /
`libavcodec.a` link is the natural next step if pursued.

---

## No-dynamic-linking implication
Not a problem for a decode-only build. ffmpeg only needs `dlopen` for *external* codec libs
(x264/x265/etc.) and some hwaccel loaders — all excluded by `--disable-everything` +
`--enable-decoder=<builtin>`. Built-in decoders are compiled into `libavcodec.a` and linked
**statically** into one ELF, matching the proven Q2/Q3/quakespasm single-ELF pattern.
`--disable-shared --enable-static` already forces this; `config.log` shows no `dlfcn`/`dlopen`
dependency in the requested config.

---

## Pi 4 HW decode (VideoCore) vs software libavcodec — (e)
**Software decode is the tractable path; HW decode is a separate, much larger project.**
- The Pi 4's VideoCore H.264 decoder is reached on Linux either via the deprecated **MMAL**
  firmware interface or the **V4L2 stateful M2M** driver (`bcm2835-codec`). **Phoenix has
  neither** — no V4L2 subsystem, no kernel codec driver. ffmpeg's `h264_v4l2m2m` / `h264_mmal`
  decoders would have nothing to bind to.
- Phoenix *does* have a userspace VideoCore mailbox pattern (thermal driver, `libvcmbox`), but
  the codec block is a different firmware channel that is not wired up. Bringing up HW decode =
  writing a from-scratch mailbox/V4L2-style codec driver + firmware protocol — **far larger**
  than the sw port and squarely **INFEASIBLE-UNATTENDED**.
- Software H.264 on Cortex-A72 @1.5 GHz with NEON: realistic for SD / ~720p; **1080p is
  marginal**. mjpeg/rawvideo are cheap. Recommend starting sw-only.

---

## Key risks
1. **NFS runtime-read limit (the headline runtime risk).** The netboot NFS root is ~100 Mbps
   with occasional read failures — the same limit that gated large-asset apps (games).
   Multi-MB/GB video files streamed off NFS during decode will hit exactly this. Mitigation:
   test with a tiny clip staged on SD/tmpfs, not a large NFS file; treat NFS video streaming as
   out of scope for a first demo.
2. **libm undefined-reference at link** (§4 step 2) — low effort but must not be skipped; the
   compile-only fix hides it until link time.
3. **Threading robustness under load** — API present, runtime unproven; de-risk with
   `-threads 1` first.
4. Perf ceiling at 1080p sw-decode (§5).

---

## Recommended approach if pursued
- **Codecs:** start `--enable-decoder=mjpeg,rawvideo` (near-zero libc surface, no threading), then
  add `h264` (with the NEON asm on) as the real target. `--enable-demuxer` only as needed
  (e.g. `mov,matroska` or just rawvideo) — keep the closure small.
- **asm:** **ON** (`--enable-asm --enable-neon`) — it assembles and is needed for perf.
- **threads:** build with pthreads, run `-threads 1` for first bring-up.
- **Build driver:** mirror the existing `tools/*-port` pattern — a `phoenix_ffmpeg_compat.h`
  force-include for the libm flag/decl reconciliation, plus a py driver that compiles each TU
  and link-drives to enumerate the undefined-symbol closure. Feed the config via a patched
  `config.h` (flip the 4 `HAVE_*` flags) rather than editing ffmpeg C.
- **Decode-only** (no encode, no network, no external libs, static single ELF).

## Rough effort estimate
- **Core-library port to a linking single ELF (mjpeg + h264 sw decode, asm on):
  ~2–4 focused sessions.** configure is free (done), asm free (done), the libm gap is the one
  real blocker and it's a known ~1-session pattern; the rest is enumerating a modest
  undefined-symbol closure and wiring a compat header + build driver.
- **On-Pi runtime bring-up + a playing/decoding demo:** add ~2–4 sessions, dominated by the
  file-delivery (NFS) problem and threading/perf tuning, plus a sink for decoded frames
  (`/dev/fb0` scanout already exists — a raw-frame-to-fb0 sink is plausible).
- **VideoCore HW decode:** not estimated — separate large driver project, out of scope.

## Go / no-go
**GO for a bounded software-decode core port** (mjpeg first, then h264, asm on, static ELF) —
the toolchain, asm, threads, and libc surface are all favorable and the single blocker is a
known-tractable libm gap. **NO-GO (defer) for VideoCore HW decode** and for any demo that
relies on streaming large video off the NFS root.

---

## 2026-08-06 core cross-build probe

Follow-up to the feasibility scan above, run **after** the 4 libm gaps (`erf/erfc/erff/erfcf`,
`exp2/exp2f`, `log2f`) were filled in libphoenix. This probe actually **builds the core static
libs** and enumerates the real undefined surface (not projected). Same rules: no ffmpeg source
edited (only the generated `config.h`), nothing committed, Pi untouched.

### Setup / sanity
- Fresh cross-checked libc = `.buildroot/_build/aarch64a72-generic-rpi4b/lib/libphoenix.a`
  (NOT the stale toolchain sysroot). All 7 new libm symbols confirmed `T` (defined):
  `erf, erfc, erff, erfcf, exp2, exp2f, log2f`.
- `./configure` (the task's decode-only line: decoders `mjpeg,rawvideo,pcm_s16le`; demuxers
  `mjpeg,wav`; protocol `file`; `--enable-asm`; pthreads auto-on) → **exit 0**. All 6 requested
  components confirmed `=1` in `config_components.h`.
- One expected config fix-up: configure link-probes against the **stale** sysroot, so it set
  `HAVE_{ERF,EXP2,EXP2F,LOG2F}=0` (→ ffmpeg emits its `static inline` fallbacks, which clash with
  libphoenix's non-static prototypes — the §4 compile blocker). Flipped those 4 flags to `1` in
  the generated `config.h`; that is the whole compile fix, and it is now *link-honest* because the
  fresh libphoenix genuinely defines them.

### Did the archives build?  **YES — all three, zero compile-fail TUs.**
```
make libavutil/libavutil.a libavcodec/libavcodec.a libavformat/libavformat.a  → MAKEEXIT=0
  libavutil.a    90 objects   (1.00 MB)
  libavcodec.a   62 objects   (0.56 MB)
  libavformat.a  29 objects   (0.38 MB)
```
No `error:` / `undefined` / `fatal` in the build log. NEON asm on (`HAVE_NEON=1`, `HAVE_ARMV8=1`).

### Undefined surface (measured via `aarch64-phoenix-nm`, counted `U` externals)
- 717 raw `U` refs across the 3 archives; 604 are cross-satisfied **within** ffmpeg itself.
- **113 external undefined** (not defined by any ffmpeg TU). Of those:
  - **102 SATISFIED by fresh libphoenix.a** — ordinary libc/POSIX + libm surface, no gaps:
    string/mem 21, stdio 11, stdlib/alloc 9, **libm 16**, pthread 12, time 5, file/fd/mmap 13,
    misc 15 (`__errno_location`, `opendir/readdir/closedir`, `gmtime_r`, `isatty`, `mkstemp`,
    `stderr`, `sinh/cosh/tanh`, …). Nothing in string/stdio/pthread/fs/time is missing.
  - **11 GENUINELY UNDEFINED** (not in libphoenix) — the entire "surface beyond libm":

    **(a) 10 = compiler-runtime, provided by libgcc — NOT a Phoenix gap.** All verified `T` in
    the toolchain's `libgcc.a` (`gcc -print-libgcc-file-name`); resolved automatically by the gcc
    driver on any link, exactly like every existing Phoenix ELF:
    - outline-atomics (5): `__aarch64_ldadd4_acq_rel`, `__aarch64_ldadd4_relax`,
      `__aarch64_ldadd8_acq_rel`, `__aarch64_ldadd8_relax`, `__aarch64_swp4_relax`
    - TFmode soft-float for 128-bit `long double` (5): `__addtf3`, `__multf3`, `__extenddftf2`,
      `__trunctfdf2`, `__floatunsitf`

    **(b) 1 = one real libc gap: `scalbn`.** Trivial: FLT_RADIX=2 on aarch64 so
    `scalbn(x,n) == ldexp(x,n)`, and libphoenix already defines `ldexp/ldexpf/frexp`. A one-line
    shim (or libphoenix add per the standing "implement missing libc" rule). Not a blocker.

### libm confirmation
Of the 7 new symbols, only **`exp2` is actually pulled** by this minimal decoder set (referenced
externally and resolved by libphoenix). The other six (`erf/erfc/erff/erfcf`, `exp2f`, `log2f`)
are **not exercised** by mjpeg/rawvideo/pcm but are **confirmed present** in libphoenix for when
h264 / richer decoders need them. So: the libm gap that blocked the prior scan is **closed** — no
libm symbol appears in the genuinely-undefined set.

### Verdict — how close is a linking decode-only ELF?
**Very close. Zero hard blockers.** The core decode libraries compile and archive cleanly with
asm on, and the *entire* external symbol surface resolves as: 102 → libphoenix, 10 → libgcc
(auto-linked), 1 → a one-line `scalbn` shim. Remaining work to a linking decode-only ELF:
1. Add/shim `scalbn` (~minutes).
2. Bake the `HAVE_{ERF,EXP2,EXP2F,LOG2F}=1` reconciliation into a proper `config.h` patch /
   `phoenix_ffmpeg_compat.h` force-include (so it survives reconfigure) — mechanical.
3. Provide `main`/entry + link against `libphoenix.a` + `libgcc.a` and drive out any last
   transitive refs. **Est. effort: well under one focused session** to a linking mjpeg/rawvideo/
   pcm decode ELF; h264 (with NEON) is the natural next increment and only risks pulling a few
   more (already-present) libm/pthread symbols.

**Two load-bearing caveats** (this is a name-level closure, not a verified link, per the prior
memo's norm): (a) matching is by symbol name across archives — archive-member transitive closure
through libphoenix is not link-verified; (b) libgcc resolution assumes the standard gcc-driver
auto-link (safe — it is how all Phoenix ELFs link today). Neither changes the verdict.

**GO** — a decode-only libavcodec ELF for Phoenix aarch64 is a short, routine step from here; the
only genuine libc addition is a trivial `scalbn` alias.

---

## 2026-08-06 decode ELF link probe

Follow-up to the core cross-build probe above. This step does the **real link** the prior probe
deferred: a minimal mjpeg-decode program driven through the actual decode call graph, linked
against the three built ffmpeg archives + the **fresh** libphoenix.a to a Phoenix aarch64 ELF.
Same rules: analysis-only, no Pi, no commits, no source-repo / sysroot / toolchain mutation
(work in job tmp `/home/houp/.claude/jobs/c8f1289c/tmp/e4_link`).

### Program (real call graph, not just name refs)
`main.c` exercises: `avcodec_find_decoder(AV_CODEC_ID_MJPEG)` → `avcodec_alloc_context3` →
`avcodec_open2` → `av_packet_alloc` / `av_frame_alloc` → `avcodec_send_packet` →
`avcodec_receive_frame` → `av_frame_free` / `av_packet_free` → `avcodec_free_context`. No real
input (drain packet only) — enough to pull the decoder open/close + send/receive machinery.
Compiled clean: `aarch64-phoenix-gcc -c main.c -I<ffmpeg-root>` → exit 0.

### LINKS? **YES — first try, exit 0, ZERO undefined.**
Working link line (headers = ffmpeg source root; archives = prior-probe build; fresh libc explicit):
```
aarch64-phoenix-gcc -o e4_decode main.o \
  -Wl,--start-group \
    <ffmpeg>/libavformat/libavformat.a \
    <ffmpeg>/libavcodec/libavcodec.a \
    <ffmpeg>/libavutil/libavutil.a \
    .buildroot/_build/aarch64a72-generic-rpi4b/lib/libphoenix.a \
  -Wl,--end-group -lm -lgcc
  → LINK_EXIT=0
```
No `-nodefaultlibs` / crt-override gymnastics needed. The gcc driver's implicit trailing
`-lphoenix` (stale sysroot) caused **no** multiple-definition and left **no** new-libm symbol
unresolved — because the fresh libphoenix.a inside the group is searched first and defines every
new libm symbol, so the linker never falls through to the stale copy.

### Verification (empirical, not projected)
- `aarch64-phoenix-readelf -h e4_decode` → `Class ELF64`, `Machine AArch64`, `Type EXEC`,
  entry `0x401568`. No dynamic section (fully static, as intended).
- **ELF size: 1,369,704 bytes (~1.31 MB).**
- `aarch64-phoenix-nm e4_decode | grep ' U '` → **0 undefined externals.** Nothing remains in
  any group (no libc/libphoenix gap, no libgcc-runtime gap, no ffmpeg-internal gap, no
  link-mechanics gap).
- Spot-checks confirm the projected closure landed as `T` (defined) in the ELF:
  - libm/from fresh libphoenix: `exp2`, `scalbn`, `ldexp`, `log2`, `pow`, `sqrt` — all `T`.
  - libgcc runtime auto-linked: `__addtf3`, `__multf3`, `__aarch64_ldadd4_acq_rel` — all `T`.
  - crt0 + entry: `_start` and `main` — `T`.
- Note vs. prior probe: `scalbn` is **no longer even a gap** — the fresh
  `.buildroot/_build/.../libphoenix.a` already defines it (`T`), so the projected "one-line
  shim" is unnecessary against this libc.

### Verdict — decode core is LINK-COMPLETE for Phoenix aarch64
The prior probe's name-level closure is now **link-verified**: a real mjpeg-decode call graph
links to a static AArch64 Phoenix ELF with **zero undefined symbols** and no link-mechanics
tricks. Both load-bearing caveats from the previous section are now discharged — archive-member
transitive closure through libphoenix **does** resolve, and gcc-driver libgcc auto-link **does**
supply the runtime. There are **no remaining toolchain/libc/link blockers** for a decode-only
(mjpeg/rawvideo/pcm) build.

**What's left before an on-Pi decode demo** (all runtime, none toolchain):
1. Package this as a proper `tools/*-port`-style driver: a `phoenix_ffmpeg_compat.h` / `config.h`
   patch that bakes the `HAVE_{ERF,EXP2,EXP2F,LOG2F}=1` reconciliation so it survives reconfigure
   (mechanical), and a real `main` that decodes a staged clip to `/dev/fb0` (raw-frame → fb0 sink
   already exists).
2. Stage a **tiny** mjpeg/clip on **SD or tmpfs**, not the NFS root — file delivery over the
   ~100 Mbps flaky NFS is the headline runtime risk (unchanged from §Risks), not a decode bug.
3. h264 (NEON asm on) is the natural next increment — only risks pulling a few more
   already-present libm/pthread symbols; re-run this exact link probe against an h264-enabled
   build to confirm.
4. Threading: run `-threads 1` for first bring-up (API present, runtime unproven).

**GO** — decode core links cleanly to a Phoenix ELF today; remaining work is a build-driver
wrapper + an on-Pi runtime/file-delivery demo, not a port blocker.
