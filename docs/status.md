# Status

## Repository State

- Repository purpose: documentation and agent scaffolding for a future Phoenix RTOS Raspberry Pi port
- Implementation state: Phase 1 common AArch64 cleanup started; first upstream build-glue step completed
- Documentation baseline prepared: 2026-03-19

## Implementation Readiness

Documentation readiness:

- ready

Tracking readiness:

- ready

Execution readiness on the current workstation:

- ready for implementation bootstrap and validated host-side Phoenix builds

Known remaining start-gate tasks before the first implementation step:

- none

Completed start-gate tasks:

- missing host prerequisite tools installed on the current workstation
- the initial Phoenix upstream repositories cloned into `sources/`
- first baseline integration manifest created under `manifests/`
- `phoenix-dev` Linux VM created and verified
- the documented Linux package baseline installed and verified inside `phoenix-dev`
- the full current `phoenix-rtos-project/.gitmodules` repo set cloned as sibling repos under `sources/`
- the local sibling-clone buildroot workflow has been defined and automated with `scripts/prepare-buildroot.sh`
- one clean upstream `host-generic-pc` build completed successfully inside `phoenix-dev`

Start-gate status:

- cleared for the first implementation steps

## Strategic Decisions Already Made

- First real target is Raspberry Pi 4 Model B.
- Raspberry Pi 5 is a second-stage target after Pi 4 stabilization.
- Final target architecture should preserve Phoenix's normal boot chain:
  `Raspberry Pi firmware -> plo -> syspage -> kernel -> user-space servers/drivers`
- Pi 4 bring-up should begin with a minimal single-core UART-booting system.
- The implementation must advance in narrow, explicitly validated steps rather than broad multi-subsystem pushes.
- Every successful implementation step must end with git commits in each touched upstream repository plus a coordination-repo state update.
- QEMU is a fast gate, not a replacement for real hardware.
- Pi 4 network boot is a preferred later-stage real-hardware deployment path for fast iteration once bootloader setup and DHCP/TFTP infrastructure are ready; SD or USB media remains the fallback and recovery path.
- This project runs on a macOS Apple Silicon workstation. The recommended execution model is macOS host for coordination and hardware control, plus a Linux arm64 VM as the primary Phoenix build and emulation environment.
- Future code must favor upstreamability: small diffs, Phoenix-native style, warning-clean builds, and no gratuitous reformatting.
- The workflow now supports explicitly authorized unattended sessions, but only under the step, validation, commit, and stop-condition rules documented in `docs/unattended-agent-mode.md`.

## Most Important Technical Findings

