# Phoenix-RTOS Pi 4 Stage 2: 4 GiB DRAM unlock + GPU memory partition

Stage 2 lifts the kernel's visible RAM from a single 1 GiB slice (the block containing `syspage->pkernel`, plus the PL011 device block) to the full A72-visible DRAM minus firmware/VC4 carve-outs. Stage 1 (caches on, Inner-Shareable Cacheable kernel mappings) is a hard prerequisite — mapping 3 GiB NC is technically possible but bandwidth-useless.

References: `docs/plans/cache-mmu-smp-impl.md` §3, `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` (TD-04, TD-15, TD-16), `tracking/current-step.md`.

Source files (sibling repos): `sources/phoenix-rtos-kernel/hal/aarch64/{_init.S,pmap.c,dtb.c,dtb.h,arch/pmap.h}`, `sources/phoenix-rtos-kernel/vm/page.c`, `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/{board_config.h,config.txt,preinit.plo.yaml}`.

## 1. Memory layout — current vs target

### 1.1 BCM2711 / Pi 4B physical map

Pi 4B SKUs ship as 2, 4, and 8 GiB. The BCM2711 firmware presents:

- 32-bit DMA window at PA `0x0000_0000`–`0x3fff_ffff`, bus alias `0xc0000000` (legacy peripherals).
- 40-bit DMA window across full DRAM, bus alias `0x0` (GENET/PCIe/SDHCI/V3D).
- DRAM banks (4 GiB SKU): `0x0000_0000`–`0x3fff_ffff` and `0x4000_0000`–`0xfbff_ffff` (3 GiB minus top VC carve-out).
- MMIO window `0xfc00_0000`–`0xff7f_ffff`: PL011 at `0xfe20_1000`, mailbox at `0xfe00_b880`, GIC-D at `0xff84_1000`.

The VPU firmware applies `gpu_mem` from `config.txt` and always reserves a framebuffer + atomic-pool window plus the armstub spin-table. Default (Pi 4B firmware ≥ 2020-04): 76 MB; real-world `gpu_mem` ranges 64–512 MB. The reservation sits at the top of ARM-visible DRAM and is exposed via `/memory@*`, `/reserved-memory/*`, and `/soc/dma-ranges` in the firmware-emitted DTB.

### 1.2 What the kernel maps today

`_init.S` lines 341–377 build the temporary TTBR0 table around `syspage->pkernel` plus an explicit block for the running PC and a 1 GiB block for PL011. Lines 484–522 populate the first 2 MiB of kernel high-VA with cacheable TTL3 entries via `_fill_page_descr`. The TD-04 hack at lines 524–558 stamps three TTL3 entries (syspage destination + two bootstrap stack pages) to AttrIdx=1 (Normal Non-Cacheable) for the BCM2711 cache anomaly workaround.

`pmap_common.kernel_ttl2` (`pmap.c` line 105) holds 512 entries; today only the first (covering `VADDR_KERNEL`) and the last (covering `VADDR_DTB` device window) are populated. The remaining 510 entries are zero — any kernel-VA access outside the two endpoint 2 MiB regions translation-faults.

`pmap_common.mem.entries` (line 128) — the frame allocator's bank list — is populated in `_pmap_preinit` (lines 907–929) from `dtb_getMemory()`. On Pi 4 with a sane firmware DTB the entries reflect the full bank list, but the kernel never uses anything beyond the kernel image's load PA and the bootstrap heap because no high-VA mapping exists for the rest.

### 1.3 Target layout

```
high VA                      Stage 2 mapping              physical
-----------------------------------------------------------------------------
0xffffffffffffffff  device window (per-page MAIR_IDX_DEVICE)
0xffffffffffe00000  PL011 + GIC + mailbox               via _pmap_halMapDevice
                    kernel direct map (cached) — lazy populated;
                    TTL2 pre-filled so pmap_enter does not have to
                    allocate TTL3 pages mid-bootstrap
0xffffffffc0000000  kernel image / heap / pagetables   pkernel..pkernel+sz
                    (TTBR1 1 GiB window today; see Risks if larger)
```

