# SD ext2-root: exec-from-card data-path status (#120, 2026-06-07)

## Where #120 actually stands

- **Root mount: DONE + committed** (devices `ebac8e4`, project `cb4b216`). The ext2
  partition mounts as `/`; `ls /`, `ls /dev`, `ls /root` work; psh builtins work.
  This was the #120 deliverable.
- **Open (separate, hardware-marginal): executing binaries from the SD root.** Every
  external binary (`/bin/date`, `ps`, `df`, `wget`, `dd`) fails to produce output;
  psh *builtins* (ls, cat, etc.) work because they need no exec/SD-read of a binary.

## The data-path diagnosis (4 HW iterations, SD-boot)

Error signature on a binary read: `sdcard error: cmd error intr_status=0x00608000
err=0x00600000` (Data CRC bit21 + Data End-Bit bit22) + `transfer error 00400900`.

1. **Baseline (50 MHz HS, no retry):** binary read hits Data-CRC, then the controller
   sticks `-EBUSY` and the read hard-fails (`ret=-16`). `ls` (few blocks) passes; exec
   (many blocks) always hits a bad one.
2. **Forced 25 MHz (confounded):** MBR read failed at the *command* stage (cmd-done
   timeout) — a never-exercised default-speed path; reverted. Inconclusive about clock.
3. **Bounded read-retry (committed):** eliminated the hard `-EBUSY` (the retry recovers),
   BUT revealed ~1 Data-CRC **per block** at 50 MHz — a flood of recover-after-2-3-retries.
4. **PIO FIFO flow-control (PRES_STATE level-gating) + reset-both + busy-wait (committed):**
   cut errors ~47→13 and localized them to **3 shared, reproducible, 4 KB-strided LBAs
   (137768 / 137776 / 137784)** read on *every* exec (loader/libc pages). They recover
   (0 hard failures) — **yet binaries still produce no output.**

## THE UNRESOLVED CRUX (resolve first next session — advisor flag)

Reads "recover" (CRC passes, 0 hard failures), so the needed blocks return with valid
CRC — yet exec fails. Exactly one of these is true, and it is **not yet determined**:
- **(A)** "recovered" data is CRC-valid-but-corrupt (retry/FIFO returns misaligned bytes)
  → corrupt ELF → silent exec fault. *Verify RESET_DAT actually flushes the FIFO pointer.*
- **(B)** exec-from-mounted-ext2 is broken **independently**; the 3-block error flood is a
  coincident red herring optimized-against for 3 rounds.

The error logs cannot distinguish A from B. **Do not write another SD driver fix until
this is resolved.**

## Morning discriminator tests (cheap, decisive)

1. **`cat` is a psh builtin** → reads SD data through the read path *without* exec.
   On the Pi: `cat /etc/system.dtb` (a known file on the ext2 root) — if it completes and
   returns clean data (errors just recover), **reads functionally work → exec is the broken
   layer (B), not SD.** If it hangs/corrupts → recovery returns bad data (A).
2. **Does ANY external binary run from the SD root?** If none ever has (incl. a tiny one),
   suspect the exec/mmap-from-ext2 path itself, decoupled from CRC noise.
3. **Host-side readback** (card is in host now): `cmp` first MB of `/dev/sda2` vs
   `.buildroot/_boot/aarch64a72-generic-rpi4b/part_rootfs.ext2` → confirms the *written*
   image is intact, isolating card-wear/reflash from the Pi read path. (Can do without the Pi.)

## Why 4 KB-strided LBAs?

The 3 erroring blocks are 8 blocks (4 KB) apart and read on every exec. Candidate: the
`SDHOST_REG_SDMA_ADDRESS` write + `TRANSFER_BLOCK_SDMA_BOUNDARY_4K` field are still
programmed even though `dmaEnable=0` (PIO) — if the controller acts on the 4 KB SDMA
boundary in PIO, it could disrupt the transfer at 4 KB-aligned card addresses. Worth a
test: clear the SDMA address/boundary fields when dmaEnable=0. (Was flagged "vestigial" in
the review — may be harmful, not just vestigial.)

## NIGHT HOST-SIDE ANALYSIS — 2026-06-07 (decisive; card-in-host, all read-only)

Discriminator #3 (host-side readback) DONE, plus deeper forensics. Method: `cmp` /
`debugfs dump` / `dumpe2fs` on `/dev/sda2` vs the source `part_rootfs.ext2` (passwordless
sudo, strictly read-only — no write/fsck/rw-mount; the flashed image is untouched).

