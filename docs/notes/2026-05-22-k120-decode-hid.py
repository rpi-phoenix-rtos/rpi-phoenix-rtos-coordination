#!/usr/bin/env python3
"""Decode HID Report Descriptor — minimal in-tree decoder for K120 docs."""
import sys

ITEM_TYPE = {0: "Main", 1: "Global", 2: "Local"}
MAIN_TAG = {
    0x8: "Input", 0x9: "Output", 0xA: "Collection",
    0xB: "Feature", 0xC: "EndCollection",
}
GLOBAL_TAG = {
    0x0: "UsagePage", 0x1: "LogicalMin", 0x2: "LogicalMax",
    0x3: "PhysicalMin", 0x4: "PhysicalMax", 0x5: "UnitExp",
    0x6: "Unit", 0x7: "ReportSize", 0x8: "ReportID",
    0x9: "ReportCount", 0xA: "Push", 0xB: "Pop",
}
LOCAL_TAG = {
    0x0: "Usage", 0x1: "UsageMin", 0x2: "UsageMax",
    0x3: "DesignatorIdx", 0x4: "DesignatorMin", 0x5: "DesignatorMax",
    0x7: "StringIdx", 0x8: "StringMin", 0x9: "StringMax",
    0xA: "Delimiter",
}
USAGE_PAGE = {
    0x01: "Generic Desktop", 0x07: "Keyboard/Keypad",
    0x08: "LEDs", 0x0C: "Consumer", 0xFF00: "Vendor-defined",
}
GD_USAGE = {0x06: "Keyboard", 0x80: "System Control"}
COLLECTION = {0x00: "Physical", 0x01: "Application", 0x02: "Logical"}
INPUT_BITS = ["Const", "Var", "Rel", "Wrap", "NonLin", "NoPref", "Null", "Vol"]


def fmt_data(b):
    if not b:
        return ""
    val = int.from_bytes(b, "little", signed=False)
    return f"0x{val:0{2*len(b)}X} ({val})"


def fmt_data_signed(b):
    if not b:
        return ""
    val = int.from_bytes(b, "little", signed=True)
    return f"{val}"


def decode(blob, label):
    print(f"=== {label} ({len(blob)} bytes) ===")
    i = 0
    indent = 0
    cur_usage_page = None
    while i < len(blob):
        prefix = blob[i]
        bSize = prefix & 0x03
        if bSize == 3:
            bSize = 4
        bType = (prefix >> 2) & 0x03
        bTag = (prefix >> 4) & 0x0F
        data = blob[i+1:i+1+bSize]
        i += 1 + bSize

        type_name = ITEM_TYPE.get(bType, f"?{bType}")
        tag_name = "?"
        comment = ""
        if bType == 0:
            tag_name = MAIN_TAG.get(bTag, f"?Tag{bTag:X}")
            if tag_name in ("Input", "Output", "Feature") and data:
                bits = int.from_bytes(data, "little")
                flags = []
                for n, bn in enumerate(INPUT_BITS):
                    if bits & (1 << n):
                        flags.append(bn)
                comment = "[" + ",".join(flags) + "]" if flags else "[Data,Array,Abs]"
            elif tag_name == "Collection" and data:
                comment = COLLECTION.get(data[0], f"?0x{data[0]:02X}")
            elif tag_name == "EndCollection":
                indent -= 1
        elif bType == 1:
            tag_name = GLOBAL_TAG.get(bTag, f"?Tag{bTag:X}")
            if tag_name == "UsagePage" and data:
                val = int.from_bytes(data, "little")
                cur_usage_page = val
                comment = USAGE_PAGE.get(val, f"0x{val:04X}")
            elif tag_name in ("LogicalMin", "LogicalMax", "PhysicalMin", "PhysicalMax"):
                comment = fmt_data_signed(data)
            elif data:
                comment = fmt_data(data)
        elif bType == 2:
            tag_name = LOCAL_TAG.get(bTag, f"?Tag{bTag:X}")
            if tag_name in ("Usage", "UsageMin", "UsageMax") and data:
                val = int.from_bytes(data, "little")
                if cur_usage_page == 0x01:
                    comment = GD_USAGE.get(val, f"0x{val:02X}")
                else:
                    comment = f"0x{val:04X}"

        pad = "  " * max(indent, 0)
        hexbytes = " ".join(f"{b:02X}" for b in blob[i-1-bSize:i])
        print(f"  {hexbytes:<14} {pad}{type_name:7s} {tag_name:<14} {comment}")

        if bType == 0 and tag_name == "Collection":
            indent += 1
    print()


for path, lbl in [("/tmp/k120-rd-if0.bin", "Interface 0 — Boot Keyboard"),
                  ("/tmp/k120-rd-if1.bin", "Interface 1 — Consumer Keys")]:
    with open(path, "rb") as f:
        decode(f.read(), lbl)
