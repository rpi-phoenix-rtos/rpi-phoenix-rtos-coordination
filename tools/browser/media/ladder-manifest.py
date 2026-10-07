#!/usr/bin/env python3
"""ladder-manifest.py -- finishes a ladder made by gen-ladders.sh and describes it in manifest.json.

  ladder-manifest.py hls <ladder dir> --renditions DIR [--secs N] [--ffmpeg VERSION]
  ladder-manifest.py mse <mse dir>    --renditions DIR [--secs N] [--ffmpeg VERSION]

hls: ffmpeg's hls muxer writes CODECS only for H.264/AAC and never FRAME-RATE; the stage-0
     variant policy (docs/browser/MSE-DESIGN.md §6.4) reads both, and a master without them would
     make "assume H.264" the only possible answer. So master.m3u8 is rewritten with the RFC 6381
     strings read from the streams themselves (hvcC/avcC/esds of the fMP4 init segments; for
     MPEG-TS the rendition the variant was copied from) and the renditions' frame rate.
     manifest.json: every variant and rendition group with its playlist (segments, durations,
     init section, byte ranges, key; for fMP4 the fragments' tfdt/duration per track, which the
     live simulation in serve-media.py uses), what the policy must choose ("expect"), and every
     file's size + sha256.
mse: manifest.json for the MSE page: per representation its MIME type with codecs, size, init
     segment and segments with their start/duration in seconds.

SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mp4box  # noqa: E402

# what the stage-0 policy must choose (design §10.1): variant NAME -> its index is looked up
EXPECT = {
    "hevc-fmp4": {"variant": "hevc-1080", "rule": "hevc8", "size": "1920x1080",
                  "why": "highest eligible HEVC 8-bit <= 1080p30"},
    "hevc-ts": {"variant": "hevc-1080", "rule": "hevc8", "size": "1920x1080", "why": "TS segments, HEVC 8-bit"},
    "hevc-main10": {"variant": "h264-720", "rule": "h264", "size": "1280x720",
                    "why": "Main10 ranks below H.264 720p", "forced_main10": "hevc10-1080"},
    "h264-only": {"variant": "h264-720", "rule": "h264", "size": "1280x720", "why": "the 720p cap"},
    "byterange": {"variant": "h264-720", "rule": "h264", "size": "1280x720", "why": "single variant, byte ranges"},
    "aes": {"variant": "h264-720", "rule": "h264", "size": "1280x720", "why": "single variant, AES-128"},
    "audio-only": {"variant": None, "rule": "fallback", "size": None,
                   "why": "no video: the lowest-bandwidth variant (informational)"},
}
ATTR = re.compile(r'([A-Z0-9-]+)=("[^"]*"|[^,]*)')


def attrs(text):
    return [(k, v) for k, v in ATTR.findall(text)]


def attr(pairs, key):
    for k, v in pairs:
        if k == key:
            return v.strip('"')
    return None


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def ffprobe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries",
                          "stream=codec_type,codec_name,width,height,avg_frame_rate,profile,pix_fmt",
                          "-of", "json", path], capture_output=True, text=True, check=True).stdout
    return json.loads(out)["streams"]


def rendition_info(rend, name, cache={}):
    """codec string, size and frame rate of renditions/<name>.mp4"""
    if name not in cache:
        path = os.path.join(rend, name + ".mp4")
        with open(path, "rb") as f:
            track = mp4box.tracks(f.read())[0]
        probe = ffprobe(path)[0]
        num, den = (int(x) for x in probe.get("avg_frame_rate", "0/1").split("/"))
        cache[name] = {"codec": track["codec"], "width": track["width"], "height": track["height"],
                       "fps": round(num / den, 3) if den and num else None, "pix_fmt": probe.get("pix_fmt")}
    return cache[name]


def read(path, start=0, length=None):
    with open(path, "rb") as f:
        f.seek(start)
        return f.read() if length is None else f.read(length)


def media_playlist(ladder, uri):
    """parse a media playlist: segments, init section, keys; fMP4 fragment timing per track"""
    path = os.path.join(ladder, uri)
    base = os.path.dirname(path)
    pl = {"uri": uri, "segments": [], "init": None, "keys": [], "ended": False}
    duration = None
    byterange = None
    next_offset = {}
    with open(path, encoding="utf-8") as f:
        lines = [x.strip() for x in f if x.strip()]
    for line in lines:
        if line.startswith("#EXT-X-TARGETDURATION:"):
            pl["target"] = int(line.split(":", 1)[1])
        elif line.startswith("#EXT-X-MAP:"):
            a = attrs(line.split(":", 1)[1])
            pl["init"] = {"uri": attr(a, "URI")}
            br = attr(a, "BYTERANGE")
            if br:
                n, _, o = br.partition("@")
                pl["init"]["range"] = [int(o or 0), int(n)]
        elif line.startswith("#EXT-X-KEY:"):
            a = attrs(line.split(":", 1)[1])
            pl["keys"].append({"method": attr(a, "METHOD"), "uri": attr(a, "URI"), "iv": attr(a, "IV")})
        elif line.startswith("#EXTINF:"):
            duration = float(line.split(":", 1)[1].split(",")[0])
        elif line.startswith("#EXT-X-BYTERANGE:"):
            byterange = line.split(":", 1)[1]
        elif line == "#EXT-X-ENDLIST":
            pl["ended"] = True
        elif not line.startswith("#"):
            seg = {"uri": line, "duration": duration}
            if byterange:
                n, _, o = byterange.partition("@")
                start = int(o) if o else next_offset.get(line, 0)
                seg["range"] = [start, int(n)]
                next_offset[line] = start + int(n)
            pl["segments"].append(seg)
            duration = byterange = None
    pl["duration"] = round(sum(s["duration"] for s in pl["segments"]), 6)
    pl["type"] = "fmp4" if pl["init"] else "ts"
    if pl["init"]:
        r = pl["init"].get("range")
        init = read(os.path.join(base, pl["init"]["uri"]), *(r or [0]))
        pl["tracks"] = mp4box.tracks(init)
        first = {}
        last = {}
        for seg in pl["segments"]:
            r = seg.get("range")
            frags = mp4box.fragments(read(os.path.join(base, seg["uri"]), *(r or [0])), pl["tracks"])
            seg["fragments"] = [{"track": fr["track"], "tfdt": fr["tfdt"], "duration": fr["duration"],
                                 "samples": fr["samples"]} for fr in frags]
            for fr in frags:
                first.setdefault(fr["track"], fr["tfdt"])
                last[fr["track"]] = fr["tfdt"] + fr["duration"]
        for t in pl["tracks"]:
            t["start"] = first.get(t["id"])
            t["end"] = last.get(t["id"])
    return pl


def codecs_of(ladder, rend, pl, variant_name):
    """the codec strings of a media playlist's own tracks"""
    if pl["type"] == "fmp4":
        return [t["codec"] for t in pl["tracks"]]
    if pl["keys"]:
        # encrypted MPEG-TS (the aes ladder): ffprobe cannot read a segment; it is the variant's
        # rendition with the AAC rendition muxed in, as gen-ladders.sh makes it
        return [rendition_info(rend, variant_name)["codec"], rendition_info(rend, "aac")["codec"]]
    seg = pl["segments"][0]
    streams = ffprobe(os.path.join(ladder, os.path.dirname(pl["uri"]), seg["uri"]))
    out = []
    for s in streams:
        if s["codec_type"] == "video":
            out.append(rendition_info(rend, variant_name)["codec"])
        elif s["codec_type"] == "audio":
            if s["codec_name"] != "aac":
                sys.exit("ladder-manifest: %s: TS audio %s, only AAC is mapped" % (pl["uri"], s["codec_name"]))
            out.append(rendition_info(rend, "aac")["codec"])
    return out


