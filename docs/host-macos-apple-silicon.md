# Host Strategy: macOS on Apple Silicon

This document defines the recommended development, build, and test strategy for this specific workstation:

- host OS: macOS
- host architecture: Apple Silicon arm64
- confirmed hardware: MacBook Pro `Mac14,5`, Apple `M2 Max`, 32 GB RAM
- confirmed date of audit: 2026-03-19

## 1. Current Host Audit

Commands run locally confirmed:

- macOS version: `26.3.1 (a)`
- kernel: Darwin `25.3.0`
- architecture: `arm64`
- hardware model: Apple `M2 Max`

Currently available host tools:

- `git`
- Homebrew
- Xcode / Command Line Tools
- `qemu-system-aarch64`
- `qemu-system-x86_64`
- `qemu-img`
- `dtc`
- `uv`
- `python3`
- `make`
- `clang`
- `expect`
- `jq`
- `screen`
- `limactl`
- `yq`
- `socat`
- `picocom`
- `tio`
- `mtools`
- `socket_vmnet`

Currently not present on the host:

- `docker`
- `colima`
- `tart`
- `podman`
- `nerdctl`
- `minicom`
- `dosfstools`-style FAT helpers

Implication:

- the host now has the documented prerequisite tools for the Linux VM workflow
- the `phoenix-dev` VM has also been created and bootstrapped with the documented package baseline
- inside `phoenix-dev`, the Ubuntu-packaged `qemu-system-aarch64` is only a fallback for generic `virt` work because Ubuntu 24.04 currently ships `8.2.2`, which does not expose `raspi4b`
- the current Pi 4 emulation lane in `phoenix-dev` is the VM-local source-built QEMU at `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64`
- the remaining gates are local Phoenix build-tree wiring and the first clean baseline build
- in the current Lima setup, the shared workspace is readable from the guest but should be treated as read-only for build artifacts; use VM-local disposable buildroots when building inside Linux

## 2. Recommendation Summary

Use a Linux arm64 VM as the primary Phoenix development environment.

Recommended stack:

1. macOS host for:
   - agent runs
   - editing and documentation
   - git coordination
   - USB serial access
   - relay or power-control access
   - invoking Linux VM builds
2. Lima VM for:
   - Phoenix builds
   - toolchain management
   - QEMU-based validation
   - image assembly
   - optional Docker-in-Linux reproducible builds
   - later DHCP/TFTP/NFS or HTTP services for Pi 4 network boot

Do not make native macOS the primary Phoenix build path.

## 3. Option Evaluation

## 3.1 Native macOS builds

Pros:

- highest local filesystem performance
- simplest direct access to USB serial and other host devices
- no VM layer

Cons:

- Phoenix officially documents Linux build flows and Linux package prerequisites
- package names, shell behavior, filesystem tools, and image utilities are Linux-shaped
- every macOS-specific workaround would increase drift from upstream assumptions

Decision:

- acceptable only for lightweight helper tasks
- not recommended as the primary implementation environment

## 3.2 Docker Desktop on macOS

Pros:

- official, polished container UX
- good for containerized builds
- Apple Silicon support is current

Cons:

- still runs Linux workloads inside a hidden VM on macOS
- best suited for containers, not a full general-purpose Linux dev workstation
- not ideal as the main place for UART-driven hardware automation or low-level networking experiments
- adds licensing considerations in some organizations

Decision:

- optional, not primary
- only worth adding later if a specific Phoenix container workflow materially helps

## 3.3 Colima

Pros:

- lightweight container-oriented workflow
- simple CLI
- built on Lima

Cons:

- mostly valuable when the main problem is "I want Docker-compatible containers on macOS"
- this project needs a conventional Linux environment, not just a container runtime
- adds an extra abstraction layer on top of Lima without solving the core hardware-lab split

Decision:

- not recommended as the primary environment
- consider only if Docker-compatible local workflows become important and direct Lima is insufficient

## 3.4 Lima

Pros:

- purpose-built for Linux VMs on macOS
- official docs describe Apple Virtualization Framework (`vz`) as the default on modern macOS
- automatic file sharing and port forwarding
- supports VM networking modes including `vzNAT` and `socket_vmnet`
- can provide bridged networking for later Pi network-boot work
- open-source and straightforward to script

