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
- The first acceptable real-device artifact handoff should now be based on a
  Pi 4 image with HDMI text console output, not on the earlier stage-panel-only
  artifact.
- USB keyboard work is now explicitly split into a generic HID class-driver
  track and a later Pi 4 transport track through BCM2711 PCIe plus VL805 xHCI.
- the platform-agnostic Phoenix PCIe server scan path no longer hardcodes
  direct ECAM access throughout its logic; it now goes through a small
  server-local config-space backend interface, which is the first enabling
  step toward a BCM2711 indexed-config backend.
- the Pi 4 path now also has the first BCM2711-specific indexed config-space
  backend behind that interface, selected by Pi 4 build settings and validated
  in compile-only form.
- the BCM2711 backend now also performs the first host-bridge preparation slice:
  reset sequencing, SerDes IDDQ clear, revision read, and early `MISC_CTRL`
  preparation before any claim of link-up or downstream enumeration.
- the BCM2711 backend now also performs the first bounded link slice:
  `PERST` release, 100 ms settle wait, link-state sampling, and RC-mode
  sampling, then uses that sampled state to gate downstream config-space
  accesses.
- the BCM2711 backend now also performs the first outbound-window and
  root-bridge shaping slice:
  one outbound window, RC BAR2 programming, and root-bridge class-code shaping
  behind the sampled link-state gate.
- the BCM2711 backend now also performs the first bridge-side exposure slice:
  root-bridge cache-line, bus-number, memory-window, and command programming on
  bus `0` behind the sampled link-state gate.
- the BCM2711 backend now also performs the next bounded bridge-capability
  slice:
  root-bridge parity plus PCIe root-control CRS software visibility behind the
  sampled link-state gate.
- the Pi 4 A72 image path now also includes and stages the `pcie` server,
  which makes the current BCM2711 transport work reachable on the actual board
  image path instead of only compile-validated in isolation.
- the Pi 4 board config now also carries the first VL805/xHCI fast-path
  contract taken from Circle:
  fixed downstream BDF, fixed xHCI class code, and MMIO through the outbound
  PCIe window.
- the first compile-valid Pi 4 xHCI code now also exists:
  a `libusbxhci` skeleton plus a Pi 4 discovery stub, validated together with
  the `usbkbd` pieces and the Phoenix USB host binary on the A72 lane.
- the Pi 4 xHCI path now also extracts the next controller-shape fields after
  the first reset/readiness checks:
  max slots, max scratchpad buffers, and context size.
- the Pi 4 xHCI path now also extracts the next controller register-layout
  facts needed before later interrupter or ring work:
  doorbell and runtime-register offsets.
- the Pi 4 xHCI path now also extracts the remaining pre-interrupt capability
  facts needed before later event-ring work:
  max interrupters, interrupt moderation scale, and maximum ERST size.
- the Pi 4 xHCI path now also extracts the remaining structural memory-layout
  capability facts needed before later DCBAA and scratchpad work:
  64-bit addressing support, scratchpad-restore support, and maximum primary
  stream array size.
- the Pi 4 xHCI path now also extracts the first operational-register memory
  layout state needed before later controller programming:
  `CRCR` and `DCBAAP`.
- the Pi 4 xHCI path now also allocates the first controller-owned memory
  objects needed for later controller setup:
  one `DCBAA` page and one first command-ring backing block.
- the Pi 4 xHCI path now also performs the first bounded controller
  register-programming step:
  `DCBAAP`, `CRCR`, and `CONFIG` are written from the previously allocated
  command-space objects and then read back for sanity checks.
- the current QEMU boot validation result is now explicit:
  - generic `virt` shell smoke still passes
  - Pi 4 `raspi4b` shell smoke also passes, but only on a DTB-prepared image
    built with:
    - `RPI4B_DTB_PATH`
    - `RPI4B_QEMU_MEMORY_SIZE=80000000`
  - the Pi 4 HDMI smoke also passes on that same DTB-prepared image
- that validation also exposed and fixed the first generic AArch64 USB-host
  portability issue in `phoenix-rtos-usb`:
  `usb.c` now passes the message-thread port value through `uintptr_t`.
- the normal `aarch64a72-generic` build now also produces the USB host and
  keyboard artifacts:
  `/sbin/usb` and `/sbin/usbkbd`, and the live Pi 4 image script now stages
  `/sbin/usb` as the intended linked-driver integration point.
- after the new xHCI capability-shape step, the remaining controller gap is
  narrower again:
  register-layout facts such as doorbell and runtime offsets should be
  extracted before any future interrupter, ring, or root-hub work.
- after the new register-layout step, the remaining controller gap is narrower
  again:
  pre-interrupt capability limits such as max interrupters and ERST sizing
  should be extracted before any future event-ring work.
- after the new pre-interrupt capability step, the remaining controller gap is
  narrower again:
  addressability and scratchpad-memory capability should be extracted before
  any future DCBAA or scratchpad-allocation work.
- after the new scratchpad/addressability capability step, the remaining
  controller gap is narrower again:
  the next clean seam is the first operational memory-layout register subset,
  especially `DCBAAP` and `CRCR`.
- after the new operational-register layout step, the next clean seam is no
  longer more capability decoding; it is the first controller-owned memory
  allocation step for `DCBAA` and the command ring.
- after the new memory-allocation step, the next clean seam is the first real
  xHCI register-programming step:
  `DCBAAP`, `CRCR`, and `CONFIG`.
- after the new register-programming step, the next clean xHCI seam is no
  longer passive controller setup; it is the first pre-run operational step
  beyond command-space binding.
- QEMU still cannot validate the real Pi 4 USB keyboard path itself, because
  the current `raspi4b` machine does not expose the BCM2711 PCIe root-port path
  needed for VL805 xHCI bring-up.
- the requested post-xHCI QEMU validation also exposed a separate Pi 4 shell
  startup issue:
  generic shell smoke still passes, Pi 4 HDMI smoke still passes, but the Pi 4
  shell smoke now stalls before `(psh)%`.
- the bounded shell-side retry fix is now also in place:
  the Pi 4 shell lane again reaches `psh: tty ready`, prompt, and `help`, so
  the remaining automated-smoke issue is stale kernel probe noise rather than
  the earlier console-open race.
- that stale probe noise is now also removed:
  generic shell smoke passes, Pi 4 shell smoke passes cleanly again, and Pi 4
  HDMI smoke still passes after deleting the obsolete `create_dev` probes.
- after that cleanup, the next clean xHCI seam is again explicit:
  the first pre-run operational step beyond `DCBAAP`, `CRCR`, and `CONFIG`
  programming.
- that run-state step is now also in place:
  the Pi 4 xHCI path can validate a bounded halted-to-run-to-halted controller
  transition while still returning `-ENOSYS`, so the next clean seam is event-
  ring preparation rather than more run-state guessing.
- that first event-ring memory step is now also in place:
  the Pi 4 xHCI path now allocates one event-ring segment and one ERST block
  and populates the first ERST entry for interrupter `0`, so the next clean
  seam is runtime-register programming for that event-ring state.
- that event-ring runtime-register step is now also in place:
  the Pi 4 xHCI path now programs and reads back `ERSTSZ`, `ERSTBA`, and
  `ERDP` for interrupter `0`, so the next clean seam is command-ring layout
  initialization rather than more event-ring plumbing.
- that command-ring layout step is now also in place:
  the Pi 4 xHCI path now initializes a real command-ring layout with a final
  link TRB and initial cycle-state contract.
- the current Pi 4 xHCI discovery contract still reports `irq = 0`, so the next
  clean seam is a single-command polled no-op submission path rather than
  interrupt-enable work.
- that single-command polled no-op path is now also in the tree:
  the Pi 4 xHCI code can now submit one internal no-op command through the
  command ring and event ring in a compile-validated form.
- that first xHCI roothub-control seam is now also in the tree:
  the Pi 4 xHCI code now answers the bounded Phoenix roothub subset needed for
  early enumeration:
  - device/config/string/hub descriptors
  - `PORTSC`-based port status
  - minimal port feature set/clear handling
- the first xHCI init-success seam is now also in the tree:
  `xhci_init()` no longer deliberately returns `-ENOSYS` after the current
  roothub-ready controller setup sequence
- the first xHCI roothub status-delivery seam is now also in the tree:
  the Pi 4 path now has a bounded temporary polling thread that completes the
  pending root-hub interrupt transfer when `xhci_getHubStatus()` reports change
  bits on the current no-IRQ path
- the first post-roothub child-device seam is now also in the tree:
  the Pi 4 xHCI path now executes a bounded `Enable Slot` command, validates
  the returned completion event, and captures the slot ID
- the next bounded child-device prerequisite is now also in the tree:
  the Pi 4 xHCI path now allocates the first per-slot controller-owned memory
  objects behind the returned slot ID:
  - one device context
  - one input context
  - one endpoint-0 ring backing block
  - one bound `DCBAA[slotId]` entry
