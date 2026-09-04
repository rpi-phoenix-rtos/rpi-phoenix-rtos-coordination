# Dropped V3D meta-copy jobs (#67): drop path, CL decode, and a CPU-emulation fix plan

> **SUPERSEDED 2026-09-04 — read this as evidence, not as a plan.** The "second, distinct, unfixed defect"
> described below (an RCL BO holding image data instead of a control list) was root-caused: pages handed to an
> *uncached* mapping still carried the previous owner's dirty cache lines, which were evicted later on top of
> Mesa's uncached writes. That is exactly why the leading byte is a previous owner's texel and never a defined
> CLE opcode. Fixed in the kernel (`5d8645f6`); see `docs/done/2026-09-04-uncached-page-stale-cache-rootcause.md`.
> The byte-level statistics here remain the best surviving fingerprint of the defect.

Date: 2026-09-03
Scope: **read-only source analysis + a written patch proposal.** No build, no `make`, no Pi cycle
(an authoritative Docker build owns the machine). No file under `sources/` or `external/` was
modified.
Target: `sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c`
Companions: [`2026-09-03-torch-intermittency-driver-analysis.md`](2026-09-03-torch-intermittency-driver-analysis.md)
(the statistical case for R1), [`2026-09-03-vkquake-torch-rootcause-candidates.md`](2026-09-03-vkquake-torch-rootcause-candidates.md).

---

## 0. Verdict up front

**The CPU-emulation approach is feasible and I can specify it exactly — but it is a partial fix, and
one of its premises in the existing in-code comment is wrong.** Three findings dominate:

1. **The copy CL is NOT malformed.** The winsys comment at `v3d_phoenix_winsys.c:1376-1389` concludes
   "the CL itself is malformed or contains something this V3D revision will not execute". I
   reconstructed the exact byte layout V3DV emits for `vkCmdCopyBuffer` on V3D 4.2 and it matches the
   recorded `RCLBYTES` dumps **byte for byte**. The measured park offset `0x43` is the **second
   dummy `STORE_TILE_BUFFER_GENERAL` (buffer_to_store = NONE, address 0) of the GFXH-1742
   workaround** in `external/mesa/src/broadcom/vulkan/v3dvx_meta_common.c:222-244` — an
   upstream-standard, no-op tile-store issued **before the copy proper is even reached**. Nothing in
   it is malformed. The wedge is in this port's tile-store/RT path, not in the control list. §2.
2. **Every RCL size on record is explained by one formula**, `rcl_len = 92 + 3 × n_supertiles`.
   Observed: 98, 101 (0x65), 122 (0x7a), 143 (0x8f) → n = 2, 3, 10, 17. And every copy BCL is exactly
   14 bytes = `NUMBER_OF_LAYERS(2) + TILE_BINNING_MODE_CFG(9) + FLUSH_VCD_CACHE(1) +
   START_TILE_BINNING(1) + FLUSH(1)`. That is a tight, cheap, structural fingerprint. §2.3
3. **Only about half of the recorded drops have a decodable control list.** Across all
   `artifacts/rpi4b-uart/*.log`, 56 `RCLBYTES` dumps exist; **25 begin with `0x79`**
   (`TILE_RENDERING_MODE_CFG`, a valid RCL) and **31 begin with RGBA pixel data or zeros**
   (13 × `0x74`, 11 × `0x2a`, 3 × `0xff`, 2 × `0x90`, 2 × `0x00`) — 29 of those 31 with
   `park_off=0x0`, i.e. CT1 stopped on byte 0 of a list that is not a list. None of `0x74`, `0x2a`,
   `0x90`, `0xff` is a defined CLE opcode (no `code="116"`, `"42"`, `"144"`, `"255"` in
   `v3d_packet.xml`), which is why the CLE halts with `int_sts=0`. For those, **there is nothing to decode: no LOAD packet, no
   STORE packet, no src/dst address, no extent.** A decode-and-memcpy fallback rejects them and
   changes nothing. That is a **second, distinct, unfixed defect** (the RCL BO holds image data at
   submit time) and it is arguably the higher-value target. §6.

So: ship the emulation (it is small, cheap, provably correct on the decodable class, and it converts
a silent permanent data loss into a correct copy for ~half the drops), **and** add the near-free
entry-time RCL validity check in §7 that discriminates the second failure mode in one boot.

---

## 1. Task 1 — the drop path, precisely

All line numbers in `sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c`.

`ioc_submit_cl()` begins at **:989**. `int job_failed = 0;` at **:994** ("set if bin or render wedged").

### 1.1 Where the timeout is detected — two places, both set the same flag

**Bin (CT0).** The wait loop is **:1092-1129**: a bounded `spins = 8000000` countdown that breaks on
`INT_FLDONE`, services binner `INT_OUTOMEM` by handing out chunks of the persistent overflow pool
(**:1105-1121**), and — the fast detector — samples `ct0ca` (core reg `0x0110`) every ~1M spins and
declares a wedge after 5 consecutive identical samples (~0.8 s) via `spins = 0` (**:1122-1128**).

On `spins == 0` (**:1130**): `job_failed = 1` (**:1131**), then a diagnostic block prints
`BIN TIMEOUT …` (**:1132-1135**), `BIN PTB …` (**:1139-1142**), `BIN MMU-VIO …` (**:1153-1155**),
`gpuva_describe("BINFAULT"/"BCLSTART"/"BINCA")` (**:1156-1158**), `BIN CT0 …` (**:1165-1167**),
`BIN FDBG …` (**:1168-1169**), the CL window at `ct0ca` (**:1170-1178**) and `BCLFULL` — up to 40
words of the BCL (**:1179-1185**). The whole block is rate-limited to the first 3 events by a
`static int bdbg` (**:1147**).

Then **:1191-1192**: `if (job_failed) goto job_retry;` — **the render (CT1) is never kicked.** This
matters for the fix: on a bin timeout the RCL was never executed, so its bytes are pristine and the
copy is trivially emulatable.

**Render (CT1).** CT1 is kicked at **:1226** (`CLE_CT1QBA` / `CLE_CT1QEA`). The wait loop is
**:1237-1253**: `spins = 16000000`, break on `INT_FRDONE`; every ~1M spins it acknowledges latched
QPU interrupt bits (**:1246**) and samples `ct1ca` (`0x0114`), declaring a wedge after 5 identical
samples (**:1248-1251**).

On `spins == 0` (**:1255**): `v3d_phoenix_render_timeouts++` (**:1257**), `job_failed = 1`
(**:1258**), then `RENDER TIMEOUT …` (**:1259-1262**), `RENDER qpu_int_acks=…` (**:1263-1264**),
`RENDER DBG fdbgo/fdbgs/errstat + wedge_op` with a small opcode name table (**:1267-1275**),
`RENDER MMU-VIO …` plus `gpuva_describe("RENDERFAULT"/"RCLSTART"/"RENDERCA")` (**:1278-1290**,
first 3 events only, `static int rdbg` at **:1279**), and `RENDER ct1ca recheck` (**:1293**).

### 1.2 What `job_failed` covers

