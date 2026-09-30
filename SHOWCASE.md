# Phoenix-RTOS on the Raspberry Pi 4 — a port built entirely by AI agents

> **Draft showcase (2026-06-25, graphics updated 2026-09-30).** A public-facing narrative of what was built and how. The code
> in this project — kernel bring-up, drivers, the GPU/Vulkan stack, the game ports — was authored
> **end-to-end by AI agents**; the human collaborator directed priorities and ran the hardware,
> but did not write the code. This document is a starting point for the outward-facing story;
> refine freely.

## What it is

A from-scratch port of [Phoenix-RTOS](https://phoenix-rtos.com/) (a small, microkernel,
message-passing real-time OS) to the **Raspberry Pi 4 / BCM2711 (Cortex-A72, AArch64)** — taken from
"does not boot" to a system that boots to a shell over the network or SD card, drives the real
hardware, and runs a Linux-style graphics stack on the V3D GPU:

- the **XFCE 4.20 desktop on Wayland**;
- an **X11 desktop**;
- five **3D games**, full screen or in desktop windows, including **vkQuake on Vulkan** and
  SuperTuxKart 1.4;
- a video player and a PDF reader;
- a Unix-like userland.

To use it, see the [User Guide](docs/USER-GUIDE.md).

## Highlights

- **Kernel & boot:** AArch64 bring-up on BCM2711 — MMU + caches enabled, exception/SError handling,
  a self-hosted hardware-watchpoint debug facility, the plo→kernel handoff, SMP-aware (4 CPUs
  enumerated, cpu0-scheduled), and a clean board layer (PL011 UART, generic timer, GICv2, DTB parse).
- **Networking:** BCM GENET gigabit Ethernet, IRQ-driven, ~0.9 ms ping RTT, lwIP stack; an NFS client
  with `/` served over NFS (`takeover` design), ~30 MB/s read; **WiFi** (BCM43455, WPA2 + DHCP) in the
  image, managed with the `wifi` command.
- **Storage & rootfs:** EMMC2 SD-card driver with an **ext2 root**, plus a full **NFS root**.
- **USB:** the VL805 xHCI host controller brought up over the BCM2711 PCIe bridge, with HID
  keyboard + mouse working through to the shell and to applications.
- **Display & GPU:** the graphics stack has three userspace servers:
  - **`rpi4-v3d-async`**, a render server that owns the V3D 4.2 GPU and runs every client's jobs
    asynchronously, with fences;
  - **`rpi4-kms`**, a KMS display server on the firmware's display planes (atomic flips, vblank
    events, scaled modes);
  - **`shmsrv`**, shared memory for the Wayland and X clients.

  They share buffers through a new kernel export primitive. On top run a Phoenix **libdrm** and
  **Mesa 26.2** (GBM, EGL, OpenGL ES 3.1, OpenGL, Vulkan/V3DV).
- **Games:** QuakeSpasm (~44 fps at 1080p), Quake II (60 fps), Quake III (~59 fps), vkQuake on
  Vulkan (≈ 39–42 fps) and SuperTuxKart 1.4 (Raspberry Pi OS parity; ~22 fps at 720p scaled to the
  screen). SDL 2 runs each game full screen on KMS or in a window on the desktop (QuakeSpasm
  ~56 fps, Quake III ~70 in a 1280×720 window).
- **Graphics desktops:** **XFCE 4.20 on the labwc Wayland compositor** (GTK 3: panel, Thunar, foot,
  the Atril PDF viewer, a video player), and **Xorg** with modesetting + glamor, Window Maker and a
  60 fps GL window through DRI3/Present.
- **Userland:** BusyBox + applets, Lua, MicroPython, OpenSSL, cURL (mbedTLS), Dropbear SSH, lighttpd,
  and more — cross-compiled and run from the NFS root.
- **Robustness engineering:** e.g. a systemic **VideoCore-mailbox serialization** fix — the single
  hardware mailbox FIFO was being raced by five+ boot-time processes (thermal, Ethernet MAC read,
  USB/PCIe bring-up, SD, GPU power-on), each destroying the others' replies; a single serialized
  `vcmbox` server resolved a whole class of non-deterministic boot failures.

## vkQuake bring-up — nine root-caused blockers in one day

A representative example of the debugging depth. vkQuake on the V3DV Vulkan driver went from
crashing immediately to rendering a frame, by root-causing and fixing, in sequence:
a NULL-client server-sound call; a dropped `VID_Init` renderer-init block; 13 Vulkan-runtime
entrypoints silently dropped by a missing `--whole-archive`; the no-WSI render-resource/command-buffer
path; a **blake3 NEON stub that overran callers' stack by 2×** for any shader >4 KB (the subtle one —
it masqueraded as an allocator and then a NULL-dispatch bug before the real cause was found); a
zero-dimension render-pass job dereferencing a NULL tile-state BO; and finally display ownership
(fbcon-disable) so the GPU's scanout reaches the HDMI. Result: `vkQueueSubmit` → the render-pass
clear visible on screen. (vkQuake has since been rebuilt on the current Vulkan stack, with
`VK_KHR_display` presentation, and plays at ≈ 39–42 fps.)

## Build & run

The project is a **coordination repo** (docs, scripts, build orchestration) plus sibling
upstream Phoenix-RTOS repos under `sources/`. The core loop:

```
./scripts/rebuild-rpi4b-fast.sh --scope core --variant netboot   # build the image
./scripts/test-cycle-netboot.sh --capture-secs 200 --label demo  # power-cycle, netboot, capture UART
```

Variants: `netboot` (TFTP image + RAM/NFS root), `sd` (ext2 root on card), `nfsroot` (NFS-owned `/`).
Validation is automated end to end: UART capture + summaries, periodic HDMI snapshots, scripted-psh
interaction, multi-boot benches, and a scriptable power plug — so "needs hardware" rarely means
"needs a human."

## Honest status

This is a research port, not a product. The graphics stack, the desktops and the games run on the
hardware. 4-core SMP scheduling works. Some subsystems have documented remaining work
([KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md)): Bluetooth has no host stack, audio has no audible
sign-off, and only the 4 GB board is validated. The `docs/` tree is the full engineering record — including the dead ends, which are
part of the story of how the agents worked.

## How the agents worked

Work proceeded as a long series of tightly-scoped steps: form a hypothesis, build, boot on real
hardware, read the UART/registers/HDMI, root-cause, fix, validate, document, commit — with a stronger
"advisor" model reviewing direction, parallel sub-agents fanning out on independent pieces, and
rollback discipline (per-step integration manifests) so a bad change is always recoverable. The
`docs/inprogress/` and `docs/done/` directories preserve the reasoning trail.
</content>
