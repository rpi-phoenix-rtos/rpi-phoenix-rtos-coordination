# V3D render/bin stall — 2026-06-21 reframe + fixes

Task #13. Supersedes the "cold-power-on-determined, shader-stage" framing.

## What the bug actually is (corrected)

The Quake GPU "render stall" is a **workload-dependent CLE control-thread hang**, NOT a
cold-power-on artifact:

- Boots render Quake cleanly for seconds→minutes (slc-7 ~6s, slc-10 ~14s, axicfg-2 ~106s,
  axicfg-3 ~88s) and **then wedge mid-render**, or survive the whole window. Not "stalled from
  frame 1."
- Two wedge modes, both a CLE thread freezing:
  - **BIN (CT0)**: `ct0ca` frozen mid-CL at a boot-specific addr; `int_sts` high bits =
    `V3D_INT_QPU_MASK` (7/8 coordinate-shader QPUs pending); FLDONE/OUTOMEM clear; `ovf_armed=0`
    (hung WITHOUT raising OUTOMEM).
  - **RENDER (CT1)**: `int_sts=0`, `ct1ca` parked **near `rcl_end`**, FRDONE never fires.
- **Data-dependent**: after a true reset, re-submitting the SAME frame re-hangs at the same
  `ct1ca` across all 3 resets; the NEXT (different) frame then renders fine. A specific frame's
  CL/tile-list triggers the hang; resetting the core alone can't help because re-running the
  same data re-hangs.

### Oracles retired
- "60/60 reset-immune / 300/300 in-boot clean" came from `rpi4-v3d-stalltest` rendering ONE
  trivial triangle per iteration — too simple to trigger a binner hang on complex geometry.
  That negative was never evidence of cold-determinism. Harness retired for this bug.
- A multi-boot **rate** A/B is statistically useless at the ~30-40% base rate (advisor: needs
  ~50-70 boots/arm). Use deterministic mechanism observation instead.

## Hypotheses tested

| Hypothesis | Verdict |
|---|---|
| Cold-power-on clock/PLL not settled | **REFUTED** — wedged boots show clk_v3d cfg=meas=500MHz delta=0, locked; temp ~38-44°C; throttled=0; corevolt 0.856V. clk fingerprint identical clean vs wedged. |
| Thermal | **REFUTED** — wedges at 38-44°C, no throttle bit. |
| GFXH-1383: HUB_AXICFG (AXI burst cap) unset → AXI deadlock | **REFUTED** — `cold_HUB_AXICFG=0x0000000f` already at power-on (firmware set MAX_LEN). Our write is a no-op. |
| `gmp=0x30` = stuck AXI transaction | **REFUTED** — `cold_GMP_STATUS=0x30` at idle too; 0x30 (RD/WR_ACTIVE) is the normal state, not a hang signature. |
| Memory/BO content (guard page, VA recycle, zeroing) | Weak — zeroing BOs changed wedge *content* (garbage→zeros) but NOT the rate; CT1 overruns regardless of content. |
| **bin→render L2T flush race (wait-new missing)** | **LEADING** — CT1 parks near rcl_end reading an incomplete tile-list; the flush was only ISSUED then CT1 kicked immediately. Fix applied; under test. |

## Fixes applied (tools/v3d-driver-port/, 2026-06-21)

1. **bin→render L2T flush wait-new** (winsys ioc_submit_cl): after issuing `L2TCACTL_L2TFLS`,
   `l2t_flush_wait()` again so the flush COMPLETES before CT0/CT1 fetch. Reordered render path
   to clean+wait → invalidate(SLCACTL) → kick. PRIMARY fix for the data-dependent CT1 wedge.
2. **True-reset recovery** (reset_reinit_core): was the weak `v3d_phoenix_powerOn` (RSTN
   re-deassert only) which could NOT clear a frozen `ct0ca`/`ct1ca`; now `idle_axi` (GMP
   STOP_REQ drain) + `v3d_phoenix_reset` (asbStop + assert PM_V3DRSTN + powerOn) + re-apply.
   HW-confirmed it can RECOVER a wedge (`RECOVERED after 1 reset`, PM_GRAFX 0x1000->0x1040 =
   RSTN actually asserted). Mitigation: fatal hang → hitch. Note: re-submitting the SAME bad
   frame still re-hangs (data-dependent), so a frame may still be dropped after retries.
3. **Cold-state probe** (`v3d_phoenix_logColdState`): logs clk cfg/meas/delta, clkstate, temp,
   throttle, corevolt, pm_grafx, cold HUB_AXICFG + GMP_STATUS once per boot. Permanently rules
   out thermal/clock/undervolt/AXICFG.
4. **Enriched BIN-TIMEOUT dump**: PTB bpca/bpcs/bpoa/bpos + GMP + int_qpu bits.
5. Defensive (no-op here, matches Linux): set HUB_AXICFG=MAX_LEN in apply_core_regs.

## RESOLUTION (2026-06-21): root cause localized to a HW-marginal depth-pipeline drain stall; mitigation hardened

