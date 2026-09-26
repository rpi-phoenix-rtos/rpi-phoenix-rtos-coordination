#!/usr/bin/env bash
#
# Build a CLONED QuakeSpasm, `quakespasm-v3da`, whose GPU layer is the new lane:
# Mesa (unchanged) + the v3da adapter (v3da_winsys.c) + libv3da-client, talking to
# the rpi4-v3d-async server over /dev/v3d-async - instead of the in-process winsys
# that drives the V3D from inside the game. The shipped /usr/bin/quakespasm is never
# rebuilt or replaced (PLAN ground rule 2).
#
# HOW (the stkprof trick, tools/gpu-lane/stkprof/build-stkprof.sh):
#   1. build the adapter objects with tools/gpu-lane/v3d-async/build.sh into
#      $OUT/v3da/ (never into that directory's out/, which a queued Pi cycle stages);
#   2. copy tools/.gpu-libs/libv3d-phoenix.a to $OUT and, IN THE COPY, delete the
#      three in-process winsys members (v3d_phoenix_winsys.o, v3d_phoenix_power.o,
#      v3d_libdrm_shim.o) and add v3da_winsys.o + libv3da-client.o;
#   3. re-run the quakespasm port's final link - the exact command recorded in the
#      port's build.log - with that archive substituted and `-o` pointed at $OUT.
#
# PROOFS it prints (and fails on):
#   * a CONTROL relink with the untouched archive reproduces the shipped
#     prog/quakespasm byte for byte (warning only, if the port tree moved on since);
#   * the clone defines phoenix_v3d_ioctl from the adapter and v3da_connect, and
#     carries none of the in-process winsys (no winsys_init / boPool_take /
#     v3d_phoenix_rcl_bad_at_entry, no "v3d-winsys: RT scanout" string);
#   * the archive copy has exactly (shipped members - 3 + 2) members;
#   * the shared inputs (the shipped archive, prog/quakespasm, the rootfs binary,
#     build.log) are unchanged afterwards.
#
# WRITES ONLY under $QSV3DA_OUT (default artifacts/quakespasm-v3da/). It does not
# touch /srv, the TFTP loader, .buildroot outputs, tools/.gpu-libs or sources/.
#
# Usage: tools/gpu-lane/v3d-async/build-quakespasm-v3da.sh [--no-control]
# Env:   QSV3DA_OUT, TARGET (default aarch64a72-generic-rpi4b), RPI4B_BUILDROOT
# Stage (coordinator only):
#   sudo cp $OUT/quakespasm-v3da.stripped <live NFS root>/usr/bin/quakespasm-v3da
#   (plus out-p2/rpi4-v3d-async; the server must be started before the game)
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${here}/../../.." && pwd)"
target="${TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
out="${QSV3DA_OUT:-${repo_root}/artifacts/quakespasm-v3da}"
tcbin="${repo_root}/.toolchain/aarch64-phoenix/bin"
ar="${tcbin}/aarch64-phoenix-ar"
nm="${tcbin}/aarch64-phoenix-nm"
strip="${tcbin}/aarch64-phoenix-strip"
readelf="${tcbin}/aarch64-phoenix-readelf"

pfx="${buildroot}/_build/${target}"
portdir="${pfx}/port-sources/quakespasm-0.97.0"
buildlog="${portdir}/build.log"
shipped_lib="${repo_root}/tools/.gpu-libs/libv3d-phoenix.a"
shipped_prog="${pfx}/prog/quakespasm"
shipped_bin="${buildroot}/_fs/${target}/root/usr/bin/quakespasm"
swap_out=(v3d_phoenix_winsys.o v3d_phoenix_power.o v3d_libdrm_shim.o)

do_control=1
while [ "$#" -gt 0 ]; do
	case "$1" in
		--no-control) do_control=0 ;;
		-h|--help) sed -n '2,40p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-quakespasm-v3da: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