- the next bounded `Address Device` prerequisite is now also in the tree:
  the Pi 4 xHCI path now prepares the minimum direct-root-port child context
  state derived from the Phoenix `usb_dev_t` contract:
  - EP0 ring layout with final link TRB
  - input-control `AddContextFlags`
  - slot-context root-hub port, speed, and context entries
  - EP0 dequeue pointer, error count, type, max packet, and average TRB length
- the first bounded xHCI `Address Device` action is now also in the tree:
  the Pi 4 xHCI path now handles only non-roothub `REQ_SET_ADDRESS` under an
  explicit temporary equality contract:
  requested USB address must match the enabled slot ID
- the first bounded non-`SET_ADDRESS` xHCI endpoint-0 transfer is now also in
  the tree:
  the Pi 4 xHCI path now executes a synchronous polled EP0
  `REQ_GET_DESCRIPTOR` control read for the first direct-root-port child under
  the current temporary slot-ID-equals-address contract
- that bounded descriptor-read path is also non-regression-validated:
  a fresh full `aarch64a72-generic-rpi4b` build passes and the Pi 4 shell smoke
  still passes after the xHCI change
- the first bounded post-enumeration xHCI control-write path is now also in
  the tree:
  the Pi 4 xHCI path now executes synchronous polled EP0 zero-length OUT
  control writes for the current direct-root-port child, limited to:
  - `REQ_SET_CONFIGURATION`
  - `CLASS_REQ_SET_PROTOCOL`
  - `CLASS_REQ_SET_IDLE`
- the first bounded non-EP0 xHCI endpoint-ownership slice is now also in the
  tree:
  the Pi 4 xHCI path now derives the keyboard interrupt-IN endpoint identity,
  allocates one transfer ring, populates one endpoint context, and completes
  one bounded `Configure Endpoint` command for the current child device
- the first bounded no-IRQ xHCI interrupt-transfer path is now also in the
  tree:
  the Pi 4 xHCI path now queues one outstanding interrupt-IN transfer on the
  configured endpoint and completes it from the existing status thread by
  polling the event ring
- the project is still not ready for interactive real-device USB testing:
  the live Pi 4 image now stages `/sbin/usb`, the Pi 4 QEMU shell smoke still
  passes after that integration, and the next concrete blocker is real-device
  validation of the new HDMI plus USB-keyboard path.
- the first refreshed real-device Pi 4 SD-card artifact is now exported at:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  with SHA-256:
  `475d8d21cdc00d2c2fc79819fe02bdcc946b5ee75329b503198dda7ac16877c3`

## Most Important Technical Findings

- Phoenix has reusable AArch64 support, but it is currently too `zynqmp`-specific in build glue and DTB assumptions.
- The first Pi 4 A72 QEMU HDMI text-console milestone is now complete:
  `pl011-tty` mirrors transmitted bytes into the firmware-allocated framebuffer,
  the background is black, and white glyphs are visible in the expected text
  rows.
- The first reusable USB keyboard foundation now exists in
  `phoenix-rtos-devices/tty/usbkbd/`: it is a generic USB HID boot-keyboard
  class driver that exposes `/dev/kbdN` and translates interrupt-IN boot
  reports into shell-usable cooked bytes.
- `pl011-tty` now also has a small optional `/dev/kbd0` reader bridge on the
  Pi 4 A72 project path, so once a USB host transport exists the current HDMI
  text console can become interactive without another console-architecture
  rewrite.
- That keyboard step does not solve Pi 4 transport yet. Real Pi 4 keyboard
  input still depends on later BCM2711 PCIe and VL805 xHCI work.
- After the first transport scoping pass, the smallest next Pi 4 USB milestone
  is now explicitly BCM2711 PCIe root-complex bring-up plus ECAM enumeration;
  xHCI should come only after that link and config-space foundation exists.
- after the first enabling transport refactor, the next bounded PCIe move is
  no longer “make the server less ECAM-shaped”; it is “add the first
  BCM2711-specific indexed config-space backend behind the new server-local
  interface”.
- after that backend step, the remaining real Pi 4 transport gap is now
  narrower and explicit: BCM2711 host-bridge initialization and link bring-up
  before downstream enumeration can be treated as meaningful.
- after the new early host-bridge step, the remaining transport gap is narrower
  again: outbound-window setup, PERST release, link-up checks, and RC-mode
  verification before downstream enumeration can be treated as meaningful.
- after the new link-state step, the remaining transport gap is narrower again:
  outbound-window setup and root-bridge shaping before downstream enumeration
  can be treated as meaningful.
- after the new outbound-window and root-bridge shaping step, the remaining
  transport gap is narrower again: root-bridge memory-window programming and
  downstream-bus exposure before downstream enumeration can be treated as
  meaningful.
- after the new xHCI capability-shape step, the remaining xHCI gap is narrower
  again: register-layout facts such as doorbell and runtime offsets should be
  extracted before any future interrupter or ring work is attempted.
- after the new bridge-exposure step, the remaining transport gap is narrower
  again: first meaningful downstream endpoint visibility before any xHCI-
  specific work can be treated as meaningful.
- after the new bridge-capability step, the remaining transport gap is narrower
  again: one direct downstream config-space readback before any broader
  enumeration or xHCI work can be treated as meaningful.
- after the new Pi 4 project-integration step, that remaining gap is now tied
  to a real staged runtime component rather than to an unreferenced server
  binary.
- after the new VL805 fast-path constants step, the remaining USB gap is now
  also explicit:
  the first compile-valid xHCI HCD skeleton before any staged runtime USB host
  path is attempted on Pi 4.
- after the new compile-valid xHCI step, the remaining gap is narrower again:
  the normal `aarch64a72-generic` build flow still does not build these USB
  pieces by default before any staged runtime USB host path is attempted.
- after the new A72 USB build-glue step, the remaining gap is narrower again:
  the xHCI runtime path still returns `-ENOSYS`, so the next useful work item
  is the first runtime-safe xHCI initialization slice rather than more build
  glue.
- after the new runtime-safe xHCI init step, the remaining gap is narrower
  again:
  the Pi 4 xHCI path now maps MMIO and validates the capability header, so the
  next useful work item is the first controller-reset and operational-readiness
  slice rather than more discovery or build glue.
- after the new xHCI reset step, the remaining gap is narrower again:
  the Pi 4 xHCI path now completes the first bounded controller reset, so the
  next strongest device-facing gap is the Pi 4 VL805 firmware-reset notify
  handshake plus later page-size, ring, interrupt, and root-hub work.
- after the new Pi 4 firmware-notify step, the remaining gap is narrower
  again:
  the image path now includes the first VL805 firmware-load hook before device
  enable, so the next useful xHCI work item is controller-readiness refinement
  inside `xhci` rather than more Pi 4 firmware plumbing.
- after the new post-notify xHCI readiness step, the remaining gap is narrower
  again:
  the Pi 4 xHCI path now validates 4K page support and a non-zero port count,
  so the next useful work item remains inside structural xHCI capability state
  rather than firmware or PCIe plumbing.
- after the post-xHCI Pi 4 GDB pass, the current non-xHCI runtime blocker is
  also narrower:
  `resolve_path("/dev/console")` now succeeds on the Pi 4 lane, but
  `open("/dev/console")` still returns `-1` across all five `psh_ttyopen()`
  retries, so the next smallest move is a shell-side retry-policy refinement.
- Pi 4 `raspi4b` QEMU is not expected to validate that PCIe milestone, because
  the emulator still lacks the relevant PCIe root-port support.
- The strongest currently available no-hardware validation for the new
  keyboard driver is component-level AArch64 compilation with the existing
  `aarch64-phoenix` toolchain.
- the strongest currently available no-hardware validation for the new PCIe
  server abstraction is:
  - representative `pcie` server compilation on the ZynqMP/Xilinx-targeted
    lane with the correct project include path
  - fresh Pi 4 A72 full-build regression validation from a disposable temp
    buildroot
- the strongest currently available no-hardware validation for the first
  BCM2711-specific backend step is:
  - preserved ZynqMP/Xilinx `pcie` server compilation from a fresh disposable
    buildroot
  - Pi 4 targeted `pcie` server compilation with
    `PCI_EXPRESS_BCM2711_INDEXED_CFG=y`
  - fresh Pi 4 A72 full-build regression from the same disposable buildroot
- the same validation shape also now covers the first BCM2711 host-bridge
  preparation hook, because that step still stays in the compile-only lane
- the same validation shape also now covers the first BCM2711 link-state step,
  because that work still stays in the compile-only lane
- the same validation shape also now covers the first BCM2711 outbound-window
  and root-bridge shaping step, because that work still stays in the
  compile-only lane
