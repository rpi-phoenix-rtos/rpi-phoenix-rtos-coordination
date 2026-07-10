# Low findings (58) — polish/quality, address last

- [typo] kernel-hal-mem hal/aarch64/pmap.c:923 — Two statements collapsed onto one line (mem.min and mem.max) — merge/formatting botch
- [correctness] kernel-hal-core hal/aarch64/cpu.c:354 — Watchpoint armed for EL0+EL1 (PAC=0b11) but only EXC_WATCHPOINT_EL0 gets the custom handler, so an EL1 hit reboots under NDEBUG
- [hack] kernel-hal-core hal/aarch64/cpu.c:322 — Debug-only watchpoint feature ships publicly through a permanent platformctl ABI with no TD marker
- [hack] kernel-hal-rest hal/aarch64/generic/generic.c:128 — _hal_cpuInit ends with a large disproved-hypothesis narrative comment describing a disabled barrier and abandoned init-order workaround
- [quality] kernel-hal-rest hal/aarch64/arch/cpu.h:89 — hal_cpuEnableInterrupts unmasks only I (daifClr #2) while hal_cpuDisableInterrupts masks I+F (daifSet #3) — intentional but asymmetric
- [hack] kernel-vm-proc vm/vm.c:43 — Boot-trace hal_consolePrint scaffolding left in _vm_init() and _map_init()
- [hack] kernel-vm-proc proc/process.c:578 — TEMP-NOMEM-DIAG lib_printf probes scattered through process_load/process_exec have no TD marker
- [hack] kernel-vm-proc include/arch/aarch64/generic/generic.h:41 — Debug-only pctl_watchpoint member baked into published platformctl_t ABI with a transient-investigation comment
- [quality] kernel-vm-proc syscalls.c:849 — Pointless err-variable churn in syscalls_msgSend and syscalls_lookup
- [duplication] plo hal/aarch64/cache.c:30 — EL-banked SCTLR read/write accessors are duplicated verbatim between cache.c and mmu.c
- [quality] plo hal/aarch64/generic/hal.c:271 — Signed/unsigned comparison in hal_memoryGetNextEntry loop bound
- [hack] plo-rest _startc.c:53 — Heap-zeroing diagnostic ships to public release mislabeled as TD-05 and without a TODO(TD-NN) code marker
- [quality] plo-rest ld/aarch64a72-generic.ldt:6 — Header comment references a 1 GB Pi 4B while the body/SIZE_DDR now describe the 4 GB layout
- [quality] dev-tty tty/usbkbd/usbkbd.c:1192 — Dead local `woke` in the kbd bridge putchar loop (never written)
- [quality] dev-tty tty/usbkbd/usbkbd.c:619 — Raw-mode keyboard read does not align to 8-byte report boundaries (mouse driver does)
- [correctness] dev-tty tty/usbkbd/usbkbd.c:653 — mtClose bypasses the owning-pid check, letting any process close another client's keyboard
- [legal] dev-tty tty/pl011-tty/teken/teken_state.h:1 — Generated teken table carries no license/provenance line
- [correctness] dev-misc misc/rpi4-vcmbox/rpi4-vcmbox.c:1149 — Write-FULL timeout in vcmbox_fifoRoundtrip returns the mislabeled outcome MBOX_EMPTY
- [correctness] dev-misc misc/rpi4-ipcprobe/rpi4-ipcprobe.c:420 — Server-side write(cfd, "PONG", 4) return value is unchecked in the named-socket probe
- [quality] dev-storage storage/bcm2711-emmc/sdcard.c:190 — sdhost_allocDMA comment says "Cap at 64 KiB (128 blocks)" but SDCARD_MAX_TRANSFER is 128 KiB
- [quality] dev-storage storage/bcm2711-emmc/sdstorage_dev.c:45 — BLK_CACHE_SECSIZE comment narrates 16 KiB / 32 blocks then defines the macro as 128 KiB
- [hack] dev-storage storage/bcm2711-emmc/sdcard.c:474 — Default-on SDREADDIAG printf diagnostics ship in the released driver (un-gated, unlike the SDCARD_DIAG_CLOCKSWEEP sweep)
- [correctness] dev-usb-pcie usb/xhci/xhci.c:4578 — xhci_allocSlotSpace overwrites the shared xhci->inputCtx on every call, leaking the previous allocation for each behind-hub slot
- [quality] dev-usb-pcie usb/xhci/xhci.c:3421 — xhci_capProbe returns -ENOSYS to signal SUCCESS, decoded by xhci_init as the go-ahead path
- [correctness] dev-video-gpio-audio-sensors audio/rpi4-audio/rpi4-audio.c:393 — Bare counter delay loop with no volatile access can be optimized away, giving zero DMA settle time
- [legal] dev-video-gpio-audio-sensors audio/rpi4-audio/rpi4-audio.c:8 — Header credits an external bare-metal tutorial ('rpi4os.com part9-sound') as the source of the bring-up sequence — confirm no code was lifted and note the tutorial's license
- [correctness] lwip drivers/bcm-genet-regs.h:172 — MDIO_READ_FAILED is defined as (1u << 29), colliding with MDIO_START_BUSY and mismatching Linux's read-fail bit
- [typo] lwip drivers/bcm-genet.c:288 — genet_readMac comment describes the UMAC_MAC0/MAC1 byte layout backwards
- [quality] lwip drivers/bcm-genet-regs.h:173 — Stale 'to be confirmed against hardware in Tier 1' comment on a now-proven MDIO bit
- [quality] fs-nfs dummyfs/srv.c:219 — Edited dummyfs main() block is space-indented, breaking the file's tab indentation (botched-edit appearance)
- [correctness] fs-nfs nfs/nfs_ops.c:95 — nfs_ops_lookup materializes distinct id-nodes for '.'/'..' path components, aliasing one object under multiple ids
- [correctness] fs-nfs nfs/nfs_ops.c:39 — nfs_err maps libnfs rc==-1 to -EIO, masking a genuine -EPERM
- [quality] fs-nfs nfs/nfs_ops.c:692 — readdir reopens+rewalks the directory on every entry (O(N^2) per full listing)
- [quality] libphoenix include/termios.h:251 — Comment on self-referential c_oflag macros overstates that they work in #if expressions
- [quality] utils nfs-smoke/nfs-smoke.c:208 — Dead vlen computation left over from a refactor in wait_for_dhcp_lease
- [correctness] utils nfs-smoke/nfs-smoke.c:299 — v3-mount-failure path returns without nfs_destroy_context, unlike sibling error paths
- [correctness] usb-stack usb/dev.c:176 — hub->portEnumFails is leaked on hub teardown (never freed in usb_devFree)
- [hack] usb-stack usb/mem.c:419 — usb_free corruption guard only validates buf->head, not the rest of the coalescing walk
- [typo] ports xterm/patches/xterm-396-phoenix.patch:976 — DEFSHELL_NAME comment self-contradicts the netboot rootfs layout it is explaining
- [hack] ports windowmaker/port.def.sh:508 — Redundant -D_SC_LINE_MAX=5 build define now that _SC_LINE_MAX is committed to libphoenix unistd.h
- [typo] ports libnfs/files/config.h:145 — HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC comment references tv_sec instead of tv_nsec
- [quality] build port_manager/port_internal.subr:28 — aarch64 branch leaves HOST_TARGET carrying the core suffix (aarch64a72), inconsistent with the arm/sparc normalization
- [hack] project _projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S:2178 — Leftover SMP-D-3 diagnostic block writes per-CPU markers to PA 0x40+cpu*4 with no TD marker
- [quality] project _projects/aarch64a72-generic-rpi4b/user.plo.yaml:2890 — Commented-out TEMP launch lines and an inconsistent default('netboot') left in the boot script
- [correctness] project _projects/aarch64a72-generic-rpi4b/phoenix-kernel8-reloc.S:2513 — Forward byte/word copy of the plo payload to a higher dest address is unsafe if payload ever exceeds the src/dest gap
- [typo] tools-v3d tools/v3d-driver-port/v3d_phoenix_winsys.c:142 — Contradictory comments on the binner-overflow pool size (64 MiB vs 32 MiB)
- [legal] tools-v3d tools/v3d-driver-port/v3d_phoenix_winsys.c:694 — Comments closely paraphrase GPL Linux v3d driver comments (pre-publication rewording advised)
- [hack] tools-quakespasm tools/quakespasm-port/platform/pl_phoenix_in.c:285 — Leftover mouse-packet debug logging tied to already-resolved task #24 fires unconditionally on every boot
- [typo] tools-quakespasm tools/quakespasm-port/platform/pl_phoenix_vid.c:517 — Stale comment claims 'winding compensated by glFrontFace(GL_CCW) below' but the code uses GL_CW and there is no such call
- [quality] tools-quakespasm tools/quakespasm-port/platform/pl_phoenix_vid.c:48 — Duplicate #include <sys/ioctl.h> and duplicate extern of v3d_phoenix_scanout_active; redundant VID_Shutdown prototype after its definition
- [hack] tools-x11-other tools/vkquake-port/platform/pl_phoenix_in.c:291 — Leftover one-time mouse-packet debug logging (task #24) with no TODO marker
- [quality] tools-x11-other tools/x11-port/ddx/fbdev.c:573 — Input-section header comment describes an InputThreadRegisterDev/SetNotifyFd model the code deliberately does NOT use
- [correctness] tools-x11-other tools/x11-port/ddx/fbdev.c:297 — Row-by-row shadow blit reads a full device pitch from a possibly-narrower shadow stride
- [quality] ext-mesa-v3d src/util/os_memory_aligned.h:47 — Unconditional `#undef HAVE_POSIX_MEMALIGN` silently changes all non-Phoenix builds
- [hack] ext-mesa-v3d src/gallium/drivers/v3d/v3d_resource.c:113 — Dead cacheable-readback infra retained with a rationale that does not hold inside Mesa, plus three stacked disproved-hypothesis comment blocks
- [typo] ext-quakespasm Quake/gl_screen.c:986 — Capture code writes .tga files but log strings and comments say .png
- [quality] ext-quakespasm Quake/r_part.c:48 — Stale doc reference: comment points to docs/inprogress/... but the file was moved to docs/done/
- [hack] ext-quakespasm Quake/gl_screen.c:918 — Debug/harness code (raw TCP frame sink, blocking connect on render path, unchecked inet_addr, leaked static IBO/socket) shipped in the engine

---

## Resolution log — 2026-07-10 session

**Fixed this session (verified: core rebuild exit 0 + image export OK; tools via cross-compile):**
- tools quakespasm/vkquake pl_phoenix_in.c — removed leftover task-#24 mouse debug logging
- tools quakespasm pl_phoenix_vid.c — dup #include, dup extern, redundant VID_Shutdown proto, stale glFrontFace comment
- tools-v3d winsys — 64-vs-32 MiB prose; reworded GPL-Linux-quoted comment
- tools-x11 fbdev.c:573 — rewrote input comment to the real timer-drain model (+ fbdev GPL keycode table → FreeBSD BSD-2-Clause, commit 95a81ce)
- dev-misc vcmbox — added MBOX_WRFULL outcome (was mislabeled MBOX_EMPTY)
- dev-misc ipcprobe — checked server-side write(PONG)
- dev-audio rpi4-audio.c:8 [legal] — reworded rpi4os.com tutorial credit (original driver; shared HW sequence = datasheet facts; no code lifted)
- plo hal.c — size_t loop index (sign-compare); ldt header 1GB-vs-4GB note clarified
- libphoenix termios.h — corrected self-referential c_oflag macro comment
- fs-nfs dummyfs srv.c — fixed botched space-indentation in main()
- ports libnfs config.h (tv_sec→tv_nsec comment); windowmaker port.def.sh (dropped redundant -D_SC_LINE_MAX=5)

**Intentional — won't-fix (documented rationale holds):**
- fs-nfs nfs_ops.c nfs_err rc==-1→EIO — libnfs's -1 is a *generic* error (detail in nfs_get_error), not errno EPERM; mapping to EPERM would misreport. Author's comment already explains this.
- dev-storage sdcard.c SDREADDIAG — bounded (max 3), read-failure-path diagnostic; useful, not slop. See PENDING (keep vs gate is your call).

**Deferred — need HW test / attended / boot-critical / ABI / GPU build (see PENDING-USER-TASKS):**
- kernel cpu.c watchpoint (EL1 handler gap) + generic.h pctl_watchpoint ABI member — touches published platformctl ABI
- usb xhci.c inputCtx overwrite/leak behind hub; xhci_capProbe -ENOSYS-as-success clarity — USB is attended/statistical
- dev-tty usbkbd 8-byte read alignment; mtClose owning-pid check — input behavior, attended
- usb mem.c free-guard walk — corruption-guard, needs care
- fs-nfs nfs_ops './'/'..' id aliasing + readdir O(N^2) — correctness/perf, needs NFS soak
- tools-x11 fbdev.c:297 shadow-blit stride — correctness, HW-render verify
- project armstub SMP-D-3 markers / kernel8-reloc forward-copy / user.plo.yaml TEMP lines — boot-critical
- plo cache.c SCTLR accessor dedup; _startc heap-zero diag — boot-critical
- build port_internal.subr HOST_TARGET suffix — build-system, needs ports build
- ext-mesa (os_memory_aligned #undef, v3d_resource dead infra); ext-quakespasm (gl_screen .tga/.png, harness TCP sink, r_part doc ref) — external clones, need GPU build
