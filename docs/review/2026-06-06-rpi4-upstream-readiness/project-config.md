# project-config — upstream-readiness review

**Area:** project-config
**Repo:** phoenix-rtos-project
**Base:** `537ef83` (origin/master) → **Head:** `cb4b216` (master)
**Files reviewed:** all 28 changed files (+2680/-0 net)

---

## Findings

### BUG / MED

#### PC-B-01
`_projects/aarch64a53-generic-rpi4b/board_config.h:17-18` · **BUG** · sev=med

**WHAT:** `PLO_GICD_BASE_ADDRESS 0x40041000` and `PLO_GICC_BASE_ADDRESS 0x40042000` do not match the Pi 4 GIC-400 registers at `0xff841000` / `0xff842000` (the correct values used by `_projects/aarch64a72-generic-rpi4b/board_config.h:14-15`).

**WHY:** The `aarch64a53-generic-rpi4b` project targets the same Raspberry Pi 4 hardware. These addresses are sourced by PLO's generic GIC driver (`plo/hal/aarch64/generic/interrupts.c:200-201`) via `plo/hal/aarch64/generic/config.h:32-33`. With the wrong addresses, PLO programs a non-existent register page and fails to initialize interrupts. The project is not in CI (`ci-project.yml` has no `aarch64a53-generic-rpi4b` entry) but it is documented as an alternative boot path in `docs/manual-operator-instructions.md:324` and `docs/source-artifacts.md:331-333`.

**REC:** Replace both values:
```c
#define PLO_GICD_BASE_ADDRESS  0xff841000u
#define PLO_GICC_BASE_ADDRESS  0xff842000u
```
Severity is med rather than high because the target is not CI-gated and its production role is unclear; see structural note on `aarch64a53-generic-rpi4b` below.

**NEEDS-HW** (requires running `TARGET=aarch64a53-generic-rpi4b` on Pi hardware to observe PLO interrupt behavior; the wrong addresses are definitively wrong regardless).

---

### ROLLBACK / HIGH

#### PC-R-01
`README.md:1-6` · **ROLLBACK** · sev=high

**WHAT:** A "Fork warning" block was prepended to the project root README.md disclosing that "This fork contains AI-generated changes … not fully reviewed and not fully tested."

**WHY:** This is accurate for the current private fork but is not appropriate for upstream presentation — it will be the first thing maintainers read. The review rubric requires removing work-in-progress scaffolding before upstream submission.

**REC:** Remove the 5 added lines (the `> Fork warning:` block through the closing blank line) when preparing the upstream PR. Do NOT apply overnight — the disclaimer is still accurate for the current fork state.

**DOCUMENT-ONLY** (remove at upstream-PR time, not before; the warning is currently true).

---

### ROLLBACK / MED

#### PC-R-02
`_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S:216-237` · **ROLLBACK** · sev=med

**WHAT:** The SMP-D-3 diagnostic block (lines 216-237) writes per-CPU markers (`0x10000000 + cpu_id`) to physical addresses `0x40–0x4F` on every boot for all four cores.

**WHY:** The comment at line 220 states *"the kernel reads PA 0x40-0x4F at boot via a TTBR0 identity mapping."* This is false — no such reader exists anywhere in `phoenix-rtos-kernel/hal/aarch64/` (confirmed by full-tree grep for the marker value and address). The block is dead diagnostic code from the SMP-D-3 investigation with a false comment and no `TODO(TD-xx)` marker.

**REC:** Remove lines 216-237 entirely. The `x7` (cpu id) assignment at line 214 is still needed by subsequent code; retain it. The `dsb sy` at line 237 should be moved into the LOCAL_CONTROL write block that immediately follows (it is consumed there), or kept with a comment explaining the ordering intent.

**APPLY-SAFE** (dead code removal; verify armstub still assembles and `strings … phoenix-armstub8-rpi4.bin | grep AS0` confirms the handoff marker survives).

---

#### PC-R-03
`_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S:286,295,302,315` · **ROLLBACK** · sev=med

**WHAT:** Four intermediate `uart_putc` calls inside the errata setup block emit progress characters `'1'`, `'4'`, `'2'`, `'5'` (steps are numbered but out of sequence: output order is 1, 4, 2, 5 because the SMPEN step was later inserted between steps 3 and 4 but numbered 4→2 in the output).

