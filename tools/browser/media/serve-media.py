#!/usr/bin/env python3
"""serve-media.py -- the HTTP server of the streaming-video test set (docs/browser/MSE-DESIGN.md §9.1).

Serves the media root (gen-ladders.sh output + the pages stage.sh copies there) to the Pi from the
netboot host, with what the bench's serve.py lacks for video: byte ranges, the HLS/DASH MIME types,
CORS, a request log, and a live HLS stream simulated from any VOD ladder. Python standard library
only (plus mp4box.py beside it).

  serve-media.py [--root DIR] [--bind ADDR]... [--port N] [--window N] [--quiet]

  GET|HEAD /<path>     files under --root. "Range: bytes=a-b | a- | -n" -> 206 + Content-Range
                       (416 when unsatisfiable; several ranges -> the whole file, 200);
                       .m3u8 application/vnd.apple.mpegurl, .m4s/.mp4 video/mp4, .m4a audio/mp4,
                       .ts video/mp2t, .aac audio/aac, .key application/octet-stream,
                       .mpd application/dash+xml, ...; every response: CORS (any origin, Range
                       allowed, Content-Range/Content-Length exposed), Cache-Control: no-cache
  OPTIONS /<path>      the CORS preflight (a Range request from another origin needs one)
  GET /phx-ping        "phx-media-server ok root=<root>"
  GET /phx-log?l=<text>, POST /phx-log (text/plain, one line per line)
                       each line -> stdout as "MEDIA-PAGE <client> <text>": the pages' events reach
                       the host log even when the Pi's console says nothing
  GET /live/<ladder>/master.m3u8[?disc=N]     the ladder's master; its URIs carry ?disc=N on
  GET /live/<ladder>/<variant>/index.m3u8[?disc=N]
                       a LIVE media playlist over the VOD ladder's segments: a sliding window of
                       --window segments (default 6) by wall clock, #EXT-X-MEDIA-SEQUENCE
                       advancing, #EXT-X-PROGRAM-DATE-TIME per segment, no #EXT-X-ENDLIST.
                       The VOD content loops: fMP4 timestamps stay continuous across the loop
                       (each segment's tfdt is advanced, ?shift=<track>:<ticks> on its URI, done by
                       this server); MPEG-TS cannot be re-stamped here, so its loop point carries
                       #EXT-X-DISCONTINUITY (a real timestamp reset).
                       ?disc=N: every N segments the stream restarts at the ladder's first segment
                       with its original timestamps behind #EXT-X-DISCONTINUITY -- a real reset,
                       like a spliced-in programme or ad. Segment, init and key URIs are absolute
                       (/ladders/...). Byte-range ladders are not offered live.

  stdout: one line per request,
    MEDIA-SERVE <client> <method> <path> <status> range=<a-b|-> bytes=<n> ms=<n> [run=<the Referer's ?run=>]
                [ua=<first per client>]
  That log is the host-side witness for the gate: which variant's playlists and segments the Pi
  fetched (MSE-DESIGN §10.1 rows 3 and 12).

SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import datetime
import functools
import http.server
import json
import os
import posixpath
import re
import sys
import threading
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mp4box  # noqa: E402

VERSION = "phx-media-server 1"
MIME = {
    ".m3u8": "application/vnd.apple.mpegurl", ".m3u": "audio/mpegurl", ".m4s": "video/mp4", ".mp4": "video/mp4",
    ".m4v": "video/mp4", ".m4a": "audio/mp4", ".ts": "video/mp2t", ".aac": "audio/aac", ".mp3": "audio/mpeg",
    ".webm": "video/webm", ".mkv": "video/x-matroska", ".ogg": "audio/ogg", ".opus": "audio/ogg",
    ".key": "application/octet-stream", ".mpd": "application/dash+xml", ".vtt": "text/vtt; charset=utf-8",
    ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
    ".mjs": "text/javascript; charset=utf-8", ".json": "application/json", ".css": "text/css; charset=utf-8",
    ".txt": "text/plain; charset=utf-8", ".png": "image/png", ".jpg": "image/jpeg", ".svg": "image/svg+xml",
    ".map": "application/json", ".ico": "image/x-icon",
}
RANGE = re.compile(r"^bytes=(\d*)-(\d*)$")
SHIFT = re.compile(r"^\d+:\d+(,\d+:\d+)*$")
CHUNK = 256 * 1024


class Ladders:
    """the ladders' manifest.json files, re-read when they change"""

    def __init__(self, root):
        self.root = root
        self.cache = {}
        self.lock = threading.Lock()

    def get(self, name):
        path = os.path.join(self.root, "ladders", name, "manifest.json")
        try:
            mtime = os.path.getmtime(path)
        except OSError:
            return None
        with self.lock:
            hit = self.cache.get(name)
            if hit and hit[0] == mtime:
                return hit[1]
        with open(path, encoding="utf-8") as f:
            manifest = json.load(f)
        with self.lock:
            self.cache[name] = (mtime, manifest)
        return manifest


