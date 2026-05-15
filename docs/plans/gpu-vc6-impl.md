# Phoenix-RTOS Pi 4 GPU support — implementation plan

Source briefs:
[`docs/research/gpu-vc6.md`](../research/gpu-vc6.md),
[`docs/research/gpu-vc6-non-linux.md`](../research/gpu-vc6-non-linux.md).
This plan converts the research synthesis into concrete deliverables for the
coordination repo and the sibling `phoenix-rtos-devices` /
`phoenix-rtos-project` trees.

## 1. Goal

"GPU works on Phoenix" is intentionally graded along the tier ladder defined
in the research brief. Each tier is its own deliverable; the project commits
only to Tiers 1 and 2 below.

- **Tier 0 — current text fbcon (DONE).** plo allocates a 1024x768x32
  framebuffer via the property mailbox in
  [`sources/plo/hal/aarch64/generic/video.c`](../../../sources/plo/hal/aarch64/generic/video.c)
  and publishes `graphmode_t` through the syspage. `pl011-tty` consumes it
  via `pctl_graphmode` and renders a fixed-font console with caches off
  (`pl011_fbcon_*` in
  [`sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`](../../../sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c)).
  This plan factors that mailbox transport into a reusable library and
  layers more behavior on top without regressing Tier 0.
- **Tier 1 — mailbox-driven KMS shim.** A userspace server `rpi4-vc6-fb`
  owning channel-8 property tags at runtime: dynamic mode set (resolution,
  depth, pixel order, virtual size, pan) plus a `/dev/fb0` device with
  mmap and a Linux-fbdev-shaped devctl ABI.
- **Tier 2 — direct HVS/PV/HDMI scanout.** Same server, additional backend
  programming HVS DLISTs, a Pixel Valve, and HDMI0 PHY/PLL directly,
  bypassing firmware. Gated on section 10 open questions (BVB clock,
  scrambling for >FHD, license risk vs `drivers/gpu/drm/vc4`).
- **Tier 3 — V3D 3D / Mesa.** Out of scope. Tens of thousands of LOC plus
  a DRM-shaped uAPI port; no non-Linux OS surveyed does this in
  production. Recorded so future steps do not silently re-open it.

Non-goals carried from the briefs: no DRM/KMS uAPI parity, no DispmanX,
no UEFI GOP, no `dma-buf`, no atomic-commit semantics.

## 2. Phoenix conventions audit

What the new code must match:

- **Server pattern.** Drivers run as userspace servers using `portCreate` +
  `msgRecv` + `create_dev`, like `pl011-tty` (lines 988–1024 of
  `pl011-tty.c`). The new server registers `/dev/fb0` via `create_dev`.
- **devctl ABI.** Driver-specific control flows over `mtDevCtl`. Phoenix
  has no in-tree fbdev consumer today, so wire-level numbers are free —
  section 4 keeps them aligned with Linux fbdev (`FBIOGET_VSCREENINFO =
  0x4600`) for trivial portable tests.
- **Mailbox protocol already in tree.** Two real consumers exist:
  - plo: `video_mailboxCall()` / `video_framebufferInit()` in
    [`sources/plo/hal/aarch64/generic/video.c`](../../../sources/plo/hal/aarch64/generic/video.c)
    (lines 101–197). 16-byte-aligned scratch buffer, all tags in one
    request, mask returned base with `0x3fffffff`.
  - pcie: `bcm2711NotifyXhciReset()` in
    [`sources/phoenix-rtos-devices/pcie/server/pcie.c`](../../../sources/phoenix-rtos-devices/pcie/server/pcie.c)
    (lines 219–283). `mmap(MAP_DEVICE|MAP_PHYSMEM)` for mailbox MMIO,
    `mmap(MAP_UNCACHED|MAP_CONTIGUOUS)` for the message buffer, `va2pa()`
    for the bus hand-off.
  Tier-1 server follows the **pcie userspace pattern**, not the plo
  bare-metal pattern.
- **Framebuffer access.** Userspace today opens the FB with
  `mmap(MAP_SHARED|MAP_UNCACHED|MAP_ANONYMOUS|MAP_PHYSMEM, -1, paddr)`
  (`pl011-tty.c:492`). The new server publishes the FB physical address
  via devctl; clients map it the same way. After TD-04 lands, the default
  becomes write-back with point-of-coherency flushes.
- **syspage `graphmode`.** Already populated by plo
  ([`sources/plo/syspage.c:676`](../../../sources/plo/syspage.c)). The
  Tier-1 server **inherits** the existing FB on first boot rather than
  re-allocating, so the fbcon and the new server agree on the live mode.
