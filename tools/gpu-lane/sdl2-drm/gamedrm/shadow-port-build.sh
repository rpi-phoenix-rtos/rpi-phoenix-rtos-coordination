#!/usr/bin/env bash
#
# Re-run one game port's own recipe (p_prepare + p_build from sources/phoenix-rtos-ports/<port>/
# port.def.sh) into a private directory, so relink-sdl-gl-game.sh can relink a clone when the
# port's tree under .buildroot/_build/<target>/port-sources/ is not there. The ports framework
# deletes that tree whenever the port's build state changes and rebuilds it only when the image
# build reaches the port -- after a failed ports stage (build 15: python) the engine objects and
# the build.log the relink reads are simply gone.
#
# What it does: extract the port's tarball, apply its patches (-p1, in order), then call the
# port's p_build under `set -x` (the framework's own tracing, so <out>/build.log holds the final
# link as the `+ aarch64-phoenix-gcc ... -o <out>/prog/<engine>` line the relink parses) with the
# framework's environment: CFLAGS/LDFLAGS = the image build's export flags (taken verbatim from
# the port link recorded in <recorded link-cmd.txt>, i.e. the flags the shipped objects were built
# with), PREFIX_A/PREFIX_H = the ports prefix, CC/STRIP from the toolchain. Everything is written
# under <out>: <out>/<src_path> (source + _phoenix_obj/), <out>/prog/, <out>/prog.stripped/.
# b_install is a no-op: nothing is installed, .buildroot is only read (the ports prefix's
# libSDL2.a/headers and tools/.gpu-libs for the port's own old-stack link, which is its control).
#
# The objects are the port's, compiled now: against the CURRENT sysroot headers, from a different
# path (so __FILE__/debug strings differ). They are not byte-identical to the shipped ones; the
# relink's control link shows by how much (frame-pacing.md lists what it compared).
#
# Usage: gamedrm/shadow-port-build.sh <port> <out dir> <recorded link-cmd.txt>
#   e.g. gamedrm/shadow-port-build.sh yquake2 build-out/quake2-drm-pace/port build-out/quake2-drm/link-cmd.txt
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

port="${1:?usage: shadow-port-build.sh <port> <out dir> <recorded link-cmd.txt>}"
out="$(realpath -m "${2:?out dir missing}")"
rec="$(realpath -m "${3:?recorded link-cmd.txt missing}")"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(cd "${here}/../../.." && pwd)"
target="${TARGET:-aarch64a72-generic-rpi4b}"
pfx="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}/_build/${target}"
tcbin="${repo_root}/.toolchain/aarch64-phoenix/bin"
pdir="${repo_root}/sources/phoenix-rtos-ports/${port}"

log() { printf '[shadow-%s] %s\n' "${port}" "$*"; }
die() { printf '[shadow-%s] ERROR: %s\n' "${port}" "$*" >&2; exit 1; }

case "${out}" in "${repo_root}/.buildroot"*|"${repo_root}/sources"*) die "refusing ${out}: .buildroot and sources/ are read-only here" ;; esac
[ -f "${pdir}/port.def.sh" ] || die "no ${pdir}/port.def.sh"
[ -f "${rec}" ] || die "no ${rec}"

# CFLAGS + LDFLAGS: every token of the recorded link before its first object, split at the
# first linker flag (the port links "${CC} ${CFLAGS} ${LDFLAGS} objs ...").
read -r -a toks < "${rec}"
cflags=() ldflags=()
for t in "${toks[@]:1}"; do
	case "$t" in
		*.o) break ;;
		-Wl,*|-L*) ldflags+=("$t") ;;
		*) [ "${#ldflags[@]}" = 0 ] || die "compiler flag after a linker flag in ${rec}: $t"; cflags+=("$t") ;;
	esac
done
[ "${toks[0]}" = aarch64-phoenix-gcc ] || die "${rec} does not start with aarch64-phoenix-gcc"
[ "${#cflags[@]}" -gt 5 ] && [ "${#ldflags[@]}" -gt 0 ] || die "could not split CFLAGS/LDFLAGS from ${rec}"

rm -rf "${out}"
mkdir -p "${out}/prog" "${out}/prog.stripped"
(
	# shellcheck disable=SC1091
	. "${pdir}/port.def.sh"
	archive="${pdir}/${archive_filename:?}"
	[ -f "${archive}" ] || die "no ${archive}"
	echo "${sha256:?}  ${archive}" | sha256sum -c --quiet - || die "tarball sha256 mismatch"
	tar -C "${out}" -xzf "${archive}"
	export PREFIX_PORT="${pdir}"
	export PREFIX_PORT_WORKDIR="${out}/${src_path:?}"
	[ -d "${PREFIX_PORT_WORKDIR}" ] || die "tarball has no ${src_path}"
	b_die() { printf '[shadow-%s] ERROR (port): %s\n' "${port}" "$*" >&2; exit 1; }
	b_install() { :; }
	b_port_apply_patches() {
		local p
		for p in "${PREFIX_PORT}"/patches/*.patch; do
			patch -d "$1" -p1 -s --no-backup-if-mismatch < "$p" || b_die "patch failed: $(basename "$p")"
			log "  patch $(basename "$p")"
		done
	}
	export -f b_die b_install
	export PATH="${tcbin}:${PATH}"
	export CC=aarch64-phoenix-gcc STRIP=aarch64-phoenix-strip
	export CFLAGS="${cflags[*]}" LDFLAGS="${ldflags[*]}"
	export PREFIX_A="${pfx}/lib" PREFIX_H="${pfx}/include"
	export PREFIX_PROG="${out}/prog" PREFIX_PROG_STRIPPED="${out}/prog.stripped" PREFIX_PROG_TO_INSTALL="${out}/prog.stripped"
	log "p_prepare -> ${PREFIX_PORT_WORKDIR}"
	p_prepare
	log "p_build (CFLAGS/LDFLAGS from $(basename "$(dirname "${rec}")")/$(basename "${rec}"); trace -> ${out}/build.log)"
	( set -x; p_build ) > "${out}/build.log" 2>&1 || { tail -30 "${out}/build.log" >&2; die "p_build failed"; }
)
log "done: $(ls "${out}"/*/_phoenix_obj/*.o | wc -l) objects, prog/$(ls "${out}/prog")"
