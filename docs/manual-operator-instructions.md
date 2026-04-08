# Manual Operator Instructions

This file collects the manual steps, physical prerequisites, and operator-provided inputs that are currently known to be required for this project.

Use it as the human-facing runbook for:

- preparing the workstation
- preparing the Linux VM
- preparing the Raspberry Pi hardware lab
- enabling real-device tests
- recording any future manual prerequisites discovered during implementation

If a future session discovers a new manual step, this file must be updated in the same session.

## 1. What Must Exist Before Implementation Starts

The following are currently required.

### Workstation and access

- the macOS Apple Silicon host machine
- administrator access on the host for installing virtualization and networking helpers
- internet access for cloning repositories and downloading packages
- enough free disk space for:
  - Phoenix source trees
  - one or more Linux VMs
  - toolchains
  - QEMU images
  - UART logs and artifacts

### Git and repository access

- local git configured on the host
- access to clone the upstream Phoenix RTOS repositories
- if changes will be pushed upstream or to forks:
  - GitHub authentication configured
  - writable fork or branch permissions available

### Additional requirements for unattended overnight runs

If the user wants the agent to continue unattended for a long period:

- leave the host machine powered on
- leave the Linux VM available
- avoid host actions that would interrupt the Codex session, the VM, or mounted workspace paths
- understand that the unattended run must still stop when a manual step, hardware step, or ambiguous architectural choice is reached
- if recurring automation is used to resume work, confirm that the workspace path remains valid and writable for the duration of that run

### Documentation baseline

Before implementation begins, the operator or agent should confirm the current docs exist and are being used:

- `AGENTS.md`
- `docs/status.md`
- `docs/git-repository-strategy.md`
- `docs/host-macos-apple-silicon.md`
- `docs/testing-automation.md`
- this file

## 2. Host Machine Manual Setup

These are the known manual setup tasks on the macOS host.

### Required host software to install

Install the following host-side tools before implementation begins:

- Lima
- `socket_vmnet`
- `yq`
- `socat`
- `picocom` or `tio`
- `mtools`

Optional later:

- Docker Desktop or another container runtime, only if a specific container workflow justifies it
- Tart, only if VM snapshot or distribution workflows become useful
- `dnsmasq` if DHCP or TFTP serving moves to the host

### Required host confirmations

Confirm:

- Xcode Command Line Tools are functional
- Homebrew is functional
- QEMU runs at least basic commands
- a serial console tool is available and tested

### Host layout

Create or preserve the expected workspace layout:

```text
phoenix-rpi/
  docs/
  manifests/
  skills/
  sources/
```

The upstream Phoenix repositories should live under `sources/`.

## 3. Linux VM Manual Setup

The current recommended primary development environment is a Linux arm64 VM managed by Lima.

### Required VM creation steps

The operator must:

1. install Lima
2. create the `phoenix-dev` VM
3. use Ubuntu 24.04 arm64 unless the project docs are updated to recommend otherwise
4. configure:
   - `vmType: vz`
   - `mountType: virtiofs`
   - `vzNAT` networking for the development VM
5. size the VM with enough resources for builds and toolchains

Recommended starting values on the current host:

- 8 CPUs
- 16 GiB RAM
- 120 GiB disk or more

### Required VM package installation

Install the Linux packages Phoenix already expects, plus project utilities.

Phoenix-documented baseline:

- `build-essential`
- `mtd-utils`
- `autoconf`
- `pkg-config`
- `texinfo`
- `genext2fs`
- `libtool`
- `libhidapi-dev`
- `python3`
- `python3-jinja2`
- `python3-yaml`

Additional project utilities:

- `git`
- `python3-venv`
- `qemu-system-arm`
- `qemu-system-aarch64`
- `qemu-utils`
- `device-tree-compiler`
- `mtools`
- `dosfstools`
- `expect`
- `socat`
- `jq`
- `yq`
- `rsync`

Current practical rule for Pi 4 emulation in `phoenix-dev`:

- keep the Ubuntu-packaged `qemu-system-aarch64` installed as a fallback
- do not assume the packaged Ubuntu 24.04 QEMU is sufficient for Pi 4 board emulation
- use the VM-local source-built QEMU at `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64` for `raspi4b` runs until this document is updated