**WHY:** `uart-summary.sh:85` references these markers with pattern `"132AS0|^132"` — but that regex does NOT match the actual emitted sequence `"1425AS0"`, so the check is already stale and produces no useful signal. Removing the four `uart_putc` calls requires updating `uart-summary.sh:85` at the same time (to strip the dead `132` component), exactly as PC-R-04 requires `summarize-rpi4b-uart-log.py` to be updated simultaneously. A build+boot smoke will not catch a broken stage-check regex — so this is not cleanly separable from the tooling update. The final `AS0\r\n` handoff (lines 389-394) IS actively parsed by `summarize-rpi4b-uart-log.py:19` and must be retained.

**REC:** Remove the four intermediate errata-step `uart_putc` calls (lines 286, 295, 302, 315) and update `uart-summary.sh:85` to check only for `"AS0"` in the same commit. Do not remove the UART chars without the coordinated `uart-summary.sh` edit.

**DOCUMENT-ONLY** (tooling-coupled change; requires coordinated edit to `scripts/uart-summary.sh:85`).

---

#### PC-R-04
`_projects/aarch64a72-generic-rpi4b/phoenix-kernel8-reloc.S:59-79,116-127` · **ROLLBACK** · sev=med

**WHAT:** The relocation trampoline emits `TR0\r\n`, `TR1\r\n`, `TR2\r\n`, `TR3\r\n` at four boot-progress checkpoints.

**WHY:** These markers ARE currently parsed by `scripts/summarize-rpi4b-uart-log.py:25-28` (patterns `\bTR0\b` through `\bTR3\b`) for boot-phase diagnosis, and the script's failure path at lines 146-149 explicitly diagnoses missing TR markers. Removing them would silently break the summary tooling. They are not pure noise.

**REC:** Retain TR0-TR3 markers as-is through the active bring-up period. Before upstream submission, decide: either (a) document them as an intentional Phoenix Pi4 boot-trace facility (add a `#if defined(BOOT_TRACE_UART)` guard so they can be compiled out for release) or (b) remove them and update `summarize-rpi4b-uart-log.py` at the same time. Do not remove the markers without a simultaneous tooling update.

**DOCUMENT-ONLY** (cannot be removed safely without updating `summarize-rpi4b-uart-log.py`).

---

### ROLLBACK / LOW

#### PC-R-05
`_projects/aarch64a72-generic-rpi4b/config.txt:3-4` and `_projects/aarch64a53-generic-rpi4b/config.txt:3-4` · **ROLLBACK** · sev=low

**WHAT:** Both config.txt files say *"This is only the first firmware-facing staging config. DTB staging and firmware-handoff support are still incomplete."*

**WHY:** This comment is stale. As of HEAD, `build.project` has a complete `rpi4b_stageDtb()` function that copies and patches the DTB, `preinit.plo.yaml` conditionally loads it via `blob`, and `user.plo.yaml` passes it to the kernel. "Incomplete" no longer accurately describes the state.

**REC:** Replace the two stale lines with a one-line accurate description, e.g. `# Phoenix-RTOS boot configuration for Raspberry Pi 4B`. Apply to both files.

**APPLY-SAFE** (comment-only; config.txt is not preprocessed).

---

#### PC-R-06
`_projects/aarch64a72-generic-rpi4b/rootfs-overlay/etc/rc.psh:1-14` · **ROLLBACK** · sev=low

**WHAT:** The `rc.psh` script is never executed. `user.plo.yaml` launches psh as `-x psh ddr ddr` (no arguments). `psh_pshapp()` in `phoenix-rtos-utils/psh/pshapp/pshapp.c:1803-1839` only runs a script when passed `-i <path>` or a positional path argument; without either it runs interactive. The rc.psh header itself says it is "run by `psh -i /etc/rc.psh`" — that invocation does not exist in the current plo.yaml.

**WHY:** The `rc.psh` is aspirational configuration for a future SD-boot iteration where psh would be launched as `-x psh;-i;/etc/rc.psh`. It currently constitutes dead config without a `TODO(TD-xx)` marker. The "STAGED BRING-UP" comment also says lwip/usb "are added back in the next iteration" — but both are already in `user.plo.yaml`.

**REC:** Either (a) wire the SD-variant plo.yaml to actually pass `-i /etc/rc.psh` to psh (completing the intended flow) or (b) mark rc.psh with `TODO(TD-xx)` explaining it is aspirational for the SD-root iter and remove the stale "STAGED BRING-UP" comment. Option (a) is preferred — the script logic appears correct (bind, dummyfs, posixsrv, psh).

