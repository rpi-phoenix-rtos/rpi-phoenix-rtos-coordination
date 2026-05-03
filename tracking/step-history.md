# Step History

## Completed Steps

### 2026-05-04: Restore TD-16 early page-table invalidation ✅
- **Kernel commit**: `5e727dcc` (`aarch64: restore early page-table invalidation`)
- **Manifest**: `manifests/2026-05-04-td16-early-pt-inval.md`
- **Image**: `0f6dc1a9e8254d9c42f41d6ee308eff074a9a6a2e0810cc1fa25044d9c260115`
- **UART log**: `artifacts/rpi4b-uart/rpi4b-uart-20260503-221342-netboot-td16-early-pt-inval.log`
- **Result**: Restored the early `_inval_dcache_range` over
  `PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK` before the first MMU enable.
  This gives Phoenix the Linux/FreeBSD-shaped page-table visibility step while
  keeping I-cache and D-cache disabled.
- **Validation**: QEMU Pi 4 smoke reaches `(psh)% help`; generic QEMU smoke
  reaches `(psh)% help`; real Pi 4 netboot reaches `(psh)%`.
- **Warnings**: No build/export/DTB/image warnings. Real Pi firmware emitted
  expected netboot-path misses and HDMI1 EDID/DSI messages. UART helper used
  `picocom` and printed `STDIN is not a TTY`, but capture completed and the
  log is valid.
- **Next recommended step**: Attempt the real early `SCTLR_EL1.M|C|I`
  transition in the Linux/FreeBSD shape, or first add a no-call early
  ESR/ELR/FAR dump if the current exception path remains too fragile for
  cache-enable fault diagnosis.

### 2026-05-04: Reject TD-16 no-call early exception dump ❌
- **Kernel commit**: none; source reverted to `5e727dcc`
- **Image tested**: `1559c85756df97bb4d18e4c6fc9702c606a55a7c1e95c3a86d15ecf585c018c1`
- **UART log**: `artifacts/rpi4b-uart/rpi4b-uart-20260503-222821-netboot-td16-early-exdump.log`
- **Result**: A no-call early exception-dump rewrite passed rebuild and both
  QEMU shell smokes after fixing macro-label assembly errors, but real Pi 4
  did not reach `(psh)%` inside 600 s. The run reached `psh: readcmd`, then
  timed out amid heavy interleaved process-spawn/debug output.
- **Warnings**: First build attempt failed with assembler errors caused by bad
  numeric macro-local labels; fixed before testing. Firmware also emitted many
  `xHC-CMD err` diagnostics during SD/USB-MSD probing before network fallback.
- **Decision**: Do not commit this diagnostic path. Future cache-enable fault
  diagnostics should use QEMU gdbstub first or a smaller controlled-exception
  test before hardware.

### 2026-05-02: Bypass TD-14 `devfs` lookup wall ✅
- **Kernel commit**: `60703368` (`rpi4b: stabilize devfs lookup during TD-14`)
- **Devices commit**: `63f1d438` (`rpi4b: keep console alias usable during TD-14`)
- **Utils commit**: `50cf5605` (`psh: trace ttyopen errno during TD-14`)
- **Manifest**: `manifests/2026-05-02-td14-devfs-direct-checkpoint.md`
- **Image**: `06071d7aac0de7d54b635d297cca9474ff4eacda13a6be3471f044ba454bb3a4`
- **UART log**: `artifacts/rpi4b-uart/rpi4b-uart-20260502-211848-netboot-td14-devfs-direct.log`
- **Result**: Fixed the relative `lookup("devfs")` query payload bug, added a direct stored OID for `devfs`, and kept `/dev/console` usable via a PL011 direct alias plus minimal stat support.
- **Validation**: QEMU Pi 4 smoke reaches `(psh)% help`; real Pi reaches `name: devfs direct hit`, `pl011-tty: tty0 ready`, `pl011-tty: console ready`, and `threads: psh user scheduled`.
- **Next recommended step**: Probe psh/libc after scheduling: root lookup, `/dev/console` stat/open, and first shell app entry.

### 2026-05-01: Clean TD-13 probes and restore mutex validation ✅
- **Kernel commit**: `37fcc58e` (`aarch64: clean td13 mutex syscall probes`)
- **Manifest**: `manifests/2026-05-01-td13-clean-probes.md`
- **Image**: `03e1988da8390512df2737d8efaa9b994725cd9873e465f318910af5e1ea6f0d`
- **UART log**: `artifacts/rpi4b-uart/rpi4b-uart-20260501-214225-netboot-td13-clean-probes.log`
- **Result**: Removed `sNN`, `M123EK`, `a..f`, `*15`, and `>` raw probe streams. Removed the temporary `TD-13-mtxbypass` and restored both `vm_mapBelongs()` validations in `syscalls_phMutexCreate()`.
- **Validation**: QEMU Pi 4 smoke reaches `(psh)% help`; real Pi still reaches `dummyfs: root initialized`, `pl011-tty: init: libtty_init ok`, `main: spawned psh (10)`, and `threads: psh user scheduled`.
- **Next recommended step**: Instrument the post-`psh` console boundary: `psh` startup, fd/devfs lookup, tty open, and first blocking read/write.

