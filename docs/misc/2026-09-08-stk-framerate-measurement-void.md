# STK frame rate: two metrics saturated, and the one number that is real

2026-09-08. Correcting the record on SuperTuxKart's in-race frame rate. **Every
STK fps figure recorded before this note is void**, including "~0.35 fps" and
"~1.0 fps", and the conclusion drawn from them ("not fill-rate bound") does not
follow. The single trustworthy number is **≥5.6 fps at 1080p with a driven
kart**, from STK's own frame counter.

## What was measured, and why each attempt failed

### Attempt 1 — `ffmpeg -vf mpdecimate` unique-frame count: confounded

Counting non-duplicate frames in a 30 fps HDMI capture is a good render-rate
proxy *only when the scene moves*. It does not here: `stk --race-now` gives the
player kart no throttle, so **the kart never leaves the start line**. Frames
differ only in the race-clock digits and a music-note glyph, and mpdecimate
scores those as duplicates. Measured 0.35–0.40 fps; understated the truth ~15x.

### Attempt 2 — race-clock advance rate: saturated

STK caps per-frame `dt` at `MAX_ELAPSED_TIME = 3.0f*1.0f/60.0f*1000.0f` = 50 ms
(`main_loop.cpp:304`), deliberately: *"Don't allow the game to run slower than a
certain amount. when the computer can't keep it up, slow down the shown time
instead"*. That suggests `fps = (game-time rate) / 0.05`, i.e. read the on-screen
race clock at two known wall-clock times. It looked solid — two independent runs
agreed to 1.3%.

It is saturated. The same metric applied to four runs:

| run | clock rate (s/s) | implied fps | true fps |
|---|---|---|---|
| 1080p, 1 kart | 0.05032 | 1.01 | ? |
| 640x480 | 0.04968 | 0.99 | ? |
| 1080p, `--no-sound` | 0.05106 | 1.02 | ? |
| 1080p, `--profile-time=6` | 0.04839 | 0.97 | **5.617** |

The last row is the control: STK's own counter reported 5.617 fps for that run,
so the metric was **5.8x low** — and it read ~1 fps for every run regardless.
It is pinned near 0.05 s/s, so it carries no information about frame rate. The
race clock also *froze outright* between two snapshots 16 s apart
(`075041`/`075057`, both 00:05.925), which no constant-dt model allows: ticks per
frame are clamped somewhere above `main_loop.cpp:488`, discarding accumulated
time, so the displayed clock lags real time by an unbounded amount.

Because both compared runs were pinned, **the 1080p-vs-640x480 comparison is
void** and the fill-rate question is reopened, not answered.

### Attempt 3 — `--profile-time=N`: this one is real

`ProfileWorld` counts frames directly and prints, at `Log::verbose` level (which
this build emits, so it lands on the UART):

```
profile: Number of frames: 1326 time 236.055008, Average FPS: 5.617335
```

Use this for all future STK performance work. Two caveats, both making it a
**lower bound** on in-race fps:

- `m_start_time` is taken in the `ProfileWorld` **constructor**
  (`profile_world.cpp:52`), before track loading, while `m_frame_count` only
  increments in `update()` — i.e. once racing. So track load time is in the
  denominator but its frames are not in the numerator.
- The geometry counters print `0.000000` — the SP renderer does not populate the
  Irrlicht `calls`/`culled`/`drawn_solid` attributes, so this gives no draw-call
  data. `m_num_calls` is accumulated but never printed.

Profile mode also **drives the kart** (`createKart` sets `KT_AI` for every kart,
`profile_world.cpp:117`), so the scene actually moves — which is both a valid
render workload and much better demo footage than a stationary kart.

## What this changes

- STK renders a moving 1080p race at **≥5.6 fps**, not ~1 fps. Marginal, but 16x
  better than the void figure and recognisably a running game.
- **Refuted:** audio is not the cost. Profile mode's only perf-relevant
  difference from normal mode is `m_sfx = m_music = false` (`main.cpp:1782`), so
  audio was the leading hypothesis for the apparent 5.6x gap — but the gap itself
  was a measurement artifact, and `--race-now --no-sound` measured the same
  (saturated) 1.02 as sound-on.
- **Reopened:** whether STK is fill-rate bound. Re-test as `--profile-time` at
  1080p vs 640x480 and compare the printed counters. Now runnable, because the
  launcher override fix (`stk-launcher.c`, this date) makes `--screensize`
  actually take effect.
- **Unknown:** whether normal race mode differs from profile mode at all. No
  trustworthy normal-mode number exists yet. The honest statement is that STK's
  measured rate is ≥5.6 fps and nothing is known about a normal/profile gap.

## Related crash (separate, filed here for the trail)

`stk --profile-time=6` printed its full report and then took a Data Abort while
formatting the **per-kart** statistics table (`profile_world.cpp:~342`):

```
Exception #36: Data Abort (EL0)  esr=0000000092000044  far=000f000e000d001c
pc=000000000091e664  lr=000000000091bd08  sp=0000007ffffff380
in thread 60, process "/usr/bin/supertuxkart" (PID: 25)
```

`esr` decodes as EC=0x24 (data abort, lower EL), WnR=1 (write), DFSC=0x04
(translation fault, level 0). `far` and `x4`/`x5`
(`000b000a00090008`, `000f000e000d001c`) are a 16-bit ascending sequence — index
or attribute data being dereferenced as a pointer. Registers also hold the track
path as packed ASCII (`x7`=`haciend`, `x9`=`stk-asse`). A stack-range line
(`sp=..f380 from=..f300 to=..f480`) printed immediately before, so stack
exhaustion in the report path is the first thing to check. Profile-report path
only — **not** on the demo path, and the `--no-sound` normal-mode run did not
crash.