Cons:

- requires installation and some initial setup
- bridged networking with `socket_vmnet` needs privileged setup
- USB device passthrough is not the main strength, so host-side USB handling remains important

Decision:

- primary recommendation

## 3.5 Tart

Pros:

- good for reproducible Apple Virtualization Framework VMs
- supports directory sharing
- useful for snapshotting or distributing known-good VM images

Cons:

- more image-centric than necessary for the first phase
- does not remove the need for host-side USB and lab-control strategy
- less natural than Lima for a standard Linux development shell on a single workstation

Decision:

- optional later enhancement
- not the first tool to install

## 3.6 Lume

Pros:

- Apple Virtualization Framework-based
- useful for automation-heavy VM scenarios

Cons:

- does not provide a clearer benefit than Lima for this conventional Linux build-and-test use case
- the project needs a stable Linux workstation abstraction more than an AI-VM product layer

Decision:

- not recommended as the primary environment for this project

## 4. Recommended Architecture

## 4.1 Host and VM split

### macOS host responsibilities

- coordination repository
- documentation updates
- local git operations
- USB-UART console access
- USB relay or smart-power control
- optional manual recovery tasks
- invoking Linux VM commands

### Linux VM responsibilities

- Phoenix source builds
- toolchain build and storage
- QEMU runs that are part of the normal validation lane
- filesystem/image creation
- network-boot server components for Pi 4 later

This split is deliberate:

- Linux handles the Linux-shaped build stack
- macOS handles the hardware attachments that are awkward to hand into Apple-Virtualization VMs

## 4.2 Recommended VM profiles

Create two Lima profiles over time.

### `phoenix-dev`

Purpose:

- everyday build, test, and QEMU work

Recommended characteristics:

- Ubuntu 24.04 arm64
- `vmType: vz`
- `mountType: virtiofs`
- `vzNAT` networking
- enough CPU, RAM, and disk for cross-toolchains and repeated builds

Suggested sizing on this host:

- CPUs: 8
- memory: 16 GiB
- disk: 120 GiB or more

### `phoenix-lab`

Purpose:

- later real-hardware network-boot lab services

Recommended characteristics:

- Ubuntu 24.04 arm64
- `vmType: vz`
- `mountType: virtiofs`
- bridged `socket_vmnet` networking on a dedicated Ethernet interface
- DHCP/TFTP and optionally NFS or HTTP services

This profile should be introduced only when Pi 4 network boot becomes valuable enough to justify the extra setup.

## 5. Filesystem Strategy

Recommended source location:

- keep the coordination repo and upstream Phoenix repos on the macOS filesystem under this workspace
- mount the workspace into the VM using Lima's shared-directory support

Why:

- the agent runs directly against the host workspace
- documentation and git coordination remain simple
- the VM can build against the same checked-out sources

Performance caveat:

- shared filesystems are usually slower than a native Linux ext4 filesystem for large build trees

Recommended mitigation:

- keep the sources on the shared mount
- keep heavyweight toolchain caches and possibly temporary build caches on VM-local storage when practical
- if Phoenix build outputs become too slow on the shared mount, move only the heavy cache or output directories into VM-local storage and sync or copy the final artifacts back

## 6. Build Strategy

Use two build lanes inside Linux.

### Lane A: native Linux build in the VM

Use for:

- normal daily development
- debugging build scripts
- fast repeated inner-loop builds

Reason:

- fewer layers than running everything in containers
- easier to debug
- aligns with Phoenix's documented Linux package model

### Lane B: containerized build inside the VM

Use for:

- clean reproducibility checks
- verifying that the environment can be recreated from a container recipe
- CI-like sanity runs

Reason:

- Phoenix already documents Docker-based build workflows
- running containers inside Linux is simpler than making macOS itself pretend to be the Linux host

Do not start by stacking Docker Desktop on macOS unless there is a specific need.

## 7. QEMU Strategy On This Host

Although `qemu-system-aarch64` is already installed on macOS, the recommended default is:

- run authoritative QEMU validation in the Linux VM
- use the Ubuntu-packaged VM QEMU as fallback only
- use the VM-local source-built QEMU for Pi 4 board emulation

Why:

- Phoenix build and run scripts are Linux-shaped
- keeping build and emulation in the same Linux environment reduces drift
- host QEMU can still be used for ad hoc experiments when helpful

Current `phoenix-dev` QEMU split:

- packaged fallback:
  - `/usr/bin/qemu-system-aarch64`
  - version: `8.2.2`
  - useful for:
    - generic `virt` validation
    - historical baseline comparisons
- Pi 4 emulation lane:
  - `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64`
  - version: `10.2.2`
  - useful for:
    - `raspi4b` smoke runs
    - Pi 4 image-shape and early-board validation

Re-verify:

- the latest stable QEMU release before rebuilding or refreshing this lane
- the exact implemented and missing `raspi4b` devices on the QEMU version in use

## 8. Real Hardware Strategy On This Host

## 8.1 USB serial and relays

Prefer keeping these on macOS:

- USB-UART adapters
- USB power relays
- other directly attached lab-control devices

Reason:

- this avoids relying on VM USB passthrough for critical recovery paths

The host can run:

- manual console tools such as `screen`, `picocom`, or `tio`
- scripted serial control via Python and `pyserial`
- relay-control scripts

## 8.2 Pi 4 network boot

Best later-stage design on this machine:

1. the Linux `phoenix-lab` VM serves DHCP and TFTP on a dedicated bridged interface
2. the macOS host triggers builds inside the VM
3. the macOS host captures UART and handles power-cycle control

Recommended physical setup:

- one dedicated USB Ethernet adapter for the Raspberry Pi boot network
- the adapter bound to the bridged Lima profile via `socket_vmnet`
- one USB-UART adapter for the console
- one controllable power relay or smart USB power switch

Fallback:

- if bridged VM networking proves unreliable on the exact host setup, keep the build in the VM but move DHCP/TFTP serving to the macOS host or to a separate small Linux controller

## 9. What To Install Before Implementation Starts

## 9.1 Host packages

Recommended first installs:

- Lima
- `socket_vmnet`
- `yq`
- `socat`
- `picocom` or `tio`
- `mtools`

Optional later installs:

- Tart
- Docker Desktop or Docker CLI only if a specific container workflow requires it
- `dnsmasq` if host-served network boot becomes the fallback path

## 9.2 Linux VM packages

At minimum, install the Linux packages Phoenix already documents for toolchain work, plus a few extra utilities for this project.

Phoenix-documented package baseline includes:

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

Additional recommended VM packages:

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
- `dnsmasq`
- `tftpd-hpa`
- optional `nfs-kernel-server`
- optional Docker Engine if Lane B is enabled

## 10. Recommended First Setup Sequence

1. install the missing host packages needed for the VM and serial workflow
2. create the `phoenix-dev` Lima VM
3. mount this workspace into the VM
4. verify a basic Linux shell, package install, and file-sharing loop
5. clone the Phoenix upstream repos into `sources/`
6. record the baseline integration manifest
7. verify one Phoenix build in Linux before starting Raspberry Pi-specific changes
8. only later add the `phoenix-lab` profile and bridged networking for Pi 4 network boot

## 11. Rules For Future Sessions

- Do not spend implementation effort trying to make native macOS the canonical build environment.
- Do not add Docker Desktop first out of habit; justify it with an actual project need.
- Keep the hardware recovery path host-controlled even if builds move into a VM.
- Record the exact host and VM versions used in manifests once implementation begins.
- Re-verify Lima networking behavior and Raspberry Pi bootloader behavior when enabling network boot for the first time.
- Run the authoritative build and warning-quality checks in Linux, not only on the macOS host.

## 12. Reference Documents

Important upstream references for this strategy:

- Phoenix Linux build documentation:
  <https://docs.phoenix-rtos.com/latest/building/linux.html>
- Phoenix build script documentation:
  <https://docs.phoenix-rtos.com/latest/building/script.html>
- Lima documentation:
  <https://lima-vm.io/docs/>
- Lima networking documentation:
  <https://lima-vm.io/docs/config/network/>
- Lima VMNet networking:
  <https://lima-vm.io/docs/config/network/vmnet/>
- Docker Desktop for Mac:
  <https://docs.docker.com/desktop/setup/install/mac-install/>
- Tart quick start:
  <https://tart.run/quick-start/>
- Lume overview:
  <https://cua.ai/docs/lume>