- **Kernel HAL.** Pi 4 HAL is in
  [`sources/phoenix-rtos-kernel/hal/aarch64a72/`](../../../sources/phoenix-rtos-kernel/hal/aarch64a72/);
  this plan adds no kernel code. Cache-coherency for FB writes is
  TD-04 / Stage 1 work, not in this plan.
- **Device naming.** `/dev/fb0` (and `/dev/fb1` once Phase 4 lands).
  Matches Linux/Mesa naming so a future Tier-3 drop-in does not rename.

## 3. File-level breakdown

New files (all in `sources/phoenix-rtos-devices/`):

- `graphics/rpi4-vc6-fb/Makefile` — binary entry, `NAME := rpi4-vc6-fb`,
  matching the shape of
  [`tty/pl011-tty/Makefile`](../../../sources/phoenix-rtos-devices/tty/pl011-tty/Makefile).
- `graphics/rpi4-vc6-fb/vc6-fb.c` — server main, port creation, devctl
  dispatch, `/dev/fb0` registration, mmap fast path.
- `graphics/rpi4-vc6-fb/vc6-mbox.c` — property-tag transport: physical
  buffer alloc, tag list builder, single-shot send, response validation.
  Cribbed from plo `video_framebufferInit()` and pcie
  `bcm2711NotifyXhciReset()` and refactored into a reusable API.
- `graphics/rpi4-vc6-fb/vc6-mbox.h` — public API for the transport.
- `graphics/rpi4-vc6-fb/vc6-fb.h` — internal types: `vc6_fb_t`, mode
  descriptor, pan state.
- `graphics/rpi4-vc6-fb/vc6-modeset.c` — high-level mode-set: build the
  six-tag chain (`SET_PHYS_WH`, `SET_VIRT_WH`, `SET_VIRT_OFFSET`,
  `SET_DEPTH`, `SET_PIXEL_ORDER`, `ALLOCATE_BUFFER`, `GET_PITCH`), verify
  pixel order post-allocate (NetBSD lesson, brief section 2).
- `graphics/rpi4-vc6-fb/vc6-hvs.c` (Tier 2 only) — DLIST builder, plane
  programming. Empty until Phase 5.
- `graphics/rpi4-vc6-fb/vc6-pv.c` (Tier 2 only) — Pixel Valve / CRTC.
- `graphics/rpi4-vc6-fb/vc6-hdmi.c` (Tier 2 only) — HDMI0 PHY+PLL.
- `tty/pl011-tty/pl011_fbcon.c` and `pl011_fbcon.h` (refactor) — extract
  the existing `pl011_fbcon_*` block (lines ~87–445 of `pl011-tty.c`) into
  its own translation unit. Pure mechanical move; same symbols, same
  caller. Phase 1 deliverable.
- `include/phoenix/fb.h` (new public header) — devctl numbers and structs
  shared between server and clients.

Modified files:

- `phoenix-rtos-devices/_targets/Makefile.aarch64a72-generic` — add
  `rpi4-vc6-fb` to `DEFAULT_COMPONENTS`.
- `phoenix-rtos-devices/tty/pl011-tty/Makefile` — add `pl011_fbcon.c` to
  `LOCAL_SRCS` once Phase 1 splits the file.
- `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml` —
  add `app -x rpi4-vc6-fb ddr ddr` between the `bind` line and `pcie`.
- `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project` —
  no change needed (existing fastlane logic picks up new components from
  the targets Makefile); only touch if the new server has special staging
  needs.

Files explicitly **not** modified:

- `sources/plo/hal/aarch64/generic/video.c` — keeps its own copy of the
  mailbox code. plo runs without dynamic memory and cannot link against a
  userspace lib; copy-paste asymmetry is acceptable here.
- `sources/phoenix-rtos-kernel/**` — no kernel changes.

## 4. Public ABI exposed by `/dev/fb0`

devctl numbers chosen to match Linux fbdev so portable test programs and a
future Mesa shim "just work":

- `FBIOGET_VSCREENINFO` (0x4600) — read `struct fb_var_screeninfo`
  (xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel,
  red/green/blue/transp bitfields, pixclock, timings).
- `FBIOPUT_VSCREENINFO` (0x4601) — request a mode change. Driver triggers
  the property-tag chain, validates response, returns the **applied** mode
  in the same struct (firmware may round/cap, NetBSD lesson).
- `FBIOGET_FSCREENINFO` (0x4602) — read `struct fb_fix_screeninfo`
  (smem_start = FB physical, smem_len, line_length = pitch, type, visual).
