# Integration State: plo-el2-fault-diagnostic-state

## Summary

- Date: 2026-05-11
- Note: plo MMU+I+D-cache enabled at EL2 via staged SCTLR writes; subsequent sync exception at VBAR_EL2+0x200 (EC=0x00 Unknown) localized inside `video_mailboxCall` at PC 0x202600. Diagnostic vector table installed at every VBAR_EL2 slot; slot E (Current EL with SPx, Sync) reads EL2 exception registers and dumps them via PL011. **Two parallel research subagents converged on**: (a) HCR_EL2 missing AMO/IMO/FMO routing, plus MDCR_EL2/HSTR_EL2/VPIDR_EL2/VMPIDR_EL2 left at UNKNOWN reset; and (b) D-cache `dc isw` set/way maintenance is not a coherency substitute per ARM ARM D5.10.2 — I-cache aliasing post SCTLR.I=1 needs per-VA-line `dc cvau`+`ic ivau` maintenance. Bundled patch addressing both is queued for the next on-device cycle.
- Generator: hand-written (no on-device test landed since last build; sibling repo HEADs match the 2026-05-10 manifest, the diff captured below is the uncommitted working tree).

## Repositories

| Repository | Branch | Commit SHA | Working tree |
| --- | --- | --- | --- |
| plo | codex/common-aarch64-platform-makefiles | `9af0f59` | clean (all of below committed) |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | `763b210a` | dirty (Step 5 M\|C\|I, parked behind plo blocker) |
| phoenix-rtos-project | master | `0f6be40` | clean (armstub committed) |
| (all other siblings) | as in `manifests/2026-05-10-pathA-plo-mmu-cache-el-aware.md` | unchanged | clean |

## What committed (plo `9af0f59` on top of `e90bdcd`)

### plo (4 files, +365 / -54)

- `hal/aarch64/generic/_init.S` (+173/-?):
  - New `uart_put_hexnibble` macro and `uart_put_hex64` helper for diag prints from asm
  - Replaces every `b .` / `b _exceptions_dispatch` in `_vector_table` with a unique `exc_tag` (A..P) so each VBAR slot leaves a distinct UART signature
  - Slot 0x200 (Current EL with SPx, Sync) now branches to `_slot_e_dump` which reads ESR_EL2 / ELR_EL2 / FAR_EL2 / SPSR_EL2 / SCTLR_EL2 / TTBR0_EL2 / TCR_EL2 / MAIR_EL2 / instruction-word at `[ELR_EL2]` and prints them as hex

- `hal/aarch64/generic/hal.c` (+119/-?):
  - `hal_memoryInit` rewritten with per-stage console markers (`mem: pre-init`, `mem: post-init`, `mem: post-map`, `mem: pre-tlbi`, `mem: post-tlbi`, `mem: post-read-sctlr`, `mem: pre-sctlr-write-{M,MI,MIC}`, `mem: post-sctlr-write-{M,MI,MIC}`, `mem: post-enable`)
  - **Staged SCTLR_EL2 writes**: M-only first (each with ISB), then OR-in I, then OR-in C. All three writes succeed on real Pi (validated 2026-05-11).
  - `hal_init` reordered: `interrupts_init`, `console_init` *before* `hal_memoryInit`, then `timer_init`, then `video_init` — so we have UART output through every MMU-affecting step
  - Per-phase markers in `hal_init`: `hal: console_init done`, `hal: hal_memoryInit done`, `hal: timer_init done`, `hal: video_init done`
  - `#include "../mmu.h"` (was using a forward declaration before)

- `hal/aarch64/mmu.c` (+42/-?):
  - `mmu_currentEL` helper, EL-aware dispatch for `mmu_invalTLB`, `mmu_setTranslationRegs`, `mmu_readSctlr`, `mmu_writeSctlr`, `mmu_mapAddr`'s TLBI
  - TCR_EL2 fixup: bit 23 (EL1.EPD1) RES0 in EL2 — cleared; IPS placed at bits 18:16 (EL2 layout) not 34:32 (EL1 layout)
  - `mmu_enable` with pre-flip and post-flip canonical barrier ritual (`ic ialluis; dsb ish; isb`)

- `hal/aarch64/generic/video.c` (+25/-?):
  - Forward declaration of `video_td16PrintHex32`
  - Console markers in `video_mailboxCall`: `mbox: pre-clean`, `mbox: post-clean`, `mbox: status0=0xXXXXXXXX`, `mbox: skip-wait`, `mbox: pre-write`, `mbox: write-done`
  - Console markers in `video_framebufferInit`: `fb: enter`, `fb: post-memset`
  - Diagnostic: read `mbox_status` register once and print as hex (returned `0x40000000` = mbox_empty)
  - Busy-wait on full bit was replaced with a "skip-wait" probe for diagnostic continuity; original wait will be restored once the EC=0x00 root cause is fixed

### phoenix-rtos-kernel (`763b210a` + 30 insertions / 23 deletions across 1 file)

- `hal/aarch64/_init.S`:
  - Step 5 of the canonical-idiom alignment: single SCTLR_EL1 write enabling M|C|I with the canonical barrier ritual (`ic ialluis; dsb ish; isb` pre-flip; `dsb sy; isb; ic ialluis; dsb ish; isb` post-flip)
  - Currently parked behind the plo blocker — kernel never reaches this code with caches expected on until plo hands off cleanly

## Tested image (last full cycle)

| Build | SHA256 | Result |
| --- | --- | --- |
| `e5a041f2…` | rich slot-E dump | reproduces EC=0x00 at PC `0x00202600` (cache-line-aligned) inside `video_mailboxCall`, dumps ESR/ELR/FAR/SPSR/SCTLR cleanly |
| (previous builds in the same line) `cda3606e…`, `df3f1fa6…`, `7499467a…`, `9efe0ee3…`, `34023901…`, `c2f0044e…`, `a8734671…`, `56c88f9b…`, `e72e27ae…` | progressively narrower instrumentation | each successfully reaches a unique marker further than the previous, confirming determinism |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
plo	9af0f59	codex/common-aarch64-platform-makefiles
phoenix-rtos-kernel	763b210ad8ee53ffcc920420725fa441879d3319	agent/rpi4-program-reloc
phoenix-rtos-project	0f6be40	master
```

(other repos: same SHAs as `manifests/2026-05-10-pathA-plo-mmu-cache-el-aware.md`)

## Next cycle (queued, blocked on Pi availability)

Single bundled patch — see `docs/status.md` 2026-05-11 entry and `tracking/current-step.md` for the full plan. Touches:

- `plo/hal/aarch64/generic/_init.S` `start_el2` — HCR_EL2 |= AMO|IMO|FMO, MDCR_EL2=0, HSTR_EL2=0, VPIDR_EL2=MIDR_EL1, VMPIDR_EL2=MPIDR_EL1, SCTLR_EL2 baseline 0x30c50838, CPTR_EL2=0x33ff, DAIF mask at top
- `plo/hal/aarch64/generic/hal.c` after the M|I|C SCTLR write — `ic iallu; dsb ish; isb`

Existing rich slot-E dump stays so the next cycle (even if the patch shifts the fault) produces TTBR0/TCR/MAIR + 4 instruction bytes at ELR — enough to decisively distinguish I-cache aliasing from architectural fault.
