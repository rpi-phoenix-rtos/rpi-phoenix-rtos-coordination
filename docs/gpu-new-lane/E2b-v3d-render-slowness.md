# E2b — Why is the V3D render phase slow in SuperTuxKart?

*Pre-registered 2026-09-26, before any E2b Pi run. Follows
[E2](E2-stk-submit-breakdown.md) trial W1 (`artifacts/rpi4b-uart/rpi4b-uart-20260926-192136-e2-prof-W1.log`).
Code: the E2b instrument lives in two uncommitted worktrees (paths under "Build") and as patches in
[`tools/gpu-lane/stkprof/`](../../tools/gpu-lane/stkprof/) (`e2b-winsys.patch`, `e2b-mesa.patch`), plus
`build-stkprof.sh` (extended) and `e2b-summarize.py`. The shipped binaries and every default
build stay byte-identical (proofs below).*

Tags as in the research doc: **[read]** = seen in source/config, **[measured]** = in a log,
**[inferred]** = reasoning, not yet measured.

## The question, and a correction to its premise

E2 W1, gameplay (68 windows, 2545 frames): **7.38 fps, 135.4 ms/frame**, of which the render spin
(CT1 kick → FRDONE) is **91.35 ms**, the bin spin 3.21 ms, CPU outside the winsys 40.4 ms and our
cache/TLB maintenance 0.28 ms, with 8 CL jobs per frame. V3D measured at 500 MHz. **[measured]**
So nearly all of the GPU time is the GPU itself executing render jobs, and our submit path does
nothing during it — the render spin only polls `FRDONE` (winsys `ioc_submit_cl`, render loop).

The brief said STK "does not change with resolution", i.e. is not fill-bound. **That premise is
void, and the evidence against it is already in the tree:**

* The 2026-09-16 note varied `--screensize`, but the render target cannot follow it: the scanout
  FBO is pinned to `/dev/fb0`'s 1920×1080 mode, and the STK memory records that a `--screensize`
  run "rendered at 1920×1080 both times" (`phxgl: scanout FBO(s) 1920x1080` in both logs).
  **[read]** The note itself concedes "a '720p' run is still presenting a 1080p surface".
* The launcher documents the one resolution change that *does* reach the GPU — the deferred
  pipeline's RTT scale — measured on this hardware (`tools/supertuxkart-port/stk-launcher.c:99-118`):
  `scale_rtts_factor` 1.0 → 5–6 fps, 0.75 → 8–9 fps, 0.5 → 10–15 fps. **[measured, HUD fps]**

Fitting frame time against RTT pixels (1/5.5, 1/8.5, 1/12.5 s at 2.07, 1.17, 0.52 Mpx) gives
**≈ 46 ms fixed + ≈ 66 ms per Mpx**. [inferred] The fixed part matches E2's CPU term (40 ms); the
pixel part matches E2's GPU term (91 ms at 1.17 Mpx ≈ 78 ms/Mpx). The fit rests on STK's HUD
counter read by eye (three coarse points), so it is *directionally* decisive — 0.5 is ~2× faster
than 1.0 — not a precise model; the E2b per-slot counters replace it. **The render phase scales with
the pixels of the deferred render targets.** So the right question is not "what fixed overhead
is there" but "why does each RTT pixel cost ~3× what it should" — candidates must be
pixel-proportional: memory bandwidth/latency, overdraw, per-tile load/store traffic, shader cost.

On the "3× slower than Pi OS": the Pi OS figures (29 fps @720p in 2019, 30–110 fps in 2023) are
not like-for-like — STK version, `anisotropic`, dynamic lights and RTT scale are not recorded.
[inferred] If Pi OS ran the same pipeline at 720p scale 1.0 (0.92 Mpx), our per-pixel GPU cost
alone (~78 ms/Mpx) would be ~72 ms/frame, i.e. ≤14 fps, against Pi OS's 30 fps (≤33 ms total). So
if the reference is honest, the GPU per-pixel cost here is **at least ~2.2×** Pi OS's. E2c below
proposes the same-board, same-settings reference that would make this a measurement.

## Candidates, ranked

### H1 — The fabric clock is pinned to half of Pi OS's: `core_freq=250` ★ strongest

* **[read]** `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt:35-36`:
  `force_turbo=1`, `core_freq=250` (same in the staged `.buildroot/_boot/.../rpi4b-bootfs/config.txt`).
  The Pi 4 default is 500 MHz; the Linux netboot reference config on this host
  (`artifacts/linux-netboot/tftp/config.txt`) sets no `core_freq`, i.e. runs the default.
* **[measured]** The firmware confirms it: every Bluetooth run prints
  `core_clk=250000000 Hz` (`artifacts/rpi4b-uart/rpi4b-uart-20260810-073804-btdaemon.log`,
  `…-20260809-213433-btminiuart.log`; `tools/bt-probe/bt-probe.c:343` asks `GET_CLOCK_RATE(4)`).
* **Origin**: `manifests/2026-04-17-pi4-uart-clock-restoration.md` — "temporary Pi 4 clock pinning
  for UART stability", added in April before the PL011 console got `init_uart_clock=48000000`
  (which makes the console independent of the core clock). The upstream-readiness punch list
  already flagged it as an "undocumented magic knob" (`docs/done/2026-06-27-upstream-readiness-punchlist.md:284`).
  Nobody re-checked it for the GPU: the research doc's "clocks match" (§1.3) compared **only the
  V3D clock (id 5)**.
* **Why it would matter** [inferred]: the V3D core runs at 500 MHz on its own clock, but its memory
  traffic (TMU texture fetches, TLB tile loads/stores, tile-list reads) leaves the core over AXI
  into the VideoCore fabric, which the `core` clock drives; the HVS scanning out 1080p60 shares
  it. Halving that clock halves fabric bandwidth and doubles fabric latency in V3D cycles.
  **This is the one link I cannot show from source** — nothing in the Linux v3d/vc4 drivers
  names the fabric clock for V3D (`bcm2711` DT gives V3D only firmware clock 5). The measurement
  decides.
* **Pixel-proportional**: yes (every RTT pixel is texture reads + tile store/load traffic).
  **Phoenix-specific**: yes. **Size**: up to ~2× on the memory-bound share.