log()  { printf '[qs-v3da] %s\n' "$*"; }
warn() { printf '[qs-v3da] WARNING: %s\n' "$*" >&2; }
die()  { printf '[qs-v3da] ERROR: %s\n' "$*" >&2; exit 1; }
sha()  { if [ -e "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else echo "absent"; fi; }

for f in "$ar" "$nm" "$strip" "$readelf" "$buildlog" "$shipped_lib" "$shipped_prog"; do
	[ -e "$f" ] || die "missing: $f (has the quakespasm port been built in this buildroot?)"
done
ship_members="$("$ar" t "$shipped_lib")"
for m in "${swap_out[@]}"; do
	grep -qx "$m" <<<"$ship_members" || die "$shipped_lib has no member $m -- the GPU lib layout changed; update this script"
done

guarded=("$shipped_lib" "$shipped_prog" "$shipped_bin" "$buildlog")
declare -A before
for f in "${guarded[@]}"; do before["$f"]="$(sha "$f")"; done
check_guarded() {
	local f bad=0
	for f in "${guarded[@]}"; do
		if [ "${before[$f]}" != "$(sha "$f")" ]; then
			printf '[qs-v3da] ERROR: shared file CHANGED during this build: %s\n' "$f" >&2
			bad=1
		fi
	done
	[ "$bad" = 0 ] || exit 1
	log "guarded shared files unchanged (${#guarded[@]} checked)"
}

mkdir -p "$out"

# --- 1. adapter objects ----------------------------------------------------------
log "building the v3da server/client/adapter into $out/v3da"
"${here}/build.sh" --out "$out/v3da" > "$out/v3da-build.log" 2>&1 || { cat "$out/v3da-build.log" >&2; die "v3da build failed"; }
for f in "$out/v3da/obj/v3da_winsys.o" "$out/v3da/obj/libv3da-client.o"; do
	[ -f "$f" ] || die "adapter object missing: $f"
done

# --- 2. the archive copy with the winsys swapped -----------------------------------
lib="$out/libv3d-phoenix-v3da.a"
cp "$shipped_lib" "$lib"
"$ar" d "$lib" "${swap_out[@]}"
"$ar" r "$lib" "$out/v3da/obj/v3da_winsys.o" "$out/v3da/obj/libv3da-client.o"
n_ship="$("$ar" t "$shipped_lib" | wc -l)"
n_new="$("$ar" t "$lib" | wc -l)"
[ "$n_new" = "$((n_ship - 3 + 2))" ] || die "archive member count $n_ship -> $n_new (expected $((n_ship - 1)))"
new_members="$("$ar" t "$lib")"
for m in "${swap_out[@]}"; do
	if grep -qx "$m" <<<"$new_members"; then die "$m still in the v3da archive"; fi
done
log "archive: $lib ($n_ship -> $n_new members: -${swap_out[*]} +v3da_winsys.o +libv3da-client.o)"

# --- 3. the port's final link, from its build.log -------------------------------
# build.log holds the bash -x trace of port.def.sh; the final link is the one line
# that writes ".../prog//quakespasm".
linkline="$(grep -E '^\+ aarch64-phoenix-gcc .* -o [^ ]*/prog/+quakespasm$' "$buildlog" || true)"
[ -n "$linkline" ] || die "no final link line (-o .../prog//quakespasm) in $buildlog"
[ "$(printf '%s\n' "$linkline" | wc -l)" = 1 ] || die "more than one final link line in $buildlog"
linkline="${linkline#+ }"
case "$linkline" in
	*" ${shipped_lib} "*) ;;
	*) die "the link line does not name $shipped_lib -- the port recipe changed; update this script" ;;
esac
out_arg="$(printf '%s\n' "$linkline" | grep -oE ' -o [^ ]+$')"

