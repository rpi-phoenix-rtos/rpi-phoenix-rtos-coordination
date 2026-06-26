# Phoenix-RTOS Pi4 port — upstream-readiness punch-list (2026-06-27)

READ-ONLY audit of the Pi4 bring-up changes (the commits ahead of `origin/master`
in each sibling repo, plus the coordination repo's `tools/`). Goal: enumerate
everything that should be cleaned up before the port is published publicly.

**Method / scope discipline.** Findings are restricted to lines that are part of
each repo's `origin/master..master` diff (provenance verified with
`./scripts/git-siblings.sh in <repo> diff origin/master..master -- <path>`).
Pre-existing upstream TODO/FIXME/dead-code in shared drivers (imx6ull, zynq,
ehci, jffs2, pppos, ia32, etc.) is **out of scope** and deliberately not listed.
TD-xx markers are classified against the authoritative *Tracking Checklist* in
[TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
(lines 1884-1934), not the older narrative.

**Builds on the prior plan.** The 2026-06-25
[cleanup-upstreamability-plan](2026-06-25-cleanup-upstreamability-plan.md) already
executed Phase C (apps → `phoenix-rtos-project/_user/`) and Phase D.1 (the
`libXt-1.3.0-alloc-diag.patch` is **confirmed removed and no longer auto-applied** —
only a historical comment remains at `tools/x11-port/build-x11-phoenix.sh:174`).
This document is the next, finer-grained pass: it catalogues the diagnostic
cruft, stale TD markers, dead code, stale comments, and style issues that remain
in the touched source.

**Off-limits (intentionally NOT flagged), per the audit brief:**
- Parked WIP: `phoenix-rtos-devices/storage/bcm2711-emmc/{sdcard.c,sdstorage_dev.c}` (#154);
  `phoenix-rtos-lwip/port/wifi-{fw,nvram}-43455.*` (#91).
- `phoenix-rtos-usb/usb/mem.c` #121 instrumentation (alloc-log + `buf->next` guard) —
  *retained diagnostic, remove only when #121 closes.*
- `#29` vkQuake probe prints and `#30` DDX FIRST/event prints in `tools/x11-port/ddx/fbdev.c`
  — active debugging, retained.

---

## Summary table (counts per repo per severity)

| Repo / area | Blocker | Should-fix | Nice-to-have |
|---|---:|---:|---:|
| phoenix-rtos-kernel | 4 | 18 | 1 |
| plo | 2 | 3 | 0 |
| phoenix-rtos-lwip | 2 | 4 | 2 |
| phoenix-rtos-usb | 0 | 2 | 2 |
| phoenix-rtos-devices | 3 | 13 | 9 |
| phoenix-rtos-filesystems | 2 | 4 | 4 |
| phoenix-rtos-utils | 1 | 2 | 2 |
| phoenix-rtos-posixsrv | 0 | 1 | 1 |
| phoenix-rtos-corelibs | 0 | 0 | 1 |
| libphoenix | 0 | 0 | 1 |
| phoenix-rtos-project | 2 | 6 | 2 |
| tools/ (coord) | 0 | 3 | 2 |
| **cross-repo (deduped)** | 0 | 2 | 0 |
| **Total** | **16** | **58** | **27** |

(Cross-repo classes — the per-repo AI-fork README banner and `Author: Claude`
authorship lines — are collapsed into the *cross-repo* row below and **not**
double-counted in the per-repo rows.)

---

## Cross-repo classes (deduped — fix once, applies everywhere)

- **All sibling `README.md` files carry the "Fork warning: AI-generated, not
  fully reviewed/tested" banner** (kernel, usb, lwip, devices, libphoenix, utils,
  filesystems, plo, project). · Should-fix · Intentional for the public fork;
  **strip per-repo before any upstream PR to that project.** · needs-judgement
- **`Author: Claude (...)` lines** in some net-new files (e.g.
  `phoenix-rtos-utils/psh/uname/uname.c:7`) while sibling new files credit the
  human author. · Should-fix · Normalize authorship across new files. · safe-mechanical

---

## phoenix-rtos-kernel

### Blocker
- `main.c:127-186` · SMP "Phase D observability" block (inside `#if NUM_CPUS != 1`, which **is** compiled — `generic/config.h` sets `NUM_CPUS=4U`) prints five `smp: intr/ppi/tmr/tick/tick+15s` lines AND calls `proc_threadSleep(15 * 1000000LL)` at line 176 — an **unconditional 15-second boot delay on every Pi4 boot**. TD-01 is RESOLVED (4-core scheduling validated). Pure disproved-hypothesis bring-up instrumentation. · Delete the whole block including the 15 s sleep. · needs-judgement
- `main.c:209,211,213,215,229,250` · Ungated ad-hoc boot-progress markers `hal_consolePrint(ATTR_USER, "hi: vm-done\n")` … `perf-done`/`proc-done`/`syscalls-done`/`proc-start-done`/`reschedule-done` in `main()`. · Delete all `hi: …` prints. · safe-mechanical
- `proc/threads.c:237,247-249` · `threads_smpTickCount[8]` diagnostic counter incremented unconditionally in the timer-ISR hot path; only read by the `main.c` diag block above. Comment itself says it "can be promoted to a debug syscall (or removed entirely)". · Remove counter + per-tick increment. · needs-judgement
- `hal/aarch64/_init.S:313-318` · "TEMP REVERT 2026-05-16" comment claims the M-only SCTLR baseline is kept "while cache enable is isolated" — but cache enable IS resolved (TD-16). The framing is stale/misleading. The SCTLR *value* may still be load-bearing (full RES1 regressed on HW), so trim only the narrative, do not touch the value. · Rewrite to state plainly why the M-only baseline is set; drop "TEMP REVERT"/"while cache enable is isolated". · needs-judgement

### Should-fix
- `hal/aarch64/aarch64.h:201-241` (esp. 237-241) · **TD-19 reconciliation.** Doc claims `hal_tlbInval*` helpers end `dsb; isb`; source helpers end `hal_cpuDataSyncBarrier()` (`dsb ish`) with **no `isb`**. The TD-19 entry warns this may be a real missing-ISB correctness hole, not merely a doc overstatement. · Confirm whether ARMv8 requires an ISB after TLBI on the *generic* helper paths (vs only `_pmap_writeTtl3`); correct whichever side is wrong. **Do NOT assume the doc is wrong and do NOT edit the TLB helpers unattended.** · needs-judgement
- `hal/aarch64/gtimer_timer.c:95-100,123-125` · `hal_smpTimerInitPerCpuCount[8]` write-only SMP bring-up counter + increment, read only by the `main.c` diag block. (`hal_smpFirstIntervalUs` in the same file IS load-bearing — keep it.) · Remove counter + increment. · needs-judgement
- `hal/aarch64/interrupts_gicv2.c:353-359,381-383,420-422` · `hal_smpInterruptsInitPerCpuCount[8]` / `hal_smpInterruptsEnabledPpiCount[8]` write-only SMP counters + increments, read only by the `main.c` diag block. (`hal_smpPrimaryReady` IS load-bearing — keep it.) · Remove both counters + increments. · needs-judgement
- `lib/lib.h:35-40` · Comment says "the shipping Pi 4 config schedules on cpu0 only (NUM_CPUS == 1)" but the rpi4b build is `NUM_CPUS=4U`; the `#if … NUM_CPUS == 1` atomics branch is dead on rpi4b. · Reconcile comment with the NUM_CPUS=4 reality (single-core *scheduling*, not NUM_CPUS==1). · needs-judgement
- `main.c:55-60` · Verbose "Removed 2026-05-17 with the cache-era debug cleanup" tombstone for a deleted `hal_cpuEnableICache()` call. · Trim to one line or delete. · safe-mechanical
- `hal/aarch64/hal.c:98-102` · Multi-line "earlier Pi 4 cache-coherency workaround faked dtbEnd … removed (TD-04)" history comment; TD-04 RESOLVED. · Trim. · safe-mechanical
- `hal/aarch64/_init.S:144-167` · NC_ATTRS macro comments narrate the deleted "old TD-04 `_hal_syspageCopied` NC override (Phase Z3)". · Keep the live "now serves TD-15 mailbox alias" note; trim deleted-override history. · safe-mechanical
- `hal/aarch64/_init.S:508-527` · ~20-line "Phase Z3 (2026-05-17): TD-04 NC override DELETED" block + a "first hunk to revert if…" note describing code that no longer exists. · Collapse to a one-line "syspage page mapped cacheable" note. · safe-mechanical
- `hal/aarch64/_init.S:657-665` · "(TD-16 RESOLVED 2026-05-17) … historical iteration-by-iteration narrative preserved in …TD-16" — redundant with lines 562-564. · Trim/merge. · safe-mechanical
- `hal/aarch64/_init.S:706-714` · "(Reverted: experimental pre-copy DEST flush …)" do-not-redo marker — archaeology of a disproved approach. · Delete (rationale belongs in git/TD doc). · safe-mechanical
- `hal/aarch64/_init.S:722-730` · Comment references named probes (E2) and `l{}/v{}` markers that no longer exist. · Trim probe references; keep the data-vs-instruction-fetch fact. · safe-mechanical
- `hal/aarch64/_init.S:1013-1031` · `_early_exception_common` header mixes live design ("uses only direct loads/stores") with TD-16-era "'E' repeated forever" story. · Keep design rationale, trim TD-16 narrative. · safe-mechanical
- `hal/aarch64/_init.S:1147-1168` · `hal_cpuInvalDataCacheAll` carries a "symptom of the cisw bug pre-fix … hung silently" narrative. The dc-isw-not-cisw rule is load-bearing; the symptom story is not. · Trim symptom paragraph. · safe-mechanical
- `hal/aarch64/_init.S:1218-1224` · Tombstone for removed `hal_cpuEnableDCache/ICache/Caches` ("Restore from history if…"). · Delete. · safe-mechanical
- `hal/aarch64/generic/console.c:30-31` · `_hal_consoleEarlyPutch` hardcodes magic UART VAs `0xffffffffffe00000` (DR) / `0xffffffffffe00018` (FR) and bit `(1U<<5)` with no `#defines` (this is the path `hal_consolePrint` actually uses). · Name the UART VA + TXFF bit as constants. · needs-judgement
- `hal/aarch64/generic/console.c:54` · Boot-progress probe print `"console: pl011 init done\n"` in `_hal_consoleInit`. · Remove or gate behind a debug flag. · safe-mechanical
- `hal/aarch64/generic/generic.c:127-148` · Large commented-out SMP-barrier investigation log ("stays disabled until we have better diagnostics" … "boot hang … with NUM_CPUS=4") — no code, describes an unresolved race. · Trim to a short KNOWN-LIMITATION note. · needs-judgement
- `hal/aarch64/pl011.c:43-77` · `hal_pl011Init` body + the `enum { pl011_dr … }` use 8-space indentation while the file/Phoenix use tabs (the TD-05 reindent remainder). · Reindent to tabs. · safe-mechanical

### Nice-to-have
- `hal/aarch64/_init.S` (multiple) · Pervasive dated `Phase Z3 / TD-04 / TD-16 / 2026-05-17` phase labels on otherwise-correct code beyond the blocks above. · Strip dated labels, keep technical rationale. · safe-mechanical

### Verified LIVE — do NOT remove (keep-list)
`cpu.c:70-77`, `exceptions.c:194-219`, `_exceptions.S:382-385` (TD-10, KNOWN LIMITATION);
`dtb.c`/`dtb.h`/`pmap.c` "TD-15 / Stage 2 phase 4" markers (TD-15 partially open);
`_memset.S:24` (TD-20, KNOWN LIMITATION); `proc/name.c:34,258` (TD-14-devfs-direct, works as designed);
`log/log.c:407-441` (permanent UART klog mirror, RPI4_LOG_TO_FILE-gated, intentional);
`spinlock.c` `#if NUM_CPUS==1` branch (dead on rpi4b but legit portability path);
`hal_smpFirstIntervalUs` (gtimer), `hal_smpPrimaryReady` (gicv2) — **not** diagnostics.

---

## plo

### Blocker
- `hal/aarch64/generic/_init.S:478-668` · The installed exception vector table (`_vector_table`, wired to VBAR_EL3/EL2/EL1) is a **diagnostic-only rig**: every slot prints a tag char and halts; slot E runs `_slot_e_dump` (full ESR/ELR/FAR/SPSR/SCTLR/TTBR/TCR/MAIR + a disproved I-cache-aliasing instruction-readback probe at 604-616). Comment says "After diagnosis we either fix the offending exception cause or restore the original vector table." This ships as plo's only exception handling. · Restore a real vector table (or document the halt-on-fault as deliberate); remove the disproved I-cache-aliasing probe. · needs-judgement
- `hal/aarch64/generic/_init.S:437-470` · `hal_exitToEL1` emits raw boot-progress markers via `uart_putc` ('A' on entry, then '3'/'2'/'1' per EL branch) in the kernel-handoff path. · Remove the markers from the handoff path. · safe-mechanical

### Should-fix
- `_startc.c:53-60` · "TD-05 diagnostic: zero the heap …" self-described diagnostic memset of the 16 KB heap every boot ("Revisit once the root cause is understood"). TD-05 largely resolved. · Remove the heap-zero, or confirm intentional and drop the "diagnostic/Revisit" framing. · needs-judgement
- `hal/aarch64/generic/_init.S:496-617` · Diagnostic helpers `uart_put_hexnibble`/`uart_put_hex64`/`_slot_e_dump` exist solely to service the diagnostic vector table above. · Remove together with the vector-table fix. · needs-judgement
- `hal/aarch64/generic/_init.S:261-268` · Disproved-hypothesis narrative ("the previous TD-05 set/way loop … misdiagnosed VPU-side-L2 aliasing … falsified"). · Trim to the live one-liner. · safe-mechanical

### Verified clean (keep)
`hal/aarch64/{mmu,cache}.c` (EL2/EL3 runtime-dispatch refactor, well-commented);
new `generic/{console,interrupts,timer}.c`, `config.h`, `ld/*.ldt` (proper enums/#defines, tabs).

---

## phoenix-rtos-lwip

### Blocker
- `port/mbox.c:18-19,129-168` · Obsolete `#121/#129` mbox-corruption diagnostic: a `va2pa` + raw 16-word memory dump around a "corrupted during USB enumeration" guard. This dates to the **embedded-USB-in-lwip era, which is gone** (USB is now a standalone daemon), so the hypothesized DMA-overrun-into-lwip-heap path no longer exists. (Note: this is lwip's `port/mbox.c`, NOT the off-limits `usb/mem.c`.) · Remove the `va2pa`/`debug()` dump block and the `sys/debug.h`/`sys/mman.h` diagnostic includes; keep or simplify the cheap NULL/bounds guard if still wanted. · needs-judgement
- `drivers/bcm-genet.c:17-19` · Stale header comment: "256-BD ring with cyclic aliasing of **16 unique** pinned buffers" — the body now backs every BD with a **unique** buffer (256), the fix that resolved the cwnd-collapse packet loss. The comment contradicts the code. · Correct the comment (256 unique buffers, no aliasing). · safe-mechanical

### Should-fix
- **GENET cacheable-RX (#11 "Policy-B") apparatus — one coherent default-off experiment the project concluded UNVIABLE** (corrupts the GPU framebuffer under load). Spans: `port/genet-rxcache-bench.c` (whole file); `drivers/bcm-genet.c` `#if GENET_RX_CACHEABLE` blocks at 93-94, 218-257, 663-696, 842, 868 + the `RXCACHE pool PA=…` print at 680 + `dmammap_cached` call at 665 + `rx_zerocopy`/`rx_copyfallback` counters (179-180, 880, 919, 1087-1092 RXSTATS); `drivers/physmmap.c:33`+`physmmap.h:26` (`dmammap_cached`); `port/main.c:30-34,162-165` (bench hook); `Makefile:37-41` + `port/Makefile` plumbing. · Either retire wholesale (it's recoverable from git + documented in the TD doc) or add a single explicit "default-off, concluded-unviable experiment — see #11" banner so a reviewer isn't misled into thinking it's a live code path. · needs-judgement
- `drivers/bcm-genet.c:680` · `RXCACHE pool PA=…` init diagnostic print (the FB-corruption-hunt line). · Remove with the apparatus above (it is inside the `#if GENET_RX_CACHEABLE` so off by default, but ships in source). · safe-mechanical
- `port/main.c` (init path) · Always-on `dhcp_start` / `tcpip_callback` init progress prints. · Gate behind a debug flag or reduce to one concise banner. · safe-mechanical
- `drivers/bcm-genet.c:1087-1092` · `RXSTATS seen=…zerocopy=…` periodic console line — already gated default-off (#31), but tied to the cacheable-RX counters above. · Resolve together with the apparatus decision. · needs-judgement

### Nice-to-have
- `drivers/ephy.c` · HCD/PHY magic numbers (register values without named constants) in the BCM54213PE additions. · Name as `#defines` where it aids the reader. · needs-judgement
- `drivers/bcm-genet.c:22` · `TODO(TD-Eth-LinkIRQ)` — **keep**: TD-Eth-LinkIRQ is PENDING (PHY INT_B not routed to GIC; MDIO poll is the portable answer). Listed only so it isn't mistaken for stale. · No action. · needs-judgement

---

## phoenix-rtos-usb

### Should-fix
- `usb/usb.c:185-194` · "survive-not-crash" guard that resets the URB finished-list to empty on detected corruption (`head->prev == NULL`) and prints `"usb: URB finished-list corrupt … resetting"`. Comment admits "Proper fix … tracked separately" — i.e. open debt left in product code masking a real corruption. (The idempotency guard at 157-179 is clean defensive code — keep.) · Either root-cause the corruption (the proper fix) or document the guard as an accepted permanent safety net, not a TODO. · needs-judgement
- `usb/dev.c:689-696` · `log_msg("Fail to match drivers for device …")` + per-interface descriptor dump on unmatched device (commit b3e97dc). Useful during bring-up; verbose for a shipped daemon. · Reduce to a single concise line or gate. · safe-mechanical

### Nice-to-have
- `usb/hub.c:37,308,325,340,344` + `usb/dev.h:76` · `TODO(#129)` enum-bounding markers (`HUB_ENUM_GIVEUP`, per-port fail counts, `hub->devs[]` tracking). These are now **permanent design** (bounding the re-enum reboot-loop), not leftover diagnostics. · Reclassify: strip the `TODO(#129)` prefix and reword as plain design comments. · needs-judgement
- `usb/usb.c:509` · `TD-12` setvbuf/buffering comment is long but load-bearing (boot back-pressure mitigation). · Keep; optionally trim. · needs-judgement

### Retained per scope (not flagged)
`usb/mem.c` #121 instrumentation — retained diagnostic, remove when #121 closes.

---

## phoenix-rtos-devices

### Blocker
- `pcie/server/pcie.c:957-962` · Self-flagged `TODO: remove this diagnostic include once VL805 BAR-programming is proven stable` — `#include <sys/debug.h>` pulled in only for bring-up `debug()` prints. **This whole Pi4 BCM2711 block is dead on every shipping target**: the standalone `pcie` daemon is de-listed (`_targets/Makefile.aarch64a72-generic:65-69`); live PCIe bring-up is in `usb/xhci/bcm2711-pcie.c`. · Remove the include with the dead block below. · needs-judgement
- `pcie/server/pcie.c:759-855` · Pi4 `#if defined(PCI_EXPRESS_BCM2711_INDEXED_CFG)` block (VL805 cmd-enable diag, fixed `usleep(200000)` settle, BAR0 program, outbound MMIO diag read) — a stale duplicate of the live `usb/xhci/bcm2711-pcie.c` logic, compiled into nothing. · Drop the entire Pi4-added BCM2711 block (file stays for other targets). · needs-judgement
- `pcie/server/pcie.c:1013-1044` · "VL805 warm-up loop" (30×100 ms outbound reads before daemon exit) — dead diagnostic scaffolding in the unbuilt daemon. · Remove with the block above. · needs-judgement

### Should-fix
- `pcie/server/pcie.c:751-757,769-778,819-826,834-853,873-879,997-1011,1041-1047` · Numerous inline `debug()` register-dump stanzas in the dead block. · Remove with the block. · safe-mechanical
- `usb/xhci/bcm2711-pcie.c:641-656,703-745,960-972,984-991,996-1037,1146-1152` · Verbose `USB-FIX-N` register-dump `debug()` banners around the **live** VL805 bring-up. The register ops are load-bearing; the `snprintf+debug` dumps are bring-up tracing. · Gate behind a debug macro or remove the dumps, keep the ops. · needs-judgement
- `usb/xhci/xhci.c:1817-1839` · `TODO(#129) diag: rate-limit the timeout report` — 12-shot `debug()` timeout dump for a now-stabilized path (the cmd-ring recover at 1847 is the real fix). · Gate or reduce to one line; resolve the TODO. · needs-judgement
- `usb/xhci/xhci.c:1842-1846` · `TODO(#129): if this reliably rescues, promote to a real bounded retry` — the rescue ships unconditionally with the decision still open. · Decide: promote to a named bounded-retry or document as permanent. · needs-judgement
- `usb/xhci/xhci.c:870-934,1351-1365,1376-1385` · `USB-DBG` enter/exit state dumps in `xhci_reset` and `enterRun` ("recovered on attempt"/"GAVE UP"). · Gate behind a debug macro; keep the logic. · needs-judgement
- `audio/rpi4-audio/rpi4-audio.c:466-485` · Boot self-test feeds a ~0.2 s 440 Hz tone through the write path and prints "self-test fed N samples". **Side-effecting** (emits audio) boot self-test. · Remove or gate behind a build flag. · needs-judgement
- `audio/rpi4-audio/rpi4-audio.c:399-401,433-435` · Unconditional full register-dump banners (`CS/DEBUG/STA`, `CM_PWMCTL/CM_PWMDIV/PWM_CTL/PWM_STA/GPFSEL4`) every boot ("P0 scout"). · Reduce to a one-line status or gate. · safe-mechanical
- `video/rpi4-fb/rpi4-fb.c:219-228` · Read-only "framebuffer read self-test" boot canary (harmless — no write). · Fold the first-pixel into the registration banner and drop the explicit self-test framing for upstream. · needs-judgement
- `tty/pl011-tty/pl011-tty.c:1143-1146` · `TODO(#127)` + `fprintf(stderr, "pl011-tty: kbd bridge opened …")`. **#127 (USB kbd) is resolved** — stale observability scaffolding. · Remove the print and the TODO. · needs-judgement
- `tty/pl011-tty/pl011-tty.c:645-648` · `TODO(TD-14-pl011-retry)` — checklist marks it **superseded** by TD-12 retry tuning; the comment narrates obsolete retry history. · Trim the stale narration; keep the current retry value. · needs-judgement
- `tty/usbkbd/usbkbd.c:80` + `tty/usbmouse/usbmouse.c:75` · `#define TRACE(fmt,...) do { if (0) printf(...); } while (0)` — permanently-disabled trace macro (dead-but-compiled scaffolding). · Remove the macro + call sites, or wire to a real debug flag. · needs-judgement
- `tty/usbkbd/usbkbd.c:766,831-832` + `tty/usbmouse/usbmouse.c:559,625` · `debug()`/`fprintf` "handleInsertion fired" / duplicate "New device" enumeration banners (one to stdout, one to klog). · Collapse to a single line each; drop the "fired" tracers. · safe-mechanical
- `misc/rpi4-sysinfo/rpi4-sysinfo.c` (whole file) · Boot-banner / device-inventory "alive touch" utility, **shipped as a default component** (`Makefile.aarch64a72-generic:43`) — not a driver; reads as bring-up scaffolding. · De-list (mirror the `rpi4-ipcprobe` treatment) consistent with the GPU-harness-removal precedent. · needs-judgement

### Nice-to-have
- `tty/usbmouse/usbmouse.c:11-20` · Header documents that usbmouse.c and usbkbd.c "share most of their scaffolding … factor the common part into a shared libhidboot core." Confirmed copy-paste (registration/idtree/URB/fifo/msgloop). · Track the `libhidboot` refactor before upstream. · needs-judgement
- `tty/usbkbd/srv.c` + `tty/usbmouse/srv.c` · 39-line near-identical standalone entry points (only the prefix differs); not built for a72. · Fold into the libhidboot refactor if pursued. · needs-judgement
- `tty/pl011-tty/pl011-tty.c:96,119,154,246,268,375,770-784,1037,1086,1220` · Pervasive dated `TD-15 Stage 4 phase 1c/1h`, `TD-12 (2026-05-17)`, `TD-14/#127` phase/date labels on correct VT100-parser / klog-drain code. · Strip the dated phase labels, keep the technical rationale. · safe-mechanical
- `usb/xhci/bcm2711-pcie.c:975-981,1076` · `USB-FIX-2 (2026-05-26)` dated-tag and `TD-15 Stage 4 phase 2:` prefix on otherwise-correct BAR0 code. · Drop the prefixes, keep the explanation. · safe-mechanical
- `misc/rpi4-sysinfo/rpi4-sysinfo.c:50` · `__DATE__/__TIME__` build stamp makes builds non-reproducible. · Drop or gate. · safe-mechanical
- `video/rpi4-fb/rpi4-fb.c:242-244` · Registration banner prints `first_px=0x…` (canary residue). · Trim to geometry only. · safe-mechanical
- `gpio/rpi4-gpio/rpi4-gpio.c:227-229` · "Self-test / acceptance line" prints GPFSEL0/GPLEV0 at boot. · Trim to a plain registration banner. · safe-mechanical
- `misc/rpi4-hwrng/rpi4-hwrng.c:186-210` · Entropy canary banner prints sample words. · Optionally drop the sample words; keep the dead-RNG guard. · safe-mechanical
- `_targets/Makefile.ia32-generic` · Pi4 added `usbkbd libusbdrv-usbkbd` to the ia32 reference build but not `usbmouse`/`libusbdrv-usbmouse` (per the USB-multidriver note the canonical reference includes the mouse). · Confirm intentional; add usbmouse for parity if ia32 mirrors a72. · needs-judgement

### Verified LIVE / clean (keep)
`tty/pl011-tty/pl011-tty.c:1195-1218` (`TODO(TD-14-tty0-nonfatal)` + `TODO(TD-14-console-alias)` — STILL ACTIVE per checklist; keep code, reconcile wording);
`tty/libtty/libtty.c:506-507` (TD-14-tiocspgrp-pgrp, correct semantics, upstreamable as-is);
`misc/rpi4-vcmbox/*` (serialization design clean; the failure-cause prints are useful);
`libklog/libklog.c` — substantial Pi4 rewrite (−141 lines) with no markers, but **review for behavior parity** with non-Pi4 consumers before publishing (needs-judgement).
`misc/rpi4-ipcprobe/*` — already de-listed; decide keep-or-drop with rpi4-sysinfo (see Should-fix).

---

## phoenix-rtos-filesystems

### Blocker
- `dummyfs/srv.c:219-241` · **Behavior change in SHARED non-Pi4 code**: the stdout-readiness wait loop `while (write(1, "", 0) < 0) usleep(...)` was **deleted** from both the `mountpt==NULL` root path and the `non_fs_namespace` path before `portCreate` (confirmed deleted, not relocated). Affects all targets, not just Pi4. · Restore the wait loops or add a comment justifying their removal + validate non-Pi4 targets. · needs-judgement
- `nfs/srv.c:389-456` (+ header :19-30, dispatch :662) · `nfs_runRoot` is a reachable mode (`main()` dispatches on the `root` argv token) that is self-documented "NOT FUNCTIONAL on the current kernel — kept for reference only" — a disproved/non-functional experiment in shipping code. · Remove the function, the `root` token dispatch, the `rootMode` plumbing, and the root-mode header paragraph. · needs-judgement

### Should-fix
- `dummyfs/srv.c:219-241,336-338` · Tab→space reindent corrupted whitespace on the touched block (mixed tabs/spaces) — breaks Phoenix style, pollutes git-blame. · Reindent to tabs. · safe-mechanical
- `nfs/srv.c:355` · `nfs_set_poll_timeout(nfs, 1)` — load-bearing lwip poll()-readiness workaround with no TD-xx marker + 6-line narrative. · Add a TD-xx/debt entry; trim narrative to one line. · needs-judgement
- `nfs/srv.c:364-385` · 22-line `nfs_set_dircache` investigation narrative (transient-ENOENT + plo boot-order). · Trim to a one-line rationale; move detail to the cited doc. · needs-judgement
- `nfs/srv.c:81-189` · `valid_ipv4` + `wait_for_dhcp_lease` duplicated from nfs-smoke ("copied so the fs server has no nfs-smoke dependency"). · Factor a shared helper or accept + drop the self-referential comment. · needs-judgement

### Nice-to-have
- `nfs/nfs_ops.c:257` · `printf("nfs-fs: fh-cache evict, %u idle\n", …)` diagnostic trace on a hot path. · Remove the printf. · safe-mechanical
- `nfs/srv.c:337-347` · 11-line `#156` issue-tracker narrative for `nfs4_set_client_name`. · Condense to 1-2 lines, drop #156 ref. · safe-mechanical
- `nfs/srv.c:348,362-363,411,422,443` · Magic numbers (RPC timeout 5000, xfer max 1024*1024, deadline 90, sleep(10), usleep(3000000)). · Name as `#defines` (root-path ones vanish with `nfs_runRoot` removal). · safe-mechanical
- `nfs/nfs_ops.c:191,204,241,561,660` + `nfs/nfs_node.{c,h}` · Internal `(#156)`/`(OQ-B)` ticket tags + "HW-observed" bring-up phrasing in lazy-close/chmod comments. · Strip ticket tags; reword as plain behavioral notes (keep the rationale + the workarounds). · safe-mechanical

---

## phoenix-rtos-utils

### Blocker
- `psh/pshapp/pshapp.c:48` · `TODO(TD-14-psh-retry)` comment is **factually wrong**: it claims the budget was "raised for Pi 4" and the upstream default is "20 × 100 ms". Verified: upstream is 5 × 100 ms (0.5 s) and Pi4 is 50 × 10 ms (0.5 s) — same wall-clock, not raised. Checklist marks TD-14-psh-retry superseded. · Correct the comment to the real upstream value, or drop the TODO + macros. · needs-judgement

### Should-fix
- `_targets/Makefile.aarch64a72-generic:9` · `DEFAULT_COMPONENTS := psh nfs-smoke` ships a boot-time FS micro-benchmark (50 stat RPCs + 2 MB read + 4 MB write, **every boot**) in the default a72 build (a53 is correctly `psh` only). · Gate behind an opt-in flag or drop from DEFAULT_COMPONENTS. · needs-judgement
- `nfs-smoke/nfs-smoke.c:~110-120` · Dead `vlen` block in `wait_for_dhcp_lease` (computes `vlen`, discards it, re-derives via strncpy+strpbrk; the dead loop also has a latent off-by-one `eq[vlen]`). · Delete the dead `vlen` while-block. · safe-mechanical

### Nice-to-have
- `nfs-smoke/Makefile:1` · Header labels it "libnfs port T1 feasibility gate, #153" — scaffolding framing. · Reframe as a util / drop the #-ticket ref if kept. · safe-mechanical
- `psh/pshapp/pshapp.c:1600` · `TODO(TD-14-ttyopen-nonfatal)` — verified ACTIVE + accurate per checklist. · Keep. · needs-judgement

(`psh/uname/uname.c:7` `Author: Claude` is counted once in the cross-repo authorship class above, not here.)

---

## phoenix-rtos-posixsrv

### Should-fix
- `special.c:117-118` · Once-per-boot `printf("posixsrv: /dev/urandom entropy source = …")` bring-up print. · Gate or drop for upstream. (The accompanying `dst` advance is a real correctness fix — keep that.) · needs-judgement

### Nice-to-have
- `special.c:141` · Stale comment "(the prior code left tails past 64 B unwritten)" references now-replaced upstream code. · Reword to describe current behavior or drop. · safe-mechanical

---

## phoenix-rtos-corelibs

### Nice-to-have
- `libstorage/include/storage/storage.h:70-85` · `STORAGE_DEEPFS_STACKSZ` + `storage_run` doc comment is a legitimate, well-documented API addition; only the internal `#120` reference should go before upstreaming. · Drop the `#120` reference. · safe-mechanical

---

## libphoenix

All Pi4 additions are clean, standard-library work (reboot platformctl split, hypot, wide-char/multibyte set, getpw*_r, getrandom/getentropy, grp stubs). `setgrent/endgrent/getgrent` are real libc additions (xterm calls `endgrent()`), not scaffolding. `unistd/file.c:355-360` `TODO(TD-14-console-open-fastpath)` is STILL ACTIVE (intentional keep).

### Nice-to-have
- `signal/signal.c:155` · `fprintf(stderr, "libphoenix: NULL handler for signal %d…")` on a "should never happen" path — emits to stderr from shipping libc. · Optionally drop the print (keep the default-disposition fix). · needs-judgement

---

## phoenix-rtos-project (board config)

### Blocker
- `_projects/aarch64a72-generic-rpi4b/user.plo.yaml:193` · Misleading comment "GL Quake (flagship) autostart — re-enabled 2026-06-25" sits directly above a launch that is **commented out** (L196-197) — comment contradicts file state, and with both flagship launches commented the image boots to bare psh with no showcase app. · Reconcile: delete the comment or uncomment exactly one flagship launch. · needs-judgement
- `_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S:216-237` · "SMP-D-3 diagnostic" block writes `0x10000000+cpu_id` to PA `0x40+cpu_id*4` for a **now-removed** kernel-side reader (board_config.h:54-57 notes the markers "have since been stripped") — an inert disproved-diagnostic write into low RAM, in the published trampoline. · Remove the diagnostic write + comment (confirm no current kernel reader). · needs-judgement

### Should-fix
- `_projects/aarch64a72-generic-rpi4b/user.plo.yaml:204-206` · `# TEMP-XTERM-TEST` session-scratch note ("morning #30", "type into xterm", "#29 retest") embedded as a launch comment. · Remove the block. · safe-mechanical
- `_projects/aarch64a72-generic-rpi4b/user.plo.yaml:195` · `# TEMP-KBD0CHK boot-to-psh` leftover manual-test marker. · Restore one flagship launch + delete the marker. · needs-judgement
- `_projects/aarch64a72-generic-rpi4b/user.plo.yaml:196-197,207-208` · Both flagship launches (rpi4-quake + rpi4-vkquake) left commented (the GL/VK swap). · Keep one active, one commented as the documented alternate; concise swap note. · needs-judgement
- `_projects/aarch64a72-generic-rpi4b/user.plo.yaml:176-179` · `rpi4-ipcprobe` commented-out one-shot "Ran 2026-06-17, all PASS … disabled". · Drop (recoverable from git). · safe-mechanical
- `_projects/aarch64a72-generic-rpi4b/config.txt:42` · Stale parenthetical "fits gpu_mem=76" contradicts L35 `gpu_mem=128`. · Update or drop. · safe-mechanical
- `_projects/aarch64a72-generic-rpi4b/busybox_config:71` · `CONFIG_DEBUG=y` builds busybox with debug symbols (bloats rootfs; upstream default n). · Set to `is not set` unless intended. · needs-judgement

### Nice-to-have
- `_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S:286,295,302,315,390-394` + `phoenix-kernel8-reloc.S:61-65,75-79,116-120,123-127` · Boot-stage `uart_putc` single-char progress markers ('1'/'4'/'2'/'5', "AS0", "TR0..TR3") in published files. · Strip, or document as intentional pre-console progress markers. · needs-judgement
- `_projects/aarch64a72-generic-rpi4b/config.txt:21` · `force_turbo=1`+`core_freq=250` undocumented magic knobs. · Add a one-line rationale. · needs-judgement

### Verified clean (keep)
`board_config.h`, `preinit.plo.yaml`, `build.project`, `ports.yaml`, `lwipopts.h`, `.lds`,
`_user/rpi4-quake/*`, `_user/rpi4-vkquake/*` (the intended flagship apps) — no dead/duplicate
defines, **no duplicate-program brick hazard** (doubled `-x` aliases sit behind mutually-exclusive `if:` gates).

---

## tools/ (coordination repo build-infra)

### Should-fix
- `tools/x11-port/patches/WindowMaker-0.95.9-phx-diag.patch:1-432` · **Adjudication:** auto-applied unconditionally at `build-wmaker.sh:271-287`; interleaves ~15 `PHX_MARK()` startup probes with a genuine `phxfile:` TTF-font fix. Markers compile to nothing unless `WMAKER_DIAG=1` (default off). It is **not** on the explicit do-not-flag list, so it is flagged here — but it is active in-progress debugging for an unresolved wmaker startup hang (same category as the off-limits #30 prints), so severity is Should-fix, not Blocker. · Before upstreaming, split into a clean font patch + drop the PHX_MARK hunks; leave as-is while the wmaker hang is open. · needs-judgement
- `tools/x11-port/build-xfbdev.sh:13,28,37` + `tools/x11-port/ddx/fbdev_stub.c` · `--stub` toggle links an empty-hook DDX as a "link-closure de-risk" — bring-up scaffold superseded by the working Xphoenix. · Drop the `--stub` branch + `fbdev_stub.c`. · needs-judgement
- `tools/x11-port/build-*.sh` (multiple) + `tools/v3d-driver-port/build-v3d-phoenix.py:27` + `tools/stress/build-stress-*.sh` + `phoenix-rtos-project/_user/rpi4-quake/Makefile:35`, `_user/rpi4-vkquake/Makefile:37` · Hardcoded absolute dev-host path `/home/houp/phoenix-rpi/…`; the quake/vkquake Makefiles bake it as non-overridable `:=` and will **publicly leak the home path**. · Derive ROOT from `$(dirname "$0")` / use overridable `?=` form uniformly. · needs-judgement

### Nice-to-have
- `tools/stress/{stress-fs,stress-ipc,stress-proc}` · Built aarch64 ELF artifacts committed next to their `.c` sources (the `tools/stress/.gitignore` doesn't cover them). · `git rm --cached` + extend `.gitignore`. · safe-mechanical
- `tools/x11-port/launcher/mouseprobe.c` · Standalone probe util tracked in repo. · Confirm still wanted. · needs-judgement

### Confirmed resolved / clean
- `tools/x11-port/patches/libXt-1.3.0-alloc-diag.patch` — **removed and no longer auto-applied** (Phase D.1 done); only a historical comment at `build-x11-phoenix.sh:174` remains.
- Genuine port patches kept: `libxcb-1.16-phoenix`, `libICE-1.1.1-phoenix`, `xorg-server-record-malloc0`, `xterm`, and the quakespasm/vkquake/mesa port patches.

---

## Safe-to-fix-mechanically vs needs-judgement

A follow-up session can batch the **safe-mechanical** items below without runtime
testing — they are comment trims, dead-print/marker deletions, reindents, and
`#define` extractions in code that already works. **But first read the
"do-NOT-touch" list** so a batch pass doesn't delete live code.

### Batch 1 — safe-mechanical (no runtime judgement)
- **kernel**: `main.c:55-60,209-250` (hi: prints + tombstone); `hal.c:98-102`;
  `_init.S:144-167,508-527,657-665,706-714,722-730,1013-1031,1147-1168,1218-1224` (history trims) + dated-label strip; `pl011.c:43-77` reindent; `generic/console.c:54` print.
- **plo**: `_init.S:261-268,437-470` (narrative trim + handoff markers).
- **lwip**: `bcm-genet.c:17-19` (256-buffer comment fix), `:680` RXCACHE print, `port/main.c` init prints.
- **usb**: `dev.c:689-696` log trim.
- **devices**: `pcie.c` debug-stanza removals (with the block); `rpi4-audio.c:399-435`,
  `usbkbd.c:766,831-832` / `usbmouse.c:559,625`, `rpi4-fb.c:242-244`, `rpi4-gpio.c:227-229`,
  `rpi4-hwrng.c` sample words, `rpi4-sysinfo.c:50` build stamp, pl011-tty dated labels,
  bcm2711-pcie.c dated tags.
- **filesystems**: `dummyfs/srv.c` reindent; `nfs/nfs_ops.c:257` printf; `nfs/srv.c:337-347,348-443`
  ticket-tag + magic-number cleanup; `nfs_node.*` ticket tags.
- **utils**: `nfs-smoke.c` dead `vlen` block; `nfs-smoke/Makefile`, `uname.c:7`.
- **posixsrv**: `special.c:141` comment.
- **corelibs**: `storage.h` #120 ref.
- **project**: `user.plo.yaml:176-179,204-206`; `config.txt:42`.
- **tools**: `git rm --cached` the stress binaries + `.gitignore`; `build-x11-phoenix.sh:174` comment.
- **cross-repo**: README fork banners + `Author: Claude` lines (per the publication call).

### Batch 2 — needs-judgement (logic / testing / a design decision)
- **kernel**: the `main.c` 15 s SMP diag block + the four SMP write-only counters
  (threads/gtimer/gicv2) — removing them is correct but touches the timer ISR and
  three files; **TD-19 ISB reconciliation (TLB path — do not edit unattended)**;
  `_init.S:313-318` SCTLR-baseline comment; `lib.h:35-40` NUM_CPUS comment;
  `console.c:30-31` UART magic-VA `#defines`; `generic.c:127-148` SMP-barrier note.
- **plo**: the diagnostic-only exception **vector table** + its hex-dump helpers
  (restore a real table or document halt-on-fault); `_startc.c` heap-zero.
- **lwip**: the GENET cacheable-RX **apparatus** decision (retire vs documented experiment);
  `port/mbox.c` obsolete #121/#129 dump removal.
- **usb**: `usb.c:185-194` survive-not-crash guard (root-cause vs accept-as-permanent);
  `hub.c`/`dev.h` #129 TODO→design reclassification.
- **devices**: dropping the dead `pcie/server/pcie.c` BCM2711 block; gating the live
  xhci/bcm2711-pcie `debug()` banners; the audio side-effecting self-test; rpi4-fb self-test;
  rpi4-sysinfo/ipcprobe keep-or-drop; the usbkbd/usbmouse `libhidboot` refactor;
  pl011-tty TD-14 marker reconciliation; `libklog.c` parity review; ia32 usbmouse parity.
- **filesystems**: dummyfs wait-loop restoration (shared code, validate non-Pi4);
  `nfs_runRoot` removal; poll-timeout/dircache workaround documentation.
- **utils**: `pshapp.c:48` TD-14 comment correction; `nfs-smoke` in DEFAULT_COMPONENTS.
- **posixsrv**: `special.c:117-118` print gating.
- **project**: `user.plo.yaml:193,195,196-208` flagship-launch reconciliation;
  armstub SMP-D-3 inert write removal; boot-stage uart markers; busybox CONFIG_DEBUG.
- **tools**: WindowMaker phx-diag patch split; `--stub` DDX removal; hardcoded dev-host paths.

### Do-NOT-touch in any batch (verified live / intentional / off-limits)
- TD-10 (kernel `cpu.c`/`exceptions.c`/`_exceptions.S`), TD-15 (kernel dtb/pmap), TD-20
  (kernel `_memset.S`), TD-Eth-LinkIRQ (lwip `bcm-genet.c:22`), TD-14-tty0-nonfatal /
  TD-14-console-alias (devices `pl011-tty`), TD-14-console-open-fastpath (libphoenix
  `file.c`), TD-14-ttyopen-nonfatal (utils `pshapp.c:1600`), TD-14-tiocspgrp-pgrp
  (devices `libtty.c`) — all STILL ACTIVE / KNOWN LIMITATION.
- `hal_smpFirstIntervalUs`, `hal_smpPrimaryReady`, the kernel klog UART mirror,
  the spinlock `NUM_CPUS==1` branch — live, not diagnostics.
- `usb/mem.c` #121 instrumentation; `wifi-*-43455.*`; `sdcard.c`/`sdstorage_dev.c`;
  `#29`/`#30` probe prints — off-limits per the brief.
