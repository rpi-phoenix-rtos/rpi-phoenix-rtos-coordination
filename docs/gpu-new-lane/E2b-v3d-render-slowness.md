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
