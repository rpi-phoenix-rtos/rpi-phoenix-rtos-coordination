# NFS rootfs — overnight session progress (2026-06-07/08)

Autonomous night session. SD card in host → **netboot only** (no SD swaps). Goal: implement
NFS support per `docs/research/2026-06-07-nfs-rootfs-feasibility/IMPLEMENTATION-PLAN.md`
(T0→T1→T2→T3b→T3), then if NFS root boots, build all phoenix-rtos-ports into it + run them.
Then leftover time → non-SD open tasks.

## Status board

- [x] **§13 Host NFS server** — DONE. `nfs-kernel-server` installed; export
  `/srv/phoenix-rpi4-nfs` → `10.42.0.0/24` + `127.0.0.1` (rw,sync,no_subtree_check,
  no_root_squash,insecure,fsid=0) via `/etc/exports.d/phoenix-rpi4.exports`. NFSv4.1 self-test
  mount from host: read + write both OK. Populated: `etc/hostname`, `etc/nfs-smoke.txt`,
  `test/f{1,4095,4096,4097,32768,32769,1048576}` + `test/MANIFEST.sha256`, empty `bin/`.
  Netboot NIC `enx00e04c68013a` @ 10.42.0.1/24 (Pi DHCP .10–.20). NFS reachable on 10.42.0.1
  once netboot brings the NIC up.
- [x] **T0 libnfs.a cross-compile** — DONE. `sources/phoenix-rtos-ports/libnfs/` (port.def.sh +
  files/config.h + files/Makefile.phoenix). Static `libnfs.a` (NFSv3+v4 sync core, AArch64)
  builds clean; symbols verified; trivial consumer links. Notes: `T0-libnfs-build-notes.md`.
  Excluded: krb5/tls/MT/nlm/rquota (none needed for mount+rw+readdir+stat). Committed (ports bf46139).
- [x] **T1 program source** — `sources/phoenix-rtos-utils/nfs-smoke/nfs-smoke.c` written +
  API-verified against the cloned libnfs header (pread/pwrite = `(nfs,fh,buf,count,offset)`).
  Does: DHCP-wait via `/dev/ifstatus` (iface-agnostic, skip lo/wl/sc), nfs_init+v4+mount,
  open+pread `/etc/hostname`, then creat+pwrite+readback `/nfs-smoke-marker.txt`. v3 fallback.
- [x] **T1 integration** — DONE. nfs-smoke util links `-lnfs` (flat sysroot), bundled in
  loader.disk, launch line in user.plo.yaml (after lwip, before psh). Two-pass build: ports
  stage stages libnfs.a, then `--scope core --variant netboot`. Committed (utils aae3d35,
  ports bf46139, project 43f4c12). Notes: T1-integration-notes.md.
- [x] **T1 HW test — PASS (2026-06-08 ~23:16).** netboot log nfsT1:
  `interface bound ip=10.42.0.12` → `mounted 10.42.0.1:/ via NFSv4 in 505 ms` →
  `READ ok 17 bytes "phoenix-rpi4-nfs"` → `WRITE ok (21 bytes, readback MATCH)`. Host shows
  the Pi-written `/srv/phoenix-rpi4-nfs/nfs-smoke-marker.txt` = "phoenix-nfs-smoke-OK". Boot
  reached (psh)%, 0 faults. **NFS client on Phoenix Pi4 is HW-proven (mount+read+write).**
  NOTE: nfs_creat(0644) over v4 yields mode 000 on host (libnfs v4-creat mode quirk) — matters
  for T3b exec (+x) and ports; track it (may need nfs_chmod after creat, or v3).