- `FBIOPAN_DISPLAY` (0x4606) — set virtual offset (`SET_VIRT_OFFSET`,
  property tag 0x48009) for double-buffering.
- `FBIOBLANK` (0x4611) — power blank/unblank via `SET_DISPLAY_BLANKING`
  (tag 0x40002, treat the firmware's silence as a soft no-op for now).
- `FBIO_WAITFORVSYNC` (0x40044620) — Tier 2 only; returns `-ENOTSUP` in
  Tier 1.

The matching mmap path: client opens `/dev/fb0`, calls
`FBIOGET_FSCREENINFO`, then `mmap(NULL, smem_len, ..., fd, 0)`. Internally
the server resolves that to the same MAP_PHYSMEM mapping that pl011-tty
uses today. Once cache-coherency lands the flag becomes `MAP_SHARED` only.

`struct fb_var_screeninfo` and `struct fb_fix_screeninfo` are defined in
the new `include/phoenix/fb.h` with field ordering and types matching
`linux/fb.h`. We do not include the Linux header (license).

## 5. Function signatures and key data structures

Mailbox transport (`vc6-mbox.h`):

```c
typedef struct vc6_mbox vc6_mbox_t;

int  vc6_mbox_init(vc6_mbox_t *m, addr_t mmio_paddr);
void vc6_mbox_fini(vc6_mbox_t *m);

/* Single-shot property-tag exchange.
 *  - buf: pointer to a zeroed, 16-byte-aligned message buffer obtained
 *    from a MAP_UNCACHED|MAP_CONTIGUOUS mmap; caller fills tags.
 *  - len: total bytes (must equal buf[0] when caller built it).
 *  - channel: typically RPI_MBOX_PROP_CHANNEL (8).
 *  Returns 0 on response==SUCCESS, -EIO otherwise. */
int  vc6_mbox_send(vc6_mbox_t *m, uint32_t channel, void *buf, size_t len);
```

Property-tag helpers (`vc6-modeset.c`):

```c
typedef struct {
    uint32_t width, height;
    uint32_t bpp;
    uint32_t pixel_order;   /* 0=BGR, 1=RGB; verify post-allocate */
    uint32_t virt_width, virt_height;
    uint32_t xoffset, yoffset;
} vc6_mode_t;

typedef struct {
    addr_t   paddr;     /* GPU returned base, masked to 0x3fffffff       */
    size_t   size;      /* allocated bytes                                */
    uint32_t pitch;     /* GET_PITCH response, MAY differ from w*bpp/8    */
    uint32_t pixel_order_actual;
} vc6_fb_layout_t;

int vc6_modeset_apply(vc6_mbox_t *m, const vc6_mode_t *want,
                       vc6_fb_layout_t *out);
int vc6_modeset_pan(vc6_mbox_t *m, uint32_t xoffset, uint32_t yoffset);
```

Server framebuffer object (`vc6-fb.h`):

```c
typedef struct {
    oid_t          oid;          /* /dev/fb0                  */
    uint32_t       port;
    vc6_mbox_t     mbox;
    vc6_mode_t     mode;
    vc6_fb_layout_t layout;
    void          *fb_user_va;   /* mmap result for clear etc */
    handle_t       lock;
} vc6_fb_t;
```

Devctl handlers (`vc6-fb.c`): one function per fbdev op, each takes a
`msg_t *` and returns Phoenix errno. Single dispatch table keyed on the
devctl number defined in section 4. mmap forwarded by the kernel to the
underlying physical region; the server only registers the size/paddr.

## 6. Phased delivery

### Phase 1 — fbcon library extraction (mechanical refactor)

- Move `pl011_fbcon_*` from `pl011-tty.c` into new `pl011_fbcon.{c,h}` in
  the same directory. No symbol renames, no behavior changes.
- Move the property-mailbox snippet from `pcie.c` into a new
  `graphics/rpi4-vc6-fb/vc6-mbox.{c,h}` and have pcie depend on it. Keep
  the existing call site identical from pcie's perspective.
- **Success:** existing rpi4b cycle (`./scripts/rebuild-rpi4b-fast.sh` →
  `./scripts/capture-rpi4b-uart.sh` →
  `python3 scripts/summarize-rpi4b-uart-log.py <log>`) reaches the same
  `fbcon: ok` UART line and the same on-HDMI banner as before.
- **Risk:** none meaningful.

### Phase 2 — `rpi4-vc6-fb` server, static mode (Tier 1 first half)

- New server registers `/dev/fb0`, **adopts** the existing plo-allocated
  framebuffer via the syspage `graphmode` (does not allocate a new one).
- Implements `FBIOGET_VSCREENINFO`, `FBIOGET_FSCREENINFO`, and mmap.
  Refuses `FBIOPUT_VSCREENINFO` until Phase 3.
- A small test program `psh`-loadable that mmaps `/dev/fb0` and writes a
  diagonal gradient — primary verification artifact.
- **Success:** UART log shows `rpi4-vc6-fb: registered /dev/fb0
  width=1024 height=768 pitch=4096 paddr=0x...`. Test program leaves a
  visible gradient on HDMI without disturbing the fbcon (they paint the
  same buffer; last writer wins, intentional).
- **Depends on:** Phase 1 + the **TD-04 cache-coherency fix** (current
  step). Without caches the gradient renders at fbcon speed (>10 minutes
  for full-screen) and the test loop is unusable. Plan ordering ties to
  [`tracking/current-step.md`](../../tracking/current-step.md).

### Phase 3 — dynamic mode set (Tier 1 second half)

- Implement `FBIOPUT_VSCREENINFO` via `vc6_modeset_apply`.
- Implement `FBIOPAN_DISPLAY` via `vc6_modeset_pan`.
- Pixel-order verification post-allocate (NetBSD lesson, brief section 2).
- **Success:** test program switches between 1024x768x32, 1280x720x32, and
  1920x1080x16 in a loop; each change is visible on HDMI; UART logs the
  applied mode. `FBIOPAN_DISPLAY` lets the test do tear-checked double
  buffer flips.
- **Depends on:** Phase 2 has a stable client (so we have something to
  measure mode change against).

### Phase 4 — dual-display via `SET_DISPLAY_NUM` (Tier 1 cap)

- Add `SET_DISPLAY_NUM` (tag 0x00040013) before each modeset chain when
  the firmware advertises >1 display via `GET_NUM_DISPLAYS` (0x00040014),
  cribbed from Circle's pattern.
- Spawn a second `/dev/fb1` if `num_displays >= 2`.
- **Success:** plug an HDMI cable into HDMI1; `/dev/fb1` appears; gradient
  test runs against either device.
- **Depends on:** Pi 4 firmware ≥ Bullseye-era release (the SET_DISPLAY_NUM
  tag is missing in older firmware). Document the minimum
  `start4.elf` hash in the relevant `manifests/*.md`.

### Phase 5 — direct HVS+PV+HDMI0 (Tier 2)

- Stub MMIO mappings for HVS (`0xfe400000`), Pixel Valve, HDMI0
  (`0xfe902000`) and HDMI1 (`0xfe905000`) per the brief.
- Implement minimum CRTC: one HVS DLIST entry pointing at the same FB the
  property mailbox produced; one PV configured for the same timings;
  HDMI0 PHY brought up at 1080p60.
- Keep the mailbox path as a compile-time alternative; bake the choice
  behind a `vc6-fb -m {mailbox|direct}` cmdline flag.
- **Success:** HDMI shows the same gradient with `firmware_kms=0` (i.e.
  with the firmware **not** owning HDMI). Confirmed by reading PV
  status registers from a follow-up devctl probe.
- **Risk** (high): HDMI clock complex (PLLH + BVB + HSM) is undocumented
  outside the GPL `drm/vc4` tree; license risk of cribbing is real. See
  open question 10.5.
- **Depends on:** all prior phases plus a clear answer on the GPL question
  and a working DTB-ish way to reach the clock-controller register block
  from userspace. May not be done in-house at all.

## 7. Test strategy

- **Unit tests (host).** `vc6-modeset.c` is mostly table-driven tag
  serialisation. A unit test under `phoenix-rtos-devices/tests/` (one
  exists for other drivers — check before adding) feeds known
  `vc6_mode_t` inputs and asserts the produced 35-word buffer matches the
  byte-for-byte recipe in
  [`sources/plo/hal/aarch64/generic/video.c`](../../../sources/plo/hal/aarch64/generic/video.c)
  lines 131–175.
- **Hardware tests.** Reuse the existing rpi4b cycle. Add a
  `scripts/test-rpi4-vc6-fb.sh` wrapper that boots the image, runs the
  gradient test, captures `/dev/fb0` over UART (CRC of a known buffer
  read back via mmap), and asserts.
- **Automation hook.** Add a row to `tracking/current-step.md` per phase;
  generate a manifest with
  `./scripts/snapshot-integration-state.sh` after each green cycle so that
  `./scripts/restore-integration-state.sh` can roll back per-phase.
- **No regression.** Tier 0 fbcon must remain functional through every
  phase; the gradient test asserts the fbcon banner is still visible
  before the test starts (we paint *after* the banner, then read it back).

## 8. Inter-dependencies

- **Phase 1** depends only on the already-completed Stage 4 phase 1
  (HDMI text fbcon present in `pl011-tty.c`). No external blockers.
- **Phase 2** depends on **Stage 1 cache-enable** (TD-04 cache-coherency
  fix, current step in `tracking/current-step.md`). Without caches the
  Tier 1 gradient runs at fbcon speed, making the test loop infeasible.
- **Phase 3** depends on Phase 2 having a stable userspace consumer to
  exercise mode changes against; without it we cannot tell whether a
  modeset was applied or just discarded by the firmware.
- **Phase 4** depends on Pi 4 firmware that supports
  `SET_DISPLAY_NUM`/`GET_NUM_DISPLAYS` (Bullseye-era VPU firmware or
  later); record the minimum `start4.elf` SHA in the manifest.
- **Phase 5** depends on (a) all prior phases, (b) a resolution to open
  question 10.5 (license audit of any HVS/HDMI knowledge transferred
  from `drivers/gpu/drm/vc4`), and (c) Phoenix DTB consumption of
  clock-controller and interrupt-controller nodes — currently minimal
  per brief section 5 Tier 2.

## 9. Effort estimate (developer-days, 1.0 dev)

- Phase 1 (fbcon + mbox extraction): **1–2 days**. Mechanical move; the
  fbcon block carries TD-15 markers that must follow per `CLAUDE.md`.
- Phase 2 (server, mmap, static mode): **3–5 days**. Risk: mmap+devctl
  glue is new for this repo; budget for the first message-loop bug.
- Phase 3 (dynamic mode set + pan): **2–4 days**. Risk: NetBSD-style
  `set_pixel_order` non-conformance — verify post-allocate.
- Phase 4 (dual-display): **1–2 days**. Depends on firmware version;
  needs a pure-HDMI1 test cable.
- Phase 5 (direct scanout): **20–40 days** if attempted, with non-trivial
  chance of being abandoned ("multiple person-months" per brief).

Tier-1 total (Phases 1–4): **~7–13 dev-days.** Tier-2 (Phase 5) is a
separate green-light gate.

## 10. Open questions for the human integrator

1. **Tier-2 horizon.** Is mailbox-driven KMS sufficient for the planned
   Phoenix Pi 4 demo, or is direct HVS scanout actually required? The
   brief flags this as the first open question (section 7.1). Tier-2
   slip would push the project sideways for months.
2. **CMA strategy.** Phoenix has no movable-page CMA. The plo path
   carves a static reservation today (`PLO_RPI_FB_*` macros in
   [`board_config.h`](../../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h)).
   Phase 3 mode-set may need to grow this on the fly — do we keep static
   carve-out and refuse modes larger than it, or build a runtime
   contiguous-page allocator?
3. **DTB depth.** Tier 2 needs clock-controller and interrupt-controller
   nodes parsed at boot. Phoenix currently passes the DTB through plo as
   an opaque blob. Who owns DTB consumption in userspace?
4. **Default mode.** Should `/dev/fb0` come up at the plo-set 1024x768x32,
   or should the server immediately probe EDID via tag 0x00030020 and
   pick the monitor's preferred mode? The latter is the Linux default
   but breaks deterministic boot output for CI.
5. **License audit for Tier 2.** Any direct HDMI/HVS knowledge originates
   in GPL `drivers/gpu/drm/vc4`. The non-Linux brief (section 11
   Synthesis) flags this as a hard blocker. Confirm with counsel before
   Phase 5 even starts.
6. **Pi 5 future.** BCM2712 redesigned HDMI again (brief section 11). Any
   Tier-2 code is Pi-4-only and adds dead weight on Pi 5. Strategic call:
   accept the throwaway, or stay on Tier 1 forever?
7. **Mesa as a North Star.** Should we shape Tier-1 helpers to look like
   DRM ioctls so Mesa could one day drop in (brief section 7.5), or stay
   Phoenix-shaped? Affects the public ABI in section 4 of this plan.

---

Cross-references:
[`AGENTS.md`](../../AGENTS.md),
[`docs/status.md`](../status.md),
[`tracking/current-step.md`](../../tracking/current-step.md),
[`docs/code-quality-and-upstreaming.md`](../code-quality-and-upstreaming.md),
[`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`](../TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md).
