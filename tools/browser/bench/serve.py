#!/usr/bin/env python3
"""serve.py -- the HTTP server of the Phoenix-RTOS browser benchmark suite.

The benchmarks cannot run from file:// (each file:// document is its own origin: Speedometer and
MotionMark drive their test frames through contentWindow, JetStream fetch()es its sources, Acid3
depends on status codes and MIME types). This serves the staged suite over HTTP, either from the
netboot host (the default: the Pi's CPU stays with the browser) or on the Pi itself (bench.sh
server=pi: /bin/python3 on 127.0.0.1). Python standard library only, the same file on both.

  serve.py [--root DIR] [--results DIR] [--bind ADDR]... [--port N] [--verbose]

  GET  /<path>                  static files under --root; a directory's .phx-headers.json maps
                                file names to {"status": N, "type": "..."} (Acid3's deliberate
                                404 and wrong MIME types)
  GET  /phx-ping                "phx-bench-server ok" (bench.sh checks the server with it)
  POST /phx-report?bench=&run=&part=   the hooks' full JSON results
  POST /report                  JetStream's own ?report=true results (run from the Referer)
       Both are written to --results as <stamp>-<run>-<bench>-<part>.json; one line on stdout
       ("SERVE report ...") per report.

SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import datetime
import functools
import http.server
import json
import os
import re
import sys
import threading
import urllib.parse

VERSION = "phx-bench-server 1"
MIME = {
    ".html": "text/html; charset=utf-8", ".htm": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
    ".cjs": "text/javascript; charset=utf-8", ".json": "application/json", ".map": "application/json",
    ".css": "text/css; charset=utf-8", ".svg": "image/svg+xml", ".xml": "application/xml",
    ".xhtml": "application/xhtml+xml", ".txt": "text/plain; charset=utf-8", ".md": "text/plain; charset=utf-8",
    ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".gif": "image/gif", ".webp": "image/webp",
    ".avif": "image/avif", ".ico": "image/x-icon", ".bmp": "image/bmp",
    ".woff": "font/woff", ".woff2": "font/woff2", ".ttf": "font/ttf", ".otf": "font/otf",
    ".wasm": "application/wasm", ".mp4": "video/mp4", ".webm": "video/webm", ".mp3": "audio/mpeg",
    ".ogg": "audio/ogg", ".wav": "audio/wav", ".pdf": "application/pdf", ".lua": "text/plain; charset=utf-8",
    ".c": "text/plain; charset=utf-8", ".py": "text/plain; charset=utf-8", ".dart": "text/plain; charset=utf-8",
}
SAFE = re.compile(r"[^A-Za-z0-9._-]")
_overrides = {}
_lock = threading.Lock()


def overrides_for(directory):
    """the .phx-headers.json of a directory, read once"""
    with _lock:
        if directory not in _overrides:
            path = os.path.join(directory, ".phx-headers.json")
            try:
                with open(path, encoding="utf-8") as f:
                    _overrides[directory] = json.load(f)
            except (OSError, ValueError):
                _overrides[directory] = {}
        return _overrides[directory]


class Handler(http.server.SimpleHTTPRequestHandler):
    server_version = VERSION
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        if self.server.verbose:
            sys.stderr.write("SERVE %s %s\n" % (self.address_string(), fmt % args))

    def guess_type(self, path):
        return MIME.get(os.path.splitext(path)[1].lower(), "application/octet-stream")

    def end_headers(self):
        # revalidate every time (a hook edited between runs is picked up; a 304 is cheap)
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        if url.path == "/phx-ping":
            body = (VERSION + " ok root=" + self.server.root + "\n").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        path = self.translate_path(url.path)
        rule = overrides_for(os.path.dirname(path)).get(os.path.basename(path)) if os.path.isfile(path) else None
        if rule:
            with open(path, "rb") as f:
                body = f.read()
            self.send_response(int(rule.get("status", 200)))
            self.send_header("Content-Type", rule.get("type", self.guess_type(path)))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        if url.path not in ("/phx-report", "/report"):
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > 256 * 1024 * 1024:
            self.send_error(411 if length <= 0 else 413)
            return
        body = self.rfile.read(length)
        if url.path == "/report":
            # JetStream's own report: the run id is in the page's address (the Referer)
            referer = urllib.parse.parse_qs(urllib.parse.urlsplit(self.headers.get("Referer", "")).query)
            run, bench, part = referer.get("run", ["manual"])[0], "jetstream", "jetstream-report"
        else:
            run = query.get("run", ["manual"])[0]
            bench = query.get("bench", ["unknown"])[0]
            part = query.get("part", ["final"])[0]
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        name = "%s-%s-%s-%s.json" % (stamp, SAFE.sub("_", run)[:64], SAFE.sub("_", bench)[:32], SAFE.sub("_", part)[:32])
        os.makedirs(self.server.results, exist_ok=True)
        path = os.path.join(self.server.results, name)
        with open(path, "wb") as f:
            f.write(body)
        print("SERVE report bench=%s run=%s part=%s bytes=%d file=%s from=%s" %
              (bench, run, part, len(body), path, self.client_address[0]), flush=True)
        reply = b'{"ok":true}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.path.dirname(here) if os.path.basename(here) == "tools" else here,
                    help="the staged suite (default: the directory above tools/ when run from the export)")
    ap.add_argument("--results", default=None, help="where reports go (default: <root>/results)")
    ap.add_argument("--bind", action="append", help="address to listen on (repeatable; default 127.0.0.1)")
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    results = os.path.abspath(args.results or os.path.join(root, "results"))
    servers = []
    for address in args.bind or ["127.0.0.1"]:
        s = Server((address, args.port), functools.partial(Handler, directory=root))
        s.root, s.results, s.verbose = root, results, args.verbose
        servers.append(s)
        print("SERVE listening http://%s:%d/ root=%s results=%s" % (address, args.port, root, results), flush=True)
    for s in servers[1:]:
        threading.Thread(target=s.serve_forever, daemon=True).start()
    try:
        servers[0].serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
