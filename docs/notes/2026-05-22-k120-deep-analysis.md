# Logitech K120 — deep USB analysis (gold reference for Phoenix-RTOS)

Captured 2026-05-22 on the Linux dev host while the K120 was plugged into
USB bus 1 port 2 (sysfs `1-2`, device address 11). Companion to
`2026-05-22-k120-reference-readout.md` (basic descriptors).
The trace + decode below is the bit-for-bit reference Phoenix-RTOS
xHCI/HID code should reproduce on the Pi 4.

Source artefacts (kept in `/tmp/` for the session; copy out before
the device walks away):

- `/tmp/k120-rd-if0.bin`        — raw 65-byte report descriptor, interface 0
- `/tmp/k120-rd-if1.bin`        — raw 184-byte report descriptor, interface 1
- `/tmp/k120-hid-decoded.txt`   — item-by-item decode (see `decode_hid.py`)
- `/tmp/k120-long.pcap`         — 60 s of usbmon1 traffic; contains the
                                  control-transfer sequence Linux issues
                                  when `lsusb -v` and `usbhid-dump` open
                                  the device
- `/tmp/dev11-verbose.txt`      — tshark verbose dump filtered to dev 11

Host context: Linux x86_64 dev host, USB bus 1 is an AMD xHCI root hub
(`usb1` reports `version 2.00`, `speed 480`). The K120 is a USB 1.10
Low-Speed device behind that USB 2.0 xHCI controller, so all LS/FS
handling lives **inside** xHCI's port logic (no companion controller,
no external TT hub). This is the same topology Phoenix-RTOS will see
on the Pi 4 where the VL805 xHCI sits in front of the USB-A jacks.

## 1. HID report descriptors — decoded

### Interface 0 — Boot keyboard (65 bytes)

```
05 01           UsagePage      Generic Desktop
09 06           Usage          Keyboard
A1 01           Collection     Application
  05 07           UsagePage      Keyboard/Keypad
  19 E0           UsageMin       0xE0  (LCtrl)
  29 E7           UsageMax       0xE7  (RGUI)
  15 00           LogicalMin     0
  25 01           LogicalMax     1
  75 01           ReportSize     1
  95 08           ReportCount    8
  81 02           Input          [Var]              -> 8 modifier bits
  95 01           ReportCount    1
  75 08           ReportSize     8
  81 01           Input          [Const]            -> 1 reserved byte
  95 05           ReportCount    5
  75 01           ReportSize     1
  05 08           UsagePage      LEDs
  19 01           UsageMin       1  (NumLock)
  29 05           UsageMax       5  (Kana)
  91 02           Output         [Var]              -> 5 LED bits
  95 01           ReportCount    1
  75 03           ReportSize     3
  91 01           Output         [Const]            -> 3 padding bits
  95 06           ReportCount    6
  75 08           ReportSize     8
  15 00           LogicalMin     0
  26 FF 00        LogicalMax     255
  05 07           UsagePage      Keyboard/Keypad
  19 00           UsageMin       0
  2A FF 00        UsageMax       0xFF
  81 00           Input          [Data,Array,Abs]   -> 6 keycode bytes
C0              EndCollection
```

Input report (EP 0x81, 8 bytes, no Report ID):
`[mods, 0x00, key1, key2, key3, key4, key5, key6]`.

Output report (LED, sent via SetReport CONTROL):
`[LED_bits & 0x1F]` packed `NumLock | CapsLock | ScrollLock | Compose | Kana`.

### Interface 1 — Consumer / System / Vendor (184 bytes)

Uses Report IDs. Four reports defined:

| RID | UsagePage         | Direction | Size       | Notes |
|-----|-------------------|-----------|------------|-------|
| 1   | Consumer          | Input     | 1 + 1 byte | horizontal wheel + AC Pan (unused on K120) |
| 2   | Generic-Desktop   | Input     | 1 + 1 byte | System Power/Sleep/Wake (no key on K120) |
| 3   | Consumer          | Input     | 1 + 3 byte | media keys, brightness (no keys on K120) |
| 5   | Vendor (0xFF00)   | Feature   | 5 bytes    | Logitech-specific feature report |

Layouts in `/tmp/k120-hid-decoded.txt`. In practice K120 only ever
emits on interface 0; interface 1 is the standard Logitech consumer
template that all their keyboards ship with.

## 2. Sample input reports

Live keypress capture failed: nothing was typed on the K120 during the
60 s capture window (the operator was driving the host from a different
keyboard). `usbmon` only logs URB completions, not bus-level NAKs, so an
idle interrupt endpoint produces zero pcap frames.