def set_attr(pairs, key, value, after):
    """set an attribute, keeping order; a new one goes after the first present key of `after`"""
    for i, (k, _) in enumerate(pairs):
        if k == key:
            pairs[i] = (key, value)
            return
    pos = len(pairs)
    for a in after:
        idx = [i for i, (k, _) in enumerate(pairs) if k == a]
        if idx:
            pos = idx[0] + 1
            break
    pairs.insert(pos, (key, value))


def do_hls(ladder, args):
    name = os.path.basename(os.path.normpath(ladder))
    master_path = os.path.join(ladder, "master.m3u8")
    with open(master_path, encoding="utf-8") as f:
        lines = [x.rstrip("\n") for x in f]
    media = []
    variants = []
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("#EXT-X-MEDIA:"):
            a = attrs(line.split(":", 1)[1])
            m = {"type": attr(a, "TYPE"), "group": attr(a, "GROUP-ID"), "name": attr(a, "NAME"),
                 "default": attr(a, "DEFAULT"), "uri": attr(a, "URI")}
            m["playlist"] = media_playlist(ladder, m["uri"])
            m["codecs"] = codecs_of(ladder, args.renditions, m["playlist"], None)
            media.append(m)
            out.append(line)
        elif line.startswith("#EXT-X-STREAM-INF:"):
            pairs = attrs(line.split(":", 1)[1])
            uri = lines[i + 1].strip()
            vname = os.path.dirname(uri) or uri
            pl = media_playlist(ladder, uri)
            codecs = codecs_of(ladder, args.renditions, pl, vname)
            group = attr(pairs, "AUDIO")
            if group:
                for m in media:
                    if m["group"] == group and m["type"] == "AUDIO":
                        codecs += [c for c in m["codecs"] if c not in codecs]
                        break
            set_attr(pairs, "CODECS", '"%s"' % ",".join(codecs), ["RESOLUTION", "AVERAGE-BANDWIDTH", "BANDWIDTH"])
            info = None
            if any(c.startswith(("hvc1", "hev1", "avc1", "avc3")) for c in codecs):
                info = rendition_info(args.renditions, vname)
                set_attr(pairs, "RESOLUTION", "%dx%d" % (info["width"], info["height"]),
                         ["AVERAGE-BANDWIDTH", "BANDWIDTH"])
                set_attr(pairs, "FRAME-RATE", "%.3f" % info["fps"], ["CODECS"])
            variants.append({"index": len(variants), "name": vname, "uri": uri,
                             "bandwidth": int(attr(pairs, "BANDWIDTH")),
                             "average_bandwidth": int(attr(pairs, "AVERAGE-BANDWIDTH") or 0) or None,
                             "resolution": attr(pairs, "RESOLUTION"), "frame_rate": attr(pairs, "FRAME-RATE"),
                             "codecs": ",".join(codecs), "audio_group": group,
                             "pix_fmt": info["pix_fmt"] if info else None, "playlist": pl})
            out.append("#EXT-X-STREAM-INF:" + ",".join("%s=%s" % kv for kv in pairs))
            out.append(uri)
            i += 1
        else:
            out.append(line)
        i += 1
    with open(master_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out).rstrip("\n") + "\n")

    expect = dict(EXPECT.get(name, {}))
    names = [v["name"] for v in variants]
    if expect.get("variant"):
        expect["index"] = names.index(expect["variant"])
    elif expect.get("rule") == "fallback":
        expect["index"] = min(range(len(variants)), key=lambda k: variants[k]["bandwidth"])
        expect["variant"] = names[expect["index"]]
    if expect.get("forced_main10"):
        expect["forced_main10_index"] = names.index(expect["forced_main10"])
    manifest = {"ladder": name, "kind": "hls", "master": "master.m3u8",
                "generated": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
                "generator": "tools/browser/media/gen-ladders.sh", "ffmpeg": args.ffmpeg, "secs": args.secs,
                "variants": variants, "media": media, "expect": expect, "files": files(ladder)}
    write_manifest(ladder, manifest)
    print("ladder-manifest: %s %d variants (%s) + %d renditions, expect %s" % (
        name, len(variants), " ".join("%s=%s" % (v["name"], v["codecs"]) for v in variants), len(media),
        expect.get("variant")))