Additional packages required for Phoenix cross-toolchain builds:

- `bison`
- `flex`
- `libgmp-dev`
- `libmpfr-dev`
- `libmpc-dev`
- `libisl-dev`
- `zlib1g-dev`

Later for network-boot lab work:

- `dnsmasq`
- `tftpd-hpa`
- optional `nfs-kernel-server`

### Required VM verification

Before implementation begins, verify:

- the host workspace is mounted into the VM
- the VM can read the shared workspace
- re-verify whether the shared workspace is writable; on the current Lima setup it should be treated as effectively read-only for build artifacts
- a simple package install succeeds
- a simple QEMU command runs
- for Pi 4 emulation work specifically:
  - verify `/usr/bin/qemu-system-aarch64 --version`
  - verify `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64 --version`
  - verify `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64 -machine help | grep raspi4b`

## 4. Phoenix Repository Preparation

Before implementation starts, the operator or agent must:

1. create `sources/` if it does not exist
2. clone the current `phoenix-rtos-project/.gitmodules` repo set into `sources/` as sibling repositories:
   - `libphoenix`
   - `phoenix-rtos-build`
   - `phoenix-rtos-corelibs`
   - `phoenix-rtos-devices`
   - `phoenix-rtos-doc`
   - `phoenix-rtos-filesystems`
   - `phoenix-rtos-hostutils`
   - `phoenix-rtos-kernel`
   - `phoenix-rtos-lwip`
   - `phoenix-rtos-ports`
   - `phoenix-rtos-posixsrv`
   - `plo`
   - `phoenix-rtos-project`
   - `phoenix-rtos-tests`
   - `phoenix-rtos-usb`
   - `phoenix-rtos-utils`
3. do not treat the smaller initial planning subset as sufficient for local builds; `phoenix-rtos-project` expects the broader repo tree above
4. record the clean baseline SHAs in a manifest under `manifests/`

### Required local buildroot preparation

Before running the main Phoenix project build locally, the operator or agent must:

1. prepare the disposable buildroot with the correct mode:
   - linked mode for the normal sibling-clone workflow:
     `scripts/prepare-buildroot.sh --link-components`
   - copied mode when the build must write into component source trees:
     `scripts/prepare-buildroot.sh --copy-components`
2. use the default generated buildroot or an explicitly chosen replacement path
   - on the current Lima setup, if the shared workspace is read-only inside the VM:
     - linked mode should use `~/phoenix-buildroots/phoenix-rtos-project`
     - copied mode should use `~/phoenix-buildroots/phoenix-rtos-project-copy`
3. re-run the script after:
   - changing files in `sources/phoenix-rtos-project`
   - changing the sibling repo inventory
   - changing any upstream source repo before re-validating in copied mode
4. treat the generated buildroot as disposable:
   - do not use nested submodule clones as the primary editable workspace
   - do not rely on the upstream `sources/phoenix-rtos-project` working copy as the main artifact directory

Current practical rule:

- use linked mode for the already verified `host-generic-pc` baseline build
- use copied mode for the Phoenix toolchain build and for current AArch64 validation work until upstream stops generating files inside component source trees

### Required AArch64 toolchain preparation

Before validating AArch64-target changes locally, the operator or agent must:

1. ensure the extra toolchain-build packages above are installed in `phoenix-dev`
2. prepare the copied disposable buildroot with:
   - `scripts/prepare-buildroot.sh --copy-components`
3. run the upstream Phoenix toolchain builder from that buildroot:
   - `./phoenix-rtos-build/toolchain/build-toolchain.sh aarch64-phoenix "$HOME/phoenix-toolchains"`
4. keep the toolchain itself on VM-local storage rather than the shared workspace
5. verify that `aarch64-phoenix-gcc` resolves before claiming an AArch64 validation lane exists
6. re-run the copied-buildroot preparation before later AArch64 validations if any upstream source repo changed

Current validated VM paths:

- copied buildroot: `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy`
- toolchain root: `/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix`
- toolchain sysroot: `/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/aarch64-phoenix`

If forks are used, also configure:

- `origin` as the writable fork
- `upstream` as the official Phoenix repository

### Current optional Pi 4 DTB input for staged boot trees