- the same validation shape also now covers the first BCM2711 bridge-exposure
  step, because that work still stays in the compile-only lane
- the full `aarch64a53-zynqmp-qemu` project build is currently blocked by an
  unrelated kernel issue outside the PCIe step:
  `hal/aarch64/interrupts_gicv2.c` references `TIMER_IRQ_GROUP` without a
  definition on that lane
- A stronger IA32 EHCI host build lane exists in principle, but `phoenix-dev`
  does not yet have an `i386-pc-phoenix` toolchain, so the current VM cannot
  validate the full `ia32-generic-qemu` build script end to end.
- Two distinct bugs had to be fixed to reach that milestone:
  - generic AArch64 kernel `_init.S` still used pre-graphics hardcoded syspage
    offsets
  - generic `plo` published graphmode metadata too early, because `video_init()`
    runs before `syspage_init()` in `plo/main()`
- a bounded gdbstub session also proved that packed `graphmode_t` by-value
  passing in `plo` was unsafe on this AArch64 path; the current code now uses a
  pointer-based `syspage_graphmodeSet()` helper instead.
- The current Pi 4 QEMU validation pair is now:
  - `./scripts/qemu-shell-smoke.sh rpi4b`
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`
- the HDMI smoke is currently the stronger revalidated Pi 4 signal:
  a clean rerun still passes, while the `rpi4b` shell helper now needs one
  bounded follow-up because a fresh rerun echoed `help` but did not complete
  the expected `Available commands:` exchange before timeout.
- the current HDMI smoke no longer validates the old top-left stage panel; it
  validates a black console background plus white text pixels in the first text
  row instead.
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
- the Ubuntu 24.04 packaged QEMU inside `phoenix-dev` is `8.2.2` and does not expose `raspi4b`, but a VM-local official QEMU `10.2.2` build now exists at `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64` and does expose `raspi4b`.
- the first `raspi4b` smoke now ran with the staged Phoenix Pi 4 image under QEMU `10.2.2`; QEMU requires `-smp 4`, and the current image still times out with no serial output, so the next blocker is in emulated Pi 4 boot progress rather than environment capability.
- official QEMU `raspi4b` docs already list `PCIE Root Port` and `GENET Ethernet Controller` as missing, so the new board-specific QEMU lane is useful for early boot and UART-path work but should not be treated as authoritative for Pi 4 PCIe or Ethernet bring-up.
- the first bounded emulated Pi 4 boot blocker is now identified: QEMU `raspi4b` direct raw-kernel boot uses the common ARM loader path, which loads raw AArch64 images at `0x00080000`, while the Pi 4 Phoenix `kernel8.img` is linked for firmware placement at `0x40080000`; the next smallest step is to validate `plo.elf` as the QEMU `-kernel` while keeping `kernel8.img` for real firmware boots.
- the Pi 4 `raspi4b` QEMU lane now reaches visible `plo` startup when `plo.elf` is used as `-kernel`, confirming that the earlier silent timeout was dominated by the raw-image handoff mismatch.
- local QEMU `10.2.2` source makes the next blocker explicit: ELF payloads are not treated as Linux kernels, so the board's AArch64 secondary-core spin-table boot stub is not installed for `plo.elf`.
- the first bounded blocker after the `plo.elf` handoff is now generic loader-side secondary-core containment: generic AArch64 `plo` has no equivalent of the existing ZynqMP non-primary-core trap, so the current Pi 4 QEMU lane falls into an early multi-core exception storm after loader startup.
- generic AArch64 `plo` now contains non-primary cores until kernel handoff, the generic `virt` lane still reaches the kernel banner and `pl011-tty: started`, and the Pi 4 `plo.elf` QEMU lane now reaches a stable post-alias loader boundary instead of an immediate exception storm.
- the next Pi 4 blocker is now strongly bounded by artifact comparison: the working generic QEMU lane includes `/etc/system.dtb`, while the current Pi 4 validation build without `RPI4B_DTB_PATH` does not include `system.dtb` in the loader payload.
- the next smallest Pi 4 follow-up is therefore DTB-backed validation on the existing QEMU lane before more invasive kernel or board-specific debugging.
- the Pi 4 DTB-backed QEMU validation is now complete: with an explicit `bcm2711-rpi-4-b.dtb`, the `raspi4b` lane reaches `pl011-tty: started`, which means the loader, kernel handoff, and enough userspace startup are now alive on the board-shaped emulation path.
- current local QEMU `10.2.2` `raspi4b` does not support `dumpdtb`; Pi 4 emulator validation therefore needs an explicit external DTB source rather than relying on QEMU to generate one.
- the next fast-lane blocker is now shared between the generic and Pi 4 QEMU lanes: both reach `pl011-tty: started` but not `tty0` or console readiness, so the next smallest step should target shared `pl011-tty` registration rather than more Pi 4 DTB or loader work.
- a bounded driver-local `create_dev()` retry experiment in `pl011-tty` did not change either QEMU lane, so that patch was reverted and should not be reused as if it were a proven fix.
- the next smallest high-signal step is now raw UART-side registration diagnostics in `pl011-tty`, because current stderr-only failure paths are not visible on the captured QEMU serial output.
- raw UART-side `pl011-tty` diagnostics now prove that both the generic and Pi 4 DTB-backed QEMU lanes reach `pl011-tty: register tty0` and then stop before either success or failure is reported.
- the current shared blocker is therefore inside the common `create_dev("/dev/tty0")` path, not in the driver-local code before or after that call.
- a bounded `libphoenix`-side `debug()` probe inside `create_dev()` produced no visible new markers on either QEMU lane and was reverted; that `debug()` path is not a useful early-boot visibility mechanism here.
- kernel-side syscall diagnostics now prove that the generic QEMU lane returns from `lookup("devfs", ...)` before hanging, and never reaches the final `msgSend()` marker for `tty0`; the live boundary is therefore between lookup return and final `msgSend()` entry inside `create_dev()`.
- the Pi 4 DTB-backed lane still does not show kernel-side markers, so the generic lane remains the authoritative fast diagnostic lane for this early `create_dev()` blocker.
- a temporary stdout-visible probe inside `libphoenix/create_dev()` also produced no visible new markers on either QEMU lane and was reverted; plain fd-1 writes are therefore not a useful early visibility path here either.
- a local raw `pl011-tty` helper now proves that the first `lookup("devfs", ...)` fails quickly on both the generic and Pi 4 DTB-backed QEMU lanes, so the first `/dev/tty0` registration attempt does not reach the create-message path at boot time.
- a temporary `dummyfs` experiment that removed the non-filesystem namespace `write(1, "", 0)` wait changed nothing on either lane and was reverted, so that startup gate is not the blocker behind the missing `devfs` lookup result.
- the bounded `pl011-tty` retry-window experiment is now complete on both QEMU lanes: each lane reaches `pl011-tty: tty0 lookup retry` and then stalls before either `lookup ok` or `lookup failed`, which means a later `lookup("devfs", ...)` call is now blocking instead of returning promptly.
- the next bounded blocker is therefore no longer inside the first raw `pl011-tty` helper branch itself; it is whether the `dummyfs` `devfs` instance reaches its main loop and actually receives or responds to the later `mtLookup` message.
- bounded `dummyfs` startup markers now prove on the generic lane that the non-filesystem `devfs` instance registers and reaches `initialized` only after the first `pl011-tty: tty0 lookup retry` marker, so `devfs` startup is genuinely late relative to the first tty-registration attempt.
- that same generic run still never reaches a non-filesystem `dummyfs: lookup recv` marker, which means the blocked later `lookup("devfs", ...)` path is not reaching the later `devfs` instance at all.
- the next bounded diagnostic target is therefore the root dummyfs instance, because unresolved `lookup("devfs", ...)` calls still travel through the root lookup path until `devfs` exists in the kernel name cache.
- the Pi 4 DTB-backed lane still shows no visible `dummyfs` markers in this step, so the generic lane remains the authoritative fast diagnostic source for this namespace-resolution blocker.
- the relabeled follow-up markers now show that the later startup markers are definitively coming from the `devfs` instance itself: `dummyfs: devfs registered` and `dummyfs: devfs initialized`.
- no `dummyfs: root ...` marker appears on the generic lane, and the current generic / Pi 4 `user.plo` images do not start a root dummyfs instance at all; the previous root-dummyfs hypothesis is therefore invalid for the current fast-lane image shape.
- the next bounded diagnostic target is now the kernel name-service layer in `proc/name.c`, because that is where `/` registration state and `lookup("devfs", ...)` branch selection actually live.
- the filtered `proc/name.c` trace is now in place on the generic lane and proves that the first `lookup("devfs")` takes the `name: devfs no root` fast-failure path, then `devfs` registers later as `name: register devfs`.
- after that first retry marker, there is still no second `create_dev: lookup devfs` entry and no second `name: devfs ...` branch marker at all, which means the retry loop is not re-entering the kernel lookup path during the observed boot window.
- the next bounded blocker is therefore no longer kernel name resolution; it is whether the `pl011-tty` retry loop ever wakes up from its `usleep(100000)` call.
- the raw post-`usleep()` marker is now in place on both QEMU lanes and never appears before timeout, which means the first bounded retry path sleeps and never wakes on both generic `virt` and Pi 4 DTB-backed `raspi4b`.
- the next bounded blocker is therefore inside the common sleep / timer wakeup path rather than inside `pl011-tty` retry control flow or a second `devfs` lookup.
- the new `proc/threads.c` markers now prove on the generic lane that the blocked retry path reaches `proc_threadNanoSleep()` and that `_threads_programWakeup()` does program a wakeup deadline.
- that same generic lane still never reaches `threads_timeintr()` before timeout, so the next bounded blocker is the common AArch64 timer source / IRQ-delivery path after wakeup programming rather than sleep enqueue itself.
- the Pi 4 DTB-backed lane still does not expose the new kernel-side `threads:` markers in this boot slice, so the generic lane remains the authoritative fast diagnostic lane for the missing timer interrupt.
- the common AArch64 timer frontend is now visible on the generic lane and selects `physical-nonsecure irq 30`.
- the first wakeup arm that follows the blocked `pl011-tty` retry reaches `gtimer_timer.c` as `gtimer: arm 1000 us`, which matches the current scheduler wakeup cap rather than the original `100000 us` sleep request.
- even with that explicit timer-source and arm visibility, the generic lane still never reaches `threads_timeintr()`, so the next bounded blocker is now the GIC-side timer-handler registration / dispatch path.
- the selected timer IRQ is now also visible on the GIC side: the generic lane reaches `gic: timer handler set`, which proves IRQ 30 handler registration succeeds.
- that same generic lane never reaches `gic: timer dispatch`, so the remaining bounded blocker is the timer-source / interrupt-generation side before GIC dispatch, not handler registration.
- the virtual-first timer-source experiment is now complete and negative: the generic lane changes from `physical-nonsecure irq 30` to `virtual irq 27`, but dispatch is still absent.
- the next bounded code clue is therefore not timer-source preference between those two architectural timers; it is explicit GIC configuration for timer PPIs, because the current generic AArch64 GIC path configures SPIs but does not explicitly configure PPIs.
- the explicit GIC PPI-configuration experiment is also negative: even after configuring non-SGI IRQs during handler registration, the generic lane still never reaches `gic: timer dispatch`.
- the remaining narrow common path is now the architectural timer sysreg write sequence itself, so the next bounded experiment is explicit synchronization after timer control and timer-value writes.
- the architectural-timer write-barrier experiment is also negative: even after explicit post-write barriers on both physical and virtual timer sysreg writes, the generic lane still never reaches `gic: timer dispatch`, `threads: timer irq`, or `pl011-tty: tty0 wake`.
- the next bounded timer clue is now register state rather than write ordering: the next common AArch64 experiment should read back the selected timer control state and timer value immediately after wakeup programming so the fast lane can distinguish failed arming from later interrupt-delivery loss.
- the architectural-timer register-readback experiment is now complete and high-signal: the generic fast lane reports `gtimer: arm 1000 us ctl 0x1 tval 58836`, which means the selected timer is genuinely armed with a live non-zero countdown.
- the next bounded clue is therefore GIC-side state for that IRQ rather than timer programming; the next common AArch64 experiment should expose the selected timer IRQ's interrupt-group and enable readback after handler registration.
- the GIC timer-state visibility step is now also high-signal: the generic fast lane reports `gic: timer handler set grp 0 en 0`, which means the selected timer IRQ still reads back as Group 0 and disabled immediately after registration.
- `sources/plo/hal/aarch64/generic/_init.S` exits EL3 to EL1 non-secure, and `sources/plo/hal/aarch64/zynqmp/_init.S` already documents and implements moving interrupts to Group 1 so non-secure code can manage them.
- the next bounded fix is therefore a timer-only Group 1 experiment in the kernel GIC path rather than another timer-programming change.
- the timer-only kernel Group 1 experiment is also negative: even after explicitly moving only the selected timer IRQ to Group 1 in the kernel GIC path, the generic fast lane still reads back `gic: timer handler set grp 0 en 0`.
- the next bounded boundary is therefore above the kernel in generic `plo` EL3 setup; the smallest next experiment is to initialize generic loader GIC state for Group 1 before the non-secure EL1 handoff.
- the generic `plo` EL3 GIC initialization experiment is the first major boundary break on the fast lane: the generic `virt` path now reaches `gic: timer dispatch`, `threads: timer irq`, `pl011-tty: tty0 wake`, `pl011-tty: tty0 ready`, `pl011-tty: console ready`, and visible later kernel startup logs.
- the Pi 4 DTB-backed `raspi4b` lane remains unchanged after that same loader-side fix, so the next bounded clue is the loader entry EL on the Pi 4 path rather than another generic timer or GIC change.
- the generic loader entry-EL visibility step is now complete: both the working generic `virt` lane and the stuck Pi 4 `raspi4b` lane enter `plo` at `EL3`.
- the next strongest Pi 4 clue is now the DTB itself: the current `RPI4B_DTB_PATH` input is only a 274-byte stub containing `compatible` plus one memory bank, which is not a real Pi 4 board tree.
- Pi 4 `raspi4b` validation is now rerun against the official Raspberry Pi firmware DTB from `raspberrypi/firmware` commit `63ad7e7980b030cb4649ecedf2255c9226e5a1e8`, path `boot/bcm2711-rpi-4-b.dtb`, size `56373` bytes.
- that official DTB materially changes the Pi 4 QEMU boundary: instead of reaching the old `pl011-tty: tty0 lookup retry` stall, the lane now stops earlier after `cmd: Executing pre-init script` and `alias: Setting relative base address to 0x0000000000200000`, with no later kernel or user-space logs.
- the old 274-byte stub DTB was therefore masking an earlier Pi 4-specific loader-side blocker, and the next bounded diagnostic target is now the `plo` `call ram0 user.plo` path rather than later kernel or user-space startup.
- the next bounded Pi 4 loader split is now fixed: add tightly filtered `plo/cmds/call.c` markers for open success, magic success, and each parsed line before `cmd_parse()` so the lane can be divided into pre-open, pre-read, pre-first-command, or first-command execution failure.
- that filtered `plo/cmds/call.c` visibility is now complete and high-signal: with the official firmware DTB, Pi 4 executes the entire `user.plo` script through `kernel ram0`, `blob ram0 system.dtb ddr`, both `app` commands, and `go!`, but still never prints the kernel banner.
- the current Pi 4 boundary is therefore no longer in pre-init or script execution; it is now strictly post-`go!`, inside `cmd_go()`, `hal_done()`, `hal_cpuJump()`, or the immediate handoff after `hal_cpuJump()`.
- the next bounded post-`go!` split is now fixed: add raw `go:` markers in `plo/cmds/go.c` around `devs_done()`, `hal_done()`, and the `hal_cpuJump()` call so the Pi 4 handoff can be divided without widening into HAL instrumentation yet.
- that filtered `plo/cmds/go.c` visibility is now complete and high-signal: both lanes reach `go: enter`, `go: devs done`, `go: hal done`, and `go: jump`, but only the generic lane reaches the kernel banner afterward.
- the current Pi 4 boundary is therefore no longer in `cmd_go()` cleanup; it is now strictly inside `hal_cpuJump()` or the immediate EL-exit handoff path in generic AArch64 `plo`.
- the next bounded jump-path split is now fixed: add raw `hal:` markers in `plo/hal/aarch64/generic/hal.c` around `hal_interruptsDisableAll()` and the call into `hal_exitToEL1()` so the C-side jump path can be exhausted before any assembly changes are made.
- that filtered `plo/hal/aarch64/generic/hal.c` visibility is now complete and high-signal: both lanes reach `hal: jump entry`, `hal: jump irq off`, and `hal: jump exit el1`, but only the generic lane reaches the kernel banner afterward.
- the current Pi 4 boundary is therefore no longer in C-side loader handoff code; it is now strictly inside the assembly EL transition in `plo/hal/aarch64/generic/_init.S` or in the first kernel instructions after that transition.
- the next bounded assembly-side split is now fixed: add tiny raw UART markers in `plo/hal/aarch64/generic/_init.S` at `hal_exitToEL1()` entry and immediately before the EL-specific transfer instruction so the loader assembly boundary can be divided before any kernel-side instrumentation is introduced.
- that assembly-side EL-exit visibility is now complete and high-signal: both lanes reach the EL3 transfer marker `A3`, but only the generic lane reaches the kernel banner afterward.
- the Pi 4 lane also prints repeated assembly markers such as `AAA333` and a later `A3`, which strongly suggests that multiple cores are taking the same generic loader EL3 handoff path during the Pi 4 `-smp 4` run.
- generic `virt -smp 4` now confirms that repeated EL3 handoff markers are a generic multi-core loader behavior, not the Pi 4 failure by themselves, because the generic lane still reaches the kernel banner and later startup logs.
- the current generic kernel target still declares `NUM_CPUS 1U` in `phoenix-rtos-kernel/hal/aarch64/generic/config.h`, so handing off multiple loader CPUs into this target is at least a design mismatch even though generic `virt -smp 4` happens to boot.
- the generic secondary-core containment experiment is now complete: generic `plo` keeps non-boot CPUs parked across the current handoff, generic `virt -smp 4` still reaches the kernel banner, and Pi 4 `raspi4b -smp 4` now shows only a single `A3` before timing out.
- secondary-core release was therefore not the root cause of the Pi 4 failure after the EL3 transfer; it was only adding noisy repeated handoff markers.
- the next bounded split is now the earliest visible generic AArch64 kernel entry point after that single `A3` marker.
- the earliest-kernel-entry visibility step is now scoped: add a raw PL011 marker at generic kernel `_start`, using project `board_config.h` for the early UART base on both generic QEMU and Pi 4, and keep the change limited to `hal/aarch64/_init.S` plus generic config glue.
- the earliest-kernel-entry visibility step is now complete: both generic QEMU and Pi 4 print `K` immediately after the loader-side `A3`, so Pi 4 definitely reaches generic kernel `_start`.
- the next bounded early-init clue is now the `__TARGET_AARCH64A53` system-register block in `hal/aarch64/_init.S`, because the active Pi 4 lane still builds as `aarch64a53` while QEMU `raspi4b` is running `-cpu cortex-a72`.
- the post-entry A53-block split is now complete too: Pi 4 prints `KLM`, so it gets past that block and still dies later in early kernel init.
- the strategic pivot is now explicit: Raspberry Pi 4 is BCM2711 with a quad-core Cortex-A72 CPU, so `aarch64a53-generic-rpi4b` should be treated only as a temporary diagnostic lane.
- the official Raspberry Pi 4 specifications page remains the authority for this CPU identity: BCM2711, quad-core Cortex-A72 (ARM v8) 64-bit SoC, so future Pi 4 target naming, CPU assumptions, and runtime validation should stay centered on the A72 lane.
- the next bounded implementation step should therefore enable a real Cortex-A72-capable generic target path, starting with removal of the first hard `aarch64a53` generic naming assumptions in `plo`.
- that first A72-enabling groundwork is now complete in `plo`: generic loader config can select `phoenix-aarch64a72-generic.elf` plus `ld/aarch64a72-generic.ldt`, while the existing A53 generic lanes still build cleanly.
- the first local `aarch64a72-generic-rpi4b` scaffold is now in place across build, project, filesystems, devices, and utils, and the new A72 Pi 4 build plus the preserved A53 generic QEMU and Pi 4 builds all complete successfully in `phoenix-dev`.
- the first `aarch64a72-generic-rpi4b` runtime validation is now complete: the A72 Pi 4 lane selects `phoenix-aarch64a72-generic.elf` but still reaches the same `A3KLM` boundary as the A53 diagnostic lane.
- the generic AArch64 identification strings are now corrected: the A72 Pi 4 loader lane reports `Cortex-A72 Generic`, the A53 lane still reports `Cortex-A53 Generic`, and the kernel-side generic platform names are now target-aware too.
- a negative experiment is now recorded too: raw UART probes placed after the `ttbr1_el1` switch are not valid on this path, because neither lane prints them and the generic fast lane regresses until the patch is reverted.
- the first C-entry visibility split is now complete: the generic lane reaches both `hal: console init done` and `main: hal init done`, while Pi 4 reaches neither.
- the generic console-init split is now complete too: the generic lane reaches `console: pl011 init done`, while Pi 4 still reaches none of the console-init markers.
- the strongest concrete blocker is now DTB address handling: the official Pi 4 firmware DTB uses bus-address serial nodes such as `serial@7e201000` plus `/soc/ranges` mapping to CPU-visible `0xfe...` space, and Phoenix still parses serial `reg` values without applying that translation.
- the Pi 4 serial DTB fix is now in place: the kernel DTB parser decodes `/soc` serial `reg` cells with the parent cell width and translates serial MMIO through `/soc/ranges`, while the generic `virt` lane still reaches the established tty / console-ready boot band.
- that same fix moves the Pi 4 A72 `raspi4b` lane much later: it now reaches `console: pl011 init done`, `hal: console init done`, `main: hal init done`, and the kernel banner before faulting with `Exception #37: Data Abort (EL1)`.
- the current Pi 4 fault symbolizes to `_map_init` in `vm/map.c` (`pc=...b198` -> line `1638`, `lr=...b0cc` -> line `1624`), so the active blocker is now well past early serial and into later kernel initialization.
- the official firmware DTB from `raspberrypi/firmware` also decompiles to `memory@0 { reg = <0x00 0x00 0x00>; }`, and Raspberry Pi documentation states that the firmware customizes the DTB before kernel handoff.
- the official Raspberry Pi kernel sources are now a preferred source for board intent over decompiling the already-built DTB:
  - `raspberrypi/linux` `rpi-6.12.y` and `rpi-6.19.y` both keep `bcm2711-rpi.dtsi` `memory@0` marked `Will be filled by the bootloader`
  - `bcm2711-rpi-4-b.dts` on those branches keeps `chosen { stdout-path = "serial1:115200n8"; }` with the comment `8250 auxiliary UART instead of pl011`
