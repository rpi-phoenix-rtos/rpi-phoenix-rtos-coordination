# Implementation plan — USB HID mouse (Pi 4 / BCM2711, VL805 xHCI)

Status: forward plan. Sequenced **after** the USB host stack reaches device
enumeration. As of 2026-05-26 the xHCI controller reaches RUNNING and the
**command ring completes commands** — the missing-scratchpad-buffer-array root
cause is fixed (`xhci_allocScratchpads`,
[`xhci.c:1018-1100`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)), and a
NO-OP now returns a Command Completion Event. The next host-stack milestones
are device enumeration (Enable Slot → Address Device → Get Descriptors → Set
Configuration) and then class drivers. **A mouse driver sits on top of that
host stack** and is the second HID target after the keyboard.

This plan deliberately mirrors the keyboard work
([`usb-xhci-impl.md`](usb-xhci-impl.md) §4) and the Tiny-X consumer plan
([`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) §4.3, Phase 5). It commits no code; a
new driver lands in `phoenix-rtos-devices/usb/usbmouse/` (sibling to
[`tty/usbkbd/`](../../sources/phoenix-rtos-devices/tty/usbkbd/)), with
descriptor-dump observability first in
[`phoenix-rtos-lwip/port/diag-udp.c`](../../sources/phoenix-rtos-lwip/port/diag-udp.c),
matching the WiFi/audio bring-up idiom.

Primary references (cited inline):
[USB Device Class Definition for HID 1.11](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)
(boot protocol §B, App. B.2 "Protocol 2 (Mouse)", class requests §7.2);
[USB HID Usage Tables 1.4](https://usb.org/sites/default/files/hut1_4.pdf)
(Generic Desktop page 0x01: X 0x30, Y 0x31, Wheel 0x38; Button page 0x09);
[USB 2.0 spec](https://www.usb.org/document-library/usb-20-specification)
(low/full-speed, interrupt endpoints, bInterval);
[xHCI 1.2 spec](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controller-interface-usb-xhci.pdf)
(Configure Endpoint §4.6.6, interrupt endpoint context §6.2.3, Transfer Events
§4.11, doorbell §4.7);
Linux `drivers/hid/usbhid/usbmouse.c` and `hid-generic` (behavioural reference
for boot-protocol mouse report layout and SET_PROTOCOL/SET_IDLE handling).

## 1. Goal and tier ladder

End goal: a Phoenix-RTOS Pi 4 image that, with a USB mouse plugged into a Pi 4
USB-A port, exposes relative pointer motion (dx, dy), button up/down state, and
a scroll wheel through an input device node that the Tiny-X
([`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md)) server and a future Quake-style game
loop can read with low latency. The mouse rides the **same HID /
interrupt-IN-endpoint plumbing the keyboard needs** — what is shared with the
keyboard and what is mouse-specific is called out explicitly throughout (§3,
§5).

The input node is a **PS/2-style 3-byte packet stream on `/dev/mouse`**,
because that is exactly what the in-tree Phoenix PS/2 mouse driver already
emits and what the Tiny-X PR's mouse backend consumes (§4) — so the mouse
delivers, byte-for-byte, the interface that consumer already expects, with
USB-HID as the transport instead of the i8042 controller.

Each tier is independently testable on the bench rig and rollback-able via a
`manifests/*.md` snapshot per the rollback discipline in
[`AGENTS.md`](../../AGENTS.md).

- **Tier 0 (MUST) — Scout: enumerate + dump descriptors.** With a mouse
  plugged in, the host stack enumerates it and a descriptor dump confirms
  `bInterfaceClass=0x03 (HID)`, `bInterfaceSubClass=0x01 (boot)`,
  `bInterfaceProtocol=0x02 (mouse)`, plus the interrupt-IN endpoint's
  `bEndpointAddress`, `wMaxPacketSize`, and `bInterval`. Exit: the existing
  `usb_dump*Descriptor` helpers
  ([`driver.c:139-205`](../../sources/phoenix-rtos-usb/libusb/driver.c)) or a
  diag-udp dump print the mouse interface + endpoint over UART/UDP. **Gated on
  the host stack reaching enumeration (§3).**
- **Tier 1 (MUST) — Raw boot reports.** Open the interrupt-IN pipe, issue
  SET_PROTOCOL(boot) + SET_IDLE(0), submit periodic interrupt transfers, and
  log each raw report (`buttons dx dy [wheel]`) over UART or the diag-udp :9999
  responder. Exit: moving the mouse / clicking prints changing `dx/dy/buttons`
  bytes. This is the first physical "the mouse talks" signal.
- **Tier 2 (MUST) — `/dev/mouse` server.** A `usbmouse` host-side driver
  (sibling to `usbkbd`) registers a port, `create_dev("/dev/mouse")`, and on
  each interrupt-IN completion translates the HID boot report into a 3-byte
  PS/2-format packet pushed to a FIFO that a blocking `mtRead` drains. Exit: a
  trivial `cat /dev/mouse | hexdump`-equivalent shows 3-byte packets changing
  as the mouse moves.