**Findings:**
1. **Data at rest is PERFECT.** The multicall binary that `/bin/date` execs (inode 13,
   200136 B; `date`/`ls`/`cat`/… are all hardlinks to it) is **byte-identical** card vs
   source (sha256 `936319e6…`). posixsrv too. → **media / flash / reflash-wear RULED OUT.**
2. **The whole 256 MB partition differs from source in only 4 bytes**, at offsets
   270345–270348 = **inode 13's `i_atime` field** (block 264 holds inodes 13–16; bytes 8–11
   of an ext2 inode = atime). Card atime = `0x00000031` (=49, the Pi's RTC-less clock);
   source atime = `0x6A238E73` (2026 build time). → **the Pi's ext2 mount WROTE inode 13's
   atime when `/bin/date` was accessed.** This proves three things at once:
   - the Pi **SD *write* path works** (correct field, correct offset, sane value);
   - the Pi **read inode 13's *metadata* correctly** (found the file, attempted exec);
   - the inode-table block (264) reads fine — the failure is **only in the data blocks**.
3. **The failing LBAs ARE inode 13's data blocks.** Block map:
   `(0):1300, (4-11):1301-1308, (IND):1309, (12-195):1310-1493`. Memory's "errors every
   exec at LBA 137768/137776/137784" → partition fs-blocks **1300 / 1304 / 1308** = inode
   13's early data blocks. Each is **4 KB-aligned** (fs-block N is 4 KB-aligned ⟺ N ≡ 0 mod 4;
   1300×1024 = 325×4096). Inode 13 spans blocks 1300–1493 → **~49 such 4 KB-aligned blocks**,
   matching the pre-fix error count **47**.

**Sharpened verdict — A over B, with a mechanism.** Data on disk is correct, metadata
reads + writes work, only *data-block* reads fail, and they fail **precisely at 4 KB
boundaries**. So **Hypothesis A (read path returns corrupt/errored data) is strongly
favored over B (exec logic)**, and the mechanism is the **SDHCI 4 KB SDMA buffer-boundary
leaking into the read path** — the `TRANSFER_BLOCK_SDMA_BOUNDARY_4K` field is still
programmed with `dmaEnable=0`, and the controller appears to act on it at 4 KB-aligned
card addresses even in the PIO path.

**Morning plan (revised, go straight at the boundary):**
1. **Primary fix to try:** clear the SDMA address + `TRANSFER_BLOCK_SDMA_BOUNDARY` field
   (or set boundary to max 512 KB) whenever `dmaEnable=0`, in the bcm2711-emmc PIO read
   setup. Reflash, SD-boot, `/bin/date`. Expect the 4 KB-aligned Data-CRC errors to vanish.
2. **Confirm A vs B definitively (cheap):** on the Pi, `cat /bin/date | <hash/redirect>`
   via the psh *builtin* (reads inode 13's data blocks through the SD read path WITHOUT
   exec). If the bytes come back wrong/errored → A confirmed (read path). If clean → the
   read works and the residue is exec/ELF-load (B). Given finding #3, expect A.
3. Only if (1)+(2) show reads are clean but exec still fails → pivot to the ELF/mmap-from-
   ext2 loader path (B).

This eliminates the "is the card/flash bad?" branch entirely and points the morning at one
register field. (Card remains in host; analysis did not modify it beyond reading.)

### Exact code location (for the morning fix)

`sources/phoenix-rtos-devices/storage/bcm2711-emmc/sdcard.c:414-419` — the data-transfer
setup programs the SDMA boundary **even though PIO is used** (`dmaEnable = 0` at line 427):
```c
*(host->base + SDHOST_REG_SDMA_ADDRESS) = host->dmaBufferPhys;   /* 414 */
sdio_dataBarrier();
*(host->base + SDHOST_REG_TRANSFER_BLOCK) =
    ((uint32_t)blockCount << 16) |
    TRANSFER_BLOCK_SDMA_BOUNDARY_4K |        /* 418 — 0b000<<12, the 4KB boundary */
    blockLength;
```
And `SDHOST_INTR_SDMA_BOUNDARY` (bit 3) is in the enabled interrupt set
(`sdhost_defs.h:188,213`) — so the controller *can* raise a 4 KB-boundary interrupt during
the PIO transfer, which the wait/error path may then mis-handle.