**NEEDS-HW** to validate the SD-variant psh auto-init path after wiring in `-i /etc/rc.psh`.

---

#### PC-R-07
`_targets/aarch64a72/generic/preinit.plo.yaml`, `_targets/aarch64a72/generic/user.plo.yaml`, `_targets/aarch64a53/generic/user.plo.yaml` · **ROLLBACK** · sev=low

**WHAT:** Three `_targets/*.plo.yaml` files have no active consumer in the current tree. The build system (`_targets/build.common:27-34`: `b_set_default_path`) falls back to the `_targets` path only when the project directory does NOT contain the file. Resolution for every existing aarch64 project:

- `_targets/aarch64a72/generic/preinit.plo.yaml` — rpi4b project overrides; **dead**.
- `_targets/aarch64a72/generic/user.plo.yaml` — rpi4b project overrides; **dead**.
- `_targets/aarch64a53/generic/user.plo.yaml` — both a53-qemu and a53-rpi4b projects override; **dead**.
- `_targets/aarch64a53/generic/preinit.plo.yaml` — **active** (neither a53 project has a preinit override; both fall back to this file).

**WHY:** The dead a72 `preinit.plo.yaml` is additionally hazardous: it maps only `0x40000000–0x7fffffff`, omitting Pi 4 DRAM chunk-1 (`0x00400000–0x3b400000`). If a future `aarch64a72-generic-foo` project is added without a project-level preinit override, it silently inherits an incomplete memory map, leaving all programs in chunk-1 (including PLO itself) unmapped.

**REC:** Remove `_targets/aarch64a72/generic/{preinit,user}.plo.yaml` and `_targets/aarch64a53/generic/user.plo.yaml`. No existing build is affected. Retain `_targets/aarch64a53/generic/preinit.plo.yaml` (it is actively used). If generic stubs are needed for future targets, they should have a comment documenting that they are intentional fallbacks, not Pi-specific configs.

**APPLY-SAFE** (file deletion; no consumer exists for the three dead files — confirm with `git grep` that no project references these by explicit path before deleting).

---

### COMMENT / MED

#### PC-C-01
`_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S:58-62` · **COMMENT** · sev=med

**WHAT:** The macros `CPUECTLR_EL1_DIS_TWALK_DESC_PREFETCH (1UL << 38)`, `CPUECTLR_EL1_L2_IFETCH_PREFETCH_DISTANCE (3UL << 35)`, and `CPUECTLR_EL1_L2_LS_PREFETCH_DISTANCE (3UL << 32)` are defined but never used in the actual assembly code. Instead, the BIC/ORR instructions at lines 290-293 use magic literals `#0x001b, lsl #32` and `#0x0040, lsl #32`. The names imply "field values" (e.g. "DISTANCE = 3") while the actual masks needed for BIC are computed differently.

**WHY:** A reviewer checking the errata implementation against the A72 TRM must verify from scratch that `0x001b << 32` = bits {32,33,35,36} = L2_LS[33:32] + L2_IFETCH[36:35], and `0x0040 << 32` = bit 38 = DIS_TWALK. The defined constants don't appear in the code and encode values rather than masks, making verification unnecessarily hard. The comment "C-3v/w: disable A72 L2/table-walk prefetch before SMPEN" at line 288 also doesn't explain why bit 38 is being SET (not cleared) while the other bits are being CLEARED.

**REC:** Replace the three unused macros with ones that reflect how they're actually used:
```c
#define CPUECTLR_EL1_L2_PREFETCH_CLEAR_MASK  ((u64)0x001b << 32) /* bits 32,33,35,36: zero L2 prefetch distances */
#define CPUECTLR_EL1_DIS_TWALK_PREFETCH_BIT  ((u64)0x0040 << 32) /* bit 38: disable table-walk desc prefetch */
```
Then use these names in the BIC/ORR lines. Drop the three old `_DISTANCE` / `DIS_TWALK` macros.

**APPLY-SAFE** (purely definitional; the generated code is unchanged if literals are substituted simultaneously).

---

#### PC-C-02
`_projects/aarch64a72-generic-rpi4b/board_config.h:37-40` · **COMMENT** · sev=low

