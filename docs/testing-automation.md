# Testing and Automation

## 1. Goals

The port should be developed in a way that supports long, semi-autonomous or autonomous AI-driven sessions. The build/test loop must therefore optimize for:

- low-latency feedback
- deterministic image creation
- deterministic UART capture
- automated board power control
- reproducible artifacts
- clear separation between emulator confidence and hardware confidence

For this workstation specifically, also optimize for:

- a clear split between macOS host responsibilities and Linux VM responsibilities
- minimal dependence on VM USB passthrough

## 2. Existing Phoenix Test Infrastructure

Phoenix already provides useful infrastructure:

- `phoenix-rtos-tests/runner.py`
- `phoenix-rtos-tests/trunner/dut.py`
- `phoenix-rtos-tests/trunner/target/*`
- `phoenix-rtos-tests/trunner/harness/plo.py`
- `phoenix-rtos-tests/trunner/host.py`

Key capabilities already present:

- `pexpect`-driven DUT control
- serial DUT abstraction
- QEMU DUT abstraction
- harness composition
- host abstraction including Raspberry Pi GPIO-backed control
- XML/CSV style report generation support in the runner

Implication:

- do not invent a brand-new test framework first
- extend the Phoenix test runner for Raspberry Pi targets

## 3. Recommended Test Pyramid

## Tier 1: static and host-only checks

Fastest checks, run on every meaningful change:

- markdown/doc lint if added later
- image assembly checks
- build-script/unit tests
- DTB parser unit tests
- target config validation
- warning-clean build verification for touched targets

## Tier 2: generic AArch64 emulator checks

Run on every low-level bring-up change:

- boot `plo`
- boot kernel
- basic shell smoke
- timer/interrupt sanity

Recommended target:

- generic AArch64 `virt` machine in QEMU

## Tier 3: Raspberry Pi-specific emulator checks

Use for narrow validation only:

- verify image shape and handoff assumptions
- check that Pi-specific code does not instantly fail under `raspi4b`

Warning:

- do not treat this as authoritative for peripherals

## Tier 4: real hardware checks

Required for all meaningful milestones:

- cold boot
- warm reboot
- shell smoke
- storage
- GPIO/I2C/SPI
- Ethernet
- USB
- soak tests

## 4. QEMU Strategy

## 4.1 Why generic `virt` should exist

Phoenix's current AArch64 emulation lane is tied to a ZynqMP target and Xilinx-flavored QEMU. A generic `virt` target would:

- validate common AArch64 work faster
- reduce platform noise during refactors
- shorten CI time
- make DTB parser and timer work easier to debug

## 4.2 Why `raspi4b` is still useful

Re-verify this section against the exact QEMU version in use.

Current `phoenix-dev` baseline on `2026-03-20`:

- packaged `/usr/bin/qemu-system-aarch64`:
  - version: `8.2.2`
  - `raspi4b`: absent
- VM-local `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64`:
  - version: `10.2.2`
  - `raspi4b`: present

Current `raspi4b` use:

- use the VM-local `10.2.2` binary for Pi 4-specific smoke runs
- keep generic `virt` as the authoritative fast lane for common AArch64 work

What `raspi4b` currently helps validate:

- image loading assumptions
- early UART path
- board-specific low-level control flow
- whether Pi 4-oriented images fail immediately under a board-shaped machine

Current local `raspi4b` smoke result:

- `raspi4b` requires at least `-smp 4`
- direct raw-image boot timed out with no serial output
- `plo.elf` as `-kernel` now reaches visible loader output
- with an explicit Pi 4 DTB passed through `RPI4B_DTB_PATH`, the current lane reaches:
  - `pl011-tty: started`
- current local QEMU `10.2.2` `raspi4b` does not support `dumpdtb`, so this lane currently needs an explicit external DTB input

Inference:

- the environment blocker is gone
- the Pi 4 path is now well past raw image placement and early multi-core startup
- the next blocker is inside the shared post-`pl011-tty: started` console-readiness path rather than basic board bring-up

Official QEMU `raspi4b` expectations to preserve:

- the official board docs list implemented PL011/AUX serial, GPIO, SD/MMC, mailbox, USB host, and VideoCore property firmware support
- the same docs list these `raspi4b` gaps:
  - `PWM`
  - `PCIE Root Port`
  - `GENET Ethernet Controller`