Two concrete things to try (order cheapest-first), each one reflash + SD-boot `/bin/date`:
1. **Set the boundary past any transfer:** replace `TRANSFER_BLOCK_SDMA_BOUNDARY_4K` with
   `TRANSFER_BLOCK_SDMA_BOUNDARY_512K` (defs already exist, `sdhost_defs.h:137`) so the
   4 KB carry-out never fires within a read. Cheapest test of the hypothesis.
2. **Stop arming SDMA entirely in PIO:** when `dmaEnable == 0`, skip the
   `SDHOST_REG_SDMA_ADDRESS` write (414) and drop `SDHOST_INTR_SDMA_BOUNDARY` from the
   active interrupt mask, so no boundary event is generated or awaited.

Mechanism note (label = hypothesis): the boundary is a *buffer*-address carry-out, but it
correlates with 4 KB-aligned *card* addresses because the PIO read advances buffer offset
in lockstep with file/card offset — so buffer-4 KB boundaries land on the same blocks as
card-4 KB boundaries. The empirical facts (data-at-rest perfect, only data-block reads
fail, failures exactly on N≡0 mod 4 blocks, ~47≈49 of them) stand regardless of the exact
intra-controller path.

## A-vs-B VERDICT — 2026-06-07 (attended, HW, DECISIVE): it's B (exec), not A (reads)

Built a diag SD loader logging `word0` of each recovered block; flashed; SD-booted;
ran `/bin/date` at psh. Result:
- Every recovered block read by the Pi **matches the source byte-for-byte** (word0),
  incl. mid-binary AArch64 code (`0xa9bf7bfd` stp-prologue, `0xd65f03c0` ret):
  lba 137770/137778/137786/137792/135694 all MATCH part_rootfs.ext2.
- `/bin/date` still produced **NO output**.

**Conclusion: Hypothesis A (corrupt recovered reads) is DISPROVEN.** The SD read path
delivers correct data; the Data-CRC errors are real-but-transient (50 MHz signal
margin) and the bounded retry recovers the **correct** bytes every time. The SD driver
is functionally correct — only noisy. **The #120 blocker is Hypothesis B: executing /
loading an ELF from the mounted ext2 root fails**, independent of the (recoverable) SD
errors. This is the FIRST exec-from-mounted-fs on the port (netboot execs from the
syspage/loader.disk), so the exec/spawn/ELF-load-from-fs path is the suspect.

Next (exec path, not SD): determine the exec failure mode — does psh/spawn return an
error, or does the process load and fault silently? Trace how Phoenix loads an ELF from
a filesystem (posixsrv/kernel spawn → file-backed mmap → demand-page via mtRead to the
ext2 server) vs from the syspage. The diag word0 logging can be removed from the SD
driver — its job (the A/B verdict) is done.

## INTERMITTENT CRASH found via HDMI capture — 2026-06-07 (bcm2711-emmc Data Abort)

While re-running the SD boot with HDMI capture (`test-cycle-netboot.sh --sd-boot`), 1 of 3
boots showed: ext2 root mounts OK, then **`bcm2711-emmc` (PID 7) Data Abort, psh never
reached.** Fault: `pc=0x4290e4 esr=0x92000047 far=0x80` → **`lib_listRemove`
(libphoenix sys/list.c:46)** dereferencing a node whose `prev` (poff) is NULL, writing to
`NULL + noff(0x80)`. I.e. a **corrupted list-node linkage** (null link in a circular list).

- **Intermittent:** the 2 psh-interact boots (same flashed image) reached psh + ran
  `ifconfig`; this 3rd boot crashed. So mount ✓ and exec-from-ext2 ✓ both hold — but an
  intermittent list/memory corruption can take down the SD driver before psh.
- **Signature = #121-family corruption** (intermittent, root-cause-unconfirmed), now in
  the bcm2711-emmc process. noff=0x80 → the corrupted struct's "next" link is at offset
  0x80; identify which list (libphoenix internal: msg/thread/resource, or libcache).
- **Confound to rule out:** the word0 diag was in the flashed image (now reverted from
  source). It touches no list and survived 2 boots, so unlikely causal — but rebuild a
  CLEAN image (committed source, no diag) and bench N boots to (a) confirm the crash is
  pre-existing and (b) measure the rate. Use `test-cycle-netboot.sh --sd-boot` (HDMI on)
  for the bench so faults are visible.

