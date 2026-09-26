#!/usr/bin/env bash
# Cross-compile ONE source file from a sibling repo and report errors -- without
# producing any build artifact.
#
# Why this exists: a `--scope core` build ends in the image stage, which
# overwrites the TFTP `loader.disk`. That makes a full build impossible whenever
# a Pi bench is measuring the current loader -- which is most of a long
# unattended run. This checks a single file under the REAL compile flags
# (-Werror included) and writes nothing, so it is safe at any time.
#
# It earned its keep immediately: it caught `*prev/*next` inside a block comment
# in vm/map.c (the `/*` sequence trips -Werror=comment), which would otherwise
# have failed a kernel gate build hours later.
#
# Usage:
#   ./scripts/syntax-check.sh <sibling-repo> <path/in/repo.c> [target]
#   ./scripts/syntax-check.sh phoenix-rtos-kernel vm/map.c
#   ./scripts/syntax-check.sh phoenix-rtos-devices audio/rpi4-audio/rpi4-audio.c
#
# Checks the file as it is in sources/ right now: it stages that copy into the
# matching .buildroot/ tree first, because .buildroot is a COPY that a real
# build refreshes with rsync --delete. Nothing else is touched, and the next
# real build overwrites the staged file anyway.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
repo="${1:?usage: syntax-check.sh <sibling-repo> <path/in/repo.c> [target]}"
rel="${2:?usage: syntax-check.sh <sibling-repo> <path/in/repo.c> [target]}"
target="${3:-aarch64a72-generic-rpi4b}"

src="${repo_root}/sources/${repo}/${rel}"
bdir="${repo_root}/.buildroot/${repo}"
obj="${repo_root}/.buildroot/_build/${target}/${repo}/${rel%.c}.o"
proj="${repo_root}/.buildroot/_projects/${target}"
tc="${repo_root}/.toolchain/aarch64-phoenix/bin"

[ -f "$src" ]   || { echo "syntax-check: no such source: $src" >&2; exit 2; }
[ -d "$bdir" ]  || { echo "syntax-check: no buildroot copy of $repo -- run a build once first" >&2; exit 2; }
[ -d "$tc" ]    || { echo "syntax-check: toolchain not found at $tc" >&2; exit 2; }

mkdir -p "$(dirname "${bdir}/${rel}")"
cp "$src" "${bdir}/${rel}"

