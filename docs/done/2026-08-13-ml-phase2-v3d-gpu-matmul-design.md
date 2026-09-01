# ML phase 2 — V3D GPU matmul acceleration for llama2 (design + feasibility)

Phase 1 (CPU) shipped: llama2.c runs on Phoenix/RPi4, deterministic, HW-verified at 260K + 15M
([[project_ml_inference_llama2]], tools/llama2-port/). Phase 1 tok/s: stories15M ≈ **5.8 tok/s** (fp32, single
CPU) — the motivation for GPU offload. This doc scopes phase 2: offloading llama2's `matmul()` (the dominant cost)
to the Pi 4's V3D 4.2 GPU via **compute (CSD)**.

## UPDATE (session 14) — the code is AHEAD of the earlier plan; approach corrected

Reading the actual port code (not just the notes) changed the plan materially:
- **The CSD dispatch handler ALREADY EXISTS.** `tools/v3d-driver-port/v3d_phoenix_winsys.c` has
  `ioc_submit_csd()` (commit **1067af1** "implement CSD (compute dispatch) — was a no-op stub"): it programs
  CSD_QUEUED_CFG0..6 from `s->cfg[]`, writes CFG0 to KICK, and synchronously blocks on `INT_CSDDONE` (BIT 7),
  wrapped in the same MMU-TLB + cache-coherency bracket as `ioc_submit_cl`. `phoenix_v3d_ioctl` routes
  `DRM_V3D_SUBMIT_CSD → ioc_submit_csd` (line ~1645), and `PARAM_SUPPORTS_CSD` returns 1. So step-1 is NOT "write
  the handler" — the handler is done, just **UNTESTED: no compute kernel has ever been dispatched through it.**
