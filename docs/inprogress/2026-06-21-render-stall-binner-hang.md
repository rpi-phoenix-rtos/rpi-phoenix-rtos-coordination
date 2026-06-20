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

## Next rungs if the flush fix is insufficient
- Faster mitigation: drop SUBMIT_MAX_RETRIES (same-data re-hangs, so 3 retries just delay the
  skip) → 1 quick retry then skip the frame = shorter hitch.
- TOP_GR_BRIDGE_SW_INIT reset rung (need the bridge physical base on BCM2711).
- If specific frames reliably hang: dump the offending RCL/tile-list and compare CL emission vs
  Mesa/Linux for that primitive class.