## 4.3 What QEMU should never be the sole authority for

- xHCI correctness
- GENET correctness
- GPIO/I2C/SPI interrupt reliability
- final storage timing behavior

## 4.4 Host versus VM execution on macOS Apple Silicon

On this machine, the recommended default is:

- build and authoritative QEMU runs in a Linux arm64 VM
- USB serial and power control on the macOS host
- use VM-local source-built QEMU for Pi 4 board emulation rather than relying on the Ubuntu package version
- when using non-interactive `limactl shell ... bash -lc` build commands, export `PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"` explicitly before Phoenix AArch64 builds

Reason:

- Phoenix build assumptions are Linux-shaped
- macOS host access to directly attached lab devices is simpler than passing them into Apple-Virtualization VMs

## 4.4.1 Current generic AArch64 loader entry matrix

The current generic AArch64 QEMU lane is now useful for validating loader entry and kernel handoff behavior across three exception-level entry modes using the same built image:

- `virt,secure=on,gic-version=2`
  - current known-good EL3 baseline
- `virt,secure=off,gic-version=2`
  - current EL1-entry lane
- `virt,secure=off,virtualization=on,gic-version=2`
  - current EL2-entry lane

Use this matrix when changing generic `plo` entry code before depending on Raspberry Pi hardware. It is faster and more controlled than jumping directly to the Pi 4 board for exception-level regressions.

## 4.5 Loader script timing caveat

Avoid using the PLO `wait` command as a passive timing delay in unattended lanes.

Current local source and runtime evidence:

- `plo/cmds/wait.c` implements `wait` by polling `lib_consoleGetc()`
- on the current generic AArch64 QEMU lane, `wait 500` produced:
  - `Waiting for input,   400 [ms]`
  - `Can't get data from console.`
  - `Please reset plo and set console to device.`

Implication:

- `wait` is an interactive loader console command, not a generic boot-script sleep primitive
- do not use it as a startup race workaround in unattended QEMU or automated hardware flows unless console input is intentionally configured and validated

## 5. Real Hardware Lab Design

## 5.1 Controller host

Recommended:

- a dedicated Linux machine
- x86_64 or arm64 is fine
- stable USB subsystem
- enough ports for UART adapters and relay control

For this specific workstation, treat the controller as a split system:

- macOS host for directly attached lab devices
- Linux VM for build and lab-network services

## 5.2 Required lab hardware

- Raspberry Pi 4 DUT
- USB-UART adapter for console
- reliable power relay or controllable USB-PD power switch
- optional HDMI capture only if display testing becomes necessary
- removable boot media or SD mux

Later for Pi 5:

- separate DUT
- optional RP1 debug UART access

## 5.3 Highly recommended automation hardware

- SD mux or programmable SD switch
- smart PDU or USB-controlled relay
- dedicated serial adapters with stable `/dev/serial/by-id` names

## 5.4 Minimum viable semi-automatic setup

If full media switching is unavailable:

1. host or Linux VM builds image
2. host writes image to removable SD/USB media
3. host power-cycles DUT
4. host captures UART
5. test runner drives shell and checks output

This is still good enough for early bring-up.

## 5.5 Preferred deployment transports for Pi 4

Verified official Raspberry Pi documentation makes Pi 4 network boot a real option, not a speculative one:

- Raspberry Pi documents Pi 4 network boot enablement through `raspi-config`
- the documented boot order after programming is `0xf21`
- Raspberry Pi documents DHCP plus TFTP-based boot stages for network boot
- Raspberry Pi documents `boot_ramdisk=1` with `boot.img`, and explicitly notes that `boot.img` can be useful for Network Boot and `RPIBOOT`

Recommended transport order:

1. earliest bring-up:
   SD or USB media with a relay and optional SD mux
2. primary Pi 4 automation path once the lab is stable:
   EEPROM-configured network boot with a controller-host DHCP/TFTP service
3. advanced path for the fastest Phoenix iteration:
   network boot into `plo`, then fetch remaining Phoenix artifacts over a network protocol such as PHFS or a simple loader-side fetch path

