# USB Stage 3 — multi-slot + route-string + TT (keyboard behind the hub)

Date: 2026-05-30. Tracks task #116. Prereqs DONE: ep0 drift fix (148bf48),
hub interrupt-EP fixes (060dc31). **Verified state:** the external VIA hub fully
configures, its status-change interrupt transfer completes, and a port-4 connect
fires `hub_devConnected` for the **low-speed keyboard** (`parentHubAddr=2 port=4
speed=low`, Logitech 046d:c31c). The keyboard's `getDevDesc` then fails because it
is attempted on **slot 1's ep0** (the hub's slot). It needs its own slot + TT.

All code in `sources/phoenix-rtos-devices/usb/xhci/xhci.c` unless noted.

## Hard constraints (xHCI, confirmed)
- One Device Slot per device — no shortcut. The keyboard needs its own slot.
- A low/full-speed device behind a high-speed hub REQUIRES TT (split transactions).
  Both the keyboard slot ctx AND the hub slot ctx carry TT-related fields.

## Sequencing — split refactor from feature; HW-gate each. DO NOT big-bang.
The tightest constraint is **don't regress 060dc31** (hub config + keyboard detect).

### Step 1 — pure slot-table refactor, ZERO behaviour change (still only slot 1)
Convert the single-slot fields to a slot table keyed by slotId; functions take a
target slot instead of reading `xhci->slotId`.
- Struct: replace `devCtx/devCtxPhys/ep0Ring/ep0RingPhys/ep0Enqueue/ep0CycleState/
  ep0RingCount/slotId/slotAddressed` with `xhci_slot_t slots[K]` (K≈8) holding
  `{ slotId, addressed, maxPacket, fwAddress, devCtx, devCtxPhys, ep0Ring,
  ep0RingPhys, ep0Enqueue, ep0CycleState, ep0RingCount }`. Keep `inputCtx` SHARED
  (commands are serialised). Keep a "primary"/rig slot index for the handoff.
- Thread a target slot through: `xhci_allocSlotSpace`, `xhci_initEp0Ring`,
  `xhci_prepareAddressContext`, `xhci_cmdAddressDevice`, `xhci_cmdConfigureEndpoint`,
  `xhci_ep0Reserve/Push`, `xhci_ep0ControlRead/WriteNoData`, the doorbell writes,
  and **`xhci_eventMatch`/`xhci_eventAwait`** (slot key per waiter — already keyed on
  (slot,ep), just generalise from `xhci->slotId` to the caller's slot).
- Rig handoff + `xhci_init` populate slots[primary] with slotId=1.
- **Gate:** build + HW-test → identical behaviour (hub_conf returns 0, interrupt
  fires, keyboard detected). Commit only after the capture confirms. This isolates
  the structural risk against known-good before any new function exists.

### Step 2 — second slot + route string + TT (the feature)
1. **Smallest signal first:** confirm a SECOND `xhci_cmdEnableSlot` returns a fresh
   slotId (≠1). The rig set slot 1 up specially; a 2nd EnableSlot is unproven. If it
   fails, everything downstream is moot — fix that first.
2. New device detection in `xhci_transferEnqueue`: a dev with `address==0` whose
   parent hub is NOT the root hub (`dev->hub->hub != NULL`) → allocate a new slot:
   EnableSlot → slotId; `xhci_allocSlotSpace(slot)`; `xhci_initEp0Ring(slot)`;
   build the keyboard input ctx; `cmdAddressDevice(slot)`; record fwAddress on the
   subsequent SET_ADDRESS (acknowledge, don't re-address — same as the slot-1 path).
3. **Keyboard slot ctx** (`xhci_prepareAddressContext`, generalised — remove the
   `dev->hub->hub != NULL` -ENOSYS reject):
   - route string = the downstream-port path: tier-1 nibble = keyboard port (4);
     derive from the dev->hub chain / `dev->locationID`. Low 20 bits of word0.
   - speed = low (psi already handled by `xhci_usbSpeedToPsi`).
   - root hub port = the HUB's root port (1), NOT dev->port. word1 bits 16-23.
   - TT Hub Slot ID = the hub's slotId (1); TT Port = keyboard port (4);
     MTT per the hub's wHubCharacteristics (VIA = single-TT → MTT=0). word2.
   - ep0 maxPacket = 8 (low-speed, fixed — NO descriptor-read-then-Evaluate fixup;
     that TODO is full-speed only).
4. **PITFALL — the hub's OWN slot ctx must declare it a hub.** For the controller to
   route split transactions to the LS keyboard, slot 1 (the hub) needs **Hub=1,
   Number of Ports, TT Think Time** set. `xhci_prepareAddressContext` sets NONE of
   these today (only speed + context-entries); the hub "worked" because they only
   matter once something downstream needs TT. So Step 2 likely needs an **Evaluate
   Context (or Configure Endpoint) on slot 1** after the hub descriptor is read
   (nports known). If the keyboard's transfers fail with a transaction error despite
   a perfect keyboard ctx, this is almost certainly why.
5. Route `transferEnqueue`/ep0/dispatcher by dev→slot (map via dev->address→slot, or
   stash slotId in dev->ctrlPipe->hcdpriv at EnableSlot time).

## Missing bit-layout #defines to ADD (verify against xHCI spec §6.2.2 slot ctx)
Struct fields (xhci.c ~310-313): `routeString_speed_mtt_hub_entries` (word0),
`maxExitLatency_rootHubPort_ports` (word1), `ttHubSlot_ttPort_ttt_intrTarget` (word2).
Present: SPEED__SHIFT=20, CONTEXT_ENTRIES__SHIFT=27, ROOT_HUB_PORT__SHIFT=16.
Add (confirm exact positions):
- word0: ROUTE_STRING = bits 0-19 (mask, no shift); MTT = bit 25; HUB = bit 26.
- word1: NUMBER_OF_PORTS__SHIFT = 24 (bits 24-31).
- word2: TT_HUB_SLOT_ID = bits 0-7; TT_PORT_NUMBER__SHIFT = 8 (bits 8-15);
  TT_THINK_TIME__SHIFT = 16 (bits 16-17).

## Concurrency
The roothub status thread polls slot-1's interrupt EP while the enumeration thread
drives slot-2 ep0. The dispatcher already keys on (slot, ep) — this is exactly the
foresight paying off. VERIFY the hub's interrupt transfer still completes WHILE the
keyboard enumerates (both threads live). The shared `inputCtx` is used serially for
commands; if Step 2 ever overlaps two slots' commands, that needs a lock — but
enumeration is sequential, so single shared inputCtx is fine for now.

## Done-when
Keyboard `New device: 046d:c31c` enumerates, `usbkbd` binds, key events flow.
VL805 has 5 root ports / 32 slots (`xHC0 ports 5 slots 32`).