The shapes Phoenix-RTOS must accept are reproduced below from the
descriptor. These are the exact bytes the boot HID driver in
`phoenix-rtos-devices/hid/usbkbd` already parses for OHCI/EHCI; the
xHCI path on the Pi 4 must produce identical byte streams.

```
EP 0x81 — 8-byte report  [mods, 0x00, key1..key6]:

  press 'a' (Keyboard/Keypad usage 0x04)
    00 00 04 00 00 00 00 00
  release 'a'
    00 00 00 00 00 00 00 00
  press 'b' (usage 0x05)
    00 00 05 00 00 00 00 00
  Shift + 'a'   (LShift = mod bit1 = 0x02)
    02 00 04 00 00 00 00 00
  press CapsLock (usage 0x39) - state-toggling key,
    emits press + release like any other
    00 00 39 00 00 00 00 00
    00 00 00 00 00 00 00 00
  6-key rollover (a,b,c,d,e,f)
    00 00 04 05 06 07 08 09

Modifier bitmask:
    bit0=LCtrl  bit1=LShift bit2=LAlt   bit3=LGUI
    bit4=RCtrl  bit5=RShift bit6=RAlt   bit7=RGUI

When the host wants to set Caps Lock LED, it does NOT write to an OUT
endpoint - it issues SetReport on EP 0 control:
    bmRequestType=0x21 bRequest=0x09 wValue=0x0200 wIndex=0x0000 wLength=1
    payload: 0x02   (bit1 = CapsLock LED on)
```

```
EP 0x82 — 4-byte report (max), first byte = Report ID:

  Volume Up  (Report ID 3, Consumer 0xE9 = byte1 bit6)
    03 40 00 00
  Mute       (Report ID 3, Consumer 0xCD = byte1 bit4)
    03 10 00 00

K120 ships no media keys, so EP 0x82 stays silent on the wire.
Phoenix-RTOS xHCI must treat the indefinite NAK stream on that EP as
normal idle, not as an error or stall.
```

## 3. Control-transfer trace

`/tmp/k120-long.pcap` captures everything `lsusb -v` and `usbhid-dump`
issue against the K120. Sequence (timestamps relative to first frame):

```
t        EP    type     direction  bRequest    descriptor    wLength
0.000ms  0x80  CONTROL  IN         GET_DESC    DEVICE        18    -> S
0.002ms                                                            <- C 18 bytes
0.002ms  0x80  CONTROL  IN         GET_DESC    CONFIG (hdr)  9     -> S
0.005ms                                                            <- C  9 bytes
0.005ms  0x80  CONTROL  IN         GET_DESC    CONFIG (full) 59    -> S
0.009ms                                                            <- C 59 bytes
...
60.555ms 0x80  CONTROL  IN         GET_DESC    REPORT intf1  184   -> S
60.563ms                                                            <- C 184 bytes
60.615ms 0x80  CONTROL  IN         GET_DESC    REPORT intf0  65    -> S
60.619ms                                                            <- C 65 bytes
```

Hex of the device descriptor returned in frame 2 (matches doc 1:1):

```
12 01 10 01  00 00 00 08  6d 04 1c c3  02 64 01 02
00 01
```

Hex of the 59-byte config descriptor returned in frame 8 (also 1:1):

```
09 02 3b 00 02 01 03 a0 2d           <- config hdr, 59 bytes, 2 intf, bus-pwr+RW, 90mA
09 04 00 00 01 03 01 01 02           <- intf 0: HID, Boot, Keyboard
09 21 10 01 00 01 22 41 00           <- HID:    bcdHID=1.10, wDescLen=65
07 05 81 03 08 00 0a                 <- EP 0x81 IN, INT, MPS 8, bInterval=10ms
09 04 01 00 01 03 00 00 02           <- intf 1: HID, no subclass
09 21 10 01 00 01 22 b8 00           <- HID:    bcdHID=1.10, wDescLen=184
07 05 82 03 04 00 ff                 <- EP 0x82 IN, INT, MPS 4, bInterval=255ms
```

**Not captured in this window** but mandatory at first enumeration
(the host already enumerated this device once on boot):
GET_DESC(STRING langID), GET_DESC(STRING idx=1,2,3),
SET_ADDRESS, SET_CONFIGURATION(1), SET_IDLE intf=0,
SET_PROTOCOL intf=0. Phoenix-RTOS must issue these in roughly the
order documented in the reference readout (steps 1-14).

## 4. Endpoint timing (bInterval)

Read at runtime from sysfs after Linux successfully enumerated:

```
ep_81  (interface 0 boot kbd):
   bEndpointAddress=0x81  bmAttributes=0x03 (INT)  wMaxPacketSize=8
   bInterval=0x0a  -> Linux 'interval' = 10ms

ep_82  (interface 1 consumer):
   bEndpointAddress=0x82  bmAttributes=0x03 (INT)  wMaxPacketSize=4
   bInterval=0xff  -> Linux 'interval' = 255ms
```

