#!/usr/bin/env python3
"""diff-boot-variants.py -- render user.plo.yaml for every RPI4B_VARIANT and diff.

WHY: the three variants (sd / nfsroot / netboot) are meant to differ ONLY in
where "/" comes from. They are expressed as jinja2 `if:` gates in one
user.plo.yaml, and a gate written `!= 'nfsroot'` silently drops a line from the
nfsroot boot script -- which is how the boot-console diagnostics came to differ
between an SD boot and a netboot (owner report, 2026-09-04).

This renders the real template with the real renderer's variables and prints,
per variant, the program each `app` line launches -- then a table of who is
missing what. Nothing is built; it only reads the template.

  ./scripts/diff-boot-variants.py [--yaml <path>] [--verbose] [--order a,b,c]

The environment is passed to the template, so knobs render as in a build, e.g.
`RPI4B_GPU_LEGACY=1 ./scripts/diff-boot-variants.py`. Exits 1 if a program name is
launched twice in one variant (plo registers one alias per name: a duplicate
bricks that boot) or if --order names programs that start out of that order.

Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import argparse, os, re, sys
from pathlib import Path

import yaml
import jinja2

VARIANTS = ("sd", "nfsroot", "netboot")
REPO = Path(__file__).resolve().parents[1]
DEFAULT_YAML = (REPO / "sources/phoenix-rtos-project/_projects"
                / "aarch64a72-generic-rpi4b/user.plo.yaml")


def render(tpl, env):
    """Same contract as image_builder.render_val for a plain string."""
    return jinja2.Template(tpl, undefined=jinja2.StrictUndefined).render(env=env)


def program_of(line):
    """`app <dev> -x prog;arg;arg ddr ddr` -> the program token (with its args)."""
    m = re.search(r"-x\s+(\S+)", line)
    if m:
        return m.group(1)
    return line.split()[0] if line.split() else line


def script_for(doc, variant, env_extra):
    env = dict(os.environ)
    env.update({
        "MAGIC_USER_SCRIPT": "0x0",
        "BOOT_DEVICE": "ddr",
        "RPI4B_VARIANT": variant,
        "RPI4B_HAVE_DTB": "true",
    })
    env.update(env_extra)
    out = []
    for item in doc["contents"]:
        if isinstance(item, str):
            out.append(item)
            continue
        cond = item.get("if")
        if cond is not None and not yaml.safe_load(render("{{ %s }}" % cond.strip("{} "), env).lower()):
            continue
        out.append(render(item["str"], env))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--yaml", default=str(DEFAULT_YAML))
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--log-to-file", default="0",
                    help="RPI4_LOG_TO_FILE value to render with (default 0)")
    ap.add_argument("--order", default="",
                    help="comma-separated program names that must start in this order "
                         "in every variant that launches them")
    args = ap.parse_args()

    doc = yaml.safe_load(open(args.yaml))
    env_extra = {"RPI4_LOG_TO_FILE": args.log_to_file}

    scripts = {v: script_for(doc, v, env_extra) for v in VARIANTS}
    progs = {v: [program_of(l) for l in s if l.startswith("app ")] for v, s in scripts.items()}

    for v in VARIANTS:
        print(f"== {v}: {len(progs[v])} app launches ==")
        if args.verbose:
            for p in progs[v]:
                print(f"   {p}")
    print()

    # Compare by program NAME (the part before the first ';'), because the same
    # program legitimately takes different arguments per variant (that is the
    # rootfs pointer). A program launched in one variant and not another is the
    # asymmetry worth reporting.
    names = {v: [p.split(";")[0] for p in progs[v]] for v in VARIANTS}
    every = sorted(set().union(*names.values()))
    width = max(len(n) for n in every)
    print(f"{'program':<{width}}  " + "  ".join(f"{v:>7}" for v in VARIANTS))
    asym = []
    for n in every:
        cells = []
        for v in VARIANTS:
            c = names[v].count(n)
            cells.append("   yes " if c == 1 else (f"   x{c} " if c else "    -  "))
        print(f"{n:<{width}}  " + "  ".join(cells))
        if len({names[v].count(n) > 0 for v in VARIANTS}) > 1:
            asym.append(n)

    print()
    if asym:
        print("ASYMMETRIC (launched in some variants only):")
        for n in asym:
            have = [v for v in VARIANTS if names[v].count(n)]
            miss = [v for v in VARIANTS if not names[v].count(n)]
            print(f"  {n:<{width}}  in: {','.join(have):<24} missing: {','.join(miss)}")
        print("\nEach one needs a reason. A rootfs difference (which server owns \"/\",")
        print("which mountpoints exist) is a reason; 'nobody updated the other gate' is not.")
    else:
        print("No asymmetry: every program is launched in every variant.")

    rc = 0
    dups = [(v, n) for v in VARIANTS for n in sorted(set(names[v])) if names[v].count(n) > 1]
    for v, n in dups:
        print(f"DUPLICATE ALIAS: {n} is launched {names[v].count(n)}x in {v} (plo aborts the boot)")
        rc = 1
    want = [n for n in args.order.split(",") if n]
    for v in VARIANTS:
        seq = [n for n in names[v] if n in want]
        expect = [n for n in want if n in names[v]]
        if want and seq != expect:
            print(f"ORDER: {v} starts {' -> '.join(seq)}, expected {' -> '.join(expect)}")
            rc = 1
        elif want:
            print(f"order ok ({v}): {' -> '.join(seq)}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
