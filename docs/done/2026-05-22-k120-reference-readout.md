# Logitech K120 USB keyboard — reference readout

Captured 2026-05-22 from the Pi 4's keyboard temporarily plugged
into the Linux dev host (`lsusb -v -d 046d:c31c` + sysfs). When
Phoenix-RTOS USB on the Pi 4 starts enumerating this keyboard,
its output should match this.

## Device descriptor (18 bytes)

```
bLength                18
bDescriptorType         1 (DEVICE)
bcdUSB               1.10
bDeviceClass            0 (class declared per-interface)
bDeviceSubClass         0
bDeviceProtocol         0
bMaxPacketSize0         8
idVendor           0x046d  Logitech
idProduct          0xc31c  Keyboard K120
bcdDevice           64.02
iManufacturer           1  "Logitech"
iProduct                2  "USB Keyboard"
iSerial                 0
bNumConfigurations      1
```

## Configuration descriptor (header + 0x3B = 59 bytes total)

```
bLength                 9
bDescriptorType         2 (CONFIG)
wTotalLength       0x003b
bNumInterfaces          2
bConfigurationValue     1
iConfiguration          3  "U64.02_B0012"
bmAttributes         0xa0  (Bus Powered, Remote Wakeup)
MaxPower               90mA  (encoded as 0x2D = 45 * 2)
```

## Interface 0 — Boot keyboard

```
bInterfaceNumber        0
bAlternateSetting       0
bNumEndpoints           1
bInterfaceClass         3  (HID)
bInterfaceSubClass      1  (Boot)
bInterfaceProtocol      1  (Keyboard)
iInterface              2  "USB Keyboard"

  HID descriptor:
    bcdHID               1.10
    bNumDescriptors      1
    bDescriptorType     34  (Report)
    wDescriptorLength   65

  Endpoint 1 IN:
    bEndpointAddress  0x81
    Transfer Type     Interrupt
    wMaxPacketSize    8
    bInterval         10  (poll every 10ms at Low-Speed = every frame)
```

## Interface 1 — HID generic (multimedia / consumer keys)

```
bInterfaceNumber        1
bAlternateSetting       0
bNumEndpoints           1
bInterfaceClass         3  (HID)
bInterfaceSubClass      0
bInterfaceProtocol      0
iInterface              2  "USB Keyboard"

  HID descriptor:
    bcdHID               1.10
    bNumDescriptors      1
    bDescriptorType     34  (Report)
    wDescriptorLength  184

  Endpoint 2 IN:
    bEndpointAddress  0x82
    Transfer Type     Interrupt
    wMaxPacketSize    4
    bInterval         255  (poll every 255 frames at Low-Speed)
```

## HID report descriptor — interface 0 (65 bytes)

```
00000000: 0501 0906 a101 0507 19e0 29e7 1500 2501  ..........)...%.
00000010: 7501 9508 8102 9501 7508 8101 9505 7501  u.......u.....u.
00000020: 0508 1901 2905 9102 9501 7503 9101 9506  ....).....u.....
00000030: 7508 1500 26ff 0005 0719 002a ff00 8100  u...&......*....
00000040: c0                                       .
```

Decoded:
- Usage Page (Generic Desktop) — Usage (Keyboard) — Application
  Collection
- 8 modifier-key bits (Usage Page Keyboard E0..E7, 1 bit × 8 = 8 bits)
- 8 padded reserved bits
- 5 LED output bits (Caps Lock etc.) + 3 padded
- 6 keycode bytes (1-byte each, range 0..255) — standard boot keyboard

→ 8-byte input report: `[modifiers, reserved, key1..key6]`.

## HID report descriptor — interface 1 (184 bytes)

This is the consumer/multimedia keys interface. First three report
IDs (0x01, 0x02, 0x03, 0x05) cover:
- 0x01: power keys (3 LE bytes)
- 0x02: system control (mute, vol-up, vol-down)
- 0x03: consumer controls (16 bits + padding)
- 0x05: vendor-specific 5-byte feature

## Other state

```
speed: 1.5  (i.e. 1.5 Mbps, USB Low-Speed)
maxchild: 0  (not a hub)
Device Status: 0x0000 (Bus Powered)
```

## Standard control-transfer sequence Phoenix should issue

1. GetDescriptor(DEVICE, 8 bytes) — get bMaxPacketSize0=8
2. SetAddress(N)
3. GetDescriptor(DEVICE, 18 bytes) — full descriptor above
4. GetDescriptor(CONFIG, 9 bytes) — header, learn wTotalLength=59
5. GetDescriptor(CONFIG, 59 bytes) — full hierarchy
6. GetDescriptor(STRING, langID=0x0409, idx=1) → "Logitech"
7. GetDescriptor(STRING, langID=0x0409, idx=2) → "USB Keyboard"
8. GetDescriptor(STRING, langID=0x0409, idx=3) → "U64.02_B0012"
9. SetConfiguration(1)
10. (HID class) GetDescriptor(REPORT, intf=0, 65 bytes)
11. (HID class) GetDescriptor(REPORT, intf=1, 184 bytes)
12. SetIdle(intf=0, duration=0) — only-on-change reports
13. SetProtocol(intf=0, BOOT=0) — actually, default is REPORT;
    for psh's `kbd` driver we may want BOOT mode = 0
14. Start IN-token polling on EP 1 IN @ 10ms

When step 1's GetDescriptor(DEVICE, 8) comes back with
`idVendor=0x046d idProduct=0xc31c`, you've confirmed Phoenix-RTOS
USB enumeration is working end-to-end.

## Why this is useful for the Pi 4 USB bring-up

Once `xhci_capProbe` succeeds (after the XHCI_MAP_SIZE fix), the
controller will reset, the root hub will detect a connect on
whichever VL805 downstream port the keyboard is plugged into,
and Phoenix-RTOS USB stack will issue the control-transfer
sequence above. Comparing what Phoenix sees against the known
descriptors above tells us exactly which step (if any) breaks.
