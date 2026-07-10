# Pending user tasks / decisions / questions

Items that need the user (hardware, judgment calls, or external actions). Claude keeps
working around these — this is a queue, not a blocker.

## Hardware tests (need the Pi + card swaps)

- [ ] **Quake flicker fix — boot the re-prepared SD.** Card `/dev/sda` now holds
  `rpi4b-sd-flickerfix-clean-2026-07-06.img`: clean mcopy-built FAT (fsck-clean),
  `ae9a8e` firmware (the flicker fix, virt_h=3240/TRIPLE-BUFFER), proven scanout-ON
  `rpi4-quake` + pak0.pak + Xphoenix. Boot it and run `rpi4-quake` — confirm the flicker
  is gone. **Boot is untested** (the earlier fwfix card failed only because a loop-mount
  `cp` corrupted the FAT — `Read start4.elf failed / bad cluster id: 0`; this image was
  built with `mcopy` and fsck's clean, so it should boot).
- [ ] Re-test the full NFS dev environment after any host reboot (see the NFS ERANGE note
  below).

## Decisions / questions

- [ ] **Durable NFS fix (design decision).** Root cause of the "NFS regression" was stale
  NFSv4 client state on the host nfsd from many rapid reboots (NFS4ERR_EXPIRED on OPEN →
  ERANGE). Immediate fix = `sudo systemctl restart nfs-server`. Durable options: (a) make
  nfs-fs/libnfs recover from NFS4ERR_EXPIRED (re-establish clientid + retry) and/or send
  periodic RENEW; (b) accept the manual restart. Which do you want?
- [ ] **Firmware pin bump policy.** `PI_FW_REF` is pinned to `41f4808` (firmware that grants
  the 3x virtual framebuffer). Any future bump must re-verify the winsys scanout-init line
  still reports TRIPLE-BUFFER.

## Known build breakage (non-blocking, found during the session)

- [ ] `lighttpd` port fails to prepare on a clean tree (missing `/etc`). Not on the RPi4
  critical path but breaks a fully-clean `--with-ports` build.

## Review-driven decisions (2026-07-06/07)

- [ ] **vkQuake/V3DV publication scope.** It's WIP, off-by-default, not HW-validated, and its
  Phoenix edits carry live bring-up bisector diagnostics (magenta test rects etc.). Decide:
  (a) publish + clean the diagnostics, (b) publish as explicitly experimental, or (c) exclude
  from the initial public release. I left it untouched (your active debugging state).
- [ ] **bcm2711-emmc ↔ zynq7000-sdcard duplication.** The SD driver copies sdcard.c/
  sdstorage_*.c verbatim (documented TODO). De-dup into a shared SDHCI core lib is a real
  refactor — worth doing before upstreaming, but it's a structural change I'd want you to
  greenlight (and it touches two drivers). Quantify + decide.
- [ ] **log/log.c klog→UART mirror default.** It's ON for ALL boards by default (opt-out via
  RPI4_LOG_TO_FILE). Should it be opt-IN (RPi4 only) to avoid changing every board's console?

## LEGAL BLOCKERS — must resolve before publication (HIGH)

The repo top-level LICENSE is **BSD-3-Clause**, but several subtrees contain/derive from
GPL/Linux code with no GPL delineation. These are the "don't want to be accused" items:

- [ ] **fbdev.c GPL keycode table** (`tools/x11-port/ddx/fbdev.c:627`) — `hidToEvdev[256]` is
  byte-for-byte Linux `usb_kbd_keycode` (GPL-2.0-or-later), with a literal "Verbatim from the
  kernel" comment. The VALUES are factual (HID Usage Tables + evdev codes — public specs, merger
  doctrine), but shipping Linux's GPL array + that comment under BSD is exposure. Options: (a)
  clean-room reimplement from the USB HID Usage Tables spec (I can, but the X keyboard needs HW
  re-validation which I can't do now), (b) legal sign-off that it's factual data, (c) isolate.
  **I did NOT modify it** — needs your legal call + a HW kbd test. Do not ship as-is.
- [ ] **quakespasm-port / vkquake-port are GPLv2 derivatives** under a BSD repo. Every file in
  `tools/quakespasm-port/platform/` (7 files) links QuakeSpasm + uses its headers; `pl_phoenix_
  stubs.c:31-92` copies `net_bsd.c` tables verbatim. None carry GPL headers. `tools/vkquake-port/`
  similarly. DECISION: how to license these — (a) mark each GPLv2 + add a subtree LICENSE, (b)
  move the glue into the `external/quakespasm`+`external/vkquake` GPL forks, (c) dual-license.
  Then fix the top-level LICENSE to note the GPL subtrees. (I can add the GPLv2 headers once you
  pick the approach.)
- [ ] **Top-level LICENSE (BSD-3-Clause) vs GPL subtrees** — reconcile once the above is decided.

## HIGH correctness (fixing where safe + testable)

- [ ] **preinit.plo.yaml gpu_mem/ddr-map mismatch** (`aarch64a72-generic-rpi4b/preinit.plo.yaml`)
  — ddr map ends 0x3b400000 (gpu_mem=76) but config.txt now sets gpu_mem=128 (GPU base
  0x38000000) → kernel gets 52MB of GPU-reserved RAM. Fix = lower map end to 0x38000000. It's a
  memory-map change → I'll apply it but it MUST be boot-validated (can't boot-test now).

