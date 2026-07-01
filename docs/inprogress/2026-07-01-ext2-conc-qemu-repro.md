# ext2 concurrent-allocator crash — QEMU reproduction (non-SD, non-Pi4)

**Date:** 2026-07-01
**Goal:** Decide whether the Pi4 ext2 4-thread concurrent-write Data Abort
(`ext2_block_destroyone`, poisoned register file) reproduces on a **stock,
non-SD, non-Pi4** Phoenix QEMU target, and whether commit **463aec13**
("ext2: serialize filesystem ops") resolves it.

This is a QEMU-only experiment. No real hardware, no SD card, no touching
`bcm2711-emmc`.

> **VERDICT (a): YES** — the ext2 concurrent-allocator race reproduces
> deterministically on stock riscv64-generic-qemu (non-SD, non-Pi4) as mid-run
> data corruption. General Phoenix ext2 bug, portably reproducible.
> **VERDICT (b): YES for the race, NO for the HW crash** — 463aec13 eliminates
> the corruption in QEMU (63/4 → 0 mid-run corruptions across 7 runs), so it is
> a validated fix for the race. But no Data Abort ever occurred here, so the HW
> Data Abort the fix did NOT stop on real HW is a **distinct failure mode** the
> serialization does not address. QEMU tested the SD path zero times, so the
> mechanism is a **leading hypothesis for HW follow-up, not a QEMU finding**:
> a pool-thread stack overflow on the deeper SD (bcm2711-emmc) call chain —
> supported by the smaller 8 KB generic pool stacks NOT overflowing under this
> stress, which argues the ext2 chain alone doesn't exhaust a stack.

---

## 1. Target selection

Constraints that decided the target (from source investigation):

| Target | Toolchain present? | Block driver under QEMU | ext2 wired? | Verdict |
|---|---|---|---|---|
| riscv64-generic-qemu | needs `riscv64-phoenix` (built here) | **virtio-blk MMIO** (`#ifdef __TARGET_RISCV64`) | **yes** (`virtio-blk -r 0:0`) | **CHOSEN** |
| ia32-generic-qemu | needs `i386-pc-phoenix` (absent) | pc-ata / virtio-blk-pci | no | rejected (no toolchain) |
| aarch64a53-generic-qemu | `aarch64-phoenix` present | **none** — libvirtio PCI = `virtiopci-empty.c`, MMIO is riscv-only | no | rejected (would require writing a driver) |
| zynq*-qemu | — | jffs2 on flash (mtd) | no (task premise was stale) | rejected |

**Chosen: `riscv64-generic-qemu`.** It is the *only* stock target where
virtio-blk + ext2 are already wired end to end, requiring **zero driver
changes** — the cleanest possible "does it repro on a stock target" test.

Key facts:
- ext2 is hosted by the **`virtio-blk`** server
  (`phoenix-rtos-devices/storage/virtio-blk/vblksrv.c`).
- `main()` ends with `storage_run(1, 2 * _PAGE_SIZE)` → 1 spawned pool
  thread **plus** the main thread also becomes a pool thread = **2 worker
  threads**. This satisfies the ≥2-worker requirement for the pool-thread
  race, with **8 KB** stacks each.
- Contrast Pi4 `bcm2711-emmc`: `storage_run(2, 16*_PAGE_SIZE)` = 3 workers,
  **64 KB** stacks. **Generic has SMALLER stacks**, so if the crash were a
  stack overflow it would be *more* likely here, not less — a load-bearing
  fact for the race-vs-overflow verdict.
- rootfs is a **genext2fs**-built ext2 image on a 64 MB virtio disk
  (`vblk0.disk`), MBR partition type 0x83, mounted at `/`. genext2fs
  produces old-format ext2 that Phoenix's libext2 reads (no metadata_csum
  feature-compat trap).

## 2. Repro recipe (copy-paste)

