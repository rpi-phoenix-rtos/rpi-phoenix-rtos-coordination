# Pi 4 USB workstream — step-by-step implementation plan (2026-06-02)

Audience: the project owner. Goal: a concrete, phased, hardware-testable plan for
the Pi 4 USB stack covering (A) code cleanup / upstreamability, (B) closing the
parked USB-FIX ledger honestly, (C) understanding + fixing TD-10 (the masked
SError / PCIe external abort), (D) a production mouse driver toward Tiny-X/Quake,
and (E) sequencing.

Each phase is independently shippable and validated on the real Pi 4 with the
project's existing harness (`scripts/test-cycle-netboot.sh`, the diag-udp `'D'`
devnode probe, live keypress to psh). Every validated phase produces a
`manifests/*.md` snapshot via `scripts/snapshot-integration-state.sh`, and the
known-good rollback tag remains available.

This plan deliberately does **not** overstate certainty. The project has a
documented history of premature "fixed" claims; where a result is one
uncontrolled observation it is labelled as such.

---

## Current USB state (2026-06-02) — honest preamble

**Architecture (live, committed):** USB now runs as a **standalone `usb`
daemon** plus separate `usbkbd` and `usbmouse` driver processes, launched from
`_projects/aarch64a72-generic-rpi4b/user.plo.yaml` (lines 26-37) *before* `lwip`.
This replaced the earlier lwip-embedded "rig" PoC (`LWIP_EMBED_USB`). The
isolation goal is realized: a USB-side fault can no longer corrupt the lwIP mbox
or kill the network (`docs/done/2026-06-01-usb-rearchitecture-plan.md`, "STEP 3
STABLE"). The kernel is on `master` (consolidated); USB siblings
(`phoenix-rtos-usb`, `phoenix-rtos-devices`) are also on `master`.

**Bring-up:** the framework's own xHCI bring-up (`usb/xhci/xhci.c` `xhci_init`,
non-rig, non-DRIVE_ONLY) is **deterministic** — controller reaches RUNNING,
events post, the VIA hub (`2109:3431`) enumerates. The historical "inbound-DMA
wall / first event @idx -1" was an artifact of the old DRIVE_ONLY/inert-FIX-19
config, not silicon.

