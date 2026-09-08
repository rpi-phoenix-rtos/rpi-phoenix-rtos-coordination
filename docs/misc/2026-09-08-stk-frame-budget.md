# SuperTuxKart's ~1 s frame: a measured budget, and where the time is NOT

2026-09-08. Direct instrumentation of the in-process V3D winsys, replacing every
inferred frame-rate figure with counters. Method: `rpi4-v3d` commit `547a70c`
(diagnostic, `TODO(diag)`) counts submits and driver time and prints raw integers
once per 32 page-flips; the flip is the true frame boundary.

## Correction: STK's "Average FPS" is a TICK rate, not a frame rate

`ProfileWorld`'s `Number of frames` / `Average FPS` counts **physics ticks**.
`main_loop.cpp:644` runs `for (i = 0; i < num_steps; i++)` and calls
`World::updateWorld` once per tick; `ProfileWorld::update` increments
`m_frame_count` on each call, and its own doc says *"ticks number of physics time
steps - should be 1"*. At the 50 ms `dt` cap there are ~6 ticks per rendered
frame, so the reported 5.6 "fps" is ~0.93 real fps.

My page-flip counter settles it: **32 frames per 32.00 s wall, six consecutive
windows ⇒ ~1000 ms/frame ⇒ ~1.0 fps.**

So the original ~1 fps figure was right; yesterday's "≥5.6 fps" retraction was
wrong and is itself retracted. Three wrong answers on one number, from three
different bad instruments — mpdecimate on a static scene, a saturated game clock,
and a tick counter mistaken for a frame counter. The flip counter is the first
measurement that counts the thing being asked about.

## The budget (steady state, hacienda, 1 kart, 1080p, full pipeline)

| component | per frame | share |
|---|---|---|
| CL submits (**exactly 8**/frame) | ~150 ms | 15% |
| all other V3D ioctls (~120/frame) | ~0.35 ms | ~0% |
| `fb_flip` VideoCore mailbox | ~0.15 ms | ~0% |
| CPU simulation (from `--no-graphics`) | ~23 ms | 2% |
| **unaccounted CPU** | **~825 ms** | **83%** |
| total | ~1000 ms | |

Raw windows (`wall_ms` for 32 frames): 32010, 32005, 31999, 31987, 32002, 32001 —
`cl=256` in every one, i.e. 8 submits/frame exactly. `tfu=0` and `csd=0` in steady
state, so textures are resident and nothing re-uploads during the race.

## What is ruled out

- **Fill rate / resolution.** Untestable via `--screensize` (the scanout FBO is
  bound to `/dev/fb0`'s native mode, so the request never reaches the render
  target — see `2026-09-08-stk-framerate-measurement-void.md`). Still open, but
  it cannot explain an 83% CPU share.
- **Render passes.** `--disable-dynamic-lights --shadows=0` removes the whole
  deferred pipeline (verified: *zero* sunlight/pointlight/tonemap/gaussian/
  combine shaders compile, vs all of them in baseline) and bought **1.7%**
  (232.02 s vs 236.06 s for the same tick budget).
- **GPU submit cost.** 15% of the frame. Even a free GPU leaves ~850 ms.
- **The VideoCore mailbox page-flip.** Measured 1–9 ms per *32* frames. This was
  my leading hypothesis — `v3d_phoenix_fb_flip` (`v3d_phoenix_power.c:310`) does
  `mmap` of the mailbox MMIO page **plus** an `mmap` of a
  `MAP_CONTIGUOUS|MAP_UNCACHED` message page, a `va2pa`, two spin-polls and two
  `munmap`s, *per frame*, on a FIFO the same file documents as unarbitrated. It
  is genuinely ugly and worth cleaning up, but it is **not** the cost. Refuted by
  measurement.
- **Audio.** `--no-sound` changed nothing.
- **Physics / AI / kart count.** 2% of the frame.

## Leading hypothesis for the 825 ms: uncached BO stores

`v3d_phoenix_winsys.c:979-981` maps **every** BO uncached by default:

```c
int mapflags = MAP_CONTIGUOUS | MAP_ANONYMOUS;
if ((c->flags & 0x1u) == 0u)
        mapflags |= MAP_UNCACHED;
```

Only `V3D_CREATE_BO_CACHEABLE` (Mesa's flag for CPU-read-back render targets)
drops it. So vertex buffers, index buffers, uniform buffers, textures **and the
command lists themselves** are uncached, and every byte Mesa's CPU side writes
into them is an uncached store — no write combining, no store merging, straight
to DRAM. The file's own comment already concedes the magnitude: *"zeroing 8 MB of
uncached fb memory per frame would tank fps."*

This fits every observation: it is invariant to resolution (upload volume does
not scale with pixels), nearly invariant to render passes (the geometry is still
uploaded), invariant to kart count, entirely CPU-side, and invisible to ioctl
timing because the stores happen in Mesa *between* ioctls. It also explains why
QuakeSpasm manages ~40 fps on the same winsys: it uploads far less per frame.

### Sanity-check the magnitude first — it constrains the experiment

825 ms is a lot to explain, and a bandwidth story alone does not fit:

| mechanism | rate | bytes/frame needed |
|---|---|---|
| cached write-back | ~3 GB/s | 2475 MB — impossible |
| uncached, write-combined streaming | ~300 MB/s | 248 MB — implausible |
| uncached, scattered small stores | ~60 MB/s | 50 MB — still large |

A kart scene does not upload tens of MB per frame, so if uncached memory is the
cause it is via **per-store latency, not bandwidth**: at 60–200 ns per
non-combined store, 825 ms is 4–14 M stores/frame, which *is* plausible when Mesa
writes millions of small values (per-vertex attributes, per-object uniforms, CL
packets) one at a time into `MAP_UNCACHED` memory.

That distinction decides the experiment: **a `memcpy` benchmark alone could
mislead**, because a large `memcpy` into Normal-NC memory may still write-combine
and look fast. The benchmark must measure *scattered small stores* (e.g. a strided
`uint32_t` write loop) alongside streaming `memcpy`, cached vs uncached.

**Not yet proven — do not record it as the cause.** The decisive experiment, in
order of cost:

1. Micro-benchmark on target: memcpy a few MB into a `MAP_CONTIGUOUS|MAP_UNCACHED`
   mapping vs a `MAP_CONTIGUOUS` one, and get GB/s for each. Combined with bytes
   written per frame this predicts the 825 ms or refutes it. Cheap, no risk.
2. Diagnostic build mapping BOs cached, with a `dc cvac` clean over dirtied
   ranges before submit (the pre-bin sequence already drains CPU stores). If fps
   jumps, confirmed. Correctness risk if any range is missed — diagnostic only,
   never in the image.

If it holds, the real fix is cached BOs plus explicit cache maintenance at submit
boundaries, which is a careful change touching all five games, not a one-liner.

## Housekeeping

The diagnostic is currently compiled into `libv3d-phoenix.a` and therefore into
all five game binaries in `.buildroot/_fs` and on the NFS export — 24 bounded
stderr lines per run, on UART only, invisible on HDMI. The **flashable image
`486acda5…` was cut before this and does not contain it.** Remove the
instrumentation when this step closes.