- Phoenix has reusable AArch64 support, but it is currently too `zynqmp`-specific in build glue and DTB assumptions.
- Phoenix's AArch64 DTB parser needs generalization for Raspberry Pi DT layouts and standard FDT cell handling.
- Phoenix's AArch64 HAL currently includes generic GICv2 support, but timer/platform selection is too platform-specific.
- Phoenix's existing test runner is already structured for UART-driven DUT automation and can be extended for Raspberry Pi targets.
- Phoenix officially documents Linux build flows and Linux package prerequisites; native macOS builds should not be treated as the primary path.
- On the current host, Homebrew, Xcode, QEMU, `dtc`, `uv`, `expect`, `jq`, `limactl`, `yq`, `socat`, `picocom`, `mtools`, and `socket_vmnet` are present, and the `phoenix-dev` Ubuntu 24.04 VM now has the documented package baseline installed.
- `phoenix-rtos-project` expects a populated multi-repo tree via its submodule paths. The full current `.gitmodules` repo set is now cloned under `sources/`, and the sibling-clone workflow is now handled through the disposable buildroot prepared by `scripts/prepare-buildroot.sh`.
- In the current Lima setup, the shared workspace path is effectively read-only from inside the Linux guest, so disposable buildroots should fall back to VM-local storage such as `~/phoenix-buildroots/phoenix-rtos-project`.
- The first clean upstream baseline build is now verified with `TARGET=host-generic-pc ./phoenix-rtos-build/build.sh clean host core fs test project image` inside the disposable buildroot, producing artifacts under `_build/host-generic-pc`, `_fs/host-generic-pc/root`, and `_boot/host-generic-pc`.
- The `aarch64-phoenix` toolchain is now installed and verified in `phoenix-dev` at `/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix`, with sysroot `/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/aarch64-phoenix`.
- The AArch64 toolchain build requires more than the baseline Phoenix package set; the currently confirmed extra VM packages are `bison`, `flex`, `libgmp-dev`, `libmpfr-dev`, `libmpc-dev`, `libisl-dev`, and `zlib1g-dev`.
- The current AArch64/libphoenix flow still generates files inside component source trees, so the linked buildroot is not sufficient for current toolchain or AArch64-target validation in the read-only Lima mount; use `scripts/prepare-buildroot.sh --copy-components` and the VM-local copied buildroot at `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy` for those lanes.
- The first upstream AArch64 cleanup step is now complete: `phoenix-rtos-kernel` and `plo` no longer hardwire top-level AArch64 platform selection through a literal `zynqmp` substring check, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with the new selection path.
- Copied buildroots exclude `.git`, so some builds may emit harmless version-probe noise such as `fatal: not a git repository`; treat the overall build exit status and produced artifacts as authoritative.
- Local `qemu-system-aarch64` in `phoenix-dev` provides the standard `virt` machine, and its DTB exposes root-level `pl011@...`, `intc@...`, `arm,armv8-timer`, and PSCI/HVC nodes; the first non-Xilinx QEMU follow-up should therefore start with kernel DTB parser recognition of those node names rather than with target metadata alone.
- The kernel DTB parser now recognizes shallow `pl011@...` and `intc@...` nodes, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- Local `virt` inspection also confirmed that the GIC `reg` property uses 16-byte tuples, so the next narrow generic-QEMU follow-up should stay in `hal/aarch64/dtb.c` and generalize interrupt-controller `reg` decoding before broader AArch64 platform work.
- The kernel DTB parser now decodes both the existing 12-byte GIC `reg` tuples and the 16-byte tuples used by local QEMU `virt`, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- There is still no reusable PL011 or ARM architectural timer implementation in the current Phoenix AArch64 tree, so the next smallest preparatory step is to expose root-level `timer` node interrupt metadata from the DTB parser before adding any runtime generic timer code.
- The AArch64 DTB API now exposes architectural timer interrupt metadata from the root-level `timer` node, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- The DTB preparation series is now far enough that the next step must introduce runtime code or new target/build structure, so the next active step is a bounded planning step to choose the smallest safe runtime follow-up.
- That runtime planning step is now complete: the next selected change is to remove the hard `TIMER_IRQ_ID` dependency from common AArch64 GICv2 code by moving timer IRQ knowledge behind the timer HAL API.
- The common AArch64 GICv2 code now queries timer IRQ identity through the timer HAL API instead of using the `TIMER_IRQ_ID` macro directly, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- Reusable AArch64 architectural timer sysreg helpers now exist in `hal/aarch64/aarch64.h`, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- The common AArch64 timer track is now explicitly shaped: the future backend should be a directly selectable common AArch64 timer implementation, and the next smallest step is to codify timer-source selection in the DTB API before the backend itself is introduced.
- The AArch64 DTB API now exposes an explicit selected generic timer source and IRQ for the common EL1 path, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- The first directly selectable common AArch64 timer backend step is now explicitly scoped: a full architectural timer backend is still premature because the current scheduler wakeup path can reprogram the timer from non-CPU0 contexts, so the next safe code steps must keep separating build and interrupt-path assumptions before changing runtime timer behavior.
- The common AArch64 kernel Makefile now exposes an explicit timer-backend selection hook, and the current ZynqMP timer backend still builds cleanly through that hook on the existing `aarch64a53-zynqmp-qemu` lane.
- Common AArch64 GICv2 handler registration now avoids SPI-style CPU retargeting for SGI/PPI interrupts, removing one interrupt-layer mismatch before a future architectural timer IRQ delivered as a PPI.
- Common AArch64 code now exposes a targeted SGI helper in addition to the broadcast helper, but the next timer-runtime step still requires an explicit SGI reservation and notification contract.
- The generic `hal/tlb/tlb.c` shared-work-plus-SGI pattern is not currently wired into AArch64 builds, so future AArch64 timer-update notifications cannot simply reuse that machinery without additional integration work.
- AArch64 now reserves `TIMER_WAKEUP_IRQ` and the scheduler can coalesce remote wakeup requests and redirect wakeup-deadline recomputation back to CPU 0, removing the main scheduler-side blocker for a future CPU-local architectural timer backend.
- The common AArch64 build now compiles a source-keyed `gtimer` helper layer, so the next backend work can focus on backend state and policy instead of raw physical-versus-virtual sysreg branching.
- The common AArch64 build now also compiles a generic timer backend-state layer that owns the selected source, IRQ, and frequency, so the next backend work can add behavior helpers without redoing state discovery.
- The backend-state layer now exposes raw-count, count-to-microseconds, and current-time helpers, so the next backend work can focus on forward conversion and wakeup programming instead of reopening current-time reads.
- The backend-state layer now also exposes a reusable microseconds-to-relative-ticks helper, so the next backend work can focus on state-keyed timer-register wrappers instead of open-coded frequency math.
- The backend-state layer now also exposes state-keyed control and relative-timer register wrappers, so the next backend work can focus on arming policy and IRQ ownership instead of low-level source dispatch.
- The backend-state layer now also exposes a backend-local wakeup helper that arms the selected architectural timer for bounded positive waits, so IRQ ownership is now the main missing piece before public AArch64 timer-HAL wiring.
- The backend-state layer now also exposes IRQ query and handler-registration helpers, so the next common-AArch64 timer step can finally move from backend-local helpers to the first public timer-HAL wrapper boundary.
- The AArch64 build now exposes an explicit public timer-implementation hook while keeping ZynqMP selected, so the next common timer step can focus on the first public `hal_timer*` wrapper file instead of reopening build glue.
- The AArch64 build now also exposes an explicit timer-implementation override hook, so the first common public timer file can be validated without replacing the default ZynqMP timer selection.
- The kernel now provides a common public AArch64 timer implementation file in `hal/aarch64/gtimer_timer.c`, and the existing copied-buildroot `aarch64a53-zynqmp-qemu` lane still builds successfully in `phoenix-dev` when that file is selected through `AARCH64_TIMER_IMPL_OVERRIDE`.
- `phoenix-rtos-build` now recognizes `aarch64a53-generic` and provides a matching generic AArch64 core-build entry point.
- `phoenix-rtos-kernel` now provides a first generic AArch64 platform scaffold, and the generic kernel target links successfully in the VM-local copied buildroot when validated with a temporary empty `board_config.h` shim via `PROJECT_PATH`.
- the first generic AArch64 `plo` scaffold is now explicitly bounded to one target-local linker template plus minimal `_init`, HAL, console, timer, and interrupt files, and it should be validated first through a direct `make -C plo base_noimg` lane in `phoenix-dev`.
- `phoenix-rtos-plo` now provides that first generic AArch64 loader scaffold, and `aarch64a53-generic` builds `plo` directly as `plo-aarch64a53-generic.elf` in the VM-local copied buildroot.
- the current generic loader fast lane is intentionally QEMU-`virt`-oriented and EL3-centric, assumes preconfigured PL011 state, and uses a polling architectural-counter timer inside `plo` to avoid widening the first runtime lane before the project entry point exists.
- the first generic QEMU project entry point is now explicitly scoped around a RAM-backed `loader.disk` loaded into `ram0` on QEMU `virt`; that was selected because the new generic `plo` path already has `ram-storage`, while generic flash, SD, and virtio boot paths do not exist yet.
- `phoenix-rtos-project` now provides that first generic AArch64 QEMU entry point, and the current project/image validation lane produces `_boot/aarch64a53-generic-qemu/plo.elf` plus `loader.disk` in `phoenix-dev`.
- the current project/image lane still uses a temporary narrower validation path:
  - prebuild the kernel directly for `aarch64a53-generic-qemu`
  - run `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host project image`
  - this is temporary until generic-target support exists in `phoenix-rtos-devices` and any remaining generic userspace blockers are removed