**RESOLVED 2026-06-02 (A/B test, `docs/done/2026-06-02-p1-ab-verdict.md`):** the
"P1 fixed enum 3/10→10/10" claim is **withdrawn**. Controlled 8+8 A/B: dump-on
6/8 pass, dump-off 8/8 pass (Fisher p≈0.47, NS); re-adding the dump gave ~25%
failures, not the 70% baseline — so P1 did NOT cause the enum swing (confounded).
USB enum is intermittently flaky regardless (~0–25%); the real wall is FIX-14
(#78) / TD-10 (#144). The tension below is kept for context.

**The headline tension you must hold in mind (CONFIRMED vs HYPOTHESIS):**

- The 2026-06-02 10-boot study reports **USB enum 3/10 → 10/10 "clean"**
  (`docs/done/2026-06-02-p1p2p3-postfix-10boot.md:73-77`), explicitly claiming
  all 10 enumerated the **hub + PIXART mouse + Logitech keyboard** with zero
  failures. Its `enum_fail` metric is `compare-boots.py`'s count of `hub.c`'s
  "Enumeration failed" line (`hub.c:318`), which fires for **any** failing port
  (hub or downstream), so `enum_fail=0` is a real signal that downstream ports
  enumerated too — not hub-only. (Verified against `compare-boots.py` + `hub.c`.)
- The earlier re-architecture log
  (`2026-06-01-usb-rearchitecture-plan.md`, "STEP 3 STABLE" and the #129 memory
  `project_usb_addressdevice_wall_129`) records the **low-speed keyboard behind
  the VIA hub's TT failing its ep0 control transfers** (`xhci: transfer completion
  timeout`), with downstream kbd/mouse binding only ~33-50% of boots and a per-port
  bound (`HUB_ENUM_GIVEUP=3`, `hub.c:37`) keeping the box stable. **Crucially this
  predates the P1 fix** (devices `3f82638`, 06-02) the study credits — so the ~50%
  figure may be obsolete.

The two sources are in tension on the downstream bind rate, and the 10/10 is **one
uncontrolled study, post-P1, not A/B-confirmed**. Separately, *enumerated*
(port addressed/configured, `/dev/kbd0` created) is **not** the same as a **live
keypress reaching psh** — and `PL011_TTY_KBD_PATH` defaults to `NULL`
(`pl011-tty.c:60`), so the kbd→psh bridge thread may not even be wired in the
default image. **Phase 0 below exists precisely to settle both**: re-measure
end-to-end (`/dev/kbd0` present + a live keypress reaching psh, `/dev/mouse0`
present + a real report) on the current standalone-daemon image, because the
keyboard last worked end-to-end (#122) in the **lwip-embedded** era and has **not**
been re-validated since USB moved to the standalone daemon. The plan must not read
"10/10" as "USB is solved."

**Other state:** P1/P2/P3 boot/console fixes are **committed** (kernel
`6cdf217e`, devices `3f82638`; this contradicts the stale memory
`project_pi4_console_p1p2p3.md` which still says "working-tree only" — see the
"Doc/memory corrections" section). SError stays masked (`NO_SERR`); the handler
is committed but dormant (kernel `bcb64610`, TD-10). USB-FIX-18 (the pre-bridge
register dump) was deleted by P1.

---

## Phase 0 — Re-validate end-to-end on the standalone daemon (GATE)

**Why first:** every later phase (cleanup deletions, the mouse work, the FIX
ledger close-outs) needs a trustworthy "is it still working" oracle. The kbd
end-to-end path (#122) predates the standalone-daemon move; nothing in the docs
confirms a live keypress reaches psh in the current config.

**Steps (no source changes — measurement only):**
1. Build current `master` image (`scripts/rebuild-rpi4b-fast.sh`).
2. Run `scripts/test-cycle-netboot.sh` with `--capture-secs 240`; after boot,
   `scripts/diag-udp-probe.sh D devnodes` to stat `/dev/kbd0`, `/dev/mouse0`,
   `/dev/usb` (the `'D'` probe lives in the lwip daemon — `diag-udp.c:382` /
   dispatch at `:6603` — and is network-readable, so it is capture-timing-
   independent and survives even a kbd-absent boot).
3. With a USB keyboard attached, press keys and confirm they reach psh on the
   UART/HDMI console (the #122 path: USB kbd → `/dev/kbd0` → `pl011_kbdthr` →
   psh). To enable the bridge thread, build pl011-tty with `PL011_TTY_KBD_PATH`
   set (it defaults to NULL — `pl011-tty.c:60`).
4. Repeat ≥5 boots (`scripts/test-cycle-bench.sh 5 phase0-e2e`) to measure the
   real per-boot bind rate for kbd and mouse.

**Exit criterion / deliverable:** a short note recording, per boot,
`/dev/kbd0`-present, live-keypress-works, `/dev/mouse0`-present. This *defines
the baseline* every later phase is measured against. No manifest needed (no code
change), but record the image SHA.

**Risk:** if the kbd/mouse bind rate is in fact ~50% (as the #129 log suggests,
not the 10/10 the boot study implies), that reframes priorities: the substantive
open problem is downstream LS-behind-TT enumeration + #124 interrupt-pipe
delivery, and the "10/10" wording in `status.md` needs correcting.

---

## A. Code cleanup / upstreamability (highest near-term priority)

The USB hot path still carries diagnostic and provisional code tied to the #129
investigation and the pre-standalone era. Below, each item is classified
**delete** (safe pure-removal — disproved-hypothesis diagnostic or dead code) or
**productionize** (needs a real replacement first). All are gated behind Phase 0
so any regression is measurable against a known baseline.

### A1 (DELETE) — `LWIP_EMBED_USB` dead code (one coherent commit)

The embedded-USB path is disabled (the Makefile block is commented out:
`phoenix-rtos-lwip/port/Makefile:53-58`). Everything keyed to it is now dead and
should be removed **together**:

- `phoenix-rtos-lwip/port/usb-embed/` — six shim files (`dev.c`, `drv.c`,
  `hcd.c`, `hub.c`, `mem.c`, `usb.c`) that just `#include` the real USB sources
  to give the embed build distinct `.o` paths, plus `xhci-rig-handoff.h`.
- The `#ifdef LWIP_EMBED_USB` block in `port/main.c` (around `:32` and
  `:263`) — the embed worker thread.
- The `#if defined(LWIP_EMBED_USB)` USB diag formatters in `port/diag-udp.c`
  (`:6430-6491`, `:6606`) — the `'U'` command and the `usbkbd_diag*` /
  `xhci_diag*` extern readouts.
- The commented Makefile block itself (`port/Makefile:46-58`).

**Coupled cleanup that dies with it:** the `xhci_diag*` globals in `xhci.c`
(`xhci_diagEventsSeen`, `xhci_diagFix19Rc`, `xhci_diagUsbsts`, `xhci_diagBringupRc`
at `:67-70`) are *written unconditionally* in `xhci.c` (`:1406`, `:1457`,
`:3280`) but only *read* under `LWIP_EMBED_USB` in `diag-udp.c`. Once the readers
are gone they are write-only dead state — remove the globals **and** their write
sites in the same commit, including the explanatory block comment at `:50-65`.

**Verify:** rebuild; lwip links without USB (already the case); Phase-0 re-run
shows no regression. This is the largest, lowest-risk mechanical cleanup.

**Note on `'D'` vs `'U'`:** the live `'D'` devnode probe (`diag-udp.c:382`) is
**separate** and stays — it stats `/dev/kbd0` etc. and is the current USB
observability tool. Only the `'U'` embed-state probe goes.

### A2 (DELETE) — `USBPOOL pa=` debug prints (`phoenix-rtos-usb/usb/mem.c`)

Two `debug()` lines log every USB DMA allocation's PA (`mem.c:86` and `:105`,
both marked `TODO(#129)`). They were added to test the **#26 pmap-aliasing
hypothesis** (USB pool physically overlapping the lwIP heap). That hypothesis is
**refuted**: the kernel-research dive found no permanent cacheable DRAM alias
(memory `usb-dma-write-loss`), and the containment test found the corruption
victim PA *in the gap between pool chunks*, not inside any chunk
(`2026-06-01-usb-rearchitecture-plan.md` "CONTAINMENT TEST RESULT … NEGATIVE →
not aliasing"). Per the project rule (remove diagnostics whose hypothesis was
disproved), these are **safe deletions**.