For the current `aarch64a72-generic-rpi4b` boot-tree staging flow, a Pi 4 DTB
is optional during ordinary no-hardware builds but will be needed for realistic
firmware boot-media preparation.

Current accepted inputs are:

- environment variable `RPI4B_DTB_PATH` pointing to a board DTB file
- project-local file:
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/bcm2711-rpi-4-b.dtb`

Current staged output path:

- `_boot/aarch64a72-generic-rpi4b/rpi4b/bcm2711-rpi-4-b.dtb`

Current practical rule:

- ordinary no-hardware validation may omit the DTB
- before the first realistic Pi 4 firmware-boot test, the operator or agent must provide a real `bcm2711-rpi-4-b.dtb` through one of the two inputs above unless the boot flow is later changed and this document is updated
- when `phoenix-dev` is restarted, do not assume `/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb`
  still exists; regenerate it with:
  - [scripts/prepare-rpi4b-dtb.sh](/Users/witoldbolt/phoenix-rpi/scripts/prepare-rpi4b-dtb.sh)

Current QEMU-only helper for the emulated Pi 4 lane:

- environment variable `RPI4B_QEMU_MEMORY_SIZE=80000000`
- use it only with direct `qemu-system-aarch64 -M raspi4b` validation, not as a
  real-device firmware setting
- purpose:
  patch `/memory@0/reg` in the staged DTB copies because direct QEMU does not
  perform Raspberry Pi firmware DTB memory customization

### Required Raspberry Pi firmware-file input for real Pi 4 boot attempts

The current staged Pi 4 boot tree is still not self-contained unless Raspberry Pi firmware files are provided separately.

Current accepted firmware inputs are:

- environment variable `RPI4B_FIRMWARE_DIR` pointing to a directory with Raspberry Pi firmware files
- project-local directory:
  `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/firmware`
- files fetched from the Raspberry Pi firmware repository boot tree:
  <https://github.com/raspberrypi/firmware/tree/master/boot>

Before the first realistic Pi 4 firmware boot attempt, the operator must provide a known-good Raspberry Pi 4 firmware-file set containing at least the files required by the current bootloader release, typically including files such as:

- `start4.elf`
- `fixup4.dat`

Current staged output path:

- `_boot/aarch64a53-generic-rpi4b/rpi4b/`

Current practical rule:

- do not assume the staged Phoenix Pi 4 tree is bootable on bare media by itself
- pair the staged Phoenix files with a known-good Raspberry Pi firmware-file set from the same validation baseline
- for this project, future agents are explicitly allowed to download the needed Pi 4 firmware files from `raspberrypi/firmware` `boot/` when that is the most direct way to produce a testable boot tree
- re-verify the exact required firmware filenames against the current Raspberry Pi bootloader release before depending on them
- optional debug firmware files such as `start4db.elf`, `fixup4db.dat`, `start4cd.elf`, and `fixup4cd.dat` may also be staged when present

### Current staged Pi 4 firmware boot-tree contents

The current early Pi 4 firmware-facing boot tree now stages:

- `config.txt`
- `kernel8.img`
- `loader.disk`
- optional `bcm2711-rpi-4-b.dtb`

Current payload rule:

- `config.txt` uses `initramfs loader.disk 0x48000000`
- this matches generic `plo` `RAM_ADDR`
- when preparing real Pi 4 boot media from the staged tree, keep `loader.disk` beside `kernel8.img`; it is no longer only a QEMU-side artifact
- the current staged tree still also needs Raspberry Pi firmware files; `config.txt`, `kernel8.img`, `loader.disk`, and the DTB are not sufficient by themselves
- the current no-hardware assembly helper is:
  - [scripts/assemble-rpi4b-bootfs.sh](/Users/witoldbolt/phoenix-rpi/scripts/assemble-rpi4b-bootfs.sh)
- by default it assembles a firmware-visible Pi 4 boot tree inside the VM at:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs`
- the current FAT-image helper is:
  - [scripts/assemble-rpi4b-bootfs-img.sh](/Users/witoldbolt/phoenix-rpi/scripts/assemble-rpi4b-bootfs-img.sh)
