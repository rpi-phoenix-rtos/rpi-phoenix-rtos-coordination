# v3d screen-create on HW — blocked by NFS-exec wall, pivot to boot-launch (2026-06-11)

## What we tried
After the Path-C Phase-2 link milestone (the full Mesa v3d driver links into a real
aarch64-phoenix ELF, `/tmp/v3dphx-harness`), the plan was to run the screen-create
harness on the Pi as the first runtime contact. Chosen fast path: stage the static ELF
into the NFS rootfs (`/srv/phoenix-rpi4-nfs/bin/v3dscreen`) and exec it from psh.

## The wall (5 HW cycles)
**Every external binary exec'd from NFS via psh produced no output and ran no code** —
not just our harness, but `busybox` and `micropython` too:
- `/bin/v3dscreen` → returns to prompt, no `harness: entering` marker, no fault.
- `/bin/hellophx` (trivial 170 KB standalone-linked printf) → silent.
- `/bin/busybox echo X` (abs path) → silent. `busybox`/`hellophx` (bare) → `psh: not found`
  (psh does no PATH search; it execs absolute paths only).
- **Discriminator (advisor):** `/bin/micropython -c "print('MP_OK'); open('/mpran.txt','w').close()"`
  → **neither** `MP_OK` on UART **nor** the `/mpran.txt` sentinel on the NFS server.

The sentinel write is independent of stdout, so its absence proves the process **never
executed** (or died before its first statement) — this is an **exec failure, not stdout
loss.** `ls`/`ps` "worked" only because they are psh **builtins** (no child process).

## Diagnosis
- micropython was **HW-confirmed running from NFS** on 2026-06-10 (plan log / memory), so
  this is a **regression in the current image**, not the driver or our build method.
- Relevant: **TD-13** ("post-spawn user-mode handoff produces no observable output") +
  the NFS-takeover **`/dev` re-bind** seen in every boot log
  (`nfs-fs: re-bound /dev (takeover, devfs port=3)`). A process spawned *after* the
  takeover re-binds `/dev` may fail to open its console/exec path. Candidate causes:
  recent kernel changes since 2026-06-10 (CNTKCTL, NFS #156 cleanup) or the nfsroot
  rebuild. **This is a Phoenix process/NFS-internals issue, orthogonal to the GPU port**
  — and per advisor guidance, NOT to be ratholed on here.

## What is NOT in doubt
- The Mesa v3d driver **links** for Phoenix (coord 607d82f). Unchanged.
- The GET_PARAM IDENT decode is **host-validated** (ver=42, vpm=64K, 8 QPU) — the
  most-likely screen-create failure point, proven off-device (coord 6ce5fa8).
- screen-create is MMIO-free (GET_PARAM-only, no BO) — so it needs no V3D power-on.

## Pivot: boot-launch (the proven output path)
`rpi4-v3d-scout` is launched at boot (before NFS takeover, `/dev`/console intact) and
prints to UART **reliably** every cycle. The screen-create harness should run the same
way, sidestepping the NFS-exec wall entirely. Two prerequisites:

1. **Shrink the binary (gc-sections).** 12 MB static is too big to bundle in loader.disk
   (TFTP) sanely. Rebuild core + aux + driver with `-ffunction-sections -fdata-sections`,
   link `-Wl,--gc-sections`. `main → v3d_screen_create → destroy` makes most of NIR/the
   compiler/draw unreachable → expect a few MB. (Needs the core-lib build turned into a
   committed recipe that takes the section flags — currently core is a /tmp artifact.)
2. **Build via `binary.mk` as a boot-launched program** (proper Phoenix link: `-mcpu=
   cortex-a72 -mstrict-align -mno-outline-atomics` — our /tmp objects were built with
   stripped `-m*`/toolchain-default flags, so a strict-align rebuild is also the correct
   ABI for on-device). Add to the boot config like the scout; read result over UART.
   Adopt **file-based readout too** (advisor) for crash-progress once an fs is writable.

Alternative if boot-bundle proves unwieldy: fix the NFS-exec regression (bisect the
kernel change vs the /dev re-bind) — but that's the rathole; boot-launch is the
GPU-forward move.

## Status
HW validation of screen-create DEFERRED to the boot-launch path. The link + IDENT-decode
milestones stand.

## Update 2 (2026-06-11): NFS-as-root is too fragile; gc-sections won't shrink; go netboot boot-launch
- **gc-sections result:** 12.0 -> 11.3 MiB only. `v3d_screen_create` references nearly
  all of Mesa, so the binary is **inherently ~11 MB** — can't shrink for bundling.
- **NFS-as-root is intermittent**, two independent failure modes seen across cycles:
  (a) takeover **succeeds** (`registered / (takeover)`) but external exec is **silent**
  (process never runs — sentinel test proved it); (b) takeover **aborts**
  (`nfs-fs: takeover aborted: mount …; keeping RAM root /`) → RAM root → everything
  "not found". Layered NFS flakiness — unfit for reliable HW iteration of the GPU work.
- **Decisive pivot: NETBOOT boot-launch.** The proven, deterministic path is a
  kernel-launched program via `user.plo.yaml`:
  `app {{ env.BOOT_DEVICE }} -x <name> ddr ddr` (see the `rpi4-v3d-scout` line ~141,
  gated to the **netboot** variant). Netboot has NO NFS takeover and NO exec-from-fs —
  the program is TFTP'd into the syspage and the kernel starts it directly, printing to
  UART reliably (the scout proves this every boot). screen-create is MMIO-free, so it
  needs no filesystem at all.
- **Plan:** create a `sources/phoenix-rtos-devices/misc/rpi4-v3d-mesa/` program
  (binary.mk, `LOCAL_SRCS = harness main`, `LOCAL_CFLAGS = -I` the Mesa src + the /tmp
  generated headers + shim-include + compat, `LOCAL_LDFLAGS = /tmp/libv3d-phoenix.a -lm`
  — the lib is now ABI-compatible: built with -mcpu=cortex-a72 -mstrict-align). Register
  it with an `app … -x rpi4-v3d-mesa ddr ddr` line (netboot variant) next to the scout.
  Rebuild the netboot image (~15 MB with the 11 MB program; verify plo/TFTP handle it),
  netboot, read `v3d screen-create harness: PASS` (or the crash marker) over UART.
- **Note for GLQuake later:** the GL/quakespasm binary + pak0.pak (~18 MB assets) WILL
  need filesystem-exec/read (SD likely more stable than NFS) — but that's a later
  problem; boot-launch gets the Mesa driver running on HW now (screen-create -> triangle).