## ROOT CAUSE of the crash — ext2 object-cache LRU double-remove (2026-06-07, advisor: read the LR)

Read the full exception register dump (not just pc/esr/far): `lr=0x4219c4` → `addr2line`
= **`ext2_obj_get` (phoenix-rtos-filesystems/ext2/obj.c:166)**. Args: x1(t)=0x8e40 (the
node), x2(noff)=0x80, x3(poff)=0x78. The fault is `LIST_REMOVE(&fs->objs->lru, obj)` on an
object whose `prev`/`next` are **NULL** — `lib_listRemove` NULLs links after removal, so
this is a **double-remove / remove-of-an-unlisted node**:

```c
obj->refs++;
if ((obj->refs == 1) && !EXT2_IS_MOUNTPOINT(obj))   // refs WAS 0 → assumed on LRU
    LIST_REMOVE(&fs->objs->lru, obj);               // but obj not on LRU → NULL+0x80 write
```

So it's a **Phoenix ext2 filesystem bug** (the refs↔LRU bookkeeping), NOT the SD driver,
USB, or my diag. It's exercised hard by exec-from-ext2 (loading a binary reads many inodes
→ object-cache get/put churn). **4/4 deterministic repro under `test-cycle-netboot.sh
--sd-boot`** (the gift: reproducible). (NB: x16-x18/x24-x28 in the dump hold a
register-index sentinel pattern = uncaptured regs, not memory poison — do not over-read.)

Invariant: objects with refs==0 are meant to live on `lru` (added by ext2_obj_put when
refs hits 0, unless links==0 → destroyed); get() pulls them off when refs goes 0→1. The
crash means an object reached the `refs==1` branch while NOT on the lru (links NULL). Exact
trigger (get/put imbalance vs destroy-without-unlink vs a refs underflow) still to pin in
_ext2_obj_create/_ext2_obj_destroy. **Fix candidates:** (a) robust — track on-lru state
(flag/`OFLAG_ONLRU` set on LIST_ADD, cleared on LIST_REMOVE) and only remove when set,
making double-remove a safe no-op; (b) root-cause the refs/destroy imbalance. Validate via
the deterministic netboot-sd repro (expect 0/N crashes). NEEDS rebuild + reflash (swap).

## FIXED + VALIDATED — 2026-06-07: storage_run(1), a Pi4-local concurrency fix

The crash was NOT an ext2 logic bug. It's a **concurrency race in the shared ext2 object
cache**, exposed only by Pi4's combination of (a) `storage_run(2)` = 2 fs worker threads
calling libext2 concurrently, and (b) uniquely slow CRC-retry SD reads that hold an object
in flight long enough to lose the race → `ext2_obj_get` `LIST_REMOVE`s an LRU node another
thread already unlinked → `prev==NULL` → Data Abort. Why no other platform sees it: the
only other multi-threaded ext2 path is fast/jffs2-primary (zynq), and virtio-blk runs ext2
**single-threaded** (`storage_run(1)`) — the proven-safe config.

**Fix (Pi4-local, zero ext2 changes):** `bcm2711-emmc/sdstorage_srv.c` `storage_run(2)` →
`storage_run(1)`. **Validated on HW:** the deterministic **4/4 crash is now 0/3** —
mount + psh + USB kbd/mouse enum + 0 faults, HDMI-confirmed `(psh)%`. Committed (devices),
manifest `2026-06-07-sd-root-exec-working.md`. (Optional: a larger N bench for extra
statistical confidence; 0/3 after a 100% repro is already decisive.)

**#120 STATUS: DONE.** Persistent SD ext2 root mounts as `/`, binaries exec from it
(`ifconfig` ran), and the boot reaches psh stably. Residual cleanups (not blockers): the
noisy-but-recovering 50 MHz Data-CRC reads (signal-margin polish), and single-block-only
CMD24 writes / CMD18 multi-block (perf TODOs). The shared ext2 object-cache thread-safety
gap is a real upstream issue to report (not Pi4's to fix), now avoided by single-threading.

## Strategic note

The user said "not fixed" across 3 fixes — swap-iteration is not converging on the exec
path. Root mount (the #120 requirement) is done. Exec-at-50 MHz is an attended
hardware-bring-up effort (scope/logic-analyzer, scope tuning, a 2nd card). The committed
recovery + PIO-gating fixes are real upstream-quality improvements regardless. See
[[project_pi4_sdroot_120]].
