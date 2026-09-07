## Device drivers, USB, networking, filesystems

This is the largest and most self-contained body of work in the fork: roughly 44 000 added lines across
`phoenix-rtos-devices` (400 commits, +36 863), `phoenix-rtos-lwip` (+3 992), `phoenix-rtos-filesystems`
(+2 939) and `phoenix-rtos-usb` (+478/-67). Almost all of `phoenix-rtos-devices` is *new files*: upstream
has no `audio/`, `bt/`, `gpu/`, `misc/`, `video/` or `wifi/` top-level directory at all, and `usb/` holds
only `ehci`, `cdc-demo` and `libusbclient` — so the Pi 4's xHCI host-controller driver is a new HCD for
Phoenix rather than a port of an existing one. `phoenix-rtos-usb` is the mirror image: no new source files,
37 commits of pure framework work on the shared DMA allocator, hub and enumeration paths. Alongside the
board work there is a new userspace NFSv4 client filesystem (`filesystems/nfs/`, 2 700 lines) that lets a
Phoenix box boot with `/` on an NFS export, and a BCM2711 gigabit Ethernet driver for lwIP.
Note on scope: the BCM2711 HEVC (`rpivid`) hardware video decoder lives outside these four repos
(`tools/hevc-decode/` in the coordination repo) and is not covered here.

### 1. New Pi 4 on-board device drivers

All are userspace servers in the standard Phoenix idiom (`mmap(MAP_PHYSMEM)` + `portCreate` +
`create_dev` + message loop). ★ marks code a maintainer on a non-Pi target would plausibly want.

| device | driver path | exposes | state |
|---|---|---|---|
| ★ VL805 xHCI USB 3.0 host controller + BCM2711 PCIe bridge | `devices/usb/xhci/` (`xhci.c`, `bcm2711-pcie.c`) | `libusbxhci` HCD behind the `usb` daemon | Working: full ring/slot/endpoint model, control and interrupt-IN transfers (no bulk/isochronous path in the driver), root-hub and behind-hub addressing, error recovery. Reliable enumeration after the two-step-BSR fix (§3). |
| V3D 4.2 GPU (Mesa gallium + V3DV) | `devices/gpu/rpi4-v3d/` | in-process winsys (`mesa/v3d_phoenix_winsys.c`) or `/dev/v3d-srv` + `libv3d-client` | OpenGL, OpenGL ES 3.1 and Vulkan render on HDMI. No DRM, no kernel GPU driver, no Vulkan WSI. See note. |
| VideoCore property mailbox | `devices/misc/rpi4-vcmbox/` | `/dev/vcmbox` + `libvcmbox` | Complete and the mandatory path — see note. |
| HDMI framebuffer | `devices/video/rpi4-fb/` | `/dev/fb0` (read/write + `RPI4FB_GETMODE`) | Byte read/write and geometry only. Deliberately **no** `FBIOGET_*` veneer and **no** `mmap(fd,0)` of the surface (needs new kernel VM work); no arbitration against the boot console. |
| ★ HDMI framebuffer console + PL011 UART tty | `devices/tty/pl011-tty/` (+ vendored `teken/`) | `/dev/tty0`, `/dev/console`, `FBCONSETMODE` | Full VT100/xterm console driven by FreeBSD `teken` (BSD-2). GNU nano and mc render correctly. |
| BCM2711 EMMC2 SD card | `devices/storage/bcm2711-emmc/` | `/dev/mmcblk0`, ext2 root | Boots from SD. UHS-I DDR50, 128 KiB multi-block transfers, DMA **reads** only — writes stay on PIO (§4). |
| BCM43455 SDIO WiFi | `devices/wifi/rpi4-wifi/` (4 805 lines) + `lwip/drivers/wifi43455.c` | `/dev/wifi` (text scan/ctl), `/dev/wifidata` (raw frames), `wifi` CLI, lwIP netif `wl2` | Firmware download, WPA2 join via the firmware supplicant, full-MTU data path, DHCP lease over the air. Throughput is poll-bound (§4). |
| BCM43455 Bluetooth | `devices/bt/rpi4-hci/` | `/dev/hci0` (raw H4 HCI), `btctl` | Controller reset, patch-RAM upload, `BD_ADDR`, HCI inquiry. Raw HCI byte stream only — no host stack (L2CAP/GAP) above it. |
| PWM audio (3.5 mm jack) | `devices/audio/rpi4-audio/` | `/dev/audio0` (s16 PCM write) | Self-chained DMA ring, DREQ-paced, with playback-rate backpressure and PIO fallback. No `snd` backend; audible sign-off is attended. |
| SoC thermal / throttle | `devices/sensors/rpi4-thermal/` | `/dev/thermal`, `/dev/throttled` | Complete for what the SoC allows: telemetry only, the VideoCore firmware owns the trip point. |
| Hardware RNG (iproc RNG200) | `devices/misc/rpi4-hwrng/` | `/dev/hwrng` | Complete; backs `/dev/urandom` and `getentropy`. |
| GPIO | `devices/gpio/rpi4-gpio/` | `/dev/gpio` (snapshot), `RPI4GPIO_GETPIN` | **Read-only by design.** Driving outputs needs a bench rig and is deferred. |
| ★ USB HID keyboard / mouse | `devices/tty/usbkbd/`, `devices/tty/usbmouse/` | `/dev/kbd0`, `/dev/mouse0` | Cooked ASCII stream *and* a raw 8-byte HID report mode (so a game can see key-up). Hosted inside the `usb` daemon; `N_URBS=1`, so the interrupt path is lossy under fast input. |
| BCM2711 GENET v5 gigabit Ethernet | `lwip/drivers/bcm-genet.c` (2 038 lines) + `drivers/ephy.c` | lwIP netif `en1` | Working and fast (§4). Multi-slot TX, 256 unique RX buffers, optional cacheable RX/TX, TX pipelining, RX input batching. |
| BCM54213PE PHY (*extension to the existing `ephy` driver*) | `lwip/drivers/ephy.c` (+185) | — | Reads speed/duplex from the Auxiliary Status Summary (0x19) and programs RGMII RXC-RXD skew via the MISC shadow register. `INT_B` is not routed to a GIC SPI on this board, so link state is MDIO-polled (as Linux and U-Boot do). |

