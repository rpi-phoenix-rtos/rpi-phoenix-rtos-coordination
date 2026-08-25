# Known issues and limitations

Open items in the Phoenix-RTOS Raspberry Pi 4 port, as of 2026-08-21. This is
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
| #67 | Quake alias-model **geometry** collapse: some models (weapon pickups, viewmodel, monsters, torch flames) rendered as a gray angular fan/wedge. | **RESOLVED** (quakespasm `3d742a3`, *"r_alias/gl_mesh: fix #67 alias-model collapse (single-pose VBO > 4KB page)"*). True root cause: a single-pose alias VBO whose data crossed a 4 KB page boundary was mis-fetched by the V3D — a **deterministic, data-dependent** attribute-fetch bug, not the intermittent cache race first suspected. Fix: drop the duplicate pose block and set `vboposes = numposes` so the buffer layout no longer straddles the page. HW-verified across boots (grenade launcher, nailgun, torches, ammo/health boxes all render correctly). **Important lesson (documented in the port journey): earlier 2026-07-25/26/27 "RESOLVED" claims were false positives** produced by a cross-boot *determinism* harness that a consistently-broken render also passes — a proxy metric that didn't test correctness. A separate, rare binner-wedge is tracked in the engineering docs. Detail: `docs/inprogress/2026-07-27-67-REAL-fix-single-pose.md`. |
| #68 | Quake multiplayer hangs at the `LOADING` screen. | Open; single-player and demos work. |
| #69 | `xbill` exits silently instead of running. | Open; other Athena/X apps (xterm, xcalc, xedit) render fine. |
| #70 | Dillo HTTPS/TLS. | **RESOLVED (2026-08-21):** Dillo 3.2.0 (TLS via **mbedTLS**) **browses the live HTTPS internet** — CA-verified TLSv1.2 as well as plain HTTP. Live internet access is via a **host-side NAT gateway** (`scripts/pi-internet-nat.sh`): the Pi reaches the internet through the dev host (a lab/netboot setup, not the Pi routing on its own), with `ntpclient` setting the cert clock. Re-confirmed 2026-08-21 (ping + HTTP round-trip to 1.1.1.1). |
| vkQuake-hang | **vkQuake hangs after the main menu** and does not respond to keyboard/mouse. | Open; the Vulkan/V3DV path renders the menu and textured 3D on the GPU, but a post-menu hang + un-wired SDL input leave it non-interactive on HW. |
| q3-lightmap | **Quake III: black lightmap sectors on some larger maps** (e.g. `q3dm7`). | **RESOLVED.** Root cause: a Phoenix `should_tile` optimization in the ported Mesa `v3d` driver forced large (≥1024²) render-target textures to a linear layout for fast readback — but Mesa also marks every renderable RGBA8 *sampled* texture as a render target, so the 1024² merged lightmap atlas was stored linear instead of UIF-tiled and the TMU sampled it as garbage/black. Fix excludes `PIPE_BIND_SAMPLER_VIEW` from that gate (a sampled texture must stay tiled). **Verified by a host-vs-Pi visual-parity harness** (deterministic demo → coherent per-frame capture streamed off the Pi → SSIM comparison against a host software-GL reference): `q3dm7` on the Pi matches the host at **SSIM 0.989** with no black sectors, and Quake II matches at **0.993**. The earlier "distorted"/striped HDMI screenshots were scanout/capture tearing of the moving scene, not a render defect. |

## Reproducible showcase build (`--with-showcase`)

> **Status: verified.** The full `--with-showcase` build — the GPU/GL + GLQuake
> spine, `rpi4-vkquake`, the X11 stack (Xphoenix + xterm/xedit/xcalc/xclock/WindowMaker),
> and `nano`/`mc`/`dillo` — builds reproducibly from a clean environment and ships
> in the SD image. Both breakages recorded below (SB-1, SB-2) are fixed and verified.

`scripts/build-showcase-apps.sh` (invoked by `rebuild-rpi4b-fast.sh --variant sd
--with-showcase`) builds the whole showcase layer from source. Reported on a
clean Ubuntu 24.04 VM (2026-07-02): the GPU/GL + GLQuake spine builds
reproducibly and `rpi4-quake` bundles into the image; `nano`, `mc`, `dillo`,
`startx` build and land in the ext2 root. Two breakages remain, both cleanly
isolated (they do **not** break the base or GLQuake build — the orchestrator
records them and continues):