def live_playlist(manifest, ladder, uri, disc, window, t0, now):
    """the live window over one media playlist of a VOD ladder (see the module doc)"""
    pl = None
    for entry in manifest["variants"] + manifest["media"]:
        if entry["uri"] == uri:
            pl = entry["playlist"]
    if pl is None:
        return None
    if any("range" in s for s in pl["segments"]):
        raise LookupError("byte-range ladders are not offered live")
    segs = pl["segments"][:disc] if disc else pl["segments"]
    n = len(segs)
    starts = []
    acc = 0.0
    for s in segs:
        starts.append(acc)
        acc += s["duration"]
    loop = acc
    fmp4 = pl["type"] == "fmp4"
    # per track: the programme's length in its own timescale (continuous re-stamping)
    ticks = {}
    if fmp4 and not disc:
        for t in pl["tracks"]:
            ticks[t["id"]] = t["end"] - t["start"]
    wall = now - t0
    k = int(wall // loop)
    j = None
    while j is None and k >= 0:
        avail = [i for i in range(n) if k * loop + starts[i] + segs[i]["duration"] <= wall]
        if avail:
            j = avail[-1]
        else:
            k -= 1
    if j is None:
        raise LookupError("no live segment published yet")
    entries = []
    while len(entries) < window and k >= 0:
        entries.append((k, j))
        j -= 1
        if j < 0:
            k -= 1
            j = n - 1
    entries.reverse()
    resets = disc or not fmp4  # a programme start is a timestamp reset
    k0, j0 = entries[0]
    disc_seq = (k0 - (1 if j0 == 0 else 0)) if resets and k0 > 0 else 0
    base = posixpath.dirname("/ladders/%s/%s" % (ladder, uri))
    target = max(1, max(int(s["duration"] + 0.5) for s in segs))  # RFC 8216: rounded EXTINF <= target
    out = ["#EXTM3U", "#EXT-X-VERSION:%d" % (7 if fmp4 else 3), "#EXT-X-TARGETDURATION:%d" % target,
           "#EXT-X-MEDIA-SEQUENCE:%d" % (k0 * n + j0), "#EXT-X-DISCONTINUITY-SEQUENCE:%d" % disc_seq,
           "## phx live simulation: ladder=%s playlist=%s disc=%s loop=%.3fs window=%d" % (ladder, uri, disc or 0,
                                                                                          loop, window)]
    if pl.get("init"):
        out.append('#EXT-X-MAP:URI="%s"' % posixpath.join(base, pl["init"]["uri"]))
    for key in pl.get("keys", []):
        line = '#EXT-X-KEY:METHOD=%s,URI="%s"' % (key["method"], posixpath.normpath(posixpath.join(base, key["uri"])))
        out.append(line + (",IV=%s" % key["iv"] if key.get("iv") else ""))
    for k, j in entries:
        s = segs[j]
        if j == 0 and k > 0 and resets:
            out.append("#EXT-X-DISCONTINUITY")
        when = datetime.datetime.fromtimestamp(t0 + k * loop + starts[j], datetime.timezone.utc)
        out.append("#EXT-X-PROGRAM-DATE-TIME:" + when.isoformat(timespec="milliseconds").replace("+00:00", "Z"))
        out.append("#EXTINF:%.6f," % s["duration"])
        seg_uri = posixpath.join(base, s["uri"])
        if ticks and k > 0:
            seg_uri += "?shift=" + ",".join("%d:%d" % (t, k * d) for t, d in sorted(ticks.items()))
        out.append(seg_uri)
    return ("\n".join(out) + "\n").encode()


def live_master(manifest_text, query):
    """the VOD master with ?<query> on every playlist URI (relative URIs then resolve under /live/)"""
    if not query:
        return manifest_text
    out = []
    for line in manifest_text.split("\n"):
        if line.startswith("#EXT-X-MEDIA:") and 'URI="' in line:
            line = re.sub(r'URI="([^"]*)"', lambda m: 'URI="%s?%s"' % (m.group(1), query), line)
        elif line and not line.startswith("#"):
            line += "?" + query
        out.append(line)
    return "\n".join(out)


class Handler(http.server.SimpleHTTPRequestHandler):
    server_version = VERSION
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # replaced by log_request_line
        pass

    def log_request(self, code="-", size="-"):
        pass

    def log_line(self, status, nbytes, rng):
        client = self.client_address[0]
        ua = ""
        with self.server.lock:
            if client not in self.server.seen:
                self.server.seen.add(client)
                ua = " ua=" + (self.headers.get("User-Agent") or "-").replace(" ", "_")
        # the page's run id (its ?run=, from the Referer): which gate arm a request belongs to
        run = urllib.parse.parse_qs(urllib.parse.urlsplit(self.headers.get("Referer") or "").query).get("run")
        if not self.server.quiet:
            print("MEDIA-SERVE %s %s %s %d range=%s bytes=%d ms=%d%s%s" % (
                client, self.command, self.path, status, rng or "-", nbytes,
                (time.monotonic() - self.t_start) * 1000, " run=" + run[0][:64] if run else "", ua), flush=True)

    def cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Range, Content-Type")
        self.send_header("Access-Control-Expose-Headers", "Content-Length, Content-Range, Accept-Ranges")
        self.send_header("Access-Control-Max-Age", "600")

    def reply(self, status, body=b"", ctype="text/plain; charset=utf-8", extra=(), rng=None, head=False):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        for k, v in extra:
            self.send_header(k, v)
        self.cors()
        self.end_headers()
        if not head and body:
            self.wfile.write(body)
        self.log_line(status, 0 if head else len(body), rng)

    def parse_range(self, size):
        """None (whole), (start, end inclusive), or 'bad' (416)"""
        value = self.headers.get("Range")
        if not value:
            return None
        m = RANGE.match(value.strip().replace(" ", ""))
        if not m or (not m.group(1) and not m.group(2)):
            return None  # several ranges or another unit: the whole file (allowed by RFC 9110)
        if not m.group(1):
            n = int(m.group(2))
            return (max(0, size - n), size - 1) if n > 0 and size > 0 else "bad"
        start = int(m.group(1))
        end = int(m.group(2)) if m.group(2) else size - 1
        if start >= size or end < start:
            return "bad"
        return start, min(end, size - 1)

    def send_bytes(self, body, ctype, head):
        """an in-memory body (playlists, re-stamped segments), Range-aware"""
        r = self.parse_range(len(body))
        if r == "bad":
            return self.reply(416, extra=[("Content-Range", "bytes */%d" % len(body))], head=head,
                              rng=self.headers.get("Range"))
        if r is None:
            return self.reply(200, body, ctype, [("Accept-Ranges", "bytes")], head=head)
        a, b = r
        return self.reply(206, body[a:b + 1], ctype, [("Accept-Ranges", "bytes"),
                                                       ("Content-Range", "bytes %d-%d/%d" % (a, b, len(body)))],
                          rng="%d-%d" % (a, b), head=head)

    def send_file(self, path, head):
        size = os.path.getsize(path)
        ctype = MIME.get(os.path.splitext(path)[1].lower(), "application/octet-stream")
        r = self.parse_range(size)
        if r == "bad":
            return self.reply(416, extra=[("Content-Range", "bytes */%d" % size)], head=head,
                              rng=self.headers.get("Range"))
        a, b = (0, size - 1) if r is None else r
        length = b - a + 1 if size else 0
        self.send_response(200 if r is None else 206)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(length))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Last-Modified", self.date_time_string(int(os.path.getmtime(path))))
        if r is not None:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (a, b, size))
        self.cors()
        self.end_headers()
        sent = 0
        if not head:
            with open(path, "rb") as f:
                f.seek(a)
                left = length
                try:
                    while left > 0:
                        chunk = f.read(min(CHUNK, left))
                        if not chunk:
                            break
                        self.wfile.write(chunk)
                        sent += len(chunk)
                        left -= len(chunk)
                except (BrokenPipeError, ConnectionResetError):
                    self.close_connection = True
        self.log_line(200 if r is None else 206, sent, None if r is None else "%d-%d" % (a, b))

    def do_OPTIONS(self):
        self.t_start = time.monotonic()
        self.reply(204)

    def do_HEAD(self):
        self.do_GET(head=True)

    def do_POST(self):
        self.t_start = time.monotonic()
        url = urllib.parse.urlsplit(self.path)
        if url.path != "/phx-log":
            return self.reply(404, b"not found\n")
        length = int(self.headers.get("Content-Length") or 0)
        if length > 1024 * 1024:
            return self.reply(413)
        text = self.rfile.read(length).decode("utf-8", "replace")
        for line in text.splitlines():
            if line.strip():
                print("MEDIA-PAGE %s %s" % (self.client_address[0], line.strip()[:2000]), flush=True)
        self.reply(204)

    def do_GET(self, head=False):
        self.t_start = time.monotonic()
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        path = urllib.parse.unquote(url.path)
        if path == "/phx-ping":
            return self.reply(200, ("%s ok root=%s\n" % (VERSION, self.server.root)).encode(), head=head)
        if path == "/phx-log":
            for line in query.get("l", []):
                print("MEDIA-PAGE %s %s" % (self.client_address[0], line.strip()[:2000]), flush=True)
            return self.reply(204, head=head)
        if path.startswith("/live/"):
            return self.live(path[len("/live/"):], query, url.query, head)
        fs = self.translate_path(path)
        if os.path.isdir(fs):
            index = os.path.join(fs, "index.html")
            if not path.endswith("/"):
                return self.reply(301, extra=[("Location", path + "/")], head=head)
            if os.path.isfile(index):
                return self.send_file(index, head)
            body = self.listing(fs, path)
            return self.reply(200, body, "text/html; charset=utf-8", head=head)
        if not os.path.isfile(fs):
            return self.reply(404, b"not found\n", head=head)
        shift = query.get("shift", [None])[0]
        if shift:
            if not SHIFT.match(shift) or not fs.endswith((".m4s", ".mp4")):
                return self.reply(400, b"bad shift\n", head=head)
            with open(fs, "rb") as f:
                data = f.read()
            try:
                data = mp4box.shift_tfdt(data, {int(t): int(v) for t, v in (p.split(":") for p in shift.split(","))})
            except ValueError as e:
                return self.reply(500, ("shift failed: %s\n" % e).encode(), head=head)
            return self.send_bytes(data, MIME.get(os.path.splitext(fs)[1], "video/mp4"), head)
        return self.send_file(fs, head)

    def live(self, rest, query, raw_query, head):
        parts = rest.split("/", 1)
        if len(parts) != 2 or not parts[0] or ".." in rest:
            return self.reply(404, b"not found\n", head=head)
        ladder, uri = parts
        manifest = self.server.ladders.get(ladder)
        if manifest is None:
            return self.reply(404, ("no ladder %s\n" % ladder).encode(), head=head)
        disc = query.get("disc", [None])[0]
        try:
            disc = int(disc) if disc else 0
        except ValueError:
            return self.reply(400, b"bad disc\n", head=head)
        keep = urllib.parse.urlencode({"disc": disc}) if disc else ""
        if uri == manifest["master"]:
            with open(os.path.join(self.server.root, "ladders", ladder, uri), encoding="utf-8") as f:
                text = f.read()
            return self.send_bytes(live_master(text, keep).encode(), MIME[".m3u8"], head)
        try:
            body = live_playlist(manifest, ladder, uri, disc, self.server.window, self.server.t0, time.time())
        except LookupError as e:
            return self.reply(404, ("%s\n" % e).encode(), head=head)
        if body is None:
            return self.reply(404, ("no playlist %s in %s\n" % (uri, ladder)).encode(), head=head)
        return self.send_bytes(body, MIME[".m3u8"], head)

    def listing(self, fs, path):
        names = sorted(os.listdir(fs))
        rows = "".join('<li><a href="%s%s">%s%s</a></li>' % (
            urllib.parse.quote(n), "/" if os.path.isdir(os.path.join(fs, n)) else "",
            n, "/" if os.path.isdir(os.path.join(fs, n)) else "") for n in names)
        return ("<!DOCTYPE html><meta charset=utf-8><title>%s</title><h1>%s</h1><ul>%s</ul>" % (
            path, path, rows)).encode()


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(os.path.dirname(os.path.dirname(here)))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.environ.get("MEDIA_ROOT", os.path.join(repo, "artifacts", "media")),
                    help="the media root (default $MEDIA_ROOT or artifacts/media)")
    ap.add_argument("--bind", action="append", help="address to listen on (repeatable; default 127.0.0.1)")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--window", type=int, default=6, help="live playlist window, segments (default 6)")
    ap.add_argument("--quiet", action="store_true", help="no per-request lines (MEDIA-PAGE lines stay)")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        sys.exit("serve-media: no media root %s (tools/browser/media/gen-ladders.sh makes it)" % root)
    ladders = Ladders(root)
    # the live clock starts a full window in the past, so the first live playlist is complete
    t0 = time.time() - args.window * 2.0
    servers = []
    for address in args.bind or ["127.0.0.1"]:
        s = Server((address, args.port), functools.partial(Handler, directory=root))
        s.root, s.ladders, s.window, s.t0, s.quiet = root, ladders, args.window, t0, args.quiet
        s.lock, s.seen = threading.Lock(), set()
        servers.append(s)
        print("MEDIA-SERVE listening http://%s:%d/ root=%s window=%d" % (address, s.server_address[1], root,
                                                                         args.window), flush=True)
    for s in servers[1:]:
        threading.Thread(target=s.serve_forever, daemon=True).start()
    try:
        servers[0].serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