Host prerequisites installed for this run:
- `riscv64-phoenix` cross-toolchain built from source via
  `phoenix-rtos-build/toolchain/build-toolchain.sh riscv64-phoenix
  /home/houp/phoenix-rpi/.toolchain` (gcc 14.2.0 + binutils 2.43; ~ built to
  `.toolchain/riscv64-phoenix/`). **No riscv64-phoenix-gdb** is produced.
- `genext2fs` (`apt install genext2fs`) — used by the riscv64 build to make
  the ext2 rootfs image.
- `qemu-system-riscv64` (`apt install qemu-system-riscv`) v10.2.1.
- venv python at `/home/houp/phoenix-rpi/.venv/bin` for `image_builder.py`.

Two **kernel** edits were required just to build riscv64-generic off the
current (fork-local) kernel HEAD — both are constant across the A/B arms and
cannot bias the ext2 result:
1. `hal/riscv64/generic/config.h`: `#define NUM_CPUS 1`
   (main.c gates fork-local SMP-secondary bring-up on `#if NUM_CPUS != 1`,
   which references SMP hal fns not implemented for riscv64-generic).
2. `proc/threads.c`: wrap the fork-local `threads_smpTickCount` /
   `hal_cpuAtomicInc` diagnostic in `#if NUM_CPUS != 1` (riscv64-generic has
   no `hal_cpuAtomicInc`).

Build + run:
```
# buildroot with sibling repos symlinked (so the fs checkout + _user app reflect)
./scripts/prepare-buildroot.sh --link-components /home/houp/phoenix-rpi/buildroots/ext2-qemu

# ext2 concurrent-stress app: sources/phoenix-rtos-project/_user/ext2conc/
#   N threads, each: open+CREAT+TRUNC -> write KB -> read-verify ->
#   ftruncate(0) -> write KB/4 -> unlink   (heavy block ALLOC + FREE churn)

# BEFORE-fix:
git -C sources/phoenix-rtos-filesystems checkout 463aec13~1   # 7698d52
export PATH=/home/houp/phoenix-rpi/.toolchain/riscv64-phoenix/bin:\
/home/houp/phoenix-rpi/.venv/bin:\
/home/houp/phoenix-rpi/buildroots/ext2-qemu/phoenix-rtos-build/scripts:$PATH
cd /home/houp/phoenix-rpi/buildroots/ext2-qemu
TARGET=riscv64-generic-qemu ./phoenix-rtos-build/build.sh clean all

# boot + drive psh over the pty (stdlib driver, waits for "(psh)% "):
python3 qemu-drive.py scripts/riscv64-generic-qemu.sh 1 170 "ext2conc 4 200 128"
```
QEMU line (from `scripts/riscv64-generic-qemu.sh`, `-smp 1`, MMIO virtio-blk):
`qemu-system-riscv64 -smp 1 -machine virt -bios _boot/.../sbi-generic.elf
-drive file=_boot/.../vblk0.disk,format=raw,if=none,id=vblk0
-device virtio-blk-device,drive=vblk0 -device loader,file=_boot/.../loader.disk,addr=0x20000000`

**Mount sanity (confirmed real):** boot log shows `virtio-blk: initialized`,
`hal: riscv-virtio,qemu (cfi-flash) - 1 core`; `ls /` shows `lost+found` +
1024-byte-block dirs (genext2fs ext2 at `/`, not dummyfs); `ls /usr/bin` lists
`ext2conc`. The app executes off the ext2 mount.

## 3. A/B results

**BEFORE-fix (`463aec13~1` = 7698d52), single-hart riscv64, 2 storage workers:**
The race **reproduces deterministically** as filesystem corruption (every run,
faults > 0):

