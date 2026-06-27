> **RETIRED (2026-06-27): superseded — the AddressDevice wall is FIXED.** This source
> comparison fed the fix that landed: the two-step BSR Address Device (devices `53383d1`)
> + TRSTRCY reset-recovery (usb `47eede9`) + the #121 dc-civac uncached-page eviction
> (usb `12c4fe8`). USB now enumerates kbd0+mouse0 on 11/11 cold boots. Kept for the
> research record of how the BSR signature was derived. See
> `2026-06-27-usb-nfs-reliability-deep-dive.md` (in docs/done/) for the resolution.

# Phoenix vs Linux xHCI: why AddressDevice intermittently never completes on the Pi 4 VL805

Date: 2026-06-27
Scope: pure source comparison (no Pi boots). Goal: find the software divergence that
makes `AddressDevice` (slot for the root-port VIA hub) intermittently post **no**
command-completion event (~3/4 cold boots) while the controller stays healthy.

Sources compared:
- Phoenix: `sources/phoenix-rtos-devices/usb/xhci/xhci.c`, `.../usb/xhci/bcm2711-pcie.c`,
  `sources/phoenix-rtos-usb/usb/{hub.c,dev.c,mem.c}`
- Linux oracle: `external/linux/drivers/usb/{host/xhci*.c,core/hub.c}`

---

## The decisive constraint (re-derived from the hard evidence + Linux/xHCI spec)

Established on HW: on a failing boot the AddressDevice completion is **genuinely ABSENT**
(full event-ring scan finds no TRB with our `parameter`), yet USBSTS HCH=0/HSE=0/HCE=0 and
IMAN.IP=0 — controller running and healthy. EnableSlot (issued just before) **always**
completes.

xHCI architecture (spec §4.6.1) **guarantees a Command Completion Event for every command
TRB the controller dequeues.** Linux relies on this: `xhci_setup_device` blocks on
`wait_for_completion` and the switch handles a device-not-ready as an *event with a code*
(`xhci.c:4526-4537`, `COMP_USB_TRANSACTION_ERROR` → "Device not responding" → `-EPROTO`),
never as a missing event.

**Therefore "no completion at all" is not the signature of a device that isn't ready for
SET_ADDRESS.** A not-ready device would make the xHC run its CErr retries and then post a
completion *with an error code* — which our ring scan would see as PRESENT. ABSENT means the
controller **never dequeued/processed the command TRB**, i.e. a doorbell / command-ring
(CRCR / cycle-bit / dequeue) / run-state / **input-context inbound-DMA** problem — not a
wire/readiness problem.

This single observation is what reorders the ranking below: the "obvious" Linux diffs
(TRSTRCY recovery delay, BSR two-step) predict the *wrong* signature, so they drop from the
top even though they are real divergences.

**The asymmetry that matters:** EnableSlot's command TRB carries `parameter = 0` — the
controller reads nothing from system memory. AddressDevice's TRB carries
`parameter = inputCtxPhys` — the controller must **inbound-DMA-read the Input Context** before
it can act. AddressDevice is the *first* command that forces such a read.

**Critical refinement (applies the completion-guarantee symmetrically):** *stale or garbage*
input-context data does NOT fit ABSENT either — a controller that dequeues the TRB and reads
bad data still completes the command with an error code (Parameter Error / Context-State
Error) = PRESENT, exactly like the device-not-ready case. The only input-context-read failure
mode that fits **ABSENT + HCH=0 + HSE=0 + HCE=0** is a **dropped / non-completing inbound TLP
that wedges the command processor mid-command**: the controller issues the inbound read for the
input context, the BCM2711 bridge drops it (transient inbound-translation state), the read
*never returns*, so the command processor never reaches the completion-write — and the
resulting external abort is **absorbed by the BCM2711 SError** path (see
`project_pi4_serror_pcie_source` — unmasking SError exposed a live external-abort source in
PCIe/VL805 bring-up, esr=0xbf000002). That trio — completion ABSENT, no HSE, no HCE, controller
not halted — is coherently explained *only* by a dropped-read wedge, not by stale data and not
by a wire-readiness NAK (both of which would post an error completion).

