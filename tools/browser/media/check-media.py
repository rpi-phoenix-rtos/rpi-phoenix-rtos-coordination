#!/usr/bin/env python3
"""check-media.py -- the host harness of the streaming-video test set (docs/browser/MSE-DESIGN.md §9.4):
no Pi, no browser. It proves the server and the media before a Pi cycle spends time on them.

  check-media.py [--root DIR] [--only server,ladders,live,decode] [--keep-log]
      starts serve-media.py on 127.0.0.1 (a free port) and checks
      server   /phx-ping; MIME type per extension; Range: a-b, a-, -n (bytes equal the file's
               slice), 416, several ranges -> 200; HEAD; CORS headers and the OPTIONS preflight;
               /phx-log (GET and POST) -> MEDIA-PAGE lines in the server's output; 404
      ladders  every ladder's manifest.json sha256/sizes against the files; every URI of every
               playlist (master -> media playlists -> init, segments, keys) answers over HTTP
               (byte ranges: 206 and the right length); CODECS + FRAME-RATE on every video
               STREAM-INF; "expect" names an existing variant; byte ranges tile the single file;
               the AES key decrypts a segment (openssl) to MPEG-TS sync bytes; fMP4 fragments
               continuous (tfdt[i+1] = tfdt[i] + duration[i]) per playlist; the MSE sets likewise
      live     the sliding window at simulated clock values (window size, MEDIA-SEQUENCE and
               DISCONTINUITY-SEQUENCE, no ENDLIST, the loop point: fMP4 re-stamped with ?shift=
               whose fetched tfdt continues the previous segment exactly; MPEG-TS and ?disc=N:
               #EXT-X-DISCONTINUITY), then over HTTP: two fetches 2.5 s apart advance the sequence
      decode   the host ffmpeg's own HLS demuxer reads the ladders over HTTP (Range, EXT-X-MAP,
               EXT-X-BYTERANGE, AES-128 through its crypto protocol, live): every variant decodes
               to the rendition's frames -- frame md5s identical to renditions/<name>.mp4 for
               fMP4, TS, byte-range, AES and the MSE segments; 30 fps x secs frames each; live
               reads 10 s without an error
  check-media.py requests LOG [--run RUN]
      the host-side witness for the Pi gate: per page run (the pages' "start ... r=<run>" lines in
      serve.log) which playlists, init sections, segments and keys were fetched, per variant
      directory, split at the page's play-request line (phase=start|played; MSE-DESIGN §10.1 row 3:
      only the chosen variant; row 12: no segment in phase=start of the memory arm).

Prints "CHECK <area> <name> PASS|FAIL <detail>" per check and "CHECK-SUMMARY pass=<n> fail=<n>";
exit status 1 on any FAIL.

SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import collections
import hashlib
import http.client
import importlib.util
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mp4box  # noqa: E402

RESULTS = {"pass": 0, "fail": 0}


def check(area, name, ok, detail=""):
    RESULTS["pass" if ok else "fail"] += 1
    print("CHECK %s %s %s %s" % (area, name, "PASS" if ok else "FAIL", detail), flush=True)
    return ok


def load_server_module():
    spec = importlib.util.spec_from_file_location("serve_media", os.path.join(HERE, "serve-media.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Client:
    def __init__(self, port):
        self.port = port

    def get(self, path, headers=None, method="GET", body=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
        conn.request(method, path, body=body, headers=headers or {})
        r = conn.getresponse()
        data = r.read()
        conn.close()
        return r.status, {k.lower(): v for k, v in r.getheaders()}, data


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start_server(root, log_path):
    port = free_port()
    log = open(log_path, "w")
    proc = subprocess.Popen([sys.executable, os.path.join(HERE, "serve-media.py"), "--root", root, "--port", str(port)],
                            stdout=log, stderr=subprocess.STDOUT)
    c = Client(port)
    for _ in range(100):
        try:
            if c.get("/phx-ping")[0] == 200:
                return proc, c
        except OSError:
            time.sleep(0.05)
    proc.kill()
    sys.exit("check-media: the server did not start (log %s)" % log_path)


def ladders(root):
    d = os.path.join(root, "ladders")
    return sorted(n for n in os.listdir(d) if os.path.isfile(os.path.join(d, n, "manifest.json"))) if os.path.isdir(d) else []


def manifest(root, ladder):
    with open(os.path.join(root, "ladders", ladder, "manifest.json"), encoding="utf-8") as f:
        return json.load(f)


# --- server -----------------------------------------------------------------------------------------
def check_server(root, c, log_path):
    A = "server"
    st, h, body = c.get("/phx-ping")
    check(A, "ping", st == 200 and b"phx-media-server" in body, body.decode(errors="replace").strip())
    lad = ladders(root)
    if not lad:
        check(A, "ladders-present", False, "no ladders under %s/ladders (gen-ladders.sh)" % root)
        return
    m = manifest(root, "hevc-fmp4" if "hevc-fmp4" in lad else lad[0])
    lname = m["ladder"]
    v0 = m["variants"][0]
    seg_rel = os.path.join(os.path.dirname(v0["uri"]), v0["playlist"]["segments"][1]["uri"])
    seg_url = "/ladders/%s/%s" % (lname, seg_rel)
    with open(os.path.join(root, "ladders", lname, seg_rel), "rb") as f:
        seg = f.read()
    want = {"/ladders/%s/master.m3u8" % lname: "application/vnd.apple.mpegurl", seg_url: "video/mp4",
            "/ladders/%s/manifest.json" % lname: "application/json"}
    if "hevc-ts" in lad:
        mt = manifest(root, "hevc-ts")
        want["/ladders/hevc-ts/%s/%s" % (os.path.dirname(mt["variants"][0]["uri"]),
                                         mt["variants"][0]["playlist"]["segments"][0]["uri"])] = "video/mp2t"
    if "aes" in lad:
        want["/ladders/aes/aes.key"] = "application/octet-stream"
    if os.path.isfile(os.path.join(root, "mse", "hevc-1080", "manifest.mpd")):
        want["/mse/hevc-1080/manifest.mpd"] = "application/dash+xml"
        want["/mse/hevc-1080/init.mp4"] = "video/mp4"
    if os.path.isfile(os.path.join(root, "pages", "b8-hls.html")):
        want["/pages/b8-hls.html"] = "text/html; charset=utf-8"
        want["/pages/media-common.js"] = "text/javascript; charset=utf-8"
    for path, mime in want.items():
        st, h, _ = c.get(path, method="HEAD")
        check(A, "mime " + os.path.splitext(path)[1], st == 200 and h.get("content-type") == mime,
              "%s -> %s %s" % (path, st, h.get("content-type")))
    n = len(seg)
    for rng, a, b in (("bytes=0-99", 0, 99), ("bytes=100-", 100, n - 1), ("bytes=-500", n - 500, n - 1),
                      ("bytes=%d-%d" % (n - 10, n + 1000), n - 10, n - 1)):
        st, h, body = c.get(seg_url, {"Range": rng})
        check(A, "range " + rng.split("=")[1].replace(str(n - 10), "end-10").replace(str(n + 1000), "past-end"),
              st == 206 and body == seg[a:b + 1] and h.get("content-range") == "bytes %d-%d/%d" % (a, b, n)
              and h.get("content-length") == str(b - a + 1), "%d %s len=%d" % (st, h.get("content-range"), len(body)))
    st, h, body = c.get(seg_url, {"Range": "bytes=%d-" % (n + 5)})
    check(A, "range unsatisfiable", st == 416 and h.get("content-range") == "bytes */%d" % n, "%d %s" % (st, h.get("content-range")))
    st, h, body = c.get(seg_url, {"Range": "bytes=0-9,20-29"})
    check(A, "range several->whole", st == 200 and body == seg, "%d len=%d" % (st, len(body)))
    st, h, body = c.get(seg_url, method="HEAD")
    check(A, "head", st == 200 and body == b"" and h.get("content-length") == str(n) and h.get("accept-ranges") == "bytes",
          "%d len=%s" % (st, h.get("content-length")))
    st, h, _ = c.get(seg_url, {"Origin": "http://elsewhere.example"})
    check(A, "cors get", h.get("access-control-allow-origin") == "*" and "Content-Range" in h.get("access-control-expose-headers", ""),
          "%s / %s" % (h.get("access-control-allow-origin"), h.get("access-control-expose-headers")))
    st, h, _ = c.get(seg_url, {"Origin": "http://elsewhere.example", "Access-Control-Request-Method": "GET",
                               "Access-Control-Request-Headers": "range"}, method="OPTIONS")
    check(A, "cors preflight", st == 204 and "Range" in h.get("access-control-allow-headers", "")
          and "GET" in h.get("access-control-allow-methods", ""), "%d %s" % (st, h.get("access-control-allow-headers")))
    st, _, _ = c.get("/ladders/no-such-ladder/master.m3u8")
    check(A, "404", st == 404, str(st))
    st, _, _ = c.get("/ladders/../../etc/passwd")
    check(A, "no traversal", st == 404, str(st))
    token = "probe-%d" % os.getpid()
    st1, _, _ = c.get("/phx-log?l=" + urllib.parse.quote("CHECK-LOG %s get" % token))
    st2, _, _ = c.get("/phx-log", {"Content-Type": "text/plain"}, method="POST",
                      body=("CHECK-LOG %s post1\nCHECK-LOG %s post2\n" % (token, token)).encode())
    time.sleep(0.3)
    with open(log_path, encoding="utf-8", errors="replace") as f:
        logged = [x for x in f if x.startswith("MEDIA-PAGE ") and token in x]
    check(A, "phx-log", st1 == 204 and st2 == 204 and len(logged) == 3, "%d %d lines=%d" % (st1, st2, len(logged)))
    with open(log_path, encoding="utf-8", errors="replace") as f:
        reqs = [x for x in f if x.startswith("MEDIA-SERVE 127.0.0.1 GET %s 206 range=0-99 " % seg_url)]
    check(A, "request log", len(reqs) == 1, reqs[0].strip() if reqs else "no line for the 0-99 range request")


# --- ladders -----------------------------------------------------------------------------------------
def fetch_ok(c, path, rng=None):
    headers = {"Range": "bytes=%d-%d" % (rng[0], rng[0] + rng[1] - 1)} if rng else {}
    st, h, body = c.get(path, headers)
    if rng:
        return st == 206 and len(body) == rng[1], st, body
    return st == 200, st, body


def check_playlist_uris(c, ladder, base_uri, pl, area):
    """every URI of one media playlist over HTTP"""
    base = "/ladders/%s/%s" % (ladder, os.path.dirname(base_uri))
    bad = []
    n = 0
    if pl.get("init"):
        ok, st, _ = fetch_ok(c, "%s/%s" % (base, pl["init"]["uri"]), pl["init"].get("range"))
        n += 1
        if not ok:
            bad.append("init %d" % st)
    for k in pl.get("keys", []):
        ok, st, body = fetch_ok(c, os.path.normpath("%s/%s" % (base, k["uri"])))
        n += 1
        if not ok or len(body) != 16:
            bad.append("key %d len=%d" % (st, len(body)))
    for s in pl["segments"]:
        ok, st, _ = fetch_ok(c, "%s/%s" % (base, s["uri"]), s.get("range"))
        n += 1
        if not ok:
            bad.append("%s %d" % (s["uri"], st))
    check(area, "uris %s" % base_uri, not bad, "%d fetched%s" % (n, (" bad: " + ", ".join(bad[:5])) if bad else ""))


def check_continuity(area, name, pl):
    bad = []
    prev = {}
    for i, s in enumerate(pl["segments"]):
        for fr in s.get("fragments", []):
            if fr["track"] in prev and prev[fr["track"]] != fr["tfdt"]:
                bad.append("seg %d track %d tfdt %d != %d" % (i, fr["track"], fr["tfdt"], prev[fr["track"]]))
            prev[fr["track"]] = fr["tfdt"] + fr["duration"]
    check(area, "continuity %s" % name, not bad, "; ".join(bad[:3]) or "%d segments" % len(pl["segments"]))


def check_ladders(root, c):
    A = "ladders"
    for name in ladders(root):
        m = manifest(root, name)
        d = os.path.join(root, "ladders", name)
        bad = []
        for rel, info in m["files"].items():
            p = os.path.join(d, rel)
            if not os.path.isfile(p) or os.path.getsize(p) != info["bytes"]:
                bad.append(rel + " size")
                continue
            h = hashlib.sha256()
            with open(p, "rb") as f:
                h.update(f.read())
            if h.hexdigest() != info["sha256"]:
                bad.append(rel + " sha256")
        check(A, "%s files" % name, not bad, "%d files%s" % (len(m["files"]), (" bad: " + ", ".join(bad[:4])) if bad else ""))
        ok, st, master = fetch_ok(c, "/ladders/%s/master.m3u8" % name)
        infs = [x for x in master.decode().splitlines() if x.startswith("#EXT-X-STREAM-INF:")]
        missing = [x for x in infs if "CODECS=" not in x or ("RESOLUTION=" in x and "FRAME-RATE=" not in x)]
        check(A, "%s master" % name, ok and len(infs) == len(m["variants"]) and not missing,
              "%d variants: %s" % (len(infs), " ".join("%s=%s" % (v["name"], v["codecs"]) for v in m["variants"])))
        e = m.get("expect", {})
        check(A, "%s expect" % name, e.get("index") is not None and m["variants"][e["index"]]["name"] == e.get("variant"),
              "index %s = %s rule=%s" % (e.get("index"), e.get("variant"), e.get("rule")))
        for entry in m["variants"] + m["media"]:
            pl = entry["playlist"]
            check_playlist_uris(c, name, entry["uri"], pl, A)
            if pl["type"] == "fmp4":
                check_continuity(A, "%s/%s" % (name, entry["uri"]), pl)
            ranges = [s["range"] for s in pl["segments"] if "range" in s]
            if ranges:
                spans = ([pl["init"]["range"]] if pl.get("init", {}).get("range") else []) + ranges
                spans.sort()
                f = os.path.join(d, os.path.dirname(entry["uri"]), pl["segments"][0]["uri"])
                tiled = all(spans[i][0] + spans[i][1] == spans[i + 1][0] for i in range(len(spans) - 1))
                check(A, "%s byte ranges tile the file" % name, spans[0][0] == 0 and tiled and
                      spans[-1][0] + spans[-1][1] == os.path.getsize(f), "%d ranges over %d bytes" % (len(spans), os.path.getsize(f)))
            if pl.get("keys"):
                key = pl["keys"][0]
                with open(os.path.normpath(os.path.join(d, os.path.dirname(entry["uri"]), key["uri"])), "rb") as f:
                    k = f.read().hex()
                seg = os.path.join(d, os.path.dirname(entry["uri"]), pl["segments"][0]["uri"])
                r = subprocess.run(["openssl", "enc", "-d", "-aes-128-cbc", "-K", k, "-iv", key["iv"][2:], "-in", seg],
                                   capture_output=True)
                clear = r.stdout
                check(A, "%s aes-128 decrypts" % name, r.returncode == 0 and len(clear) >= 376 and clear[0] == 0x47
                      and clear[188] == 0x47, "rc=%d %d bytes, sync %s" % (r.returncode, len(clear), clear[:1].hex()))
    mse = os.path.join(root, "mse", "manifest.json")
    if os.path.isfile(mse):
        with open(mse, encoding="utf-8") as f:
            mm = json.load(f)
        for rep in mm["representations"]:
            bad = []
            for i in range(len(rep["segments"]) - 1):
                a, b = rep["segments"][i], rep["segments"][i + 1]
                if a["tfdt"] + a["ticks"] != b["tfdt"]:
                    bad.append("seg %d" % i)
            ok, st, _ = fetch_ok(c, "/mse/%s/%s" % (rep["id"], rep["init"]))
            oks = [fetch_ok(c, "/mse/%s/%s" % (rep["id"], s["uri"]))[0] for s in rep["segments"]]
            check(A, "mse %s" % rep["id"], ok and all(oks) and not bad,
                  "%s, %d segments, %.3f s%s" % (rep["mime"], len(oks), rep["duration"], (" gaps " + ",".join(bad)) if bad else ""))


# --- live --------------------------------------------------------------------------------------------
def parse_live(text):
    lines = text.decode().splitlines()
    out = {"seq": None, "dseq": None, "segs": [], "endlist": "#EXT-X-ENDLIST" in lines, "map": None}
    disc = False
    for x in lines:
        if x.startswith("#EXT-X-MEDIA-SEQUENCE:"):
            out["seq"] = int(x.split(":")[1])
        elif x.startswith("#EXT-X-DISCONTINUITY-SEQUENCE:"):
            out["dseq"] = int(x.split(":")[1])
        elif x.startswith("#EXT-X-MAP:"):
            out["map"] = re.search(r'URI="([^"]+)"', x).group(1)
        elif x == "#EXT-X-DISCONTINUITY":
            disc = True
        elif x and not x.startswith("#"):
            out["segs"].append({"uri": x, "disc": disc})
            disc = False
    return out


def check_live(root, c, srv):
    A = "live"
    names = ladders(root)
    t0 = 1_000_000.0
    for name in [n for n in ("hevc-fmp4", "h264-only", "hevc-ts") if n in names]:
        m = manifest(root, name)
        for entry in [m["variants"][m["expect"]["index"]]] + m["media"]:
            pl = entry["playlist"]
            loop = sum(s["duration"] for s in pl["segments"])
            fmp4 = pl["type"] == "fmp4"
            label = "%s/%s" % (name, entry["uri"])
            # at the start, one segment later, and across the loop point
            a = parse_live(srv.live_playlist(m, name, entry["uri"], 0, 6, t0, t0 + 13.0))
            b = parse_live(srv.live_playlist(m, name, entry["uri"], 0, 6, t0, t0 + 15.1))
            check(A, "%s window" % label, len(a["segs"]) == 6 and not a["endlist"] and b["seq"] == a["seq"] + 1,
                  "seq %s -> %s, %d segments" % (a["seq"], b["seq"], len(a["segs"])))
            w = parse_live(srv.live_playlist(m, name, entry["uri"], 0, 6, t0, t0 + loop + 5.0))
            wrap = [i for i, s in enumerate(w["segs"]) if s["uri"].split("?")[0].endswith(pl["segments"][0]["uri"])]
            if not wrap or wrap[-1] == 0:
                check(A, "%s loop" % label, False, "no loop point in the window: %s" % [s["uri"] for s in w["segs"]])
                continue
            i = wrap[-1]
            if fmp4:
                shifted = "?shift=" in w["segs"][i]["uri"] and not any(s["disc"] for s in w["segs"])
                st1, _, last = c.get(w["segs"][i - 1]["uri"])
                st2, _, first = c.get(w["segs"][i]["uri"])
                init = mp4box.tracks(c.get(w["map"])[2])
                f1 = mp4box.fragments(last, init)
                f2 = mp4box.fragments(first, init)
                cont = st1 == 200 and st2 == 200 and all(
                    x["tfdt"] + x["duration"] == y["tfdt"] for x, y in zip(f1[-len(f2):], f2) if x["track"] == y["track"])
                check(A, "%s loop continuous" % label, shifted and cont,
                      "%s: tfdt %s -> %s" % (w["segs"][i]["uri"].split("/")[-1], [x["tfdt"] + x["duration"] for x in f1],
                                             [y["tfdt"] for y in f2]))
            else:
                check(A, "%s loop discontinuity" % label, w["segs"][i]["disc"] and w["dseq"] == 0 and "?shift" not in w["segs"][i]["uri"],
                      "DISCONTINUITY before %s, dseq=%s" % (w["segs"][i]["uri"].split("/")[-1], w["dseq"]))
            d = parse_live(srv.live_playlist(m, name, entry["uri"], 5, 6, t0, t0 + 3 * 5 * pl["segments"][0]["duration"] + 3.0))
            restarts = [s for s in d["segs"] if s["disc"]]
            check(A, "%s disc=5" % label, len(restarts) >= 1 and all(s["uri"].endswith(pl["segments"][0]["uri"]) for s in restarts)
                  and not any("?shift" in s["uri"] for s in d["segs"]) and d["dseq"] >= 1,
                  "%d restarts in the window, dseq=%s" % (len(restarts), d["dseq"]))
    if "hevc-fmp4" in names:
        m = manifest(root, "hevc-fmp4")
        uri = m["variants"][m["expect"]["index"]]["uri"]
        st, _, master = c.get("/live/hevc-fmp4/master.m3u8?disc=10")
        check(A, "http master", st == 200 and b"index.m3u8?disc=10" in master, "%d" % st)
        st1, _, one = c.get("/live/hevc-fmp4/" + uri)
        time.sleep(2.5)
        st2, _, two = c.get("/live/hevc-fmp4/" + uri)
        a, b = parse_live(one), parse_live(two)
        ok = st1 == st2 == 200 and b["seq"] > a["seq"] and not b["endlist"]
        segs_ok = all(c.get(s["uri"], method="HEAD")[0] == 200 for s in b["segs"]) and c.get(b["map"], method="HEAD")[0] == 200
        check(A, "http window advances", ok and segs_ok, "seq %s -> %s, uris %s" % (a["seq"], b["seq"], "ok" if segs_ok else "BAD"))
        st, _, _ = c.get("/live/byterange/" + manifest(root, "byterange")["variants"][0]["uri"]) if "byterange" in names else (404, 0, 0)
        check(A, "byterange refused", st == 404, str(st))


# --- decode (the host ffmpeg's hls demuxer over HTTP) ------------------------------------------------
def frame_md5s(url, stream="v:0", extra=()):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostdin", "-v", "error", *extra, "-i", url, "-map", "0:" + stream,
                        "-f", "framemd5", "-"], capture_output=True, text=True)
    md5s = [x.split(",")[-1].strip() for x in r.stdout.splitlines() if x and not x.startswith("#")]
    return r.returncode, md5s, r.stderr.strip().splitlines()[:3]


def check_decode(root, c, secs):
    A = "decode"
    base = "http://127.0.0.1:%d" % c.port
    names = ladders(root)
    frames = int(round(secs * 30))
    ref = {}

    def reference(rend):
        if rend not in ref:
            rc, md5s, err = frame_md5s(os.path.join(root, "renditions", rend + ".mp4"))
            ref[rend] = md5s if rc == 0 else None
        return ref[rend]

    hls_opts = ("-allowed_extensions", "ALL", "-allowed_segment_extensions", "ALL")
    for name in names:
        m = manifest(root, name)
        for v in m["variants"]:
            if not v["resolution"]:
                continue
            want = reference(v["name"])
            rc, md5s, err = frame_md5s("%s/ladders/%s/%s" % (base, name, v["uri"]), extra=hls_opts)
            check(A, "%s/%s" % (name, v["name"]), rc == 0 and len(md5s) == frames and md5s == want,
                  "rc=%d %d frames (want %d)%s %s" % (rc, len(md5s), frames, "" if md5s == want else " md5 DIFFERS from the rendition",
                                                      " | ".join(err)))
        rc, md5s, err = frame_md5s("%s/ladders/%s/master.m3u8" % (base, name), "a:0", hls_opts)
        check(A, "%s audio" % name, rc == 0 and len(md5s) > secs * 46, "rc=%d %d audio frames %s" % (rc, len(md5s), " | ".join(err)))
    mse = os.path.join(root, "mse", "manifest.json")
    if os.path.isfile(mse):
        with open(mse, encoding="utf-8") as f:
            mm = json.load(f)
        with tempfile.TemporaryDirectory() as tmp:
            for rep in mm["representations"]:
                cat = os.path.join(tmp, rep["id"] + ".mp4")
                with open(cat, "wb") as out:
                    for part in [rep["init"]] + [s["uri"] for s in rep["segments"]]:
                        out.write(c.get("/mse/%s/%s" % (rep["id"], part))[2])
                stream = "v:0" if rep["kind"] == "video" else "a:0"
                rc, md5s, err = frame_md5s(cat, stream)
                if rep["kind"] == "video":
                    want = reference(rep["id"])
                    check(A, "mse %s" % rep["id"], rc == 0 and md5s == want, "rc=%d %d frames %s" % (rc, len(md5s), " | ".join(err)))
                else:
                    rc2, want, _ = frame_md5s(os.path.join(root, "renditions", rep["id"] + ".mp4"), "a:0")
                    check(A, "mse %s" % rep["id"], rc == 0 and md5s == want, "rc=%d %d frames" % (rc, len(md5s)))
    if "hevc-fmp4" in names:
        rc, md5s, err = frame_md5s("%s/live/hevc-fmp4/master.m3u8" % base, "v:0",
                                   hls_opts + ("-live_start_index", "0", "-t", "10"))
        check(A, "live hevc-fmp4", rc == 0 and len(md5s) >= 250, "rc=%d %d frames in 10 s %s" % (rc, len(md5s), " | ".join(err)))
        # ?disc=3: the picture timestamps must really restart every 3 segments (6 s); counted in
        # packets (a time interval never ends when the timestamps keep going back)
        r = subprocess.run(["ffprobe", "-v", "error", "-live_start_index", "0", "-read_intervals", "%+#330",
                            "-select_streams", "v:0", "-show_entries", "packet=pts_time", "-of", "csv=p=0",
                            "%s/live/hevc-fmp4/master.m3u8?disc=3" % base], capture_output=True, text=True, timeout=120)
        pts = [float(x) for x in r.stdout.split() if x.strip()]
        resets = [(pts[i - 1], pts[i]) for i in range(1, len(pts)) if pts[i] < pts[i - 1] - 1]
        check(A, "live hevc-fmp4?disc=3 resets", r.returncode == 0 and len(pts) >= 300 and len(resets) >= 1
              and all(abs(a - 6.0) < 0.1 and b < 0.2 for a, b in resets),
              "%d packets, resets %s" % (len(pts), " ".join("%.3f->%.3f" % ab for ab in resets)))


# --- requests (the Pi gate's host-side witness) ------------------------------------------------------
def requests(log, only_run):
    run = "-"
    phase = "-"
    per = collections.OrderedDict()
    start = re.compile(r"^MEDIA-PAGE \S+ (B8\w+) t=\d+ r=(\S+) start\b")
    played = re.compile(r"^MEDIA-PAGE \S+ B8\w+ t=\d+ r=(\S+) play-request\b")
    req = re.compile(r"^MEDIA-SERVE (\S+) (GET|HEAD) (\S+) (\d+) range=(\S+) bytes=(\d+) ms=\d+(?: run=(\S+))?")
    with open(log, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = start.match(line)
            if m:
                run, phase = m.group(2), "start"
                continue
            m = played.match(line)
            if m and m.group(1) == run:
                phase = "played"
                continue
            m = req.match(line)
            if not m or m.group(3).startswith(("/phx-", "/pages/", "/vendor/", "/favicon")):
                continue
            path = urllib.parse.urlsplit(m.group(3)).path
            kind = ("playlist" if path.endswith(".m3u8") else "key" if path.endswith(".key") else
                    "init" if re.search(r"init[^/]*\.mp4$", path) else "manifest" if path.endswith(".json") else "segment")
            # the Referer's run id when the request had one; else the last page start seen
            r = m.group(7) or run
            ph = phase if r == run else "start"  # its start beacon has not arrived yet
            d = per.setdefault((r, ph), collections.OrderedDict()).setdefault(os.path.dirname(path), collections.Counter())
            d[kind] += 1
            d["bytes"] += int(m.group(6))
    for (r, ph), dirs in per.items():
        if only_run and r != only_run:
            continue
        for d, cnt in dirs.items():
            print("REQUESTS run=%s phase=%s dir=%s %s" % (r, ph, d, " ".join("%s=%d" % kv for kv in cnt.items())))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("what", nargs="?", default="all", choices=["all", "requests"])
    ap.add_argument("log", nargs="?")
    ap.add_argument("--run")
    ap.add_argument("--root", default=os.environ.get("MEDIA_ROOT", os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))),
                                                                                 "artifacts", "media")))
    ap.add_argument("--only", default="server,ladders,live,decode")
    ap.add_argument("--keep-log", action="store_true")
    args = ap.parse_args()
    if args.what == "requests":
        if not args.log:
            sys.exit("usage: check-media.py requests LOG [--run RUN]")
        requests(args.log, args.run)
        return
    root = os.path.abspath(args.root)
    areas = args.only.split(",")
    log_path = os.path.join(tempfile.gettempdir(), "check-media-serve-%d.log" % os.getpid())
    proc, c = start_server(root, log_path)
    try:
        secs = 60
        if ladders(root):
            secs = manifest(root, ladders(root)[0])["secs"]
        if "server" in areas:
            check_server(root, c, log_path)
        if "ladders" in areas:
            check_ladders(root, c)
        if "live" in areas:
            check_live(root, c, load_server_module())
        if "decode" in areas:
            check_decode(root, c, secs)
    finally:
        proc.terminate()
        proc.wait()
    print("CHECK-SUMMARY pass=%d fail=%d root=%s%s" % (RESULTS["pass"], RESULTS["fail"], root,
                                                       (" serve-log=" + log_path) if args.keep_log else ""))
    if not args.keep_log:
        os.unlink(log_path)
    sys.exit(1 if RESULTS["fail"] else 0)


if __name__ == "__main__":
    main()