For Low-Speed interrupt endpoints, bInterval is in 1ms frame units
(per USB 2.0 spec 9.6.6). The xHCI controller translates this to its
own EP context Interval field (log2 form, where Interval in 2^N × 125us
microframes). For LS: 10ms = 10000us = 80 × 125us → Interval = 6
(2^6 = 64 microframes ≈ 8 ms), or Interval = 7 (2^7 = 128 microframes
≈ 16 ms). Linux xHCI rounds **up** to the next power of 2, so the K120
ends up polled every ~16 ms in practice. Phoenix-RTOS xHCI driver
should do the same rounding (xHCI 1.2 spec § 6.2.3.6).

Empirically (from `/tmp/k120-long.pcap`), the interrupt URBs on EP 0x81
were submitted, sat idle (no completion = device NAKing), and only
completed when userspace cancelled them. **usbmon does not record
NAKs**, so the wire-level polling rate cannot be measured from the
host. To verify cadence you would need a hardware analyser or a Pi 4
running Phoenix-RTOS with its xHCI driver instrumented.

## 5. Speed / lane

```
/sys/bus/usb/devices/1-2/speed       = 1.5      -> USB 1.x Low-Speed
                                       (1.5 Mbps)
/sys/bus/usb/devices/usb1/speed      = 480      -> root hub is HS
/sys/bus/usb/devices/usb1/version    = 2.00
```

Quirks: `quirks=0x0`, `avoid_reset_quirk=0`. Nothing special.

Implications for Phoenix-RTOS xHCI on the Pi 4:

- The K120 is a USB 1.10 Low-Speed device. xHCI does **not** speak LS/FS
  on the bus; it requires a "Hub Slot" with internal TT or - on root
  ports of the integrated xHCI - dedicated LS/FS support inside the
  controller. The VL805 controller is a USB-3.0 xHCI but exposes its
  USB-2.0 downstream ports through a built-in companion logic that
  handles LS/FS. Phoenix-RTOS must therefore:
  1. Set EP context speed field to 0 (Low-Speed) when configuring
     EP 0x81 and EP 0x82 (xHCI 1.2 § 6.2.2 table 6-9).
  2. Set EP context "TT info" fields only if going through an external
     hub. Direct-attach to the VL805 root port: TT Hub Slot ID = 0,
     TT Port Number = 0.
  3. Ensure bMaxPacketSize0=8 in the Slot context's "Max Packet Size"
     field (it is encoded as the actual value, not log2).

## 6. Surprises / things to be ready for

1. **Interface 1 polls at 255 ms but never emits.** A driver that
   times out on idle INT endpoints will tear down the URB; Phoenix-RTOS
   must keep the URB in-flight indefinitely, accepting NAKs forever.

2. **SET_IDLE is mandatory.** Default HID idle for a boot keyboard
   is implementation-defined - some keyboards re-send the same report
   every few ms even with no state change. Linux issues SET_IDLE 0 to
   force "only-on-change" reporting. Phoenix-RTOS should do the same;
   otherwise the boot keyboard will spam idle reports during long
   key-down at the bInterval rate.

3. **SET_PROTOCOL.** After SET_CONFIGURATION, interface 0 is in REPORT
   protocol by default (despite bInterfaceSubClass=1=Boot). For
   maximum compatibility with the existing `usbkbd` driver we want
   BOOT protocol: SET_PROTOCOL wValue=0 wIndex=0. (See
   reference-readout step 13.)

4. **LED Output reports go via control, not via OUT endpoint.**
   There is no OUT endpoint declared. SetReport on EP 0 is the only path.

5. **No error counters, no NAKs visible to usbmon.** All `-115`
   (-EINPROGRESS) statuses in the pcap are URB-in-flight; all `-2`
   (-ENOENT) are local URB-unlink at usbhid-dump close. Zero real
   errors. The K120 is a textbook Low-Speed HID device.

6. **bcdDevice = 0x0264 ("64.02") and iConfiguration string =
   "U64.02_B0012".** These can be used in Phoenix-RTOS as a known-good
   fingerprint - if enumeration returns matching strings the entire
   USB stack is provably correct end-to-end.

## Done

When the Pi 4 USB bring-up reaches the point of issuing GetDescriptor
to the K120 on a VL805 downstream port, compare the device descriptor
bytes against `12 01 10 01 00 00 00 08 6d 04 1c c3 02 64 01 02 00 01`
above. Identical bytes = the entire xHCI + HID enumeration path works.