- that source-level confirmation means future DT analysis should not assume `stdout-path` alias resolution is immediately useful for the current Phoenix PL011 console path, and it also reinforces that direct `raspi4b` QEMU is missing firmware-time DTB customization
- the next bounded Pi 4 clue is therefore QEMU-specific: direct `raspi4b` validation is using an uncustomized firmware DTB without the Raspberry Pi firmware in the loop, so the next smallest step should validate a QEMU-only payload-DTB memory fix before widening into general VM or memory-management debugging.
- that one-off QEMU-only `memory@0/reg = <0x00 0x00 0x80000000>` experiment is now also complete and negative, so the next smallest step is to instrument the live `_vm_init` / `_map_init` boundary instead of adding speculative DTB automation first.
- the `_vm_init` / `_map_init` visibility step is now complete and high-signal: Pi 4 reaches `vm: enter`, `vm: page init done`, `vm: map init`, `map: enter`, `map: pool link`, and then explicitly `map: zero free` before aborting inside `_map_init`.
- the current Pi 4 exception now symbolizes to `_map_init` lines `1644-1645`, immediately after the new zero-free marker, which confirms the live failure is the `map_common.nfree - 1U` underflow path rather than a later VM issue.
- the strongest current root cause is now earlier DTB-backed memory-bank parsing:
  - `hal/aarch64/dtb.c:dtb_parseMemory()` still hardcodes a 16-byte `<addr,size>` assumption
  - the Pi 4 root memory node uses root cell widths instead (`#address-cells = 2`, `#size-cells = 1`)
  - that explains why the earlier one-off Pi 4 QEMU memory-size patch was negative: Phoenix never parsed that 3-cell memory node at all