def do_mse(top, args):
    reps = []
    for rep in sorted(os.listdir(top)):
        d = os.path.join(top, rep)
        if not os.path.isfile(os.path.join(d, "init.mp4")):
            continue
        tracks = mp4box.tracks(read(os.path.join(d, "init.mp4")))
        if len(tracks) != 1:
            sys.exit("ladder-manifest: mse %s has %d tracks (one per representation)" % (rep, len(tracks)))
        t = tracks[0]
        segs = []
        for f in sorted(x for x in os.listdir(d) if x.endswith(".m4s")):
            frags = mp4box.fragments(read(os.path.join(d, f)), tracks)
            start = frags[0]["tfdt"] / t["timescale"]
            dur = sum(fr["duration"] for fr in frags) / t["timescale"]
            segs.append({"uri": f, "start": round(start, 6), "duration": round(dur, 6),
                         "tfdt": frags[0]["tfdt"], "ticks": sum(fr["duration"] for fr in frags),
                         "bytes": os.path.getsize(os.path.join(d, f))})
        video = t["handler"] == "vide"
        reps.append({"id": rep, "kind": "video" if video else "audio",
                     "mime": '%s/mp4; codecs="%s"' % ("video" if video else "audio", t["codec"]),
                     "codec": t["codec"], "width": t["width"] or None, "height": t["height"] or None,
                     "timescale": t["timescale"], "init": "init.mp4", "segments": segs,
                     "duration": round(segs[-1]["start"] + segs[-1]["duration"] - segs[0]["start"], 6)})
    manifest = {"kind": "mse", "generated": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
                "generator": "tools/browser/media/gen-ladders.sh", "ffmpeg": args.ffmpeg, "secs": args.secs,
                "representations": reps, "files": files(top)}
    write_manifest(top, manifest)
    print("ladder-manifest: mse %s" % " ".join("%s=%s/%d" % (r["id"], r["codec"], len(r["segments"])) for r in reps))


def files(top):
    out = {}
    for d, _, names in os.walk(top):
        for n in sorted(names):
            if n in ("manifest.json", ".settings"):
                continue
            p = os.path.join(d, n)
            out[os.path.relpath(p, top)] = {"bytes": os.path.getsize(p), "sha256": sha256(p)}
    return dict(sorted(out.items()))


def write_manifest(top, manifest):
    with open(os.path.join(top, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("mode", choices=["hls", "mse"])
    ap.add_argument("dir")
    ap.add_argument("--renditions", required=True)
    ap.add_argument("--secs", type=float, default=60)
    ap.add_argument("--ffmpeg", default="?")
    args = ap.parse_args()
    (do_hls if args.mode == "hls" else do_mse)(os.path.abspath(args.dir), args)


if __name__ == "__main__":
    main()
