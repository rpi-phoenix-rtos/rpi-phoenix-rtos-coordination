# Pool-thread / worker-thread stack-overflow audit (Task #152)

**Date:** 2026-06-08
**Type:** Read-only audit — NO code changed, NO build, NO hardware.
**Scope:** Every userspace server/daemon launched on the Pi 4 by
`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`,
plus the filesystem libs (ext2, meterfs) they run on top of.

## Background

In #120 the SD-boot crash was a **pool-thread stack overflow**. `libstorage`'s
`storage_run(nthreads, stacksz)` was called with `stacksz = 2*_PAGE_SIZE`
(8 KB); the Pi 4 ext2-over-SD handler call chain (`msgRecv` → storage handler →
ext2 → SD block I/O) overflowed it. Because `storage_run` carves all worker
stacks as **adjacent slices of one `malloc`** with **no guard page**, the
overflow silently clobbered the neighbouring worker's stack and surfaced as a
bogus ext2 `lib_listRemove` crash. Fix: `storage_run(2, 16*_PAGE_SIZE)` (64 KB)
in `bcm2711-emmc/sdstorage_srv.c:430`.

This audit asks: **which other Pi 4 userspace servers carry the same latent
risk** — small (8 KB or less) worker/pool stacks feeding a deep call chain, with
no guard page so an overflow corrupts silently rather than faulting cleanly.

## The guard-page point (applies to every row below)

None of the stacks audited here have a guard page. They are either:

- **static buffers** in the process's `.bss` (`char stack[N]`) — overflow walks
  into the adjacent global, or
- **one `malloc` carved into adjacent slices** (`storage_run`) — overflow walks
  into the next worker's stack, or
- **a per-thread `malloc`** (lwip `sys_thread`) — overflow walks the heap.

In every case an overflow is **silent adjacent-memory corruption**, not a clean
stack-guard fault. That is exactly why #120 was misdiagnosed as an ext2 bug, and
it is why a small number on a deep chain is more dangerous than the number alone
suggests: you do not get told it overflowed, you get a corrupted neighbour and a
crash somewhere unrelated.

## Audit table