# Stage the sibling HEADERS from the same directory too. A change that spans a
# .h and its .c -- adding an inline helper and calling it, say -- would otherwise
# compile the new .c against the OLD header and report a bogus "implicit
# declaration". Found the first time this script met such a change (2026-09-19,
# sdio_submitBarrier). Headers are cheap to copy and the buildroot copy is
# rsynced over by the next real build anyway.
for _h in "$(dirname "$src")"/*.h; do
    [ -e "$_h" ] || continue
    cp "$_h" "$(dirname "${bdir}/${rel}")/"
done

# ...and any header the file includes by a RELATIVE path outside its own
# directory. Staging only the sibling directory was not enough: umass.c includes
# "../pc-ata/mbr.h", so a constant added there read as undeclared here while the
# real build compiled it fine -- a false FAILURE, the mirror image of the false
# CLEAN this script was fixed for on 2026-09-20.
while IFS= read -r _inc; do
    _from="$(dirname "$src")/${_inc}"
    _to="$(dirname "${bdir}/${rel}")/${_inc}"
    [ -f "$_from" ] || continue
    mkdir -p "$(dirname "$_to")"
    cp "$_from" "$_to"
done <<EOF
$(grep -oE '#include[[:space:]]*"[^"]*/[^"]*"' "$src" 2>/dev/null | sed -e 's/.*"\(.*\)"/\1/')
EOF

# Ask make what it WOULD run for that object, and take the compiler line FOR THIS
# FILE.
#
# â `make -n <obj>` prints the recipes for every prerequisite in that directory,
# not just the one asked for: requesting `sdstorage_srv.o` emitted three gcc lines
# (sdcard.c, sdio.c, sdstorage_srv.c, in that order). The original `grep -m1` took
# the FIRST, so unless your file happened to sort first this script compiled a
# DIFFERENT source and reported CLEAN for it. Caught 2026-09-20 by a negative
# control: a file the real build rejects with `error: 'SDCARD_BLOCKLEN' undeclared`
# passed this check. Every "CLEAN" from before that date is only as good as the
# file's position in its directory.
#
# So match on the source path, and require exactly one hit.
#
# ⚠ The project's build.project EXPORTS make variables that Makefiles test, e.g.
# `export PCI_EXPRESS_BCM2711_INDEXED_CFG=y`, which usb/xhci/Makefile turns into a
# -D. A bare `make -n` never sees them, so bcm2711-pcie.c failed here with
# `ECAM_SIZE undeclared` while the real build compiled it (found 2026-09-26).
# Import every plain `export NAME=literal` line; values built from `$(...)` or
# other variables are skipped, since sourcing the whole script is not safe here.
if [ -f "${proj}/build.project" ]; then
    while IFS= read -r _kv; do
        export "${_kv?}"
    done <<EOF
$(grep -E '^export [A-Z_][A-Z0-9_]*=[^$" ]*$' "${proj}/build.project" | sed -e 's/^export //')
EOF
fi
cmd=$(cd "$bdir" && PATH="${tc}:$PATH" TARGET="$target" make -n "$obj" 2>/dev/null \
        | grep -- '-phoenix-gcc ' | grep -F -- "$(basename "$rel")" || true)
n=$(printf '%s' "$cmd" | grep -c . || true)
if [ -z "$cmd" ]; then
    echo "syntax-check: could not recover a compile command for ${repo}/${rel}." >&2
    echo "  (is it actually built for ${target}? some files are per-target)" >&2
    exit 2
fi
if [ "$n" -ne 1 ]; then
    echo "syntax-check: ${n} candidate compile commands mention $(basename "$rel");" >&2
    echo "  refusing to guess -- a wrong pick reports CLEAN for another file." >&2
    exit 2
fi

# -fsyntax-only writes nothing. Drop the output and dependency flags so gcc does
# not object to them, and add the board include: board_config.h lives in
# _projects/<target>/ and the top level injects that -I through the environment,
# so it is absent from a bare `make -n`. Confirmed against the real build's own
# .d file, which records board_config.h resolving from exactly that directory.
cmd=$(printf '%s\n' "$cmd" \
    | sed -e 's/ -c / /' \
          -e 's/ -o "[^"]*"//g' -e 's/ -o [^ ]*//g' \
          -e 's/ -MD//g' -e 's/ -MP//g' \
          -e 's/ -MF [^ ]*//g' -e 's/ -MT "[^"]*"//g' -e 's/ -MT [^ ]*//g')

# A few sources are compiled by a build OTHER than their repo's own Makefile and
# so need include paths that `make -n` here never shows. gpu/rpi4-v3d/mesa/*.c is
# the case in hand: it is built inside the Mesa tree (build-showcase-apps.sh) and
# includes "drm-uapi/v3d_drm.h", which resolves from external/mesa/include. Set
# SYNTAX_CHECK_CFLAGS to supply those; everything else still comes from the real
# command line, so -Werror and the warning set are unchanged.
printf 'syntax-check: %s/%s  (target %s)\n' "$repo" "$rel" "$target"
[ -n "${SYNTAX_CHECK_CFLAGS:-}" ] && printf 'syntax-check: extra flags: %s\n' "$SYNTAX_CHECK_CFLAGS"
if (cd "$bdir" && PATH="${tc}:$PATH" eval "$cmd -fsyntax-only -I${proj} ${SYNTAX_CHECK_CFLAGS:-}"); then
    printf 'syntax-check: CLEAN (compiles under the real flags, -Werror included)\n'
else
    rc=$?
    printf 'syntax-check: FAILED (rc=%d) -- fix before spending a build on it\n' "$rc" >&2
    exit "$rc"
fi
