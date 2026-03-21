# Circle Reference Review

This document records implementation-relevant findings from the Circle project
for Raspberry Pi 4 bring-up work in Phoenix RTOS.

Primary local reference clone:

- `/Users/witoldbolt/phoenix-rpi/external/circle`

Important upstream repository:

- <https://github.com/rsta2/circle>

## 1. How To Use Circle Safely

Circle is a strong behavioral and architectural reference for Raspberry Pi bare
metal work, but it is GPL code and written in a different style and language
family than Phoenix.

Implication for Phoenix:

- use Circle to understand hardware sequences, firmware contracts, register
  ordering, and subsystem decomposition
- do not copy Circle code literally into Phoenix
- prefer the same underlying official sources Circle itself cites when possible
  such as Linux driver behavior, Raspberry Pi property tags, and xHCI or PCIe
  specifications

This is especially important for upstreamability and licensing hygiene.

## 2. What Circle Confirms About Early Pi 4 Video

The most useful Circle video path for the current Phoenix stage is the mailbox
+ property-tag framebuffer setup.

Key source files:

- `external/circle/lib/bcmmailbox.cpp`
- `external/circle/include/circle/bcmmailbox.h`
- `external/circle/lib/bcmpropertytags.cpp`
- `external/circle/include/circle/bcmpropertytags.h`
- `external/circle/lib/bcmframebuffer.cpp`
- `external/circle/include/circle/bcmframebuffer.h`
- `external/circle/lib/screen.cpp`
- `external/circle/include/circle/screen.h`
- `external/circle/sample/02-screenpixel/kernel.cpp`
- `external/circle/sample/03-screentext/kernel.cpp`

### 2.1 Mailbox/property request model

Circle uses the standard Raspberry Pi property mailbox flow:

- build a property buffer in coherent low memory
- append `PROPTAG_END`
- issue the request over the property mailbox channel
- pass the bus address of the buffer, not the CPU virtual address
- use explicit memory barriers around submission and completion

Concrete details from Circle:

- `CBcmPropertyTags::GetTags()` obtains a coherent page from
  `CMemorySystem::GetCoherentPage(COHERENT_SLOT_PROP_MAILBOX)`
- the request address is translated through `BUS_ADDRESS(...)`
- `DataSyncBarrier()` runs before mailbox submission
- `DataMemBarrier()` runs after the response returns
- `CBcmMailBox::WriteRead()` serializes mailbox access with a spinlock outside
  early-use mode

This strongly supports the current Phoenix Pi 4 choice to keep the mailbox
request buffer in low physical memory. That is already consistent with the
earlier Phoenix gdbstub finding that a high-linked `plo` request buffer failed
while a low physical request buffer succeeded.

### 2.2 Framebuffer allocation sequence

Circle allocates a framebuffer using a compact fixed tag block:

1. set physical width and height
2. set virtual width and height
3. set depth
4. set virtual offset
5. allocate buffer
6. get pitch

This exact shape appears in:

- `external/circle/lib/bcmframebuffer.cpp`
  `CBcmFrameBuffer::s_InitTags`

This confirms the current Phoenix `plo` HDMI bring-up should stay on the same
minimal contract:

- do not start with full HDMI controller programming
- rely on the firmware property interface for the earliest visible output
- defer richer runtime display support until later

### 2.3 Returned framebuffer address handling

Circle masks the framebuffer base returned by firmware with:

- `0x3FFFFFFF`

in:

- `external/circle/lib/bcmframebuffer.cpp`

This is the usual VideoCore-to-ARM alias cleanup step. It is a useful check for
future Phoenix runtime framebuffer work if any returned address looks like a bus
alias rather than a directly usable ARM physical address.

### 2.4 Display selection on Pi 4

Circle explicitly handles multiple displays on Raspberry Pi 4:

- `PROPTAG_GET_NUM_DISPLAYS`
- `PROPTAG_SET_DISPLAY_NUM`
- `PROPTAG_GET_DISPLAY_DIMENSIONS`

Key code:

- `CBcmFrameBuffer::GetNumDisplays()`
- `CBcmFrameBuffer::SetDisplay()`
- constructor logic in `CBcmFrameBuffer`

Important behavior:

- it caches the number of displays
- it selects the active display before later property calls
- if width or height is not specified, it queries display dimensions
- if the reported size is outside a sane range, it falls back to `640x480`