| ID | What fails | Root cause (precise) | Fix lead |
|---|---|---|---|
| SB-1 | **8 X11 apps + the Xphoenix server** failed to build: xterm, xedit, xcalc, xclock, xlogo, xbill, WindowMaker, Xphoenix. | Cascade from **`libICE-1.1.1` build failure**: `src/iceauth.c` (`arc4random_buf`) calls `getentropy()` including only `<unistd.h>` → `implicit declaration of function 'getentropy'` (→ `-Werror`). Root cause was NOT a missing function — `getentropy` is implemented (`libphoenix/stdlib/getrandom.c`) and its symbol is in the toolchain `libphoenix.a`; it was only declared in `<sys/random.h>`, not `<unistd.h>` where glibc/BSD (and libICE) expect it. | **FIXED** (libphoenix `79ee015`): added the `getentropy` prototype to `<unistd.h>`. Verified: the full X11 stack (libICE + the 8 apps + Xphoenix) now builds in the clean `--with-showcase` build and ships in the image. |
| SB-2 | **`rpi4-vkquake`** failed to link in the clean `--no-cache` Docker build, so it was silently skipped and never shipped in the image. | Two config-difference regressions surfaced only in the from-scratch build (dev archives linked because their working trees carried the fixes uncommitted): (1) `-DHAVE_SPIRV_TOOLS` on a clean host made `spirv_to_nir.c` reference `spirv_print_asm`; (2) after that, `dri_util.c` referenced `driQueryOptionstr`, unresolved by the V3DV aux closure. | **FIXED.** (1) `-Dspirv-tools=disabled` added to the v3dv `meson setup` in `build-showcase-apps.sh`; (2) weak `driQueryOptionstr` stub added to `tools/v3d-driver-port/v3dv_gap_stubs.c` (coord `672c199`). Verified: the clean `--no-cache` build now links `rpi4-vkquake` and installs `/usr/bin/rpi4-vkquake` (~12.8 MB) into the SD image. vkQuake also renders correctly on HW (water/torches fixed, coord `2354fd6` + vkquake `0d8dc54`). |

## System-level limitations