Support daemons, not devices: `misc/rpi4-klogd` (klog ring → `/var/log/messages`, the file half of a
DEBUG/USER logging split), `misc/rpi4-sysinfo` (boot banner + device-node inventory),
`misc/rpi4-ipcprobe` (one-shot AF_UNIX / `getrandom` readiness probe, not a default component).

**The mailbox server is load-bearing.** The BCM2711 has one VideoCore property-mailbox FIFO at
`0xfe00b880` with *no hardware arbitration*, and it is the only route to SoC temperature, the board MAC,
throttle state and power/clock for V3D, HVS and USB. At boot, thermal, genet, the USB daemon's VL805
bring-up, the SDIO driver and V3D power-on all want it; two concurrent readers pop and discard each
other's response word. `rpi4-vcmbox` owns the FIFO and serialises every caller — a Phoenix server handles
one message at a time, so serialisation is free — reusing a single early-allocated low-PA uncached bounce
buffer so it stays VideoCore-addressable on 4/8 GB boards. This "one server owns the unarbitrated
peripheral" pattern is reused verbatim by the V3D server, and is the generalisable idea here.

**V3D, honestly.** Mesa's `v3d` gallium driver and `v3dv` (Vulkan) are cross-compiled against Phoenix and
driven through a hand-written winsys that implements `DRM_IOCTL_V3D_*` directly on the hardware — BO =
`mmap` + `va2pa`, GPU VA through the V3D MMU's flat page table, `SUBMIT_CL` = CT0/CT1 QBA/QEA plus
`FLDONE`/`FRDONE` and an L2T flush. There is no DRM layer and no kernel GPU driver; submits are
synchronous, so `drmSyncobj*` is stubbed. The Mesa archives are built by standalone Python scripts
(`mesa/build-{v3d,gl,v3dv}-phoenix.py`) that re-emit the host Mesa build's `compile_commands.json` with
the Phoenix toolchain — they are *not* wired into the framework Makefiles, which cannot run meson/ninja.
`/dev/v3d-srv` (`rpi4-v3d.c` + `v3d_gpu.c`) is the multi-client answer: it takes sole ownership of the
GPU's single MMU page-table base, submit registers and power domain, and clients route MMIO-touching
ioctls to it. It is HW-proven and built as a first-class component, but it is **not auto-launched** —
taking exclusive ownership conflicts with the in-process-winsys GPU apps that currently ship, so the
shipping path today is one app at a time with the winsys linked in-process.