Frame allocator state after Stage 2: `pmap_common.mem.entries[]` carries every DRAM bank reported by `/memory`; `pmap_common.mem.resvRegions[]` carries every `/reserved-memory` child plus the `get_vc_memory` range; `_pmap_getPage` (TD-15 phase 4b, `pmap.c` lines 727–741) flips reserved pages to `PAGE_OWNER_BOOT`, keeping VC4/CMA/spin-table out of the free pool.

## 2. Mailbox tags + DTB consumption

Two paths produce equivalent information; the kernel should consume the DTB path because plo already moved the firmware-handoff parsing into the DTB pipeline (`preinit.plo.yaml` blob loads `system.dtb`). The mailbox path stays available as a parity check.

### 2.1 Property tags (raspberrypi/firmware mailbox property interface ABI)

| Tag        | Name              | Request words | Response words | Meaning |
|------------|-------------------|---------------|----------------|---------|
| `0x00010005` | `get_arm_memory`  | 0             | 2 (base, size) | Lowest ARM-visible DRAM range, in bytes. |
| `0x00010006` | `get_vc_memory`   | 0             | 2 (base, size) | VPU-reserved range at the top of ARM-visible DRAM. |
| `0x00038049` | `set_framebuffer_off` | 1 (offset) | 1            | Used for VPU quiescing experiments. Not needed for Stage 2 itself. |

Pi 4B 4 GiB sample (firmware 2024-q1, `gpu_mem=76`, `total_mem=4096`):

```
get_arm_memory  -> base = 0x00000000, size = 0xfb000000   (4 016 MiB)
get_vc_memory   -> base = 0xfb000000, size = 0x04c00000   (   76 MiB)
```

`get_arm_memory` returns a single contiguous range below the carve-out; on 4 GiB SKUs the range stops below 4 GiB by exactly `gpu_mem` MB. The 4-GB total minus 4016-MB ARM range minus 76-MB VC range matches because firmware also reserves a small (~4 MB) IO/atomic-pool window at the top.

Reference: `raspberrypi/firmware` wiki `mailbox-property-interface`, plus `mailbox-tags` documentation in `linux/Documentation/devicetree/bindings/soc/raspberrypi/`.

### 2.2 DTB consumption today

`dtb.c` already parses (sufficient for Stage 2): `/memory@*` → `dtb_getMemory`; `/reserved-memory/*` → `dtb_getReservedMemory` (TD-15 phase 4, lines 270–322); `/soc/dma-ranges` → `dtb_armToBus()` (TD-15 phase 4, lines 254–267).

Pi 4 firmware emits `/reserved-memory` containing at minimum: `linux,cma`, atomic-pool, `vc4-cma` (when vc4 overlay loaded), and `armstub@0` (256 B spin-table). The vc4 top-of-DRAM carve-out is not always present in `/reserved-memory` — older firmware reports it only via `get_vc_memory`. Robust scheme: also clamp `pmap_common.mem.max` to `arm_memory.base + arm_memory.size − 1` from a kernel-side mailbox call.

### 2.3 P1 phase: read mailbox + log

In `_pmap_preinit` after `dtb_getMemory()`, before clamping `pmap_common.mem.max`, issue `get_arm_memory` and `get_vc_memory` via a kernel-side mailbox client (newly written under `hal/aarch64/generic/`) and:

1. Print parsed values via `lib_printf`.
2. Cross-check against `dtb_common.memBanks[]`. Disagreement → print warning, prefer mailbox (firmware truth).
3. Add the VC range to `pmap_common.mem.resvRegions[]` if it is not already covered by a `/reserved-memory` child.

P1 deliberately does not change the page allocator — it only logs. The diagnostic data flushes out firmware-version-specific ABI differences before P2/P3 act on it.

## 3. TTBR1 expansion plan

