# Pi 4 (BCM2711) GPU bring-up: forward-research brief

Forward-looking research note for the orchestrator. Phoenix-RTOS today
runs a caches-off mailbox-allocated linear framebuffer on the Pi 4. This
document scopes what "real" GPU work would entail. Every non-obvious
claim is cited inline; primary sources are listed at the end.

## 1. Hardware overview (VC6 in BCM2711)

The BCM2711 SoC integrates the **VideoCore VI** subsystem. The display
pipeline is a chain of three hardware blocks plus encoders:

- **HVS (Hardware Video Scaler)** at MMIO base `0x7e400000` (legacy) /
  `0xfe400000` (Low Peripheral). The HVS does translation, scaling,
  colourspace conversion, and compositing of source planes into a FIFO
  for the next stage. [vc4 docs]
- **Pixel Valves (CRTCs)** consume scaled pixels from an HVS FIFO at
  the encoder's pixel clock and produce timed video. In DRM terms the
  Pixel Valve maps to a CRTC. [vc4 docs]
- **Encoders**: Pi 4 has **two HDMI controllers (HDMI0, HDMI1)** plus
  legacy DPI/DSI/VEC/TXP paths. The HDMI block was rewritten between
  Pi 0–3 and Pi 4: "the HDMI controller has all its registers shuffled
  and split in multiple controllers", and exposes a PHY with VCO/PLL
  registers (`HDMI_TX_PHY_CTL_0..3`, `HDMI_TX_PHY_PLL`,
  `HDMI_TX_PHY_CLK_DIV`). [LWN drm/vc4 BCM2711 series]

The **3D engine** is `V3D 4.2` (Broadcom internal id `V3D-7268`-class).
Reported by the kernel as `revision 4.2.14.0, MMU: yes`; supports a
**tiled bin/render pipeline plus TFU (texture formatting unit) and CSD
(compute shader dispatch)**. [drm/v3d kernel docs; bakhi.github.io v3d]

The legacy peripheral DMA block is the same family as Pi 3 but with
new "40-bit" channels: **channels 0–6 are normal, 7–10 are lite (64KB
limit), and 11–14 are 40-bit-capable** with bursts and access above
the 1 GB boundary. The DMA engines must be programmed with the
**legacy master addresses (`0x7Enn_nnnn`)**, which the VideoCore
re-translates to the 35-bit physical map. [BCM2711 ARM Peripherals
datasheet, "DMA Controller" chapter]

## 2. Linux driver landscape (`linux/drivers/gpu/drm/`)

Two separate DRM drivers cooperate on Pi 4:

- **`vc4/`** owns the **display pipeline**: HVS, CRTCs (pixel valves),
  HDMI0/1, DPI, DSI, VEC, TXP. Key files: `vc4_drv.c`, `vc4_kms.c`,
  `vc4_crtc.c`, `vc4_hvs.c`, `vc4_plane.c`, `vc4_hdmi.c`, `vc4_dpi.c`,
  `vc4_dsi.c`, `vc4_vec.c`. It exposes a standard DRM modesetting
  ABI (atomic KMS, planes, CRTCs, connectors). On Pi 0–3 it also owns
  V3D 2.x (`vc4_v3d.c`, `vc4_gem.c`, `vc4_render_cl.c`); on Pi 4 the
  3D bits of vc4 are **disabled** and a separate driver takes over.
  [kernel.org drm/vc4]
- **`v3d/`** owns the **3D engine** (Broadcom V3D 3.x and newer,
  including BCM2711). Compatible string `brcm,2711-v3d`. Files include
  `v3d_drv.c`, `v3d_gem.c`, `v3d_mmu.c`, `v3d_bo.c`, `v3d_sched.c`.
  ABI: V3D-specific ioctls (`SUBMIT_CL`, `SUBMIT_TFU`, `SUBMIT_CSD`,
  `WAIT_BO`, etc.) used by Mesa. [kernel.org drm/v3d; kernel.org
  v3d Kconfig; drm/v3d patchwork v6 series adding bcm2711]

Mesa uses both: the **V3D Gallium driver** drives V3D ioctls for
rendering, while a `vc4_dri.so` `kmsro` shim does buffer handoff to
the vc4 KMS driver for scanout. Pi 4 reaches **OpenGL ES 3.1 / GL
3.1** in mainline Mesa (development branch named **V3D 4.2**).
[mesa3d.org v3d driver page; Phoronix Mesa 19.3 V3D ES 3.1]

