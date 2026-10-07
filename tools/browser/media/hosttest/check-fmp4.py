#!/usr/bin/env python3
"""Host harness 2 of docs/browser/MSE-DESIGN.md §9.4: WebCore's fragmented-MP4 parser
(FFmpegFMP4Parser, patch webkit_wpe/patches/webkit-mse/0032 of phoenix-rtos-ports) against
FFmpeg's mov demuxer, under ASan + UBSan, no Pi.

For every fMP4 stream of the media test set (artifacts/media: the MSE segment sets and the
fMP4 HLS ladders) and for small streams it makes itself (muxed audio+video, implicit data
offsets, negative composition offsets, FLAC/AC-3/E-AC-3/MP3 sample entries):

  oracle  the parser's samples (pts, dts, duration, size, key flag, payload MD5) and track
          configurations (codec, extradata MD5) == ffprobe's packets and streams, with the edit
          list applied and without it (-ignore_editlist 1)
  splits  every split point of the init segment + the first 3 media segments (sparse inside a
          large mdat payload: its first/last 256 bytes and every 4099th byte) == the one-append run
  random  N runs of random append sizes over the same bytes == the one-append run
  reset   reset() inside every box of a media segment, then the next segments
  switch  a second initialization segment on the same parser (quality and codec switch)
  mutate  random byte changes (the sanitizers are the check)

  check-fmp4.py [--media DIR] [--runs N] [--quick] [--parser-dir DIR | --ports DIR]

The parser source is taken from --parser-dir (a patched WebKit tree's
Source/WebCore/platform/graphics/ffmpeg), else extracted from the 0032 patch in --ports
(default: the coordination repo's sources/phoenix-rtos-ports). Prints CHECK lines and a
CHECK-SUMMARY; exit 1 on a FAIL.

Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../../.."))
RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append(ok)
    print("CHECK %s %s%s" % (name, "PASS" if ok else "FAIL", " " + detail if detail else ""), flush=True)


def extract_new_files(patch, names, out):
    """the content of new files in a git patch (the parser ships as new files of 0032)"""
    text = open(patch, encoding="utf-8").read().split("\n")
    found = {}
    i = 0
    while i < len(text):
        m = re.match(r"^diff --git a/(\S+) b/\S+$", text[i])
        if m and os.path.basename(m.group(1)) in names:
            name = os.path.basename(m.group(1))
            while not text[i].startswith("@@"):
                i += 1
            i += 1
            body = []
            while i < len(text) and not text[i].startswith("diff --git"):
                if text[i].startswith("+"):
                    body.append(text[i][1:])
                i += 1
            found[name] = "\n".join(body) + "\n"
            continue
        i += 1
    for name in names:
        if name not in found:
            sys.exit("check-fmp4: %s not in %s" % (name, patch))
        with open(os.path.join(out, name), "w", encoding="utf-8") as f:
            f.write(found[name])


def build(parser_dir, work):
    exe = os.path.join(work, "fmp4-harness")
    cmd = ["g++", "-std=c++20", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
           "-fno-sanitize-recover=all", "-I" + os.path.join(HERE, "include"), "-I" + parser_dir,
           os.path.join(HERE, "fmp4-harness.cpp"), os.path.join(parser_dir, "FFmpegFMP4Parser.cpp"), "-lcrypto", "-o", exe]
    subprocess.run(cmd, check=True)
    return exe


def run(exe, args):
    p = subprocess.run([exe] + args, capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1"))
    return p.returncode, p.stdout, p.stderr


def top_boxes(data):
    out, pos = [], 0
    while pos + 8 <= len(data):
        size, kind = struct.unpack_from(">I4s", data, pos)
        if size == 1:
            size = struct.unpack_from(">Q", data, pos + 8)[0]
        if size < 8 or pos + size > len(data):
            break
        out.append((kind.decode("latin1"), pos, pos + size))
        pos += size
    return out


def split_fragmented(path, outdir):
    """one fragmented .mp4 -> init.mp4 (up to moov) + seg-N.m4s (each up to its mdat)"""
    data = open(path, "rb").read()
    os.makedirs(outdir, exist_ok=True)
    files, start, n = [], 0, 0
    for kind, b, e in top_boxes(data):
        if kind in ("moov", "mdat"):
            name = os.path.join(outdir, "init.mp4" if kind == "moov" else "seg-%05d.m4s" % n)
            open(name, "wb").write(data[start:e])
            files.append(name)
            start, n = e, n + 1
    return files


def make_tiny(work):
    """small streams for the cases the test set does not have (all splits are affordable here)"""
    sets = {}
    ff = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
    v = ["-f", "lavfi", "-i", "testsrc2=size=192x108:rate=30:duration=6"]
    a = ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=6"]
    x264 = ["-c:v", "libx264", "-bf", "3", "-g", "30", "-keyint_min", "30", "-sc_threshold", "0", "-pix_fmt", "yuv420p"]
    x265 = ["-c:v", "libx265", "-tag:v", "hvc1", "-x265-params", "keyint=30:min-keyint=30:scenecut=0:bframes=4:log-level=error", "-pix_fmt", "yuv420p"]
    frag = ["-movflags", "frag_keyframe+empty_moov+default_base_moof", "-f", "mp4"]
    afrag = ["-movflags", "empty_moov+default_base_moof", "-frag_duration", "1000000", "-f", "mp4"]
    # (AC-3: the moov needs the first packet's dac3, so the muxer delays it instead of writing it empty)
    ac3frag = ["-movflags", "delay_moov+default_base_moof", "-frag_duration", "1000000", "-f", "mp4"]
    recipes = {
        # audio + video in one file: two trafs per moof
        "tiny-avc-aac-muxed": v + a + x264 + ["-c:a", "aac", "-b:a", "64k"] + frag,
        # no base at all: the second traf's data follows the first's (implicit base)
        "tiny-hevc-opus-implicit": v + a + x265 + ["-c:a", "libopus", "-b:a", "48k", "-movflags",
                                                     "frag_keyframe+empty_moov+omit_tfhd_offset", "-f", "mp4"],
        # trun version 1, negative composition offsets
        "tiny-avc-negcto": v + x264 + ["-movflags", "frag_keyframe+empty_moov+default_base_moof+negative_cts_offsets", "-f", "mp4"],
        "tiny-flac": a + ["-c:a", "flac"] + afrag,
        "tiny-ac3": a + ["-c:a", "ac3", "-b:a", "128k"] + ac3frag,
        "tiny-eac3": a + ["-c:a", "eac3", "-b:a", "96k"] + ac3frag,
        "tiny-mp3": a + ["-c:a", "libmp3lame", "-b:a", "96k"] + afrag,
    }
    for name, args in recipes.items():
        out = os.path.join(work, name + ".mp4")
        p = subprocess.run(ff + args + [out], capture_output=True, text=True)
        if p.returncode:
            check("make " + name, False, p.stderr.strip()[-200:])
            continue
        sets[name] = split_fragmented(out, os.path.join(work, name))
    return sets


def media_sets(media):
    sets = {}
    for d in sorted(glob.glob(os.path.join(media, "mse", "*", "init.mp4"))):
        rep = os.path.dirname(d)
        sets["mse/" + os.path.basename(rep)] = [d] + sorted(glob.glob(os.path.join(rep, "seg-*.m4s")))
    for d in sorted(glob.glob(os.path.join(media, "ladders", "*", "*", "init_*.mp4"))):
        var = os.path.dirname(d)
        name = "ladders/" + os.path.relpath(var, os.path.join(media, "ladders"))
        if name.startswith("ladders/byterange"):
            continue  # one file with byte ranges: the same bytes as h264-only's segments
        sets[name] = [d] + sorted(glob.glob(os.path.join(var, "seg-*.m4s")))
    return sets


def ffprobe_samples(path, raw):
    cmd = ["ffprobe", "-v", "error", "-ignore_editlist", "1" if raw else "0", "-show_data_hash", "md5", "-of", "json",
           "-show_entries", "packet=stream_index,pts,dts,duration,size,flags:stream=index,codec_type,codec_name,extradata_size,width,height,sample_rate,channels",
           "-show_packets", "-show_streams", path]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(p.stderr)
    j = json.loads(p.stdout)
    per = {}
    for pk in j.get("packets", []):
        per.setdefault(pk["stream_index"], []).append("%s,%s,%s,%s,%s,%s" % (
            pk.get("pts"), pk.get("dts"), pk.get("duration"), pk.get("size"), "K" if pk.get("flags", "_")[0] == "K" else "_",
            pk.get("data_hash", "").replace("MD5:", "")))
    streams = []
    for s in sorted(j.get("streams", []), key=lambda s: s["index"]):
        if s.get("codec_type") in ("video", "audio"):
            streams.append(s)
    return per, streams


def parse_dump(text):
    tracks, samples = [], {}
    for line in text.splitlines():
        if line.startswith("TRACK "):
            tracks.append(dict(kv.split("=", 1) for kv in line[6:].split() if "=" in kv))
        elif line.startswith("SAMPLE "):
            kind, rest = line[7:].split(",", 1)
            samples.setdefault(len(samples), None)
    return tracks


def pts_shift(got, want):
    """FFmpeg's mov demuxer adds -min(composition offset) to every pts when a track has negative
    offsets (trun version 1), so that pts >= dts; the parser keeps pts = dts + offset (ISO/IEC
    14496-12, and what Chromium's MSE parser reports). Returns that shift when it is the only
    difference, else 0."""
    if len(got) != len(want) or not got:
        return 0
    g = [x.split(",") for x in got]
    w = [x.split(",") for x in want]
    if any(a[1:] != b[1:] for a, b in zip(g, w)):
        return 0
    deltas = {int(b[0]) - int(a[0]) for a, b in zip(g, w)}
    least = min(int(a[0]) - int(a[1]) for a in g)
    if len(deltas) == 1 and least < 0 and deltas == {-least}:
        return -least
    return 0


def parser_durations(got, want):
    """FFmpeg's mov demuxer runs the AC-3 / E-AC-3 parser, whose packet duration (a whole frame,
    1536 samples) replaces the container's for the last sample, which a muxer may shorten to the
    end of the audio. The parser reports the container's. Returns (container, codec) when that is
    the only difference, else None."""
    if len(got) != len(want) or not got or got[:-1] != want[:-1]:
        return None
    a, b = got[-1].split(","), want[-1].split(",")
    if a[:2] == b[:2] and a[3:] == b[3:] and int(a[2]) < int(b[2]):
        return a[2], b[2]
    return None


def oracle(exe, name, files, work):
    cat = os.path.join(work, "cat.mp4")
    with open(cat, "wb") as out:
        for f in files:
            out.write(open(f, "rb").read())
    for raw in (False, True):
        tag = "%s %s" % (name, "raw" if raw else "edits")
        rc, out, err = run(exe, ["dump"] + (["--raw"] if raw else []) + files)
        if rc:
            check("oracle " + tag, False, err.strip()[-300:])
            return
        tracks = [line for line in out.splitlines() if line.startswith("TRACK ")]
        mine = {}
        # the dump prints samples in file order; group them per track (ffprobe's stream order is
        # the trak order, which is the parser's track order)
        track_ids = [int(re.search(r"id=(\d+)", t).group(1)) for t in tracks]
        current = {}
        for line in out.splitlines():
            if line.startswith("SAMPLE "):
                kind, rest = line[7:].split(",", 1)
                # identify the track by kind when the stream has one track of each kind
                mine.setdefault(kind, []).append(rest)
        try:
            theirs, streams = ffprobe_samples(cat, raw)
        except RuntimeError as e:
            check("oracle " + tag, False, "ffprobe: " + str(e)[-200:])
            return
        problems, notes = [], []
        if len(streams) != len(tracks):
            problems.append("tracks %d vs ffprobe streams %d" % (len(tracks), len(streams)))
        for index, s in enumerate(streams):
            kind = s["codec_type"]
            got = mine.get(kind, [])
            want = theirs.get(s["index"], [])
            shift = pts_shift(got, want)
            parsed = parser_durations(got, want)
            if shift:
                notes.append("%s pts +%d in ffprobe (mov's dts_shift for negative composition offsets)" % (kind, shift))
            elif parsed:
                notes.append("%s last duration %s in the container, %s from FFmpeg's codec parser" % (kind, parsed[0], parsed[1]))
            elif got != want:
                diff = next((i for i, (g, w) in enumerate(zip(got, want)) if g != w), min(len(got), len(want)))
                problems.append("%s: %d vs %d samples, first difference at %d: %s | %s" % (
                    kind, len(got), len(want), diff, got[diff] if diff < len(got) else "-", want[diff] if diff < len(want) else "-"))
            if index < len(tracks):
                t = dict(kv.split("=", 1) for kv in tracks[index][6:].split() if "=" in kv)
                size, _, digest = t["extradata"].partition(":")
                if int(size) != int(s.get("extradata_size", 0)):
                    problems.append("%s extradata %s bytes vs ffprobe %s" % (kind, size, s.get("extradata_size", 0)))
                if digest != "-" and digest != s.get("extradata_hash", "").replace("MD5:", ""):
                    problems.append("%s extradata MD5 differs" % kind)
                if kind == "video" and t["size"] != "%sx%s" % (s.get("width"), s.get("height")):
                    problems.append("video size %s vs %sx%s" % (t["size"], s.get("width"), s.get("height")))
        n = sum(len(v) for v in mine.values())
        codecs = ",".join(re.search(r"codec=(\S+)", t).group(1) for t in tracks)
        check("oracle " + tag, not problems, ("samples=%d codecs=%s%s" % (n, codecs, "".join("; " + x for x in notes))) if not problems else "; ".join(problems)[:600])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--media", default=os.environ.get("MEDIA_ROOT", os.path.join(ROOT, "artifacts", "media")))
    ap.add_argument("--parser-dir")
    ap.add_argument("--ports", default=os.path.join(ROOT, "sources", "phoenix-rtos-ports"))
    ap.add_argument("--runs", type=int, default=10000, help="random split runs per stream (large video streams: a tenth)")
    ap.add_argument("--quick", action="store_true", help="the oracle and 100 random runs only")
    ap.add_argument("--work", default=None)
    args = ap.parse_args()

    work = args.work or tempfile.mkdtemp(prefix="fmp4-hosttest-")
    os.makedirs(work, exist_ok=True)
    parser_dir = args.parser_dir
    if not parser_dir:
        patches = sorted(glob.glob(os.path.join(args.ports, "webkit_wpe", "patches", "webkit-mse", "0032-*.patch")))
        if not patches:
            sys.exit("check-fmp4: no webkit-mse/0032 patch under %s (--parser-dir or --ports)" % args.ports)
        parser_dir = os.path.join(work, "parser")
        os.makedirs(parser_dir, exist_ok=True)
        extract_new_files(patches[-1], ["FFmpegFMP4Parser.h", "FFmpegFMP4Parser.cpp"], parser_dir)
        print("check-fmp4: parser from %s" % patches[-1])
    else:
        print("check-fmp4: parser from %s" % parser_dir)
    exe = build(parser_dir, work)
    print("check-fmp4: ffprobe %s, work %s" % (subprocess.run(["ffprobe", "-version"], capture_output=True, text=True).stdout.split()[2], work))

    sets = media_sets(args.media)
    if not sets:
        print("check-fmp4: no media under %s (tools/browser/media/gen-ladders.sh makes it)" % args.media)
    sets.update(make_tiny(work))

    for name, files in sets.items():
        oracle(exe, name, files, work)
        head = files[:4]
        size = sum(os.path.getsize(f) for f in head)
        if not args.quick:
            rc, out, err = run(exe, ["splits"] + (["--sparse"] if size > 400000 else []) + head)
            check("splits " + name, rc == 0, (out.strip() + " " + err.strip())[-300:])
            if len(files) >= 5:
                rc, out, err = run(exe, ["reset"] + files[:6])
                check("reset " + name, rc == 0, (out.strip() + " " + err.strip())[-300:])
        runs = 100 if args.quick else (args.runs if size <= 400000 else max(args.runs // 10, 100))
        rc, out, err = run(exe, ["random", str(runs), "1"] + head)
        check("random " + name, rc == 0, (out.strip() + " " + err.strip())[-300:])
        if name.startswith("tiny-") and not args.quick:
            rc, out, err = run(exe, ["mutate", str(args.runs), "7"] + files)
            check("mutate " + name, rc == 0, (out.strip() + " " + err.strip())[-300:])

    for a, b in (("mse/hevc-720", "mse/hevc-1080"), ("mse/h264-720", "mse/hevc-1080"), ("mse/aac", "mse/opus")):
        if a in sets and b in sets:
            rc, out, err = run(exe, ["switch"] + sets[a][:3] + ["--"] + sets[b][:3])
            check("switch %s -> %s" % (a, b), rc == 0, (out.strip() + " " + err.strip())[-300:])

    passed = sum(RESULTS)
    print("CHECK-SUMMARY %d/%d PASS" % (passed, len(RESULTS)))
    if not args.work:
        shutil.rmtree(work, ignore_errors=True)
    return 0 if passed == len(RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