- the root-memory parser fix is now in place: `hal/aarch64/dtb.c` decodes root
  memory banks with the DTB root cell widths, and the generic `virt` lane still
  reaches the established later boot band.
- that fix also cleanly separates the remaining Pi 4 issues:
  - direct `raspi4b` QEMU with the unmodified official firmware DTB still hits
    `map: zero free`, because QEMU is not performing the Raspberry Pi
    firmware-time `memory@0/reg` population
  - the same Pi 4 lane with a one-off `memory@0/reg = <0x00 0x00 0x80000000>`
    DTB patch now moves past `_map_init`, reaches `vm: map init done`, and
    stalls later after `dummyfs: devfs initialized`
- the Raspberry Pi source-reference rule is now stronger too:
  - `rpi-6.19.y` and `rpi-7.0.y` currently carry identical Pi 4
    `bcm2711-rpi-4-b.dts` and `bcm2711-rpi.dtsi`
  - future DT debugging should consult both Raspberry Pi Linux DTS sources and
    the Raspberry Pi device-tree documentation, not only decompiled DTBs
- the Pi 4 A72 project build now has a narrow QEMU-only DTB memory hook:
  `RPI4B_QEMU_MEMORY_SIZE=80000000` patches `memory@0/reg` in both staged DTB
  copies without changing the default real-device DTB path.
- that automated hook is validated:
  - `fdtget` on the staged boot DTB now returns `0 0 -2147483648`
  - the `raspi4b` lane reaches the same later boundary as the manual patched
    DTB run:
    - `vm: map init done`
    - `gtimer: source virtual irq 27`
    - `gic: timer handler set grp 1 en 1`
    - `dummyfs: devfs initialized`
  - then stalls before any visible `gic: timer dispatch`, `threads: timer irq`,
    or `pl011-tty: tty0 wake`
