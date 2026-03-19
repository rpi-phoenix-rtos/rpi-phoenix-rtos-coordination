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

Later for network-boot lab work:

- `dnsmasq`
- `tftpd-hpa`
- optional `nfs-kernel-server`

### Required VM verification

Before implementation begins, verify:

- the host workspace is mounted into the VM
- the VM can read and write the shared workspace
- a simple package install succeeds
- a simple QEMU command runs

## 4. Phoenix Repository Preparation

Before implementation starts, the operator or agent must:

1. create `sources/` if it does not exist
2. clone these Phoenix repositories into `sources/`:
   - `phoenix-rtos-kernel`
   - `plo`
   - `phoenix-rtos-devices`
   - `phoenix-rtos-filesystems`
   - `phoenix-rtos-build`
   - `phoenix-rtos-project`
   - `phoenix-rtos-tests`
3. record the clean baseline SHAs in a manifest under `manifests/`

If forks are used, also configure:

- `origin` as the writable fork
- `upstream` as the official Phoenix repository

## 5. What Must Be Provided For Real-Device Testing

The following physical items are currently required to run tests on an actual Raspberry Pi board.

### Mandatory hardware

- Raspberry Pi 4 Model B board
- stable power supply for the board
- USB-UART adapter that uses 3.3 V TTL serial
- boot media:
  - microSD card initially
  - optional USB storage later
- Ethernet connection for the board

### Strongly recommended hardware

- controllable power relay or smart USB power switch
- dedicated USB Ethernet adapter for the Pi boot network
- spare boot media for recovery
- a second DUT later if Pi 5 work needs to proceed without disturbing Pi 4 stabilization

### Optional but useful hardware

- SD mux or programmable SD switch
- HDMI capture for display-oriented debugging later
- breakout wiring labeled per DUT to reduce operator mistakes

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

1. connect the UART adapter and confirm the host can see the serial device
2. connect power in a way that can be safely cycled
3. prepare boot media or confirm network-boot infrastructure is not yet in use
4. attach Ethernet if the test expects it
5. record the DUT identity:
   - board model
   - board revision if known
   - serial adapter path
   - MAC address if known
6. confirm a recovery path exists:
   - known-good SD image
   - spare boot media
   - or a known-good network boot tree

## 8. Additional Manual Steps For Network Boot

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

## 9. Required Manual Steps During Ongoing Implementation

After each successful implementation step, the operator or agent must ensure:

1. the touched upstream repositories are committed
2. the coordination repo is updated
3. the integration manifest is updated with tested SHAs
4. this file is updated if any new operator action, physical prerequisite, one-time setup task, or recovery step was discovered

## 10. Update Policy For This File

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