### 2026-05-01: Fix TD-13 `proc_mutexCreate` atomic hang ✅
- **Kernel commit**: `23b9a127` (`aarch64: avoid exclusive atomics on single-core bringup`)
- **Manifest**: `manifests/2026-05-01-td13-atomic-fallback.md`
- **Image**: `3e89b7c2c738892b5d71f03460e2fe026e0f0099cdb0cdec0b9749182e2e588b`
- **UART log**: `artifacts/rpi4b-uart/rpi4b-uart-20260501-191724-netboot-td13-atomic-fallback.log`
- **Result**: The `proc_mutexCreate` wall was narrowed to `resource_put()` / `lib_atomicDecrement()` and fixed for the current single-core AArch64 target by replacing GCC exclusive-access atomics with DAIF-masked plain updates when `NUM_CPUS == 1`.
- **Validation**: QEMU Pi 4 smoke reaches `(psh)% help`; real Pi reaches `M12abcdef3K`, initializes `dummyfs` and `pl011-tty`, spawns `psh`, and logs `threads: psh user scheduled`.
- **Next recommended step**: Clean or gate TD-13 UART probes and rerun QEMU + real Pi to diagnose why the real board still does not show a clean `(psh)%` prompt.

### 2026-04-19: Complete Map Relocation in Syspage Initialization ✅
- **Commits**: 1bb7f806, 1c6a5267, d1996d8f, aff01622, 2f0b391f
- **Result**: Kernel completes all map relocation and reaches program relocation phase
- **Image**: bb7861c314ca675eeee1f98e7744df29c123efa0533f3d007bc0c49b5d469531
- **Details**: Fixed infinite loop in entry relocation, implemented workaround to skip entry relocation, completed all map processing
- **Markers**: NYOPSTUZbcdeFGVWXf → NYOPSTUZbcdeFGVWXabcdefgmklmno (map relocation completed)

### 2026-04-19: Fix Syspage Access Crash After MMU Enable ✅
- **Commits**: 448c5e9c, de3e7e33, 3615bc1f, 43c4a20b, 2ac28beb
- **Result**: Kernel progresses from syspage_init() crash to _hal_init()
- **Image**: 2f166572e5f2380748317e2128f5633cd4367c07a5d3baf3bb280b6e3a17b991
- **Details**: Identified BSS region not mapped in MMU, implemented temporary fix to use original syspage
- **Markers**: NYOPSTUZbcde → NYOPSTUZbcdeFGVWXf (progress to _hal_init)

### 2026-04-19: Fix UART Corruption After MMU Enable ✅
- **Commit**: 6a0bdd06
- **Result**: Clean UART output after MMU enable, boot progresses to kernel entry point
- **Image**: 991e51d4bdafdbf7f5cc13ddff070654ee274ba886b05d5a47989a2878305e69
- **Details**: Replaced physical UART calls with virtual address macro after MMU enable

### 2026-04-18: Inline Critical Setup Functions
- **Commit**: (previous commit)
- **Result**: Progressed from NYO hang to NYOPSTUZb markers
- **Details**: Moved stack setup after MMU enable and inlined critical functions

### 2026-04-17: Separate MMU and Cache Enable
- **Commit**: (previous commit)
- **Result**: Progressed from X3 hang to NYO markers
- **Details**: Separated MMU enable from cache enable to avoid Cortex-A72 issues

### 2026-04-16: Fix CPACR_EL1 FPU/SIMD Setup
- **Commit**: (previous commit)
- **Result**: Progressed from early hang to X3 markers
- **Details**: Fixed CPACR_EL1 to enable (not disable) FPU/SIMD

### 2026-04-15: Initial SMP Enable for A72
- **Commit**: (previous commit)
- **Result**: Identified SMP enable issues
- **Details**: Added basic SMP enable using direct register writes

## Next Steps

### Immediate Next Step
1. **Debug program relocation hang**
   - Add strategic debug markers to program relocation section
   - Identify exact failure point (NULL pointer vs circular list issue)
   - Implement temporary workaround if needed
   - Goal: Reach marker `Y` (end of syspage_init())

### Short Term Goals
1. **Complete kernel initialization**
2. **Achieve console output**
3. **Test basic device drivers**
4. **Reach user-space entry**

### Longer Term Goals
1. **Implement proper MMU mapping for BSS/data region**
2. **Restore syspage copy operation**
3. **Full device driver support**
4. **Networking stack**
5. **Filesystem support**
6. **Multi-core SMP**

## Current Status

**Current Position**: HAL initialization entry (marker 'f')
**Blockers**: None - syspage access issue resolved
**Next Focus**: HAL initialization completion and console output

**Progress Summary**:
- ✅ Early boot sequence working
- ✅ MMU enable working
- ✅ Virtual memory transition working
- ✅ Syspage access working
- ✅ Kernel entry working
- 🔄 Current: HAL initialization

**Technical Debt**:
- Temporary fix for syspage access (skip copy)
- Need proper BSS region MMU mapping
- Need to restore syspage copy operation

Last updated: 2026-04-19
### 2026-05-02: Reach Real Pi 4 UART Shell Prompt ✅
- **Commits**: kernel `60703368`, devices `63f1d438` and `3ee4702`,
  libphoenix `3c76bba`, utils `da2f541`
- **Result**: Real Raspberry Pi 4 netboot reaches `(psh)%` on UART.
- **Image**: `d219efa27dd617ea171465f601742427ca1c96f3d505fb3979a1c7a27d0c520e`
- **Log**: `artifacts/rpi4b-uart/rpi4b-uart-20260502-220314-netboot-td14-readcmd-long.log`
- **Details**: Bypassed the stale `lookup("devfs")` root-query wall with a
  direct namespace OID, kept PL011 `/dev/console` usable via direct alias and
  stat/attr support, fixed `TIOCSPGRP` to use the requested foreground
  process-group ID, and avoided duplicate `/dev/console` canonicalization in
  the TD-14 fast path.
- **Next**: Remove or gate excessive diagnostic output, then run interactive
  shell smoke commands on real hardware.