- `libphoenix` now builds successfully for `aarch64a53-generic-qemu`, and its AArch64 reboot helper now handles both the ZynqMP and generic `platformctl_t` layouts cleanly.
- the broader generic `host project image` lane now succeeds again from the current copied-buildroot baseline; the next fastest blocker is therefore back in generic QEMU runtime progress rather than in project-build plumbing.
- the refreshed generic QEMU smoke lane is still kernel-only by construction: the current generic `user.plo` loads only the kernel and DTB, and the next fast-lane blocker is the missing userspace console path built around a reusable PL011 tty driver.
- in `phoenix-rtos-devices`, the first missing layer for that console path is now confirmed: `_targets/Makefile.aarch64a53-generic` does not exist yet, so the next repo-local unblock is target scaffolding before PL011 driver code.
- `phoenix-rtos-devices` now exposes that generic AArch64 target scaffold and validates successfully for `aarch64a53-generic-qemu`; the next fast-lane blocker is the first reusable PL011 tty driver slice itself.
- that first PL011 slice is now explicitly scoped as a single-instance polling `pl011-tty` driver with `libtty`, `libklog`, `/dev/tty0`, and `/dev/console`, configured first through `board_config.h`.
- the new `pl011-tty` scaffold now builds directly on `aarch64a53-generic-qemu`; the next fast-lane decision is how to integrate it with the generic target in the smallest useful way.
- that integration choice is now fixed: the next smallest fast-lane step is to add `pl011-tty` to the generic devices target defaults before wiring board-specific base addresses or `user.plo`.
- `pl011-tty` is now in the generic devices default component set; the next smallest blocker is the missing generic-QEMU `board_config.h` wiring for its PL011 base address and clock.
- local QEMU `virt,secure=on` inspection now pins those board-config values: the usable non-secure PL011 is at `0x09000000` and its fixed clock is `24 MHz`.
- the generic QEMU project now supplies those values in `board_config.h`; the next smallest fast-lane step is to load `dummyfs` and `pl011-tty` in the right order from `user.plo`.
- that `user.plo` ordering is now fixed as the next minimal image step: `dummyfs;-N;devfs;-D` first, then `pl011-tty`, with `psh` intentionally deferred.
- the generic image now packages `dummyfs` and `pl011-tty`, but the visible smoke result still stops at the first kernel banner line; the next fast-lane step should be chosen from that updated runtime state rather than from more packaging-only work.
- that next fast-lane step is now fixed as plain `psh` integration, following the proven minimal `dummyfs + tty + psh` shape used by another generic target and still avoiding `rc.psh` overlay work for now.
- the generic image now packages `dummyfs`, `pl011-tty`, and `psh`, but the visible smoke result is still unchanged; the next fast-lane step must therefore diagnose whether generic userspace startup is being reached at all.
- that diagnostic choice is now fixed as a raw PL011 startup banner from `pl011-tty`, because it is the smallest high-signal test that stays repo-local and does not require broader kernel tracing.
- that diagnostic now passes: `pl011-tty: started` appears on the generic QEMU console, proving that the packaged userspace path reaches the PL011 driver on the non-secure UART.
- the next smallest unknown is now `/dev/console` readiness, and the selected follow-up is a second raw PL011 banner emitted only after successful console-device registration.
- that follow-up diagnostic now also ran, and the new `pl011-tty: console ready` banner never appears even after a 20-second QEMU run; the current fast-lane boundary is therefore between `pl011_init()` completion and successful console-device registration.
- the next selected split point is successful `/dev/tty0` registration, because it is the immediate runtime boundary before `_PATH_CONSOLE` registration in `pl011-tty`.
- that `/dev/tty0` diagnostic also stayed absent, so the current fast-lane boundary is now between `pl011_init()` completion and the first successful `create_dev()` call; local `create_dev()` and `dummyfs -D` source inspection makes a startup-order race the next bounded hypothesis to test.
- the selected next runtime test is now a single `wait 500` between `dummyfs;-N;devfs;-D` and `pl011-tty` in the generic `user.plo`, because it is the smallest change that can test the observed `/dev` namespace readiness hypothesis.
- that `wait` test was rejected and reverted: local QEMU output plus `plo/cmds/wait.c` confirm that `wait` is an interactive loader command, not a passive sleep, so it is unsuitable for unattended generic-QEMU or real-hardware automation unless a loader input device is intentionally configured.
- the first Pi 4-specific scaffold step is now fixed: start with a project-local `aarch64a53-generic-rpi4b` layered on top of the existing generic target, rather than widening the target matrix before board-local overrides are in place.
- that first Pi 4 scaffold is now implemented and build-validated in `phoenix-dev`; the new `aarch64a53-generic-rpi4b` project provides Pi 4 board-local overrides while intentionally deferring the real firmware-facing DTB and boot-partition staging decisions to the next step.
- that next Pi 4 staging decision is now fixed: emit a firmware-facing boot directory with a project-local `config.txt` and renamed raw `plo` image before widening into DTB import or firmware-handoff code.
- that firmware-facing staging step is now implemented and build-validated; `_boot/aarch64a53-generic-rpi4b/rpi4b/` now contains `config.txt` and `kernel8.img`, while DTB staging and EL3-only loader entry remain the next two concrete blockers.
- the Pi 4 project now has an optional project-local DTB staging hook: when `RPI4B_DTB_PATH` is set or `_projects/aarch64a53-generic-rpi4b/bcm2711-rpi-4-b.dtb` exists, the build stages `bcm2711-rpi-4-b.dtb` into `_boot/aarch64a53-generic-rpi4b/rpi4b/`; otherwise the default build remains self-contained and stages only `config.txt` and `kernel8.img`.
- local QEMU reset logging now gives a useful generic loader-entry matrix for the same image:
  - `virt,secure=on` starts in EL3 and is the current known-good baseline
  - `virt,secure=off` starts in EL1h
  - `virt,secure=off,virtualization=on` starts in EL2h
