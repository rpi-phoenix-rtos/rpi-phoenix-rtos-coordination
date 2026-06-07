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

## Strategic note

The user said "not fixed" across 3 fixes — swap-iteration is not converging on the exec
path. Root mount (the #120 requirement) is done. Exec-at-50 MHz is an attended
hardware-bring-up effort (scope/logic-analyzer, scope tuning, a 2nd card). The committed
recovery + PIO-gating fixes are real upstream-quality improvements regardless. See
[[project_pi4_sdroot_120]].