- **Kernel generation path is PROVEN (for FS/VS) and extends to compute.** `tools/v3d-shader-tool/v3d_shader_dump.c`
  already drives Mesa's `v3d_compile()` off-device (host) to turn a NIR shader into V3D-4.2 QPU bytecode (used to
  derive GLQuake shaders). Mesa's v3d compiler supports compute: `struct v3d_compute_prog_data { base; uint16_t
  local_size[3]; ... }` (v3d_compiler.h:1111). So: build a compute NIR shader → `v3d_compile(MESA_SHADER_COMPUTE)`
  → QPU insts + `v3d_compute_prog_data`. The prog_data (local_size + the standard CSD layout) supplies the
  `drm_v3d_submit_csd.cfg[0..6]` fields (workgroup size/count, shader addr, uniforms addr, etc.).
- **Net:** the remaining work is (a) compile a compute kernel to QPU off-device via the shader tool, (b) a small
  on-Phoenix harness that lays out BOs (shader/uniforms/input/output), fills `cfg[]`, calls the existing
  ioc_submit_csd, and reads back — numeric-verify. This is more tractable than the original "build the whole CSD
  path" estimate.

**Revised step 1:** extend v3d-shader-tool with a trivial COMPUTE shader (e.g. `out[gl_GlobalInvocationID.x] =
gid`), get its QPU + v3d_compute_prog_data; build the minimal CSD harness; dispatch through ioc_submit_csd; read the
output BO back and assert exact values. That validates the untested handler end-to-end and nails the cfg[] layout.
Then the matmul kernel, then wire into llama2.

## CONCRETE CSD SUBMIT RECIPE (session 15-16) — everything the harness needs

**Kernel (probe `out[gid]=gid`):** 12 QPU words (in tools/v3d-shader-tool/shaders-dump.txt), local_size=16x1x1,
threads=4, single_seg=0, shared=0. Compiled off-device via the extended v3d-shader-tool.

**Uniforms buffer (3 words, from the tool's prog_data dump; QPU disasm confirms ldunif order word0→1→2):**
`[0]=0x0000ffff` (const), `[1]=0x0000001a` (const), `[2]=<output BO GPU VA>` (the TMU store base — the QPU does
`add tmua, r5, r4` with r5=word[2]). Only word[2] is runtime-filled by the harness.

**drm_v3d_submit_csd.cfg[0..6]** (authoritative, from Mesa gallium v3dx_draw.c CSD dispatch; ver<71 = V3D 4.2):
- num_wgs = Π num_workgroups[i]; for a 1-workgroup probe: num_workgroups={1,1,1}, num_wgs=1, wg_size=16.
- cfg[0..2] = num_workgroups[i] << V3D_CSD_CFG012_WG_COUNT_SHIFT.
- cfg[3] = (wgs_per_sg & 0xf)<<WGS_PER_SG_SHIFT | (batches_per_sg-1)<<BATCHES_PER_SG_M1_SHIFT | (wg_size & 0xff)<<WG_SIZE_SHIFT.
  wgs_per_sg via v3d_csd_choose_workgroups_per_supergroup(); batches_per_sg=DIV_ROUND_UP(wgs_per_sg*wg_size,16).
- cfg[4] = num_batches - 1 (ver<71). num_batches = batches_per_sg*whole_sgs + DIV_ROUND_UP(rem_wgs*wg_size,16).
- cfg[5] = shader_BO_GPU_VA | V3D_CSD_CFG5_PROPAGATE_NANS (ver<71) | (single_seg?SINGLE_SEG:0) | (threads==4?THREADING:0).
- cfg[6] = uniforms_BO_GPU_VA.
- coef[0..3] = 0 (Mesa doesn't set them for ver<71). Shift/flag constants: broadcom v3d packet/regs (V3D_CSD_*).

**Handler:** the existing (untested) ioc_submit_csd (winsys) programs CSD_QUEUED_CFG0..6 from cfg[], kicks CFG0,
blocks on INT_CSDDONE. So the harness just needs BOs + cfg[] + uniforms + SUBMIT_CSD ioctl, then read back.

## STAGED BRING-UP (advisor, session 16) — do NOT test all 5 unverified layers at once

First run of an untested GPU submit rarely works; with one Pi + UART/HDMI, a failed `out[i]==i` gives no layer
attribution. So stage each kernel so a failure points at ONE layer:
1. **Handler liveness:** a kernel that only thread-ends (no TMU, no uniforms). Success = ioc_submit_csd returns +
   CSDDONE fired, no hang. Isolates handler + cfg[] + BO plumbing.
2. **Constant store:** write a fixed constant to out[0] at a HARDCODED offset (minimal/no uniforms, no gid math).
   Success = readback shows the constant. Isolates TMU-write + output-BO-VA path.
3. **out[gid]=gid** with the full 3-word uniforms. If step 2 passed and this fails → gid math / supergroup packing.

Emit all three kernels via the host v3d-shader-tool (same off-device oracle as the QPU).

**Two landmines that masquerade as "wrong kernel":**
- **CPU cache on readback:** ioc_submit_csd's bracket handles the GPU side, but the CPU's view of the output BO must
  be invalidated after CSDDONE — or **map the output BO UNCACHED** for the probe (removes the variable; matches the
  winsys "MAP_CONTIGUOUS returns non-zeroed DRAM" class of gotcha).
- **wgs_per_sg:** do NOT hand-port `v3d_csd_choose_workgroups_per_supergroup` — have the host tool CALL it and PRINT
  the value (+ num_batches, + the full cfg[0..6]) for num_wgs=1/wg_size=16, and hardcode that for the probe.

**Highest-leverage — host submit oracle:** extend the tool to print the exact cfg[0..6] + uniform bytes Mesa would
produce for this dispatch, so the harness reproduces known-good bytes instead of hand-encoding → step-1 failures can
only be plumbing.

**Timebox:** one clean staged attempt. If step 1 hangs the GPU with no signal after ~2 turns of blind poking, BANK
and rotate — the kernel-gen (first V3D compute QPU) + this ABI recipe are already durable, reusable deliverables
(same clean-line discipline as the coreutils bank). 5 turns deep with no Pi cycle is mild evidence this is closer to
the owner-gate reality than autonomously-trivial; the staged attempt will tell which, fast.

## MATMUL PLAN (advisor, session 21) — CSD bring-up is DONE (3/3 steps HW-verified); this is the payload

**Verification — numeric diff is the PRIMARY gate, not the story.** GPU fp accumulation ≠ CPU order, so full-pipeline
bit-identical is DEAD once offloaded — accept it. The real gate: per-matmul **GPU-vs-CPU numeric diff, tight relative/
ULP tolerance, random inputs at real weight magnitudes** — deterministic + near-airtight. The end-to-end llama2 run is
a DEMO, not a check (a fluent story survives a subtly-wrong matmul; a correct matmul still diverges the token stream
via ULP→argmax flips).

**Perf — MICROBENCH one matmul BEFORE integrating.** matmul-vector is memory-bandwidth-bound (each weight read once);
dispatch is synchronous spin-on-CSDDONE + double cache-flush; llama2 fires ~dozens of matmuls/token. GPU-incl-dispatch
may be SLOWER than CPU. Measure a single dim×dim (GPU w/ persistent pre-alloc BOs vs CPU) in isolation — that number
decides whether integration is worth building. **Success = "a numerically-correct V3D matmul, integrated as an
optional llama2 path"; tok/s is a MEASURED outcome reported honestly.** A negative perf result is a legit finding, NOT
a failure — and it must NOT spawn an optimization grind (tiling/shared-mem/async). Measure → integrate correct kernel
→ document perf reality → LAND the arc (same bank discipline, applied to perf).

**Technical flags:** (a) this kernel does the **first TMU general LOADs** (reads of w,x) — a distinct path from the
proven writes; expect maybe one more bring-up bug (the numeric diff catches it instantly; load has no flush-visibility
trap since it's the GPU reading DRAM we wrote). (b) **Pre-allocate persistent BOs** (w/x/xout/uniforms) ONCE + reuse
across dispatches — per-call CREATE_BO in the hot loop would dominate + pollute the perf number.

**Kernel (v1, do NOT optimize before microbench):** one invocation per output row i, loop over inner dim j:
`xout[i]=Σ_j w[i*n+j]*x[j]`. Kernel-gen: hand-build the NIR loop (nir_push_loop + load_ssbo/fmul/fadd/store_ssbo) OR
GLSL→SPIR-V(glslangValidator offline)→spirv_to_nir→v3d_compile (like gen-triangle-spirv.py). NEXT.

## Feasibility — ESTABLISHED

- **HW:** BCM2711 V3D 4.2 supports compute-shader dispatch (CSD). The Linux v3d driver implements it
  (external/linux/.../v3d/v3d_submit.c) and exposes `DRM_V3D_PARAM_SUPPORTS_CSD`.
- **uAPI:** the port's `tools/v3d-driver-port/v3d_drm.h` already has `DRM_V3D_SUBMIT_CSD` (0x07) +
  `struct drm_v3d_submit_csd { __u32 cfg[7]; __u32 coef[4]; ... }` — the standard CSD submit (cfg = workgroup
  dims + shader/uniforms addresses; coef = the dispatch grid/wg config).
- **Mesa:** upstream Mesa v3d gallium supports GL/PIPE compute shaders. BUT the port's Mesa build list
  (v3d-core-sources.txt) does **not** appear to include the v3d compute-compiler paths — so using Mesa to compile
  the kernel needs those sources added (non-trivial), OR hand-write the QPU kernel.
- **Port submit path:** `tools/v3d-driver-port/v3d_libdrm_shim.c` is **render-only** today — `SUBMIT_CL` is
  synchronous (blocks on FLDONE/FRDONE), programming CT0CA/CT1CA. There is **no CSD handler yet**. Adding one is
  **additive** (a new ioctl path), so it does NOT touch the load-bearing GL render path → **low regression risk** to
  Quake/vkQuake/X11 (all of which use SUBMIT_CL rendering).

## The key enabler — matmul is NUMERICALLY verifiable (not HDMI-bound)

Unlike DRI/DRM or a DE (which need HDMI/visual verification, poor for autonomous mode), a compute matmul writes its
result to a buffer that we **read back and diff against the CPU matmul** — deterministic, exact (or ULP-bounded for
fp), no display involved. This upgrades phase 2 from "owner-gate / not autonomously verifiable" (the earlier
assumption) toward **autonomously attemptable**, gated only by CSD bring-up difficulty.

## Design

`matmul(xout, x, w, n, d)` computes `xout[i] = sum_j w[i*n+j]*x[j]` for i in [0,d) — a matrix(d×n)·vector(n).
Called per layer for the big weight matrices (dim×dim attention proj, dim×hidden FFN). Offload the large ones to
V3D; keep small ops on CPU.

- **Approach A — Mesa compute shader:** add the v3d compute-compiler sources to the port build; write a GLSL/SPIR-V
  matmul compute shader; dispatch through Mesa's compute pipe. Pro: reuse Mesa's QPU codegen. Con: the port's Mesa
  compute path is currently unbuilt (real integration work); heavier.
- **Approach B — hand-authored QPU kernel + direct CSD submit:** write one matmul kernel in V3D QPU (via
  tools/v3d-shader-tool) and submit it through a new `SUBMIT_CSD` shim handler. Pro: minimal, targeted, no
  Mesa-compute dependency. Con: hand-QPU (VPM/TMU loads, the V3D 4.2 ISA) is hard.
- **Recommendation:** start with **B's submit-path bring-up** using a *trivial* kernel (below); pick A vs B for the
  real matmul kernel after the dispatch path works.

## Build plan (multi-cycle, each step numerically verified)

1. **CSD dispatch bring-up (milestone).** Add `SUBMIT_CSD` to v3d_libdrm_shim.c: program CSD_QUEUED_CFG0..7 from
   `cfg[]`/`coef[]`, kick, block on CSDDONE (model on the existing synchronous SUBMIT_CL). Run a trivial compute
   kernel (e.g. write threadIdx to an output buffer, or vector-add) → **read back + numeric-verify**. This proves
   compute works on the port. Autonomously verifiable.
2. **matmul compute kernel** (A or B) → numeric-diff vs CPU matmul on random inputs (ULP-bounded).
3. **Wire into llama2:** V3D path for the big matmuls, CPU fallback; verify **end-to-end deterministic output
   bit-identical to phase-1 CPU** (the existing 260K/15M references) + measure tok/s speedup.

## Risk / autonomy

- Regression: LOW — additive CSD path, GL render untouched. Git-disciplined (commit each step; the shim change is
  isolated).
- Autonomy: steps 1–3 are numerically verifiable over psh/diag (no HDMI). **Attempt autonomously**, starting at the
  CSD bring-up milestone; escalate to owner-gate only if QPU codegen (kernel authoring) proves intractable without a
  display/owner in the loop.
- Perf expectation: V3D is a modest GPU; a well-mapped fp32 matmul could give a several-× speedup over 5.8 tok/s,
  but memory-bandwidth-bound — measure, don't assume.

## NEXT ACTION when resumed
Implement step 1 (CSD dispatch bring-up in v3d_libdrm_shim.c + trivial kernel + numeric read-back). That single
milestone decides Approach A vs B and whether the whole phase is autonomously tractable.