- generic `plo` now handles EL1, EL2, and EL3 entry in one localized AArch64 assembly path, and the same generic QEMU image now reaches visible loader, kernel, and early userspace output in all three entry modes.
- the remaining caveat on that path is diagnostic rather than boot-blocking: generic non-EL3 loader exception-context save is not yet independently hardened, so the currently validated result is the normal no-fault fast path.
- the Pi 4 boot tree now reuses that existing generic `ram0` path: it stages `loader.disk` next to `kernel8.img` and uses `initramfs loader.disk 0x48000000` so Raspberry Pi firmware preloads the payload to generic `plo` `RAM_ADDR`.
- the next concrete Pi 4 blocker is now kernel DTB propagation, not raw payload transport: the generic AArch64 kernel requires a syspage program named `system.dtb`, but the current Pi 4 `user.plo.yaml` still loads only the kernel and user-space programs.
- the next selected Pi 4 step is therefore project-local DTB propagation: reuse the existing optional Pi 4 DTB input, copy it into `${PREFIX_ROOTFS}/etc/system.dtb`, and restore the `blob {{ env.BOOT_DEVICE }} /etc/system.dtb ddr` payload entry so the generic kernel contract stays intact.
- the Pi 4 project now propagates an optional board DTB into both the firmware boot tree and the kernel-visible payload as `system.dtb`; default no-DTB builds stay green, and supplied-DTB builds now emit the expected `blob` load entry in the generated loader script.
- the current `phoenix-dev` QEMU inventory does not expose a `raspi4b` machine at all, so the no-hardware Pi 4 lane on this workstation remains generic `virt` runtime validation plus Pi 4 artifact inspection rather than Pi 4 board emulation.
- the next concrete Pi 4 boot blocker is now loader MMIO addressing: `sources/plo/hal/aarch64/generic/config.h` still hardcodes QEMU `virt` UART and GIC base addresses, so the current Pi 4 `kernel8.img` would still talk to the wrong MMIO blocks on real hardware until those addresses are made board-overridable.
- generic `plo` now accepts project-local MMIO base overrides for UART0 and GICv2 while preserving the current QEMU `virt` defaults, and the generic `virt` smoke lane still boots after that change.
- the current Pi 4 firmware handoff no longer appears to have a raw loader placement mismatch: `kernel_address=0x40080000` in the Pi 4 `config.txt` matches `ADDR_PLO 0x40080000` in `plo/ld/aarch64a53-generic.ldt`.
- the next Pi 4 deployment blocker is now firmware-file completeness rather than loader placement: the staged `_boot/.../rpi4b/` tree still lacks Raspberry Pi firmware files, so it is not yet a self-contained first-partition boot bundle.
- the Pi 4 project now accepts an operator-supplied Raspberry Pi firmware directory through `RPI4B_FIRMWARE_DIR` or `_projects/aarch64a53-generic-rpi4b/firmware` and stages required firmware files such as `start4.elf` and `fixup4.dat` into `_boot/.../rpi4b/` while keeping default no-firmware builds green.
- Phoenix upstream style is conservative and review-oriented: file headers, tabs in C, localized `clang-format off/on`, direct control flow, `static const` hardware tables, and warning-clean builds enforced by `-Werror` in `phoenix-rtos-build/Makefile.common`.
- Pi 4 uses BCM2711 with GIC-400, PL011, BCM2711 PCIe, VL805 xHCI over PCIe, GENET Ethernet, and Broadcom SDHCI.
- Pi 5 uses BCM2712 plus RP1, with most I/O behind a PCIe-connected southbridge-like peripheral controller.