Today TCR_EL1 (`_init.S` line 122) sets `T1SZ=25` → 39 unsigned VA bits at TTBR1, i.e. a 512 GiB high half. TTBR1 starts at `VADDR_KERNEL = 0xffff_ffff_c000_0000` (1 GiB window) per `arch/pmap.h` line 22; the kernel currently uses one TTL2 page (`pmap_common.kernel_ttl2`) covering `0xffff_ffff_8000_0000` upward in 1 GiB chunks resolved through that one TTL2.

For Stage 2 the high-VA window the kernel actually needs is bounded by:

- Code/data/heap/stack of the kernel itself: a few MB.
- Per-process kernel mappings (PT pages, slab pages): `(allocsz + freesz) / 4` heuristic in `_page_init` (`page.c` line 493), which for 4 GiB DRAM is ~1 GiB. That sizing is already correct because it consumes whatever VA `_pmap_kernelSpaceExpand` is willing to back.
- DTB and device window: top 2 MiB (`pmap_common.devices_ttl3`).

Required VA budget: 1 GiB is plenty for the kernel proper plus its heap. The frame allocator does not need a 1:1 direct-map of all DRAM; Phoenix maps user pages on demand through `pmap_enter`. The only thing Stage 2 widens is *which physical pages* the allocator can hand out, not the kernel's high-VA footprint.

Concrete TTL2/TTL3 expansion:

- Pre-populate `pmap_common.kernel_ttl2[1..N]` with TTL3 page descriptors (one TTL3 per 2 MiB of kernel high VA the bootstrap will touch). Today only entry `[0]` is set.
- Each new TTL3 page is allocated from the bootstrap heap (`pmap_common.heap` is one page; need either a bigger heap or grow on the fly via `_page_alloc(... PAGE_KERNEL_PTABLE)` once `_page_init` finishes).
- Attributes: cacheable Inner-Shareable for normal kernel data, MAIR_IDX_DEVICE for `_pmap_halMapDevice` consumers, MAIR_IDX_NONCACHED only for explicit DMA buffers (Stage 2 §6).

The simpler implementation: extend `_pmap_kernelSpaceExpand` (already at `pmap.c` line 749) so that on Pi 4 its `vaddr` end argument grows to `VADDR_KERNEL + 1 GiB`, and let it allocate TTL3 pages lazily via `_page_alloc`. No change to TTL1/TTL2 layout, just stop clamping the expansion ceiling.

## 4. Frame-allocator extension

The relevant pieces:

- `pmap_common.mem.entries[PMAP_MEM_ENTRIES]` (`pmap.c` line 128) — bank list. Already sized at 64; never the limiter.
- `pmap_common.mem.{min,max}` — physical range bounds. Set in `_pmap_preinit` from `dtb_getMemory()`. Currently *correct* for 4 GiB SKU because `dtb_getMemory` reports the full ARM-usable range.
- `_pmap_getPage` (line 645) — iterates entries, returns next free PA. Already handles multi-bank lists.
- `_page_init` (`vm/page.c` line 424) — drives `pmap_getPage` until ENOMEM, building `pages_info.pages[]`. The `page_t` array sits at `*bss` (kernel BSS top) and grows via `_page_sbrk`. For 4 GiB / 4 KiB pages × 32-byte `page_t` = 32 MiB of BSS-backed metadata — within reason but worth checking against `vkernelEnd`.

Changes (no new function signatures needed; the existing API is sufficient):

- `_pmap_preinit` (`pmap.c` line 853, no signature change): after `dtb_getMemory()`, fold in the mailbox-derived `vc_memory` range as a synthetic reserved region when not already in `pmap_common.mem.resvRegions[]`.
- `_pmap_getPage` (`pmap.c` line 645, no signature change): logic already returns `PAGE_OWNER_BOOT` for reserved regions (lines 727–741). No edit beyond ensuring the VC-memory range is in `resvRegions[]`.
- `_page_init` (`vm/page.c` line 424, no signature change): no logic edit; once `_pmap_getPage` walks the full bank list, the loop at line 447 naturally consumes all of DRAM minus reserved.
- `_pmap_kernelSpaceExpand` (`pmap.c` line 749, no signature change): callers in `vm/page.c` already pass an `end` ceiling that scales with allocator size; no edit needed beyond ensuring the bootstrap heap can satisfy the initial TTL3 allocations.