### 2. Changes to existing Phoenix drivers and subsystems

- `devices/pcie/server/pcie.c` (+763): BCM2711 host-bridge support for the generic PCIe server — an
  indexed config-space backend, root/downstream bridge window shaping, link-state gating, and the
  `NOTIFY_XHCI_RESET` mailbox call the firmware needs before VL805 is enabled. **Caveat:** the Pi 4 does
  not use this daemon. Bridge bring-up was folded into the `usb` daemon via
  `libusbxhci`'s `bcm2711_pcie_initVL805()`, and `pcie` is deliberately absent from the a72 target. The
  code stands as the separate-daemon pattern for a future a72 board.
- ★ `devices/tty/libtty/libtty.c`: `FIONREAD` implemented (was `-EINVAL`) — see §3.
- `devices/libklog/libklog.c`: reworked so the log drain can be owned by a tty driver that
  attaches directly to the kernel log port `{0,0}` instead of going through a `/dev/kmsg` devfs node
  (nothing registers one on this board).
- `filesystems/dummyfs/srv.c`: srv-init stabilisation and cleanups; `dummyfs` is the pre-takeover RAM `/`.
- ★ `filesystems/ext2/`: fs-global operation serialisation — see §3.
- ★ `lwip/port/`, `lwip/include/arch/`: `sys_mbox_trypost_coalesce`, `dmammap_cached`, the netif-driver
  list exposed for out-of-tree netifs, `/dev/ipstats`, socket-layer fixes (§3), and lwIP checksum
  algorithm 3 as the default (word-at-a-time; a measurable win on the A72, no-op where overridden).
- ★ `usb/` (all 37 commits): the shared USB framework — DMA allocator, hub, enumeration. See §3 and §5.

### 3. ★ General bug fixes

These are defects in shared Phoenix code that would bite any target.

**USB / xHCI**