Instrument-validated wedge dump (full RCL dump + per-address BO handle/base/offset + multi-match
detector) settled it decisively:
- **RCLSTART** rcl_start → single BO, off=0x0 (no offset bug, no wrong-BO; OVERLAP detector silent).
- **RCLFULL** the RCL is COMPLETE + VALID (0x79 config head, 0x7c TILE_COORDINATES, 0x17
  SUPERTILE iteration) and **byte-identical** to a CLEAN frame's RCL head (8/8) — config is the
  SAME on rendering vs wedging frames, so NOT a config/EZ/offset bug.
- **WEDGECA** ct1ca is in the VALID per-tile sublist BO (CT1 correctly branched out of the RCL).
- **fdbgs=0x000350ef** decoded vs external/linux v3d_regs.h = QXYF_FIFO_OP_VALID (valid frag work
  queued) + XYNRM_IP_STALL + **DEPTHO_FIFO_IP_STALL (depth OUTPUT)** + INTERPZ_IP_STALL + full
  EZTEST backup. The depth-output/interp-Z stages won't drain → whole fragment pipeline backs up.

Root cause = a **content-triggered HW depth/fragment-pipeline drain stall** on complex geometry
under the **render-to-scanout** path (the full-screen color RT forced RASTER + backed by the
UNCACHED HDMI framebuffer). Corroborated as part of the broader V3D RT **coherency wall** the port
already documented (v3d_resource.c: "making a GPU-rendered RT BO CPU-cacheable HANGS THE GPU after
the first cacheable readback — a coherency wall"). The existing Phoenix EZ-disable fix
(v3dx_draw.c) addresses a DISTINCT early-Z *input* hang; this is the depth *output* stage, which
EZ-disable does not cover. Ruled OUT by reading the upstream diff: no depth-config divergence
(depth stays tiled/uncached; only the COLOR RT diverges, gated on PIPE_BIND_RENDER_TARGET).

**Mitigation hardened (the deliverable):** on a wedge, do ONE true reset (asbStop + assert RSTN +
power-on + re-apply core regs) to clean the wedged core for the NEXT (different) frame, and DROP
the current frame — NO re-submit (re-submitting the same data re-hangs, HW-confirmed, so it would
only stack another multi-second spin-timeout). Converts a wedge from a multi-second freeze into a
single dropped frame. Remaining hitch is dominated by the ~2.5 s detection spin-timeout; tuning it
down (toward the heaviest legit frame time, ~100 ms) is a follow-up that needs the worst-case
legit frame measured to avoid false-positive drops.

## CONFIRMED (2026-06-21): render-to-scanout (live-fb contention) is the trigger

Decisive experiment: rebuilt rpi4-v3d-stalltest to render 3000 COMPLEX depth-tested perspective
frames (1152 tilted tris/frame, heavy overdraw, per-frame rotation) CONTINUOUSLY to its own FBO —
a RASTER render target in NORMAL DRAM, NOT the scanout fb. Result: **0 wedges over 3000 frames**
(wedged_frames=0, render_timeouts=0). Identical-complexity geometry that wedges Quake does NOT
wedge when the RT is not the live HDMI framebuffer.

=> The trigger is NOT geometry/RASTER/uncached alone — it is the GPU writing the LIVE scanout fb
while the HVS display controller continuously reads it for HDMI. That memory contention stalls
the depth-OUTPUT FIFO on heavy frames. A DRAM RT (no concurrent display reader) never wedges.

REWORK (in progress) — decouple the depth-using render from the live fb:
- Quake renders depth-tested geometry to a DRAM color RT (RASTER, no scanout backing) — proven
  no-wedge by the stalltest.
- A per-frame GPU glBlitFramebuffer (COLOR only, NO depth) resolves that RT to the scanout fb.
  The blit has no depth FIFO, so it cannot hit the depth-output stall, and it is a single light
  streaming pass (brief fb-contention window) vs the whole depth-tested render.
- Contained to the present layer (pl_phoenix_glctx.c two FBOs + a resolve; pl_phoenix_vid.c
  GL_EndRendering calls the resolve in the scanout branch). No Mesa/winsys changes.

## Next rungs (root cause is HW-marginal; these are perf/robustness follow-ups, not blockers)
- Tune the wedge-detection spin-timeout down (measure the heaviest legit Quake frame first).
- The real fix would keep render-to-scanout but avoid the uncached-linear-store pressure that
  triggers the depth-FIFO stall (e.g. tiled render + a GPU tile-store-to-linear resolve, or a
  cacheable scanout with proper invalidate) — both interact with the documented RT coherency wall.

## (historical) Next rungs if the flush fix is insufficient
- Faster mitigation: drop SUBMIT_MAX_RETRIES (same-data re-hangs, so 3 retries just delay the
  skip) → 1 quick retry then skip the frame = shorter hitch.
- TOP_GR_BRIDGE_SW_INIT reset rung (need the bridge physical base on BCM2711).
- If specific frames reliably hang: dump the offending RCL/tile-list and compare CL emission vs
  Mesa/Linux for that primitive class.
