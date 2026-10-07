#!/usr/bin/env python3
"""mp4box.py -- the few ISO-BMFF (MP4) facts the media test set needs, Python standard library only.

Used by gen-ladders.sh (through ladder-manifest.py: RFC 6381 codec strings for the HLS masters,
which ffmpeg's hls muxer leaves out for HEVC), by serve-media.py (the live simulation shifts
the fragments' baseMediaDecodeTime so a looped VOD ladder stays continuous) and by
check-media.py (fragment continuity).

  tracks(data)            the tracks of an init segment or a whole non-fragmented MP4:
                          [{id, handler, timescale, fourcc, codec, width, height, trex_duration}]
  fragments(data)         the track fragments of a media segment:
                          [{track, tfdt, tfdt_offset, tfdt_version, duration, samples}]
                          (duration in the track's timescale; trex_duration from the init
                          segment when neither trun nor tfhd carries sample durations)
  shift_tfdt(data, ticks) a copy of a media segment with every tfdt advanced by ticks[track]
  python3 mp4box.py FILE...   prints both, for a quick look

SPDX-License-Identifier: BSD-3-Clause
"""
import struct
import sys

CONTAINERS = {b"moov", b"trak", b"mdia", b"minf", b"stbl", b"mvex", b"moof", b"traf", b"edts", b"dinf"}


def boxes(data, start=0, end=None):
    """yield (type, payload_start, box_end) for the boxes in data[start:end]"""
    end = len(data) if end is None else end
    pos = start
    while pos + 8 <= end:
        size, kind = struct.unpack_from(">I4s", data, pos)
        header = 8
        if size == 1:
            size = struct.unpack_from(">Q", data, pos + 8)[0]
            header = 16
        elif size == 0:
            size = end - pos
        if size < header or pos + size > end:
            raise ValueError("truncated %r box at %d (size %d, %d left)" % (kind, pos, size, end - pos))
        yield kind, pos + header, pos + size
        pos += size


def find(data, path, start=0, end=None):
    """every (payload_start, box_end) at a box path such as b"moov/trak/mdia/mdhd" """
    first, _, rest = path.partition(b"/")
    found = []
    for kind, p, e in boxes(data, start, end):
        if kind == first:
            found.extend(find(data, rest, p, e) if rest else [(p, e)])
    return found


def _hevc_codec(fourcc, data, p, e):
    """hvc1.<profile>.<compat>.<tier><level>.<constraints> (ISO/IEC 14496-15 Annex E)"""
    c = data[p:e]
    space = c[1] >> 6
    tier = (c[1] >> 5) & 1
    profile = c[1] & 0x1F
    compat = struct.unpack_from(">I", c, 2)[0]
    compat_rev = int("{:032b}".format(compat)[::-1], 2)
    constraints = list(c[6:12])
    while constraints and constraints[-1] == 0:
        constraints.pop()
    level = c[12]
    s = "%s.%s%d.%X.%s%d" % (fourcc, ("", "A", "B", "C")[space], profile, compat_rev, "LH"[tier], level)
    return s + "".join(".%X" % b for b in constraints)


def _avc_codec(fourcc, data, p, e):
    c = data[p:e]
    return "%s.%02x%02x%02x" % (fourcc, c[1], c[2], c[3])


def _esds_codec(data, p, e):
    """mp4a.<object type>[.<audio object type>] from an esds box"""
    pos = p + 4  # version + flags

    def descriptor(pos):
        tag = data[pos]
        pos += 1
        size = 0
        for _ in range(4):
            b = data[pos]
            pos += 1
            size = (size << 7) | (b & 0x7F)
            if not b & 0x80:
                break
        return tag, pos, size

    tag, pos, _ = descriptor(pos)
    if tag != 3:
        return "mp4a"
    flags = data[pos + 2]
    pos += 3
    if flags & 0x80:
        pos += 2
    if flags & 0x40:
        pos += 1 + data[pos]
    if flags & 0x20:
        pos += 2
    tag, pos, _ = descriptor(pos)
    if tag != 4:
        return "mp4a"
    oti = data[pos]
    pos += 13
    if oti == 0x40 and pos < e:
        tag, pos, size = descriptor(pos)
        if tag == 5 and size:
            aot = data[pos] >> 3
            if aot == 31:
                aot = 32 + (((data[pos] & 7) << 3) | (data[pos + 1] >> 5))
            return "mp4a.40.%d" % aot
    return "mp4a.%02X" % oti


def _sample_entry(data, p, e, handler):
    """(fourcc, codec string, width, height) of the first entry of an stsd payload"""
    for kind, sp, se in boxes(data, p + 8, e):
        fourcc = kind.decode("latin-1")
        if handler == "vide":
            width, height = struct.unpack_from(">HH", data, sp + 24)
            child = sp + 78
        elif handler == "soun":
            width = height = 0
            version = struct.unpack_from(">H", data, sp + 8)[0]
            child = sp + 28 + (16 if version == 1 else 36 if version == 2 else 0)
        else:
            return fourcc, fourcc, 0, 0
        codec = fourcc
        for ck, cp, ce in boxes(data, child, se):
            if ck == b"hvcC":
                codec = _hevc_codec(fourcc, data, cp, ce)
            elif ck == b"avcC":
                codec = _avc_codec(fourcc, data, cp, ce)
            elif ck == b"esds":
                codec = _esds_codec(data, cp, ce)
            elif ck == b"dOps":
                codec = "opus"
            elif ck == b"dac3":
                codec = "ac-3"
            elif ck == b"dec3":
                codec = "ec-3"
            elif ck == b"dfLa":
                codec = "flac"
        return fourcc, codec, width, height
    return "", "", 0, 0


