# Known issues and limitations

Open items in the Phoenix-RTOS Raspberry Pi 4 port, as of 2026-07-02. This is
the user-facing summary; the exhaustive engineering registries are:

- [docs/inprogress/pi4-hardware-support-matrix.md](inprogress/pi4-hardware-support-matrix.md)
  — per-peripheral status with evidence.
- [docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
  — the `TD-NN` transitional-debt registry (its bottom "Tracking Checklist" is
  authoritative; most `TD` items are already RESOLVED).

## Application bugs

These affect the showcase apps, not the base system.

| ID | Symptom | Status / workaround |
|---|---|---|
| #64 | SD-side filesystem stack pressure under load (deep fs call chains). | Open. |
| #66 | Stale `/tmp/.X0-lock` prevents the X server restarting after an unclean exit. | Open; remove `/tmp/.X0-lock` before relaunching the X server. |
| #67 | Quake torch/flame rendering artifacts. | Open; cosmetic — gameplay unaffected. |
| #68 | Quake multiplayer hangs at the `LOADING` screen. | Open; single-player and demos work. |
| #69 | `xbill` exits silently instead of running. | Open; other Athena/X apps (xterm, xcalc, xedit) render fine. |
| #70 | Dillo has no TLS/HTTPS support. | Open; plain-HTTP pages load; HTTPS is unavailable. |

## Reproducible showcase build (`--with-showcase`)

> **Status: integration in progress, not yet reviewed/merged.** The
> `build-showcase-apps.sh` orchestrator + `--with-showcase` wiring below were
> produced by an automated build pass that was interrupted before final review;
> the scripts are staged in the working tree. The SB-1/SB-2 findings are that
> pass's VM observations — treat them as leads pending verification, not settled.

`scripts/build-showcase-apps.sh` (invoked by `rebuild-rpi4b-fast.sh --variant sd
--with-showcase`) builds the whole showcase layer from source. Reported on a
clean Ubuntu 24.04 VM (2026-07-02): the GPU/GL + GLQuake spine builds
reproducibly and `rpi4-quake` bundles into the image; `nano`, `mc`, `dillo`,
`startx` build and land in the ext2 root. Two breakages remain, both cleanly
isolated (they do **not** break the base or GLQuake build — the orchestrator
records them and continues):

| ID | What fails | Root cause (precise) | Fix lead |
|---|---|---|---|
| SB-1 | **8 X11 apps + the Xphoenix server** fail to build: xterm, xedit, xcalc, xclock, xlogo, xbill, WindowMaker, Xphoenix. | Cascade from **`libICE-1.1.1` build failure**: `src/iceauth.c:93` (`arc4random_buf`) calls `getentropy()`, which Phoenix libc lacks (`implicit declaration of function 'getentropy'`). The existing `tools/x11-port/patches/libICE-1.1.1-phoenix.patch` fixes a different (K&R `time()`) issue, not this. Without libICE, `libSM` fails its `pkg-config` check (`Package 'ice' ... not found`), then `libXt`/`libXmu`/`libXaw` fail, so every Athena-toolkit app and the toolkit-linked X server fail. | One fix unblocks all 8: either add `getentropy` to libphoenix, or extend the libICE patch to avoid `getentropy` in `arc4random_buf` (e.g. fall back to `/dev/urandom` / `rand`). The Athena libs + apps built historically, so this is a fresh gap on the clean host. |
| SB-2 | **`rpi4-vkquake`** cannot link (`undefined reference to spirv_print_asm`), so `build-vkquake-phoenix.py --link` returns non-zero. | On a clean Ubuntu host the Mesa V3DV meson config compiles with **`-DHAVE_SPIRV_TOOLS`**, so `spirv_to_nir.c` in `libv3dv-phoenix.a` references `spirv_print_asm`, which the V3DV aux-source closure does not define. (The historical hand-built archive linked with 0 undefined — a config difference regressed it.) The orchestrator **removes the broken `libvkquake.a`** so `rpi4-vkquake` is skipped and the base/GLQuake build is unaffected. | Verify `grep HAVE_SPIRV_TOOLS /tmp/mesa-v3dv-build/compile_commands.json`; candidate fixes: add `-Dspirv-tools=disabled` to the v3dv `meson setup` in `build-showcase-apps.sh`, or add the source defining `spirv_print_asm` to `tools/v3d-driver-port/v3dv-aux-sources.txt`. vkQuake is WIP regardless (see GPU Vulkan below). |

## System-level limitations

| Area | Limitation | Status |
|---|---|---|
| SMP | 4-core scheduling is active (`NUM_CPUS=4` in `hal/aarch64/generic/config.h`): every secondary re-arms its own per-CPU CNTV timer and runs the scheduler on its tick; cpu0 additionally does the global sleep-queue housekeeping. Validated to psh with real LDAXR/STXR exclusives. | Working (TD-01/TD-11 resolved 2026-05-27). |
| Board portability (TD-06) | The DTB parser assumes a single interrupt controller; only the 4 GB Pi 4B is validated (1/2/8 GB models untested). | Known limitation. |
| SError masked (TD-10) | Asynchronous SError is masked in early kernel paths because a live PCIe/VL805 USB external-abort SError is not yet root-caused; unmasking regresses boot. A dump-and-halt handler is implemented and armed for when the abort is fixed. | Known, HW-gated. |
| `hal_memset` DC-ZVA (TD-20) | The `dc zva` fast path is disabled on the Cortex-A72 pending proof of the EL2 DC-ZVA trap state (does not reproduce in QEMU). Performance-only; correctness-safe. | Known limitation. |
| Ethernet link IRQ (TD-Eth-LinkIRQ) | The PHY's `INT_B` line is not routed to a GIC SPI on the Pi 4 board, so link state is MDIO-polled at 1 Hz (as Linux/U-Boot also do). | By design for this board. |
| SD throughput | Reads use UHS-I **DDR50** (1.8V) + **SDMA** + multi-block CMD18 (~38 MB/s, ~86% of Linux on the same card); writes are multi-block CMD25 but **PIO** (a BCM2711 controller quirk corrupts DMA writes, so they stay PIO). Remaining levers: DMA writes and larger ADMA transfers. | Fast, correct reads; writes correct but PIO-bound. |
| GPU Vulkan (V3DV) | Full Vulkan init and shader execution work on hardware; vkQuake is paused at the no-WSI texture-upload gap (buffer→image copy). | Partial. |
| Audio | PWM audio over the 3.5 mm jack works with a streaming DMA ring and a Quakespasm mixer backend; an audible end-to-end sign-off on real headphones is still pending. | Partial. |

## Not started

WiFi (BCM43455 — **blocked** on a firmware-execution gate that needs deeper
hardware visibility), Bluetooth, USB mass storage, I²C / SPI / PWM general
drivers, and camera (CSI-2) / DSI display are not implemented.
