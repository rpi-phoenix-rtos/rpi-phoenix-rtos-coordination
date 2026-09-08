# STK time-to-race: what helps, what doesn't

2026-09-08. All figures are wall-clock seconds for the same deterministic
1325-tick profile lap (`stk --profile-time=6 --track=hacienda --numkarts=4`),
which includes asset load, shader work and the race itself.

## The numbers

| config | first run of a boot | second run in the same boot |
|---|---|---|
| full deferred pipeline | 55.5 · 55.6 · 56.3 · 50.0 · 47.3 (n=5) | 20.0 |
| `--disable-dynamic-lights --shadows=0` | 22.7 · 22.4 (n=2) | 21.8 |

Two separate effects, and conflating them is what tripped me up:

1. **The deferred pipeline has a ~30 s one-time, per-boot warm-up cost.** It is
   paid only on the first run after a boot. `--disable-dynamic-lights
   --shadows=0` never pays it.
2. **It costs nothing at steady state.** Warm, full-deferred is 20.0 s against
   no-deferred's 21.8 s — if anything marginally *faster*. So the flags buy
   warm-up time, not frame rate.

### Correction to my own retraction

I first measured 55.5 → 21.8 in one boot and reported a "2.54× render win". I
then reversed the arms, saw that whichever ran *second* was ~20 s regardless, and
retracted it as an ordering artifact. **That retraction was too strong.** The
ordering effect is real *and* so is the flag effect — they are different things.
The correct statement is (1) and (2) above: no steady-state render win, but a
genuine ~2.4× improvement in time-to-race on a fresh boot, which is exactly the
case a demo take hits.

## RAM-staging the assets: measured, and it is a LOSS

`ram-stage-play /usr/share/supertuxkart/stk-assets /tmp/stk-assets /bin/stk …`
(the launcher gained a RAM-art-root detection for the test, since STK finds
assets only via `SUPERTUXKART_ASSETS_DIR` and psh cannot set environment
variables; reverted afterwards):

```
ram-stage: DONE creating RAM disk: 4120 files, 137.38 MiB in 72.978 s (1.88 MiB/s)
profile: Number of frames: 1325 time 47.272003
```

**73 s of staging to save 2.7 s** (47.3 vs 49.97 warm-cache) — a net loss of 70 s.
The copy is latency-bound on 4120 small files at 1.88 MiB/s, nowhere near NFS's
~30 MB/s sequential throughput.

Why it works for Quake but not STK: `ram-stage-play` copies the *whole* tree,
and quakespasm/Quake II read most of their ~47 MiB. STK's art root is 149 MiB
spanning 40 tracks and it reads a small fraction of it for one race, so staging
pays for 40 tracks to use one. A subset stage would not close the gap either —
even the ~35 MiB needed would cost ~18 s to copy against 2.7 s saved.

**Corollary:** the first-run cost is therefore *not* asset-read latency. With the
whole art root in RAM the run still took 47.3 s, so whatever the ~30 s is, it is
not reading asset files.

## The shader disk cache is not the answer either

Making the cache persist across boots (`sync-netboot-tree` `054bb8a72`, which
stopped wiping it every cycle) was worth **~10%**: 49.97 s against ~55.5 s. Real,
but it does not touch the ~30 s.

## Practical recommendation for a demo take

- Want the **best-looking** race: keep the full deferred pipeline (dynamic
  lights + shadows) and accept ~50 s before the lap completes on the first run
  after boot. It is a one-time wait, trivially cut in editing, and steady-state
  frame rate is the same or slightly better.
- Want a **quick** take: add `--disable-dynamic-lights --shadows=0` and the same
  lap finishes in 22 s on any boot.
- Do **not** RAM-stage STK's assets. Do keep using it for Quake.

## Still open

What the ~30 s actually is. It is not asset reads (proven above), not the shader
disk cache (only 10%), and not steady-state rendering (20.0 vs 21.8 warm). It is
specific to the deferred pipeline and warms up once per boot. Next probe: it is
most likely first-use compilation or GPU-side setup of the deferred pipeline's
shader variants that the disk cache is not capturing — instrument the Mesa cache
hit/miss counts, or time `ShaderFilesManager` per shader.

## The ~30 s is NOT shader compilation either (2026-09-08)

Free probe, no instrumentation: count cache entries either side of one
full-deferred first-run-of-boot.

```
entries BEFORE: 200
sync-netboot-tree.sh: Mesa shader disk cache KEPT — GPU driver unchanged (200 entries)
profile: Number of frames: 1325 time 49.840004
compiles: 50
entries AFTER:  200
```

**Zero new entries while 50 shaders "compiled"** — every one was a disk-cache
*hit*, and the run still took 49.8 s against no-deferred's 22.4 s. (The cache is
demonstrably being written when it needs to be: it grew 134 → 200 during the
preceding no-deferred run.) So the backend compile is worth only the ~5.5 s the
persistent cache already saved, and **~27 s of the deferred-pipeline first-run
cost is something else entirely.**

What the constraints leave. It must be (a) specific to the deferred pipeline —
no-deferred never pays it, both measured as first-run-of-boot; (b) warmed
per-*boot*, not per-process — full-deferred as a second run in the same boot is
20.0 s in a fresh process; and (c) not asset reads, not shader source reads, not
the shader cache, and not steady-state rendering.

That combination rules out anything process-local. The leading hypothesis is the
**kernel's contiguous physical allocator**: every V3D BO is
`mmap(MAP_CONTIGUOUS)`, and the deferred pipeline allocates many more and larger
render targets at 1080p (G-buffer, shadow maps, blur chain). First use has to
find/compact contiguous ranges; a second run reuses ranges just freed and already
coalesced, which is exactly a per-boot warm-up that a fresh process still
benefits from.

To test: time the `mmap` calls in `ioc_create_bo` (the winsys already has a
`V3D_BO_TRACE` env gate at `v3d_phoenix_winsys.c:345`, though psh cannot set
environment variables so it needs a marker-file gate like the frame diagnostic
used), and compare total mmap time and largest-allocation latency between a
first and second run.

**Priority note:** this is now understood well enough to be worth little. It is a
one-time ~27 s wait on the first race after a boot, it is avoidable outright with
`--disable-dynamic-lights --shadows=0`, and it is trivially cut in editing. Not
worth further Pi cycles unless it turns out to affect something else.