Implication for Phoenix:

- the current single-display HDMI marker path is fine for the first manual
  trial
- the next HDMI refinement should not hardcode a complex EDID or mode parser
- instead, preserve the current firmware-driven display path and add a small
  display-number or fallback policy only if the first hardware trial shows it
  is necessary

### 2.5 Text console layering

Circle’s screen stack is layered:

- `CBcmFrameBuffer` provides raw pixels
- `CTerminalDevice` renders text and cursor behavior
- `CScreenDevice` wraps both and registers a `ttyN` style device name

Key code:

- `external/circle/lib/screen.cpp`
- `external/circle/include/circle/screen.h`

This is valuable conceptually for Phoenix:

- keep early `plo` HDMI as the smallest raw-visibility path
- later add a runtime text-console layer on top of a framebuffer device, rather
  than trying to turn the early loader marker code directly into the final
  console design

## 3. What Circle Confirms About Pi 4 USB Keyboard Input

Circle is useful here too, but mostly as a reminder of sequencing.

Key source files:

- `external/circle/include/circle/usb/usbhcidevice.h`
- `external/circle/include/circle/usb/xhcidevice.h`
- `external/circle/lib/usb/xhcidevice.cpp`
- `external/circle/include/circle/bcmpciehostbridge.h`
- `external/circle/lib/usb/usbdevicefactory.cpp`
- `external/circle/include/circle/usb/usbkeyboard.h`
- `external/circle/lib/usb/usbkeyboard.cpp`
- `external/circle/include/circle/input/keyboardbehaviour.h`
- `external/circle/lib/input/keyboardbehaviour.cpp`
- `external/circle/include/circle/input/keyboardbuffer.h`
- `external/circle/lib/input/keyboardbuffer.cpp`
- `external/circle/sample/08-usbkeyboard/kernel.cpp`
- `external/circle/sample/08-usbkeyboard/kernel.h`

### 3.1 Pi 4 keyboard path depends on PCIe plus xHCI

This is the most important sequencing result from Circle.

On Pi 4:

- `CUSBHCIDevice` resolves to `CXHCIDevice`
- not to the older DWC host controller path

That mapping is explicit in:

- `external/circle/include/circle/usb/usbhcidevice.h`

Then `CXHCIDevice::Initialize()` does Pi 4-specific work:

- initialize the Broadcom PCIe host bridge
- issue `PROPTAG_NOTIFY_XHCI_RESET`
- enable the VL805 xHCI device over PCIe

That is explicit in:

- `external/circle/lib/usb/xhcidevice.cpp`
- `external/circle/include/circle/bcmpciehostbridge.h`

Implication for Phoenix:

- USB keyboard on Pi 4 is not the next cheap no-UART improvement
- it depends on PCIe host support plus xHCI host support plus HID keyboard
  support
- it should stay behind the current earlier milestones:
  HDMI visibility, first real boot, storage confidence, then PCIe and xHCI

So Circle is useful here primarily to validate our bring-up ordering, not to
justify jumping straight to keyboard support.

### 3.2 Device matching and enumeration model

Circle matches USB devices in a small factory:

- keyboards are matched on interface string `int3-1-1`
- then instantiated as `CUSBKeyboardDevice`
- exported through the device-name service as `ukbdN`

That happens in:

- `external/circle/lib/usb/usbdevicefactory.cpp`
- `external/circle/lib/usb/usbkeyboard.cpp`

This is a useful design clue for Phoenix:

- keep keyboard support as a separate HID keyboard driver layered on top of the
  generic USB host stack
- expose keyboard devices through a stable namespace rather than hardwiring them
  into the shell path

### 3.3 HID keyboard data path

Circle uses the USB boot keyboard report format:

- report size `8`
- first byte modifiers
- one reserved byte
- six keycode bytes

That is explicit in:

- `external/circle/include/circle/usb/usbkeyboard.h`
- `external/circle/include/circle/usb/usbhid.h`

`CUSBKeyboardDevice::ReportHandler()`:

- optionally strips a leading report ID for a known quirk
- compares current and previous reports
- emits modifier press or release events
- emits key press or release events only on changes
- feeds cooked behavior translation or raw handlers

This is useful for Phoenix later because it suggests a clean split:

