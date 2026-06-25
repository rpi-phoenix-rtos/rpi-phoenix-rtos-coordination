# Unattended night handoff — 2026-06-24/25

You were away; I worked autonomously with subagent fan-out + serial HW validation.
Everything below is committed. UART/netboot was free (you were away) so I validated on HW.

## ✅ DELIVERED (validated on hardware + committed)

1. **Mailbox universal serialization — your headline ask. ROOT-CAUSED + FIXED + HW-VALIDATED.**
   The single BCM2711 VideoCore mailbox FIFO (0xfe00b880, ch8) was driven by 5+ processes with
   no cross-process lock; each popped+discarded others' responses → transient failures that can
   break many things non-deterministically (your insight was right). Fix = a single serialized
   server `misc/rpi4-vcmbox/` (owns the FIFO + one low VC-addressable bounce buffer + internal
   retry + a 3-way failure-mode discriminator) and a `libvcmbox` client lib. Converted + HW-proven:
   - **thermal** — the reported `mailbox temperature read failed` is GONE (`T=35012 mC`).
   - **genet MAC** — correct `dc:a6:32:3c:dd:f1` via the server, link+IP up.
   - **usb VL805 bring-up** — 2/2 clean boots, full enumeration (kbd+mouse), 0 faults.
   - **sdio** — converted + build-verified (full validation needs an attended SD-swap).
   Docs: `2026-06-24-vcmbox-mailbox-serialization.md`. Commits across devices/lwip/project/coord.

2. **vkQuake on-HW Tier-1 milestone.** vkquake-phoenix (22.5 MB, real SPIR-V — 41 shaders) boot-
   launched: **Vulkan fully initializes on the V3D** (instance/enumerate/device/queue rc=0, fb0
   1920×1080 scanout), pak0.pak found over NFS, then a **NULL+0x70 deref at pc=0x4c5d20 right
   after VID_Init** (the no-WSI first-frame path). Specific next step in
   `2026-06-25-vkquake-hw-tier1-result.md`. GPU-binary swap reverted; GL flagship untouched.

3. **GPU build durability.** `build-gl-phoenix.py` had a mesa generated-sources regression (libGL
   4-FAIL → flagship couldn't relink); FIXED → libGL 0-FAIL (325 objs), libquakespasm relinks.

4. **X11 desktop (#30).** `startx desktop` → twm + a managed/draggable xeyes. Root-caused twm's
   crash (libXt `malloc0_returns_null=no` vs Phoenix `malloc(0)`==NULL → rebuilt `=yes`). kbd0-free
   ordering verified by source (fbcon-disable precedes the kbd open). HW mouse/keypress sign-off
   is yours to eyeball.

5. **Publishability cleanup.** Removed disproved/throwaway diagnostics (pl011-tty #126 mouse reader,
   v3d-winsys render-stall dump) + resolved their markers; HW-smoke clean (USB+kbd+console intact).
   Cataloged the statistically-sensitive USB #129 TODOs for a reviewed pass (not touched).

6. **Userland ports verified from NFS.** openssl (`version` — zero-stdout anomaly RESOLVED), curl
   7.64.1+mbedTLS, dropbearkey RSA gen, and the new fs_mark all run from the NFS rootfs.

## 🤔 DECISIONS I made autonomously (reverse if you disagree)

- **Mailbox fix = userspace server, not a kernel primitive (TD-15).** Netboot-recoverable (a kernel
  change risks bricking boot unattended); idiomatic Phoenix; single bounce buffer also fixes the
  buffer-above-VC-range mode. Staged rollout, validated per slice.
- **v3d power-on NOT converted to vcmbox.** It runs late (GPU-binary startup, after other mailbox
  users finish) so its race risk is minimal, and it's a fiddly host-built-lib cross-link. Deferred
  as low-priority; documented.
- **vkQuake HW = one brave Tier-1 attempt**, then reverted the swap to keep the GL flagship intact.
- **Flagship (rpi4-quake) left autostart-OFF** (the `TEMP-NO-QUAKE-AUTOSTART` state you set) — your
  recent testing is X11, which needs no Quake render loop. Re-enable = uncomment the two
  `rpi4-quake` launch lines in `user.plo.yaml` + `rebuild --scope core` (now builds clean).

## ⏸ PARKED / needs you or a careful attended session

- **#31 logging (/var/log + debug/user build mode)** — NOT done. It touches console/klog internals
  (my unattended-scoping rule flags console-internals as attended: if it breaks you lose console
  visibility). Safe additive slice possible (a syslogd mirroring klog to /var/log + a `logread`
  command, console unchanged); the *suppress-console-in-user-mode* part is the delicate bit. Want
  me to do the additive slice next, or hold the whole thing for an attended session?
- **NFS #156 first-read-ENOENT — RE-ROOT-CAUSED tonight: it's a BOOT-ORDER RACE, not the dircache**
  (the dircache is already off at HEAD, so the old "disable dircache → ERANGE" angle is moot). psh
  launches as a *sibling* of the NFS takeover server without gating on it, so the first command(s)
  run while "/" is still the dummyfs RAM root, before takeover does `portRegister("/")`. Proven
  across 3 UART logs (the ENOENT always precedes `nfs-fs: registered / (takeover)`; every access
  after that line succeeds first-try). **Practical impact is low** — interactive use is past the
  race by the time you type; scripted use just waits for the takeover line (documented protocol).
  Real fix = gate psh on takeover-complete (a plo/psh boot-order change — fiddly + brick-risk per
  the dup-program-brick lesson), DEFERRED. Diagnosis: `2026-06-25-nfs-156-first-read.md`. Code
  shipped = a comment correction only (filesystems `fafa024`, build-verified, no behavior change).
- **vkQuake crash (NULL+0x70 @ 0x4c5d20)** — needs a debug-symbol build to name the function, then
  fix the no-WSI surface/swapchain shim. Next focused session.
- **fb0 mmap (#5), NFS MT server (#7), TD-05 + kernel TD items** — kernel/complex; deferred.
- **RTC/SNTP auto-run** — needs a host-side NTP server on the netboot link.
- **sdio vcmbox validation** — needs an attended SD-card swap (the pre-bind devfs-resolve branch).

## ❓ For you when you're back
1. Re-enable the Quake flagship autostart, or keep it off while you test X11?
2. #31 logging: additive slice now, or hold for attended (console-internals risk)?
3. vkQuake: want me to continue the crash debug (debug build → name 0x4c5d20)?