| Server (plo `-x`) | Thread-creation site (file:line) | Stack size | Call-chain depth | Verdict | Recommended fix |
|---|---|---|---|---|---|
| **bcm2711-emmc / storage** | `phoenix-rtos-devices/storage/bcm2711-emmc/sdstorage_srv.c:430` `storage_run(2, 16*_PAGE_SIZE)` | **64 KB** ×2 workers + caller | **Very deep** — storage handler → ext2 → SD block I/O | **OK** (already #120-fixed) | none — this is the reference fix |
| **nfs** | `phoenix-rtos-filesystems/nfs/srv.c:367` (mount), `:372` (loop) — `NFS_STACKSZ = 16*_PAGE_SIZE` | **64 KB** each | **Very deep** — msgRecv → handler → libnfs sync API → BSD socket syscalls | **OK** (built this way tonight; lesson pre-applied) | none — confirms task's expectation |
| **usb** | `phoenix-rtos-usb/usb/usb.c:483` (`ustack`, msg thread), `:489` (`stack[]`, status threads); buffers declared `:51` `ustack[2048]`, `:53` `stack[N][2048]` | **2 KB** each (msgthr + status threads) | **Very deep** — msgRecv/status → device enumeration → class drivers (usbkbd/usbmouse hosted in-process via USB_HOSTDRV_LIBS) → DMA pool | **RISK** | raise to **16–32 KB** and re-run the kbd-attach abort bench (see hypothesis below) |
| **lwip (genet)** | lwip core: `phoenix-rtos-lwip/port/threads.c:142` `beginthreadex(... stacksize)`, stacksize from `DEFAULT_THREAD_STACKSIZE`/`TCPIP_THREAD_STACKSIZE` | **16 KB** (rpi4b `lwip/lwipopts.h:54-55` = `4*4096`) | Deep — TCP/IP stack | **OK** | none |
| **lwip — genet irqThread** | `phoenix-rtos-lwip/drivers/bcm-genet.c`; buffer `irq_stack[4096]` (`uint32_t` ⇒ **16 KB**) | **16 KB** | Moderate — IRQ → ring drain → `pbuf`/netif input | **✅ FIXED (#152, lwip 3d11426)** | none — bumped 8→16 KB (was *exactly* the 8 KB that bit #120) |
| **lwip — genet linkPollThread** | `phoenix-rtos-lwip/drivers/bcm-genet.c`; buffer `link_poll_stack[2048]` (`uint32_t` ⇒ **8 KB**) | **8 KB** | Shallow — MDIO poll loop | **✅ FIXED (#152, lwip 3d11426)** | none — bumped 4→8 KB for uniformity |
| **dummyfs** (`-x dummyfs`/`dummyfs-root`) | main msg loop runs on the **main thread** (`phoenix-rtos-filesystems/dummyfs/srv.c:278` `for(;;) msgRecv`); only extra thread is the async mount helper `:271` `dummyfs_mount_async`, buffer `:115` `mtstack[4096]` | main stack (process default); helper **4 KB** | Shallow — RAM-backed fs, no block-device recursion; mount helper is a one-shot splice | **OK** | none |
| **posixsrv** | `phoenix-rtos-posixsrv/srv.c:54,57`; buffer `:28` `stacks[4][0x1000]` | **4 KB** ×4 worker threads | Moderate — pipe/pty/event-queue handlers, no fs/DMA recursion | **OK** (watch: see note) | none now; revisit if a handler grows deep buffers |
| **pl011-tty** | `phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c:1164-1173` (klog/main/kbd/mouse/pool threads); buffers `:138-141,147` all `stack[4096]` | **4 KB** each | Shallow — libtty + UART/console; klogthr writes framebuffer | **OK** | none |
| **rpi4-thermal** | none — single-threaded `for(;;) msgRecv` on main thread (`sensors/rpi4-thermal/rpi4-thermal.c:198`) | main stack | Shallow — mailbox property read | **OK** | none |
| **rpi4-hwrng** | none — single-threaded main-thread msg loop (`misc/rpi4-hwrng/rpi4-hwrng.c:115`) | main stack | Shallow — RNG200 FIFO poll | **OK** | none |
| **rpi4-fb** | none — single-threaded main-thread msg loop (`video/rpi4-fb/rpi4-fb.c:150`) | main stack | Shallow — byte read/write to FB surface | **OK** | none |
| **rpi4-gpio** | none — single-threaded main-thread msg loop (`gpio/rpi4-gpio/rpi4-gpio.c:151`) | main stack | Shallow — register snapshot | **OK** | none |
| **nfs-smoke** | one-shot probe (no server pool) | main stack | Moderate (libnfs once) | **OK** | none |
| **psh** | interactive shell, main thread | main stack | Shallow (shell) | **OK** | none |
| **ext2 (lib)** | **no threads of its own** (`grep beginthread phoenix-rtos-filesystems/ext2/` = empty) — runs **on the storage_run pool thread** | inherits caller's `storage_run` stack | Very deep | depends on caller | see "ext2/meterfs generalization" |
| **meterfs (lib)** | **no threads of its own** (grep empty) — runs on caller's thread | inherits caller | Moderate | depends on caller | see generalization |

## ext2 / meterfs generalization (the load-bearing conclusion)

`ext2` and `meterfs` spawn **no threads of their own** — they execute entirely on
whatever thread their host storage driver hands them, i.e. the `storage_run`
pool thread. That is precisely why #120 surfaced as a bogus ext2 crash: ext2's
deep chain overflowed the **storage driver's** 8 KB pool stack, not anything
ext2 owned.

**Therefore: any storage driver that mounts ext2 (or any comparably deep fs)
with an 8 KB `storage_run` pool stack reproduces #120.** On the Pi 4 today only
`bcm2711-emmc` mounts ext2 and it is already fixed to 64 KB. But the latent rule
holds for the whole family.

## The libstorage "default" question — corrected finding

The task framing ("libstorage defaulted worker stacks to 2*_PAGE_SIZE") is
**not accurate as stated**, and the correction matters for the fix scoping:

- `storage_run(unsigned int nthreads, unsigned int stacksz)` has **no
  compiled-in default**. `stacksz` is a **required parameter**; `storage.c:573`
  just does `malloc(nthreads * stacksz)` with whatever the caller passes. The
  header (`libstorage/include/storage/storage.h:71`) documents **no recommended
  value**.
- So 8 KB was never a *library* default — it was a **per-caller convention**.
  Survey of every `storage_run` caller in the tree:

  | Caller | stacksz passed |
  |---|---|
  | `bcm2711-emmc` (Pi 4) | `16*_PAGE_SIZE` = **64 KB** (#120 fix) |
  | `imx6ull-flashnor` | `4*4096` = **16 KB** |
  | `flashdrv` | `2*_PAGE_SIZE` default, overridable via `FLASHSRV_STORAGE_STACK_SIZE` |
  | `virtio-blk` | `2*_PAGE_SIZE` = **8 KB** |
  | `zynq7000-sdcard` | `2*_PAGE_SIZE` = **8 KB** |
  | `zynq-flash` | `2*_PAGE_SIZE` = **8 KB** |
  | `grlib-nandfctrl2` | `2*_PAGE_SIZE` = **8 KB** (×31 threads) |
  | `imx6ull-flash` | `2*_PAGE_SIZE` = **8 KB** (×31 threads) |

  The 8 KB convention is copy-pasted across most drivers. None of these are Pi 4
  servers, so they are **out of scope for this audit's verdicts**, but they show
  the convention is systemic.

**Recommendation (default vs per-caller):** There is **no central default to
raise** — `storage_run` cannot pick a safe size because it has no knowledge of
the mounted-fs call-chain depth, and bumping a single hardcoded floor would
over-allocate the shallow flash drivers (esp. the 31-thread NAND/NOR drivers,
where 8 KB → 64 KB is +1.75 MB of stack). The correct shape of the fix is:

1. **Add a documented safe-minimum constant to libstorage** (e.g.
   `STORAGE_DEEPFS_STACKSZ = 16*_PAGE_SIZE`) **with a header comment** stating
   "use this when mounting ext2/jffs2/any deep fs; the bare 8 KB is only safe
   for shallow block/flash handlers." This makes the #120 lesson discoverable at
   the call site instead of living in a one-off comment.
2. Keep the size a **per-caller argument** (the Pi 4 driver already passes
   64 KB), since shallow drivers legitimately want the smaller stack.

## USB 2 KB stacks — RISK, with a specific hypothesis to test

The `usb` daemon runs its message thread (`ustack`) and status threads on
**2 KB** stacks (`usb.c:51,53`) while driving the **deepest** chain of any Pi 4
process: PCIe/VL805 bring-up, device enumeration, and the in-process HID class
drivers (usbkbd/usbmouse linked via `USB_HOSTDRV_LIBS`, so their attach paths
run *inside* these threads). 2 KB with no guard page is alarming for that depth.

**Hypothesis this audit surfaces (NOT asserted as proven):** the documented
intermittent USB kbd-attach abort — *"wild jump through corrupted code pointer,"
"#121-family corruption"* (see `MEMORY` / `docs/notes/2026-06-06-usb-intermittent-
kbd-attach-abort.md`) — is consistent with a 2 KB stack overflow silently
corrupting an adjacent stack/pointer during attach. This is a **lead, not a
verdict**: the enum bench passes 10/10, so it is not a deterministic overflow.

**Recommended follow-up:** bump the USB daemon stacks to **16–32 KB** and re-run
the kbd-attach abort bench. Cheap to try, and it directly tests whether the
intermittent corruption is a stack-depth artifact. Do this on an **attended**
session (USB daemon internals are a known statistical-regression hazard; see
MEMORY "Unattended vs attended scoping").

## Executive summary

- **15 Pi 4 plo-launched servers/daemons audited** (+ ext2/meterfs libs they run on).
- **RISK:**
  - **`usb`** — **2 KB** msg + status thread stacks on the deepest call chain in
    the system; headline finding; plausible mechanism for the known intermittent
    kbd-attach corruption. Bump to **16–32 KB**, attended, re-run abort bench.
  - **genet `irq_stack`** — **8 KB** (`uint32_t[2048]`), the *exact* size that
    bit #120; empirically stable but borderline. Bump to **16 KB** for margin.
- **OK / already fixed:** `bcm2711-emmc` (64 KB, #120 fix) and `nfs` (64 KB,
  applied tonight) are the two deep-fs servers and both are correct. lwip core
  (16 KB), posixsrv (4 KB ×4), pl011-tty (4 KB), dummyfs (main thread + 4 KB
  one-shot helper), and the four single-threaded rpi4 device drivers
  (thermal/hwrng/fb/gpio, no worker threads at all) are all shallow enough.
- **libstorage has NO 8 KB default to raise** — `storage_run`'s stacksz is a
  required per-caller argument; 8 KB is a copy-pasted convention. Recommend
  adding a *documented safe-minimum constant* (e.g. `16*_PAGE_SIZE` for deep-fs
  mounts) to libstorage's header and keeping the per-caller override, rather
  than forcing one floor (which would waste ~1.75 MB on the 31-thread flash
  drivers).
- **ext2/meterfs own no threads** — they run on the host storage driver's pool
  thread, so the #120 risk is *entirely* a property of the driver's
  `storage_run` stacksz. Any future Pi 4 storage driver mounting a deep fs must
  pass ≥16 KB (Pi 4's ext2-over-SD needed 64 KB).
- **No guard pages anywhere** — every overflow here is silent adjacent
  corruption, which is why sizes must carry margin rather than be tuned to the
  bone.

## Recommended stack sizes (follow-up fixes, to be done attended)

| Server | current | recommended | priority |
|---|---|---|---|
| usb (ustack + status) | 2 KB | 16–32 KB | high (test abort hypothesis) |
| genet irq_stack | 8 KB | 16 KB | medium |
| genet link_poll_stack | 4 KB | 8 KB (uniformity) | low |
| libstorage header | — | add documented `16*_PAGE_SIZE` deep-fs constant | medium (upstreamable) |

No code was changed by this audit.