- a transport/HID layer that reports raw boot keyboard state
- a translation layer that turns state changes into cooked input events
- an optional raw mode for diagnostics and low-level debugging

### 3.4 Keyboard behavior and LED policy

Circle separates keyboard policy from transport:

- `CKeyboardBehaviour` tracks modifiers and last key
- it translates USB key codes through a keymap layer
- it supports cooked string callbacks, console-switch actions, and shutdown
  actions
- LED state is derived from behavior state and pushed back to the keyboard

Important operational detail:

- `UpdateLEDs()` is intentionally called from non-interrupt context in the main
  loop
- `SetLEDs()` must not run in interrupt context

That is visible in:

- `external/circle/lib/input/keyboardbehaviour.cpp`
- `external/circle/lib/usb/usbkeyboard.cpp`
- `external/circle/sample/08-usbkeyboard/kernel.cpp`

This is a good future rule for Phoenix too:

- do not bury LED or policy writes inside interrupt handlers
- keep interrupt-time work small and defer side-effect-heavy control transfers

### 3.5 Plug-and-play update loop

The sample keyboard app does not assume the keyboard exists at boot. It:

- initializes the screen, serial, logger, interrupt system, timer, and USB HCI
- repeatedly calls `UpdatePlugAndPlay()` from task context
- asks the device-name service for `ukbd1`
- registers handlers after the keyboard appears

This is important because it matches how Phoenix should eventually structure USB
input too:

- enumeration must be asynchronous
- shell or console consumers should not assume the keyboard is already present
- hotplug needs a real device lifecycle, not a single static boot probe

## 4. What Circle Suggests We Should Do Next

Circle makes two sequencing decisions clearer.

### 4.1 Near-term: keep pushing HDMI visibility

For the current lab shape:

- real Pi 4 board
- HDMI screen
- no USB-TTL cable

the next valuable work should stay in the HDMI path, not USB keyboard.

The strongest Circle-inspired next refinement is:

- turn the current `plo` framebuffer marker into a minimal text or status
  banner with a few deterministic states such as:
  boot reached, DTB loaded, kernel jump started, failure fallback

That gives real-board observability without widening into a full graphics stack.

### 4.2 Mid-term: do not start USB keyboard without PCIe plus xHCI

Circle confirms that a realistic Pi 4 USB keyboard path is:

1. BCM2711 PCIe host bridge
2. VL805 xHCI initialization
3. USB enumeration and hub handling
4. HID keyboard support
5. cooked input integration for the shell or console

So future agents should not plan a “small USB keyboard step” before the PCIe
and xHCI milestones exist.

## 5. Concrete Phoenix Guidance Derived From Circle

### 5.1 Keep

- low physical mailbox/property request buffers on Pi 4
- firmware property-tag framebuffer allocation for early visibility
- small layered design: raw framebuffer first, text console later
- deferred keyboard LED updates outside interrupt context
- asynchronous USB hotplug expectations

### 5.2 Avoid

- copying Circle code verbatim
- treating USB keyboard support as an early shortcut around UART absence
- collapsing framebuffer allocation, text rendering, and final runtime display
  policy into one patch

### 5.3 Apply Soon

- add one HDMI-visible text or status refinement in `plo`
- keep the current QEMU HDMI smoke green while making that refinement
- use Circle’s display-selection and sane-fallback behavior as a reference if
  the first real-board test shows display-selection issues

## 6. Most Relevant Circle Paths To Revisit Later

For early HDMI/runtime display follow-up:

- `external/circle/lib/bcmframebuffer.cpp`
- `external/circle/lib/screen.cpp`
- `external/circle/lib/terminal.cpp`

For Pi 4 PCIe and xHCI follow-up:

- `external/circle/include/circle/bcmpciehostbridge.h`
- `external/circle/lib/usb/xhcidevice.cpp`
- `external/circle/lib/usb/xhcimmiospace.cpp`
- `external/circle/lib/usb/xhcislotmanager.cpp`
- `external/circle/lib/usb/xhcieventmanager.cpp`

For HID keyboard follow-up:

- `external/circle/lib/usb/usbdevicefactory.cpp`
- `external/circle/lib/usb/usbkeyboard.cpp`
- `external/circle/lib/input/keyboardbehaviour.cpp`
- `external/circle/lib/input/keyboardbuffer.cpp`