Implications:

- do not assume SD card rewriting should remain the steady-state workflow
- do keep SD or USB bootable media available as a recovery path
- network boot should become the default high-frequency hardware loop for Pi 4 if lab networking is reliable

## 5.6 Recommended Pi 4 network-boot lab design

Controller host should provide:

- DHCP
- TFTP
- build artifact staging
- UART capture
- power control

Recommended setup:

- one dedicated Ethernet segment or VLAN for DUT booting
- per-board boot directories keyed by MAC address or serial number
- a known-good fallback boot tree
- a way to switch a board back to local-media boot for recovery

On this workstation, the preferred implementation is:

- Linux VM serves DHCP and TFTP over a bridged `socket_vmnet` interface
- macOS host captures UART and drives relays

Recommended staged use:

1. use local media while the first `plo` boot path is fragile
2. once `plo` reliably starts from firmware, switch the board to network boot for rapid iteration
3. later reduce moving parts further by letting `plo` load the kernel or userspace artifacts over the network when that is simpler than repackaging media images

If bridged VM networking is unstable on the exact host release or hardware adapter combination:

1. keep building in the VM
2. move DHCP/TFTP serving to the macOS host or to a separate controller
3. keep UART and power control on the host

## 6. Image Build and Deployment Workflow

Recommended automation pipeline:

1. build Phoenix target
2. assemble boot assets
3. choose deployment mode:
   - local media image
   - network-boot directory tree
   - network-boot `boot.img`
4. place runtime/rootfs image or network-served artifacts
5. deploy to DUT media or TFTP root
6. reboot DUT
7. capture boot log
8. run smoke suite
9. archive logs and artifacts

Artifact classes to retain:

- built loader image
- built kernel image
- DTB used
- rootfs image
- served boot tree or `boot.img`
- full UART log
- parsed test report

## 7. Suggested Raspberry Pi Target Additions to Phoenix Tests

New future test targets should include:

- `aarch64-generic-virt-qemu`
- `aarch64a72-rpi4b`
- later `aarch64a76-rpi5b` or equivalent naming aligned with Phoenix conventions

Each target should specify:

- shell prompt
- boot method
- serial defaults
- flashing/update method
- reboot method
- expected boot timeout

## 8. Smoke Test Definition

Every boot test should at minimum verify:

1. loader prompt or loader handoff banner
2. kernel banner
3. shell prompt
4. `ps`
5. `ls /`
6. `date`
7. `reboot`

As drivers come online, extend smoke tests:

- storage mount/read/write
- GPIO set/read/interrupt
- I2C probe
- SPI loopback
- `ifconfig` and `ping`
- USB enumeration

## 9. Soak and Stress Tests

Nightly or scheduled jobs should include:

- 100-iteration boot loop
- 100-iteration reboot loop
- long shell idle stability
- network transfer soak
- storage write/read verification
- USB hotplug repetitions

Later:

- SMP stress
- interrupt flood
- watchdog handoff and reset recovery

## 10. Failure Classification

Future agents should classify failures into:

- build failure
- image assembly failure
- firmware load failure
- `plo` boot failure
- kernel boot failure
- shell startup failure
- runtime regression
- hardware-lab failure

This classification should appear in logs and issue summaries. It prevents wasting time debugging relay or media issues as kernel bugs.

## 11. Recommended Logging Discipline

Always keep:

- raw UART transcript
- concise boot summary
- exact image hash or build identifier
- exact firmware/config used
- exact DUT board revision if known

For transient or unstable components, log:

- EEPROM version
- firmware files used
- QEMU version
- relevant `config.txt` settings

For code-quality-sensitive steps, also keep:

- build logs showing warning-clean status for touched targets
- the exact commit SHAs validated

## 12. AI-Agent Workflow Recommendations

For autonomous sessions:

1. run fast host-only checks first
2. run generic emulator checks second
3. run a very small hardware smoke
4. only then run heavier tests

This preserves iteration speed and reduces pointless hardware churn.

## 13. Future Enhancements

Good later additions:

- automatic UART log parsing into structured failure summaries
- boot timing trend tracking
- image deployment manifest files
- flaky-test quarantine with explicit documentation
- board farm management if more than one DUT is used