- **Tier 3 (MUST) — Tiny-X consumes it.** Tiny-X's mouse backend reads
  `/dev/mouse`, the X cursor tracks on HDMI, buttons click. Exit: the X cursor
  follows the mouse and a button press registers in `xev` (depends on
  [`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) Phases 1-4 landing first).
- **Tier 4 (STRETCH) — Report-protocol + descriptor parser.** Parse the HID
  report descriptor so non-boot-compliant mice, 5-button mice, high-resolution
  wheels, and tilt wheels work without relying on boot protocol. Exit: a mouse
  that reports nothing in boot mode (rare, but real for some gaming mice) works
  through report protocol; wheel + extra buttons decode correctly.
- **Tier 5 (STRETCH) — Richer event interface for game loops.** An optional
  binary event-record interface (`{ dx, dy, buttons, wheel, timestamp }`,
  §4.2) on a second node for consumers that want absolute timestamps and a
  larger button set than the 3-byte PS/2 packet carries. Exit: a Quake-style
  test reader gets timestamped relative deltas with sub-frame latency.

Tiers 0-1 are "the mouse talks"; Tier 2-3 are "Phoenix has a pointer device
Tiny-X can use"; Tiers 4-5 are production hardening for arbitrary mice and
game-grade latency.

## 2. USB HID mouse fundamentals

### 2.1 HID class and the boot/report protocol split

A USB mouse is a HID-class device: `bInterfaceClass = 0x03`
(`USB_CLASS_HID`, [`usb.h:62`](../../sources/phoenix-rtos-usb/libusb/include/usb.h)).
HID defines two ways the device can report:

- **Report protocol** (the default). The device describes its report layout in
  a **HID report descriptor** (descriptor type `0x22`,
  `USB_DESC_TYPE_HID_REPORT`, [`usb.h:78`](../../sources/phoenix-rtos-usb/libusb/include/usb.h)),
  a byte-coded grammar of Usage Pages, Usages, Logical Min/Max, Report Size,
  Report Count, Input/Output items
  ([HID 1.11 §6.2.2](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)).
  Parsing it requires a state machine.
- **Boot protocol.** A fixed, descriptor-free report layout the BIOS can use
  with no parser. HID 1.11 defines exactly two boot devices via
  `bInterfaceSubClass = 0x01` (Boot Interface Subclass) and
  `bInterfaceProtocol`: **`0x01` = keyboard**, **`0x02` = mouse**
  ([HID 1.11 §4.2, App. E.3-E.4](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)).

**Boot protocol is the pragmatic first target** for exactly the reason it was
chosen for the keyboard ([`usbkbd.c:51`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)
uses `usbkbd_bootSubclass=0x1` + boot protocol via `SET_PROTOCOL(0)`): it needs
**no HID report-descriptor parser**. The driver issues SET_PROTOCOL(boot) and
then reads a known fixed-shape report off the interrupt-IN endpoint. The vast
majority of physical mice implement boot protocol. Report-protocol parsing is
deferred to Tier 4.

### 2.2 Boot-protocol mouse report format

The boot mouse report is 3 (or 4) bytes
([HID 1.11 App. B.2 "Protocol 2 (Mouse)"](https://www.usb.org/sites/default/files/documents/hid1_11.pdf);
Linux `usbmouse.c` decodes exactly this):

| Byte | Field | Encoding |
|---|---|---|
| 0 | Buttons | bit0 = left, bit1 = right, bit2 = middle (bits 3-7 reserved/extra) |
| 1 | dX | signed 8-bit relative motion (`int8_t`), +right |
| 2 | dY | signed 8-bit relative motion (`int8_t`), +down |
| 3 | Wheel | signed 8-bit relative scroll (`int8_t`), present on most mice; **not** part of the strict boot report, but reported by nearly all real mice and accepted by hosts |

This maps to HID Usage Tables 1.4: Button page `0x09` (Button 1/2/3), Generic
Desktop page `0x01` Usage X `0x30`, Usage Y `0x31`, Wheel `0x38`
([HUT 1.4 §4, §8](https://usb.org/sites/default/files/hut1_4.pdf)). Because dX/dY
are signed and **relative**, the device sends a report only when something
changes (or per SET_IDLE policy); there is no "current position" — the host
accumulates deltas. This is precisely what Tiny-X and a game loop want.

The keyboard's boot report is a different fixed 8-byte shape
(`{ modifiers, reserved, key[6] }`,
[`usbkbd.c:53`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)
`usbkbd_reportSize=8`). **The report *parsing* is the only mouse-specific HID
logic** — everything around it (pipe open, SET_PROTOCOL, SET_IDLE, periodic
interrupt-IN submission) is identical to the keyboard.

### 2.3 The two control requests: SET_PROTOCOL and SET_IDLE

Both are class-specific control transfers on endpoint 0, recipient = interface
— the keyboard already issues both and the mouse copies the pattern verbatim
([`usbkbd.c:370-395`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)):

- **SET_PROTOCOL(boot)** — `bRequest = CLASS_REQ_SET_PROTOCOL (0x0B)`
  ([`usb.h:55`](../../sources/phoenix-rtos-usb/libusb/include/usb.h)),
  `bmRequestType = host→dev | class | interface`, `wValue = 0` (0 = Boot,
  1 = Report), `wIndex = interface`. Switches the device into the fixed-layout
  boot report ([HID 1.11 §7.2.6](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)).
- **SET_IDLE(0)** — `bRequest = CLASS_REQ_SET_IDLE (0x0A)`,
  `wValue = 0` (duration 0 = report only on change, not periodically).
  ([HID 1.11 §7.2.4](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)).
  For a mouse, idle=0 means no spurious "no movement" reports — exactly what we
  want for latency and bus economy. SET_IDLE is **optional**: the keyboard
  tolerates failure (`usbkbd_setIdle` return ignored,
  [`usbkbd.c:715-718`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c));
  the mouse should do the same.

Phoenix builds these `usb_setup_packet_t`s and submits them with
`usb_transferControl(drv, pipeCtrl, &setup, NULL, 0, usb_dir_out)`
([`driver.c:69`](../../sources/phoenix-rtos-usb/libusb/driver.c)) on the
control pipe opened in the insertion handler. **No new core API is needed.**

## 3. Transfer model and enumeration prerequisites (shared with keyboard)

### 3.1 Topology and speed

A USB mouse is a low-speed (1.5 Mbps) or full-speed (12 Mbps) HID device. On
the Pi 4 it sits **behind the VL805 xHCI controller's internal hub**: the Pi
4B's four user-facing USB-A ports are fronted by the VL805's integrated USB
2.0 + USB 3.0 hubs, so even a "directly plugged" mouse is logically one hop
below the root hub. This is the same topology the keyboard already traverses
(the K120 keyboard is seen by firmware as VID 046d PID c31c in the pre-handoff
DEV scan, [`docs/inprogress/status.md`](../inprogress/status.md) "Updated baseline"), so downstream
hub enumeration is a host-stack concern shared with the keyboard, tracked in
[`usb-xhci-impl.md`](usb-xhci-impl.md) §8 "Hub support" and Open Questions.
Phoenix's [`hub.c`](../../sources/phoenix-rtos-usb/usb/hub.c) already handles
USB 2.0 hubs as a class; the mouse depends on that path, not on new code.

A mouse uses **one Interrupt IN endpoint** polled at its `bInterval`
(typically 10 ms full-speed, encoded as frames; or `8` = 8 ms; high-end mice
report faster). The endpoint descriptor's `bInterval`
([`usb.h:193`](../../sources/phoenix-rtos-usb/libusb/include/usb.h)) drives the
xHCI endpoint context's interval field (§3.3).

### 3.2 Enumeration milestones the mouse depends on (host-stack, NOT this plan)

These are **host-stack milestones owned by [`usb-xhci-impl.md`](usb-xhci-impl.md)**,
not by the mouse driver. The mouse cannot begin until they pass — this plan is
honest that Tier 0 is gated on them:

| Milestone | xHCI mechanism | Code site | Status (2026-05-26) |
|---|---|---|---|
| Command ring completes | NO-OP returns Command Completion Event after scratchpad alloc | `xhci_allocScratchpads` [`xhci.c:1035`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c), `xhci_cmdNoopSelftest` | **DONE today** (per task context) |
| Enable Slot | ENABLE_SLOT TRB → slotId 1..N | `xhci_cmdEnableSlot` [`xhci.c:1197`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c) (per [`usb-xhci-impl.md`](usb-xhci-impl.md) §2.7) | NEXT |
| Address Device | ADDRESS_DEVICE TRB, input ctx from `xhci_prepareAddressContext` | [`usb-xhci-impl.md`](usb-xhci-impl.md) §4 step 3 | NEXT |
| Get Descriptors | GET_DESCRIPTOR(DEVICE/CONFIG/STRING) via ep0 control reads | `usb_getDescriptor` [`dev.c:89`](../../sources/phoenix-rtos-usb/usb/dev.c), `usb_devEnumerate` [`dev.c:624`](../../sources/phoenix-rtos-usb/usb/dev.c) | NEXT |
| Set Configuration | SET_CONFIGURATION standard request | `usb_setConfiguration` [`driver.c:98`](../../sources/phoenix-rtos-usb/libusb/driver.c) | NEXT |
| Interface→driver match | walk interfaces, match class/subclass/protocol filters | `usb_drvMatchIface` [`drv.c:255`](../../sources/phoenix-rtos-usb/usb/drv.c), `usb_drvcmp` [`drv.c:209`](../../sources/phoenix-rtos-usb/usb/drv.c) | exists |

When these pass for the **keyboard**, they pass for the **mouse** — the only
difference is the matched filter triple (§5.1). So the cheapest sequencing is:
land the keyboard end-to-end first (it is the active work stream), then the
mouse is a small delta on a proven path. The mouse plan does **not** re-derive
enumeration; it cross-references it.

### 3.3 What the xHCI driver must support for the interrupt endpoint (already in tree)

The mouse's periodic transfers reuse machinery the keyboard bring-up already
wired into [`xhci.c`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c). This
is the strongest argument that the mouse is mostly a driver-layer add:

- **Configure Endpoint command.** `xhci_initInterruptInPipe`
  ([`xhci.c:1892`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)) builds
  an input context with the add/drop-context flags for the interrupt-IN
  endpoint, sets `XHCI_EP_CTX_TYPE_INTERRUPT_IN`
  ([`xhci.c:1929`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)), fills
  the interval, CErr=3, max-packet, average-TRB-len / max-ESIT-payload fields,
  and issues `xhci_cmdConfigureEndpoint` ([`xhci.c:1677`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)).
  This is xHCI 1.2 §4.6.6 Configure Endpoint + §6.2.3 Endpoint Context.
- **An interrupt transfer ring.** Allocated per pipe
  (`XHCI_TRANSFER_RING_SIZE = 0x1000`,
  [`xhci.c:157`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)),
  page-aligned, with a LINK TRB closing the ring
  ([`xhci.c:1946-1952`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)).
- **Periodic transfer submission.** `xhci_submitInterruptIn`
  ([`xhci.c:1984`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)) drops a
  NORMAL TRB pointing at the report buffer with IOC+ISP set, updates the
  interrupter ERDP, and rings the slot/endpoint doorbell
  (`xhci_dbWrite32(xhci, slotId*4, endpointId)`,
  [`xhci.c:2034`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)).
- **Interval encoding.** `xhci_convertInterval`
  ([`xhci.c:1811`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)) maps the
  endpoint's `bInterval` into the xHCI endpoint-context interval (2^(interval)
  microframes per xHCI §6.2.3.6). Low/full-speed interval handling is the one
  spot worth re-verifying for a slow mouse vs. the keyboard.
- **Transfer Event consumption.** The completion is read off the event ring —
  the same command-ring/event-ring machinery being brought up. Today
  `xhci_submitInterruptIn` is paired with a polled/event-driven completion path
  feeding the generic `usb_completion_t` callback; the driver's
  `completion` handler (§5.3) is invoked with the report bytes and length.

**Open item shared with keyboard:** completion delivery is currently
polling-leaning ([`usb-xhci-impl.md`](usb-xhci-impl.md) §11 "Interrupt vs
polling"). A mouse at 8-10 ms polling is light, but for game-grade latency
(Tier 5) the GIC interrupter should drive the event ring. Track under the same
follow-on as the keyboard, not as mouse-specific work.

## 4. The input event interface

### 4.1 Decision: emit PS/2 3-byte packets on `/dev/mouse`

Phoenix **already has** a mouse input abstraction, and Tiny-X **already**
consumes it. The in-tree PS/2 mouse driver
([`tty/pc-tty/ttypc_mouse.c`](../../sources/phoenix-rtos-devices/tty/pc-tty/ttypc_mouse.c),
ia32 only) creates **`/dev/mouse`** and emits **raw 3-byte PS/2 packets** via a
circular event queue, read in multiples of 3 with first-byte resync on the
`bit3` sync flag (`event_queue_get_mouse`,
[`event_queue.c:135-183`](../../sources/phoenix-rtos-devices/tty/pc-tty/event_queue.c)).
The Tiny-X PR (`devices#512`, noted in
[`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) §"Input — mouse" and §2) brings a PS/2
mouse for the PC target whose backend reads exactly that `/dev/mouse` 3-byte
stream.

**Therefore the cleanest mouse interface is to make the USB mouse look like a
PS/2 mouse on `/dev/mouse`:** translate the HID boot report into the PS/2
"standard 3-byte packet" and push it into the same kind of event queue. Tiny-X
gets the device it already knows, with no X-side code change beyond pointing
its mouse backend at `/dev/mouse`.

The PS/2 standard packet (which the existing `/dev/mouse` reader already
expects) is:

| Byte | Bits | Meaning |
|---|---|---|
| 0 | bit0 LeftBtn, bit1 RightBtn, bit2 MiddleBtn, **bit3 = 1 (sync, always set)**, bit4 XSign, bit5 YSign, bit6 XOverflow, bit7 YOverflow | flags |
| 1 | dX (9-bit two's complement; low 8 bits here, sign in byte0 bit4) | X movement |
| 2 | dY (9-bit two's complement; low 8 bits here, sign in byte0 bit5) | Y movement |

The HID→PS/2 translation is nearly trivial because both are relative:

```
ps2[0] = 0x08                                  /* sync bit always set */
       | (hid_buttons & 0x07)                  /* L/R/M map 1:1 */
       | (dx < 0 ? 0x10 : 0)                    /* X sign */
       | (dy_ps2 < 0 ? 0x20 : 0);               /* Y sign */
ps2[1] = (uint8_t)dx;                           /* HID +X = right = PS/2 +X */
ps2[2] = (uint8_t)dy_ps2;                       /* NOTE sign flip, below */
```

Coordinate-system caveat to put in a code comment: **HID dY is +down, PS/2 dY
is +up** ([HID 1.11 App. B.2](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)
vs. PS/2 mouse convention). The existing `/dev/mouse` consumers (and the X
backend) expect PS/2 semantics, so negate Y on translation:
`dy_ps2 = -hid_dy`. Overflow bits stay 0 (clamp dx/dy to int8 range; a single
HID report can't exceed it). The scroll wheel (HID byte 3) has **no field in
the 3-byte PS/2 packet** — see §4.3.

### 4.2 Optional richer event record (Tier 5, for game loops)

The 3-byte PS/2 packet is lossy for game-grade input: no wheel, no timestamp,
only 3 buttons. For a Quake-style reader or a future evdev shim, expose a
**second optional node** (e.g. `/dev/mouse-ev` or via `mtDevCtl` mode select)
emitting a compact fixed record:

```c
/* Tier 5: binary mouse event record, relative deltas + timestamp. */
typedef struct {
    int16_t  dx;          /* accumulated since last read, +right     */
    int16_t  dy;          /* accumulated since last read, +down (HID) */
    int8_t   wheel;       /* relative scroll                          */
    uint8_t  buttons;     /* bit0 L, bit1 R, bit2 M, bit3..7 extra    */
    uint64_t timestamp;   /* gettime() ns at report receipt           */
} usbmouse_event_t;
```

A blocking `mtRead` returns whole records; `mtGetAttr(atPollStatus)` returns
`POLLIN` when records are queued (the `usbkbd` poll pattern,
[`usbkbd.c:566-579`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)).
A game loop reads non-blocking each frame and sums deltas; X reads the PS/2
stream. Both views are fed from the **same** interrupt-IN completion handler.

### 4.3 What Tiny-X actually wants — and the wheel question

[`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) §4.3 already specifies the mouse path:
a `usbmouse` driver mirroring `usbkbd` (srv.c + usbmouse.c + Makefile),
exposing "a raw report endpoint that the same evdev shim translates to
`EV_REL`/`EV_KEY`", and notes the boot-mouse report is 3-4 bytes
(`{ buttons, dx, dy[, wheel] }`). Two interface shapes therefore satisfy it:

1. **PS/2 `/dev/mouse`** (recommended Tier 2-3) — Tiny-X's kdrive `ps2` mouse
   backend reads it directly. Wheel is lost (PS/2 standard packet has no wheel
   byte); acceptable for the first cursor-tracking demo. The IntelliMouse
   PS/2 extension (4-byte packet with Z) is a later option if the kdrive
   backend supports it.
2. **evdev `/dev/input/event1`** (Tier 4+/5, the path
   [`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) §4.2 prefers for the keyboard) —
   `EV_REL/REL_X/REL_Y/REL_WHEEL` + `EV_KEY/BTN_LEFT…`, which **does** carry
   the wheel and arbitrary buttons, and lets xkb/X handle acceleration. If the
   keyboard lands an evdev shim, the mouse should feed the same shim
   (`/dev/input/event1`) so the wheel and extra buttons survive.

**Relative vs absolute:** a mouse is intrinsically relative; X's pointer
acceleration and the game loop both want relative deltas. There is no absolute
mode for a mouse (that is a tablet/touchscreen concern). Confirm at Tier 3 that
the Tiny-X kdrive backend is configured for a relative pointer (it is, for both
`ps2` and `evdev` mouse backends).

**Recommendation:** Tier 2-3 ship PS/2 `/dev/mouse` (smallest path to a moving
cursor, reuses the exact in-tree interface). If/when the keyboard evdev shim
lands, add an evdev view (Tier 4+) so the wheel and >3 buttons reach X.

## 5. Phoenix conventions audit and what is shared with the keyboard

### 5.1 The canonical host-side HID driver idiom (from `usbkbd`)

The mouse driver is a **near-clone of `usbkbd`**
([`usbkbd.c`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)), which is
the canonical Phoenix host-side HID driver: a multiserver-model process that
registers itself with the `usb` daemon via a C-constructor
(`usb_driverRegister`, [`usbkbd.c:836-839`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)),
declares class/subclass/protocol filters, and gets insertion/deletion/completion
callbacks. Shared vs. mouse-specific:

| Concern | Keyboard (`usbkbd`) | Mouse (`usbmouse`) | Shared? |
|---|---|---|---|
| Driver registration | `usb_driver_t usbkbd_driver` + `__attribute__((constructor))` ([`usbkbd.c:819-839`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | identical, `name="usbmouse"` | **shared idiom** |
| Match filter | `{ANY, ANY, USB_CLASS_HID, 0x01, 0x01}` ([`usbkbd.c:104-106`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | `{ANY, ANY, USB_CLASS_HID, 0x01, 0x02}` | **mouse-specific** (protocol 0x02) |
| Control pipe open + SET_CONFIG | `usb_open(...,control,0)` + `usb_setConfiguration(...,1)` ([`usbkbd.c:691-701`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | identical | **shared** |
| SET_PROTOCOL(boot) | `usbkbd_setProtocol` wValue=0 ([`usbkbd.c:370-381`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | identical | **shared** |
| SET_IDLE(0) | `usbkbd_setIdle`, failure tolerated ([`usbkbd.c:384-395`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | identical | **shared** |
| Interrupt-IN pipe open | `usb_open(...,interrupt,in)` ([`usbkbd.c:703`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | identical | **shared** |
| URB alloc + async submit | `usb_urbAlloc` + `usb_transferAsync` ([`usbkbd.c:404,421`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | identical (report size 4 not 8) | **shared API, mouse report size** |
| Report parsing | `usbkbd_handleReport` → usage→ASCII ([`usbkbd.c:343-367`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | `usbmouse_handleReport` → PS/2 packet (§4.1) | **mouse-specific** |
| Device node | `/dev/kbd%d` ([`usbkbd.c:723`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | `/dev/mouse` (PS/2-compatible name) | **mouse-specific name** |
| FIFO + blocking read | `fifo_t` + cond ([`usbkbd.c:186-200,480-509`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | event-queue of 3-byte packets (reuse `pc-tty/event_queue.[ch]`) | **shared pattern, diff payload** |
| msg loop (open/read/close/poll) | `usbkbd_msgthr` ([`usbkbd.c:512-591`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | identical shape | **shared** |
| insertion / deletion / completion handlers | `usbkbd_handle*` ([`usbkbd.c:594-784`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) | identical shape | **shared** |

The single net-new logic in the mouse driver is `usbmouse_handleReport`
(boot-report → PS/2 packet, ~30 lines) plus the wheel/extra-button accumulation
for the optional event record (§4.2). Everything else is a structural copy of
`usbkbd` with renamed symbols and a different filter triple. **Reusing
`pc-tty/event_queue.[ch]`** (the 3-byte-packet circular buffer with sync-byte
resync, [`event_queue.c`](../../sources/phoenix-rtos-devices/tty/pc-tty/event_queue.c))
gives the `/dev/mouse` read semantics for free; promote it to a shared location
(`phoenix-rtos-devices/tty/common/` or similar) rather than duplicating.

### 5.2 No generic Phoenix input/evdev abstraction exists

A sweep of `sources/` found **no `/dev/input`, no evdev, no `input_event`,
no `EV_REL`/`EV_KEY`** anywhere in the tree (grep returned nothing). The only
input abstractions are:
- ASCII char stream on `/dev/kbd0` (USB keyboard,
  [`usbkbd.c`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)).
- 3-byte PS/2 packet stream on `/dev/mouse` (PC PS/2 mouse,
  [`ttypc_mouse.c`](../../sources/phoenix-rtos-devices/tty/pc-tty/ttypc_mouse.c)).

So `/dev/mouse` (PS/2 packets) **is** the canonical Phoenix pointer interface
today. This plan adopts it rather than inventing a new one. The evdev shim is a
separate, larger piece of work shared with the keyboard
([`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) §4.2 Design B) and is the right home
for a future `/dev/input/event*` story; the mouse should feed it when it lands,
not block on it.

### 5.3 The completion callback (the periodic-report heartbeat)

`usbkbd_handleCompletion` ([`usbkbd.c:594-620`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c))
is the model: the `usb` daemon calls it with the report `data`+`len` each time
the interrupt-IN transfer completes; it parses the report, then **re-arms the
transfer** (`usb_transferAsync(drv, pipeIntIn, c->urbid, reportSize, NULL)`,
[`usbkbd.c:615`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)) to
keep polling. The mouse handler is identical except `usbmouse_handleReport`
emits a PS/2 packet (and optionally an event record) instead of ASCII. On
`c->err` it marks the device disconnected and signals readers
([`usbkbd.c:603-610`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)).

## 6. File-level breakdown

```
sources/phoenix-rtos-devices/usb/usbmouse/        # NEW, sibling to tty/usbkbd/
    Makefile                    # builds libusbdrv-usbmouse (mirror tty/usbkbd/Makefile)
    srv.c                       # tiny main() wrapper (mirror tty/usbkbd/srv.c, 595 B)
    usbmouse.c                  # the driver: registration, insertion/deletion/
                                #   completion handlers, msg loop, /dev/mouse,
                                #   usbmouse_handleReport (HID boot -> PS/2 packet)

sources/phoenix-rtos-devices/tty/common/          # NEW shared (or keep in pc-tty)
    event_queue.c/.h            # PROMOTED from tty/pc-tty/ (3-byte packet ring,
                                #   event_queue_get_mouse sync-byte resync)

sources/phoenix-rtos-build/
    build-core-aarch64a72-generic.sh  # add libusbdrv-usbmouse to
                                      #   USB_HOSTDRV_LIBS (currently only
                                      #   "libusbdrv-usbkbd", line 37)

sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/
    (build.project / user.plo.yaml)   # ensure usbmouse driver linked into the
                                      #   usb daemon image alongside usbkbd
```

Notes:
- The keyboard is built into the aarch64 USB daemon via
  `USB_HOSTDRV_LIBS="libusbdrv-usbkbd"`
  ([`build-core-aarch64a72-generic.sh:37`](../../sources/phoenix-rtos-build/build-core-aarch64a72-generic.sh)).
  The mouse adds `libusbdrv-usbmouse` to the same list — both class drivers are
  statically linked host-side drivers in the one `usb` daemon process (the
  `usb_drvType_intrn` model, [`usbdriver.h:33`](../../sources/phoenix-rtos-usb/libusb/include/usbdriver.h)).
- Tier 0-1 descriptor-dump / raw-report scout work can begin as a new
  single-character sub-command in
  [`diag-udp.c`](../../sources/phoenix-rtos-lwip/port/diag-udp.c) (the `'x'`
  xHCI dump / `'X'` bring-up / `'R'` reset commands already live there,
  [`diag-udp.c:5805-5812`](../../sources/phoenix-rtos-lwip/port/diag-udp.c)),
  then migrate into `usbmouse.c` once enumeration is proven — the same
  "scout-in-diag-udp then promote" arc the WiFi/audio plans use.
- The `libusb/hid_client.c` / `libusb/include/hid_client.h` in the tree are the
  **device-side (gadget) HID** implementation (descriptor tables, `hid_send`),
  **not** host-side, and are not reused here — noted to avoid confusion.

## 7. Phased delivery

| Phase | Scope | Success criterion | UART / UDP signature | Est. |
|---|---|---|---|---|
| **P0** | Scout: confirm a plugged mouse enumerates; dump device/config/interface/endpoint descriptors. New diag-udp sub-command or `usb_dump*` over UART. | Mouse interface reported `class=03 sub=01 proto=02`; int-IN endpoint addr/size/interval printed | diag `'m'` → `iface 03/01/02 ep 81 mps=4 bInterval=10` | 0.5 wk (after enumeration lands) |
| **P1** | Open ctrl pipe, SET_CONFIG, SET_PROTOCOL(boot), SET_IDLE(0), open int-IN pipe, submit periodic transfers; log raw reports. | Moving/clicking prints changing `buttons dx dy [wheel]` bytes | `usbmouse: rpt 01 ff 02 00` lines change with motion | 0.5-1 wk |
| **P2** | `usbmouse` driver: register, `/dev/mouse`, completion → PS/2 3-byte packet → event queue → blocking `mtRead`. | `cat /dev/mouse \| hexdump` shows 3-byte packets tracking motion | `usbmouse: New device: /dev/mouse` | 1 wk |
| **P3** | Tiny-X consumes `/dev/mouse`; cursor tracks on HDMI; buttons click. | X cursor follows mouse; `xev` shows button + motion events | HDMI cursor moves; `xev` ButtonPress | 0.5 wk (after Tiny-X P1-4) |
| **P4** | Report-protocol + HID report-descriptor parser for non-boot / wheel / 5-button mice. Optionally feed evdev shim with wheel. | A non-boot mouse and a wheel decode correctly | `usbmouse: report-proto, wheel=+1` | 2 wk |
| **P5** | Optional `usbmouse_event_t` record interface + IRQ-driven event ring for game-grade latency. | Game-style reader gets timestamped deltas; <2 ms report-to-read | latency probe under target | 1-2 wk |

Total to Tier 3 (cursor tracking in Tiny-X): **~3-4 weeks** focused, *after the
host stack reaches enumeration and the keyboard validates the HID path* — most
of the mouse is a structural copy of `usbkbd`. Each phase ends with a
`manifests/*.md` snapshot via
[`scripts/snapshot-integration-state.sh`](../../scripts/snapshot-integration-state.sh).

## 8. Test strategy

The project idiom is real-Pi bench cycles
([`scripts/rebuild-rpi4b-fast.sh`](../../scripts/rebuild-rpi4b-fast.sh) →
[`scripts/capture-rpi4b-uart.sh`](../../scripts/capture-rpi4b-uart.sh) →
`scripts/summarize-rpi4b-uart-log.py`) plus the diag-udp :9999 responder for
on-demand state, and HDMI snapshots for the cursor demo.

- **P0 (enumeration/descriptors).** Plug a known-good USB-2 mouse into one of
  the Pi 4's USB-2 (black) ports — avoid the blue USB-3 ports until super-speed
  is supported, same caveat as the keyboard
  ([`usb-xhci-impl.md`](usb-xhci-impl.md) §7). Pass: descriptor dump shows the
  HID-boot-mouse triple `03/01/02`. Use `--capture-secs 180+` so user-space is
  up before plugging (per [`AGENTS.md`](../../AGENTS.md) test-cycle notes).
- **P1 (raw reports).** Move the mouse in a known pattern (e.g. slow right,
  then click-left); confirm `dx` goes positive, `buttons` bit0 toggles. This is
  the first physical signal — recorded in the manifest note.
- **P2 (`/dev/mouse`).** A short Phoenix shell reader (`hexdump`-equivalent or a
  10-line test app) reads `/dev/mouse` and prints packets; confirm 3-byte
  alignment via the sync bit (`byte0 & 0x08`) and that dx/dy track motion, Y
  negated vs. HID (§4.1).
- **P3 (Tiny-X).** HDMI frame-grab: cursor visible and follows the mouse; a
  click highlights a `twm`/`xterm` titlebar or registers in `xev`. Diff against
  a golden "cursor at corner" PNG.
- **P4-P5.** A non-boot / wheel mouse decodes; a latency probe (timestamp at
  report receipt vs. read) stays under the game-loop target.
- **Future automation.** Same options as the keyboard
  ([`usb-xhci-impl.md`](usb-xhci-impl.md) §7): a host-controlled USB-HID gadget
  (another Pi/board emulating a mouse) replaying canned boot-mouse reports into
  the Pi 4's port for in-loop, hands-free input testing.

Regression: each phase snapshots via
[`scripts/snapshot-integration-state.sh`](../../scripts/snapshot-integration-state.sh),
restorable with `scripts/restore-integration-state.sh <manifest>`. The UART
summariser ([`scripts/uart-summary.sh`](../../scripts/uart-summary.sh)) should
learn to recognise a `usbmouse: New device: /dev/mouse` stage line.

## 9. Inter-dependencies

Depends on (in order):

1. **USB host stack reaching enumeration + interrupt transfers** — the hard
   gate. Owned by [`usb-xhci-impl.md`](usb-xhci-impl.md): command ring (done),
   then Enable Slot / Address Device / Get Descriptors / Set Configuration
   (§3.2), then a validated interrupt-IN periodic transfer. **The mouse cannot
   start Tier 0 until a HID device enumerates.**
2. **Keyboard HID path validated first.** The keyboard exercises the identical
   SET_PROTOCOL/SET_IDLE/interrupt-IN/completion machinery (§5). Landing the
   keyboard end-to-end de-risks the mouse to a ~3-week driver delta. Sequence:
   keyboard → mouse.
3. **BCM2711 PCIe bridge reliability.** The mouse inherits the host stack's
   current statistical bring-up risk (cap-probe poisoning / bridge degradation
   across resets, [`docs/inprogress/status.md`](../inprogress/status.md) "USB status: statistical").
   The mouse adds no PCIe complexity but is blocked whenever USB is.
4. **VL805 internal-hub topology.** The mouse is one hop below the root hub
   through the VL805's hubs; depends on
   [`hub.c`](../../sources/phoenix-rtos-usb/usb/hub.c) downstream-hub
   enumeration (shared with keyboard, [`usb-xhci-impl.md`](usb-xhci-impl.md) §8).
5. **Tiny-X (Tier 3 only).** Cursor demo depends on
   [`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) Phases 1-4 (port skeleton, Xfbdev,
   `/dev/fb0`, keyboard) landing first; mouse is its Phase 5.
6. **Stage-1 caches + DMA coherency.** With caches off the report buffers and
   transfer rings are non-cacheable and coherent automatically. When caches
   land, every `va2pa` DMA buffer (transfer ring, report buffer) must be
   Normal-NC or explicitly cleaned/invalidated — the same TD the keyboard track
   carries ([`usb-xhci-impl.md`](usb-xhci-impl.md) §11 "DMA coherency"). Track a
   new `TD-xx` only if the mouse adds a buffer the keyboard didn't.

Independent of: GENET, WiFi, audio, SMP (the driver is event-driven and light).

## 10. Open questions and risks

- **Wheel through PS/2 `/dev/mouse`.** The standard 3-byte PS/2 packet has no
  wheel byte. Tier 2-3 lose the wheel; the IntelliMouse 4-byte PS/2 extension
  or the evdev path (§4.3) recovers it. Decide at P3 whether the Tiny-X kdrive
  `ps2` backend negotiates IntelliMouse, else defer wheel to the evdev shim.
- **Y-axis sign.** HID is +down, PS/2 is +up (§4.1). A sign error here makes the
  cursor move the wrong way vertically — easy to get wrong, easy to fix; verify
  at P3 against the on-screen cursor.
- **bInterval / polling rate for low-speed mice.** `xhci_convertInterval`
  ([`xhci.c:1811`](../../sources/phoenix-rtos-devices/usb/xhci/xhci.c)) must map
  a low/full-speed mouse's frame-based `bInterval` correctly into the xHCI
  microframe interval (xHCI §6.2.3.6); a wrong interval gives sluggish or
  bus-flooding polling. Verify with a slow (10 ms) and a fast mouse.
- **Boot vs. report protocol quirks.** A few mice ignore SET_PROTOCOL or send a
  longer report than the boot 3-4 bytes; the handler must tolerate
  `len >= 3` and not assume exactly 3 (read what the endpoint delivers, decode
  the first 3-4 bytes). Some gaming mice only work in report protocol → Tier 4.
- **Absolute vs. relative for Tiny-X.** Confirmed relative (§4.3); no absolute
  mode for a mouse. Re-confirm the Tiny-X pointer backend is relative at P3.
- **Multiple pointer devices / `/dev/mouse` naming.** The PC driver uses a
  fixed `/dev/mouse` (single device,
  [`ttypc_mouse.c:236`](../../sources/phoenix-rtos-devices/tty/pc-tty/ttypc_mouse.c));
  the USB keyboard uses indexed `/dev/kbd%d`
  ([`usbkbd.c:723`](../../sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c)).
  Decide whether to keep the single canonical `/dev/mouse` (simplest for
  Tiny-X) or index (`/dev/mouse%d`) for hot-plugging a second mouse. Recommend
  single `/dev/mouse` for the first mouse + indexed fallback.
- **Power / low-speed device handling.** Bus-powered low-speed mice draw
  trivially; PORTSC.PP / port-power-good handling is the host stack's job
  ([`usb-xhci-impl.md`](usb-xhci-impl.md) §3 "Port-power assertion") and shared
  with the keyboard — no mouse-specific power work expected.
- **Latency for the game loop (Tier 5).** Polled completion (current xHCI
  posture) caps latency at the polling cadence; game-grade input wants the GIC
  interrupter wired to the event ring (shared follow-on,
  [`usb-xhci-impl.md`](usb-xhci-impl.md) §11). Quantify at P5.

---

This plan supersedes nothing currently in tree. It is referenced from
[`docs/inprogress/status.md`](../inprogress/status.md) once the host stack reaches enumeration and the
mouse becomes the active step in
[`tracking/current-step.md`](../../tracking/current-step.md). It is the concrete
realization of [`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) §4.3 / Phase 5 and the
HID follow-on to [`usb-xhci-impl.md`](usb-xhci-impl.md).