New helper (small):

- `int hal_mailboxGetArmMemory(addr_t *base, size_t *size);`
- `int hal_mailboxGetVcMemory(addr_t *base, size_t *size);`

Both live under `hal/aarch64/generic/` (new file `mailbox.c`/`.h`) or alongside `dtb.c`. Implementation: same channel/protocol the plo `video.c` mailbox client uses (`mbox_chan_prop = 8`, request/response ABI). The kernel-side client does not need DMA — the mailbox property buffer can sit in the kernel's BSS and be flushed via `hal_cpuFlushDataCache` before/after.

## 5. GPU carve-out — boundary discovery

Three sources of truth, in trust order:

1. **`get_vc_memory` mailbox tag.** Always returns the carve-out base/size as the firmware sees it. Authoritative.
2. **`/reserved-memory/vc4-cma@*` (or similar) DTB child.** Present when the dtoverlay or base DTB declares it. Trust if present and consistent with #1.
3. **`config.txt gpu_mem=N`.** Indirect — the firmware applies `gpu_mem` to compute the carve-out, but `config.txt` parsing in the kernel is brittle; do not trust as primary. Use only as a diagnostic cross-check.

Implementation: P1 logs all three. P2/P3 fold #1 and #2 into `pmap_common.mem.resvRegions[]`. The page allocator then refuses to hand out frames inside the carve-out via the existing TD-15-phase-4b path.

Test variants for confidence:

- `gpu_mem=64` (small carve, exposes whether the allocator pushes right up to the boundary).
- `gpu_mem=256` (default-ish for HDMI use).
- `gpu_mem=512` (what real HDMI + V3D workloads need).

For each, verify `_get_meminfo` reports `(arm_memory_size − sum(reserved-memory))` ± a few MiB.

## 6. DMA-able pool

Pi 4 peripheral DMA constraints (BCM2711 datasheet ch. "ARM peripherals", plus `linux/arch/arm/boot/dts/bcm2711.dtsi`):

| Peripheral | DMA window | Bus alias | Notes |
|------------|-----------|-----------|-------|
| GENET (1 GbE) | 32-bit | `0xc0000000` legacy or `0x00000000` 40-bit | Kernel quirk required to enable 40-bit on early firmware. |
| SDHCI / SDIO | 32-bit | `0xc0000000` | Firmware-mediated for arasan SDHCI; on emmc2 only. |
| PCIe RC (xHCI hub, NVMe) | 30-bit (1 GiB) | `0xc0000000` | The PCIe outbound mapping in `board_config.h` line 39 is `0x6_0000_0000` PA → `0xf800_0000` PCIe; PCIe inbound restricts DMA targets to the lower 1 GiB unless the inbound window is reprogrammed. |
| V3D (GPU shader) | 32-bit | mapped via SMMU when present, else legacy alias. |
| DMA channel 0–6 (legacy) | 32-bit | `0xc0000000` | Used by audio, MMC. |
| DMA channel 11–14 (40-bit) | 40-bit | `0x00000000` | Limited use today. |

Implication for Phoenix: most peripherals can only DMA into the first 1 GiB. We need a **DMA-coherent allocator** that returns pages with `addr < 0x4000_0000`. The simplest implementation is a flag on `_page_alloc`:

- Add `vm_flags_t PAGE_DMA_LOW1GB`.
- Add `_page_get(addr_t addr)` already exists; reuse the size-bucket free list but filter on `p->addr < 0x4000_0000` when flag is set.
- Drivers like `pcie`, `xhci`, `genet` (when ported), `sdhci` request DMA buffers via the flagged path.
- Where `dtb_armToBus()` returns the 40-bit alias and the peripheral supports it, the flag can be dropped. Today only PCIe really benefits.