## 3. Firmware coupling

The VPU firmware (`start4.elf` etc.) still owns clocks (including PLLs
and the SDRAM clock), thermal management, and core power-state
sequencing on Pi 4. Linux talks to it through the **mailbox property
interface on channel 8**. Property tags relevant to display/GPU
bring-up include: `0x00040001` (allocate framebuffer),
`0x00048003/04/05` (set physical/virtual size, depth),
`0x00040008` (get pitch), `0x00038009` (set clock rate),
`0x00038046` (set domain power state). [raspberrypi/firmware wiki
"Mailbox property interface", "Mailbox framebuffer interface"]

What a kernel can do without firmware help:
- Read/write peripheral MMIO including HVS, Pixel Valves, HDMI PHY.
- Drive DMA engines on legacy bus addresses.

What it cannot do without firmware help (or a from-scratch port of
firmware-level code):
- Bring up the HDMI clock chain end-to-end at arbitrary modes (PLLH,
  pixel/BVB clocks live in an undocumented clock complex).
- Manage thermal throttling and load-step DVFS.
- Power-gate or un-gate the V3D and HDMI blocks.

## 4. Memory model

**V3D 4.2 has a single-level page-table MMU** mapping a 4 GB virtual
range to AXI bus addresses. The page table itself must be physically
contiguous (worst case ~4 MB). [bakhi.github.io v3d; kernel.org drm/v3d]

The Linux v3d driver uses `shmem`-backed BOs (not CMA) for general
GPU memory because of the V3D MMU, but **dmabuf imports of physically
contiguous buffers still occur** (e.g. for scanout from vc4). The
upstream kernel's Pi 4 device tree pins **CMA into the lower 1 GB**
(`alloc-ranges = <0x0 0x00000000 0x40000000>`) because some BCM2711
masters cannot address above 1 GB. [DeepWiki bcm2711.dtsi]