* **Recommended first Pi run — no instrument needed** (a ≥2× hypothesis grades on fps alone):
  a config.txt A/B with the **shipped** `stk` (flipstat gameplay fps, same track/laps, one boot
  per arm): `core_freq=250` vs the line removed / `core_freq=500`, `force_turbo=1` kept in both.
  The runtime knob `V3D_PHX_CORE_HZ` exists in the clone but will very likely be clamped: with
  `force_turbo=1` the firmware's maximum for CORE is `core_freq` itself (`pctr-clk … max=`
  shows it within seconds). E2b-base then explains whatever the A/B shows.
* **Side risks if changed** [read]: the mini-UART (Bluetooth) baud follows the core clock
  (`bt-probe` reads it live, so it self-adjusts); `wifi/rpi4-wifi/rpi4-wifi.c:284` hard-codes a
  250 MHz SDHCI base clock — whether that clock is the core clock or the separate EMMC clock must
  be checked before a config change ships (WiFi is not needed for the E2b runs). PL011 console:
  unaffected (`init_uart_clock`). Watch, not a known dependency: genet / the NFS root, audio
  PWM, and any driver that assumed a 250 MHz core clock (Pi OS changes the core clock at runtime
  routinely, so the hardware copes; Phoenix's drivers were never exercised that way).

### H2 — Early-Z is forced off for every job ★ strong, Phoenix-specific

* **[read]** `external/mesa/src/gallium/drivers/v3d/v3dx_draw.c:932-933` sets
  `job->first_ez_state = job->ez_state = V3D_EZ_DISABLED` unconditionally at the top of
  `v3d_update_job_ez` (commit `2728620c216`, 2026-07-16, "force EZ off on Phoenix 26.2 — avoids
  constant depth-drain render wedge (5fps->38fps)"). The comment further down
  (`v3dx_draw.c:1027-1036`, "EZ is ENABLED on this port") is stale.
* The wedge it avoided was diagnosed in the GLQuake render-to-scanout era; the SDL2 glue now
  attributes the depth-output-FIFO stall to GPU/display contention on the scanned-out buffer,
  cured by triple buffering (`sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c:68-78`).
  [inferred] The reason for the force-off may no longer exist; nobody has re-tested.
* STK's SP renderer draws the whole scene once into an MRT G-buffer with `GL_LEQUAL` depth
  (`stk-code-1.4/src/graphics/shader_based_renderer.cpp:252-266`, `FBO_SP`, 2 colour targets +
  depth-stencil). Without early-Z, **every fragment of every occluded triangle runs the full
  fragment shader** (textured, anisotropic ×4 by default) and is only rejected at the TLB. With a
  track's depth complexity of 2–4, that is most of the geometry pass. Pi OS has EZ on (upstream
  disables it only for odd frame sizes, `v3dx_draw.c:980`; 1440×810 is even).
* **Pixel-proportional**: yes. **Phoenix-specific**: yes. **Size**: the late-Z-rejected share of
  the geometry pass's fragment work.

### H3 — Per-tile load/store traffic the application did not ask for (medium, unknown)

* **[read]** The scanout FBOs carry one shared `GL_DEPTH_COMPONENT24` renderbuffer
  (`sdl_phoenix_glctx.c:259-280`). On a real window system, Mesa's DRI path invalidates the
  depth/stencil after `SwapBuffers` [inferred — from memory of the DRI2/DRI3 flush path, not
  re-read this session], so the next frame never stores (or loads) it; here nothing does, and the port's `PHOENIX_glInvalidateFramebuffer` wrapper deliberately *drops* invalidates
  while a scanout FBO is bound (`project_gles_fb0_redirect_traps`). [inferred] The final 1080p job
  may store ~8.4 MB of depth per frame it never reads; any RTT that is drawn without a clear gets
  a full TLB load. STK never calls `glInvalidateFramebuffer` itself.
* Unknown from reading: which of the 8 jobs load/store what. The Mesa job note answers it.

### H4 — QPU reservation `MISCCFG.QRMAXCNT = 2` (low–medium)

* **[read]** The winsys writes `MISCCFG = (2 << 1) | OVRTMUOUT` at every core init
  (`v3d_phoenix_winsys.c:215` `V3D_QRMAXCNT (2)`, `:2616`). **[measured]** Cold value `0x6`
  (QRMAXCNT=3, OVRTMUOUT=0) in the W1 log (`v3d-coldstate: … cold_MISCCFG=0x00000006`).
  Linux never writes MISCCFG on V3D 4.2 (`external/linux/drivers/gpu/drm/v3d/v3d_gem.c:29-30`,
  only `ver < 41`).
* The field's meaning is undocumented; the winsys comment ("bin-vs-render QPU split") is a prior
  agent's inference, tuned to zero wedges, never measured for throughput. If it limits the QPUs
  fragment shading may use, the counters show high QPU idle during render.

### H5 — The GPU is not actually running for the whole render spin (check, not a suspect)

Clock gating, a throttle or an external stall would show as **counter cycles ≪ 500 MHz × spin
wall**. The instrument's cycle counter makes this a built-in validity check (V0).

### H6 — The workload itself (not a port defect, but a lever)

Same Mesa 26.2 compiler as Pi OS, so shader code, register allocation and QPU thread counts are
identical for the same shaders. STK defaults (`user_config.hpp`): `anisotropic=4` (:965),
`enable_dynamic_lights=true` (:689), `light_scatter=true` (:986). These cost the same on Pi OS;
they explain part of a gap only if the Pi OS reference used lower settings. STK has CLI switches
for both (`--anisotropic=N`, `--disable-dynamic-lights`, `main.cpp:920-988`).

### Refuted or low, from source

| # | Candidate | Verdict | Evidence |
|---|---|---|---|
| R1 | Texture / RT tiling forced linear | **refuted for STK** (census confirms) | The RASTER gate (`v3d_resource.c:933-936`) needs `RENDER_TARGET && ≥1024×768 && (peek_next_scanout() || !SAMPLER_VIEW)`. STK's RTTs are textures → carry `SAMPLER_VIEW` → stay UIF; `next_scanout` is a one-shot consumed by the glue's 3 scanout FBOs. Only the scanout RT is RASTER — as on Pi OS (the HVS cannot scan UIF). NPOT mip decline (`v3d_blit.c`) only affects load-time mipmap generation. Side note: `v3d_resource.c:141-143` flags *every* RT ≥1024×768 `V3D_CREATE_BO_SCANOUT` regardless of `SAMPLER_VIEW`, so STK's RTTs bypass the BO cache (an allocation cost, not a render cost). |
| R2 | Debug/unoptimised Mesa | refuted | `/tmp/mesa-v3d-build`: buildtype release, `-O3 -DNDEBUG -DMESA_DEBUG=0` on all 907 entries; `transform()` keeps them. |
| R3 | Wrong device info → wrong compiler decisions | refuted (live check added) | `ioc_get_param` constants: `CORE0_IDENT1=0x81001422` → 2 slices × 4 QPUs, VPM 64 KiB, V3D 4.2 (`v3d_device_info.c:66-74`). E2b prints the live registers next to the constants. |
| R4 | Per-job cache maintenance | low | Linux also flushes L2T + invalidates slices before **both** the bin and the render job (`v3d_sched.c:238,292`, `v3d_gem.c:250-262`); ours are waited (0.28 ms/frame total, E2). |
| R5 | GPU MMU | low | 4 KiB PTEs with `PTE_W|PTE_V` only (winsys `:1740`, `:1805`); Linux uses the same format and adds super/big pages opportunistically (`v3d_mmu.c:27-30,125-129`), worth +1.4 % avg / +8 % best (Igalia). The per-submit TLB clear makes every job start with a cold TLB — small and not counter-visible. |
| R6 | Uncached BO mappings | not a GPU cost | The GPU ignores ARM page attributes; uncached BOs cost CPU time (E2's C term). |
| R7 | Something serialising the render phase | refuted | Between the CT1 kick and `FRDONE` the winsys only polls (plus a QPU-int ack every 1 M spins); CT1 is kicked after the waited bin→render L2T flush, as Linux's render job also starts behind an invalidate. |

## The instrument (built, default-off, clone-only)

Everything is compiled only with `-DV3D_PHX_SUBMIT_PROFILE` (winsys, the E2 macro) and
`-DV3D_PHX_JOB_NOTE -DV3D_PHX_RES_CENSUS -DV3D_PHX_EZ_KNOB` (three Mesa files), into a cloned binary
`supertuxkart-e2b` + launcher `stk-e2b` — never the shipped name, never the E2 `-prof` clone.

**1. V3D performance counters per CL job (winsys).** Register layout and source IDs are hardware
facts cross-checked against Linux `v3d_regs.h:348-380` / `v3d_perfmon.c` and Mesa
`v3d_performance_counters.h` (the two v4.2 tables agree entry for entry, 87 counters):
`PCTR_0_EN 0x650`, `CLR 0x654`, `OVERFLOW 0x658`, `SRC_x 0x660+4x` (four 7-bit IDs each),
`PCTRi 0x680+4i`. Per job: program + clear before the CT0 kick, read 32 slots at `FLDONE` (bin),
clear before the CT1 kick, read at `FRDONE` (render). Sums per window and per **slot** (the job's
index since the last flip; STK's frame is a fixed sequence of 8 jobs). Reads are timed into a new
CL phase `pct` so E2's `sum == ioctl` identity still holds (`pct` is inside `ioc` but in none of
e2-summarize's G/M/C/B/P/R buckets, so on an E2b log those shares sum to 100 % minus `pct`,
~0.03 ms/frame). Every counter line carries `ck=` (FNV-1a over the line text): the UART corrupts
~1.3 % of lines and these are 3–4× longer than most, so a flipped digit would otherwise pass a
format check straight into V0 and every ratio; `e2b-summarize.py` drops and counts mismatches. Re-programmed every job, so the wedge
path's core reset cannot silently zero the configuration.

| set | the 32 counters (Linux/Mesa v4.2 IDs) |
|---|---|
| **A** — where the cycles go | cycle-count(32); QPU idle(13), active vertex(14), active fragment(15), valid-instr(16), waiting TMU(17), scoreboard(18), varyings(19), stalled vertex(33), stalled fragment(34); I-cache hit/miss(20,21), uniform-cache hit/miss(22,23); TMU quads(24), TMU misses(25), MRU hits(85), config accesses(49), TMU active(53), TMU stalled(54); CLE bin/render active(28,29); L2T hit/miss(30,31); VDW/VCD stalls(26,27); FEP prims no-pixels/pixels(0,1), **early-Z clipped quads(2)**, valid quads(3); **TLB quads failing Z(5)**, passing Z(6) |
| **B** — the memory system | cycle-count(32); TLB quads written/non-zero/zero/partial(9,8,7,48); PTB prims binned(35); memory writes core/L2T/PTB/TLB(71,72,73,74), reads core/L2T/PTB/TLB/PSE/GMP(75,76,77,79,78,80); words PTB-wr/TLB-wr/PSE-rd/TLB-rd(81,82,83,84); L2T no-ID / cmd-queue stalls(50,51); L2T reads and misses by client: TMU(56,64), CLE(57,65), VCD(58,66), TMU-cfg(59), SLC0(60,68); L2T TMU writes(52) |

`export V3D_PCTR=AB` alternates the sets per 5 s window, so one run yields both.

**2. Clocks.** Once (`pctr-clk`): measured/configured/max/min CORE clock, V3D measured/max, ARM
measured, via the serialized `/dev/vcmbox`. Per window (`pctr-w`): ARM/CORE/V3D measured.

**3. Mesa job note** (`v3d_job.c`, weak call right before `SUBMIT_CL`): draw size, tile grid and
tile size, MRT count, internal bpp, draw calls, MSAA, double-buffer, clear/load/store/invalidate
masks, EZ state, colour/depth tiling and cpp, scanout-BO flag. Kept per slot.

**4. Resource census** (`v3d_resource.c`): every created resource counted by class (buffer /
texture / RT / depth) × level-0 tiling; each RT, depth buffer and ≥1024-texture printed at
creation (load time only, no per-frame UART). ⚠ `scanout_bo=1` there (and the job note's
`/scanout`) is Mesa's `bo->scanout`, which `v3d_resource_bo_alloc` sets for *every* RT
≥1024×768 (`v3d_resource.c:141-143`): on STK's 1440×810 RTTs it means "BO cache bypassed", **not**
"aliases the framebuffer". Which BOs really alias the three firmware buffers is the winsys's
`RT scanout buf%d PA … gpuva …` lines at start-up (exactly three).

**5. A/B knobs, all default-off even in the clone:**
`V3D_PHX_EZ=1` (Mesa: let upstream's EZ state machine run), `V3D_PHX_CORE_HZ=<Hz>` (winsys: one
`SET_CLOCK_RATE(CORE)` before the first job, before/after/max printed — no config.txt change),
`V3D_PHX_QRMAXCNT=0..7` (winsys: MISCCFG override at every core init). STK's own
`--anisotropic=N` / `--disable-dynamic-lights` are the workload knobs.

**Byte-identity (checked by `build-stkprof.sh` on every run, all passed 2026-09-26):**
PROOF 1 worktree winsys without macro == pristine (merge-base) compile, byte-identical; PROOF 1b
== the member in the shipped `libv3d-phoenix.a`; PROOF M1 each worktree Mesa file without macros
== pristine `external/mesa` compile; PROOF M1b == the shipped archive member (all three
byte-identical); PROOF 2 control relink == shipped `prog/supertuxkart`; guarded shared files
unchanged. The plain E2 build (`build-stkprof.sh --verify-only`, no env) still passes unchanged.

## Pre-registered measurement plan

**Binary**: `stk-e2b` → `/usr/bin/supertuxkart-e2b`. Cache state: warm (cycle log says
`Mesa shader disk cache KEPT`). Command, every trial:
`stk-e2b --track=hacienda --numkarts=4 --profile-laps=2 [arm args]`, one run per boot, the arm
set with psh `export` lines before it and **asserted from the log** (`pctr-clk`, `V3D_PHX_EZ knob:`,
`pctr QRMAXCNT override`, `stk-e2b: DATADIR=`), never from the command sent.

| arm | env / args | trials | question |
|---|---|---|---|
| **core-cfg** (first) | shipped `stk`, config.txt `core_freq=250` vs 500 (coordinator) | 1 + 1 | H1 on fps alone (flipstat); cheapest decisive test |
| **base** | `export V3D_PCTR=AB` | 2 | the counter split (verdicts below); per-slot attribution; census |
| **core** | base + `export V3D_PHX_CORE_HZ=500000000`, or base on the `core_freq=500` boot config | 2 | H1 with counters (what changed: TMU waits, L2T stalls). If `pctr-clk … meas now` stays 250 MHz the firmware clamped the runtime request → use the config route |
| **ez** | base + `export V3D_PHX_EZ=1` | 2 | H2, graded with wedges (`TIMEOUT`, `GPU wedged`) |
| **qr** | base + `export V3D_PHX_QRMAXCNT=3` | 1 | H4 (the cold/firmware value) |
| aniso | base + `--anisotropic=0` | 1 | H6 lever (workload, not the port) |
| lights | base + `--disable-dynamic-lights` | 1 | H6; roughly Pi OS's "low" preset |

Order: core-cfg → base → core → ez → qr → aniso → lights; if base's verdict already names one hypothesis,
run its arm first. A later arm may be combined (core+ez) only after both single arms are graded.

**Windows**: identical to E2 (`fps > 3`, contiguous race run, first/last dropped; windows with a
wedge, spin-cap exit, TIMEOUT line or a failed job excluded). ≥ 10 gameplay windows per set.

**Validity gates (a trial failing any is void):**
1. **V0**: set-A cycle-count / render-spin wall = 480–520 MHz (else read H5 first — the GPU is
   not running the whole spin).
2. E2's gates: CL phase-sum/ioctl 0.98–1.00, counter/`cg` wall 0.99–1.01, R < 2 % of the frame;
   `pct` (counter reads) < 1 % of the frame.
3. No latched counter overflow (`pctr-w ovf=0x00000000`).
4. base fps within 5 % of E2 W1's 7.38 (else the instrument perturbs the frame).
5. The E2b binary is a different layout: never count its runs, or any C1 signature in them, in C1
   statistics.

**Numbers** (`e2b-summarize.py`), gameplay aggregate and per slot, render phase:
QPU idle / fragment-active / valid-instruction / waiting-TMU shares of 8 × cycles; TMU miss rate
and stalled/active; L2T hit rate; **late-Z reject share** = TLB-quads-failing-Z / (failing +
passing); early-Z clip share = FEP-clipped / FEP-valid quads; quads per pixel per slot (overdraw);
set B memory transactions/words per unit and per pixel, L2T stall shares; measured clocks.

**Pre-registered verdicts** (base arm, set A, render phase; thresholds fixed now):

| pattern | reading | next |
|---|---|---|
| clk ∉ 480–520 MHz | GPU not busy/clocked the whole spin | H5: clock/throttle/external stall; nothing else is interpretable |
| late-Z reject ≥ 30 % **and** EZ-clip < 2 % | overdraw shaded in full (H2) | **ez** arm; predicted saving ≈ that share of the geometry slot's fragment work |
| QPU waiting-TMU ≥ 25 % of QPU-cycles **or** TMU stalled ≥ 50 % of TMU-active | memory latency/bandwidth bound (H1) | **core** arm, then aniso |
| valid-instruction ≥ 60 % | shader ALU bound | workload (H6): the port is not the gap; settle with E2c |
| QPU idle ≥ 50 % | QPUs starved: fixed-function (TLB load/store, tile lists, CLE) or reservation | set B words-per-pixel vs the Mesa note's load/store masks (H3); **qr** arm (H4) |
| none | — | read the components; no verdict |

More than one pattern may fire; each fires its arm.

**Grading an arm**: an arm "moves" the render phase if its trial-mean render ms/frame differs
from the base trials' mean by > 10 % **and** by more than the spread between the two base trials
(W1's per-window render already ranges 80–134 ms with the scene, so only trial means over the
same track and lap count are compared). fps is reported beside it (flipstat, gameplay windows).

**What each arm outcome means for the lane:**
* **core moves ≥ 25 %** → H1 confirmed: one config.txt line (plus the WiFi/BT clock checks) is the
  biggest STK win available, and it lifts every GPU app — re-baseline the whole showcase gate.
  core moves < 10 % → the fabric clock is not the limiter; drop H1.
* **ez moves** with 0 wedges → re-enable EZ upstream-style (a Mesa change through the normal gate,
  all five games + X); ez moves **with** wedges → the EZTEST stall is still real: root-cause it
  (the counters now show where) before shipping. ez does not move → overdraw is not the cost.
* **qr moves** → the QRMAXCNT tuning traded throughput for wedge margin; needs its own wedge study.
* **aniso / lights move, the port knobs don't** → the gap is the workload; E2c decides whether Pi
  OS really does better on the same settings.

### E2c (optional, coordinator) — the same-board reference

The host already has a Linux netboot lane (`artifacts/linux-netboot/`, stock `vc4-kms-v3d`,
default clocks). Running the same STK 1.4 build with the same arguments
(`--screensize=1920x1080 --track=hacienda --numkarts=4 --profile-laps=2`, `scale_rtts_factor=0.75`,
same anisotropic/dynamic-light settings) there, with Mesa's perfmon (`GL_AMD_performance_monitor`
exposes the same v4.2 counters through the kernel's perfmon), would turn "3× slower than Pi OS"
from a citation into a like-for-like number and give the counter values Phoenix should reach.

## Build, stage, run, read

**Build** (writes only under `artifacts/stkprof-e2b/`; safe while a bench runs):

```
STKPROF_OUT=artifacts/stkprof-e2b STKPROF_NAME=e2b \
STKPROF_DEVICES=/home/houp/.claude/jobs/c8f1289c/tmp/wt-devices-e2b \
STKPROF_MESA=/home/houp/.claude/jobs/c8f1289c/tmp/wt-mesa-e2b \
  tools/gpu-lane/stkprof/build-stkprof.sh
```

**PROOF 2 must pass before any fps A/B against the shipped `stk`**: at 20:04 it warned, because
another build had just replaced `libphoenix.a`, `libv3d-phoenix.a` and `libGL-phoenix.a` while the
shipped `prog/supertuxkart` (18:50) was not yet relinked (PROOFs 1/1b/M1/M1b stayed
byte-identical — the replaced members are unchanged). Re-run the build after the STK port is
relinked against the new libraries.

Worktrees (uncommitted, per PLAN rule 5): `phoenix-rtos-devices` branch `gpu-lane/e2b-pctr` at
`/home/houp/.claude/jobs/c8f1289c/tmp/wt-devices-e2b` (base `0425f93`), `external/mesa` branch
`phx/e2b-instr` at `/home/houp/.claude/jobs/c8f1289c/tmp/wt-mesa-e2b` (base `51c5ee977ba`). To
recreate them elsewhere: `git worktree add -b <branch> <path> <base>` and `git apply` the two
patches in `tools/gpu-lane/stkprof/`.

**Stage** (coordinator; `$export_dir` = the live `fsid=0` export, as in the stkprof README):
`install -m 755 artifacts/stkprof-e2b/supertuxkart-e2b.stripped "$export_dir/usr/bin/supertuxkart-e2b"`,
`install -m 755 artifacts/stkprof-e2b/stk-e2b "$export_dir/bin/stk-e2b"`. Not in `tools/.gpu-libs`,
so the shader cache is not invalidated.

**Run** (one arm per boot), e.g. the base arm:
`./scripts/test-cycle-psh-interact.sh --label e2b-base1 --idle-secs 60 --max-cmd-secs 420 -- 'export V3D_PCTR=AB' 'stk-e2b --track=hacienda --numkarts=4 --profile-laps=2'`
with a Bash `timeout` of 600000 (check the script's multi-command syntax; psh has `export`).

**Read**: `python3 tools/gpu-lane/stkprof/e2b-summarize.py <log>` (counters, clocks, census,
per-slot table, verdicts) and `python3 tools/gpu-lane/stkprof/e2-summarize.py <log>` (the E2
frame split; its arm line will say "not asserted" because the launcher is `stk-e2b` — expected).

New log lines: `pctr-ident` (once, live IDENT/UIFCFG/MISCCFG vs the constants), `pctr-clk`
(once), `pctr-w` / `pctr-b` / `pctr-r` (per window), `pctr-j` + `pctr-jsK` (per slot, every 4th
window; `V3D_PCTR_SLOTS_EVERY=N`) — all five end in `ck=<fnv1a32>`, `v3d-job-note:` / `V3D_PHX_EZ knob:` (once), `v3d-res-census:`.

## Result

*(empty until the first trials: per-arm tables from `e2b-summarize.py`, verdicts, decision.)*

## Result — step 1: core clock A/B (queue5, build 9, shipped `stk`, warm cache, interleaved)

| trial | `core_freq` | gameplay fps (flipstat mean, windows) | exceptions |
|---|---|---|---|
| T1 | 250 | 7.30 (69) | 0 |
| T2 | 500 | 8.39 (72) | 0 |
| T3 | 250 | 7.56 (72) | 0 |
| T4 | 500 | 8.39 (72) | 0 |

**500 MHz core is +13 % (7.43 → 8.39 fps), reproducible.** A real gain for every GPU app — but not
the 3× gap: the core/bus clock is a contributor, not the main cause. Next: the counter run (E2b base)
and early-Z. Adopting `core_freq=500` needs the core-clock dependents checked first
(`rpi4-wifi.c:284` hard-codes 250 MHz; the Bluetooth mini-UART's baud divisor follows the core
clock; the April UART reason for pinning 250 is obsolete for the PL011 console).

## Result — base arm, trial 1 (`e2b-base1`, build 9, core 250 MHz, V3D 500 MHz)

Render 91.2 ms/frame (set A) / 93.6 (set B), bin 3.3 ms; counter reads cost 0.06 ms/frame.
- **Shader cores are not the limit:** QPU idle 3.3 %, fragment-active 15.6 %, valid instructions 20.3 %,
  QPU waiting on TMU 0.1 % — the QPUs are busy but not executing much, i.e. they are fed slowly.
- **H2 overdraw — confirmed as a factor:** 35 % of fragment quads reach the tile buffer and fail Z there
  (`late_z_reject`), 0 % are early-Z clipped (EZ forced off). → `ez` arm (queued).
- **Memory / texture path — the strongest signal:** TMU stalled **79 %** of its active cycles; **L2T hit
  rate 6.7 %** (render: 3.58 M L2T misses vs 0.26 M hits per frame; TMU reads 3.44 M misses/frame, CLE reads
  98 % misses). Texture data is effectively refetched from DRAM every job. Candidates: the per-job L2T
  flush/clean sequence (E2 steps: waited L2T flush before bin, again before render, fix-A, post-job clean —
  8 jobs/frame ⇒ the cache is emptied 16–24× per frame), or an L2T/GMP configuration that disables caching
  for TMU traffic. → new hypothesis **H7 (L2T effectively disabled/flushed)**, test: drop the pre-render L2T
  flush and fix-A in the clone (knobs exist in the async server; for the old winsys a profile-macro knob) and
  re-read `l2t_hit%`; also read L2TCACTL/GMP config at runtime.
- Per slot: the 1440×810 lit pass (s1, 36 % of render, 32.7 ms/job, only 3 draws but 2.40 quads/px, MRT ×2)
  and s5 (24.6 %, 3.12 quads/px) dominate; the final 1920×1080 composite (s7) is a RASTER scanout target as
  on Pi OS.

## Review of base1 by the E2b agent (2026-09-26, 23:40) — two corrections, and H7

Re-read of `rpi4b-uart-20260926-230207-e2b-base1.log` with the fixed `e2b-summarize.py`.

### Correction 1 — QPU shares were normalised wrongly (instrument bug, mine)

The QPU-state counters (idle / active / stalled / valid-instruction) have a capacity of **2 per core
cycle, not 8** (8 QPUs × one count per 4-clock instruction slot, or equivalently per slice). Proof
from the run itself: idle + active(vertex, fragment) + stalled(vertex, fragment) sums to **0.998–1.000
× (2 × cycle-count)** in slots s1, s3, s4, s5, s6, s7 and 0.944 over the whole render phase (per
window 0.932–0.967), against 0.24 × (8 × cycles). The summarizer now normalises by 2 and prints the
partition as check **V0b** on every run. Corrected render phase: **QPU idle 13 %, fragment active
(issuing) 62 %, fragment stalled 18 %**, TMU-only waits 0.3 % (not "idle 3 %, valid 20 %"). Note that
ID 16 "valid instructions" is **active + stalled** ("work resident"), not issue: render 81.2 % =
62.4 + 17.9 + 0.9 vertex; s1 96.4 % = 83.7 + 12.7 exactly. Per slot, issuing: s1 (lit pass, 36 % of
render) **84 %**, s2 83 %, s6 65 %, s5 44 % (+ 32 % stalled, 22 % idle), s3/s4 (half-res blurs)
54–61 % with 35–44 % idle. The 18 % fragment stalls are not TMU-only, scoreboard or varyings stalls
(all ≈ 0) — their cause is uncategorised by these counters. Caveat: s0 and s2 (partition 0.61 /
0.66) and the bin phase (0.40) leave 35–60 % of QPU capacity in no counted state, so for them the
shares are lower bounds, not a split. This is a correction of a normalisation error, not a moved
threshold; with it the pre-registered **H6 (shader-bound, valid-instr ≥ 60 %) fires** — the render
phase is dominated by fragment-shader execution (the lit pass above all), next to H2 (35 % late-Z
rejects: shaded work thrown away) and the H1 TMU pattern (TMU stalled 79 % of its active cycles —
but the QPUs rarely stall on the TMU alone, so the TMU is not what holds the QPUs back).

### Correction 2 — the L2T hit/miss counters are not trustworthy on V3D 4.2

All 64 source IDs of sets A and B match Mesa's and Linux's v4.2 tables entry for entry (the two
tables agree on all 87 entries; `v3d_performance_counters.h:133-218`, `v3d_perfmon.c` v42 table) — so
the instrument programs exactly what the published tables name. But the L2T rows contradict their
labels, within one run:

| observation (per frame, render phase) | why it cannot be what the label says |
|---|---|
| ID 56 "L2T-TMU-reads" = **0**, ID 64 "L2T-TMU-read-miss" = **3.44 M** | misses > accesses |
| ID 57 "L2T-CLE-reads" = **258 914** = ID 76 "L2T-memory-reads" = **258 914** (same windows) | two IDs, one signal |
| ID 30 "L2T-total-cache-hit" = 258 196 (set A) ≈ ID 76 memory reads = 258 914 (set B) | "hits" track memory fills, i.e. behave like misses |
| ID 31 "misses" = 3.58 M ≈ Σ per-client "miss" IDs 64+65+66+68 = 3.89 M (other windows) | "misses" track total requests |

Mesa's V3D 7.1 table (same file, :82-87) names the per-client counters "read hits"/"read misses",
which hints the 4.2 "reads" labels are really something else [inferred]. If 30/31 are swapped the
L2T hit rate is ~93 %, not 6.7 %. The summarizer now prints these contradictions as
`L2T LABEL INCONSISTENT` and labels the ratios `(label?)`. **Do not use an L2T hit rate from these
IDs.**

What *is* trustworthy is the DRAM accounting, because it can be calibrated. TLB store transactions
(225 000/frame) against the bytes the Mesa job notes require (Σ W×H×cpp of the stored buffers of the
8 jobs = 57.28 MB) → **255 B/transaction** (loads: 254 B; TLB words/transaction = 16.0 → 16 × 16 B).
In 256-B transactions: DRAM reads **84 MB/frame** (L2T 66.3, TLB loads 9.4, unattributed 8.4), writes
**57.6 MB/frame**. What is measured vs inferred: the 256-B unit is **calibrated** for TLB stores and
loads only. Applying it to L2T and core reads is **inferred** from the additive identity
core_rd ≈ l2t_rd + tlb_rd + pse_rd + … (329 k ≈ 296 k + 33 k unattributed, all in one unit) and from a
lower bound: if an L2T memory read were a 64-B line, L2T reads would be 16.6 MB/frame — below the
≈ 58 MB the render targets alone must supply — so the L2T transaction must average ≳ 220 B. So the render phase moves 142 MB in 91 ms = **1.55 GB/s** — far below what the
LPDDR4 delivers — and the 66 MB of L2T memory reads are about the pipeline's *compulsory* texture
traffic [inferred, hand estimate from the job notes: the RTT samplings alone are ≈ 58 MB/frame — s1 reads normal+depth 9.3 MB, s2 depth
4.7, s3/s4 2.3 each, s5 diffuse/specular/half/normal/colour/depth ≈ 26, s6 9.3, s7 4.7 — plus the scene
textures of s0]. **Re-fetching caused by L2T flushes can therefore be at most ~10 MB/frame (~15 % of
L2T reads)**, and less of the render time, since the phase is not bandwidth-bound.

### H7 — per-job L2T maintenance vs Linux (facts)

| step | Linux (`drivers/gpu/drm/v3d`) | old winsys (`v3d_phoenix_winsys.c`) | async server (`v3da_jobs.c`) |
|---|---|---|---|
| MMU TLB + PTE cache | only when PTEs change (`v3d_mmu.c:146,159`, insert/remove) | every CL, TFU and CSD job (`mmu_flush_tlb`) | every job; `V3DA_KNOB_TLB_ON_CHANGE` (bit 0) |
| before CT0 | `v3d_bin_job_run` → `v3d_invalidate_caches` (`v3d_sched.c:238`, `v3d_gem.c:250-261`): **L2T FLUSH (FLM=0) issued, not waited** ("L2T accesses will be stalled until the flush has completed", `v3d_gem.c:177-190`), then slice invalidate | SLCACTL first (#67 ordering), L2T FLUSH **waited**, then **fix-A**: a second waited FLUSH | same; bits 1 (no wait-new), 2 (no fix-A); **new bit 7 LINUX_ORDER** (L2T then slices) |
| before CT1 | `v3d_render_job_run` → `v3d_invalidate_caches` again (`v3d_sched.c:292`): FLUSH not waited + slices | waited FLUSH + SLCACTL | same; bit 3 (no wait); **new bit 6 NO_HANDOFF_FLUSH** (diagnostic) |
| after FRDONE | nothing; a CLEAN only through a CACHE_CLEAN job when the submit asks (`v3d_sched.c:695-702`) | L2T **CLEAN** (FLM=2) issued, not waited | same; **new bit 5 NO_POST_CLEAN** |
| flush mode / range | FLM_FLUSH=0; `L2TFLSTA=0`, `L2TFLEND=~0` (`v3d_gem.c:35-36`) | identical (`L2TCACTL_L2TFLS`; `apply_core_regs`) | identical (`v3da_hw.c:261-262`) |
| L2C (`L2CACTL`) | never written on ver ≥ 33 (`v3d_gem.c:165-168`) | `L2CCLR|L2CENA` at every core init | same (`v3da_hw.c:260`) |
| MISCCFG | not written on 4.2 | QRMAXCNT=2 \| OVRTMUOUT | same (`v3da_hw.c:264`) |
| AXICFG / GMP | MAX_LEN after a bridge reset (GFXH-1383); GMP only STOP_REQ while resetting (`v3d_gem.c:42,80`) | same | same |

So **the L2T is emptied at exactly the same two points per job on Linux** (clean + invalidate before
the bin job and before the render job): a Linux render job also starts with a cold L2T. What we do
*more* is: waits (Linux relies on the hardware interlock), fix-A (a redundant second flush), a CLEAN
after every render, a TLB flush on every job, and the `L2CACTL` write. None of these changes what the
L2T holds when the render job starts, except the TLB/PTE-cache flush (cold MMU TLB). **Nothing in the
configuration can make TMU reads uncacheable**: the V3D PTE format has no cache attribute
(`v3d_mmu.c:27-30`: superpage, bigpage, writeable, valid + PFN; ours `PTE_W|PTE_V`), GMP is
protection (never enabled here or in Linux), and Linux's register map has no L2T enable bit
(`L2TCACTL` = L2TFLS, FLM, TMUWCF only). The one register we write that Linux does not is `L2CACTL`
(V3D 3.2's cache controller; its effect on 4.2 is unknown) — hence the `l2c` arm.

**Prediction [inferred, pre-registered]:** H7 is weak. With both the DRAM accounting (≈ compulsory)
and the Linux comparison (same flush points), the "linux" arm should move render time ≤ 5 % and L2T
memory reads ≤ 10 %. Its possible CPU-side gain (no waits, no fix-A, fewer TLB flushes) shows in the
submit phases, not the render spin.

### H7 instrument (built, default-off)

**Old-lane clone `stk-e2bh7`** (`artifacts/stkprof-e2b-h7/`, new name — the staged `stk-e2b` and
`artifacts/stkprof-e2b/` are untouched). Same E2b worktree, profile macro only, all proofs
byte-identical (PROOF 1/1b/M1/M1b, PROOF 2 passed at 23:24). Env knobs:
* `V3D_PHX_L2T=linux` — per CL job exactly Linux's sequence: TLB flush only after a CREATE_BO/GEM_CLOSE
  since the last flush; before CT0 an unwaited L2T FLUSH then the slice invalidate; no fix-A; before
  CT1 an unwaited FLUSH + slices; no post-render CLEAN. The GFXH-1897 wait-old before each
  `L2TCACTL` write is kept (it normally finds the unit idle).
* `V3D_PHX_L2T=nohand` — `linux` minus the pre-CT1 L2T flush (diagnostic: CT1 may read stale L2T lines
  of tile lists; expect wedges if the flush is load-bearing).
* `V3D_PHX_NO_L2C=1` — do not write `L2CACTL` at core init (Linux never does on 4.2).
* `V3D_PHX_PXLOG=N` — every Nth flip hash a 16×16 grid of the displayed buffer; per window `h7`
  line: `tlb_flush= tlb_skip= px_n= px_same= px_black=` (frozen / black frame guard).
* Once: `h7 l2t-mode=… L2TCACTL= L2TFLSTA= L2TFLEND= L2CACTL= SLCACTL= GMP_CFG= GMP_STATUS=
  MMUC_CONTROL= MMU_CTL= HUB_AXICFG=` — the live configuration the coordinator asked for.
* Also fixed: in `V3D_PCTR=AB` mode, per-slot lines now appear for set B too (they were only ever
  printed in set-A windows: an even `every` over alternating sets).

**Async server** (`tools/gpu-lane/v3d-async/`, edited in place — committed tree was clean at 23:11;
diff also in `tools/gpu-lane/stkprof/h7-v3da.patch`; built into `out-h7/`, `-Werror` clean). New
`V3DA_KNOB_*` bits, all default 0 = unchanged behaviour: bit 5 `NO_POST_CLEAN`, bit 6
`NO_HANDOFF_FLUSH`, bit 7 `LINUX_ORDER`, bit 8 `PX_LOG` (no GPU effect: prints
`V3DA srv pxlog n= h= zero=` for **every** pan — use in guard runs, not fps runs), and
`V3DA_KNOB_LINUX = 0xaf` (bits 0,1,2,3,5,7). The game clone is unchanged (knobs are server-side).
`tools/gpu-lane/stkprof/h7-pxcompare.py <base1> <base2> <arm>` aligns the pxlog sequences
(quakespasm's timedemo is frame-deterministic), takes the frames on which the two base runs agree
(the deterministic, changing subset — particles and water warp make the rest differ run to run) and
requires the arm to be identical on ≥ 95 % of them with no new black frames (`GUARD PASS/FAIL`).

⚠ **The STK guard (`px_black`/`px_same`) cannot catch the failure the `linux` arm is most likely to
cause.** Removing the L2T waits previously produced *geometry mangle* (torches, small models; the
#67 analysis quoted in the winsys comment above the pre-bin flush), not black or frozen frames. So
the quakespasm pixel guard is the load-bearing correctness test and runs **before** any STK L2T arm;
HDMI snapshots are the only STK-side check for mangle.

Note for readers of base1: it has **no set-B per-slot lines** (the AB/`every` bug above), so the
per-slot DRAM view exists only from `stk-e2bh7` on.

### H7 pre-registered arms

| arm | binary / command | runs | reads |
|---|---|---|---|
| h7-stk-base | `export V3D_PCTR=AB`, `export V3D_PHX_PXLOG=8`, `stk-e2bh7 --track=hacienda --numkarts=4 --profile-laps=2` | 1 | the new binary's baseline (+ set-B slot DRAM lines) |
| h7-stk-linux | + `export V3D_PHX_L2T=linux` | 2 | render ms/f, L2T memory reads (ID 76), core reads, per-slot DRAM, `tlb_skip` |
| h7-stk-nohand | + `export V3D_PHX_L2T=nohand` | 1, after linux passes | as above + wedges |
| h7-stk-l2c | + `export V3D_PHX_NO_L2C=1` (L2T mode shipped) | 1 | as above; `h7` line shows `L2CACTL` |
| h7-qs-fps | `/bin/rpi4-v3d-async -r 1 -m serial -i -k <K>` then `quakespasm-v3da +timedemo demo1`, K = `0x0`, `0xaf`, `0xef`, interleaved | 3 per K | timedemo fps; `qstat` render busy; wedges/err |
| h7-qs-guard (first) | same with K \| `0x100`: `0x100` ×2, `0x1af` ×1, `0x1ef` ×1 | 4 | `h7-pxcompare.py base1 base2 arm` → `GUARD PASS/FAIL` |

Grading (fixed now): an arm **moves** a quantity if its trial mean differs from its base by > 5 %
(render ms/frame, fps) or > 10 % (L2T memory reads) and by more than the base trials' spread.
**Correctness gate** (an arm that fails it is void, whatever its speed): 0 wedges / TIMEOUT /
DROPPED lines, server `err=0 wedges=0`; STK: `px_black=0`, `px_same` not above base, HDMI snapshots
look like base; quakespasm: `h7-pxcompare.py` GUARD PASS (≥ 95 % identical on the base-deterministic
subset, no new black frames). **An STK L2T arm runs only after its quakespasm guard passed.**

| outcome | reading |
|---|---|
| linux moves render time/L2T reads by < 5 % / < 10 % | H7 refuted as a GPU-time cause (as predicted); keep Linux's sequence only if it wins CPU time or fps and passes the correctness gate |
| linux lowers L2T memory reads ≥ 20 % **and** render ≥ 10 % | H7 real: re-fetch after our extra maintenance matters (most likely the TLB/PTE-cache flush) — split with a TLB-only arm next |
| nohand wedges or corrupts | the pre-CT1 flush is load-bearing, as Linux assumes; never drop it |
| nohand clean and faster than linux | the handoff flush costs render time; still unsafe without a proof that CT1 cannot see stale tile-list lines |
| l2c moves anything | the `L2CACTL` write has an effect on 4.2 → drop it (Linux parity) through the normal gate |

Run order suggestion: h7-qs-fps/guard can go first (short, deterministic); the `ez` arm (H2) and
E2c (same settings on Pi OS — now the most informative comparison, since the render phase is
shader-issue bound) matter more than H7 for the 3× question.

## Result — base2 + ez1

- **base2** reproduces base1: render 93.7 ms/frame, L2T hit 6.6 %, TMU stalled 78 %, late-Z reject 34.9 %.
- **ez1** (`V3D_PHX_EZ=1`, upstream early-Z): EZ engaged (3 job slots `ez=LT`, one slot early-Z-clips 51.5 %
  of quads; late-Z reject 35 → ~25 %), **0 wedges**, but render **91.1 ms/frame, 7.24 fps — unchanged.**
  ⇒ H2 (overdraw) is real but not what costs the time. The memory/texture path (H1/H7: L2T hit 6.7 %, TMU
  stalled ~79 %) is now the lead; H7 test design in progress. (ez2, qr1 running.)