Stage 2 P3 only adds the *plumbing* and a single user (`xhci` already needs a DMA-capable command ring). Bringing GENET/SDHCI in scope is Stage 2 follow-up, not gated on the 4 GiB unlock itself.

## 7. Phased delivery

| Phase | Scope | Files touched | Boots after each phase? |
|-------|-------|---------------|--------------------------|
| P1 | Kernel-side mailbox client. Read `get_arm_memory` / `get_vc_memory`. Log values. Compare against `dtb_getMemory()`. No allocator change. | new `hal/aarch64/generic/mailbox.{c,h}`, hook in `_pmap_preinit` | yes — purely additive |
| P2 | Expand TTBR1 high-VA: drop the implicit 1 GiB clamp in `_pmap_kernelSpaceExpand` callers. Pre-populate `pmap_common.kernel_ttl2[]` for the address range the kernel will touch under load. | `pmap.c`, `_init.S` lines 484–502 | yes — kernel can map > 2 MiB high-VA |
| P3 | Extend frame allocator: fold mailbox VC memory into `resvRegions[]`. Verify `_pmap_getPage` walks every bank. Add `PAGE_DMA_LOW1GB`. | `pmap.c`, `vm/page.c`, `vm/page.h` (new flag) | yes — `_get_meminfo` reports near-4 GiB |
| P4 | Stress test: heavy malloc, pattern, free. Log meminfo before/after. Verify no allocations land in carve-out. | `test/` script + manual psh stress | yes — characterized |

Per phase: snapshot a manifest with `scripts/snapshot-integration-state.sh` once boot passes.

## 8. Test strategy

- **Smoke**: `meminfo` (or `_get_meminfo` syscall via `psh`) reports `(allocsz + freesz) / 1024 ≈ 3.95 GiB` on a 4 GiB SKU with `gpu_mem=76`.
- **Pattern**: psh script — `for size in 1G 1G 1G; do dd if=/dev/urandom of=/tmp/$size bs=1M count=1024; sha256sum /tmp/$size; rm /tmp/$size; done`. Bytes survive a copy round-trip and the totals do not OOM.
- **Carve-out exclusion**: dump `/proc/pmap` (or equivalent) and confirm no PA in the VC range is marked `PAGE_FREE`.
- **DMA flag**: `pcie` driver allocates command rings and reports their PA — must be `< 0x4000_0000` after `PAGE_DMA_LOW1GB` is honoured.
- **Variant matrix**: `gpu_mem ∈ {64, 256, 512}`; for each, expect `meminfo ≈ 4 GiB − gpu_mem − ~32 MiB firmware`.
- **Regression**: TD-04 hack must continue to work — the syspage destination page stays NC. Log its TTL3 entry attribute on every boot.

Validation harness (per `CLAUDE.md`):
`./scripts/rebuild-rpi4b-fast.sh → ./scripts/capture-rpi4b-uart.sh → python3 scripts/summarize-rpi4b-uart-log.py <log>`.

## 9. Risks and Stage-1 dependencies

