#!/usr/bin/env bash
#
# Browser track D: a SCRATCH build of WPE WebKit (wpe-browser) outside the image build, for
# development and hand-staged Pi checks. The build itself is the webkit_wpe port's
# files/build-wpe.sh (phoenix-rtos-ports, since B6): this wrapper points it at this repository's
# tree and runs its compile under scripts/heavy-build.sh. See README.md next to this script.
#
# Usage: tools/browser/wpe/build.sh --out <dir> [--dl <dir>] [-j N]
#            [--stage ruby|deps|compat|extract|configure|build|plugins|all] [--mesa-variant gles|wayland] [--clean]
#   environment: WPE_PORT_DIR=<webkit_wpe port directory> (default: the ports repository's, i.e.
#                after branch webkit-wpe-port is merged; a worktree before), WEBKIT_SRC=<patched
#                tree>, PHX_TREE=<_build/<target>>, PHX_WEBKIT_DEPS / PHX_ICU_PREFIX and the other
#                PHX_* of build-wpe.sh.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
port="${WPE_PORT_DIR:-${root}/sources/phoenix-rtos-ports/webkit_wpe}"
[ -x "${port}/files/build-wpe.sh" ] || {
	echo "build.sh: no ${port}/files/build-wpe.sh: merge phoenix-rtos-ports branch webkit-wpe-port, or set WPE_PORT_DIR to its worktree's webkit_wpe" >&2
	exit 1
}
# --out must be outside the repository (the build is ~15 GB)
out=""
prev=""
for a in "$@"; do
	case "${a}" in --out=*) out="${a#--out=}" ;; esac
	[ "${prev}" != --out ] || out="${a}"
	prev="${a}"
done
case "$(realpath -m "${out:-/nonexistent}")/" in
	"${root}/"*) echo "build.sh: --out must be outside the repository" >&2; exit 2 ;;
esac

# the jsc shell (tools/browser/jsc) keeps its own copies of patches 0001-0005
for p in "${root}"/tools/browser/jsc/patches/webkit/*.patch; do
	cmp -s "${p}" "${port}/patches/webkit/$(basename "${p}")" ||
		echo "build.sh: warning: tools/browser/jsc/patches/webkit/$(basename "${p}") differs from the port's copy" >&2
done

export PHX_TREE="${PHX_TREE:-${root}/.buildroot/_build/aarch64a72-generic-rpi4b}"
export PHX_TC="${PHX_TC:-${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix}"
# A scratch tree configured before the port (from tools/browser/jsc/cmake) keeps that toolchain
# file: a changed one makes build-wpe.sh rebuild WebKit from scratch (~2 h).
export PHX_CMAKE_HERE="${PHX_CMAKE_HERE:-${root}/tools/browser/jsc}"
export PHX_HEAVY_BUILD="${root}/scripts/heavy-build.sh"
# ccache changes every compile line, so an existing scratch tree would rebuild once: opt-in here
export PHX_CCACHE="${PHX_CCACHE:-0}"
exec "${port}/files/build-wpe.sh" "$@"