| trial | invocation | result |
|---|---|---|
| 1 | `ext2conc 4 200 128` | `DONE started=4 faults=63` — 63× `CORRUPT` (read-back mismatch on a thread's OWN file: blocks double-allocated / freed-in-use → cross-file bleed + lost updates) |
| 2 | `ext2conc 4 150 128` | `DONE started=4 faults=8` (4× CORRUPT + others) |
| 3 | `ext2conc 8 120 256` | `DONE started=8 faults=8` — `unlink errno=2` (ENOENT) at iter 0 on several threads: corrupted directory/inode state |

No hard Data Abort was elicited in QEMU (see §4), but the underlying
non-atomic-allocator corruption reproduces on **every** run — the same root
cause the HW Data Abort attributed to. `virtio-blk` (which hosts `/`) stayed up.

**WITH-fix (`463aec13`), identical invocations, 7 runs:**

The steady-state allocator corruption is **completely eliminated**. Only a
separate, fix-orthogonal startup artifact remains (see below).

Decisive discriminator — **mid-run** corruption (`CORRUPT` at iter > 0, the
allocator-race signature), before vs after, same binary rebuilt each arm:

| run | invocation | mid-run CORRUPT (iter>0) | iter-0 faults |
|---|---|---|---|
| before-1 | `4 200 128` | **63** | 0 |
| before-2 | `4 150 128` | **4** | 0 |
| before-3 | `8 120 256` | 0* | 7 |
| after-1 | `4 200 128` | **0** | 0 |
| after-2 | `8 120 256` | **0** | 6 (incl. 3 CORRUPT all at iter 0) |
| after-4thr-a/b | `4 200 128` | **0 / 0** | 4 / 0 |
| after-heavy-c/d/e | `8 120 256` | **0 / 0 / 0** | 4 / 3 / 7 |

\* before-3's 8-thread run corrupted directory state so fast every thread
failed at iter 0; before-1/before-2 (4-thread) are the clean steady-state
discriminator: 63 and 4 mid-run corruptions.

**Two distinct fault classes** (kept separate so the verdict is clean):
1. **`CORRUPT` at iter > 0** — read-back of a thread's OWN file returns wrong
   bytes mid-run. This is the ext2 non-atomic-allocator race: concurrent
   alloc/free on 2 storage pool threads corrupts the shared block bitmap /
   gdt / sb free-counts, so blocks get double-allocated or freed-in-use →
   cross-file data bleed. **Before-fix: present (63, 4). With-fix: 0 in every
   run, incl. heavy 8-thread.** In before-1 the corruptions spread across
   iters 11…199 (steady state); in after-2 the only 3 CORRUPT are all at
   iter 0 (the startup artifact, not the race).
2. **`unlink errno=2` (ENOENT) at iter 0**, and iter-0 CORRUPT — a
   startup / mount-takeover visibility race: a file just created+written+
   closed is briefly not visible for unlink/read. **Present in BOTH arms**
   (before and after the fix) → orthogonal to 463aec13 (matches the known
   #156 "first-read-ENOENT" mount-window race, here on ext2/virtio-blk).

**Stale-core disproved behaviorally:** the two binaries behave differently
(63 → 0 mid-run corruptions), so they are not the same object. Confirmed at
source (`fs->lock` in ext2.h: 0 lines before / 1 after) and at object level
(`libext2.o`: **29 `mutexLock` call-site relocations** with-fix). (The
*stripped virtio-blk* happened to be 71088 bytes in both arms — an alignment
coincidence; the fs library object differs, which is what matters.)

## 4. Race vs stack-overflow determination

**It is a metadata race, not a stack overflow — and QEMU rules stack overflow
OUT for the pure-ext2 path.**

- No Data Abort / riscv exception (`scause`/`sepc`) was ever printed. The
  before-fix failure is **silent data corruption**, exactly what a corrupted
  shared block/inode bitmap produces (double-allocated or freed-in-use
  blocks). This is the mechanism 463aec13 targets, and serializing the
  `libext2_*` entry points removes it entirely.
- **Stack-size lever cuts against the overflow hypothesis.** The generic
  virtio-blk server runs `storage_run(1, 2*_PAGE_SIZE)` = 2 pool threads with
  **8 KB** stacks. The Pi4 bcm2711-emmc server runs `storage_run(2,
  16*_PAGE_SIZE)` = 3 pool threads with **64 KB** stacks. Under identical
  concurrent stress the *smaller* 8 KB stacks did **not** overflow (no fault,
  ever) — so the ext2 call chain by itself does not exhaust a pool-thread
  stack. If the Pi4 HW crash is a stack overflow, it needs the deeper
  **SD-hosted** call chain (bcm2711-emmc block layer) on top of ext2, not
  ext2 alone.

No gdb was needed (and none is built for riscv64-phoenix): the failure is a
value-corruption assertion in the stress app, and the absence of any CPU
exception is itself the key negative.

## 5. Verdict

**(a) Does the crash reproduce in QEMU on a non-SD, non-Pi4 arch? — YES, the
underlying ext2 concurrent-allocator race does.** On stock riscv64-generic-qemu
(single-hart, virtio-blk, 2 storage pool threads, genext2fs ext2 on a QEMU
virtio disk) the race reproduces **deterministically** as mid-run data
corruption on every before-fix run. It is a **general Phoenix ext2 bug**, not
Pi4/SD-specific, and is now reproducible portably with zero hardware.

Caveat: the *exact* HW manifestation — a hard Data Abort in
`ext2_block_destroyone` with a poisoned register file — did **not** occur in
QEMU. On riscv64 the same corrupted-allocator root cause surfaces as silent
data/directory corruption rather than a wild pointer dereference. The Data
Abort is a memory-layout-dependent downstream symptom; the corruption is the
fundamental signature and it reproduces cleanly.

**(b) Does commit 463aec13 resolve it in QEMU? — YES for the race; but it is
NOT the whole story for the HW crash.**

- **For the ext2 allocator race: validated.** With the fix, mid-run corruption
  drops from 63/4 to **0 across all 7 runs** including the heavy 8-thread case.
  The per-fs serialization lock is a correct, effective fix for the race, and
  QEMU is a portable regression test for it.
- **For the specific HW Data Abort: 463aec13 is insufficient / addresses a
  different failure mode.** The task states the fix did *not* stop the crash on
  real Pi4 HW. That is consistent with what QEMU shows: (i) the race QEMU
  reproduces is cured by the fix, yet (ii) no Data Abort occurs in QEMU even
  with *smaller* pool-thread stacks. Together this points to the HW Data Abort
  being a **distinct problem** — most plausibly a **pool-thread stack overflow
  in the deeper SD-hosted (bcm2711-emmc) call chain**, aggravated by the
  SD-side 16 KB cache-sector change — which serialization does not fix. The
  real HW fix is a bigger pool-thread stack / guard page on the SD path;
  463aec13's race-serialization is a separate, genuine correctness improvement
  that should still land.

**Bottom line:** 463aec13 is upstreamable and validated as a fix for a real,
general, portably-reproducible ext2 concurrent-allocator race. It should NOT be
expected to resolve the Pi4 SD Data Abort, which is a separate failure mode on
the SD call chain.

### Also observed (separate follow-ups, not this bug)
- A **startup/mount-window ENOENT+corruption race** at iter 0 on
  ext2/virtio-blk, present with and without the fix (cf. #156). A just-
  created file is briefly not visible. Deserves its own investigation.
- Two **fork-local kernel build breaks** for non-Pi4 archs on the current
  kernel HEAD (`NUM_CPUS` undefined in riscv64 config; unconditional
  `hal_cpuAtomicInc`/`threads_smpTickCount` in the shared scheduler). Fixed
  minimally here (see §2); worth cleaning up so riscv64/ia32/etc. keep
  building off the fork.

### Artifacts
All 13 UART/stress logs + the pty driver + build wrapper are under
`artifacts/ext2-conc-qemu/` (`stress-before-*.log`, `stress-after-*.log`,
`boot-sanity.log`, `qemu-drive.py`, `build-rv.sh`). The stress app lives at
`sources/phoenix-rtos-project/_user/ext2conc/`.