**Consistency requirement:** because B6 (#26) is being *closed as
not-our-bug-for-this-HW* on exactly this refutation, deleting the probe and
closing #26 must land consistently — do not leave #26 "open" while removing its
only probe.

**Verify:** rebuild; Phase-0 re-run unchanged; grep confirms no remaining
`USBPOOL` references.

### A3 (PRODUCTIONIZE, do NOT delete) — `xhci_cmdRingRecover` (`xhci.c`)

`xhci_cmdRingRecover` (`xhci.c:1787`) aborts a wedged command ring (CRCR.CA),
drains the event ring, and re-inits the producer so the caller's
`HUB_ENUM_RETRIES` retry actually re-executes. It is invoked from the `cmdExec`
timeout path (`:1961`) with the explicit `TODO(#129): if this reliably rescues,
promote to a real bounded retry.`

This is a **safety net for a timeout path the 10-boot study did not exercise**
(0 timeouts on those boots — but kbd/mouse enum is still flaky per #129).
Deleting recovery code because "enum looked clean once" repeats the
premature-confidence trap. Instead:

- Convert the fire-and-return-`-ETIMEDOUT` pattern into the bounded retry its own
  TODO names: a small fixed retry count around the `cmdExec` recover step, so a
  transient wedge is retried `N` times then surfaced as a hard error (no infinite
  loop — the unbounded retry was the source of the Step-3 reboot-loop, since
  bounded in `hub.c`).
- Keep the function; tighten its logging (see A4); drop the `TODO(#129)` marker
  once it is a real bounded retry, replacing it with a plain doc comment.

**Verify:** Phase-0 re-run; ideally exercise the timeout path deliberately
(e.g. a multi-boot bench until a flaky kbd-enum boot hits it) and confirm
recovery + bound, no flood, network stays up.

### A4 (PRODUCTIONIZE) — rate-limited timeout logs (`xhci.c`, `hub.c`)

Three rate-limited diagnostic log sites exist because an unbounded timeout flood
back-pressures the UART and reboots the box:

- `xhci.c:1939-1953` — `cmdExec` timeout: first 12 in full (TRB type, USBSTS,
  USBCMD, ring indices), then suppress. Marked `TODO(#129)`.
- `xhci.c:2780-2789` and `:2899-2908` — transfer-completion timeout: first one in
  full, then suppress.
- `hub.c:309-320` — enumeration-failure log: bounded, then "logging suppressed
  (link flapping)". Marked `TODO(#129)`.

These are **legitimate production behavior** (bounded logging on a fault path is
correct), but they are currently shaped as ad-hoc `static unsigned` counters with
`#129` markers and verbose register dumps tuned for the investigation.
Productionize:

- Keep the rate-limit; convert the verbose first-N register dump to a single
  concise `log_error`/`log_info` line per the canonical USB logging convention
  (`phoenix-rtos-usb/usb/log.h`; memory `phoenix-usb-cross-platform-patterns`).
- Drop the `TODO(#129)` markers (the behavior is keeper, not a debt).

**Verify:** Phase-0 re-run; confirm a flaky-enum boot still bounds the log and
the box stays reachable.

### A5 (DELETE) — throwaway USB-mouse bring-up reader (`pl011-tty.c`, #126)

`pl011_mousethr` (`pl011-tty.c:1067`, marked `TODO(#126-mouse-validate):
throwaway bring-up diagnostic`) opens `/dev/mouseN` and decodes raw 4-byte boot
reports to the UART. It exists only because nothing in-tree consumed
`/dev/mouseN`, so `usbmouse`'s interrupt URBs never started. It is a pure
diagnostic and its comment already says "Remove once a real pointer consumer
exists."

**Sequencing:** delete this **only after** D provides a real consumer (Tiny-X
input or a Quake input shim), OR keep a *minimal* "open the node to kick polling"
stub if mouse URBs must start before a graphics consumer exists. Until then it is
the only thing exercising the mouse path, so removing it prematurely would mask a
mouse regression. Tag it to D's milestone, not A's first commit.

**Verify:** after D, the mouse path is exercised by the real consumer; remove the
thread + its `beginthread` at `pl011-tty.c:1180` + the `#127` TODO observability
hook at `:1022`, and confirm `/dev/mouse0` still delivers reports.

### A6 (DELETE) — remaining `TODO(#129)` diagnostic markers + the top-of-file diag block

After A1-A4, sweep for residual `#129` diagnostic markers and the
cross-process-observability block comment at `xhci.c:50-65`. Distinguish:

- **Diagnostic markers** (delete with their code): the diag globals (A1), the
  `eventAwait` diag note (`xhci.c:1720`), the `scanAll`/`HUB_ENUM_GIVEUP` *infra*
  comments in `hub.c` that are now permanent behavior (rewrite as plain
  comments, drop `#129`).
- **Real behavior markers** (keep, drop only the `#129` tag): `HUB_ENUM_GIVEUP`
  bound (`hub.c:37`), the `devs[]` tracking (`hub.c:341`), the disabled
  initial-scan block (`hub.c:570-580`) — **decide**: the scan was parked after 4
  variants failed; either delete the disabled scan code entirely (it is dead and
  the approach was abandoned) or keep it behind a clearly-named build flag. Prefer
  **delete** for upstreamability unless D needs an at-init port sweep (see D/#124).

**Also (upstreamability, from memory `phoenix-usb-cross-platform-patterns`):**
the wider style cleanup — replace `fprintf(stderr)`/`debug()` in `usb/xhci/` with
`log_*`; remove Pi-4 `debug()` scaffolding from shared `phoenix-rtos-usb/usb/`
code; single-source `XHCI_MAP_SIZE`; stop forking `pcie/server/pcie.c`. These are
larger mechanical churns; schedule them as their own small commits after the
diagnostic deletions, lowest-risk first.

---

## B. USB-FIX / parked-item ledger — close what's done, keep what's real

The FIX-* items are **not** in `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`
(which only tracks TD-NN); they live in the USB notes
(`2026-05-29-usb-reanalysis.md`, `2026-05-30-*`, `2026-06-01-usb-rearchitecture-plan.md`)
and the memory files. The governing constraint: the 2026-06-02 "10/10" result is
**one uncontrolled study, post-P1, not A/B-confirmed**, and it measures hub-enum,
not kbd/mouse bind. **Do not retire any item whose mechanism that study did not
actually exercise.**

| Item | What it is | Verdict | Concrete close-out / follow-up |
|------|-----------|---------|-------------------------------|
| **FIX-7** (skip HCRST if controller usable) | Don't reset a controller the firmware already brought up | **LIKELY OBSOLETE / SUPERSEDED** | The standalone daemon does a full normal `xhci_init` deterministically; the firmware-already-up concern was a bring-up-timing theory (memory `usb-dma-write-loss`, 2026-05-28 PERST theory). Action: confirm `xhci_init` does not gratuitously HCRST a usable controller, then close FIX-7 as folded into the deterministic bring-up. Low effort. |
| **FIX-14** (DMA write asymmetry / "events never post / inbound writes lost") | The historical inbound-DMA wall | **KEEP OPEN — do NOT retire on "10/10"** | Bring-up DMA now lands (eventsSeen>0), so the *bring-up* face is resolved. BUT the #129 wall (`AddressDevice`/ep0 transfers to the LS-kbd-behind-TT get **no completion**) is the same inbound-completion family and is **still flaky** (`project_usb_addressdevice_wall_129`). The 10-boot study did not prove kbd/mouse DMA completions land every boot. Close FIX-14 only when Phase 0 shows reliable downstream completion AND the P1 A/B (C-Phase) confirms the SError abort is not the mechanism. |
| **FIX-17** (MSI / HARD_DEBUG) | Use MSI instead of poll-only events | **OBSOLETE as a correctness fix; OPEN as perf** | Circle/Linux on Pi 4 use **legacy INTA, not MSI**; events DMA-write and are polled fine (memory `usb-dma-write-loss`, 2026-05-27 Circle study). MSI is not the inbound-DMA enabler. Close the "MSI needed for correctness" framing. Keep a perf follow-up: `xhci.c` registers **no IRQ handler** (poll-only); a real IRQ-driven event path matters once USB is reliable + under SMP (memory `phoenix-usb-cross-platform-patterns`). Re-file as a perf task, not a USB-FIX. |
| **FIX-99** (flaky bridge pre-state) | Rapid-cycle bridge degradation; bring-up depends on prior process state | **MOSTLY DISSOLVED by standalone daemon** | The split-process bridge-state hazards (CRCR-stale-after-HCRST, parked `--bridge-only` daemon) are gone now that one long-lived daemon owns bridge + controller (`2026-06-01-usb-rearchitecture-plan.md` Step 3). Action: confirm no `--bridge-only` path remains and the VL805-CRCR-stale memo no longer applies, then close. Watch for any residual rapid-power-cycle flakiness in the Phase-0 bench. |
| **#26** (kernel pmap / MMIO aliasing) | USB DMA pool physically aliasing the lwIP heap | **CLOSE — refuted for this HW** | Refuted twice: kernel keeps no permanent cacheable DRAM alias (research dive, `usb-dma-write-loss`); containment test put the corruption victim in the inter-chunk gap (`2026-06-01-rearchitecture` "CONTAINMENT … NEGATIVE"); 4GB-aperture lead also CLOSED (`addressdevice_wall_129`). The mbox corruption was a **CPU use-after-free of a reused lwIP memp slot**, not aliasing/DMA — and isolation (standalone daemon) made it moot for USB. Close #26; this is the same refutation that justifies deleting the USBPOOL probe (A2). Residual lwIP UAF (`sys_mbox_free` doesn't null `ring`) is an **lwIP** bug, re-file there. |
| **#111** (Linux VL805 / RC config diff) | Does Phoenix's RC/VL805 config diverge from Linux in a way that matters | **LARGELY EXHAUSTED; one open thread** | Cross-OS audits (Circle, pcie-brcmstb) found our RC inbound config matches; the 4GB-vs-3GB `SCB_SIZE`/`dma-ranges` lead was **CLOSED** (inbound abort is address-independent — `addressdevice_wall_129`). The one thread still alive overlaps **C/TD-10**: the live `esr=0xbf000002` external-abort during PCIe/VL805 bring-up — that *is* a Linux-aborts-vs-we-mask difference. Fold #111's remainder into C (TD-10); close the config-diff portion. |

**Net ledger:** close FIX-7, FIX-17 (correctness), FIX-99, #26, and #111's
config portion as resolved/obsolete; **keep FIX-14 open** pending Phase-0
downstream evidence + the C-Phase A/B; re-file the IRQ-driven-events (ex-FIX-17)
and lwIP `sys_mbox_free` items in their proper homes. Deliverable: one
coordination-repo commit updating the ledger (and `status.md`'s "10/10" wording,
see Phase 0).

---

## C. Understand + fix TD-10 — the masked SError / PCIe external abort

### What TD-10 is (for the record)

On AArch64, **SError** is an asynchronous external-abort exception (bus/parity
errors, externally-aborted device accesses). Phoenix runs the Pi 4 with SError
**masked** everywhere via the `NO_SERR` (0x100) bit in the PSR
(`hal/aarch64/cpu.c:79,83` for thread contexts; `_exceptions.S:386` for the
`hal_jmp` user-entry branch; macro at `arch/cpu.h:41`). A proper handler **is**
implemented — `exceptions_serrorHandler` (`hal/aarch64/exceptions.c:208`,
registered at `:349`, kernel commit `bcb64610`) — which dumps ESR/ELR/FAR and
**halts** (never reboots, to preserve the evidence). It is committed but
**dormant**, because removing `NO_SERR` and unmasking on real Pi 4 revealed a
**live external-abort SError source in the BCM2711 PCIe / VL805 USB bring-up**:

- Signature: `esr=0xbf000002` → EC=0x2F (SError), IL=1, **IDS=1** (so the rest of
  the ISS is A72 IMPLEMENTATION-DEFINED — EA/AET/DFSC are *not* architecturally
  decodable), `far=0`, **imprecise** (captured PC is wherever EL0/EL1 happened to
  be, not the faulting access).
- Isolation-proven (memory `pi4-serror-pcie-source`): with USB disabled, boot
  runs 15+ s with full networking and **zero** SErrors; with USB enabled, the
  first SError fires exactly when `usb-hcd ops->init` begins.
- Earlier localization (log-and-continue handler): 17 SErrors in two bursts, both
  in USB bring-up (`bcm2711_pcie_initVL805` and the old lwip DRIVE_ONLY path).
  **No synchronous aborts** anywhere — so it is a posted-write / fabric **async**
  abort charged imprecisely, NOT the bridge-register reads.
- The RC **outbound**-error latch (`0xfd500000 + 0x6004..0x6020`) reads **clean**
  on a failing boot (`bcm2711_pcie_dumpRcErr`, since removed) → not a swallowed
  CPU→PCIe *outbound* abort. Leading remaining hypothesis: an **inbound**
  (VL805→memory) abort, visible only as the masked SError.

This is the kind of masked software fault the project owner correctly suspected
(USB kbd works on this board under Linux/firmware → our bug). It is **both** the
TD-10 unmask blocker **and** a primary mechanistic lead for the USB
downstream-completion wall (an externally-aborted controller→memory access ⇒
"completion never posts").

### The open question post-P1

P1 deleted the USB-FIX-18 diagnostic that read 10 RC registers **before**
`bcm2711PrepareHostBridge` clears `SERDES_IDDQ` — each of those pre-IDDQ accesses
hit the external-abort path. The 10-boot study credits P1 with USB enum going
3/10→10/10 (`status.md`), implying those aborts were a real contributor. **But:**
P1 removed a *diagnostic* trigger; it is **unconfirmed** whether a *real*
external-abort source still fires on the current code when SError is unmasked
(the prior 17-SError localization included `bcm2711_pcie_initVL805` itself, not
just the dump). The honest position: P1 may have removed *some or all* triggers;
re-measurement is required.

### Step-by-step diagnostic + fix plan (investigation — unknowns, not a guaranteed fix)

This is research-shaped. JTAG is unavailable; the imprecise async signature makes
UART localization hard (the storm garbles multi-core logging). Use the
network-readable diag-udp path and the RC error-latch registers, not UART floods.

1. **Re-attempt the unmask on current consolidated code.** Remove `NO_SERR` from
   the three PSR sites (`cpu.c:79,83`; `_exceptions.S:386`), keeping the
   dump-and-halt handler. Boot once. *Outcome A:* clean boot, no SError → P1 (and
   the consolidated bring-up) removed the source; proceed to step 4 to confirm and
   close TD-10. *Outcome B:* the handler halts on an SError → a real source
   remains; capture the ESR/ELR/FAR dump (single halt, so it is not garbled) and
   go to step 2.
2. **Bisect the trigger (only if B).** Temporarily switch the handler to
   log-and-continue (the prior async+far=0 ⇒ eret-resume-safe approach) so the
   boot proceeds and you can correlate. Localize *which* PCIe/xHCI access fires it
   by gating bring-up phases: bridge-prepare (IDDQ clear) → RC config →
   VL805 config → `xhci_init` reset → first doorbell. The 2026-05-29 localization
   already pointed at `bcm2711_pcie_initVL805` + the controller bring-up; confirm
   on current code. Cross-check the RC **outbound** error latch
   (`0xfd500000+0x6004..0x6020`) over diag-udp (it persists, no UART garble): a
   VALID=1 latch names the exact offending CPU→PCIe address + cause; VALID=0
   confirms the inbound-abort hypothesis.
3. **Candidate fixes (in increasing cost):**
   - **Ordering / barriers:** ensure no RC/VL805 access happens before
     `SERDES_IDDQ` is cleared and the link is up (P1 was exactly this class —
     extend the audit to every access in `bcm2711-pcie.c` / `xhci_init`). Add the
     missing `dsb`/read-back fences around bridge-window programming.
   - **Only-touch-RC-after-IDDQ-clear:** make the bring-up structurally refuse any
     RC/MEM-window access until the link-up + IDDQ-clear gate has passed (turn the
     P1 lesson into an invariant, not a one-off deletion).
   - **Inbound-abort root cause (if step 2 says inbound):** decode the VL805/RC
     inbound config against Linux pcie-brcmstb on the 3 GB-limited Pi 4 wrapper;
     the address-independence finding (`addressdevice_wall_129`) argues against a
     window/`SCB_SIZE` misconfig, so look at MISC_CTRL / inbound-decode enables /
     a NACK on a specific transaction type rather than an aperture bug.
   - **ESR/FAR decode tooling:** keep the handler's raw ESR dump; add a small
     decode (EC/IL/IDS, and for IDS=0 the EA/AET/DFSC fields) so future SErrors
     are self-describing.
4. **Exit criterion:** SError **unmasked** (remove `NO_SERR` permanently from all
   three sites) **and** a clean multi-boot bench (≥5) with full networking and
   USB bring-up, handler armed, zero SErrors. Add the synthetic-SError injection
   test TD-10 requires. Then close TD-10 (remove the entry + the `NO_SERR`
   markers).

**Honesty caveat:** this phase may *not* yield a fix — if a real inbound abort
remains and is JTAG-gated, the deliverable is a fully-characterized,
network-readable diagnosis + a decision (e.g. keep SError masked **only** for the
specific bring-up window, unmasked elsewhere) rather than a clean unmask. State
that outcome explicitly if reached; do not force a premature close.

**Dependency note:** C is the deepest lead for FIX-14 (B). If C confirms an
inbound abort during downstream transfers, that likely *is* the kbd/mouse
completion wall — making C and the downstream-enum work two faces of one bug.

---

## D. A production mouse driver (toward Tiny-X / Quake)

### Honest scope

There is **no graphics stack in-tree** — no Tiny-X, no X server, no `libpixman`,
no Quake, nothing under `phoenix-rtos-ports` for graphics (verified). A separate
`docs/todo/gpu-quake-bringup-plan.md` exists for that workstream. So D is **not**
"integrate the existing tinyx port"; the X/Quake bring-up is a large, separate,
mostly-from-scratch effort. **What the USB workstream owes graphics is a narrow,
well-defined contract**: a reliable, low-latency `/dev/mouseN` with a documented
report format an input layer can consume. Do not fold the X/Quake port into this
plan.

What already exists: `tty/usbmouse/usbmouse.c` is a **real** driver — it creates
`/dev/mouseN`, has an rx fifo, whole-packet (`mtRead`) reads, `poll` support, and
forwards 4-byte HID **boot-protocol** reports (buttons + relative X/Y + wheel,
`usbmouse.c:66-70,208-218`). It is *not* a throwaway. The throwaway is
`pl011_mousethr` (A5). `USBMOUSE_N_URBS` is hardcoded to 1 with the same crash
rationale as the keyboard (`usbmouse.c:58-63`).

### Deliverables (USB-side only)

1. **Document + stabilize the report contract.** Define and document the
   `/dev/mouseN` wire format the input layer consumes. Today it is raw 4-byte
   boot-mouse packets (relative). Decide whether the contract is (a) boot-protocol
   relative packets (simplest; sufficient for Quake-style relative-look and for a
   relative X pointer), or (b) a richer struct. Write it down so the future X /
   Quake input shim has a stable target. This is mostly documentation + a header.
2. **Boot report vs report-protocol parsing.** The driver currently forces
   **boot protocol** (`usbmouse_setProtocol`, `usbmouse.c:221`). For a production
   pointer (extra buttons, hi-res wheel, higher report rates) plan optional
   **report-protocol** parsing (parse the HID report descriptor, locate the X/Y/
   button/wheel fields). Keep boot protocol as the reliable default; report
   protocol is an enhancement gated on the HID-descriptor-parse work. Absolute
   pointing (touchscreen/tablet) is out of scope unless such a device is on the
   bench — most USB mice are relative-only.
3. **Multi-URB low-latency delivery (shared with #124 — the keystone).** Both
   `usbmouse` and `usbkbd` hardcode `N_URBS=1` because queuing multiple URBs on a
   single interrupt pipe **crashes the HCD** (`usbkbd.c:46-62`, `usbmouse.c:58-63`:
   the xHCI per-slot interrupt pipe tracks a single in-flight priv). With one URB
   there is a re-arm gap between completion and resubmit, so fast events drop
   (observed: 3 keypresses, 1 echoed). The real fix is **HCD-level**
   (`xhci.c` interrupt-pipe support for a ring of in-flight URBs per pipe, or a
   faster in-completion resubmit) — this is #124 and is a **shared prerequisite**
   for both reliable keyboard *and* a usable mouse. The per-driver `N_URBS` bump
   is only safe *after* the HCD work; do not bump the constant before then.
4. **Remove the throwaway reader (A5)** once the real consumer exists.

### Integration points (forward-looking, not this workstream)

- Tiny-X / a framebuffer X server would open `/dev/mouseN` (relative) +
  `/dev/kbd0` and the HDMI fbcon framebuffer (already up — P2). The input contract
  from D1 is the handoff.
- Quake's input layer similarly consumes relative mouse deltas + key events. The
  same `/dev/mouseN` + `/dev/kbd0` contract serves it.
- Both depend on #124 delivery reliability — a dropped-keypress / laggy-pointer
  device is unusable for interactive graphics. **#124 is the real gate for D being
  worth shipping.**

---

## E. Sequencing + dependencies

```
Phase 0  Re-validate end-to-end on standalone daemon         [GATE for all below]
            |
   +--------+-----------------------------+-------------------------+
   |                                       |                         |
A. Cleanup (mostly independent)     C. TD-10 SError           B. FIX ledger
   A1 LWIP_EMBED_USB dead code         (investigation,           (doc/ledger;
   A2 USBPOOL prints  -- ties --       parallel)                 ties to A2/#26,
   A4 rate-limit logs                  step1 unmask retry        to C for FIX-14)
   A3 cmdRingRecover (productionize)   step2 bisect
   A6 #129 marker sweep + style        step3 candidate fixes
   |                                    step4 unmask + close
   |
#124  HCD multi-URB / low-latency interrupt-pipe delivery   [shared prerequisite]
   |        (the real substantive driver work; gates reliable kbd AND mouse)
   |
D. Production mouse driver
   D1 document report contract
   D2 report-protocol parsing (optional, after HID-descriptor parse)
   D3 multi-URB delivery (== #124)
   D4 remove pl011_mousethr (A5)  [after a real consumer exists]
```

**Ordering rationale:**

- **Phase 0 gates everything** — without a trustworthy end-to-end oracle, every
  later "no regression" claim is unfounded, and the "10/10" wording may be
  masking a ~50% downstream bind rate.
- **A (cleanup) goes first and is mostly independent.** A1/A2/A4/A6 are
  delete/productionize commits that don't change USB *behavior* (except A3, which
  preserves behavior while productionizing). Each is one small reviewable commit +
  a manifest snapshot. A2 and B/#26 must land consistently. A5 waits for D.
- **#124 (multi-URB HCD delivery) is the substantive shared prerequisite** for
  both reliable keyboard and a usable mouse (D). It is HCD-level work in `xhci.c`,
  the highest-value functional task after cleanup. It also likely shares a root
  with the downstream-enum flakiness (`2026-06-01-rearchitecture` "NEXT FOCUS =
  interrupt-pipe reliability (#124)").
- **C (TD-10) runs in parallel** — it is investigation with unknowns, doesn't
  block cleanup, and feeds FIX-14's close-out. If C confirms an inbound abort
  during transfers, C and #124 may converge on one bug.
- **B (ledger)** is a documentation/coordination task; it depends on A2 (#26),
  C (FIX-14), and Phase 0 (the bind-rate truth), so it closes *after* those
  inform it — but the obviously-obsolete items (FIX-7, FIX-17 correctness, FIX-99,
  #26, #111-config) can be closed as soon as A2 lands.
- **D** is last among USB work and gated on #124; the X/Quake consumer is a
  separate workstream.

**Per-phase hardware validation (all use the same harness):**
`scripts/rebuild-rpi4b-fast.sh` → `scripts/test-cycle-netboot.sh --capture-secs
240` → `scripts/uart-summary.sh <label>` for stage health, plus
`scripts/diag-udp-probe.sh D <label>` for `/dev/kbd0`/`/dev/mouse0` presence, plus
live keypress for the kbd e2e path. Multi-trial (`scripts/test-cycle-bench.sh N
<label>`) for any reliability claim — single boots are anecdotal given documented
per-boot non-determinism.

**Rollback / manifest discipline (every validated phase):**
commit the touched sibling repo(s) in small reviewable commits, then
`scripts/snapshot-integration-state.sh` to write a `manifests/<date>-<label>.md`
recording all sibling SHAs; `scripts/restore-integration-state.sh <manifest>`
rolls back. The current known-good baseline is the committed `master` state
(USB standalone daemon stable, manifest
`2026-06-01-usb-standalone-daemon-stable`).

---

## Doc / memory corrections found while writing this plan

- **`project_pi4_console_p1p2p3.md` is out of date:** it states P1/P2/P3 are
  "working-tree only … not committed." They ARE committed (kernel `6cdf217e`
  "permanent klog UART mirror + serialized console (P2/P3)"; devices `3f82638`
  "drop PCIe pre-init bridge dump (P1); klog->fbcon direct drain (P2)").
- **The two USB-enum sources conflict; the metric does NOT prove a live keypress
  reaches psh.** The postfix 10-boot study claims full **hub + mouse + keyboard**
  enumeration 10/10 (`2026-06-02-p1p2p3-postfix-10boot.md:73-77`). Its `enum_fail`
  metric is `compare-boots.py`'s count of `hub.c`'s "Enumeration failed" line
  (`hub.c:318`), which fires for **any** failing port — hub *or* downstream device
  — so `enum_fail=0` is a genuine signal that downstream kbd/mouse ports
  enumerated too (my earlier "hub-only" read was wrong; verified against
  `compare-boots.py` + `hub.c`). The 06-01 #129 log's ~33-50% downstream-bind
  figure **predates the P1 fix** (devices `3f82638`, 06-02) that the study credits
  with the improvement, so it may be obsolete. What is *still* unverified either
  way: (a) `enumerated` (port addressed/configured, `/dev/kbd0` created) is not the
  same as a **live keypress reaching psh** — and `PL011_TTY_KBD_PATH` defaults to
  `NULL` (`pl011-tty.c:60`), so the kbd→psh bridge thread may not even be wired in
  the default image; (b) the kbd e2e path has not been re-run on the standalone
  daemon at all. Phase 0 resolves both. `status.md`'s "10/10" is about
  *enumeration*; it should be qualified with "live keypress→psh not re-validated
  on the standalone daemon" once Phase 0 measures it — but do **not** call it an
  overstatement of enumeration.
- **FIX-* items are not in `TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`** (only TD-NN
  are). If the FIX ledger is to be authoritative it should move into that file or
  a dedicated `docs/inprogress/usb-fix-ledger.md`; today it is scattered across notes +
  memories, which is why B above had to reconstruct it from primary sources.
- **The disabled hub initial-scan code** (`hub.c:570-580`) and four parked scan
  variants are dead-but-present; the approach was abandoned (`addressdevice_wall_129`
  "scan-vs-interrupt distinction is likely noise"). It should be deleted for
  upstreamability unless #124 revives an at-init port sweep.
</content>
</invoke>