- [x] **T2 + T3b wiring** — DONE (host-only build; HW UNVERIFIED). mt* fs server
  `sources/phoenix-rtos-filesystems/nfs/` (srv.c + nfs_node.{c,h} + nfs_ops.{c,h} + Makefile),
  dummyfs-shaped, links `-lnfs`. All core mt* ops implemented (lookup/open/close/read+symlink/
  write/truncate/getattr[All]/setattr/create/destroy/unlink/link/readdir/statfs). Mounts at
  `/nfstest` via `mtSetAttr(atDev)` splice (NOT root); `mkdir;/nfstest` + `nfs;/nfstest;...`
  launch lines added after lwip/nfs-smoke. chmod-after-creat fixes the v4 mode-000 bug; both
  loop + splice on 64 KB stacks (#120). Built `--scope core --variant netboot`: `prog.stripped/nfs`
  283 KB, bundled in loader.disk, launch lines embedded. Notes: T2-fs-server-notes.md.
  Orchestrator: watch UART for `nfs-fs: mounted at /nfstest` + `ls /nfstest`/`cat`/exec, 0 faults.
- [x] **T3b HW test — READ + WRITE PASS (2026-06-08 ~01:5x).** Boot-config dup-mkdir fixed
  (single `mkdir;/dev;/nfstest`; committed project 82d0ed3). Netboot: `nfs-fs: mounted at
  /nfstest`, psh reached, NFS fs server 0 faults. From psh: `ls /nfstest/test` lists all host
  files; `cat /nfstest/etc/hostname`="phoenix-rpi4-nfs"; `cp /nfstest/etc/hostname
  /nfstest/pi-copy.txt` → host shows pi-copy.txt (content match, mode rw-rw-rw via chmod fix);
  read-back matches. **NFS is a working read-write Phoenix filesystem at /nfstest.** (Two
  `Data Abort (EL0)` in process "usb" = the KNOWN intermittent USB HID-attach corruption
  [[project_usb_kbd_attach_abort]], far=wild ptr — UNRELATED to NFS, pre-existing, ~1/N boots.)
- [~] **T3b EXEC — mmap hypothesis DISPROVEN; likely stale test. Needs HW re-test.**
  Investigated host-only (`T3b-exec-fix-notes.md`). The loader does NOT mmap the fs:
  `proc_fileSpawn` (process.c:1266) → `vm_objectGet`/`proc_size` (mtGetAttr atSize) →
  `process_load` whose page faults are served by `object_fetch` (vm/object.c:174) =
  `proc_open`+`proc_read(off,4096)`+`proc_close` per page. The NFS server already
  implements every one of those ops correctly (proven by the working depth-2 `cat`
  through the mount). So an mmap/mtDevCtl handler would be DEAD CODE — **no NFS code
  change made.** Leading cause of the earlier ENOENT: the exec target
  `/nfstest/bin/nfs-smoke` did not exist when exec was first tried (night doc lists
  `bin/` as empty; binary mtime 01:52 is later/ambiguous). The staged binary is now
  byte-identical (`cmp` clean) to the syspage-loaded `nfs-smoke` → a valid loadable ELF.
  **Next (HW, orchestrator): the 3-step bisecting test in T3b-exec-fix-notes.md §5**
  (`ls /nfstest/bin` → `cat`/`stat /nfstest/bin/nfs-smoke` → exec it). All-pass = the
  wall was the empty-bin stale test → unblocks T3 + ports.
- [ ] **T3** NFS as rootfs (decide-before-register + fallback) — GATED on exec-from-NFS.
- [ ] **Ports** build phoenix-rtos-ports into the NFS root + run them — GATED on exec-from-NFS.

## Key facts / decisions
- OQ answers recorded in the plan (§12 resolutions): cache-OFF first; v4 preferred, v3 fallback
  OK; MT eventually required; uid0/no-security PoC; ignore licensing; proceed unattended with
  safe fallback.
- Boot config: `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`.
  Programs launched `app <BOOT_DEVICE> -x <prog>;<arg1>;<arg2> ddr ddr`. `/dev` bind happens
  before services; `/dev/ifstatus` (lwip) available after lwip launch (line 77).
- Serial-HW constraint: only ONE netboot at a time → HW tests run under the main orchestrator,
  host-only build/code work can be parallel subagents.
- Build/test loop: `./scripts/rebuild-rpi4b-fast.sh --variant netboot` (NOT --scope core unless a
  core/sibling changed — utils/ports are core objects, so a committed/uncommitted change there
  needs `--scope core`) → `./scripts/test-cycle-netboot.sh --label nfsT1 --capture-secs 180`.

## SD (#154) — PARKED (do not touch overnight; needs card swaps)
Root cause FOUND (writes succeed, Transfer-Complete IRQ never fires → CMD13-poll fix designed).
See `docs/inprogress/2026-06-07-sd-write-completion-rootcause.md`. sdcard.c has the reset-on-
timeout fix (keep) + a gated-OFF diagnostic. Resume tomorrow with the user for card swaps.