- the next bounded runtime experiment is now selected too:
  - the current common policy prefers the virtual timer first
  - the Pi 4 patched lane currently reports `gtimer: source virtual irq 27`
  - the next smallest follow-up is to force the Pi 4 patched lane to the
    non-secure physical timer and compare whether dispatch resumes
- that physical-timer experiment is now complete and negative:
  - the Pi 4 patched lane now reports `gtimer: source physical-nonsecure irq 30`
  - it still reaches `gic: timer handler set grp 1 en 1`,
    `threads: wakeup programmed`, and `dummyfs: devfs initialized`
  - it still does not reach `gic: timer dispatch`, `threads: timer irq`, or
    `pl011-tty: tty0 wake`
- timer-source choice is therefore no longer the most likely blocker; the next
  bounded question is whether the selected timer IRQ ever becomes pending in
  the Pi 4 GIC at all
- the next experiment is now fixed:
  add one first-arm timer-IRQ pending probe so the Pi 4 lane can be divided
  into:
  - timer never asserted into the GIC
  - timer asserted but still never dispatched
- that pending-state probe is now complete and high-signal:
  - generic `virt` reports `gtimer: pending 1`
  - Pi 4 A72 patched lane reports `gtimer: pending 0`
  - so the current Pi 4 blocker is earlier than GIC dispatch or CPU-interface
    handling; the selected timer IRQ is not even reaching pending state in the
    bounded probe window
- the next bounded timer-side question is now explicit: whether the Pi 4 timer
  is actually counting down after the first arm or remains inert before ever
  reaching pending state
- that timer-countdown readback is now complete and high-signal:
  - generic `virt` reports `gtimer: pending 1` and `gtimer: post 2000 us ctl 0x5 ...`
  - Pi 4 A72 patched lane reports `gtimer: pending 0` and `gtimer: post 2000 us ctl 0x5 ...`
  - so the Pi 4 timer does expire locally, but the interrupt still does not
    appear in the current GIC pending view or dispatch path
- the external bare-metal reference sweep is now complete enough to be useful:
  - `rpi4-osdev`, `rpi-os`, and the OSDev bare-bones article all reinforce the
    Pi 4 basics around low peripheral mode, `0x80000` AArch64 boot, and simple
    CPU containment
  - Circle is the strongest external reference for the current Phoenix seam
    because it explicitly uses the Pi 4 non-secure physical timer path, requires
    the physical counter on Pi 4, and maps that timer to GIC PPI 14 -> IRQ 30
- the next bounded diagnostic target is now explicit again:
  compare the current `ISPENDR`-based timer pending readback with a private
  pending-state readback for the same Pi 4 timer PPI
- that private-pending-state readback is now complete and negative:
  - generic `virt` reports `gtimer: pending 1` but `gtimer: ppi pending 0`
  - Pi 4 A72 patched lane reports `gtimer: pending 0` and `gtimer: ppi pending 0`
  - so `PPISR` is not the missing view that explains the Pi 4 gap
- the most visible remaining runtime difference is now the timer group:
  - generic lane timer registration reads back `grp 0 en 1`
  - Pi 4 lane timer registration reads back `grp 1 en 1`
- that bounded Group 0 experiment is now complete and negative:
  - generic `virt` remains healthy with `gic: timer handler set grp 0 en 1`
  - Pi 4 A72 patched lane can also be forced to `gic: timer handler set grp 0 en 1`
  - Pi 4 still reports `gtimer: pending 0` and `gtimer: ppi pending 0`
  - Pi 4 still never reaches `gic: timer dispatch`
- the timer-group difference is therefore ruled out as the last missing
  variable, and the next move should restore the Pi 4 board-config baseline
  before testing a new timer-routing hypothesis
- that baseline restore is now complete too:
  - Pi 4 A72 lane is back to `gic: timer handler set grp 1 en 1`
  - Pi 4 still reports `gtimer: pending 0` and `gtimer: ppi pending 0`
  - Pi 4 still never reaches `gic: timer dispatch`
- the next bounded hypothesis is now the BCM2711 local interrupt controller:
  Circle enables `ARM_LOCAL_TIMER_INT_CONTROL0` bit 1 for the non-secure
  physical timer and checks `ARM_LOCAL_IRQ_PENDING0`, while Phoenix currently
  has no code touching that local interrupt block
- that local-route-enable experiment is now complete too:
  - Pi 4 A72 lane writes and reports `gic: local timer route 0x2`
  - Pi 4 still reports `gtimer: local pending 0x0`
  - Pi 4 still reports `gtimer: pending 0` and no `gic: timer dispatch`
- the route-enable write alone is therefore not the missing piece
- the next bounded local-block variable from Circle is the Pi 4 prescaler
  write in `external/circle/lib/sysinit.cpp`:
  `ARM_LOCAL_PRESCALER = 39768216U`
- that prescaler experiment is now complete and negative too:
  - Pi 4 A72 lane logs `gic: local prescaler 39768216`
  - Pi 4 still reports `gtimer: local pending 0x0`
  - Pi 4 still never reaches `gic: timer dispatch`
- the most important new source-level finding is now QEMU-specific:
  local QEMU 10.2.2 source shows that `raspi4b` wires:
  - `GTIMER_PHYS -> GIC PPI 14` in `hw/arm/bcm2838.c`
  - unlike `hw/arm/bcm2836.c`, the Pi 4 QEMU model does not put the CPU
    physical timer through `bcm2836_control` first
- so the local-controller detour is not on the active Pi 4 QEMU timer path,
  and the next bounded move should restore that fast lane baseline before
  resuming direct GTIMER-to-GIC debugging
- that fast-lane restore is now complete:
  - Pi 4 QEMU is back to `gic: timer handler set grp 1 en 1`
  - Pi 4 QEMU again reports only `gtimer: pending 0` and
    `gtimer: ppi pending 0`
  - no local-controller traces remain in the active fast lane
- the next direct-GIC split should now probe the CPU-interface pending view
  itself through `GICC_HPPIR`
- that `GICC_HPPIR` probe is now complete:
  - generic lane reports `gtimer: hppir 1023` after a successful dispatch
  - Pi 4 QEMU lane reports `gtimer: hppir 0` while dispatch is still absent
- the next bounded follow-up should stay read-only and explain the Pi 4
  CPU-interface view, most likely by probing the alias or alternate pending
  register path before changing any timer or interrupt policy
- the next concrete Pi 4 boot blocker is now loader MMIO addressing: `sources/plo/hal/aarch64/generic/config.h` still hardcodes QEMU `virt` UART and GIC base addresses, so the current Pi 4 `kernel8.img` would still talk to the wrong MMIO blocks on real hardware until those addresses are made board-overridable.
- generic `plo` now accepts project-local MMIO base overrides for UART0 and GICv2 while preserving the current QEMU `virt` defaults, and the generic `virt` smoke lane still boots after that change.
- the current Pi 4 firmware handoff no longer appears to have a raw loader placement mismatch: `kernel_address=0x40080000` in the Pi 4 `config.txt` matches `ADDR_PLO 0x40080000` in `plo/ld/aarch64a53-generic.ldt`.
- the next Pi 4 deployment blocker is now firmware-file completeness rather than loader placement: the staged `_boot/.../rpi4b/` tree still lacks Raspberry Pi firmware files, so it is not yet a self-contained first-partition boot bundle.
- the Pi 4 project now accepts an operator-supplied Raspberry Pi firmware directory through `RPI4B_FIRMWARE_DIR` or `_projects/aarch64a53-generic-rpi4b/firmware` and stages required firmware files such as `start4.elf` and `fixup4.dat` into `_boot/.../rpi4b/` while keeping default no-firmware builds green.
- future agents are explicitly allowed to source Pi 4 firmware files and the board DTB from the Raspberry Pi firmware repository boot tree at `https://github.com/raspberrypi/firmware/tree/master/boot` when that is the most direct path to a testable boot bundle; the exact required file set should still be re-verified against the active Pi 4 firmware baseline.
- `gdb-multiarch` `15.1` is now installed in `phoenix-dev`, and the QEMU gdbstub is now a proven low-level inspection lane for the current Pi 4 bring-up work.
- the decisive Pi 4 QEMU blocker was not timer policy after all; it was DTB GIC discovery:
  - at `_hal_interruptsInit + 64`, generic returned pre-map
    `gicd = 0x08000000`, `gicc = 0x08010000`
  - the same Pi 4 breakpoint originally returned
    `gicd = 0x0`, `gicc = 0x0`