## Immediate Next Implementation Milestones

1. Define the first emulated generic AArch64 smoke lane.
2. Run the first end-to-end generic `virt` boot attempt.
3. Trim the remaining generic QEMU boot blockers one by one until there is stable `plo` and kernel output.
4. Add the matching emulated test target and smoke harness once the QEMU invocation and success signal stabilize.
5. Reuse the proven generic `virt` boot pieces as the immediate template for the first Raspberry Pi 4 `plo` path.

## Pi 4 Success Criteria for "Phase 1"

- Stable boot from Raspberry Pi firmware into `plo`
- Stable `plo` UART console
- Stable `plo -> kernel` transfer
- Kernel MMU, exception, interrupt, and timer paths working
- Single-core shell on UART
- Reliable reboot

## Pi 4 Success Criteria for "Developer Complete"

- SD boot and persistent rootfs
- UART, GPIO, I2C, SPI, PWM
- Ethernet
- PCIe host bridge
- xHCI USB host
- USB mass storage
- Watchdog, thermal, RNG
- Reproducible build/test automation against real hardware

## Pi 5 Entry Gate

Do not start full Pi 5 enablement until Pi 4 has:

- stable boot
- stable storage
- stable Ethernet
- stable USB host
- a working real-device regression loop

## Re-Verify Before Depending On

- Raspberry Pi EEPROM/config behavior
- QEMU `raspi4b` peripheral completeness
- exact network boot and `boot.img` behavior on the current Raspberry Pi bootloader release
- Lima `socket_vmnet` behavior on the exact macOS and Lima versions in use when bridged lab networking is enabled
- Pi 5 debug/bootloader options such as `enable_rp1_uart`, `pciex4_reset`, `os_check`
- Linux and BSD support state for Pi 5 Ethernet and RP1 peripherals
