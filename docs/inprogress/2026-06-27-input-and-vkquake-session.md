# 2026-06-27 — interactive input (#30) + vkQuake textures (#29) + USB #121 (#33)

Autonomous/attended-mixed session. Started with the user present (xterm netboot test) and
continued unattended. HW boots run serially by the orchestrator; a subagent
(`af9951fdf5006ccb8`) does the host-side vkQuake/V3DV winsys work.

## #30 — X11 keyboard input: FIXED, HW-confirmed up to the human hop

Two distinct bugs, both fixed:

1. **DDX return-type bug (coord `aef9f9f`).** `fbdevKeyboardInit`/`fbdevKeyboardEnable` were
   declared `Bool` and returned `TRUE` (==1), but kdrive's `KdKeyboardProc` (hw/kdrive/src/
   kinput.c) checks `!= Success` and `Success==0`. So `TRUE != Success` → `DEVICE_INIT` rejected
   → `Enable` never ran → kbd0 never opened. (The mouse was unaffected: `KdPointerDriver`
   already returns `Status`.) The decisive symptom was the *absence* of any `[fbdev]` keyboard
   line in the old logs. Fix: declare both `static Status`, return `Success`.

2. **poll()-readiness input bug (coord `3dc26a9`).** After (1), the DDX opened kbd0(fd=7)+
   mouse0(fd=8) but nothing reacted and no cursor showed. Root cause: the read callbacks were
   registered via `InputThreadRegisterDev`, whose only wake sources are `poll()`-readiness (a
   dedicated input thread or `SetNotifyFd` in the main loop). **Phoenix `poll()` does not reliably
   wake on HID fd-readiness** — the very reason the DDX screen flush was already timer-driven
   (`fbdevFlushTimerCb` fires on poll() *timeout*). So the read callbacks never fired.
   Fix: drain kbd0+mouse0 from a 16 ms `OsTimer` (`fbdevInputTimerCb`) on the main dispatch
   thread; the Enable funcs store the device handles and no longer call `InputThreadRegisterDev`,
   so the timer is the sole reader (no input-thread race, no `input_lock`). Bounded non-blocking
   drain empties the FIFO each tick → ≤1-tick latency, no loss.

HW-confirmed (kbd0-timerdrain boot): kbd0+mouse0 open, twm+xterm+BusyBox shell render
**unregressed**, no dispatch wedge, no faults. Self-diagnosing markers added so the morning
physical test is unambiguous:
- `[fbdev] kbd/mouse FIRST drain: N bytes` → bytes reached the device fd.
- `[fbdev] kbd/mouse FIRST event enqueued` → event reached dix.
0 bytes ⇒ drain not reaching device; bytes-but-no-reaction ⇒ downstream keycode/focus.

**Remaining (human):** one physical keypress into xterm + a mouse wiggle. The kdrive software
cursor (`softCursor=TRUE`) composites into the shadow buffer, so it should appear on first
pointer motion through this path.

**Morning recipe:** power on (netboot server is up), at psh type `/nfstest/bin/startx term`,
then type into the xterm and move the mouse.

## #29 — vkQuake textures: TFU works, textures upload, tiling issue remains

- `DRM_V3D_SUBMIT_TFU` implemented in `tools/v3d-driver-port/v3d_phoenix_winsys.c` (coord
  `5d663cc`); mesa `DISABLE_TFU` force reverted (`56c6e6a`). Present-hang GONE, multi-frame
  present, TFU ioctl executes/declines correctly.
- **Black-texture root cause (3-stage probe):** vkQuake passed `VkBufferImageCopy.imageExtent =
  0x1x1` for *every* texture — the build computes `mipwidth=0` despite `glt=2x2` (image itself is
  created correctly, e.g. `IMAGE create=2x2x1`). Fixed at point-of-use (vkquake `f4d923e`): re-
  derive `imageExtent` from `glt->{width,height}` at the copy call. (The *why* of `mipwidth=0` —
  a clobber in the gl_texmgr barrier block ~1215 — is deferred; the override holds.)
- **HW after the fix:** all textures get correct extents (2×2…640×512), all 18 copies take
  `path=TFU`, and the conchars rects show **sampled content** (were pure black).
- **New open issue:** sampled textures are dark + horizontally **striped**, whitetexture not
  white — classic TFU-destination **tiling/format mismatch** (TFU output tiling vs TMU sampler
  layout). Agent investigating `ioc_submit_tfu` OTYPE/tiling vs the VkImage slice tiling.

## #33 — USB #121: mechanism root-caused, self-localizing guard committed (usb `53b3db2`)

The intermittent boot Data Abort in the `usb` daemon fired this session. addr2line:
`usb_allocFrom` mem.c:238, reached via the `buf->next` recursion (mem.c:273). Register bytes
decode to ASCII: `buf->next`(offset 0) = `" Phoenix"`, `buf->head`(offset 32) = `"..Raspb"` —
readable **device-name string** data, not DMA garbage. Mechanism = an **adjacent-buffer forward
overflow** (pool buffers are USB_BUF_SIZE-aligned; an alloc near the tail of buffer N overflowed
a string descriptor into buffer N+1's header). `usb_chunkSane` guarded `buf->head` + the chunk
walk but not `buf->next` → still crashed.

Fix (additive, corruption-path only): `usb_bufSane()` validates `buf->next` before the recursion
(smashed link → bounded leak + report, no crash, and USB can survive to enumerate); an alloc-log
ring (addr,size,**caller PC**) dumped by the report flags the allocation abutting the smashed
buffer = the overflow writer → `addr2line prog/usb` names it. Next recurrence self-localizes.
Intermittent (~1 boot in 4); clean boots enumerate kbd0+mouse0.

## State for the morning
- Bundled netboot image = **boot-to-psh** (no Quake autostart), ready for the #30 keyboard test.
- NFS-staged `Xphoenix` carries both DDX fixes (md5 changes per build).
- Netboot server (DHCP/TFTP) + NFS export are UP.
- vkQuake agent iterating on the TFU tiling fix; orchestrator will bundle+boot when it lands,
  then restore boot-to-psh.

## Commits
- coord: `aef9f9f` (DDX kbd return-type), `3dc26a9` (DDX timer-drain input), `5d663cc` (TFU),
  vkQuake-texture/probe commits in the GPU-libs flow.
- usb: `53b3db2` (#121 buf->next guard + alloc-log).
- mesa/vkquake: TFU revert + extent fix + probes (in the tracked patches).
