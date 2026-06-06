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

## Strategic note

The user said "not fixed" across 3 fixes — swap-iteration is not converging on the exec
path. Root mount (the #120 requirement) is done. Exec-at-50 MHz is an attended
hardware-bring-up effort (scope/logic-analyzer, scope tuning, a 2nd card). The committed
recovery + PIO-gating fixes are real upstream-quality improvements regardless. See
[[project_pi4_sdroot_120]].