- the bounded `hal/aarch64/dtb.c` fix is now in place:
  - shallow `/soc/interrupt-controller@...` nodes are recognized
  - `/soc` GIC `reg` tuples are decoded through `/soc` cell widths and
    `ranges` translation
  - the old non-`/soc` fallback path remains intact
- after that fix, the same Pi 4 pre-map breakpoint now returns:
  - `gicd = 0xff841000`
  - `gicc = 0xff842000`
- the Pi 4 `raspi4b` QEMU lane now reaches the same early boot band as the
  generic lane, including:
  - `gic: timer dispatch`
  - `threads: timer irq`
  - `pl011-tty: tty0 ready`
  - `pl011-tty: console ready`
  - `main: Starting syspage programs ...`
  - `dummyfs: initialized`
- bounded syspage spawn visibility now proves that both generic and Pi 4 lanes
  successfully spawn:
  - `dummyfs`
  - `pl011-tty`
  - `psh`
- the first bounded interactive generic PTY probe now shows that serial input
  is echoed back but still produces no visible shell prompt or command output
- the active later-boot boundary is therefore now inside the shared `psh`
  startup path rather than in the kernel syspage spawn loop
- the bounded `psh` startup-marker experiment is now complete and negative on
  both generic and Pi 4:
  - neither lane prints even `psh: root ready` or `psh: run enter`
  - both lanes still stop visibly after `dummyfs: initialized`
- the first below-stdio hook is now complete and positive on both lanes:
  - generic prints `threads: psh user scheduled`
  - Pi 4 prints `threads: psh user scheduled`
- `psh` therefore does reach first user-mode execution on both lanes
- the next bounded visibility step should now move to the earliest `psh`-local
  syscall result, starting with `lookup("/")`
- the bounded `psh` root-lookup success trace is now complete and still
  negative on both lanes:
  - generic shows no `syscalls: psh root lookup ok`
  - Pi 4 shows no `syscalls: psh root lookup ok`
- the current ambiguity is now narrower:
  - `psh` may be looping on failed `lookup("/")`, or
  - `psh` may not be reaching that syscall path yet
- the first-result `psh` root-lookup trace is now complete and decisive on both
  lanes:
  - generic prints `syscalls: psh root lookup -22`
  - Pi 4 prints `syscalls: psh root lookup -22`
- the shared blocker is therefore no longer “missing rootfs” but a shared
  `lookup()` contract failure returning `EINVAL`
- the root-dummyfs fast-lane image fix is now in place in
  `phoenix-rtos-project`:
  - both generic and Pi 4 start a root `dummyfs-root` instance before the
    existing `dummyfs;-N;devfs;-D` instance
  - the extra boot alias is necessary because
    `phoenix-rtos-project/_targets/build.common:b_mkscript_user()` aliases each
    `app` payload by basename, so two `dummyfs` payloads collide in `plo`
- both fast lanes now recover past the old rootfs stall and print:
  - `name: register /`
  - `dummyfs: root initialized`
  - `syscalls: psh root lookup 0`
  - `psh: root ready`
  - `psh: app run`
  - `psh: run enter`
  - `psh: tty open`
  - `psh: app done`
- the next shared later-boot blocker is therefore inside
  `psh_ttyopen("/dev/console")`, before `psh: tty ready`
- the bounded `psh_ttyopen()` failure trace is now complete and identical on
  both lanes:
  - generic prints `psh: tty open fail open -2`
  - Pi 4 prints `psh: tty open fail open -2`
- the next shared blocker is therefore even narrower:
  `open("/dev/console", O_RDWR) -> -ENOENT` despite
  `pl011-tty: console ready`
- the first kernel-side `/dev/console` lookup experiment is now also bounded:
  - a `syscalls_lookup()` trace for `/dev/console` never fires on the generic
    fast lane
  - source review of `phoenix-rtos-kernel/posix/posix.c:posix_open()` explains
    why: the open path goes through `proc_lookup()` directly
- the next bounded trace should therefore move to `posix_open()` or
  `proc_lookup()` rather than adding more noise to `syscalls_lookup()`
- the `posix_open()` console trace is now also complete and negative on both
  fast lanes:
  - generic prints no `posix: psh console open ...`
  - Pi 4 prints no `posix: psh console open ...`
- the libphoenix `open()` console trace is likewise negative on both lanes:
  - generic prints no `open: console ...`
  - Pi 4 prints no `open: console ...`
- built-image inspection and a bounded QEMU gdbstub session now prove the real
  live call path:
  - `psh_ttyopen("/dev/console")` really does call the linked libphoenix
    `open()` symbol in the built `psh` image
  - inside `open()`, `stat("/dev/console")` returns `-1`
  - `open()` then falls through to `resolve_path("/dev/console", ..., 1, 1)`
  - `resolve_path()` returns `NULL`
  - `sys_open()` is therefore never reached
- the current shared blocker is no longer a call-path mismatch and no longer a
  kernel-side open syscall issue; it is now specifically the libphoenix path
  canonicalization path for `/dev/console`
- the earlier silent libphoenix trace is therefore a visibility limitation of
  that trace path, not evidence that `open()` was skipped
- a second bounded QEMU gdbstub pass now proves the failure is even earlier
  than the `console` leaf:
  - the first `resolve_path()` call reached from `stat()` fails with
    `errno = ENOENT`, `partial = "/dev"`, `is_leaf = 0`
  - the second `resolve_path()` call reached directly from `open()` fails the
    same way, also at intermediate `/dev`, even with `allow_missing_leaf = 1`
- the shared fast-lane blocker is therefore:
  `lookup("/dev") -> -ENOENT` inside libphoenix path resolution
- source review of `dummyfs` and project files makes the likely reason explicit:
  - the root `dummyfs-root` instance auto-populates only `/syspage`
  - the `dummyfs;-N;devfs;-D` instance registers `devfs` in the non-filesystem
    namespace, not at `/dev`
  - the current generic and Pi 4 fast-lane projects do not stage the usual
    pre-shell `devfs -> /dev` bind path used by established projects
- the pre-shell `/dev` fast-lane fix is now in place in
  `phoenix-rtos-project`:
  - both fast-lane projects stage `psh` aliases for `mkdir` and `bind`
  - both fast-lane `user.plo` scripts now run:
    - `mkdir /dev`
    - `bind devfs /dev`
    before the final `psh` app
- that fix is validated on both emulator lanes:
  - generic `virt` now reaches:
    - `psh: tty ready`
    - `psh: readcmd`
    - `(psh)%`
  - Pi 4 `raspi4b` now reaches the same prompt band:
    - `psh: tty ready`
    - `psh: readcmd`
    - `(psh)%`
- the obsolete `/dev/console` investigation probes are now removed again from:
  - `psh`
  - `libphoenix`
  - kernel lookup / posix open paths
- both emulator lanes still reach the same prompt band after that cleanup, so
  the prompt-reaching fast lane now exists without the old console-specific
  diagnostic scaffolding
- the first command-level shell smoke has now been attempted with `expect`:
  - Pi 4 `raspi4b` succeeds with the built-in `help` command:
    - `(psh)% help`
    - `Available commands:`
    - returned `(psh)%`
  - generic `virt` now also succeeds after a runtime-only launch fix:
    - switching from `-serial mon:stdio -serial null -display none`
      to `-nographic -monitor none`
    - `(psh)% help`
    - `Available commands:`
    - returned `(psh)%`
- the first command-level smoke is therefore now common to both fast QEMU
  lanes, and the earlier generic-only failure is confirmed to have been a QEMU
  stdio-launch issue rather than a Phoenix shell bug
- that smoke is now packaged in:
  - `scripts/qemu-shell-smoke.sh generic`
  - `scripts/qemu-shell-smoke.sh rpi4b`
- both helper runs print the same compact success markers and point to the
  corresponding VM-side log file under `/tmp`
- the first candidate external-applet smoke, `ls /`, is not a good fit for the
  current fast lane:
  its natural markers like `dev` collide with earlier boot-band text such as
  `create_dev: ...`, so the next applet smoke should use a unique token instead
- the next candidate, `echo codex-smoke-echo`, is also not a valid proof shape:
  the token appears in the echoed command line itself, so it still does not
  independently prove applet stdout
- the first clean external-applet smoke is now proven on both fast lanes with:
  - `echo -h`
  - `Usage: echo [options] [string]`
  - returned `(psh)%`
- the first no-hardware Pi 4 firmware-artifact helper is now in place:
  - `scripts/assemble-rpi4b-bootfs.sh`
- it assembles a firmware-visible boot tree containing the staged Phoenix Pi 4
  files plus Raspberry Pi firmware files under:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs`
- the next artifact layer is also now in place:
  - `scripts/assemble-rpi4b-bootfs-img.sh`
- it builds a portable FAT image at:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img`
- that FAT image is now the selected first real-device artifact for Pi 4; a
  larger SD-card image is deferred until the project actually needs more than
  the firmware-visible boot partition