- ★ `usb/mem.c` `12c4fe8` — stale cache lines in recycled DMA pages. `usb_allocUncached` maps
  `MAP_UNCACHED` physical pages a prior *cached* owner had dirtied; those stale dirty lines wrote back
  over the uncached pool *after* it was initialised, smashing the free-list with other processes' boot
  banners and failing USB bring-up ~1 boot in 8. Fix: one `dc civac` over the freshly mapped region
  (EL0-legal via `SCTLR_EL1.UCI`; the A72's PIPT D-cache acts on the physical line regardless of this
  mapping's attributes). 0 events in ~15 cold boots.
- ★ `usb/dev.c` `7259b26` — a control transfer's buffer overflowed into the adjacent DMA-pool
  free-chunk header.
- ★ `xhci.c` `255ce87` — interrupt-IN delivered exactly one report, ever. The submit path `memset` the
  ring and rewrote slot 0 with a *fixed* producer cycle bit on every submit, never advancing an enqueue
  pointer; after the controller followed the Link TRB and toggled its dequeue cycle, every resubmit
  looked un-owned. Replaced with a real circular producer. This is why a USB keyboard registered only
  the first keypress.
- ★ `xhci.c` `53383d1` — single-step (`BSR=0`) Address Device intermittently wedges the VL805's command
  processor: EnableSlot completed, then Address Device produced *no* completion event at all (a
  non-responsive device yields an *error* completion, never a missing one). Linux issues `BSR=1`
  (setup-context-only) then `BSR=0`; doing the same took Address-Device timeouts from ~3 of 4 cold boots
  to 0 in ~15.
- ★ `xhci.c` `e371967` — no error recovery existed at all: Disable Slot, Reset Endpoint and Set TR
  Dequeue Pointer appeared nowhere, and pipe destroy freed only software state. So the framework's
  enumeration retry re-drove a slot the hardware still held halted (one transient Split Transaction
  Error → Context State Error on both retries), costing roughly one boot in three its keyboard and
  mouse. Now Disable Slot is issued when the default control endpoint (DCI 1) is torn down.
- ★ `xhci.c` `4576e72` — PORTSC RW1C over-clear: the four `C_*` change-bit clears wrote back the *other*
  change bits still set, so a sibling port event that raced the write was silently cleared and lost.
  Same commit: the shared per-controller `inputCtx` scratch buffer was reallocated on every
  `allocSlotSpace`, leaking one buffer per device behind a hub.
- ★ `usb/hub.c` `47eede9` — no reset-recovery delay. Enumeration ran EnableSlot/Address Device the
  instant port reset completed; USB 2.0 §7.1.7.5 mandates TRSTRCY ≥ 10 ms before a device accepts
  SET_ADDRESS (Linux waits ~50 ms). 50 ms added.
- ★ `usb/hub.c` `03bd903`, `3c7fdb2` — a device that failed enumeration was retried forever, because
  each failed attempt reset the port and the port-status change re-triggered enumeration; the resulting
  transfer-timeout flood rebooted the board (~85 resets observed). Per-port failure counting with a
  give-up limit fixes that, and `3c7fdb2` fixes the follow-on: the counter was only cleared when a
  *tracked* device disconnected, and a device that never enumerated was never tracked — so a port that
  hit the limit ignored every later replug, permanently.
- `usb/dev.c` `e0911ce` — `sprintf` of `/dev/usb-%04x-%04x-if%02d` into `char[32]`; `%02d` is a *minimum*
  width, so a corrupt interface number overflowed the stack buffer.

**tty and filesystems**

- ★ `libtty` `b247643` — `FIONREAD` fell through to `-EINVAL`, so any program asking how many input bytes
  are pending got a failed ioctl and a garbage count (readline/bash rely on it). Returns
  `fifo_count(rx_fifo)`.
- ★ `ext2` `463aec1` — the block and inode allocators mutate fs-global state (on-disk bitmaps, the
  in-memory group-descriptor table, superblock free counts) with a non-atomic
  read-bitmap/toggle/write-bitmap/update-counts sequence, and *nothing* protected it: `obj->lock` and
  `objs->lock` cover objects, not the filesystem. Under a multi-worker server two threads interleave and
  corrupt the bitmaps — a deterministic Data Abort in `ext2_block_destroyone` under concurrent-write
  stress. Fixed with one per-fs mutex taken at the `libext2_*` entry points, which are never re-entrant,
  so it is trivially the outermost lock (`fs->lock > {obj->lock, objs->lock} > storage`).
- ★ `nfs` `fc2f62b` — an upstream **libnfs 6.0.2** bug worth knowing about: `readlink_cb` records
  `-ENAMETOOLONG` for an over-long target, then `cb_data_is_finished()` overwrites `status` with the RPC
  success code, so `nfs_readlink` returns phantom success with the buffer *unmodified*. Any
  `readlink`/`realpath` of a symlink longer than the caller's buffer resolved to stale garbage. Fixed by
  staging through a `PATH_MAX` buffer and applying POSIX truncation locally.
- ★ `lwip/port/sockets.c` `7428162` — three independent socket-layer defects: `FIONBIO` read the flag
  before `lwip_ioctl` could set it (so the socket stayed blocking), `getnameinfo` could write past the
  caller's buffer when only the host *or* only the service was requested, and `getifaddrs` did not
  report all interfaces.
- The ~20-commit V3D "corrupt control list" arc (`6502f67`, `1e0d1c2`, `6c1e321` and the diagnostics
  around them) converged on two lifetime bugs in the winsys' own BO table rather than anything in the
  GPU: BO handles and closed BOs' CPU addresses were being recycled, so a stale handle resolved to a
  live BO and a new mapping landed on a live one. Fix: never recycle handles or mappings, and invalidate
  a BO's PTEs on close.

### 4. Performance

| what | before → after | how |
|---|---|---|
| GENET / NFS throughput | NFS read **29.9 MB/s**, write **19.7 MB/s** on a Pi 4B | 256 unique RX buffers (aliasing was corrupting RX), multi-slot TX, TX pipelining, RX input batching, cacheable TX mapping via `dmammap_cached`, `recvmbox` coalescing |
| SD card read | single-block → **~16×**; then DDR50 20.1 → **33.4 MB/s**; then 128 KiB transfers → **38.3 MB/s** | CMD18/CMD25 multi-block with Auto-CMD23, SDMA read path, UHS-I DDR50 (ACMD41 S18R → CMD11 voltage switch → CMD6 → HC2), `SDCARD_MAX_TRANSFER` and the libcache sector raised to 128 KiB |
| SD card write | ~11× multi-block, ~17.4 MB/s at the DDR50 clock | stays on **PIO**: SDMA writes silently corrupted the first block 1–2 runs in 10 on this Arasan/BCM2711 controller (reads are exact), so DMA writes are gated off deliberately |
| NFS RPC latency | **20×** overall slowdown removed | `libnfs poll_timeout` was 100 ms, and the socket `poll()` never woke on readiness — every RPC ate a 100 ms stall. Set to 1 ms. |
| NFS path resolution | d(d+1)/2+2d+1 round trips → linear in depth | `_resolve_abspath` walks a path prefix by prefix and `nfs_ops_lookup` re-`lstat`'d every component of each prefix. A 5-component `stat()` spent ~36 ms re-asking about `/usr`, `/usr/share`, … Fixed with a deliberately short (100 ms) per-node positive attribute cache, invalidated on every mutation and on NFSv4 reclaim. |
| NFS directory scan | ~38 ms *per* `readdir()` → one listing per scan | `nfs_ops_readdir` opened the directory, walked to the cookie, emitted one entry and closed — a full READDIR round trip per entry. On a 649-entry export directory one scan cost ~25 s. The snapshot is now held on the node across a sequential scan. |
| NFS per-RPC size | 32 KB → 1 MB | raises large sequential read/write ceiling |
| WiFi | RX and TX both to low single-digit MB/s | SDPCM glom de-aggregation (below) plus RX poll cadence. **Do not quote a single figure:** the fork's own `fcb4311` retracts its earlier numbers after identical code re-measured 2.6× apart. |

WiFi throughput is worth a note because the mechanism is clear even where the numbers are not. The
firmware bundles received frames into superframes on SDPCM channel 3 and the driver had no
de-aggregation, so it dropped ~18% of all inbound frames and left TCP to retransmit them; asking the
firmware to stop (`bus:rxglom = 0`) is accepted and ignored. The wire format was decoded from a hardware
dump after three guesses failed. What remains is not the radio: the idle RX poll interval sets the
ceiling (an N µs sleep caps you near one frame per N µs), and each frame costs a message round trip plus
a backplane window setup plus four PIO SDIO transfers. An event-driven read and F2 DMA are the named
next levers. Two other measured findings were kept but explicitly *disproved* as the cause: SDPCM credit
windows never closed (`blocked=0`), and byte-mode RX is 5× faster than block mode here.

### 5. Stability and robustness

- `bcm2711-emmc` runs its fs server single-threaded and with enlarged pool-thread stacks (`0982cdb`,
  `07bb181`, `1741541`) — SD-boot crashed in `ext2_obj_get` before `psh`.
- `usb/mem.c` hardening (`016f9bb`, `c0af52e`, `53b3db2`): the DMA allocator validates chunk *and*
  buffer headers and, on corruption, logs a self-localising report (the allocation abutting the smashed
  header, plus a recent-free ring) and leaks the rest of the chain instead of faulting the daemon
  mid-enumeration. `1242d39` makes duplicate transfer completions idempotent; `a4b4628` drains the
  URB finished list from `usb_init`.
- Bounded waits and truncation guards on every mailbox client (`70b5b34`, `b511550`, `432db4c`,
  `ec7f2ae`): unbounded mailbox spins and 32-bit truncation of a 64-bit physical address were the two
  recurring defect classes across these drivers.
- Isolating USB in its own daemon means a USB-side failure no longer takes the network down with it
  (`03bd903`).
- NFS resilience: bounded retry on transient RPC errors (`82b5530`, `434d1b3` — fixes intermittent
  `exec` failures), recovery from NFSv4 lease expiry `NFS4ERR_EXPIRED` (`8231627`), a stable NFSv4
  client id so a rapid reboot does not collide with its own prior state (`4b5acb4`), and takeover of `/`
  degrading to the RAM root rather than bricking the boot (`eed921c`).
- A round of leak and bounds fixes from a dedicated review pass: `hub->portEnumFails` on teardown and on
  the `hub_conf` error path, `usbkbd`/`usbmouse` insertion error paths, `rpi4-audio` partial-mmap leak
  and DMA 1 GB-straddle guard, `rpi4-fb` rejecting (not truncating) a write past the surface end.

### 6. Networking (lwIP) and filesystems (NFS)

**lwIP.** Beyond the two new netifs, the port gained `sys_mbox_trypost_coalesce` (a producer merges onto
the queued tail entry instead of taking a new slot, which is what keeps a gigabit RX burst from
overrunning the recvmbox; coalescing only runs on a non-empty queue, so no wakeup is lost),
`dmammap_cached` for buffers the CPU copies through, the netif-driver list exposed so an out-of-tree
netif can register, `/dev/ipstats` for driver triage, an RTT ping-pong and streaming mode in `net-test`
(this is how the throughput numbers above were measured), and optional iperf. The `wifi43455` netif
notes two things a *new* netif name has to do that an in-list driver gets free: `netif_dev_init` applies
mtu/hwaddr/flags defaults only to a hardcoded list of driver names, so the driver must set them itself
including `NETIF_FLAG_UP` (without it `dhcp_start()` returns `ERR_ARG`, which showed up on hardware as
`dhcp_start: -16` right after a successful join); and since the WiFi daemon starts *after* lwIP, init
must register link-DOWN and wait for its device files rather than requiring them. It also stopped calling
`netif_set_default()` unconditionally, which had been silently pushing all off-subnet traffic over WiFi.

**NFS.** `filesystems/nfs/` is a new userspace NFSv4 client filesystem server on libnfs (`srv.c`,
`nfs_ops.c`, `nfs_node.c`, ~2 700 lines) with a node cache keyed by both path and id, a filehandle
cache with lazy close, and a "takeover" mode: the export is mounted as `/` *after* a RAM `/` has brought
up lwIP, which is what makes NFS-as-rootfs possible on a diskless board at all. Getting POSIX semantics
right on top of it took most of the work, and each fix is a real gap rather than Pi-specific:
`readdir` synthesises `.` and `..` (NFS READDIR omits them, and POSIX requires them) with
server-resolved inode numbers; `st_blocks` was 0 for every file because libnfs does not map the server's
`SPACE_USED`, so `du` and `ls -s` reported nothing; a node unlinked while open stayed bound in the
by-path table, so a later create of the same name reused it and writes went through an orphaned
filehandle while `stat` resolved the new inode — a freshly written file could `stat` as 0 bytes; and
FIFOs and device nodes now work on an NFS root, by recording the name locally and splicing the owning
server's oid onto it (`mkfifo` had returned `EIO`, because Phoenix asks the owning filesystem for an
`otDev` node carrying posixsrv's pipe oid and the code discarded it, then tried `nfs_mknod`, which
NFSv4 rejects). That limit is stated rather than hidden: such a name lives in one mount, is not listed
by a `readdir` of the export, and does not survive a remount — which matches what the object is.
`be68a10` closes out a leak audit of the directory-snapshot cache (an NFSv4 reclaim stranded a whole
listing, tens of KB per reclaim, because with the dircache disabled nothing in libnfs would ever free
it). Verification is stated in test terms throughout: over the netboot NFS root, `libc/stdio` 80 tests
0 failures, `libc/misc` 207 tests down to 2 known-clock failures, `libc/dirent` 38/0.
