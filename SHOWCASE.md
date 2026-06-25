# Phoenix-RTOS on the Raspberry Pi 4 — a port built entirely by AI agents

> **Draft showcase (2026-06-25).** A public-facing narrative of what was built and how. The code
> in this project — kernel bring-up, drivers, the GPU/Vulkan stack, the game ports — was authored
> **end-to-end by AI agents**; the human collaborator directed priorities and ran the hardware,
> but did not write the code. This document is a starting point for the outward-facing story;
> refine freely.

## What it is

A from-scratch port of [Phoenix-RTOS](https://phoenix-rtos.com/) (a small, microkernel,
message-passing real-time OS) to the **Raspberry Pi 4 / BCM2711 (Cortex-A72, AArch64)** — taken from
"does not boot" to a system that boots to a shell over the network or SD card, drives the real
hardware, and runs **GLQuake on the V3D GPU**, an **X11 desktop**, and a Unix-like userland — with a
**Vulkan (vkQuake) path rendering its first frame on the GPU**.

## Highlights

- **Kernel & boot:** AArch64 bring-up on BCM2711 — MMU + caches enabled, exception/SError handling,
  a self-hosted hardware-watchpoint debug facility, the plo→kernel handoff, SMP-aware (4 CPUs
  enumerated, cpu0-scheduled), and a clean board layer (PL011 UART, generic timer, GICv2, DTB parse).
- **Networking:** BCM GENET gigabit Ethernet, IRQ-driven, ~0.9 ms ping RTT, lwIP stack; an NFS client
  + server with `/` served over NFS (`takeover` design) and ~8.5 MB/s throughput.
- **Storage & rootfs:** EMMC2 SD-card driver with an **ext2 root**, plus a full **NFS root**.
- **USB:** the VL805 xHCI host controller brought up over the BCM2711 PCIe bridge, with HID
  keyboard + mouse working through to the shell and to applications.
- **Display & GPU:** HDMI framebuffer + console; the **V3D 4.2 GPU powered on and driven by a ported
  Mesa `v3d` Gallium driver + GL frontend** → **GLQuake**: textured, depth-tested 3D, demos and a
  live single-player level at ~40 fps @ 1080p, rendered on the GPU and scanned out to HDMI.
- **Vulkan:** the Mesa **V3DV** Vulkan ICD ported to Phoenix → a hand-authored SPIR-V triangle
  (Tier-4b), and **vkQuake** taken from instant-crash to a **first frame rendered on the V3D through
  Vulkan and displayed on HDMI** (see "vkQuake bring-up" below).
- **Graphics desktop:** a kdrive **X server (Xphoenix)** with an fbdev DDX + the full X client/toolkit
  library stack → `xeyes`/`twm` running interactively with a working mouse.
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
clear visible on screen. (Sustaining the frame loop + drawing the 2D HUD is the current work.)

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

This is an in-progress research port, not a product. GLQuake is the proven graphics capstone;
vkQuake renders on the GPU but its frame loop / HUD are still being finished. SMP runs but schedules
on cpu0. Some subsystems (WiFi, audio audibility, SD write) have documented, hardware-gated
remaining work. The `docs/` tree is the full engineering record — including the dead ends, which are
part of the story of how the agents worked.

## How the agents worked

Work proceeded as a long series of tightly-scoped steps: form a hypothesis, build, boot on real
hardware, read the UART/registers/HDMI, root-cause, fix, validate, document, commit — with a stronger
"advisor" model reviewing direction, parallel sub-agents fanning out on independent pieces, and
rollback discipline (per-step integration manifests) so a bad change is always recoverable. The
`docs/inprogress/` and `docs/done/` directories preserve the reasoning trail.
</content>