**WHAT:** `TODO(#126)` comment for `PL011_TTY_MOUSE_PATH "/dev/mouse"` says this is a "throwaway USB-mouse bring-up reader" to be removed once a real pointer consumer exists. The marker references a GitHub issue number (`#126`) rather than the project's `TD-xx` debt tracking convention.

**WHY:** `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` uses `TD-xx` identifiers for all source-code markers; `#126` is an issue-tracker reference not visible from the source alone. This creates an inconsistency in how temporary code is tracked.

**REC:** If the mouse path is genuinely deferred, assign a `TD-xx` ID in `TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` and update the comment to `TODO(TD-xx)`. If the validation is complete and the path is no longer temporary, remove both the `#define` and the corresponding code in `pl011-tty.c`.

**NEEDS-HW** to validate that mouse path can be removed.

---

### STYLE / LOW

#### PC-S-01
`_projects/aarch64a72-generic-rpi4b/board_config.h:25,27` · **STYLE** · sev=low

**WHAT:** `PLO_RPI_MAILBOX_BASE_ADDRESS` and `RPI_MAILBOX_BASE_ADDRESS` are both defined as `0xfe00b880u` — same address, two different macro names.

**WHY:** The split is intentional: `PLO_RPI_MAILBOX_BASE_ADDRESS` is consumed by PLO's `plo/hal/aarch64/generic/video.c`, while `RPI_MAILBOX_BASE_ADDRESS` is consumed by `phoenix-rtos-devices` (`usb/xhci/bcm2711-pcie.c`, `pcie/server/pcie.c`). However, the naming split is not documented inline, and a reader seeing two identical constants for the same register has no immediate explanation for why. Other `board_config.h` files in the tree (referent: `_projects/aarch64a53-generic-rpi4b/board_config.h`) use a single prefix per address symbol.

**REC:** Add a brief comment above the two definitions explaining the namespace split:
```c
/* PLO_RPI_ prefix: consumed by PLO video/mailbox driver (plo/hal/aarch64/generic/video.c).
 * RPI_       prefix: consumed by userspace device drivers (bcm2711-pcie.c, pcie.c).
 * Same physical register; two symbols to keep PLO and userspace include paths independent. */
```

**APPLY-SAFE** (comment-only).

---

#### PC-S-02
`_projects/aarch64a72-generic-rpi4b/lwip/lwipopts.h:1-12` · **STYLE** · sev=low

**WHAT:** The rpi4b `lwipopts.h` opens with a Phoenix-RTOS copyright block using `SPDX-License-Identifier: BSD-3-Clause`. All other `lwipopts.h` files in the project tree have no license header at all (referents: `_projects/armv7a7-imx6ull-evk/lwip/lwipopts.h`, `_projects/ia32-generic-qemu/lwip/lwipopts.h`, `_projects/armv7m7-imxrt106x-evk/lwip/lwipopts.h` — all start directly with `#define`).

**WHY:** The convention for `lwipopts.h` across Phoenix targets is: no header. Using `SPDX-License-Identifier: BSD-3-Clause` on a file that is a project-specific override (not derived from the BSD-3-licensed lwIP source) is also semantically questionable; other Phoenix project files use the `%LICENSE%` placeholder or no header.

**REC:** Remove the copyright block (lines 1-12) and start with `#define LWIP_TCPIP_CORE_LOCKING 1` to match the convention of all other Phoenix `lwipopts.h` files. If a header is desired, use the standard Phoenix `%LICENSE%` placeholder form as seen in `board_config.h`.

**APPLY-SAFE** (header change only; `--scope core` rebuild required because lwipopts.h is included by the lwip build).

---

### ARCH / LOW

#### PC-A-01
`_projects/aarch64a72-generic-rpi4b/build.project:54-60` and `_projects/aarch64a53-generic-qemu/build.project:12-18` · **ARCH** · sev=low

**WHAT:** Both projects define `fastlane_stagePshApplets()` which copies the psh binary to produce `mkdir` and `bind` as multi-call aliases.

**WHY:** No upstream Phoenix project does this (referents: `_projects/ia32-generic-qemu/build.project` — no staging function; `_projects/armv7a7-imx6ull-evk/build.project` — no staging function). Both referents rely on the psh / phoenix-utils build system to produce the applet binaries natively. The copy-based alias is a bringup workaround for aarch64 not having the applet-symlink infrastructure wired up, and it is not documented as such.

