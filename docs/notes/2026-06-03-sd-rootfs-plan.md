# SD card + persistent rootfs — execution plan (2026-06-03)

Goal: a real BCM2711 EMMC2 SD-card block device + an on-disk **ext2** filesystem
mounted as `/`, replacing the interim diskless `dummyfs-root`. This is the
idiomatic Phoenix path for a capable target (mirrors ia32/zynq), per the
architecture already settled in
[2026-05-31-rpi4b-rootfs-emmc2-ext2-plan.md](2026-05-31-rpi4b-rootfs-emmc2-ext2-plan.md)
and the rc.psh model in
[2026-05-31-rpi4b-rcpsh-rootfs-conversion-plan.md](2026-05-31-rpi4b-rcpsh-rootfs-conversion-plan.md).
This doc is the **current, actionable execution plan** and updates those where
state has moved on.

## What changed since the 2026-05-31 plans

- **The driver now exists.** `phoenix-rtos-devices/storage/bcm2711-emmc/` is
  written and runs: `sdcard.c` (SDHCI/Arasan core), `bcm2711-sdio.c` (EMMC2
  @`0xfe340000`, IRQ 158, 100 MHz refclk via VideoCore mailbox), `sdstorage_dev.c`
  (block dev + MBR → `/dev/mmcblk0[pN]`), `sdstorage_srv.c` (libstorage +
  `-r <dev>:<fs>` + `portRegister("/")`). So #119 is no longer "write the
  server" — it is **"fix the one failing init step."**
- **The blocker is concrete:** card init aborts at **ACMD41 (SD_SEND_OP_COND)** —
  `sdcard.c:524` logs `op cond fail` when the first ACMD41 transaction returns
  `< 0`, before the OCR ready-poll loop is even entered. Firmware boots from this
  same card, so the card + wiring are good — this is a controller/command-layer
  bug in our re-init.