- by default it assembles a portable FAT image inside the VM at:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img`
- the current host-side export helper is:
  - [scripts/export-rpi4b-fat-image.sh](/Users/witoldbolt/phoenix-rpi/scripts/export-rpi4b-fat-image.sh)
- by default it exports that FAT image into the host workspace at:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-bootfs.img`
- current validated exported-image SHA-256:
  - `fab57080ef7c770ac9346cfd9e86b6ef71c31d47559fe0bd955bee6b71d3a108`
- the current full-disk-image helper is:
  - [scripts/assemble-rpi4b-sdimg.sh](/Users/witoldbolt/phoenix-rpi/scripts/assemble-rpi4b-sdimg.sh)
- by default it assembles a VM-local Pi 4 SD-card image at:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-sd.img`
- current validated layout:
  - DOS partition table
  - one bootable FAT32 partition
  - partition start sector `2048`
  - embedded FAT offset `1048576`
- the current host-side full-disk-image export helper is:
  - [scripts/export-rpi4b-sdimg.sh](/Users/witoldbolt/phoenix-rpi/scripts/export-rpi4b-sdimg.sh)
- current practical note:
  - the helper now uses `limactl copy --backend=rsync`
  - this is required because the older host export path produced a corrupted
    host-visible copy even though the VM-local SD image itself was valid
- by default it exports that disk image into the host workspace at:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated exported full-image SHA-256:
  - `acea299fb225edb0293b4d022b9b19d984fe51627a168bd69c403442590b757d`
- the current exported full-disk artifact includes the latest firmware-stage
  early handoff state:
  - Pi 4 A72 `plo` restored to the last coherent high-DDR placement used by
    the current Phoenix `plo` memory map
  - `config.txt` again uses:
    - `kernel_address=0x40080000`
    - `boot_load_flags=0x1`
  - `config.txt` now also uses:
    - `armstub=phoenix-armstub8-rpi4.bin`
  - that custom Pi 4 armstub now also performs the current bounded Circle-style
    EL3 preparation:
    - local timer control and prescaler setup
    - `CNTFRQ_EL0 = 54000000`
    - early GIC group-1 distributor / CPU-interface enablement
  - that custom Pi 4 armstub now also performs the current earliest-entry
    board-visible proof:
    - drives GPIO42 high on the primary core
    - should make the ACT LED turn on if the custom armstub executes
  - the next persistent post-armstub proof is now in `plo` `_startc()`:
    - it drives GPIO42 low
    - if the ACT LED ends the boot attempt off, the board reached `_startc()`
      and the remaining failure is later than the armstub boundary
  - Pi 4 `plo` now also uses the ARM-visible GICv2 aliases:
    - `0xff841000`
    - `0xff842000`
  - `disable_splash=1` kept so the firmware rainbow does not hide the handoff
- the current exported full-disk artifact also includes the latest HDMI staging
  state:
  - `hdmi_force_hotplug=1`
  - `disable_overscan=1`
- the current validated Pi 4 HDMI text-console signature in QEMU is:
  - black background
  - white text glyphs rendered from early runtime output
  - interactive `psh` prompt later in the boot path
- the previously validated three-stage top-left panel is now an earlier loader
  progress aid, not the final runtime HDMI signature

### Current first macOS flashing workflow

Use this only with the current host-visible full-disk artifact:

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`

Recommended manual sequence on macOS:

1. refresh the exported artifact if needed:
   - [scripts/export-rpi4b-sdimg.sh](/Users/witoldbolt/phoenix-rpi/scripts/export-rpi4b-sdimg.sh)
2. verify the exported artifact before flashing:
   - [scripts/verify-rpi4b-sdimg.sh](/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh)
   - current expected SHA-256:
     `acea299fb225edb0293b4d022b9b19d984fe51627a168bd69c403442590b757d`
3. if you want the exact commands printed for a chosen disk identifier:
   - [scripts/print-rpi4b-macos-flash-commands.sh](/Users/witoldbolt/phoenix-rpi/scripts/print-rpi4b-macos-flash-commands.sh) `diskN`
4. if you want a prefilled first-trial report file before you start:
   - [scripts/create-rpi4b-first-trial-report.sh](/Users/witoldbolt/phoenix-rpi/scripts/create-rpi4b-first-trial-report.sh)
