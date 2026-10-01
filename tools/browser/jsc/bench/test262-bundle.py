#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
#
# Build a self-contained test262 subset bundle (JSON) for tools/browser/jsc/bench/test262-run.js.
#
# The Pi shell (psh) has no loops or pipes, so the whole run happens inside one `jsc` process:
# this host-side step resolves every test's harness includes and strictness variants ahead of
# time, and the runner only has to evaluate strings in fresh realms.
#
#   test262-bundle.py <test262-checkout> <out.json> [--stride N] [--dirs a,b,...]
#
# Selection (deterministic): every Nth test file, in sorted path order, under the given
# directories of test/; module tests (and dynamic import(), which loads fixture files), tests
# needing features JSC ships disabled or unimplemented (WebKit's own JSTests/test262/config.yaml
# skip list) and agent/SharedArrayBuffer tests are skipped and counted. Default-mode tests run
# twice (sloppy and strict), as test262 specifies.
#
# Known harness artifact: the jsc shell gives every global object an `arguments` array
# (jsc.cpp, GlobalObject), so the few eval-code tests asserting "no global 'arguments' binding"
# fail on every platform; compare a target's failure list with the host's, not with zero.

import argparse
import json
import os
import re
import sys

# WebKit 2.54's JSTests/test262/config.yaml "skip: features" plus the features it gates behind
# runtime options that are off by default (flags: section), plus multi-agent tests.
SKIP_FEATURES = {
    "callable-boundary-realms", "FinalizationRegistry.prototype.cleanupSome", "decorators",
    "source-phase-imports", "joint-iteration", "Intl.Era-monthcode", "await-dictionary",
    "import-bytes", "immutable-arraybuffer", "error-stack-accessor",
    "SharedArrayBuffer", "Atomics", "Temporal", "ShadowRealm", "json-parse-with-source",
    "iterator-sequencing", "explicit-resource-management", "import-defer",
    "CanBlockIsFalse", "CanBlockIsTrue", "host-gc-required",
    # Not a JSC limitation: these import *_FIXTURE.js files relative to the test's own path,
    # which a bundled, in-memory run does not have.
    "dynamic-import",
}

FRONTMATTER = re.compile(r"/\*---(.*?)---\*/", re.S)


def parse_frontmatter(text):
    m = FRONTMATTER.search(text)
    meta = {"includes": [], "flags": [], "features": [], "negative": None}
    if not m:
        return meta
    body = m.group(1)
    # A tiny YAML subset: the keys test262 uses, as inline lists or "- item" blocks.
    lines = body.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        key = re.match(r"^(\w+):\s*(.*)$", line)
        if key and key.group(1) in ("includes", "flags", "features"):
            val = key.group(2).strip()
            items = []
            if val.startswith("["):
                items = [x.strip() for x in val.strip("[]").split(",") if x.strip()]
            else:
                j = i + 1
                while j < len(lines) and re.match(r"^\s+-\s+", lines[j]):
                    items.append(re.sub(r"^\s+-\s+", "", lines[j]).strip())
                    j += 1
                i = j - 1
            meta[key.group(1)] = items
        elif key and key.group(1) == "negative":
            neg = {}
            j = i + 1
            while j < len(lines) and re.match(r"^\s+\w+:", lines[j]):
                k, v = lines[j].strip().split(":", 1)
                neg[k.strip()] = v.strip()
                j += 1
            meta["negative"] = neg
            i = j - 1
        i += 1
    return meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("checkout")
    ap.add_argument("out")
    ap.add_argument("--stride", type=int, default=10)
    ap.add_argument("--dirs", default="built-ins,language,annexB")
    args = ap.parse_args()

    root = args.checkout
    harness_cache = {}

    def harness(name):
        if name not in harness_cache:
            with open(os.path.join(root, "harness", name), encoding="utf-8") as f:
                harness_cache[name] = f.read()
        return harness_cache[name]

    files = []
    for d in args.dirs.split(","):
        base = os.path.join(root, "test", d)
        for dirpath, _, names in os.walk(base):
            for n in names:
                if n.endswith(".js") and "_FIXTURE" not in n:
                    files.append(os.path.relpath(os.path.join(dirpath, n), root))
    files.sort()
    picked = files[::args.stride]

    tests = []
    sources = {}
    skipped = {"module": 0, "feature": 0, "raw-async": 0}
    for rel in picked:
        with open(os.path.join(root, rel), encoding="utf-8") as f:
            text = f.read()
        meta = parse_frontmatter(text)
        flags = set(meta["flags"])
        if "module" in flags:
            skipped["module"] += 1
            continue
        if SKIP_FEATURES.intersection(meta["features"]):
            skipped["feature"] += 1
            continue
        neg = meta["negative"]
        if neg and neg.get("phase") == "resolution":
            skipped["module"] += 1
            continue
        is_async = "async" in flags
        if "raw" in flags:
            if is_async:
                skipped["raw-async"] += 1
                continue
            includes, modes = [], ["raw"]
        else:
            includes = ["assert.js", "sta.js"] + [i for i in meta["includes"]
                                                  if i not in ("assert.js", "sta.js", "doneprintHandle.js")]
            for inc in includes:
                harness(inc)  # fail early on a missing harness file
            if "onlyStrict" in flags:
                modes = ["strict"]
            elif "noStrict" in flags:
                modes = ["sloppy"]
            else:
                modes = ["sloppy", "strict"]
        for mode in modes:
            t = {"p": rel, "m": mode, "i": includes}
            if is_async:
                t["a"] = 1
            if neg:
                t["n"] = {"phase": neg.get("phase", ""), "type": neg.get("type", "")}
            tests.append(t)
        sources[rel] = text

    rev = ""
    revfile = os.path.join(root, ".test262-revision")
    if os.path.exists(revfile):
        rev = open(revfile).read().strip()
    # Harness files and test sources are stored once; test262-run.js concatenates
    # ("use strict" first for strict runs, then the harness includes, then the test).
    bundle = {"revision": rev, "stride": args.stride, "dirs": args.dirs, "files": len(picked),
              "skipped": skipped, "harness": harness_cache, "sources": sources, "tests": tests}
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(bundle, f, separators=(",", ":"))
    print("test262 bundle: %d files picked (stride %d of %d), %d runs, skipped %s -> %s"
          % (len(picked), args.stride, len(files), len(tests), skipped, args.out))


if __name__ == "__main__":
    sys.exit(main())