Exactly two conditions, both timeouts: **bin spin exhausted / `ct0ca` frozen**, and **render spin
exhausted / `ct1ca` frozen**. It is *not* set for: binner overflow-pool exhaustion (logged at
**:1118-1119** but the loop continues), an MMU violation (only observed via `MMU_VIO_ADDR` in the
dump), a GMP violation, or a TFU/CSD failure (separate functions). There is no success/failure
signal from the GPU other than the FLDONE/FRDONE latches.

### 1.3 What state is reset

Everything happens in `reset_reinit_core()` at **:958-970**, called from **:1373**:

- `idle_axi(W.core0)` (**:948-956**) — `GMP_CFG_STOP_REQ`, then a bounded spin for
  `GMP_STATUS` RD/WR counts + `CFG_BUSY` to clear (mirrors Linux `v3d_idle_axi`).
- `v3d_phoenix_reset()` (declared **:322**, implemented in `v3d_phoenix_power.c`) — a **true** reset:
  `asbStop` + assert `PM_V3DRSTN` + power back on.
- `apply_core_regs()` (**:887-931**) — re-writes `MMU_PT_PA_BASE`, the full `MMU_CTL` fault config,
  the illegal-access scratch page, `MMUC_CONTROL`, `L2CACTL = L2CCLR|L2CENA`, the whole-cache L2T
  flush range (`L2TFLSTA = 0`, `L2TFLEND = ~0`, **:915-916**), the GFXH-1383 `HUB_AXICFG` burst cap
  (**:923**), and `MISCCFG` (**:929**).

**Not reset:** the MMU page table, the BO table, the GPU-VA allocator, and the BO contents. Those
survive, which is precisely why a CPU emulation of the dropped copy is possible after the reset.

### 1.4 What is logged on the drop

Inside `if (job_failed)` at **:1309**:

- `v3d_phoenix_render_recoveries++` (**:1310**) and `GPU wedged — true reset + drop this frame
  (mitigation; drops=%u)` (**:1311-1313**).
- `gpuva_describe("DROP-BCLSTART", s->bcl_start)` (**:1339**) and
  `gpuva_describe("DROP-RCLSTART", s->rcl_start)` (**:1340**) — resolve each list address to the
  owning BO, or report "NO live BO" / "N OVERLAPPING BOs".
- `RCLBYTES gpuva=… n=… park_off=…:` followed by every byte of the RCL, with `>` marking the park
  offset — **only when `rcl_end - rcl_start <= 256`** (**:1348-1365**). The park offset is computed
  at **:1351-1353** and is `0xffffffff` when `ct1ca` is below `rcl_start` (i.e. CT1 never ran — the
  bin-timeout case).
- `DROPPED job bcl=[…..] %u B  rcl=[…..] %u B` plus the string
  `"  <-- TINY CL: likely a one-shot upload/meta-copy, NOT a redrawable frame"` when the **BCL** is
  under 256 bytes (**:1366-1372**).
- `reset_reinit_core()` (**:1373**).
- **:1376-1389** is the "do not re-try the retry" note recording that a one-shot upload CL re-wedges
  at the same offset on a freshly reset core, 3/3 attempts.

### 1.5 The function reports SUCCESS. Quoted.

After the `if (job_failed)` block, control falls through to the unconditional readback epilogue and
the single return:

```c
	/* L2T flush so RT stores reach RAM before CPU readback (scout finding). */
	l2t_flush_wait(c0);                       /* GFXH-1897: render flush must complete first */
	c0[CTL_L2TCACTL/4]=L2TCACTL_L2TFLS|(2u<<1); /* FLM_CLEAN */
	return 0;
}
```
— `v3d_phoenix_winsys.c:1391-1395`. **`return 0;` is at :1394**, and it is the *only* return in the
function. There is no path by which a caller learns a job was dropped.

The silence is then amplified by the fence shims: `drmSyncobjWait` returns 0 ignoring handles and
timeout, `drmSyncobjCreate` hands out the constant handle 1 (`mesa/v3d_libdrm_shim.c:16-38`),
`drmSyncobjQuery2` reports `~0ull` and `drmSyncobjTimelineWait` reports "all points reached"
(`mesa/v3dv_libdrm_shim.c:52-86`), and `DRM_V3D_WAIT_BO` returns 0 unconditionally
(`v3d_phoenix_winsys.c:1917-1918`). Nothing above the winsys can observe the loss.

---

## 2. Task 2/3 — how a job's lists are addressed, and what a meta-copy CL actually contains

### 2.1 GPU VA → CPU pointer is already solved in this file

`struct pbo` (**:261-269**) records, per BO: `handle`, `cpu` (the `mmap`'d, normally **uncached**,
`MAP_CONTIGUOUS` CPU pointer), `pa`, `gpuva`, `size`. `ioc_create_bo` (**:590-723**) allocates the
GPU VA with `va_alloc` (**:557-579**), maps the pages (`MAP_UNCACHED` unless
`V3D_CREATE_BO_CACHEABLE`, **:685-687**), `memset`s the BO to zero (**:701**), and writes one PTE per
page from the *actual* per-page `va2pa` (**:707-710**).

Three helpers already exist and are exactly what a decoder needs:

- `gpuva_to_cpu(gpuva)` (**:752-760**) — linear scan of `W.bos`, returns
  `(char *)b->cpu + (gpuva - b->gpuva)` for the first live BO covering `gpuva`, else NULL.
- `gpuva_bo_remaining(gpuva)` (**:822-830**) — bytes from `gpuva` to the end of the owning BO, for
  bounds checks. Already used this way by the CPU UIF tiler (see its comment at **:819-821**).
- `gpuva_describe(label, gpuva)` (**:836-853**) — logs every live BO covering `gpuva`, and warns when
  0 or >1 match (so "first match" ambiguity is visible rather than inferred).

Crucially, **Mesa and the winsys share one mapping.** `DRM_V3D_MMAP_BO` (**:1938-1947**) returns
`(uint64_t)(uintptr_t)b->cpu` as the mmap offset and the libdrm shim hands that pointer straight
back. So the bytes `gpuva_to_cpu()` reads are literally the bytes Mesa's packer wrote, and the bytes
the GPU fetches through the PTEs derived from the same pages. There is no aliasing gap to worry
about, and the existing `RCLBYTES` dump is therefore a **valid** read of the control list.

**Answer: yes — the winsys can read (and write) the BCL, RCL, indirect tile list, and both copy
buffers of any job, with no new plumbing.**

### 2.2 `vkCmdCopyBuffer` on V3D 4.2 is a CL render job — confirmed, with the code path

`external/mesa/src/broadcom/vulkan/v3dvx_meta_common.c`, `v3dX(meta_copy_buffer)` at **:1368-1516**:

- **:1376-1443** — the TFU raster→raster fast path is inside `#if V3D_VERSION >= 71`, so on 4.2 it is
  **compiled out**. (It cannot be back-ported: on 4.2 the TFU "can handle raster sources but always
  produces UIF results", **:1018** — raster output only exists from 7.1's `TFU_IOC`.)
- **:1451-1457** — `item_size` starts at 4 and halves while either the absolute src or dst offset is
  misaligned; **:1459-1460** halves again while `region->size` is not a multiple. `4 → RGBA8UI`,
  `2 → RG8UI`, `1 → R8UI` (**:1466-1481**).
- **:1483-1513** — one `V3DV_JOB_TYPE_GPU_CL` job per chunk. `framebuffer_size_for_pixel_count`
  (**:1328-1366**) picks `w × h` with `w*h <= num_items` (asserted **:1361**);
  `v3dv_job_start_frame(job, w, h, 1, true, 1, BPP_32, …)` (**:1492**);
  `v3dX(job_emit_binning_flush)(job)` (**:1499**) — **the only thing in the BCL besides the prolog**;
  `v3dv_job_allocate_tile_state` (**:1500**); `v3dX(meta_emit_copy_buffer_rcl)` (**:1503**).
  `items_copied = w*h`, `bytes_copied = items_copied * item_size` (**:1508-1509**).
- `v3dv_queue.c:751-759` maps the job to the ioctl: `rcl_start = job->rcl.bo->offset`,
  `rcl_end = rcl_start + v3dv_cl_offset(&job->rcl)`, `qma/qms = tile_alloc`, `qts = tile_state`.

**The src/dst addresses are NOT in the RCL.** `emit_copy_buffer_per_tile_list`
(**:766-800**) emits the load/store into **`job->indirect`** (a separate CL/BO, **:775**) and the RCL
only gets `START_ADDRESS_OF_GENERIC_TILE_LIST { start, end }` pointing at it (**:796-799**). Any
decoder must follow that pointer.

The addresses are carried by:

| packet | opcode | length | address field |
| --- | --- | --- | --- |
| `LOAD_TILE_BUFFER_GENERAL` | **30 (0x1e)** | 13 B | `Address`, 32 bits, `start="64"` → bytes 9..12 (`v3d_packet.xml:528-558`) |
| `STORE_TILE_BUFFER_GENERAL` | **29 (0x1d)** | 13 B | `Address`, 32 bits, `start="64"` → bytes 9..12 (`v3d_packet.xml:491-526`) |

set by `emit_linear_load` (**:272-288**, `memory_format = V3D_TILING_RASTER`,
`height_in_ub_or_stride = stride`, `buffer_to_load = RENDER_TARGET_0`) and `emit_linear_store`
(**:290-309**, same, `clear_buffer_being_stored = false`). The **extent** is not in either packet: it
comes from `TILE_RENDERING_MODE_CFG_COMMON`'s `Image Width/Height (pixels)`
(`v3d_packet.xml:1018-1019`), and the stride from the load/store's own 20-bit
`Height in UB or Stride` field.

`cl_packet_length = max(field.end / 8) + 1` with every declared `start` shifted up by 8 for the
opcode byte (`src/broadcom/cle/gen_pack_header.py:106-111`, `:228`). Hence: TRMC\* 9, TBMC(v42) 9,
`NUMBER_OF_LAYERS` 2, `TILE_LIST_INITIAL_BLOCK_SIZE` 2, `MULTICORE_RENDERING_TILE_LIST_SET_BASE` 5,
`MULTICORE_RENDERING_SUPERTILE_CFG` 9, `TILE_COORDINATES` 4, `SUPERTILE_COORDINATES` 3,
`START_ADDRESS_OF_GENERIC_TILE_LIST` 9, `BRANCH_TO_IMPLICIT_TILE_LIST` 2, LOAD/STORE 13, and 1 byte
each for `FLUSH`(4), `START_TILE_BINNING`(6), `END_OF_RENDERING`(13), `RETURN_FROM_SUB_LIST`(18),
`FLUSH_VCD_CACHE`(19), `END_OF_LOADS`(26), `END_OF_TILE_MARKER`(27), `TILE_COORDINATES_IMPLICIT`(125).

### 2.3 The exact byte layout — verified against a real recorded wedge

From `artifacts/rpi4b-uart/rpi4b-uart-20260903-200200-vkq-mutex-T8.log`, a drop whose RCL decoded
cleanly (`n=101`, `park_off=0xffffffff`, i.e. a **bin** timeout so CT1 never ran):

```
79 00 b7 00 01 00 40 00 00 | 79 41 00 00 00 00 00 00 00 | 79 02 00 00 00 80 3f 00 00 | 7e 05 |
7b 00 10 cd 0a | 7a 00 00 03 01 03 10 00 00 | 7c 00 00 00 | 1a |
1d 08 00 00 00 00 00 00 00 00 00 00 00 | 1b | 7c 00 00 00 | 1a |
1d 08 00 00 00 00 00 00 00 00 00 00 00 | 1b | 13 |
14 00 60 cd 0a 20 60 cd 0a | 17 00 00 | 17 01 00 | 17 02 00 | 0d
```

| off | bytes | packet (source) |
| --- | --- | --- |
| `0x00` | 9 | `TILE_RENDERING_MODE_CFG_COMMON` sub-id 0, **w=0x00b7=183, h=1**, nRT=1, early-Z disable (`v3dvx_meta_common.c:59-74`) |
| `0x09` | 9 | `…CFG_COLOR` sub-id 1 (`:133-137`) |
| `0x12` | 9 | `…CFG_ZS_CLEAR_VALUES` sub-id 2, z = `0x3f800000` = 1.0f (`:173-176`) |
| `0x1b` | 2 | `TILE_LIST_INITIAL_BLOCK_SIZE` (`:178-182`) |
| `0x1d` | 5 | `MULTICORE_RENDERING_TILE_LIST_SET_BASE` → tile_alloc (`:201-203`) |
| `0x22` | 9 | `MULTICORE_RENDERING_SUPERTILE_CFG` (`:205-217`) |
| `0x2b` | 4 | `TILE_COORDINATES` — GFXH-1742 pass 1 (`:222-244`) |
| `0x2f` | 1 | `END_OF_LOADS` |
| `0x30` | 13 | `STORE_TILE_BUFFER_GENERAL`, **buffer_to_store = 8 (NONE)**, address 0 |
| `0x3d` | 1 | `END_OF_TILE_MARKER` |
| `0x3e` | 4 | `TILE_COORDINATES` — GFXH-1742 pass 2 |
| `0x42` | 1 | `END_OF_LOADS` |
| **`0x43`** | 13 | **`STORE_TILE_BUFFER_GENERAL`, buffer_to_store = NONE, address 0 ← the measured park offset** |
| `0x50` | 1 | `END_OF_TILE_MARKER` |
| `0x51` | 1 | `FLUSH_VCD_CACHE` (`:246`) |
| `0x52` | 9 | `START_ADDRESS_OF_GENERIC_TILE_LIST` **start=0x0acd6000 end=0x0acd6020 → exactly 32 B** |
| `0x5b` | 3×3 | `SUPERTILE_COORDINATES` × 3 (`:262-268`) |
| `0x64` | 1 | `END_OF_RENDERING` |
| | **0x65 = 101 B** | ✔ matches `rcl=[0x0acd5000..0x0acd5065] 101 B` |

So `rcl_len = 92 + 3n`. All four RCL sizes on record fit: 98 (n=2), 101 (n=3), 122 (n=10),
143 (n=17). And the generic tile list is **exactly 32 bytes** —
`TILE_COORDINATES_IMPLICIT(1) + LOAD(13) + END_OF_LOADS(1) + BRANCH_TO_IMPLICIT_TILE_LIST(2) +
STORE(13) + END_OF_TILE_MARKER(1) + RETURN_FROM_SUB_LIST(1) = 32` — which the observed
`start=0x0acd6000, end=0x0acd6020` confirms independently.

The BCL is `NUMBER_OF_LAYERS(2) + TILE_BINNING_MODE_CFG(9) + FLUSH_VCD_CACHE(1) +
START_TILE_BINNING(1)` from `v3dX(job_emit_binning_prolog)` (`v3dvx_cmd_buffer.c:82-121`) `+ FLUSH(1)`
from `v3dX(job_emit_binning_flush)` (`:38-46`) = **exactly 14 bytes**, matching every
`DROPPED job … bcl=[…] 14 B` line on record. **No primitive is ever binned**, so every per-tile list
is empty, the `BRANCH_TO_IMPLICIT_TILE_LIST` returns immediately, and the tile buffer holds exactly
the bytes the LOAD brought in. **That is the property that makes a CPU `memcpy` semantically
identical to the job** — and it lives in the BCL, not the RCL.

### 2.4 Consequence for the record: the in-code diagnosis at :1376-1389 needs correcting

The park at `0x43` is a `STORE_TILE_BUFFER_GENERAL` with `buffer_to_store = NONE` and address 0 —
the second half of Mesa's GFXH-1742 workaround, emitted identically by upstream on every Linux Pi 4.
It is *before* the generic tile list, so **the copy never begins**. The list is well-formed; what
wedges is this port's tile-store path on a `w × 1` raster frame. That is consistent with the file's
own repeated finding of an "RT coherency wall" (`:1296-1300`) and with R2's unaligned/byte-strided
raster store (`w=183, item_size=1` here — an odd width, exactly the class the torch analysis
predicted). It is *not* evidence that Mesa emitted something illegal.

---

## 3. Task 4 — the patch

Design decisions, stated before the code:

- **Wedge-path only, not submit-entry.** Emulating *every* copy at entry is tempting (it would remove
  the wedge and the ~0.8-1.6 s hitch entirely), but BOs are `MAP_UNCACHED` (**:685-687**) and vkQuake
  re-flushes staging **every frame** through this same path (torch doc §4.2c). A CPU `memcpy` over
  uncached DRAM on the A72 is orders slower than the GPU; doing it unconditionally risks
  milliseconds per frame. Wedge-path emulation has **zero happy-path cost**. Entry-time emulation is
  a measured follow-up (§7.3).
- **Structural decode, not a byte-size heuristic.** The existing `< 256 B` test (**:1370**) is a
  log-annotation heuristic and is not safe to act on. The classifier below requires the BCL to decode
  to exactly the 5-packet no-bin prolog, the RCL to decode to exactly the copy layout with no
  addressed store, and the 32-byte generic tile list to be exactly `LOAD(RASTER,RT0) → STORE(RASTER,
  RT0)` with equal format and equal stride. A real render pass that happens to load and store a
  raster colour attachment (the scanout RT *is* raster on this port, `:654-675`) fails the **BCL**
  test, which is the whole point of checking it.
- **Still `return 0`.** See §3.4.

### 3.1 New file-scope helpers (place after `gpuva_describe`, i.e. after :853)

```c
/* --- CLE decode for the dropped-one-shot-copy rescue -------------------------------------
 * V3D 4.2 CLE opcodes and packet lengths (bytes, INCLUDING the opcode byte). Transcribed from
 * mesa external/mesa/src/broadcom/cle/v3d_packet.xml; lengths follow gen_pack_header.py
 * (length = max(field.end/8) + 1, every declared field start shifted up by 8 for the opcode). */
#define CLOP_END_OF_RENDERING     0x0du  /* 13 */
#define CLOP_FLUSH                0x04u  /*  4 */
#define CLOP_START_TILE_BINNING   0x06u  /*  6 */
#define CLOP_RETURN_FROM_SUBLIST  0x12u  /* 18 */
#define CLOP_FLUSH_VCD_CACHE      0x13u  /* 19 */
#define CLOP_GENERIC_TILE_LIST    0x14u  /* 20 */
#define CLOP_BRANCH_IMPLICIT_TILE 0x15u  /* 21 */
#define CLOP_SUPERTILE_COORDS     0x17u  /* 23 */
#define CLOP_END_OF_LOADS         0x1au  /* 26 */
#define CLOP_END_OF_TILE          0x1bu  /* 27 */
#define CLOP_STORE_TILE_GENERAL   0x1du  /* 29 */
#define CLOP_LOAD_TILE_GENERAL    0x1eu  /* 30 */
#define CLOP_NUMBER_OF_LAYERS     0x77u  /* 119 */
#define CLOP_TILE_BINNING_MODE    0x78u  /* 120 */
#define CLOP_TILE_RENDER_MODE     0x79u  /* 121, all sub-ids */
#define CLOP_SUPERTILE_CFG        0x7au  /* 122 */
#define CLOP_TILE_LIST_SET_BASE   0x7bu  /* 123 */
#define CLOP_TILE_COORDS          0x7cu  /* 124 */
#define CLOP_TILE_COORDS_IMPLICIT 0x7du  /* 125 */
#define CLOP_TILE_LIST_BLOCKSIZE  0x7eu  /* 126 */

#define CLLEN_TILE_RENDER_MODE    9u
#define CLLEN_TILE_BINNING_MODE   9u
#define CLLEN_NUMBER_OF_LAYERS    2u
#define CLLEN_TILE_LIST_BLOCKSIZE 2u
#define CLLEN_TILE_LIST_SET_BASE  5u
#define CLLEN_SUPERTILE_CFG       9u
#define CLLEN_TILE_COORDS         4u
#define CLLEN_SUPERTILE_COORDS    3u
#define CLLEN_GENERIC_TILE_LIST   9u
#define CLLEN_BRANCH_IMPL_TILE    2u
#define CLLEN_LOADSTORE          13u
#define CLLEN_COPY_TILE_LIST     32u   /* 1+13+1+2+13+1+1 */
#define CLLEN_COPY_BCL           14u   /* 2+9+1+1+1 */
#define CLLEN_COPY_RCL_FIXED     92u   /* rcl_len == 92 + 3 * n_supertiles */

/* Output Image Format values the buffer-copy path can select (v3d_packet.xml:261-263). */
#define OIF_RGBA8UI 34u
#define OIF_RG8UI   35u
#define OIF_R8UI    36u
#define MEMFMT_RASTER 0u    /* V3D_TILING_RASTER */
#define BUF_RT0       0u
#define BUF_NONE      8u

/* aarch64 is little-endian and CLE packets are LSB-first byte streams, so these are plain
 * unaligned little-endian loads — no byte swapping anywhere in this decoder. Read byte-wise:
 * a 13-byte packet is not 4-byte aligned, and its 20-bit stride field is not byte-aligned. */
static inline uint32_t cl_rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t cl_rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* LOAD/STORE_TILE_BUFFER_GENERAL field accessors. Declared starts are shifted up by 8, so the
 * payload begins at p[1]: buffer 4b@0, memory_format 3b@4, output/input_image_format 6b@12,
 * clear_buffer_being_stored 1b@18, height_in_ub_or_stride 20b@28, address 32b@64. */
static inline uint32_t ls_buffer(const uint8_t *p) { return p[1] & 0x0fu; }
static inline uint32_t ls_memfmt(const uint8_t *p) { return (p[1] >> 4) & 0x07u; }
static inline uint32_t ls_format(const uint8_t *p) { return (cl_rd16(&p[2]) >> 4) & 0x3fu; }
static inline uint32_t ls_clearing(const uint8_t *p) { return (p[3] >> 2) & 0x01u; }
static inline uint32_t ls_stride(const uint8_t *p) { return (cl_rd32(&p[4]) >> 4) & 0xfffffu; }
static inline uint32_t ls_address(const uint8_t *p) { return cl_rd32(&p[9]); }
/* TILE_RENDERING_MODE_CFG_COMMON: sub-id 4b@0, image_width 16b@8, image_height 16b@24.
 * NOTE none of these are minus_one fields (unlike TILE_BINNING_MODE_CFG's width/height, which
 * ARE — that asymmetry is why this decoder never reads TBMC's fields, only its opcode). */
static inline uint32_t trmc_subid(const uint8_t *p) { return p[1] & 0x0fu; }
static inline uint32_t trmc_width(const uint8_t *p) { return cl_rd16(&p[2]); }
static inline uint32_t trmc_height(const uint8_t *p) { return cl_rd16(&p[4]); }

struct cl_bufcopy {
	uint32_t src_va, dst_va;
	uint32_t w, h, cpp, stride, bytes;
};

static uint32_t cl_cpp_for_format(uint32_t fmt)
{
	return (fmt == OIF_RGBA8UI) ? 4u : (fmt == OIF_RG8UI) ? 2u : (fmt == OIF_R8UI) ? 1u : 0u;
}
```

### 3.2 The classifier

```c
/* Decode (bcl, rcl) and return 1 iff this job is EXACTLY the control-list pair V3DV emits for
 * vkCmdCopyBuffer on V3D 4.2 (external/mesa/src/broadcom/vulkan/v3dvx_meta_common.c:1445-1516):
 * a raster TLB load of the source into RT0 followed by a raster store to the destination, with
 * NOTHING binned. Because the BCL binds no primitive, the tile buffer holds exactly the loaded
 * bytes, so store == load == memcpy of w*h*cpp bytes.
 *
 * Everything is checked, nothing is assumed: opcode sequence and total length on BOTH lists, the
 * 32-byte generic tile list, RASTER memory format on both sides (this is what excludes every
 * buffer<->image copy, whose store is UIF/LINEARTILE), equal formats, and stride == w*cpp. On
 * rejection *why names the first thing that did not match, at its offset. */
static int cl_decode_buffer_copy(const struct drm_v3d_submit_cl *s,
                                 struct cl_bufcopy *c, const char **why)
{
	static char reason[96];
	uint32_t blen = s->bcl_end - s->bcl_start;
	uint32_t rlen = s->rcl_end - s->rcl_start;
	const uint8_t *b, *r, *t;
	uint32_t o, tl_start = 0, tl_end = 0, ngen = 0, nst = 0;
	uint32_t w = 0, h = 0;

	*why = reason;

	/* (0) cheap size gates: the copy BCL is exactly 14 B and the copy RCL is 92 + 3n. */
	if (blen != CLLEN_COPY_BCL) {
		snprintf(reason, sizeof(reason), "bcl len %u != %u", blen, CLLEN_COPY_BCL);
		return 0;
	}
	if (rlen < CLLEN_COPY_RCL_FIXED + 3u || rlen > 4096u ||
	    ((rlen - CLLEN_COPY_RCL_FIXED) % 3u) != 0u) {
		snprintf(reason, sizeof(reason), "rcl len %u not 92+3n", rlen);
		return 0;
	}
	b = (const uint8_t *)gpuva_to_cpu(s->bcl_start);
	r = (const uint8_t *)gpuva_to_cpu(s->rcl_start);
	if (b == NULL || r == NULL ||
	    gpuva_bo_remaining(s->bcl_start) < blen || gpuva_bo_remaining(s->rcl_start) < rlen) {
		snprintf(reason, sizeof(reason), "list not fully inside a live BO");
		return 0;
	}

	/* (1) BCL: NOTHING is binned. v3dvx_cmd_buffer.c:82-121 + :38-46. Opcode match only --
	 * TILE_BINNING_MODE_CFG's width/height are minus_one-encoded and are not needed. */
	{
		static const uint8_t want[5] = { CLOP_NUMBER_OF_LAYERS, CLOP_TILE_BINNING_MODE,
			CLOP_FLUSH_VCD_CACHE, CLOP_START_TILE_BINNING, CLOP_FLUSH };
		static const uint8_t len[5] = { CLLEN_NUMBER_OF_LAYERS, CLLEN_TILE_BINNING_MODE,
			1u, 1u, 1u };
		unsigned i;
		for (o = 0, i = 0; i < 5u; i++) {
			if (b[o] != want[i]) {
				snprintf(reason, sizeof(reason), "bcl op 0x%02x at 0x%x (want 0x%02x) -- "
					"something was binned", b[o], o, want[i]);
				return 0;
			}
			o += len[i];
		}
		if (o != blen) { snprintf(reason, sizeof(reason), "bcl walk %u != %u", o, blen); return 0; }
	}

	/* (2) RCL: the fixed copy layout. Any opcode outside this set, any ADDRESSED store, or any
	 * load, rejects -- those belong to a render pass, not a meta-copy. */
	for (o = 0; o < rlen; ) {
		uint32_t op = r[o], plen;
		switch (op) {
		case CLOP_TILE_RENDER_MODE:
			plen = CLLEN_TILE_RENDER_MODE;
			if (trmc_subid(&r[o]) == 0u) { w = trmc_width(&r[o]); h = trmc_height(&r[o]); }
			break;
		case CLOP_TILE_LIST_BLOCKSIZE: plen = CLLEN_TILE_LIST_BLOCKSIZE; break;
		case CLOP_TILE_LIST_SET_BASE:  plen = CLLEN_TILE_LIST_SET_BASE;  break;
		case CLOP_SUPERTILE_CFG:       plen = CLLEN_SUPERTILE_CFG;       break;
		case CLOP_TILE_COORDS:         plen = CLLEN_TILE_COORDS;         break;
		case CLOP_SUPERTILE_COORDS:    plen = CLLEN_SUPERTILE_COORDS;    break;
		case CLOP_END_OF_LOADS:
		case CLOP_END_OF_TILE:
		case CLOP_FLUSH_VCD_CACHE:
		case CLOP_END_OF_RENDERING:    plen = 1u;                        break;
		case CLOP_STORE_TILE_GENERAL:
			plen = CLLEN_LOADSTORE;
			/* only the two GFXH-1742 no-op stores (buffer NONE, address 0) may appear here */
			if (ls_buffer(&r[o]) != BUF_NONE || ls_address(&r[o]) != 0u) {
				snprintf(reason, sizeof(reason), "rcl has an ADDRESSED store at 0x%x", o);
				return 0;
			}
			nst++;
			break;
		case CLOP_GENERIC_TILE_LIST:
			plen = CLLEN_GENERIC_TILE_LIST;
			tl_start = cl_rd32(&r[o + 1]);
			tl_end   = cl_rd32(&r[o + 5]);
			ngen++;
			break;
		default:
			snprintf(reason, sizeof(reason), "rcl op 0x%02x at 0x%x not in copy set", op, o);
			return 0;
		}
		o += plen;
	}
	if (o != rlen || ngen != 1u || nst != 2u || r[rlen - 1u] != CLOP_END_OF_RENDERING) {
		snprintf(reason, sizeof(reason), "rcl shape walk=%u/%u gen=%u nullstores=%u",
			o, rlen, ngen, nst);
		return 0;
	}
	if (w == 0u || h == 0u) { snprintf(reason, sizeof(reason), "no TRMC extent"); return 0; }

	/* (3) the 32-byte generic tile list: implicit coords, RASTER load, RASTER store, return. */
	if (tl_end - tl_start != CLLEN_COPY_TILE_LIST) {
		snprintf(reason, sizeof(reason), "tile list %u B != 32", tl_end - tl_start);
		return 0;
	}
	t = (const uint8_t *)gpuva_to_cpu(tl_start);
	if (t == NULL || gpuva_bo_remaining(tl_start) < CLLEN_COPY_TILE_LIST) {
		snprintf(reason, sizeof(reason), "tile list not inside a live BO");
		return 0;
	}
	if (t[0] != CLOP_TILE_COORDS_IMPLICIT || t[1] != CLOP_LOAD_TILE_GENERAL ||
	    t[14] != CLOP_END_OF_LOADS || t[15] != CLOP_BRANCH_IMPLICIT_TILE ||
	    t[17] != CLOP_STORE_TILE_GENERAL || t[30] != CLOP_END_OF_TILE ||
	    t[31] != CLOP_RETURN_FROM_SUBLIST) {
		snprintf(reason, sizeof(reason), "tile list not LOAD->STORE "
			"(%02x %02x %02x %02x %02x %02x %02x)",
			t[0], t[1], t[14], t[15], t[17], t[30], t[31]);
		return 0;
	}
	{
		const uint8_t *ld = &t[1], *st = &t[17];
		uint32_t fmt = ls_format(ld), cpp = cl_cpp_for_format(fmt), stride = ls_stride(ld);
		if (ls_memfmt(ld) != MEMFMT_RASTER || ls_memfmt(st) != MEMFMT_RASTER) {
			snprintf(reason, sizeof(reason), "not raster<-raster (ld %u st %u) -- image copy",
				ls_memfmt(ld), ls_memfmt(st));
			return 0;
		}
		if (ls_buffer(ld) != BUF_RT0 || ls_buffer(st) != BUF_RT0 || ls_clearing(st) != 0u) {
			snprintf(reason, sizeof(reason), "load/store not plain RT0"); return 0;
		}
		if (ls_format(st) != fmt || cpp == 0u || ls_stride(st) != stride) {
			snprintf(reason, sizeof(reason), "format/stride mismatch (ld %u/%u st %u/%u)",
				fmt, stride, ls_format(st), ls_stride(st));
			return 0;
		}
		/* the invariant that ties the extent to the bit offsets above: stride == w * cpp
		 * (v3dvx_meta_common.c:812). If this fails, one of the two decodes is wrong -- refuse. */
		if (stride != w * cpp) {
			snprintf(reason, sizeof(reason), "stride %u != w*cpp (%u*%u)", stride, w, cpp);
			return 0;
		}
		c->src_va = ls_address(ld);
		c->dst_va = ls_address(st);
		c->w = w; c->h = h; c->cpp = cpp; c->stride = stride;
		c->bytes = w * h * cpp;      /* == h * stride; w*h <= region items, :1361 */
	}
	*why = "ok";
	return 1;
}
```

### 3.3 The rescue, spliced into the drop path

Counter next to the existing ones (near **:937**):

```c
/* How many dropped one-shot buffer copies were completed on the CPU instead of being lost.
 * Exported so a #67 pass-rate bench can correlate emulations with the torch verdict. */
volatile unsigned v3d_phoenix_copy_emulations = 0;
volatile unsigned v3d_phoenix_copy_rejects = 0;
```

Insert immediately **after** `reset_reinit_core();` (**:1373**) — the reset must come first because
`apply_core_regs()` re-establishes `L2TFLSTA/L2TFLEND` and the MMU, and it does not disturb DRAM or
the BO table, so the source bytes are still there:

```c
		reset_reinit_core();   /* clean the wedged core so the next (different) frame renders */
		(void)attempt;

		/* RESCUE. The drop above is a sound trade for a frame that will be redrawn next tick,
		 * but on V3D 4.2 vkCmdCopyBuffer is also a CL RENDER job (the TFU fast path is
		 * #if V3D_VERSION >= 71 in v3dvx_meta_common.c:1376-1443), so this path can swallow a
		 * ONE-SHOT buffer upload, and there a drop is permanent: the destination keeps the zeros
		 * ioc_create_bo memset in (l.701) for the life of the process. That is the leading
		 * explanation for #67's per-boot-latched missing torch geometry.
		 *
		 * Re-submitting does not work -- HW-measured, 3/3 attempts re-wedge at the identical
		 * offset on a freshly reset core (see the note below). So COMPLETE THE COPY ON THE CPU.
		 * That is legitimate here and nowhere else: cl_decode_buffer_copy proves the BCL binned
		 * NOTHING, so the tile buffer would have held exactly the bytes the RASTER load brought
		 * in, and the RASTER store of those same bytes is a memcpy. Precedent: the TFU CPU-tile
		 * path already populates a destination image on the CPU and skips the hardware kick
		 * (l.1509-1532). */
		{
			struct cl_bufcopy cp;
			const char *why = "?";
			if (cl_decode_buffer_copy(s, &cp, &why)) {
				uint8_t *sp = (uint8_t *)gpuva_to_cpu(cp.src_va);
				uint8_t *dp = (uint8_t *)gpuva_to_cpu(cp.dst_va);
				uint32_t sroom = gpuva_bo_remaining(cp.src_va);
				uint32_t droom = gpuva_bo_remaining(cp.dst_va);
				if (sp != NULL && dp != NULL && sroom >= cp.bytes && droom >= cp.bytes) {
					/* memmove, not memcpy: vkCmdCopyBuffer forbids overlapping regions, but
					 * overlap here would be silent corruption and memmove costs nothing.
					 * Byte-wise on purpose -- item_size 1 puts both ends at arbitrary
					 * alignment (this job: w=183, cpp=1). */
					memmove(dp, sp, cp.bytes);
					/* Drain the stores to the point the V3D (a non-coherent external AXI
					 * master) observes. MUST be dsb (completion), not dmb -- identical
					 * reasoning to the submit prologue at l.998-1020 and the TFU CPU-tile
					 * barrier at l.1514. */
					__asm__ volatile("dsb sy" ::: "memory");
					/* Read-side epilogue, verbatim from the TFU CPU-tile fallback
					 * (l.1519-1522): clean L2T to RAM and WAIT (GFXH-1897), then drop the
					 * read-only slice/TMU view of this GPU VA so the next job cannot serve a
					 * stale line for it. No TMU write-combiner drain -- no GPU unit wrote. */
					l2t_flush_wait(c0);
					c0[CTL_L2TCACTL/4] = L2TCACTL_L2TFLS | L2TCACTL_FLM_CLEAN;
					l2t_flush_wait(c0);
					c0[CTL_SLCACTL/4] = SLCACTL_INVAL_ALL;
					v3d_phoenix_copy_emulations++;
					fprintf(stderr, "v3d-winsys: COPY RESCUED on CPU %u B "
						"src=0x%08x -> dst=0x%08x (%ux%u cpp=%u stride=%u) n=%u\n",
						cp.bytes, cp.src_va, cp.dst_va, cp.w, cp.h, cp.cpp, cp.stride,
						v3d_phoenix_copy_emulations);
				}
				else {
					v3d_phoenix_copy_rejects++;
					fprintf(stderr, "v3d-winsys: COPY RESCUE ABORTED %u B src=0x%08x(%p,%u) "
						"dst=0x%08x(%p,%u) -- out of BO bounds\n", cp.bytes,
						cp.src_va, (void *)sp, sroom, cp.dst_va, (void *)dp, droom);
				}
			}
			else {
				/* Not a buffer copy (or not decodable). Say WHY -- this turns the RCLBYTES hex
				 * dump above into a decoder and makes an unknown drop diagnosable. About half
				 * the drops on record land here with an RCL that is RGBA pixel data, which is a
				 * DIFFERENT defect; see docs/misc/2026-09-03-v3d-dropped-metacopy-fix-plan.md. */
				v3d_phoenix_copy_rejects++;
				fprintf(stderr, "v3d-winsys: DROP not an emulatable copy: %s\n", why);
			}
		}
```

Also worth two lines: record cacheability per BO so the rescue can refuse a cacheable destination
(a cacheable dst would need `dc cvac` per line, which this patch does not do). In `struct pbo`
(**:261-269**) add `int cacheable;` and in `ioc_create_bo` set
`b->cacheable = ((c->flags & 0x1u) != 0u);` next to **:716**; then reject in the rescue. Today V3DV
never passes `V3D_CREATE_BO_CACHEABLE` — only the gallium GL driver defines and uses it
(`external/mesa/src/gallium/drivers/v3d/v3d_bufmgr.h:73`, `v3d_bufmgr.c:130`) — so this is
future-proofing, not a live bug.

### 3.4 Task 4, the return value — answered directly

- **After a real emulated copy: keep `return 0` (:1394).** It is now truthful: the transfer the job
  described actually happened, in order (submits are synchronous and serialized by
  `v3d_submit_lock`, **:76-121**), before the ioctl returned. Nothing above needs to know which
  engine did it — exactly the contract the TFU CPU-tile path already relies on
  (`return 0; /* image populated by the CPU; do NOT kick the TFU */`, **:1532**).
- **For a dropped RENDER frame: also keep `return 0`.** An error propagates through V3DV to
  `VK_ERROR_DEVICE_LOST` and aborts the application; a frame that will be redrawn next tick is
  strictly better lost silently than escalated into a process kill. This **supersedes** the torch
  doc's E4 item 2 ("return `-EIO`") — that suggestion predates the measurement that the retry itself
  fails and it prices a dropped redrawable frame the same as a dropped one-shot upload. If a test
  harness wants the loud behaviour, gate it: `if (getenv("V3D_STRICT_DROP")) return -EIO;` — off by
  default.
- **For a decode reject: `return 0` and the new one-line reason.** We cannot tell whether it was a
  frame or a transfer, so the safe behaviour is unchanged, plus a diagnostic.

---

## 4. Task 5 — correctness traps

1. **Endianness — none.** aarch64 is little-endian and CLE packets are LSB-first byte streams
   (`gen_pack_header.py` emits `v |= …; dst[i] = v >> (8*i)`). All reads are plain LE. Do not add
   swapping.
2. **Alignment — real, and the reason the decoder is byte-wise.** A 13-byte LOAD/STORE inside a
   32-byte tile list is not 4-byte aligned, its 20-bit stride field is not byte-aligned, and with
   `item_size == 1` (the R2 class — the decoded example is `w=183, cpp=1`) both copy endpoints can be
   at any byte offset. Never cast a packet or a copy endpoint to `uint32_t *`; `cl_rd16/cl_rd32` and
   `memmove` are byte-safe.
3. **Tiled vs raster.** Only `memory_format == RASTER` **on both the load and the store** is a memcpy.
   `vkCmdCopyBufferToImage`'s TLB path (`emit_copy_buffer_to_layer_per_tile_list`,
   `v3dvx_meta_common.c:1182-1306`) stores UIF/LINEARTILE and has a different packet sequence, so it
   is rejected twice over. Do **not** try to extend this to image copies — that is the
   `uif_pixel_off` tiler's job (**:1402-1420**) and it is `cpp == 4` UIF only.