- **The console is now usable for testing.** USB keyboard works end-to-end
  (#124) and the box reaches an interactive psh on HDMI/UART, so SD bring-up can
  be driven and observed live, not just via netboot logs.

Keep `dummyfs-root` as the interim `/` until the EMMC2 path is proven; run both
and flip the root only when stable (netboot of kernel+syspage stays orthogonal).

---

## Phase 1 — ROOT-CAUSE the ACMD41 `op cond fail` (the blocker, #119)

ACMD41 is CMD55 (APP_CMD, with RCA) followed by the app command; the wrapper is
`sdcard.c:367` (`SDIO_CMD55_APP_CMD`, `host->card.rca`) → ACMD41 at
`sdcard.c:524`. The failure is the command *transaction* erroring, not an OCR
ready-timeout. **Confirm on clean UART/HDMI which sub-step fails before writing
any fix** (project rule: confirm before fixing).

Leading hypotheses, ranked (verify, don't assume):

1. **R3 response-type / CRC handling (most likely).** ACMD41's response is **R3
   (OCR)**: 48-bit, **no CRC**, **no command-index** field. SDHCI must be told
   not to validate CRC/index for R3, or a *valid* response is rejected as an
   error. Check how `sdio_cmdSend`/`_sdio_cmdSend` set the command's response-type
   flags for ACMD41 vs CMD8(R7)/CMD3(R6) — a wrong `CMD_RESP_*`/CRC-check bit here
   produces exactly this immediate `< 0`. Cross-check against Linux
   `drivers/mmc/host/sdhci.c` + `sdhci-iproc`/`bcm2835` response-type tables.
2. **CMD55 itself failing.** If CMD55 (`sdcard.c:367`) errors (wrong RCA, wrong
   response type R1, or not-in-idle), the ACMD never reaches the card. At init the
   RCA must be 0; confirm `host->card.rca == 0` at this point and that CMD55
   checks `CARD_STATUS_APP_CMD` (`sdcard.c:373`).
3. **Clock/divider at 400 kHz.** `sdcard_calculateDivisor` (`sdcard.c:865`) from a
   100 MHz refclk must land ≤400 kHz for ID phase; verify the divisor written and
   that `INTERNAL_CLOCK_STABLE` was reached (`sdcard.c:911`) before any command.
4. **Firmware-left controller state.** VideoCore left EMMC2 running at high clock
   in TRAN; our CMD0 must return it to idle. Confirm a full controller soft-reset
   (CMD/DAT/ALL reset bits) + re-clock happens before CMD0, not just CMD0.
5. **Timeout / busy bits.** `DATA_TIMEOUT_VALUE` and the CMD-inhibit (CMD/DAT
   lines busy) wait before issuing — ensure we wait for `!CMD_INHIBIT` before
   ACMD41.

Diagnostics (block driver → no network; use UART + the existing `LOG_ERROR`):
- Temporarily widen the failure log to dump the SDHCI **Interrupt Status** (error
  bits: CRC, end-bit, index, timeout), the command register written, and the raw
  response for CMD55 + ACMD41. One concise dump, removed before close.
- Compare the exact register write sequence against `storage/zynq7000-sdcard`
  (same libstorage/SDHCI model, known-good) and Linux sdhci-iproc.

**Exit criterion:** init reaches CMD2/CMD3/CMD7 (TRAN state), `sdstorage_dev`
registers `/dev/mmcblk0` and `/dev/mmcblk0pN`, and a raw 512-byte read of the MBR
(sector 0) returns the partition table. Validate with the driver enabled in
`user.plo.yaml` and a `psh` `ls /dev` + a read probe.

---

## Phase 2 — ext2 read-mount validation (de-risk the FS layer)

Before building images, prove the ext2 lib mounts our block device.

- Confirm `sdstorage_srv.c` registers ext2: `storage_registerfs("ext2",
  libext2_storage_mount, libext2_umount)` (add if missing — verify against the
  zynq7000-sdcard template `:394`).
- Put a known **ext2** partition on the test card (e.g. `mkfs.ext2` a small image
  on p2 from the host), boot with `bcm2711-emmc -r /dev/mmcblk0p2:ext2`, and read
  a known file. This isolates FS-mount bugs from image-build bugs.

**Exit:** mount succeeds, a file reads back correct bytes. (Read-only first;
write tested in Phase 6.)

---

## Phase 3 — Build the ext2 rootfs image (#120 build side)

Mirror the ia32 image-build template (`_targets/ia32/generic/build.project:121-144`).

1. **Host prereq:** install `genext2fs` (host currently has only `mkfs.ext2`).
   genext2fs builds a deterministic image from a directory **without root/loop
   mount** — preferred for reproducible CI. Document the dependency.
2. **build.project rule (rpi4b):** `genext2fs -b <size_kb> -i 2048 -d
   "$PREFIX_ROOTFS" part_rootfs.ext2`. `PREFIX_ROOTFS` is already the full tree
   (psh + ~40 applets, servers, `/etc`); add `/etc/rc.psh` overlay (Phase 5).
3. Decide the rootfs **size** and inode ratio; leave headroom for writes.

**Exit:** a `part_rootfs.ext2` produced by the build, loop-mountable on the host
to verify contents.

---

## Phase 4 — Assemble the 2-partition SD image

The Pi boots p1 (FAT, VideoCore firmware + plo + kernel+syspage). Add p2 (ext2 root).

- `scripts/assemble-rpi4b-sdimg.sh`: keep the existing FAT boot partition; add a
  second MBR primary `type=83` (Linux) after it via `sfdisk`, then `dd` the
  `part_rootfs.ext2` into p2. (Model the partition geometry on the existing FAT
  layout; the 2026-05-31 ext2 doc notes the current FAT part at sector 2048.)
- Optionally add a `nvm.yaml` (model `ia32 nvm.yaml`) so the image build is
  declarative rather than ad-hoc.

**Exit:** a flashable SD image whose p2 the driver enumerates as
`/dev/mmcblk0p2` and mounts as ext2.

---

## Phase 5 — Pivot root to SD + boot integration

- `user.plo.yaml`: launch `app ... -x bcm2711-emmc;-r;/dev/mmcblk0p2:ext2 ddr ddr`
  **before** `psh`. The server's `portRegister(oid.port,"/")` (`sdstorage_srv.c:327`)
  overrides the dummyfs root once mounted. Keep `dummyfs-root` line until proven,
  then remove (it's the fallback root).
- **Ordering / race:** posixsrv retries `lookup("/")` (it tolerates a late root),
  but the storage server must register `/` before psh tries to exec from it.
  Use the `W` (wait-for-exit) vs `X` (async) discipline from the rc.psh model so
  startup is serialized (the #123-class device-node race).
- `psh -i /etc/rc.psh`; `/etc/rc.psh` (baked into the ext2 image) mounts
  `dummyfs -m /tmp` + binds `devfs /dev` — **not** `/` (root already mounted by
  the server), per the ia32 rootfs-overlay model.

**Exit:** box boots with `/` served by ext2-on-SD; `psh` runs binaries from the
SD rootfs; `ls /` shows the on-disk tree.

---

## Phase 6 — Write-persistence + reboot validation

- Mount r/w (`-c 0` write-back vs `-c 1` write-through — **decision below**),
  write a file from psh, **reboot**, confirm it persists.
- Validate fsync/sync path (`mtSync`) and that an unclean power-cut doesn't
  corrupt (ext2 has no journal — consider mount options / accept fsck-on-host for
  now; document the durability story).

**Exit:** persistent read-write `/` across reboots; this closes #120 and unblocks
#118 (busybox/coreutils live on the SD rootfs, built `--with-ports`).

---

## Sequencing

```
Phase 1 ACMD41 fix (#119)  ── BLOCKER, do first; everything gates on it
   └─ Phase 2 ext2 read-mount (de-risk FS)
        └─ Phase 3 build ext2 image ──┐
        └─ Phase 4 assemble SD image ─┴─ Phase 5 pivot root + rc.psh (#120)
                                            └─ Phase 6 write-persistence + reboot
                                                 └─ #118 busybox on SD rootfs
```

## Validation harness

- On-device: `scripts/test-cycle-netboot.sh --sd-boot` (kernel+syspage netboot is
  orthogonal; the SD card carries the rootfs). The working USB keyboard means psh
  can be driven interactively for `ls`/`cat`/write tests; HDMI snapshots capture
  state. `scripts/uart-summary.sh` for stage health.
- Every validated phase → `scripts/snapshot-integration-state.sh <slug>` manifest;
  `restore-integration-state.sh` rolls back.

## Risks / unknowns

- **Phase 1 may be a small fix (response-type flag) or a deeper SDHCI sequencing
  bug.** Confirm the sub-step on clean logs before committing a fix; do not
  fix-then-hope.
- ext2 has no journal — power-cut durability is limited; acceptable for bring-up,
  document it. littlefs/jffs2 are MTD-only (not block) so not candidates here.
- genext2fs host dependency must be installed (PEP 668 / no system pollution rules
  don't apply — it's a host build tool, install via the OS package manager).

## Open decisions for the user

1. **Cache mode:** write-back (`-c 0`, faster, riskier on power-cut) vs
   write-through (`-c 1`, default, safer). Recommend write-through for bring-up.
2. **First rootfs contents:** minimal (psh + core applets + servers) first, add
   **busybox** (`--with-ports`) once the mount is proven — or include busybox from
   the start. Recommend minimal-first to isolate FS bugs.
3. **Image-build style:** ad-hoc `assemble-rpi4b-sdimg.sh` vs declarative
   `nvm.yaml` (ia32 model). Recommend nvm.yaml for reproducibility, ad-hoc script
   acceptable for first light.