5. identify the target SD card:
   - `diskutil list`
6. unmount the whole target disk:
   - `diskutil unmountDisk /dev/diskN`
7. write the image to the raw device:
   - `sudo dd if=/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img of=/dev/rdiskN bs=4m`
8. flush and eject the card:
   - `sync`
   - `diskutil eject /dev/diskN`

Critical cautions:

- replace `diskN` with the actual SD-card device node from `diskutil list`
- use the whole-disk node, not a partition node like `diskNs1`
- writing this image overwrites the current contents of the target card
- prefer `/dev/rdiskN` over `/dev/diskN` for faster raw writes on macOS
- re-verify the target disk before running `dd`
- the new helper scripts are intentionally non-destructive:
  - the verification helper checks path, size, SHA-256, the FAT boot-sector
    signature, and a directory listing at the embedded partition offset
  - the flash helper only prints commands and does not write to the disk

## 5. What Must Be Provided For Real-Device Testing

The following physical items are currently required to run tests on an actual Raspberry Pi board.

### Mandatory hardware

- Raspberry Pi 4 Model B board
- stable power supply for the board
- boot media:
  - microSD card initially
  - optional USB storage later
- a way to write the prepared image to the microSD card from the host

### Strongly recommended hardware

- USB-UART adapter that uses 3.3 V TTL serial
- Ethernet connection for the board
- HDMI display
- USB keyboard

### Optional but useful hardware

- USB mouse
- controllable power relay or smart USB power switch
- dedicated USB Ethernet adapter for the Pi boot network
- spare boot media for recovery
- a second DUT later if Pi 5 work needs to proceed without disturbing Pi 4 stabilization
- SD mux or programmable SD switch
- HDMI capture for display-oriented debugging later
- breakout wiring labeled per DUT to reduce operator mistakes

### Current practical note when no USB-UART adapter is available

- the project can still progress to the first Pi 4 SD-card boot attempt without
  UART
- however, the current Phoenix bring-up path is still primarily validated
  through PL011 serial plus QEMU, not yet through a fully hardware-validated
  HDMI or network-visible runtime
- the Pi 4 QEMU lane now reaches a real HDMI text console, not just the earlier
  `plo` marker panel
- the current Pi 4 image now also enables two firmware-stage HDMI settings in
  `config.txt` for the first board trial:
  - `hdmi_force_hotplug=1`
  - `disable_overscan=1`
- until framebuffer or network-level observability is implemented and tested
  further on hardware, lack of UART should still be treated as a major
  reduction in failure visibility
- in that no-UART lab shape, prioritize:
  - correct image assembly
  - correct SD-card writing
  - visible firmware-side signs of life on HDMI if any
  - later alternate observability work before broad hardware debugging

### Current first-boot expectations for the no-UART lab

- the first manual SD-card boot attempt should currently be treated as a media
  deployment and gross boot-behavior check, not as a strong milestone
  validation step
- at the current project state, the first real-device image now includes the
  intended HDMI text-console plus USB-host path, so a keyboard-driven runtime
  shell is a reasonable target for the first manual Pi 4 trial even though it
  is not yet hardware-validated
- the first positive HDMI sign now worth looking for is broader than before:
  - `plo` may still briefly show its earlier top-left progress panel
  - the stronger target is a black text-console background with white glyphs
    from runtime output
  - a visible `(psh)%` prompt would be the strongest expected no-UART sign
    if the full QEMU behavior carries over to real hardware
- the current image tries to make that more likely and more legible by:
  - forcing HDMI output mode even if hotplug detection is flaky
  - disabling firmware overscan so the upper-left marker is less likely to be
    cropped
- lack of visible HDMI output does not yet cleanly distinguish:
  - firmware boot failure
  - `plo` failure
  - kernel boot with only UART-visible output
  - later runtime failure before a visible display path exists
- until the project adds an alternate observability path, silent behavior on a
  no-UART board should be treated as low-information

## 6. Known Manual Wiring Requirements

For early UART-driven bring-up, the operator must connect the Raspberry Pi UART correctly.

Known practical requirements:

- connect ground between host UART adapter and the Pi
- connect the adapter RX to the Pi TX
- connect the adapter TX to the Pi RX
- use 3.3 V TTL levels only
- do not connect 5 V TTL serial