For Phoenix without CMA: minimum needed is a **boot-time-reserved
contiguous region** (carved out of the kernel's normal allocator) of
at least:
- One scanout-sized buffer (e.g. 1080p32 ~= 8 MB).
- ~4 MB for a hypothetical V3D MMU page table.
- A pool for V3D BOs (start with ~32 MB).

A `dma-buf`-style handle is **not** required for tiers 1–2; it would
only be relevant if Phoenix later wants Mesa interop.

## 5. Phoenix-RTOS minimum to claim "working", graded path

### Tier 0 — caches-off framebuffer text console **(have)**
Mailbox `allocate_buffer` + linear writes. Already in phase 1.

### Tier 1 — KMS-style mailbox modesetting (small lift)
Set physical/virtual size, depth, and pitch via channel 8 property
tags before allocating the framebuffer; respond to mode changes by
re-issuing the tag chain. Deps: Phoenix mailbox driver (have),
contiguous buffer allocator (have for FB), no DTB or interrupt
plumbing required. **Estimate: a few hundred LOC**, almost entirely
in a new `phoenix-rtos-devices` graphics driver. Reference: existing
mailbox FB code already in tree.

### Tier 2 — direct HVS+Pixel-Valve+HDMI scanout (large lift)
Skip firmware for display: program HVS DLIST entries, configure a
Pixel Valve CRTC, drive HDMI0 PHY/PLLs, hand pixels through. MMIO
regions: HVS (`0xfe400000`), pixel valves (`0xfe206000` and other
PV instances), HDMI0 (`0xfe902000`), HDMI1 (`0xfe905000`). Deps:
- Phoenix DTB consumption (currently minimal — would need to grow).
- Clock-tree code for PLLH / pixel / BVB clocks. The "BVB" (Bit
  Vector Bus) clock is one of `clock-names` for the Pi 4 hdmi node
  alongside the pixel and HSM clocks; getting it wrong is a known
  trap.
- Interrupt routing for vblank.
- Port `vc4_hvs.c`, `vc4_crtc.c`, `vc4_hdmi.c` minus the Linux DRM
  framework. **Estimate: multiple person-months**; this is the
  realistic upper bound of what Phoenix would build before stopping.

### Tier 3 — V3D 3D acceleration + Mesa userspace
V3D MMU programming, CL submission, scheduler, fence/sync, and a
DRM-shim ABI that Mesa's v3d Gallium driver expects. Deps: every
Tier-2 dep plus a Phoenix `dma-buf` analogue, a GEM-shaped BO
allocator, the V3D ioctl surface, and a Mesa port (`v3d` Gallium
driver currently assumes Linux DRM). **Estimate: not realistic
in-house** without committing to a Mesa fork. Mesa's V3D driver is
**~tens of thousands of LOC** and depends on `nir`, `kmsro`, and the
DRM uAPI. [mesa3d.org]

## 6. Known quirks

- **HDMI clock topology**: Pi 4 has dual HDMI controllers, each with
  its own PHY/PLL, plus a separate **BVB** (Bit Vector Bus) clock and
  an **HSM** clock. Mainlining HDMI took ~89 patches and a year of
  rework. [LWN "drm/vc4: Support BCM2711 Display Pipeline"]
- **HDMI without scrambling caps the mode**: BCM2711 HDMI is limited
  to lower modes when scrambling is not configured; a working driver
  must implement scrambling for 4K@60. [Patchwork v7 "Limit the
  BCM2711 to the max without scrambling"]
- **DMA legacy addressing** is mandatory; using ARM physical
  addresses on the legacy DMA channels yields silent corruption.
  [BCM2711 ARM Peripherals datasheet, DMA chapter]
- **Mailbox framebuffer pitch is firmware-chosen**, not equal to
  `width * bpp/8`; must be read back via `0x00040008`.
- **V3D 4.2 MMU page-table memory must be physically contiguous**
  (~4 MB worst case).

## 7. Open questions for the orchestrator

1. Tier 1 vs Tier 2 horizon — is mailbox-driven KMS sufficient for
   any realistic Phoenix demo, or is direct HVS scanout actually
   required?
2. Do we ever need 3D, or is GPU work scoped at "decent display
   output and a hardware cursor"? Tier-3 cost is an order of
   magnitude above Tier 2.
3. CMA strategy: does Phoenix want a real movable-page CMA, or just
   a static reservation? V3D itself does not require CMA; only
   scanout buffers do.
4. DTB depth: Tier 2 requires consuming clock-controller and
   interrupt-controller nodes that Phoenix currently ignores.
5. Public/upstreamability target: are Tier-1 helpers worth shaping
   to look like Linux DRM ioctls so Mesa could one day drop in, or
   should they stay Phoenix-shaped?

---

## Primary sources

- BCM2711 ARM Peripherals datasheet, `datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf` (chapters: DMA controller, peripheral address map).
- Raspberry Pi processor docs: <https://www.raspberrypi.com/documentation/computers/processors.html>.
- Linux drm/vc4 docs: <https://docs.kernel.org/gpu/vc4.html>.
- Linux drm/v3d docs: <https://docs.kernel.org/gpu/v3d.html>.
- drm/v3d Kconfig: <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/v3d/Kconfig>.
- drm/v3d add bcm2711 series (v6): <https://patchwork.kernel.org/project/linux-arm-kernel/patch/20220603092610.1909675-4-pbrobinson@gmail.com/>.
- "drm/vc4: Support BCM2711 Display Pipeline" (LWN summary): <https://lwn.net/Articles/830516/>.
- vc4 BCM2711 HDMI patch v2 89/91: <https://patchwork.kernel.org/patch/11508509/>.
- HDMI scrambling cap patch v7: <https://patchwork.freedesktop.org/patch/408324/>.
- Mailbox property interface: <https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface>.
- Mailbox framebuffer interface: <https://github.com/RaspberryPI/firmware/wiki/Mailbox-framebuffer-interface>.
- Mesa V3D driver page: <https://docs.mesa3d.org/drivers/v3d.html>.
- Mesa VC4 driver page: <https://docs.mesa3d.org/drivers/vc4.html>.
- Phoronix V3D OpenGL ES 3.1 in Mesa 19.3: <https://www.phoronix.com/news/V3D-OpenGL-ES-3.1-Bits-Mesa>.
- v3d hardware notes (third-party, useful summary of MMU/CL/TFU): <https://bakhi.github.io/mobileGPU/v3d/>.
- DeepWiki bcm2711 SoC notes (CMA in low 1 GB): <https://deepwiki.com/raspberrypi/linux/2.2-bcm2711-soc-(raspberry-pi-4)>.
- v3d_mmu source: <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/v3d/v3d_mmu.c>.
- vc4 sources (rpi tree): <https://github.com/raspberrypi/linux/tree/rpi-6.6.y/drivers/gpu/drm/vc4>.