relink() {   # $1 = libv3d archive, $2 = output ELF
	local cmd="${linkline% -o *}"
	cmd="${cmd/ ${shipped_lib} / $1 }"
	rm -f "$2"
	# The recorded command calls the toolchain by bare name, as the port build did.
	( export PATH="${tcbin}:${PATH}"; eval "$cmd -o '$2'" ) || die "link failed: $2"
	[ -f "$2" ] || die "link reported success but produced no ELF: $2"
	local ph
	ph="$("$readelf" -l "$2" 2>/dev/null)"
	if grep -q INTERP <<<"$ph"; then die "$2 has a PT_INTERP segment"; fi
}
[ -n "$out_arg" ] || die "could not parse the -o argument"

if [ "$do_control" = 1 ]; then
	log "control relink with the UNMODIFIED archive (proves the recipe reproduces shipped)"
	relink "$shipped_lib" "$out/quakespasm-control"
	if cmp -s "$out/quakespasm-control" "$shipped_prog"; then
		log "  PROOF: control relink == shipped prog/quakespasm (byte-identical)"
	else
		warn "control relink differs from shipped prog/quakespasm: an input changed since the shipped build"
		warn "  (libphoenix, SDL2, libGL or the port objects). An A/B against the shipped binary then"
		warn "  compares more than the GPU layer -- rebuild the port (coordinator) first, or A/B against"
		warn "  $out/quakespasm-control instead."
	fi
	"$strip" -o "$out/quakespasm-control.stripped" "$out/quakespasm-control"
fi

log "v3da relink"
relink "$lib" "$out/quakespasm-v3da"

# --- proofs on the clone -------------------------------------------------------------
syms="$("$nm" "$out/quakespasm-v3da")"
grep -qE ' T phoenix_v3d_ioctl$' <<<"$syms" || die "clone lacks T phoenix_v3d_ioctl"
grep -qE ' T v3da_connect$' <<<"$syms" || die "clone lacks T v3da_connect (libv3da-client not linked)"
grep -qE ' T drmSyncobjExportSyncFile$' <<<"$syms" || die "clone lacks the adapter's drmSyncobj surface"
for s in winsys_init boPool_take v3d_phoenix_rcl_bad_at_entry v3d_c1_lookup_pa mboxProp; do
	if grep -qE " [tTdDbB] ${s}$" <<<"$syms"; then die "clone still carries in-process winsys symbol $s"; fi
done
# grep -a on the ELF itself (a `strings | grep -q` pipeline dies of SIGPIPE under pipefail)
if grep -aq 'v3d-winsys: RT scanout' "$out/quakespasm-v3da"; then
	die "clone still carries in-process winsys strings"
fi
grep -aq 'v3da-winsys: connected' "$out/quakespasm-v3da" || die "clone lacks the adapter's banner"
log "  PROOF: the clone links the v3da adapter + libv3da-client and none of the in-process winsys"
"$strip" -o "$out/quakespasm-v3da.stripped" "$out/quakespasm-v3da"

{
	echo "built:             $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "devices HEAD:      $(git -C "${repo_root}/sources/phoenix-rtos-devices" rev-parse HEAD)"
	echo "libv3d (shipped):  $(sha "$shipped_lib")"
	echo "libv3d (v3da):     $(sha "$lib")"
	echo "v3da_winsys.c:     $(sha "${here}/v3da_winsys.c")"
	echo "libv3da-client.c:  $(sha "${here}/libv3da-client.c")"
	echo "v3da_proto.h:      $(sha "${here}/v3da_proto.h")"
	echo "build.log:         $(sha "$buildlog")"
	echo "shipped prog:      $(sha "$shipped_prog")"
	echo "shipped stripped:  $(sha "$shipped_bin")"
	echo "v3da stripped:     $(sha "$out/quakespasm-v3da.stripped")"
	echo "server (same build): $(sha "$out/v3da/rpi4-v3d-async")"
} > "$out/BUILD-INFO.txt"

check_guarded
log "done:"
log "  $out/quakespasm-v3da.stripped -> stage as /usr/bin/quakespasm-v3da"
log "  $out/quakespasm-v3da          (unstripped, for addr2line)"
log "  $out/v3da/rpi4-v3d-async      -> the matching server (protocol v2); $out/v3da/v3dasync-ping"
log "  $out/BUILD-INFO.txt"