4. **Overlap.** Vulkan forbids overlapping `vkCmdCopyBuffer` regions, so this should not arise, but
   `memmove` makes it harmless instead of corrupting.
5. **Extent.** `bytes = w * h * cpp` and the checked invariant `stride == w * cpp` make it also
   `h * stride`. `framebuffer_size_for_pixel_count` guarantees `w*h <= num_items` (asserted at
   `v3dvx_meta_common.c:1361`) and the caller advances by `w*h*item_size` per job
   (**:1508-1512**), so per-job emulation of exactly `w*h*cpp` contiguous bytes is complete and
   non-overlapping across a split copy. Multi-region `vkCmdCopyBuffer2` calls are independent jobs
   (**:1832-1837**), so no cross-job bookkeeping is needed.
6. **Cache coherency.** V3DV BOs are `MAP_UNCACHED` (**:685-687**), so no CPU cache maintenance is
   required — only the `dsb sy` write drain, which is exactly why the file already uses `dsb` and not
   `__sync_synchronize()` (see the long justification at **:998-1020**). The `L2TFLS|FLM_CLEAN` +
   `SLCACTL_INVAL_ALL` epilogue is copied from the TFU CPU-tile fallback (**:1519-1522**); it is
   belt-and-braces here (the next submit's prologue re-invalidates at **:1035-1076**) but it costs
   nothing on a path that already paid for a full core reset, and it keeps the two CPU-produces-data
   paths symmetric. A **cacheable** destination would additionally need `dc cvac` per line — refuse
   it (§3.3) rather than get it subtly wrong.
7. **Ordering vs the reset.** Do the copy *after* `reset_reinit_core()`; `apply_core_regs()` is what
   re-arms `L2TFLSTA/L2TFLEND` (**:915-916**) and the MMU, and the reset touches neither DRAM nor the
   BO table.
8. **Idempotence on a mid-list render wedge.** If CT1 had already stored some tiles before wedging,
   re-copying the whole range writes the same source bytes again — idempotent. (On a **bin** timeout,
   `goto job_retry` at **:1191-1192** means CT1 never ran at all, so this does not even arise; 11 of
   the 27 decodable dumps on record are that case, identifiable by `park_off=0xffffffff`.)
9. **`gpuva_to_cpu` is first-match.** Use `gpuva_describe` output (already printed at **:1339-1340**)
   to confirm single-match; the bounds checks via `gpuva_bo_remaining` cap the damage if a VA is ever
   ambiguous.
10. **`snprintf` in the reject path** writes into a `static char` buffer. The drop path is inside
    `v3d_submit_lock` (**:1884-1900**), so it is serialized; do not move it outside the lock.

---

## 5. What this fix does and does not buy

**Does:** for a dropped job that decodes as a pure buffer copy, the destination gets the correct
bytes instead of staying zero. If R1 in the torch analysis is right, that removes the #67 mechanism
for that class of drop and converts a permanent, per-boot-latched geometry loss into a one-off
~1 s hitch. It also makes every non-copy drop *self-describing* in the UART log for the first time.

**Does not:** it does not stop the wedge (the copy job still burns a full timeout + reset), and it
does not help the ~half of recorded drops whose RCL is not a control list (§6). Expect the #67 rate
to improve but not necessarily to zero.

---

## 6. The second failure mode — and why it may matter more

56 `RCLBYTES` dumps exist across `artifacts/rpi4b-uart/*.log`. Grouped by first byte and park offset:

| first byte | park_off | count | reading |
| --- | --- | --- | --- |
| `0x79` | `0xffffffff` | 11 | valid RCL, CT1 never started → **bin** timeout; fully emulatable |
| `0x79` | `0x43` | 4 | valid RCL, parked on the 2nd GFXH-1742 dummy store; emulatable |
| `0x79` | `0x1000`(4) / `0xf6329f2c`(3) / `0x70`(2) / `0x1001`(1) | 10 | valid RCL, park outside/odd; still emulatable (the decode never uses the park) |
| `0x74`(13), `0x2a`(11), `0xff`(3), `0x90`(2), `0x00`(2) | `0x0` for 29 of 31 | 31 | **RCL is RGBA pixel data or zeros** — CT1 stopped on byte 0 of a non-list |

(25 decodable + 31 non-decodable = 56. Reproduce with
`grep -h RCLBYTES artifacts/rpi4b-uart/*.log | sed 's/.*park_off=\(0x[0-9a-f]*\): >*\([0-9a-f][0-9a-f]\).*/park=\1 first=\2/' | sort | uniq -c | sort -rn`.)

Example (`rpi4b-uart-20260903-200200-vkq-mutex-T8.log`):

```
DROP-RCLSTART gpuva=0x0acd5000 -> BO handle=380 base=0x0acd5000 size=4096 off=0x0 cpu=0x13bfc000
RCLBYTES gpuva=0x0acd5000 n=101 park_off=0x0: >74 56 30 ff 00 00 00 ff 00 00 00 ff …
```

A single, unambiguous BO whose base *is* `rcl_start`, and its first bytes are RGBA texels
(every 4th byte `0xff`). Since `DRM_V3D_MMAP_BO` hands Mesa this very pointer (**:1938-1947**), this
is not an instrumentation artifact: **the control list Mesa built is not in the buffer Mesa
submitted.** `0x74` is not a defined CLE opcode (`v3d_packet.xml` has no `code="116"`), so the CLE stops at
byte 0 with `int_sts=0` — which is exactly the reported signature. `rcl_end - rcl_start` is still `92 + 3n`, so V3DV's *bookkeeping*
was right; only the *content* is wrong. Candidates worth one instrumented boot: a `v3dv_bo` cache
recycle handing one winsys handle to two logical objects, the double `munmap` of the same address
(V3DV's `v3dv_bo_free` and our `ioc_close_bo` at **:743** both unmap `b->cpu`), or a BO closed and
its VA re-issued (`va_free` → first-fit `va_alloc`, **:557-588**) while a built submit still names it.
This is a genuine silent-corruption bug independent of the drop mitigation, and it is not fixed here.

---

## 7. Follow-ups, cheapest first

**7.1 Entry-time RCL validity check (near-free, decisive for §6).** At the top of `ioc_submit_cl`,
before the CT0 kick, read `r[0]` and reject-log if it is not a plausible RCL opener
(`0x79`/`0x7c`/`0x7e`). Two byte reads per submit. If the RCL is *already* pixel data at entry, the
corruption predates the GPU (a Mesa/BO-lifetime bug); if it is valid at entry and pixels at wedge
time, something overwrote it during the job. One boot settles it, no rebuild of anything but the GPU
lib.

**7.2 Reuse the decoder in the diagnostics, and dump the tile list.** Print the decoded copy
descriptor (src/dst/bytes) alongside `DROPPED job` even when the rescue is disabled, so the "is
`0acd5` flame.mdl's upload?" question in torch-doc §4.3 is answered by a dst address instead of
inferred from a VA page. Five more lines: when the RCL decodes, hexdump the **32-byte generic tile
list** too (`gpuva_to_cpu(tl_start)`) — without it a log can never validate stage 3 or recover the
src/dst addresses offline, which is the gap §8 has to work around today.

**7.3 Entry-time emulation, measured.** Emulating every buffer copy on the CPU would remove the
wedge and the reset hitch entirely (and mirrors what upstream does from 7.1 by using the TFU). The
open question is cost: BOs are uncached and vkQuake re-flushes staging every frame. Measure with the
existing counters before considering it; a middle path is to emulate at entry only for the
`cpp == 1` / odd-stride class, which is the class that actually fails and is rare.

**7.4 The real cure for the wedge is still open.** The park at `0x43` is an empty tile store on a
`w × 1` raster frame. Worth probing whether a wider/squarer frame geometry, or `cpp == 4` (i.e.
making vkQuake stage with alignment 4 — torch-doc E2b, a one-line engine change), makes it go away.
That would fix the trigger rather than paper over the loss.

---

## 8. Validation (after the Docker build finishes; none of it was run here)

- **Zero hardware first, but only stages 0-2.** The `RCLBYTES` dump contains the **RCL bytes only**,
  so replaying it by hand validates the size gates, the RCL opcode walk, the extent decode and the
  `tl_end - tl_start == 32` check (the tile-list start/end are inside the dumped bytes) — it
  **cannot** validate stage 3, because the 32-byte indirect tile list is never dumped. Expect:
  stages 0-2 pass on all **25** `0x79` dumps and reject all **31** others. The 101-byte example in
  §2.3 is a worked case with the expected answer `w=183, h=1, tile list 0x0acd6000..0x0acd6020`.
  (Stage 3 and the BCL fingerprint need one instrumented boot — see 7.2.)
- **Then a pass RATE, never a screenshot** (#67 standing rule; 5 prior false closures):
  `./scripts/test-cycle-bench.sh 8 vkq-copyrescue -- "vkquake +map start"` then
  `./scripts/check-torch-rois.py --rate`. Correlate `COPY RESCUED` counts with the verdict.
- Confirm `v3d_phoenix_copy_emulations` is 0 in a GLQuake/gallium run: the GL driver uploads vertex
  data by CPU `memcpy` into a mapped BO and emits no transfer job, so the rescue must never fire
  there.
- Rebuild the GPU lib and **clear the V3D shader disk cache on restage** (standing footgun).
