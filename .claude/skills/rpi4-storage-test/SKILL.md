---
name: rpi4-storage-test
description: >-
  Test a block device or filesystem on the Pi 4 (USB stick /dev/umass*, SD
  /dev/mmcblk0, ext2 via libext2) for CORRECTNESS or SPEED. Use for any change to
  libext2, libcache, umass, or the SD driver, and for any "is the data intact" /
  "how fast is it" question about storage. Gives the e2fsck oracle loop, the
  known-clean reference image, and the measurement traps that have produced false
  readings here.
---

# Testing storage and filesystems on the Pi 4

The lesson this skill exists for: **on this project, storage bugs are silent.**
Ten ext2 defects were found on 2026-09-21 and not one of them announced itself —
files read back wrong, blocks leaked, inodes were freed under live files, and
every single test still "passed" by return code. Grade by an external oracle, not
by rc and not by your own filesystem's opinion of itself.

## ★ FIRST: run it on the HOST. No Pi cycle needed.

`tools/libext2-hosttest/run.sh [blocksz]` runs **the real libext2** — the same
eight `.c` files that ship — against a file-backed device, then hands the image
to `e2fsck -fn`. Under ASan+UBSan, in under a second. `tools/libcache-hosttest/`
does the same for libcache's read and write paths.

```
./tools/libext2-hosttest/run.sh 1024     # SD root's block size
./tools/libext2-hosttest/run.sh 4096     # the stick's
./tools/libcache-hosttest/run.sh         # read shapes
./tools/libcache-hosttest/run-write.sh   # ranged write-back A/B
```

It works because libext2 reaches storage through two plain callbacks
(`fs->legacy.read/write`), so `pread`/`pwrite` on a `mke2fs` image is a complete
substitute; only the Phoenix *headers* are shimmed. The callbacks also **count
device operations**, which no Pi measurement gives you directly.

⛔ **Do not open a storage investigation with a Pi cycle any more.** The eleven
defects of 2026-09-21 each cost 5-10 min per attempt. On its first two runs the
host harness found two more (`i_blocks` not decremented for freed indirect
blocks; `ext2_inode_init`/`_sync` indexing `fs->gdt[]` before validating `ino`).
**Test both block sizes** — they take different arithmetic paths, and defect 12
showed at 1 KiB and 4 KiB with different residuals.

Spend the Pi cycle on *confirming*, not discovering. The host harness is not a
substitute for the gate: it cannot see the driver, the cache policy the driver
picks, DMA, or anything above the callback boundary.

## ⚠ Reading a partition back off the Pi: two traps that cost cycles

Both of these produced a short image that looked exactly like a corrupted or
truncated device (2026-09-22).

**1. `bs=1M` cannot reach the end of a partition that is not a whole number of
MiB.** `/dev/mmcblk0p2` is 2102724 sectors = 1026.72 MiB, so a 1 MiB read at
offset 1026 MiB overruns the end by 286 KiB and is refused. `dd ... bs=1M` with
no `count` therefore stops after the last WHOLE megabyte, prints **no summary
line**, and leaves an image ~756 KB short — and `e2fsck` then says "The physical
size of the device is ... blocks. Either the superblock or the partition table
is likely to be corrupt", which reads like a real defect and is not.

* compute the exact byte count from `fdisk -l` (`sectors x 512`) and use a
  `bs`/`count` that divides it, or
* **check for `dd`'s `records out` line before trusting the image.** Its absence
  means dd aborted, whatever the file size looks like.

(Fixed in the driver as of `822e933`: a crossing read now returns a short read
and `offs >= size` returns 0, so `dd` with no `count` works. The trap remains
worth knowing for any older image.)

**2. The SD card looks dirty after a boot, and MOST OF IT IS NORMAL.** ↩ An
earlier version of this skill said the card leaks 2 inodes + 2 blocks per
power-cut cycle and that `sync` fixes it. **Both claims were wrong** and are
retracted; here is what is actually true, each measured:

* **`/var/tmp` is created at boot.** That is +1 inode and +1 block on every
  freshly flashed card, and it is a real directory, not a leak. Four hours went
  into "explaining" it. One command settles this class of question:
  `debugfs -R "ncheck <inode>" img` — **ask what the inode's NAME is before
  counting inodes.**
* **`e2fsck`'s "Inode N was part of the orphaned inode list" does not mean N is
  allocated.** Check with `testi`: here all three flagged inodes were **free in
  both** the pristine and the used image. A correctly freed inode carries a
  non-zero `dtime`, and `e2fsck` walks the orphan chain whenever
  `s_last_orphan != 0` — which is a **host image-build artefact**, present in
  the pristine image too. Benign noise.