| Area | Limitation | Status |
|---|---|---|
| SMP | 4-core scheduling is active (`NUM_CPUS=4` in `hal/aarch64/generic/config.h`): every secondary re-arms its own per-CPU CNTV timer and runs the scheduler on its tick; cpu0 additionally does the global sleep-queue housekeeping. Validated to psh with real LDAXR/STXR exclusives. | Working (TD-01/TD-11 resolved 2026-05-27). |
| Board portability (TD-06) | The DTB parser assumes a single interrupt controller; only the 4 GB Pi 4B is validated (1/2/8 GB models untested). | Known limitation. |
| SError masked (TD-10) | Asynchronous SError is masked in early kernel paths because a live PCIe/VL805 USB external-abort SError is not yet root-caused; unmasking regresses boot. A dump-and-halt handler is implemented and armed for when the abort is fixed. | Known, HW-gated. |
| `hal_memset` DC-ZVA (TD-20) | The `dc zva` fast path is disabled on the Cortex-A72 pending proof of the EL2 DC-ZVA trap state (does not reproduce in QEMU). Performance-only; correctness-safe. | Known limitation. |
| Stack-exhaustion signal DoS | A userspace process that **exhausts its 1 MiB main-thread stack** takes down the kernel: fault-signal delivery (`hal_cpuPushSignal`, aarch64) writes the signal frame to the now-unmapped user stack and double-faults at EL1, with no recovery — so any program overflowing its stack crashes the box (register-confirmed via a stack-bomb repro, `tools/stack-bomb/`). Fix = a guarded signal-frame push (validate the target is in a writable mapped region before writing; terminate the process cleanly on failure), minding the demand-paged-stack case. **Attended** (highest-blast-radius kernel path — invisible to a green-boot smoke). | Known; turnkey work-order in [docs/inprogress/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md](inprogress/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md). |
| Ethernet link IRQ (TD-Eth-LinkIRQ) | The PHY's `INT_B` line is not routed to a GIC SPI on the Pi 4 board, so link state is MDIO-polled at 1 Hz (as Linux/U-Boot also do). | By design for this board. |
| SD throughput | Reads use UHS-I **DDR50** (1.8V) + **SDMA** + multi-block CMD18 (~38 MB/s, ~86% of Linux on the same card); writes are multi-block CMD25 but **PIO** (~17 MB/s DDR50 / ~13 MB/s HS50 — a BCM2711 controller quirk corrupts DMA writes, so they stay PIO). Multi-block writes verified 0-corruption over 3 runs × {8,32,128,256} blocks. The 1.8V/DDR50 switch is reliable on netboot and best-effort on SD-boot (falls back to HS50). Remaining (attended) levers: DMA writes and reliable DDR50 on SD-boot. | Fast, correct reads; writes correct but PIO-bound. |
| GPU Vulkan (V3DV) | Full Vulkan (V3DV) is available on Phoenix with GPU acceleration on real BCM2711 V3D 4.2 hardware: init, texture upload (no-WSI buffer→image copy resolved), SPIR-V vertex/fragment/compute shaders, and render passes all execute on the GPU. vkQuake runs GPU-accelerated (fb0 scanout, no WSI), but **hangs after the main menu and does not respond to keyboard/mouse** (see the `vkQuake-hang` row above). | Driver works (GPU-accelerated); vkQuake non-interactive. |
| Audio | PWM audio over the 3.5 mm jack works with a streaming DMA ring and a Quakespasm mixer backend; an audible end-to-end sign-off on real headphones is still pending. | Partial. |
| Interactive `bash` | ✅ **Fixed (2026-08-25).** GNU bash 5.2 now runs fully interactively over the console (prompt, command execution, stays until `exit`) — HW-verified. The earlier "self-exits at the prompt" was a **libphoenix `select()` bug**: a NULL (infinite) timeout was clamped to a 0 ms poll and returned 0 immediately instead of blocking, so GNU readline's blocking `select()` in `rl_getc()` treated it as a timeout and aborted. Fixed in `libphoenix sys/select.c` (a general fix for any blocking `select(…, NULL)`). | Fixed (libphoenix select NULL-timeout). |
| Netboot NFS-root reliability | With `/` mounted over **NFS on a 100 Mbps link**, large binaries and asset-heavy games can intermittently fail to launch or load data files. Two distinct modes: (1) a **boot-order race** — `psh` can start before the NFS-root takeover completes, so an early command hits the pre-takeover RAM root and reports `not found` (worked around in the test harness by waiting for the takeover; the clean fix is a `plo` boot-order gate); (2) **transient runtime read failures** on individual files (e.g. a game aborting on a missing pak asset) attributable to the NFS lease-reclaim window / stale server state, aggravated by the slow link. Booting from the **SD/eMMC ext2 root** (local storage) avoids both, and a **gigabit link** greatly reduces mode (2). | Known; use SD-boot or a gigabit link for asset-heavy workloads. |

## Partial / not started

- **WiFi (BCM43455) — control-plane only; do NOT rely on wireless networking
  yet.** The **control-plane works**: the driver associates to a real WPA2-PSK
  access point and completes the 4-way key handshake. The **data-plane does not
  carry traffic yet** — this is under active debugging, not abandoned (see
  `docs/inprogress/wifi-bcm43455-impl.md`). Until it lands, **use wired Ethernet**
  (fully working). The proprietary Cypress firmware blobs are **not vendored** in
  this repository (copyright/EULA hygiene); `scripts/stage-bcm43455-firmware.sh`
  stages them locally into a gitignored `.firmware/`.
- **Bluetooth (BCM43455) — driver-level bring-up only.** `/dev/hci0` comes up, the
  firmware patchram loads (323/323), a real BD_ADDR is read, and an HCI Inquiry
  completes. There is **no host Bluetooth stack** — no pairing, profiles, or audio
  — so it is not usable for real Bluetooth work yet.
- USB mass storage, I²C / SPI / PWM general-purpose drivers, and camera (CSI-2) /
  DSI display are not implemented.