## Flagged fixes needing your care (HW-untestable / concurrency)

- [ ] **posixsrv special.c random_hwrngFd race** (med, latent) — worker threads mutate the
  static fd without a lock: lazy-init double-open (banner x2 + fd leak) and close-under-read
  on the hwrng-failure path. Clean fix = a special.c-local mutex around the init + close
  transitions (posixsrv_common.lock is static to posixsrv.c, not reachable from special.c),
  or refcount the fd. Latent (failure-path only) + HW-untestable threading → left for you.

- [ ] **interrupts.h TIMER_WAKEUP_IRQ enum-vs-#ifdef** (med, subtle) — it's an `enum` constant
  but proc/threads.c gates the SMP timer-wakeup coalescing-IPI facility behind
  `#ifdef TIMER_WAKEUP_IRQ`, which is ALWAYS false → the facility never compiles. Current 4-core
  SMP works via the fallback (secondaries reprogram the timer directly). DECISION: either
  (a) `#define TIMER_WAKEUP_IRQ 1U` to ENABLE it (changes SMP wakeup behavior → needs 4-core HW
  validation; the never-compiled code may have latent bugs), or (b) delete the dead enum + the
  5 #ifdef blocks (behavior-preserving cleanup, discards the intended optimization). Your call.

- [ ] **pcie-server dead BCM2711 duplicate** (med) — pcie/server/pcie.c carries a ~600-line
  BCM2711 bring-up block that is a dead, older, buggier duplicate of the LIVE
  usb/xhci/bcm2711-pcie.c (incl. the B1 bcm2711EncodeBar2Size 4GiB->1MB truncation and heavy
  diag scaffolding). It's never compiled for rpi4. RECOMMEND deleting the BCM2711 block from
  pcie-server (the live PCIe init is in usb/xhci); confirm pcie-server's BCM2711 path isn't
  meant to be revived, then I'll remove it. Large deletion → your greenlight first.