* **Compare against the pristine image OF THE SAME BUILD.** Every build makes a
  fresh filesystem with its own counts. The tell when you get this wrong is the
  **total** block count moving (1051314 vs 1051284), which cannot happen from
  use. Extract it: `dd if=artifacts/rpi4b/rpi4b-sd-2part.img of=/tmp/cur_p2.img
  bs=512 skip=135168 count=2102724`.

`/` *is* the card and the harness cuts power, so there is no clean unmount —
that is still true, and **`/usr/bin/sync` is still worth ending a cycle with**
now that it does something (it was an empty libphoenix stub until `40a1efb`;
`fsync` also sent a zeroed oid, and no filesystem handled `mtSync` at all). But
with the full chain in place the counts were **unchanged**, which falsified the
unflushed-metadata story rather than confirming it.

## The oracle: `e2fsck` on a read-back, host-side

This is what the host harness automates, and it is still how you grade the real
device. Phoenix writes, Linux judges.

```
# on the Pi, after the workload and AFTER unmounting:
/usr/bin/dd if=/dev/umass1 bs=1M count=1024 of=/final.img     # NFS export = host disk
# on the host:
cp /srv/phoenix-rpi4-nfs-gcc16/final.img /tmp/f.img && e2fsck -fn /tmp/f.img
```

Read the output precisely — the two directions mean different things:

| e2fsck says | meaning |
|---|---|
| `Block bitmap differences: -N` | N marked USED on disk, owned by nobody ⇒ **leaked space**, not data loss |
| `Block bitmap differences: +N` | N marked FREE but a file claims it ⇒ **live data at risk**. Serious. |
| `Multiply-claimed block(s) in inode X: …` | two files share blocks ⇒ one has been overwritten |
| `Inode bitmap differences: +(a--b)` | live inodes marked free ⇒ next create hands them out |
| `Deleted inode N has zero dtime` | inode freed without stamping `i_dtime` |
| `Inode N, i_blocks is 0, should be M` | `i_blocks` not maintained ⇒ `stat`/`du` lie |
| no output at all before the `PHXCLEAN:` summary line | **clean.** The only acceptable result. |

⚠ A `WARNING: Filesystem still has errors` line means NOT clean, even when every
`TAG-` line in your script looked fine.

## The known-clean reference

- `/data/test-artifacts/clean4k.img` on the NFS export — a 1 GiB **4 KiB-block** ext2 containing
  `/phoenix/data.bin` (8 MiB) and its `SHA256SUMS`. `e2fsck`-clean by construction. Rebuild it on the host with
  `mke2fs -t ext2 -b 4096 -d <stage>` over a 1 GiB `truncate`d file, `<stage>/phoenix/` holding `data.bin` = the
  reference file below plus `sha256sum data.bin > SHA256SUMS` (recreated that way 2026-09-27).
- `/data/test-artifacts/ref-data.bin` — a bare copy of that same 8 MiB file, so the Pi can `cmp`
  against it directly without mounting anything.
- Restore from the Pi itself, no human: `/usr/bin/dd if=/data/test-artifacts/clean4k.img of=/dev/umass1 bs=1M`
  (~60 s at 18 MB/s).

**Always start a correctness test from a restore.** A filesystem damaged by the
previous run is not a baseline, and several hours were lost reasoning about
damage that a prior test had caused.

⚠ **Confirm the restore completed**: it must print `1073741824 bytes`. One run
stopped at 255 MiB while still printing a normal `copied` line, and the
conclusion drawn from it ("the medium is untouched") was wrong.

## Grading integrity

Use `cmp` against `/data/test-artifacts/ref-data.bin`, and check it **after a remount** as well as
before — the page/block cache will happily return correct data for a file whose
on-disk mapping is broken:

```
echo "TAG-intact: $(/usr/bin/cmp /data/test-artifacts/ref-data.bin /mnt/umass1/phoenix/data.bin 2>&1 | tail -1)  (blank = identical)"
```

`cmp` also names the **first differing byte**, which localises the fault:
`differ: char 65537` = offset 65536 = logical block 16 of a 4 KiB fs. Feed that
to `debugfs -R "stat /path" image` host-side to learn whether that offset is
direct, indirect or double-indirect — that is how the double-indirect leak was
localised.