- **Hard dependency on Stage 1.** Mapping 3 GiB Non-Cacheable would burn an order of magnitude of memory bandwidth. If Stage 1 phase B (D+I cache enable) cannot land cleanly, Stage 2 must be deferred until it does. Mapping > 1 GiB in NC is permitted as a debug fallback only.
- **`page_t` metadata cost.** 4 GiB / 4 KiB × ~32 B = ~32 MiB of `page_t` array sitting in BSS, growing via `_page_sbrk`. Verify that `vkernelEnd` + 32 MiB does not collide with the high-VA device window or run off the kernel's 1 GiB TTBR1 slice. If collision: bump `T1SZ` (TCR_EL1) to widen the window.
- **Mailbox call timing.** The mailbox property interface uses MMIO at `0xfe00_b880`. The kernel reaches `_pmap_preinit` *before* `_pmap_halMapDevice` is wired up. Workaround: temporarily map the mailbox 4 KiB page in `pmap_common.devices_ttl3` early (slot 2, after the PL011 early VADDR), call once, drop the mapping. plo already does this pattern for video init.
- **`/reserved-memory` parse stability.** TD-15 phase 4b cap is `MAX_RESV_REGIONS = 16`. Real Pi 4 firmware reports ≤ 8. If a future overlay pushes past 16, regions silently drop. Add a kernel warning.
- **TTBR1 1 GiB sufficiency (cache-mmu-smp open-question 5).** If the kernel heap + slab + page-table arena exceeds 1 GiB, `_pmap_kernelSpaceExpand` will fail at the high-VA boundary. Mitigation: widen TTBR1 by reducing `T1SZ` from 25 → 24 (2 GiB) when adding the first DRAM-bank-2 mapping. Cheap; do it pre-emptively in P2.
- **DMA-low alignment with `dtb_armToBus()`.** The translation table from `/soc/dma-ranges` already exists. If the peripheral uses the 40-bit alias the `PAGE_DMA_LOW1GB` flag is unnecessary; if 32-bit, the flag is mandatory. The driver-side decision is per-peripheral; encode it in the driver, not in the page allocator.
- **TD-04 NC-page interaction.** The syspage destination and bootstrap stack pages are explicit AttrIdx=1 (NC). When pre-populating TTL3 entries for `kernel_ttl2[1..N]`, do not blindly inherit `DEFAULT_ATTRS` for those addresses. Audit `_pmap_writeTtl3` callers to confirm the override path still triggers.

## 10. Open questions

1. **Does the firmware ever return `get_arm_memory` larger than the `/memory@*` reg? **In a clean bring-up, no — both reflect the same `total_mem − gpu_mem` calculation. But raspberrypi/firmware has shipped with quirks where `dtoverlay` modifications to `/memory` make the DTB report less than the mailbox. P1 logs both; if they diverge we must pick one canonical source. Recommend: trust mailbox for upper bound, trust DTB for bank list.
2. **Is `PAGE_DMA_LOW1GB` enough, or do we need a more granular `dma_mask` per device? ** For Stage 2, one flag covers everything legacy. Future GENET / V3D work may need 36-bit / 40-bit windows; the flag approach is extensible by adding `PAGE_DMA_LOW4GB` etc.
3. **Should the kernel reduce `gpu_mem` automatically when no HDMI/V3D is in use?** No — `gpu_mem` is `config.txt`-controlled and applied before the kernel runs. Out of scope; document the trade-off.
4. **`_pmap_kernelSpaceExpand` ceiling.** Today `vm/page.c` line 493 caps the expansion at `(allocsz + freesz) / 4 + 8 MiB`. With 4 GiB DRAM that becomes ~1 GiB. Confirm the 1 GiB TTBR1 window (with `T1SZ=25`) can accommodate the kernel image plus this expansion before P2 lands. If not, widen `T1SZ` in P2.
5. **CMA pool size.** Pi 4 firmware reserves a Linux-style CMA pool; Phoenix has no CMA consumer, so the pool is just deadweight. Should we petition the firmware (via `dtparam` / overlay) to shrink it? Better yet, P3 could add a knob to *release* CMA pages back to the free pool after the kernel decides it has no Linux CMA consumer. Out of scope for Stage 2; flag as a follow-up cleanup.
6. **Boot-time visibility of allocator state.** `vm_pageinfo` exists but the existing `vm: Initializing page allocator (X+Y)/Z` print at `vm/page.c` line 503 is the only on-boot signal. Extend it post-Stage-2 to print per-bank usage so the GPU carve-out is visible without psh — useful for headless bring-up.
7. **Plo low identity map.** Plo's `preinit.plo.yaml` line 5 declares `map ddr 0x00400000 0x3b400000 rwx` — a 944 MiB identity map starting at 4 MiB. Once the kernel takes over and Stage 2 lands, the plo map is irrelevant (kernel TTBR0 is dropped at `_init.S` line 768), but if Stage 2 ever needs to call back into firmware below 4 MiB or above 0x3b400000 the plo map would not cover it. Verify nothing in Stage 2 requires it.