- the operator-facing handoff is now also in place:
  - `scripts/export-rpi4b-fat-image.sh`
- it exports the current Pi 4 FAT image into a stable host-visible path:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-bootfs.img`
- the current validated exported-image SHA-256 is:
  - `fab57080ef7c770ac9346cfd9e86b6ef71c31d47559fe0bd955bee6b71d3a108`
- the immediate remaining operator gap is now SD-card usability rather than VM
  artifact access, because the current exported file is a raw FAT filesystem
  image rather than a full flashable disk image
- that gap is now partly closed too:
  - `scripts/assemble-rpi4b-sdimg.sh`
- it produces a VM-local full Pi 4 disk image at:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-sd.img`
- the current validated layout is:
  - DOS partition table
  - one bootable FAT32 partition
  - partition start sector `2048`
  - embedded FAT offset `1048576`
- the immediate remaining operator gap is now host visibility for that full
  disk image rather than image shape
- that host-visibility gap is now closed too:
  - `scripts/export-rpi4b-sdimg.sh`
- it exports the current Pi 4 full disk image into:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- the current validated exported full-image SHA-256 is:
  - `acfdb8c251be03a716cdd9811b151c412de1e3a11c24db76ed5a476d8fc8f107`
- the project now has a host-visible flashable Pi 4 SD-card image artifact for
  the first manual hardware trial
- the operator runbook now includes an explicit macOS flashing workflow for:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- the docs now also state the current first-boot limitation clearly:
  without USB-TTL serial, the first hardware trial is useful as an
  artifact-deployment check but not yet as a strong runtime-validation milestone
- the next non-UART observability target is now selected too:
  start with a `plo`-side Raspberry Pi mailbox framebuffer step, because it is
  visible on HDMI, testable under `raspi4b`, and narrower than early networking
  or full runtime display plumbing
- that HDMI observability step is now implemented and validated under
  `raspi4b` QEMU:
  - `plo` performs a Raspberry Pi mailbox property transaction
  - allocates a framebuffer
  - paints a visible marker rectangle in the upper-left corner
- the decisive constraint discovered during that step is now explicit:
  the current generic `plo` link address places a static mailbox request buffer
  above `0x40000000`, and that high buffer failed on the Pi 4 `raspi4b` lane;
  a bounded gdbstub experiment proved that redirecting the exact same request
  buffer to low physical memory (`0x02000000`) immediately produced a valid
  framebuffer allocation response
- the implemented source fix therefore keeps the mailbox/framebuffer logic but
  moves the Pi 4 property-request buffer into a board-provided low physical
  window instead of widening into broader graphics changes
- the currently validated Pi 4 HDMI fast-lane signature is:
  - framebuffer size `1024 x 768`
  - a small top-left staged progress panel
  - panel background pixel `(20, 20) -> (72, 72, 72)`
  - stage pixels `(48, 48)`, `(112, 48)`, `(176, 48)` all lit as
    `(240, 240, 240)` by the time `plo` reaches kernel jump
  - preserved background pixel `(639, 479) -> (160, 96, 48)`
- that visibility path is now regression-testable with one command:
  - `scripts/qemu-rpi4b-hdmi-smoke.sh`
  - it runs the existing Pi 4 QEMU lane, captures a framebuffer dump, and
    validates the current marker and background pixels
- the next smallest real-board refinement is now also implemented at the
  firmware staging layer:
  - the Pi 4 project `config.txt` now adds `hdmi_force_hotplug=1`
  - and `disable_overscan=1`
- this is intentionally narrow:
  - force HDMI mode even if hotplug detection is flaky
  - avoid default firmware overscan cropping of the upper-left marker
  - do not widen yet into explicit fixed HDMI modes or broader safe-mode
    bundles
- this is still an early `plo` visibility path only:
  it is not yet a runtime display console, windowing path, or general graphics
  subsystem
- the HDMI path is now more useful for the current no-UART lab:
  it shows three coarse loader milestones instead of only "framebuffer alive"
- that firmware refinement is now build-validated too:
  the rebuilt staged Pi 4 boot artifact contains both added `config.txt`
  lines in `_boot/aarch64a72-generic-rpi4b/rpi4b/config.txt`
- the host-visible Pi 4 SD-card artifact has now been refreshed through the
  full helper chain after that firmware change, so
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  is the current flashable image that includes the HDMI refinement
- after the staged HDMI progress implementation, the host-visible Pi 4 SD image
  has now been refreshed again, and the current validated exported full-image
  SHA-256 is:
  - `b2f3a33fe7b4e96d364b6e7579350d7c548359701cbaf0e9ac6b86fbf18860b0`
- Circle has now been reviewed in detail as a Pi 4 external reference:
  - it strongly confirms the current Phoenix early-HDMI direction:
    low-memory mailbox property requests plus firmware-allocated framebuffer
  - it also confirms that Pi 4 USB keyboard support is not a near-term shortcut
    for the current lab, because Circle reaches it through BCM2711 PCIe plus
    VL805 xHCI before HID keyboard handling
  - the detailed review is now captured in `docs/circle-reference-review.md`
- an important constraint on that choice is now explicit:
  Phoenix already has `plo` `graphmode` state, but the current AArch64 kernel
  path does not yet expose an IA32-style `pctl_graphmode` consumer, so the
  first visibility step should stop at early framebuffer life signs
- the currently available real-hardware lab is weaker than the ideal UART lab:
  microSD plus HDMI plus Ethernet plus USB keyboard or mouse are available, but
  no USB-TTL adapter is currently available
- that means the first hardware-oriented steps should prioritize boot-media
  usability and alternate observability paths, while keeping explicit notes
  that early runtime failures may remain low-visibility until UART, framebuffer,
  or network-level visibility improves
- debugger-first is now the recorded policy for QEMU runtime triage:
  future sessions should start with a bounded gdbstub inspection and only add
  source-level probes after documenting why GDB cannot answer the current
  question
- the user has explicitly deferred further SD-image refresh and manual Pi 4
  board testing until HDMI text output exists, so the current fast lane shifts
  from artifact handoff to framebuffer text enablement
- Phoenix already contains an in-tree framebuffer text renderer and bundled 8x16
  font in `phoenix-rtos-devices/tty/pc-tty/ttypc_fbcon.c` and
  `phoenix-rtos-devices/tty/pc-tty/ttypc_fbfont.h`
- the Pi 4 `plo` path already stores framebuffer geometry in the generic
  AArch64 syspage through `syspage_graphmodeSet()`, but generic AArch64 does
  not yet expose an IA32-style `pctl_graphmode` query for runtime consumers
- the shortest current route to HDMI text is therefore:
  expose generic AArch64 graphmode metadata through `platformctl`, then reuse
  the existing Phoenix framebuffer renderer instead of adding a new font stack
- copied-buildroot validation must be run sequentially per target; concurrent
  generic and Pi 4 builds against the same copied buildroot race on shared
  host-artifact paths such as `_build/host-generic-pc`
- local QEMU `10.2.2` `hw/intc/arm_gic.c` does not expose an explicit
  CPU-interface read case for offset `0x28`, so older `AHPPIR` experiments
  should not be treated as authoritative outside the exact runtime context in
  which they were observed.
- Phoenix upstream style is conservative and review-oriented: file headers, tabs in C, localized `clang-format off/on`, direct control flow, `static const` hardware tables, and warning-clean builds enforced by `-Werror` in `phoenix-rtos-build/Makefile.common`.
- Pi 4 uses BCM2711 with GIC-400, PL011, BCM2711 PCIe, VL805 xHCI over PCIe, GENET Ethernet, and Broadcom SDHCI.
- Pi 5 uses BCM2712 plus RP1, with most I/O behind a PCIe-connected southbridge-like peripheral controller.

## Immediate Next Implementation Milestones

1. Scope the smallest alternate-observability step for a Pi 4 lab without
   USB-TTL serial.
   Result: selected `plo` mailbox framebuffer as the next bounded path.
2. Keep the new Pi 4 HDMI visibility path stable and regression-testable.
   Result: the current helper is now `scripts/qemu-rpi4b-hdmi-smoke.sh`.
3. Keep the current QEMU shell smoke baseline stable:
  `help` plus the validated external-applet follow-up `echo -h`.
4. Use the current QEMU shell confidence to drive the next bounded steps toward
   a visible first real-device signal beyond UART-only diagnostics.
5. Use the Circle review to keep the next bounded move on the HDMI-visible path
   rather than prematurely widening into PCIe, xHCI, or USB keyboard work.
6. Reuse the existing `pc-tty` framebuffer text path surgically instead of
   porting the whole IA32 `pc-tty` app or introducing a new font subsystem.
7. Defer more SD-image refresh work until HDMI text output exists.
8. Keep the new prompt-reaching lane stable while avoiding new diagnosis-only
   probe accumulation.

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
