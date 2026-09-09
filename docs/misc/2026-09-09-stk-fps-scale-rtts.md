# STK frame rate: measured budget, the shipped 1.5x, and the 2.2x the Y-flip gate blocks

Owner asked (2026-09-09) to improve SuperTuxKart's frame rate. Result: **shipped ~1.5x**
(avg 6 -> 9 fps) by rendering the deferred pipeline at 0.75 scale and upscaling; a further
**2.2x is measured and available but blocked** by Mesa's size-based Y-flip gate.

## Measured, on hardware, hacienda, 4 karts (STK's own on-screen counter)

| `scale_rtts_factor` | deferred RTT size | FPS min/avg/max | orientation |
|---|---|---|---|
| 1.0 (was default) | 1920x1080 | **5/6/6** | correct |
| **0.75 (shipped)** | 1440x810 | **8/9/9**, and 7/9/9 on a clean boot | **correct** |
| 0.5 | 960x540 | **10/13/15** | **3D scene UPSIDE DOWN** |

`scale_rtts_factor` scales every deferred render target (`rtts.cpp`) and the final
pass-through upscales to the window, so the 1080p GUI/HUD stays crisp. It is the setting
upstream ships on Android by device tier, so it is an exercised path, not a hack.

## The 0.5 flip is OUR bug, not STK's — and it has a second payoff

Mesa's `st_atom_framebuffer.c` forces `Y_0_TOP` only for FBOs **>= 1024x768**. At 0.5 the
deferred RTTs are 960x540, i.e. on the *other* side of that size test, so the upscale blit
lands flipped while the GUI (drawn straight to the scanout FBO) stays upright. 0.75 was
chosen to sit safely above the threshold; the minimum that clears it is **0.711**
(768/1080).

That makes the size test worth replacing with a real scanout predicate for two independent
reasons now: it unlocks STK's 2.2x **and** it retires the Quake II underwater Y-mirror
stopgap. Work order: `2026-09-08-flipy-scanout-gate-work-order.md`.

## Frame budget (from `rpi4b-uart-20260908-120533-stk-clockfix.log`, in-process counters)

171 ms/frame total: **150 ms (87.7%) inside 8 synchronous CL submits** (~18.8 ms each),
~21 ms CPU, page-flip 0.14 ms. Submits are synchronous by design in
`v3d_phoenix_winsys.c` (the ioctl spin-waits FLDONE then FRDONE), so that 150 ms is real
GPU-busy time. The 8 submits map exactly onto the 8 FBO binds STK issues per frame
(G-buffer, sunlight/pointlights, 3 light-scatter passes, colours, tonemap, scanout) --
`cl=8/frame` exactly, so there are **no spurious job flushes** in Mesa.

The 0.75 result settles the open question in that budget: the cost is **pixel-proportional**
(fill rate / bandwidth), not fixed per-submit overhead. Cutting pixels cut frame time.

## Three corrections to the record

1. **The prior A/B in `2026-09-08-stk-frame-budget.md` is VOID.** It reported
   `--disable-dynamic-lights --shadows=0` buying 1.7%. That run predates the
   `CLOCK_MONOTONIC` fix, when `getLimitedDt()` padded every frame out to the next
   whole-second `time()` boundary -- a 150 ms frame and a 900 ms frame both cost 1000 ms, so
   the comparison had **no discriminating power**. The deferred pipeline had never actually
   been perf-tested on this port. Same voiding applies to the `--no-sound` and kart-count
   comparisons. It does NOT affect `cl_ms`/`fb_flip_ms`/submit counts, which are direct
   counters.
2. **Upstream's own Pi mitigation is silently inert here.** `graphics_restrictions.cpp`
   matches rules by OS name with a `#else return false` fallthrough, so every rule carrying
   an `os` attribute never fires on Phoenix -- 36 of 43, including
   `<card vendor="Broadcom" os="linux" disable="HighDefinitionTextures256"/>`. On Linux/Pi
   that clamps `max_texture_size` to 256; here STK runs HD textures at 512 **and**
   uncompressed (compression is disabled on purpose -- it costs ~200 s/kart at startup).
   Untested lever, expected modest and confined to the G-buffer pass.
3. **My first 0.5 test was invalid and I nearly published its null result.** `psh`'s `mkdir`
   has no `-p`, so `mkdir -p /tmp/stk/config-0.10` failed, the `cp` went nowhere, and the
   launcher then seeded its own default config -- the run measured the baseline. It reported
   5/5/6 and would have "proved" that fill rate is not the bottleneck. Fix: two separate
   `mkdir`s, and `cat` the file on the target to confirm before believing the run.

## A/B recipe (no rebuild needed)

Stage a variant next to the assets, then at the psh prompt:
`mkdir /tmp/stk` -> `mkdir /tmp/stk/config-0.10` ->
`cp /usr/share/supertuxkart/config-X.xml /tmp/stk/config-0.10/config.xml` ->
`cat /tmp/stk/config-0.10/config.xml` (verify!) -> `stk --profile-laps=2 --track=hacienda --numkarts=4`.
Grouped params must be **attributes of the group element** (`<Video .../>` for
`scale_rtts_factor`/`max_texture_size`, `<GFX .../>` for `light_scatter`/`anisotropic`);
the flat form parses silently and is ignored. Keep `version="8"` and `enable_internet="2"`.

## Tested: `light_scatter="false"` is NOT worth shipping (and it confirms the diagnosis)

Measured on top of the shipped 0.75, settled mid-race (timer 00:24, 0 faults):
**9/9/10 vs 8/9/9** -- min +1, avg unchanged, max +1, i.e. inside single-run noise.

That is a useful negative. It removes 3 of the 8 passes, but those three are the
**half-resolution** light-scatter passes (~13% of the frame's tiles), so cutting them buys almost
nothing -- exactly what pixel-proportional cost predicts and the opposite of what a
fixed-per-submit-cost model would predict. Taken with the 0.75 result, the frame is
**fill-rate/bandwidth bound**, confirmed from two directions. Not shipped: no measurable gain, and
it costs the fog's light-scatter glow on hacienda.

⚠ **Measurement footgun found while doing it.** The first attempt read **9/9/10** off the final
frame and looked like a small win. It was invalid: STK had taken two `Exception #36` EL0 aborts
(its known ~40%-of-runs app-side crash), the last two frame transitions had motion of exactly
**0.000**, and the frame showed a *restarted* race at timer `00:00.033` -- a cumulative fps
counter with almost no samples. Rule: for an STK fps reading, require a **non-trivial race timer**
and a **non-zero motion** delta into that frame, and check the run's fault count first.

## Untested levers, ranked (all config-only, no driver risk)
2. `--disable-hd-textures` + `max_texture_size="256"` -- reproduces upstream's Broadcom rule.
   Both halves needed; the flag alone leaves the size at 512.
3. `--anisotropic=0`, `--disable-particles` -- free CLI probes; aniso 4x is live and
   particles blend into an RGBA16F target.
4. `--disable-dynamic-lights` -- best diagnostic (collapses 8 passes to 1-2), weak ship
   candidate: no tonemap, visibly flatter.

Explicitly de-ranked: Early-Z re-enable (`v3dx_draw.c` forces `V3D_EZ_DISABLED`; the commit
that disabled it recorded a constant EZTEST drain wedge -- demo-breaking across all five
games and X), and async submit (ceiling ~12%, needs interrupt-driven completion in the
winsys).
