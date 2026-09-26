# E3 / E6 (+ E4 lite) — firmware planes, SMI vblank, scan-out range

Experiments E3 and E6 of the [new-lane plan](PLAN.md), with a first E4 number, for the display
server design in [`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md)
§4.5 (Stage A: firmware planes) and §6.

**Status:** facts gathered from source, probe written and compiled
(`tools/gpu-lane/kmsprobe/`). The Pi run below is **pre-registered and has not been run**. No
existing driver was changed. A proposed additive `/dev/vcmbox` extension is in
`tools/gpu-lane/kmsprobe/vcmbox-xl.patch`; it is **not applied**.

## Questions

- **E3-a** Does the pinned firmware honour property-mailbox `SET_PLANE` (tag `0x00048015`) for a
  physically contiguous buffer **we** allocate? For more than one plane at once (full-screen
  primary + a small overlay moved every frame)?
- **E3-b** Does the firmware raise the **SMI vblank interrupt** (SMI block `0xfe600000`, GIC SPI 112
  = Phoenix IRQ 144), and at what rate and jitter? Does it depend on `dtoverlay=vc4-fkms-v3d`?
- **E3-c** What happens to the firmware framebuffer (fbcon, `/dev/fb0`) under and around our
  planes? Can it be blanked and brought back?
- **E6-a** Which physical range can a plane scan out from: low RAM vs above 1 GiB, and with which
  bus-address convention (`0xC0000000 | PA` vs raw PA)?
- **E6-b** How much physically contiguous memory is realistic for a scan-out pool?
- **E4-lite** `SET_PLANE` latency per call, flips/s with vsync off, and how long after
  `SET_PLANE` returns the hardware actually has the new pointer.

## 1. What is known before the run

### 1.1 Sources read

Facts only (tag numbers, struct layouts, register offsets, IRQ numbers). No code was copied.

| Source | Licence | Used for |
|---|---|---|
| `external/linux` = `github.com/raspberrypi/linux` @ `57083d8cf8f0` (2026-06-09): `drivers/gpu/drm/vc4/vc4_firmware_kms.c`, `vc4_regs.h`, `vc4_hvs.c`, `include/soc/bcm2835/raspberrypi-firmware.h`, `include/linux/broadcom/bcm2835_smi.h`, `arch/arm/boot/dts/broadcom/bcm2711*.dtsi`, `overlays/vc4-fkms-v3d*-overlay.dts`, `overlays/README` | GPL-2.0 | wire formats and facts only |
| `raspberrypi/userland` `interface/vctypes/vc_image_types.h` (master, fetched 2026-09-26) | BSD-3 | `VC_IMAGE_*` enum; values checked by compiling the header on the host: `ARGB8888 = 43`, `XRGB8888 = 44` |
| Bench DTB: `.buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/bcm2711-rpi-4-b.dtb`, decompiled with `dtc` | — | node addresses, IRQs and `dma-ranges` on **this** bench |
| Bench `config.txt`, `overlays/vc4-fkms-v3d.dtbo`, `start4.elf` (`strings`) | — | what the overlay changes |
| `sources/phoenix-rtos-devices/misc/rpi4-vcmbox/` @ `432db4cc` | Phoenix | `/dev/vcmbox` ABI and its limits |
| `sources/phoenix-rtos-kernel` (`syscalls.c`, `vm/object.c`, `vm/page.c`, `hal/aarch64/interrupts_gicv2.c`, `hal/aarch64/generic/generic.c`, `proc/userintr.c`) | Phoenix | `MAP_CONTIGUOUS`, IRQ trigger config, user IRQ semantics |

### 1.2 Wire formats

**Property tags used** (`raspberrypi-firmware.h`): `GET_FIRMWARE_REVISION 0x1` (:38),
`GET_BOARD_REVISION 0x10002` (:46), `GET_ARM_MEMORY 0x10005`, `GET_VC_MEMORY 0x10006` (:49-50),
`FRAMEBUFFER_BLANK 0x40002` (:106), `GET_PHYSICAL_WH 0x40003`, `GET_VIRTUAL_WH 0x40004`,
`GET_DEPTH 0x40005`, `GET_PIXEL_ORDER 0x40006`, `GET_PITCH 0x40008`, `GET_VIRTUAL_OFFSET 0x40009`,
`GET_LAYER 0x4000c` (:116), `GET_NUM_DISPLAYS 0x40013` (:124), `GET_DISPLAY_ID 0x40016` (:122),
`SET_DISPLAY_NUM 0x48013`, `SET_VIRTUAL_OFFSET 0x48009`, `SET_VSYNC 0x4800e` (:149, blocks until
the next vsync: `bcm2708_fb.c:719-725` FBIO_WAITFORVSYNC), `SET_PLANE 0x48015` (:156),
`GET_DISPLAY_TIMING 0x40017` (:157), `GET_DISPLAY_CFG 0x40018` (:159).

**`SET_PLANE` value buffer: 60 bytes = 15 words** (`vc4_firmware_kms.c:64-96`), little-endian:

| word | contents |
|---|---|
| 0 | `display` u8 · `plane_id` u8 · `vc_image_type` u8 · `layer` s8 |
| 1 | `width` u16 · `height` u16 |
| 2 | `pitch` u16 (bytes) · `vpitch` u16 |
| 3–6 | `src_x`, `src_y`, `src_w`, `src_h` — u32, **16.16 fixed point** |
| 7 | `dst_x` s16 · `dst_y` s16 |
| 8 | `dst_w` u16 · `dst_h` u16 |
| 9 | `alpha` u8 · `num_planes` u8 · `is_vu` u8 · `color_encoding` u8 |
| 10–13 | `planes[4]` — u32 bus address per image plane |
| 14 | `transform` (bit 1 rot180, bit 16 hflip, bit 17 vflip; `:99-102`) |

- `display` is the firmware **display id** from `GET_DISPLAY_ID(index)`, not the index
  (`:1993-2006`; id 2 = HDMI0 in the type table `:344-357`).
- `plane_id` = `i + display_index * 8`, 8 planes per display: 0 primary, 1–6 overlay, 7 cursor
  (`:1868-1877`).
- Linux maps KMS zpos 0 to layer −127 and keeps the others (`:890-918`, `:556-557`); the comment
  says the firmware fb defaults to layer −127 [cited, `:891`].
- **Unset a plane** = `SET_PLANE` with only `display` and `plane_id` filled, all else zero
  (`vc4_plane_set_blank`, `:385-395`).
- `planes[0]` = `bo->dma_addr + offset` (`:560-561`). The DRM device is the `vc4` node, which sits
  at the **DT root** (`bcm2711.dtsi:15`, bench DTB `/gpu` line 2042) with no `dma-ranges`, and CMA
  is confined below 768 MiB (`bcm2711-rpi-ds.dtsi:81`). So Linux passes the **raw CPU physical
  address, below 768 MiB** [derived]. The firmware itself reports the framebuffer to the ARM as
  `0xC0000000 | PA` (plo masks it, `plo/hal/aarch64/generic/video.c:191`).
- `GET_DISPLAY_TIMING` value = `struct set_timings`, 36 bytes (`:129-174`); request word 0 low byte =
  display id (`:1398-1402`). `GET_DISPLAY_CFG` = 2 × u32 max pixel clock, Hz (`:47-49`, `:1975-1985`).
- Firmware fb blank: `SET_DISPLAY_NUM(index)` + `FRAMEBUFFER_BLANK(1)` in one list (`:1848-1866`).

**SMI vblank convention** (`vc4_firmware_kms.c:262-271`, `:1225-1273`, `:2018-2027`;
`bcm2835_smi.h:184-248`):

- Registers: `SMICS` +0x00, `SMIDSW0` +0x14, `SMIDSW1` +0x1c.
- The firmware signals by setting `SMICS` bits 9–11 (INTD/INTT/INTR, the SMI interrupt enables).
  The ARM handler writes `SMICS = 0`, then reads `SMIDSW0`.
- If `SMIDSW0 & 0xffff0000 != 0xabcd0000`: old firmware, one event for all displays. Otherwise bit 0
  of `SMIDSW0` / `SMIDSW1` = vblank of display 0 / 1, acknowledged by writing `0xabcd0000` back.
- Linux does nothing to *enable* it beyond `SMICS = 0` before requesting the IRQ.
- Bench DTB `firmwarekms@7e600000` (lines 1910-1917): `reg <0x7e600000 0x100>`,
  `interrupts <0 0x70 4>` = **GIC SPI 112, level-high**, `status = "disabled"` in the base DTB.
  Phoenix IRQ = 112 + 32 = **144** (the SPI+32 convention of `bcm-genet`'s 157 → 189). Phoenix
  configures every SPI level-high (`hal/aarch64/generic/generic.c:32-35`), `SIZE_INTERRUPTS = 256`.
- A Phoenix user IRQ handler runs at EL1 in the registering process's address space; returning
  ≥ 0 broadcasts the cond, < 0 does not (`proc/userintr.c:49-98`).

**HVS witness** (read-only). BCM2711 = HVS5, `reg <0x7e400000 0x8000>` (bench DTB line 1403-1410,
IRQ SPI 97): `SCALER_DISPCTRL` 0x00, `DISPSTAT` 0x04, `DISPLISTx` 0x20+4x (word index into dlist
memory), `DISPCTRLx` 0x40+0x10x, `DISPSTATx` 0x48+0x10x, mode = bits 31:30 (0 disabled, 1 init,
2 run, 3 eof) (`vc4_regs.h:240-475`). Display-list memory at **+0x4000** for HVS5
(`vc4_hvs.c:2182`, `vc4_regs.h:550`), 16 KiB = 4096 words (`vc4_regs.h:548`). An entry's control word
has END = bit 31, SIZE = bits 29:24 (`vc4_regs.h:1136-1140`); walk as in `vc4_hvs.c:264-276`.
HVS5 frame counters, 6 bits: ch0 = `DISPSTAT1[25:20]`, ch1 = `DISPSTAT1[19:14]`,
ch2 = `DISPSTAT2[19:14]` (`vc4_hvs.c:812-826`). HVS5 plane pointers are single 32-bit words
(`vc4_plane.c:1754`; the upper-bits word is HVS6 only).

**Bus-address facts** (bench DTB): `soc` `dma-ranges = <0xc0000000 0x0 0x0 0x40000000 …>`
(line 141): soc-bus masters reach **the low 1 GiB only**, through the `0xC0000000` alias. The HVS
node is in `soc`. `emmc2bus` has the same window (line 2070). The SD driver's "write-DMA quirk" was
exactly a missing `0xC0000000` translation (memory `project_pi4_sd_fullspeed_state`, 2026-09-20).

**`/dev/vcmbox` ABI limit.** A request is `vcmbox_req_t` in the 64-byte `msg.i.raw`: 3 header
words + **at most 12 value words (48 bytes)** (`libvcmbox.h:40-53`). `vcmbox_call` rejects
`valBufSize > 48` (`libvcmbox.c`), and the server caps value words at 12 but writes the *uncapped*
`valBufSize` into the tag header (`rpi4-vcmbox.c:167-183`). **`SET_PLANE` (60 B) and
`GET_EDID_BLOCK_DISPLAY` (136 B) do not fit.** M2 needs a large-buffer path in any case. Options the
probe supports:

- `xl` — [`vcmbox-xl.patch`](../../tools/gpu-lane/kmsprobe/vcmbox-xl.patch): additive. `req->nIn =
  0x584c0000` selects a path carrying the value buffer in `msg.i.data` / `msg.o.data`, up to 1 KiB.
  A current server rejects `nIn > 12` with `-EINVAL` **before touching the FIFO**
  (`rpi4-vcmbox.c:232-236`), so the probe detects it safely. Checked with `git apply --check`;
  the patched server compiles standalone with `-Wall -Wextra`. Not applied.
- `raw60` (the default on the current server) — send 12 value words with `valBufSize = 60`. The
  firmware reads words 12–14 (`planes[2]`, `planes[3]`, `transform`) from the END word (zeroed by
  the server's memset) and from bounce-buffer words 18–19. **No vcmbox transaction writes those
  words (the most it builds is 18), but they are not zero either:** the bounce buffer is a
  `MAP_CONTIGUOUS` page (`rpi4-vcmbox.c:309-310`), and `MAP_CONTIGUOUS` memory is **not zeroed**
  (`rpi4-audio.c:638-639`; the V3D binner-pool bug). They hold whatever DRAM held when the server
  started, and a non-zero `transform` would mirror or rotate the plane, or get the tag rejected.
  So before using `raw60` the probe **measures the instrument**: `bounce_tailCheck()` finds the
  bounce PA (`-P <pa>`, else the server's boot banner `bounce buf_pa=0x…` in `/dev/kmsg`, a 64 KiB
  ring on this project, `board_config.h:67`). It proves the page is the bounce buffer by
  fingerprinting a `GET_FIRMWARE_REVISION` it has just made (`[28, RESP_OK, 0x1, …, rev]`), prints
  words 17–20, and zeroes 18–20 if they are not zero. The server never reads or writes those words;
  both mappings are uncached. Every `SET_PLANE` result line carries `tail=clean|zeroed|dirty|unknown`.
  With `tail=unknown`, a failure is **not** interpretable. **Still unknown:** whether the firmware
  tolerates a total-size word (72) smaller than the tag it describes.
- `v48` — declare a 48-byte value buffer (a consistent message). The firmware reads the same tail if
  it ignores the declared size, and may reject the short tag.

### 1.3 What `dtoverlay=vc4-fkms-v3d` changes (bench `config.txt` has it)

The bench overlay (`overlays/vc4-fkms-v3d.dtbo`, decompiled) edits the DT that the firmware hands
over. Phoenix does not read that DT for display. The overlay does five things:

1. `cma` size = 256 MiB. Linux only.
2. `fb` (`/soc/fb`, simplefb/bcm2708_fb) → `disabled`.
3. `firmwarekms` (`/soc/firmwarekms@7e600000`) → `okay`.
4. `v3d` → `okay`.
5. `vc4` (`/gpu`) → `okay`, plus bootargs `clk_ignore_unused`.

What the **firmware** does with it is not documented. `start4.elf` contains the strings
`/soc/firmwarekms@7e600000` and `/soc/fb` next to each other, and `fake_vsync_isr`, `vc4-fkms-v3d`
and `vc4-kms-v3d`. The project already relies on the firmware reacting to this overlay: the
config comment says V3D MMIO reads `0xdeadbeef` without it. **[inferred]** The firmware inspects the
status of the `firmwarekms` node, and plausibly gates the SMI "fake vsync" interrupt and/or the
fkms plane API on it.

**Consequence for the design:** a cycle on the bench as-is (overlay present) answers only the
with-overlay case. The probe does not depend on the DT, and it records which vblank sources work,
so it can be rerun unchanged with the overlay removed (Cycle B, optional, §4). That cycle would also
lose V3D for the old lane until the line is restored, so it is the coordinator's decision.

### 1.4 Bench memory facts (from the 2026-09-26 boot logs)

- 4 GB board: `vm: Initializing page allocator (35724+60)/3997696KB`.
- Firmware fb at PA `0x3d3fd000`, 1920×1080, pitch 7680, virtual height 3240 (triple buffer).
- `gpu_mem=128`. The GPU reserve is at the top of the first GiB; `info` reports the exact range
  via `GET_VC_MEMORY`.
- `/dev/vcmbox` bounce buffer at `0x03a73000`.
- On a 4 GB Pi 4, RAM also lies above 4 GiB. **A 64→32-bit truncation there points the firmware at
  unrelated low memory.** The probe refuses such buffers instead of wrapping them.
- `MAP_CONTIGUOUS` = `vm_objectContiguous` → buddy `_page_alloc`: power-of-two blocks and **no
  placement control** (`vm/object.c:480-512`, `vm/page.c:39-88`). A 1080p XRGB buffer
  (8,294,400 B) takes an 8 MiB block. The pages are not zeroed. `MAP_UNCACHED` mappings are
  cleaned and invalidated at map time (kernel `5d8645f6`).

## 2. Method

`kmsprobe` (see its [README](../../tools/gpu-lane/kmsprobe/README.md)) is a standalone Phoenix
process. All mailbox calls go through `/dev/vcmbox`.

The key design choice is an **independent witness**. A mailbox `RESP_OK` does not show that a
plane was accepted: `/dev/vcmbox` returns no per-tag response bit, and an unknown tag is simply left
unanswered. So after every `SET_PLANE` [inferred: the firmware leaves an unknown tag unanswered rather than failing the call] the probe walks the **hardware display lists** (HVS mapped
read-only) and looks for the buffer's address in the low 30 bits of any word. It reports the full
word, so **the alias the firmware wrote** is visible (E6). The HVS frame counters give a vblank
count that does not depend on SMI. The HDMI snapshots give the visual check.

- `vblank` runs in three phases. **(1)** 1 s of polling `SMICS` / `SMIDSW0` and `GICD_ISPENDR`
  bit 144, with no handler registered: "the firmware never raises it" vs "the IRQ does not reach
  us". **(2)** `secs` of the Phoenix IRQ. **(3)** 30 blocking `SET_VSYNC` calls, which give a
  refresh estimate that needs neither.
- `stack` shows what happens around the firmware fb, in five visual steps held `hold` seconds each:
  **(1)** green 256² plane 1, layer 1, at (64, 64); **(2)** + blue 256² plane 0, layer −127, at
  (384, 64); **(3)** fb blanked; **(4)** fb unblanked; **(5)** both planes unset.
- `plane` flips a full-screen primary between buffers A and B every vblank, with the 240² checker
  marker at left (A) or right (B). A 128² magenta overlay moves 4 px per vblank. The primary sits
  one layer above the firmware fb unless `-B` is given. The first 48 flips measure **latch time**
  (SET_PLANE return → new pointer in the HVS list). The steady phase then measures flips/s, missed
  vblanks and SET_PLANE latency per plane. The vsync source is the SMI IRQ if it delivers within
  100 ms, otherwise the HVS frame counter, otherwise a 60 Hz timer. The source used is reported.
- `flipmax` flips the primary with no vsync (E4).
- `range` needs placement, which the buddy allocator does not offer. It takes up to 48 × 8 MiB
  chunks until one lands in the requested window, gives the rest back, then shows a full-screen
  checkerboard from it with each eligible convention: `c0` = `0xC0000000|PA` (only if the buffer
  ends ≤ 1 GiB), then `00` = raw PA (only if it ends ≤ 4 GiB). `fb1` uses the firmware fb's own
  second stacked buffer (`fb_pa + pitch*1080`), mapped `MAP_PHYSMEM`: a region the HVS is known to
  scan. It is the **control**, separating "SET_PLANE does not work" from "our memory is not
  scannable".
- `contig` finds the largest single `MAP_CONTIGUOUS` allocation (256, 128, 64, 32, 16 MiB, freed at
  once) and counts how many 8 MiB chunks land below 1 GiB, at 1–4 GiB and above 4 GiB, up to a cap.

**Colour key for `range` snapshots:**

| target | `c0` | `00` |
|---|---|---|
| `lo` / `any` / `<pa>` | red / white | blue / yellow |
| `hi` | (skipped: above the `c0` window) | cyan / black |
| `fb1` | orange / black | white / purple |

Anything else on screen during a `range` hold (noise, an old frame, black) means the HVS scanned
other memory, or nothing.

## 3. Run plan — Cycle A (bench as-is, overlay present)

**Transport: pick one before the cycle.**

- **Recommended: apply [`vcmbox-xl.patch`](../../tools/gpu-lane/kmsprobe/vcmbox-xl.patch)** to
  `sources/phoenix-rtos-devices`, rebuild `--scope core`, and check with `strings` on
  `loader.disk` that `rpi4-vcmbox` has the new path. The patch is additive (existing callers take
  the same code path through the refactored `vcmbox_run`), but it changes a boot-critical server,
  so it needs the project's `--scope core` gate before it goes on `master`. With it, the probe
  prints `vcmbox xl=1 … setplane_transport=xl` and nothing below depends on server internals.
- **Hedge, no rebuild:** the current server, with `raw60`. Grade only runs whose lines read
  `tail=clean` or `tail=zeroed`. If `vcmbox_tail located=0`, read `bounce buf_pa=` from the boot log
  of that same cycle; the next cycle can pass it with `-P`. The PA changes from boot to boot:
  27 different values in the last 40 logs.

**Preconditions:**

- Netboot lane.
- No other Pi cycle running.
- No GPU app in the command list. The old winsys still flips via a **direct** FIFO write
  (`gpu/rpi4-v3d/mesa/v3d_phoenix_power.c:627`), which would race `/dev/vcmbox`.
- Binary staged at the live `fsid=0` export root as `/kmsprobe` (see README).

**Build + stage:**

```
S=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot; V=sources/phoenix-rtos-devices/misc/rpi4-vcmbox
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -std=gnu11 --sysroot=$S/ -B$S/lib/ -I$V \
    -o tools/gpu-lane/kmsprobe/kmsprobe tools/gpu-lane/kmsprobe/kmsprobe.c $V/libvcmbox.c
EXPORT=$(awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports); sudo cp tools/gpu-lane/kmsprobe/kmsprobe "$EXPORT/kmsprobe"
```

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label e3-kms --idle-secs 6 --max-cmd-secs 120 \
    --hdmi-dense-on 'KMSPROBE stack start' -- \
    "/kmsprobe info" \
    "/kmsprobe vblank 10" \
    "/kmsprobe stack 6" \
    "/kmsprobe plane 20" \
    "/kmsprobe restore" \
    "/kmsprobe flipmax 8" \
    "/kmsprobe range fb1 8" \
    "/kmsprobe range lo 8" \
    "/kmsprobe range hi 8" \
    "/kmsprobe -b c0 plane 8" \
    "/kmsprobe contig 256" \
    "/kmsprobe restore"
```

**Harness notes:**

- Every mode prints a `KMSPROBE tick` line at least every 2 s, so `--idle-secs 6` never cuts a
  running command. The longest command is `stack 6`, about 35 s, well under `--max-cmd-secs 120`.
- Estimated wall clock:
  - harness overhead ≈ 12 × (6 s idle + 3 s between commands + 2 s newline re-send) ≈ 130 s;
  - probe run time ≈ 150 s;
  - netboot to the prompt 60–150 s;
  - total ≈ 6–7 min.
- If it has to be split, use two cycles:
  - **A1:** `info`, `vblank 10`, `stack 6`, `plane 20`, `restore`;
  - **A2:** `info`, `flipmax 8`, `range fb1 8`, `range lo 8`, `range hi 8`, `-b c0 plane 8`,
    `contig 256`, `restore`.
- Do **not** pass `--stamp`: it injects `[T+…]` markers into the middle of lines and breaks
  tag-grepping.
- HDMI snapshots go dense (every 5 s) from the first `stack` line, so every hold of 6–8 s gets at
  least one frame. Snapshots land in `artifacts/hdmi/<ts>-e3-kms-tick.png`, in order. Map them to
  steps by the order of the `stack step=` / `range try` / `plane start` lines.
- `-b c0 plane 8` is a hedge in case the default raw-PA convention shows nothing. If `range lo`
  shows that `00` works, it adds nothing and can be dropped.

**Grading** (tagged lines only):

```
grep -a '^KMSPROBE ' artifacts/rpi4b-uart/rpi4b-uart-*-e3-kms.log
```

Key lines:

- `vcmbox xl=` and `vcmbox_tail … tail=` (transport, and whether the raw60 result is interpretable)
- `display …` (mode, fb, fb layer, `fb_in_dlist`, HVS channel)
- `info fb_ptr … word=` (the alias the firmware uses for its own fb)
- `vblank poll|irq|mbox_vsync|verdict`
- `stack step=1..5`
- `plane first|latch|result`
- `flipmax result` (its `vblanks_irq` counts vblanks during the vsync-off loop)
- `range try …`
- `contig …`
- `restore done`

## 4. Optional Cycle B (overlay removed) — only if Cycle A leaves E3-b open

Run this if Cycle A shows no SMI IRQ, or if it works and we want to know whether Stage A depends on
the overlay:

1. Comment out `dtoverlay=vc4-fkms-v3d` in the TFTP bootfs `config.txt`.
2. Run the same command list minus `flipmax`, `range` and `contig`.
3. Restore the line.

The old lane's V3D is gated for that boot (display is unaffected). The probe needs no change.

## 5. What each outcome means for M2 (pre-registered)

**Stage A viable as designed**, all of:

1. A `SET_PLANE` of **our** buffers is in the HVS list (`primary_in_dlist=1`) and visible, for the
   primary and the overlay at once, in at least one convention.
2. `plane result` at the display refresh: `flips_per_s` within 2 % of `hvs_hz`, `missed` ≤ 1 % of
   flips, `setplane_us_p99` ≤ 2 ms per plane. Two planes per frame must fit inside one frame with
   a wide margin.
3. A vblank source with jitter ≤ 1 ms that costs no mailbox call per frame: SMI IRQ
   (`vblank irq hz≈refresh`) or, failing that, the HVS frame counter.
4. `restore done fb_in_dlist=1`: fbcon comes back.

**Findings that change the plan:**

| Observation | Meaning for M2 |
|---|---|
| SMI IRQ at ≈ refresh, `poll=yes`, `gic_pending_samples>0` | Stage A vblank as designed (§4.5). |
| `poll=no`, `gic_pending_samples=0`, but HVS frames advance | The firmware raises no SMI event in this configuration. Run Cycle B to see whether it is overlay-gated. Stage A can still run on the **HVS frame counter** (a polling thread; the HVS's own interrupt, SPI 97, would need writes into firmware-owned HVS state) — a design change in §4.5, not a blocker. |
| `poll=yes` but `vblank irq count=0` | The firmware raises it but Phoenix IRQ 144 is not delivered: a plumbing bug on our side (GIC routing / SPI number). Fixable, not a firmware limit. |
| `SET_PLANE` shows nothing and is not in the list, **but `range fb1` works** | The firmware accepts only memory it allocated, or our convention is wrong. Read the `range lo` rows: if neither `c0` nor `00` works for our memory, Stage A cannot use client buffers → **Stage B (native HVS)** or firmware-allocated buffers (`ALLOCATE_MEMORY`/`LOCK_MEMORY` tags) become necessary. |
| Nothing works, including `fb1`, with `transport=raw60`/`v48` | Not a firmware verdict: the old-server transport may be malformed (and it is uninterpretable if `tail=unknown`). Apply `vcmbox-xl.patch` and rerun before concluding — which is why §3 recommends running with it in the first place. |
| Only one convention works | That convention is the Stage A contract. Record the word the firmware wrote (`dlist_word`, `word_alias`). |
| `range hi` shows the pattern | Scan-out reaches above 1 GiB, so the pool can be anywhere below 4 GiB. If it shows noise / other content → the pool must live below 1 GiB (§4.4 "below the 32-bit bus limit" becomes "below 1 GiB"). |
| `stack` step 2: blue plane invisible while fb is up | Layer −127 sits under the fb. M2 must blank the fb (as Linux does) or stack above it; step 3 then shows whether blanking uncovers it. |
| `stack` step 3/4: fb goes and comes back | Console handover by `FRAMEBUFFER_BLANK` works. M2 can hand the display back to fbcon on server exit or crash. If it does **not** come back, the M2 server must own the console (fbcon into a kms buffer) before it can ship. |
| `plane latch` ≈ 0 µs (pointer present when the call returns) | The firmware writes the list synchronously and the swap happens at the next frame start: a flip completes at the next vblank, so `FLIP_COMPLETE` = next vblank event. If latch ≈ one frame: `SET_PLANE` itself waits for vblank (a blocking flip), so the server must not issue it from its event loop. **Caveat:** the flip alternates A/B, so the "new" buffer was on screen two frames earlier. If the firmware double-buffers its lists, a stale copy may still hold that pointer, so a bimodal histogram (some ≈ 0, some ≈ a frame) can be an artefact; `hvs_dump` shows whether `DISPLISTx` alternates. |
| `flipmax flips_per_s` ≫ refresh | Async (tearing) flips are possible through the firmware; E4's 44 vs 141 fps gap is then not flip latency. If `flipmax` ≈ refresh: every `SET_PLANE` blocks to vblank, which alone caps a multi-plane commit at refresh/planes. Weigh **Stage B**. |
| `setplane_us_p99` > 8 ms or frequent `missed` with 2 planes | The firmware mailbox is too slow for per-frame multi-plane commits. Stage A only for the primary + cursor; overlays need Stage B. |
| `contig` 8 MiB chunks mostly above 1 GiB | A scan-out pool must be reserved **early** (first allocation at server start) or by a boot-time carve-out, if scan-out turns out to be limited to 1 GiB. |

## 6. Risks and how the probe handles them

- **Display left dark.** Every plane touched is recorded and unset on exit, on SIGINT/SIGTERM, and
  after each mode. The fb is unblanked. If the fb pointer is then missing from the HVS list but was
  there at start, the probe re-asserts the fb (`FRAMEBUFFER_BLANK(0)` + `SET_VIRTUAL_OFFSET` to the
  saved offset) and reports `restore … fb_in_dlist`. `kmsprobe restore` unsets all 8 planes after a
  crash, and the plan runs it twice. Worst case: HDMI stays dark until reboot. UART / psh are
  unaffected, and the next cycle boots clean.
- **IRQ storm** (a level line the handler cannot silence). The handler clears `SMICS` even on a
  spurious entry. If claimed + spurious entries exceed 2000/s, the handler is removed and the test
  falls back to the HVS counter (`vblank storm` line).
- **Wrong address handed to the HVS** (E6 trials). The HVS only reads. Garbage or another image on
  screen for one hold is the expected "no" answer. PAs are never truncated, above-4 GiB buffers are
  refused, and contiguity is checked with `va2pa` of the first and last page.
- **`raw60` relies on server internals.** Bounce words 18–19 are whatever DRAM held (not zeroed);
  the probe locates, verifies and zeroes them first (`vcmbox_tail`) and tags every result with
  `tail=`. It writes three words of another process's page, words that process provably never
  uses. A firmware that copies the property buffer by its total size could still read a non-zero
  `transform` → the image mirrored or rotated (visible, harmless) or the
  tag rejected. The `xl` patch removes the dependency.
- **`/dev/vcmbox` blocked by `SET_VSYNC`**: 30 calls ≈ 0.5 s, during which thermal and other
  clients wait. Every wait inside the server is bounded (`MBOX_SPINS`, 8 retries). On the client
  side, `msgSend` is unbounded only if the server itself hangs.
- **Memory pressure in `contig` / `range`**: up to 256 MiB + 256 MiB (contig) or 384 MiB (range)
  held for about a second, then freed. A 256 MiB `MAP_CONTIGUOUS` needs a 512 KiB kernel
  allocation for its page array; that may fail, and is reported as `ok=0`.
- **GIC distributor mapped read-only** to read one pending bit; no writes.

## 7. Not determinable before the run

- Whether the firmware raises SMI vblank or honours `SET_PLANE` **without** the overlay (Cycle B).
- **First among the confounds:** on the current server, whether the `raw60` / `v48` tail was
  really zero when the firmware read it. This is only known if `vcmbox_tail` located and verified
  the page (`tail=clean|zeroed`), and even then the firmware may read beyond the message's declared
  total size. The `xl` path is the clean answer.
- Whether a raw PA above 1 GiB reaches the right memory on the HVS5 bus, or aliases into the low
  GiB the way VC4's legacy aliases did [inferred risk]. `range hi` answers it.
- The layer of the firmware fb (`GET_LAYER` may not be implemented; the probe falls back to
  layer 0 for planes meant to cover it).
- Whether plane 0 *replaces* the firmware fb element or adds to it. `stack` step 2 plus the
  `fb_in_dlist` column answer it.
- How the firmware behaves if a DRM-style client later calls `SET_TIMING` (mode change). Not
  exercised here.

## 8. Results

*(to be filled after Cycle A: log path, snapshot paths, the tagged lines, the outcome row that
applies, and the resulting M2 decision)*