def tracks(data):
    """the tracks of an init segment (or of a whole MP4)"""
    moov = find(data, b"moov")
    if not moov:
        raise ValueError("no moov box")
    mp, me = moov[0]
    trex = {}
    for p, _ in find(data, b"mvex/trex", mp, me):
        track_id, _, duration = struct.unpack_from(">III", data, p + 4)
        trex[track_id] = duration
    out = []
    for tp, te in find(data, b"trak", mp, me):
        (hp, _), = find(data, b"tkhd", tp, te)
        track_id = struct.unpack_from(">I", data, hp + (20 if data[hp] == 1 else 12))[0]
        (dp, _), = find(data, b"mdia/mdhd", tp, te)
        timescale = struct.unpack_from(">I", data, dp + (20 if data[dp] == 1 else 12))[0]
        (rp, _), = find(data, b"mdia/hdlr", tp, te)
        handler = data[rp + 8:rp + 12].decode("latin-1")
        (sp, se), = find(data, b"mdia/minf/stbl/stsd", tp, te)
        fourcc, codec, width, height = _sample_entry(data, sp, se, handler)
        out.append({"id": track_id, "handler": handler, "timescale": timescale, "fourcc": fourcc,
                    "codec": codec, "width": width, "height": height, "trex_duration": trex.get(track_id)})
    return out


def fragments(data, init_tracks=None):
    """the track fragments of a media segment (every moof in it)"""
    trex = {t["id"]: t["trex_duration"] for t in (init_tracks or [])}
    out = []
    for mp, me in find(data, b"moof"):
        for tp, te in find(data, b"traf", mp, me):
            (hp, _), = find(data, b"tfhd", tp, te)
            flags = struct.unpack_from(">I", data, hp)[0] & 0xFFFFFF
            track_id = struct.unpack_from(">I", data, hp + 4)[0]
            pos = hp + 8
            if flags & 0x01:
                pos += 8
            if flags & 0x02:
                pos += 4
            default_duration = struct.unpack_from(">I", data, pos)[0] if flags & 0x08 else trex.get(track_id)
            tfdt = tfdt_offset = tfdt_version = None
            for p, _ in find(data, b"tfdt", tp, te):
                tfdt_version = data[p]
                tfdt_offset = p + 4
                tfdt = struct.unpack_from(">Q" if tfdt_version == 1 else ">I", data, tfdt_offset)[0]
            duration = samples = 0
            for p, _ in find(data, b"trun", tp, te):
                tflags = struct.unpack_from(">I", data, p)[0] & 0xFFFFFF
                count = struct.unpack_from(">I", data, p + 4)[0]
                pos = p + 8 + (4 if tflags & 0x01 else 0) + (4 if tflags & 0x04 else 0)
                stride = 4 * sum(1 for bit in (0x100, 0x200, 0x400, 0x800) if tflags & bit)
                samples += count
                if tflags & 0x100:
                    duration += sum(struct.unpack_from(">I", data, pos + i * stride)[0] for i in range(count))
                elif default_duration is not None:
                    duration += default_duration * count
                else:
                    raise ValueError("track %d: no sample durations (trun, tfhd and trex have none)" % track_id)
            out.append({"track": track_id, "tfdt": tfdt, "tfdt_offset": tfdt_offset, "tfdt_version": tfdt_version,
                        "duration": duration, "samples": samples})
    return out


def shift_tfdt(data, ticks):
    """a copy of a media segment, every track's tfdt advanced by ticks[track_id] (dict) or ticks (int)"""
    out = bytearray(data)
    for frag in _tfdt_only(data):
        add = ticks.get(frag["track"], 0) if isinstance(ticks, dict) else ticks
        value = frag["tfdt"] + add
        if frag["tfdt_version"] == 1:
            struct.pack_into(">Q", out, frag["tfdt_offset"], value)
        elif value < 1 << 32:
            struct.pack_into(">I", out, frag["tfdt_offset"], value)
        else:
            raise ValueError("track %d: a version-0 tfdt cannot hold %d" % (frag["track"], value))
    return bytes(out)


def _tfdt_only(data):
    """the tfdt fields of a media segment (no sample-duration bookkeeping)"""
    for mp, me in find(data, b"moof"):
        for tp, te in find(data, b"traf", mp, me):
            (hp, _), = find(data, b"tfhd", tp, te)
            track_id = struct.unpack_from(">I", data, hp + 4)[0]
            for p, _ in find(data, b"tfdt", tp, te):
                version = data[p]
                yield {"track": track_id, "tfdt_version": version, "tfdt_offset": p + 4,
                       "tfdt": struct.unpack_from(">Q" if version == 1 else ">I", data, p + 4)[0]}


def main():
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            data = f.read()
        if find(data, b"moov"):
            for t in tracks(data):
                print("%s track %d %s %s %dx%d timescale=%d" % (path, t["id"], t["handler"], t["codec"], t["width"],
                                                                 t["height"], t["timescale"]))
        for fr in fragments(data):
            print("%s fragment track=%d tfdt=%d duration=%d samples=%d" % (path, fr["track"], fr["tfdt"], fr["duration"],
                                                                            fr["samples"]))


if __name__ == "__main__":
    main()