- [ ] **tools-v3d gl_stubs.c posix_memalign ignores alignment** (med, latent) — returns plain
  malloc() (16-byte aligned) for any request. Primary consumer (Mesa util_sparse_array,
  align 64) is already worked around (#undef HAVE_POSIX_MEMALIGN). Recommended fix: validate
  alignment (power-of-2, >= sizeof(void*)) and return EINVAL for alignment > 16 rather than
  silently returning under-aligned memory (no free()-compatible aligned allocator in libphoenix).
  Left unfixed: it's in the GPU render path you're testing + I can't HW-validate a behavior change.

- [ ] **mesa v3d_resource.c:909 scanout heuristic** (med) — the size-only heuristic
  (width>=1024 && height>=768) forces RASTER + SCANOUT-backing + Y-flip on ANY render target,
  including large sampled render-to-textures, not just the framebuffer. Could mis-handle a
  large RTT. This is the PROVEN render path directly related to the flicker you're testing —
  I deliberately did NOT change it. Review alongside the flicker fix; likely wants a
  bind-flag check (PIPE_BIND_DISPLAY_TARGET/SCANOUT) rather than a size heuristic.

## Low-severity, HW-sensitive (kbd path) — flagged not applied
- [ ] usbkbd.c:619 raw-mode read not aligned to 8-byte HID report boundaries (usbmouse does
  align). Fix = round read len down to a multiple of usbkbd_reportSize. Kbd read path → HW test.
- [ ] usbkbd.c:653 mtClose bypasses the owning-pid check (any process can close another
  client's /dev/kbd). Fix = drop mtClose from the exemption. Could affect X11/psh close flows → HW test.

## Debug data-watchpoint facility (Route A) — decision cluster
- [ ] cpu.c:354 arms the watchpoint for EL0+EL1 (PAC=0b11) but only EXC_WATCHPOINT_EL0 has a
  handler → an EL1 hit reboots under NDEBUG. cpu.c:322 + generic.h:41 bake a debug-only
  pctl_watchpoint into the PUBLISHED platformctl_t ABI with transient #121/Route-A comments and
  no TODO(TD-nn) marker. DECISION: keep the data-watchpoint as a supported debug feature
  (then: fix the EL0/EL1 arming-vs-handler mismatch, de-scope the ABI comments, add a TD entry)
  OR gate/remove it before publishing the ABI. It's a debug facility → your call.

- [ ] **plo hal/aarch64/generic/_init.S diagnostic vector table** (med) — the generic (rpi4)
  _vector_table wires all slots to tag-print-and-halt / a disproved-hypothesis ESR/ELR/FAR dump
  ("TD-diag", not a tracked marker), never reaching the shared _exceptions_dispatch it includes.
  Dead at runtime (DAIF stays masked, plo polls) but ships publicly. Fix = a real vector table
  branching to _exceptions_dispatch (mirror hal/aarch64/zynqmp/_init.S), or a minimal marked
  halt handler. Boot-critical (bootloader) → needs boot validation; flagged.

## fs-nfs low items
- [ ] dummyfs/srv.c ~219-230: an edited block is space-indented in a tab-indented file
  (botched-paste look, 15 scattered lines). Best fixed mechanically (clang-format the file, or
  `unexpand --first-only -t8`) rather than by hand — flagged to avoid a new inconsistency.
- [ ] nfs/nfs_ops.c:95 (`.`/`..` lookups materialize distinct id-nodes → object aliased under
  multiple ids). Real but touches NFS path resolution (HW-sensitive) → validate on HW.
- [ ] nfs/nfs_ops.c:39 nfs_err maps libnfs rc==-1 to -EIO (may mask -EPERM); nfs_ops.c:692
  readdir is O(N^2) per listing (acceptable per the single-entry contract) — minor; note in comments.

## Judgment calls (bounded diagnostics — keep vs gate)
- [ ] dev-storage sdcard.c SDREADDIAG: a bounded (max 3), read-failure-path diagnostic
  (HS50-margin-vs-wedged discriminator), default-on. NOT disproved-hypothesis slop — it's a
  useful field diagnostic. Inconsistent with the gated SDCARD_DIAG_CLOCKSWEEP. Keep as-is, or
  gate for consistency — your call (I left it: it's a deliberate capped diagnostic, not spam).

## Licensing decisions (resolved-in-code, confirm the choice)
- [x] **fbdev.c GPL keycode table** — RESOLVED (commit 95a81ce). Replaced the verbatim
  Linux-kernel `usb_kbd_keycode[]` with a FreeBSD BSD-2-Clause reproduction
  (`tools/x11-port/ddx/hid_evdev_map.h`). Core keys byte-verified; fbdev.c compiles.
- [x] **Quake/vkQuake glue licensing** — RESOLVED (commit db25415). GPL-2.0-or-later
  SPDX headers + COPYING + README on `tools/{quakespasm,vkquake}-port/`; carved out of
  the BSD claim in LICENSING.md/README.md.
  - [ ] **CONFIRM the license choice**: I marked the *author-written* glue GPL-2.0-or-later
    (conservative — it links into the GPL engines). If you'd rather license *your* glue
    permissively and rely on the fact it's only combined with GPL at build time, that's a
    legal call for you. GPLv2 is the safe default and what "not accused" points to.
  - [ ] **Publication-time relocation (needs GitHub)**: your model is "glue lives in the
    external forks." That requires forking sezero/quakespasm + Novum/vkQuake under your
    account, committing the now-headered glue there, repointing the `bootstrap-linux-host.sh`
    clone URLs, and pushing. It's deferred because (a) you said local-commits-only/no-github,
    and (b) `external/` currently clones *upstream* (not a fork) and is gitignored, so a
    relocation now would break a fresh-checkout build. The headers travel with the files, so
    this work isn't wasted when you do relocate.