**REC:** Add an inline comment explaining *why* the copy is needed for aarch64 and that native applets are the intended upstream path. Before upstream submission, investigate whether `phoenix-rtos-utils bind` and `mkdir` can be built for `aarch64a72-generic` natively (they compile for ia32 without modification). If native applets are feasible, replace the copy workaround. If the workaround must stay for initial submission, add a `TODO(TD-xx)` entry.

**NEEDS-HW** (native applet path requires a build test for aarch64).

---

## Structural notes (not per-finding)

### `_targets` PLO YAML coverage (see also PC-R-07)

The build system (`_targets/build.common:27-34`) resolves `PLO_SCRIPT_PREINIT` and `PLO_SCRIPT_USER` by checking the project path first, then falling back to the `_targets/<family>/<subfamily>/` path:

| file | active consumer? |
|---|---|
| `_targets/aarch64a72/generic/preinit.plo.yaml` | **dead** — rpi4b project overrides |
| `_targets/aarch64a72/generic/user.plo.yaml` | **dead** — rpi4b project overrides |
| `_targets/aarch64a53/generic/preinit.plo.yaml` | **active** — fallback for a53-qemu AND a53-rpi4b (neither has a project-level preinit) |
| `_targets/aarch64a53/generic/user.plo.yaml` | **dead** — both a53 projects override with their own |

The three dead files are captured in finding PC-R-07.

### `_projects/aarch64a53-generic-rpi4b` — functional gap; maintained but underdocumented

This project targets Pi 4 hardware with the A53 ABI. It is not in CI, but is documented in `docs/manual-operator-instructions.md:324` and `docs/source-artifacts.md:331-333` as an alternative boot path, and the `small-repos` review area (`docs/review/2026-06-06-rpi4-upstream-readiness/small-repos.md:283`) confirms "aarch64a53-generic scaffolding is not dead." It has no `nvm.yaml` (falls back to `_targets/aarch64a53/generic/nvm.yaml`) and no `preinit.plo.yaml` (falls back to `_targets/aarch64a53/generic/preinit.plo.yaml` — that file is active and correct). Beyond PC-B-01 (wrong GIC addresses), the project's purpose is not documented in-tree. For upstream, add a brief comment in `board_config.h` or `build.project` explaining the use case (e.g., AArch64 baseline without A72-specific errata/features; compatibility testing).

### `_projects/aarch64a53-generic-qemu` — clean, minimal, appropriate for upstream

This project correctly auto-generates its DTB at build time via `qemu-system-aarch64 ... -dumpdtb`. The run script `scripts/aarch64a53-generic-qemu.sh` uses correct paths consistent with the build output. No issues.

### busybox_config — justified subset

The rpi4b `busybox_config` enables 187 items vs the ia32 referent's 204. The 17-item delta is networking applets (`PING`, `IFCONFIG`, `NC`, `NTPD`, `ROUTE`, `TELNETD`, `NICE`) that are deliberately disabled — lwip handles networking for this target. Core applets (shell, coreutils, compression, editors, runsv) are at parity with ia32. No bloat finding.

### Duplicate-program check in user.plo.yaml — clean

The two `bcm2711-emmc` entries are gated by mutually exclusive `RPI4B_VARIANT` conditionals (`== 'sd'` vs `!= 'sd'`), so only one is ever emitted into the final PLO script. No duplicate-program brick hazard.

---

## Summary

**Counts by category:** BUG×1 (1 med) · ROLLBACK×7 (1 high, 3 med, 3 low) · COMMENT×2 (1 med, 1 low) · STYLE×2 (2 low) · ARCH×1 (1 low) = **13 findings**.

**Top 3 issues:**

1. **PC-R-01 (ROLLBACK/HIGH):** The README.md "Fork warning" disclosing AI-generated / unreviewed status must be removed before upstream submission — it is the first thing maintainers will read.
2. **PC-B-01 (BUG/MED):** `aarch64a53-generic-rpi4b/board_config.h` has wrong GIC base addresses — the project cannot correctly initialize PLO interrupts on real Pi 4 hardware as-is.
3. **PC-R-03/PC-R-04 (ROLLBACK/MED):** Both the intermediate armstub UART markers (`1/4/2/5`) and the reloc-trampoline markers (TR0-TR3) are coupled to `uart-summary.sh` and `summarize-rpi4b-uart-log.py` respectively; neither can be removed without a coordinated tooling update in the same commit.