⚠ **`sha256sum` on the Pi can CRASH** (`Data Abort (EL0) far=0x30`, the known
`libc-uninit-main` fwrite-on-NULL bug). A crashed checksum prints nothing and
reads exactly like corruption. It fooled me twice. `cmp` has been reliable;
prefer it, and if you do use `sha256sum`, grep the log for `Data Abort` before
believing a mismatch.

## Measuring speed

- **Use coreutils `/usr/bin/dd`'s own reported rate.** Never harness wall-clock,
  never `/bin/dd` (busybox, which cannot self-report).
- Sizes matter on flash: the test stick does **39.9 MB/s for 256 MiB** but
  **18.2 MB/s for 1 GiB** — that is its SLC cache, not variance. Compare like
  with like.
- `/usr/bin/dd if=/dev/umass1 skip=<large>` fails with
  `cannot fstat '/dev/umass1': Function not implemented` and leaves a **0-byte
  file**. Small skips work. Check dump sizes before trusting a dump.

## Measuring write amplification — the two-boot split

umass prints `write amplification: asked X B, device Y B` at unmount, and those
counters are **cumulative from boot**. A script that restores a 1 GiB image and
then writes 72 MiB through the filesystem reports `1.01x` — which says nothing,
because 97% of the sample is raw full-line traffic. Reading that as "amplification
is gone" nearly closed the work on a false green.

Split it across two boots, and let the stick keep state between them:

```
boot A:  restore only                      (wrA.sh — no mount, no fs traffic)
boot B:  mount, workload, umount           (the amplification line is now clean)
```

Also pick the right workload: **bulk sequential writes are not amplified** (a
1 MiB write fills every 64 KiB cache line), so measure with **metadata** — e.g.
200 small files. Measured 21.02x with whole-line flush, 1.00x with ranged.

## Testing `fsync()`: the tool can fail BEFORE the syscall you are testing

`fsync()` on a **raw block-device fd** is the only thing the kernel oid fix
(`482b54c2`) changes — a regular file goes to `libext2_handler`, which dispatches
`libext2_sync(fdata)` and never reads `msg->oid`. So SQLite is **not** a test of it.

Use coreutils `sync FILE` (verified against `coreutils-9.5/src/sync.c`): with an
operand and no flags it takes `MODE_FILE` → **`fsync(fd)`**, opening `O_RDONLY |
O_NONBLOCK`, so it is non-destructive. `--data` would give `fdatasync`, and no
operand at all gives plain `sync()` — a different test.

⚠ **But it does `fcntl(F_GETFL)` / `fcntl(F_SETFL)` between the open and the
`fsync`, and bails out if either fails.** A build where `fcntl` does not work on a
block-device fd therefore never reaches `fsync` and still exits non-zero — which
would read as "fsync failed" if you graded by rc. Grade by which of the three
tagged lines appears:

| output | meaning |
|---|---|
| `error opening …` | never got an fd — check the path |
| `couldn't reset non-blocking mode …` | ⚠ **VOID** — it bailed before `fsync()`; this measured `fcntl` |
| `error syncing …` | `fsync()` ran and failed |
| *(silent)* | `fsync()` ran and succeeded |

And always pair it with a **positive control** — `sync /nonexistent` must print
`error opening`. An absence-of-error result with no control is not a measurement.

Run it on the **netboot** lane: the card still enumerates (`2 partition(s)`), but
`/` is NFS, so `/dev/mmcblk0p2` is an unmounted raw device you can safely sync.

## Assert the work actually happened

A timing over a loop that did nothing looks excellent. Print the count FIRST and
treat a wrong count as void, not fast:

```
n=$(/usr/bin/ls "$dir" | /usr/bin/wc -l)
echo "TAG-$label: count=$n secs=$secs   (count must be 200 or the timing is void)"
```

This bit me directly: `mkdir` issued as a separate psh command ran **before** the
mount, the mount hid the directory, both loops wrote nothing, and the script
reported `0 s for 200`. Create directories inside the same script, after the
mount.

## Running it

See the `rpi4-run` skill for the cycle mechanics and the Pi lock. Sizing for
storage work specifically: a restore is ~60 s and a 1 GiB read-back ~60 s, so
`--idle-secs 460 --max-cmd-secs 700` with a Bash `timeout` of 600000 is the
usual shape; expect to be backgrounded and wait for the task notification.

Stage scripts on the NFS export (`/srv/phoenix-rpi4-nfs-gcc16/`, the **live**
`-gcc16` one) and invoke as `/bin/bash /yourscript.sh`.