**Why the generic command-ring path is proven innocent:** `xhci_cmdNoopSelftest` (`xhci.c:3084`)
and `xhci_cmdEnableSlot` (`xhci.c:3086`) are commands #1 and #2 on the *same* command ring,
both complete, no ring wrap between them and AddressDevice (#3), constant cycle bit, CRCR
already published. So cmd-ring fetch / cycle-bit / CRCR / doorbell / event-ring consumption are
*empirically working* for two prior commands. The failure must therefore be in what
AddressDevice *adds* over EnableSlot — the input-context inbound read — not in the command-ring
machinery.

Linux refs for the asymmetry:
- EnableSlot params all zero: `xhci-ring.c:4516-4520` `queue_command(xhci, cmd, 0, 0, 0, TRB_TYPE(trb_type) | SLOT_ID_FOR_TRB(slot_id), false)`
- AddressDevice TRB carries `in_ctx->dma`: `xhci-ring.c:4524-4530`, pointer from `xhci.c:4496`
- Input context is **coherent** DMA (`dma_pool_zalloc(device_pool)`, `xhci-mem.c:470`,
  pool created `xhci-mem.c:2481`) — Linux needs no per-command flush *only because* the pool
  is coherent. A non-coherent / wrong-window buffer breaks exactly this read.

---

## RANKED divergences

### #1 (TOP SYMPTOM-FIT) — Input-Context inbound DMA read for AddressDevice is the first of its kind; nothing re-validates the inbound path or barriers the late-allocated context

**Confidence the symptom fits: HIGH. Confidence it's THE cause: MEDIUM (needs the BSR=1 partition test below).**

Phoenix builds the Input Context and its DCBAA slot entry in allocations that happen *after* the
controller's inbound window was last re-settled. For slots[0] this is at init
(`xhci.c:3088`, right after EnableSlot, after the NoOp's pre-R/S re-settle fired); for
behind-hub slots it is later, during the first ep0 transfer (`xhci.c:2405`):
- `xhci_allocSlotSpace` (`xhci.c:2044-2106`): `usb_allocAligned` the input ctx + dev ctx +
  ep0 ring, `va2pa`, then `dcbaa[slotId] = devCtxPhys` (line 2098).
- `xhci_prepareAddressContext` (`xhci.c:2199-2293`): `memset` + field writes into the input ctx.
- `xhci_cmdAddressDevice` (`xhci.c:2022`) → `xhci_cmdExec` issues the command.

These buffers come from the USB DMA pool (`usb/mem.c:199`: `MAP_UNCACHED | MAP_CONTIGUOUS`,
Normal-NonCacheable), and the 4 GB inbound BAR2 window (`bcm2711-pcie.c:860/1304/1380`
`bcm2711SetRcBar2(bcm, 0u, 0x100000000ull)`) covers all of low DRAM, so *static* coverage is
fine. What is NOT guaranteed on a per-boot basis:

1. **Inbound-window re-settle vs. the input-ctx mmap.** `bcm2711_pcie_resettleOutboundWindow()`
   is called after HCRST (`xhci.c:952`) and once more right before the first R/S=1, inside the
   NoOp's `enterRunState` (`xhci.c:1300-1305`). For **slots[0]** (the failing root-port-hub
   slot) `xhci_allocSlotSpace` runs at init `xhci.c:3088` — right after EnableSlot, i.e.
   **after that last pre-R/S re-settle has already fired** (it fired inside the NoOp). For
   behind-hub slots `allocSlotSpace` runs even later, during the first ep0 transfer
   (`xhci_allocSlotForDev`, `xhci.c:2405`). Either way the input ctx + ep0 ring DMA buffers are
   `mmap`'d **after** the inbound window was last asserted. The codebase's own comments
   repeatedly warn that any new MAP_DEVICE/DMA mmap in this process can churn the bridge inbound
   (RC_BAR2/UBUS_BAR2) translation (`xhci.c:1293-1299`, `xhci_map` comment `xhci.c:752-757`). If
   the inbound translation is in a transient state when the controller issues the inbound read
   for the input context, the read is **dropped (TLP never returns) → the command processor
   wedges mid-command → no completion is ever written**, while the abort is SError-absorbed so
   HCH/HSE/HCE stay clear. Per-boot timing-dependent → matches the ~3/4 intermittency.

2. **No barrier tying the late context writes to the command.** The only `dsb sy` is inside
   `xhci_cmdExec`/`xhci_enterRunState` (`xhci.c:1317`, `1841`) just before the doorbell. That
   *does* order the writes on the CPU side, so a pure CPU store-buffer race is ruled out (this
   matches Linux's `wmb()` in `queue_trb`, `xhci-ring.c:3323`). So #1's mechanism is the
   bridge inbound translation, not a CPU barrier.

**Why EnableSlot escapes:** it reads nothing (`parameter=0`), so an unstable inbound window is
invisible to it — exactly the observed "EnableSlot always works, AddressDevice doesn't."

**Linux match:** Linux's input context is coherent and its inbound IOMMU/dma-ranges window is
established once and never churned by later allocations. Phoenix's per-process bridge-mapping
design (USB-in-lwip / merged xhci-owns-bridge) is the divergence.

**Proposed change (after the partition test confirms direction):**
- Re-settle the inbound window immediately before issuing AddressDevice (call
  `bcm2711_pcie_resettleOutboundWindow()` at the top of `xhci_cmdAddressDevice`, or once after
  `allocSlotSpace`/`prepareAddressContext` and before `cmdExec`), so the controller's
  input-ctx read happens against a freshly-asserted RC_BAR2/UBUS_BAR2 translation. Add a
  `dsb sy` after the context writes (already implied by cmdExec, but make it explicit at the
  end of `prepareAddressContext`).
- Stronger structural fix: pre-allocate the slots[0] input/dev ctx + ep0 ring during
  `xhci_init` (before the *last* pre-R/S re-settle at `xhci.c:1300`), so no DMA buffer the
  controller will read is ever mmap'd after the inbound window is settled. This removes the
  late-mmap churn entirely for the root-port hub slot.

---

### #2 (HEADLINE EXPERIMENT — partitions the whole ranking) — Issue AddressDevice with BSR=1 first, then BSR=0

**This is not primarily a "fix"; it is the one HW test that splits the entire hypothesis space.**

Linux's "new scheme" issues AddressDevice **BSR=1** (`SETUP_CONTEXT_ONLY`) first — sets up the
slot/ep0 context with **no SET_ADDRESS token on the wire** — then BSR=0:
- BSR bit set from the setup enum: `xhci-ring.c:4530` `... | (setup == SETUP_CONTEXT_ONLY ? TRB_BSR : 0)`
- BSR=1 step: `xhci_enable_device` → `SETUP_CONTEXT_ONLY` (`xhci.c:4601-4604`)
- BSR=0 step: `xhci_address_device` → `SETUP_CONTEXT_ADDRESS` (`xhci.c:4595-4598`)
- Orchestrated in usb-core: `hub.c:5037 if (do_new_scheme) hub_enable_device(udev); ...`
  then always BSR=0 at `hub.c:5074`. (So it is conditional/new-scheme, not unconditional.)

Phoenix issues **BSR=0 single-step** directly:
`xhci_cmdAddressDevice(xhci, xhci->cur, 1)` with `setAddress=1` → BSR bit NOT set
(`xhci.c:1991-1993`, `3482`).

**Why this is the headline test:** both BSR=1 and BSR=0 dequeue the command and force the
controller to inbound-DMA-read the Input Context (#1). The *only* delta is that BSR=0 also runs
the on-the-wire SET_ADDRESS token. So one HW run partitions **where** the controller wedges:
- If **BSR=1 completes reliably** → the wedge is at the **wire SET_ADDRESS step**, and the fix
  is the **BSR two-step itself** (do BSR=1 then BSR=0). NOTE: this is *not* the same as "add a
  TRSTRCY delay" — a not-ready device on the wire would have posted an *error* completion, which
  we never saw, so a wedge-at-wire that yields ABSENT is a VL805 single-step-BSR quirk, not a
  10 ms-timing problem. Do not "fix" a positive BSR=1 result with a delay; adopt the two-step.
- If **BSR=1 is ALSO intermittently ABSENT** → the wedge is at the **context read / command
  processing** (#1: dropped inbound TLP), and both the wire step and TRSTRCY are exonerated.
  (This is the outcome the ABSENT-not-error signature predicts.)

**Proposed change for the test:** add a BSR=1 AddressDevice immediately before the existing
BSR=0 one in the `address==0` block (`xhci.c:3471-3488`) and in `xhci_allocSlotForDev`
(`xhci.c:2429`): call `xhci_cmdAddressDevice(xhci, slot, 0)` (setAddress=0 → BSR=1) then
`xhci_cmdAddressDevice(xhci, slot, 1)` (BSR=0). Note the existing one-shot ep0-ring caveat in
the comment at `xhci.c:3443-3455`: a second BSR=0 would reload trDequeuePtr; a BSR=1 then BSR=0
pair is the spec-sanctioned sequence and avoids that. Keep the framework SET_ADDRESS ack path
unchanged.

---

### #3 (CONFIRMED DIVERGENCE; mechanism-fit POOR) — No TRSTRCY (port reset-recovery) delay before AddressDevice

**Confirmed divergence. But predicts an error completion, not ABSENCE — so it likely is NOT
the #129 cause. Worth doing for spec-compliance / robustness, and it is cheap, but rank its
*causal* likelihood low.**

Linux waits after every successful port reset, before addressing:
- `hub.c:3157-3164`: `/* TRSTRCY = 10 ms; plus some extra */ reset_recovery_time = 10 + 40;`
  `msleep(reset_recovery_time);` (i.e. **50 ms** default, +100 for SLOW_RESET hubs), inside
  `hub_port_reset` done-path.
- Plus `hub.c:5106 msleep(10)` after SET_ADDRESS ("let SET_ADDRESS settle") in `hub_port_init`.
- Reset timing constants: `HUB_ROOT_RESET_TIME 60`, `HUB_SHORT_RESET_TIME 10`,
  `HUB_LONG_RESET_TIME 200`, `HUB_RESET_TIMEOUT 800` (`hub.c:2900-2904`).

Phoenix waits **0 ms** of recovery:
- Root port: `xhci_setPortFeature(RESET)` (`xhci.c:3297-3308`) asserts PR, waits PR→0, waits
  PLS→U0, then returns immediately. No recovery delay.
- Downstream hub ports: `hub_portReset` (`hub.c:200-218`) sets FEAT_RESET, polls C_RESET,
  clears change bits, returns — no recovery delay.
- Enumeration then proceeds straight into `usb_devEnumerate` → `usb_getDevDesc` → AddressDevice
  (`dev.c:644`, then the ep0 path triggers `xhci_cmdAddressDevice` at `xhci.c:3482`).

**Proposed change:** add `usleep(10000)` (TRSTRCY; 50 ms to match Linux's margin) after PLS=U0 in
`xhci_setPortFeature(RESET)` (`xhci.c:3304-3308`) and after the change-bit clear in
`hub_portReset` (`hub.c:215`). Low-risk, spec-mandated; do it regardless, but don't expect it
to fix the ABSENT-completion wall by itself.

---

### #4 (RULED OUT as cause; minor spec note) — Controller bring-up / register ordering

**Matches Linux closely enough; No-Op + EnableSlot completing proves the command ring, CRCR,
doorbell, DCBAA, run-state and event-ring path all work. Not the cause.**

- Phoenix order before R/S: CONFIG.MaxSlotsEn → DCBAAP_LO/HI → CRCR_LO/HI
  (`xhci.c:1232-1236`), scratchpad installed into DCBAA[0] earlier (`xhci.c:1185`,
  before DCBAAP write), event ring programmed, then R/S in `xhci_enterRunState`
  (`xhci.c:1340-1341`, sets RUN|INTE|HSEE together like `xhci_run`).
- Linux order: CONFIG (`xhci.c:562`) → CRCR (`xhci.c:565`) → DCBAAP (`xhci.c:568`) →
  doorbell ptr → ERST → RUN (`xhci.c:148-151`). Only diff: Phoenix writes DCBAAP before CRCR;
  Linux CRCR before DCBAAP. Spec allows any order before RUN; the No-Op/EnableSlot self-test
  proves Phoenix's order works. **Ruled out.**
- HCRST/CNR waits match Linux (`xhci.c:884` CNR-before, `917` HCRST self-clear, `926` CNR-after,
  `943` 100 ms post-reset settle vs Linux's `xhci.c:226` CNR handshake + Intel udelay).
  Phoenix even halts-before-reset like Linux (`xhci.c:901-912`). **Matches.**

One genuine *latent* gap (NOT the intermittent cause, but a correctness note): the known-broken
command-ring recovery after a timeout (`xhci_cmdRingRecover`, CRR won't clear on abort,
`xhci.c:1724+`). This is downstream of the first-attempt failure and only matters for retries;
flagged but out of scope for "why attempt #1 doesn't complete."

---

### #5 (RULED OUT) — Scratchpad / MaxScratchpadBuffers

**Matches Linux. Ruled out** — and if it were wrong the controller could not run No-Op /
EnableSlot at all (it would hang at R/S, which is the documented failure mode the code already
fixed).

- VL805 reports 31 scratchpad buffers; Phoenix allocates the array + 31 page buffers and writes
  `dcbaa[0] = scratchpadArrayPhys` (`xhci.c:1117-1192`), before DCBAAP (`xhci.c:3055` ordering),
  before R/S. Matches Linux `scratchpad_alloc` → `dcbaa->dev_context_ptrs[0] = sp_dma`
  (`xhci-mem.c:1709`), installed in `xhci_mem_init` before RUN.

---

### #6 (RULED OUT) — Context size (CSZ / 64-byte contexts)

**Matches Linux for this controller; and EnableSlot succeeding proves it.** Phoenix reads
`HCCPARAMS1.CSZ` and rejects anything but 32-byte contexts (`xhci.c:973`, `1002-1005`). The
VL805 uses 32-byte contexts (CSZ=0); init would hard-fail otherwise. If the size were wrong the
output device context written by EnableSlot/AddressDevice would be misparsed, but EnableSlot's
slot-id return is correct. Linux: `CTX_SIZE(HCC_64BYTE_CONTEXT(...))` (`xhci-caps.h:43,63`).
**Ruled out.** (Stated explicitly because the task asked.)

---

### #7 (CONFIRMED DIVERGENCE; not enum-path causal) — VL805 quirks Phoenix doesn't apply

Linux applies, for VIA 0x1106:0x3483 (`xhci-pci.c:460-468` + vendor-wide `:448-449`):
`XHCI_LPM_SUPPORT`, `XHCI_TRB_OVERFETCH`, `XHCI_EP_CTX_BROKEN_DCS`, `XHCI_AVOID_DQ_ON_LINK`,
`XHCI_VLI_SS_BULK_OUT_BUG`, `XHCI_RESET_ON_RESUME`, and (fw < 0x0138C0)
`XHCI_VLI_HUB_TT_QUIRK`. Phoenix applies none.

Relevance to the AddressDevice wall: **low**. None of these alter the Enable Slot → Address
Device sequence:
- `XHCI_TRB_OVERFETCH` (`xhci-mem.c:2470-2473`): doubles ring segment alloc/alignment because
  the HC prefetches past segment bounds. *This is the one worth noting* — Phoenix's command
  ring is a single 4 KB page (`xhci.c:151-152`) and the event ring a single 4 KB page
  (`xhci.c:154`). If the VL805 over-fetches past the 4 KB command-ring segment boundary while
  reading the command TRB, it could read garbage — a *possible* secondary contributor to a
  mis-processed AddressDevice TRB. Low probability (the cmd TRB is near ring start, far from the
  page end) but cheap to harden (double the command/event ring segment size or pad a guard
  page). Listed under #7 rather than #1 because the ABSENT signature points at the input-context
  read, not the cmd-ring fetch, and EnableSlot (same ring) succeeds.
- Others (`EP_CTX_BROKEN_DCS`, `AVOID_DQ_ON_LINK`, `VLI_SS_BULK_OUT_BUG`, `LPM`, `HUB_TT`,
  `RESET_ON_RESUME`) act on transfer-ring dequeue recovery, USB3 LPM, UMS bulk-OUT, or
  resume/runtime-PM — none on AddressDevice. **Not the cause.**

Probe-time notes (informational, not causal): Linux skips the BIOS/xHCI handoff for VL805 under
bcm2711-pcie (`pci-quirks.c:1276-1282`) and applies a PCIe ASPM/L0s fixup
(`drivers/pci/quirks.c:6296-6304`). Phoenix's bring-up already special-cases the bridge; no
action implied for the enum wall.

---

## Top-3 summary for the orchestrator

1. **Run the BSR=1-then-BSR=0 partition test (#2) FIRST.** One HW run decides *where* the
   controller wedges: BSR=1 reliable → wedge is at the wire SET_ADDRESS step → adopt the **BSR
   two-step** (not a delay). BSR=1 also ABSENT → wedge is at the input-context inbound read →
   implement #1. Add a BSR=1 AddressDevice before the existing BSR=0 at `xhci.c:3482` (and
   `:2429`).
2. **If BSR=1 is also intermittently ABSENT → implement #1 (dropped inbound-TLP wedge):**
   re-settle the inbound BCM2711 window (and add a `dsb sy`) immediately before AddressDevice;
   and/or pre-allocate the slots[0] input/dev ctx + ep0 ring during `xhci_init` *before* the
   first NoOp (so the buffers exist before the last pre-R/S inbound re-settle and no
   controller-read DMA buffer is mmap'd after the window settles).
3. **Add the TRSTRCY recovery delay (#3) regardless** (`usleep(10000-50000)` after PLS=U0 in
   `xhci_setPortFeature(RESET)`, `xhci.c:3304`): cheap and spec-mandated. But its mechanism-fit
   for the ABSENT signature is poor (it would yield an *error* completion, not absence), so do
   NOT expect it to fix the wall and do not rely on it alone.

Ruled out / matches Linux: register ordering (#4), scratchpad (#5), context size (#6). VL805
quirks (#7) are a real divergence but not on the AddressDevice path (watch `XHCI_TRB_OVERFETCH`
+ single-page command ring only as a cheap secondary hardening).