For the standard Raspberry Pi 40-pin header early-console path, the expected UART pins are typically:

- Pin 8: `GPIO14` / `TXD0`
- Pin 10: `GPIO15` / `RXD0`
- Pin 6: `GND`

Re-verify if a future board configuration or overlay changes the active UART routing.

## 7. Required Manual Steps Before The First Real-Device Test

Before the first hardware boot attempt, the operator must:

1. write the currently selected Pi 4 image artifact to microSD or otherwise
   prepare the boot medium
2. if a UART adapter is available:
   - connect it and confirm the host can see the serial device
3. connect power in a way that can be safely cycled
4. attach HDMI if the current test is expected to use display-visible behavior
5. attach Ethernet if the test expects it
5. record the DUT identity:
   - board model
   - board revision if known
   - serial adapter path if present
   - MAC address if known
6. confirm a recovery path exists:
   - known-good SD image
   - spare boot media
   - or a known-good network boot tree

## 8. First Manual Pi 4 Trial In The Current No-UART Lab

For the current lab shape, the first practical manual trial is:

1. export the current full disk image:
   - [scripts/export-rpi4b-sdimg.sh](/Users/witoldbolt/phoenix-rpi/scripts/export-rpi4b-sdimg.sh)
   - current exported artifact:
     [artifacts/rpi4b/rpi4b-sd.img](/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img)
   - current SHA-256:
     `acea299fb225edb0293b4d022b9b19d984fe51627a168bd69c403442590b757d`
   - focused trial checklist:
     [pi4-first-hardware-trial.md](/Users/witoldbolt/phoenix-rpi/docs/pi4-first-hardware-trial.md)
2. flash the image to microSD using the workflow above
3. insert the card into the Pi 4
4. attach:
   - HDMI
   - USB keyboard
   - Ethernet if desired
5. power on the board
6. record the gross result:
   - any visible HDMI behavior
   - ACT LED behavior if observed
   - whether the board appears to reboot repeatedly or stay powered

Current specific HDMI sign to record if present:

- whether the ACT LED ends the boot attempt on or off after the initial
  firmware blink
- early top-left progress panel if it appears
- later black background with white text glyphs
- any readable Phoenix boot lines or `(psh)%` prompt
- whether typing on the USB keyboard changes the visible shell state
- whether the picture is stable, flashes briefly, or disappears during later
  boot progress

Do not over-interpret the result:

- the current software path is still primarily validated through QEMU plus UART
  on emulated lanes
- display output is now a meaningful real-hardware sign
- keyboard interaction is now part of the staged Pi 4 image path, but it still
  needs first real-hardware confirmation

## 9. Additional Manual Steps For Network Boot

These are not required on day one, but they are known likely operator tasks for the later fast-iteration lab setup.

The operator will need to:

1. provide a dedicated Ethernet segment or VLAN for Pi boot traffic
2. provide a host Ethernet interface or USB Ethernet dongle dedicated to that segment
3. configure the later `phoenix-lab` Linux VM or another controller to serve:
   - DHCP
   - TFTP
   - optional NFS or HTTP
4. configure the Pi 4 bootloader or EEPROM boot order for network boot as required by the current Raspberry Pi firmware guidance
5. keep a fallback SD or USB recovery path available

When this setup is first enabled, record:

- the exact EEPROM or bootloader state
- the exact boot-order configuration
- the exact DHCP or TFTP configuration path
- the recovery procedure if network boot fails

## 10. Required Manual Steps During Ongoing Implementation

After each successful implementation step, the operator or agent must ensure:

1. the touched upstream repositories are committed
2. the coordination repo is updated
3. the integration manifest is updated with tested SHAs
4. this file is updated if any new operator action, physical prerequisite, one-time setup task, or recovery step was discovered

## 11. Update Policy For This File

This file must be updated whenever the project learns any new manual requirement, including:

- a new required host package
- a new VM setup step
- a one-time bootloader or EEPROM programming action
- a cable or wiring requirement
- a relay or power-control requirement
- a dedicated network requirement
- a recovery procedure
- any test step that cannot be performed autonomously by the agent

Do not leave such knowledge only in chat history or in unrelated technical documents.
