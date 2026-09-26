#!/usr/bin/env bash
#
# Build a CLONED SuperTuxKart 1.4, `supertuxkart-v3da` + its launcher `stk-v3da`,
# whose GPU layer is the new lane: Mesa (unchanged) + the v3da adapter
# (v3da_winsys.c) + libv3da-client, talking to the rpi4-v3d-async server over
# /dev/v3d-async -- instead of the in-process winsys that drives the V3D from
# inside the game. The shipped /usr/bin/supertuxkart and /bin/stk are never rebuilt
# or replaced (PLAN ground rule 2; STK is also the C1 workload, which is
# layout-sensitive, so the shipped binary must stay the one the C1 series measures).
#
# HOW (build-quakespasm-v3da.sh's archive swap + build-stkprof.sh's STK relink):
#   1. build the server, probe and adapter objects with build.sh into $OUT/v3da/
#      (never into out/, out-p*/ or out-h7/, which queued Pi cycles stage). The
#      server built here is protocol-matched to the adapter linked into the clone:
#      stage THIS pair together;
#   2. copy tools/.gpu-libs/libv3d-phoenix.a to $OUT and, IN THE COPY, delete the
#      three in-process winsys members (v3d_phoenix_winsys.o, v3d_phoenix_power.o,
#      v3d_libdrm_shim.o) and add v3da_winsys.o + libv3da-client.o;
#   3. re-run the supertuxkart port's stage-4 link (CMake's link.txt + the SDL2 GL
#      glue objects + the same --start-group as sources/phoenix-rtos-ports/
#      supertuxkart/port.def.sh) from the port's existing build tree, with that
#      archive substituted and `-o` pointed at $OUT;
#   4. build the `stk-v3da` launcher: tools/supertuxkart-port/stk-launcher.c with
#      only its exec path and banner rewritten. It must seed the same config.xml
#      (scale_rtts_factor=0.75 sets the render workload), so running the engine bare
#      is NOT equivalent to `stk`. The new lane needs no extra environment: the
#      adapter finds the server at /dev/v3d-async and reads only V3D_FLIPSTAT /
#      V3D_FLIPSTAT_MS (same meaning as the old winsys).
#
# PROOFS it prints (and fails on):
#   * the v3d-async sources (*.c, *.h) did not change while build.sh compiled them
#     (another agent may be editing them); their git state goes into BUILD-INFO;
#   * a CONTROL relink with the untouched archive reproduces the shipped
#     prog/supertuxkart byte for byte (warning only, if the port tree moved on);
#   * the archive copy has exactly (shipped members - 3 + 2) members;
#   * the clone defines phoenix_v3d_ioctl, drmSyncobjExportSyncFile (adapter) and
#     v3da_connect (client); none of the in-process winsys symbols; no more
#     undefined symbols than the shipped prog/supertuxkart (a static ELF);
#   * the clone carries the adapter's tag strings and none of the old winsys'
#     identifying strings -- and the shipped binary the reverse (inverse control);
#   * the guarded shared inputs are unchanged afterwards.
#
# WRITES ONLY under $STKV3DA_OUT (default tools/gpu-lane/v3d-async/out-stk/). It does
# not touch /srv, the TFTP loader, .buildroot outputs, tools/.gpu-libs (so the Mesa
# shader-cache fingerprint of sync-netboot-tree.sh is unaffected) or sources/.
#
# Usage: tools/gpu-lane/v3d-async/build-stk-v3da.sh [--no-control]
#   --no-control   skip the control relink (saves one ~40 s link)
# Env:   STKV3DA_OUT, TARGET (default aarch64a72-generic-rpi4b), RPI4B_BUILDROOT
# Stage (coordinator only; the live export is the fsid=0 one):
#   install -m 755 $OUT/supertuxkart-v3da.stripped <export>/usr/bin/supertuxkart-v3da
#   install -m 755 $OUT/stk-v3da                   <export>/bin/stk-v3da
#   install -m 755 $OUT/v3da/rpi4-v3d-async        <export>/bin/rpi4-v3d-async
#   (docs/gpu-new-lane/M1-async-render-server.md, "STK clone (stk-v3da)")
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${here}/../../.." && pwd)"
target="${TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
out="$(realpath -m "${STKV3DA_OUT:-${here}/out-stk}")"
tcbin="${repo_root}/.toolchain/aarch64-phoenix/bin"
cc="${tcbin}/aarch64-phoenix-gcc"
ar="${tcbin}/aarch64-phoenix-ar"
nm="${tcbin}/aarch64-phoenix-nm"
strip="${tcbin}/aarch64-phoenix-strip"
readelf="${tcbin}/aarch64-phoenix-readelf"

pfx="${buildroot}/_build/${target}"                        # the ports' shared install prefix
sysroot="${pfx}/sysroot"
stkbuild="${pfx}/port-sources/supertuxkart-1.4/stk-code-1.4/build"
linktxt="${stkbuild}/CMakeFiles/supertuxkart.dir/link.txt"
gluedir="${stkbuild}/_phoenix_glue"
shipped_lib="${repo_root}/tools/.gpu-libs/libv3d-phoenix.a"
gllib="${repo_root}/tools/.gpu-libs/libGL-phoenix.a"
shipped_prog="${pfx}/prog/supertuxkart"                                # unstripped
shipped_bin="${buildroot}/_fs/${target}/root/usr/bin/supertuxkart"     # stripped, what gets staged
shipped_launcher="${buildroot}/_fs/${target}/root/bin/stk"
launcher_src="${repo_root}/tools/supertuxkart-port/stk-launcher.c"
swap_out=(v3d_phoenix_winsys.o v3d_phoenix_power.o v3d_libdrm_shim.o)
name="v3da"

# Identifying strings. The adapter deliberately prints the old winsys'
# `v3d-winsys: flipstat` line (every grader reads it), so the negative check uses
# strings only the in-process winsys objects carry.
new_tags=("v3da-winsys: connected to rpi4-v3d-async" "v3da-winsys: cstat t=" "v3da-winsys: scanout init")
old_tags=("v3d-winsys: RT scanout" "v3d-pool: BO page pool ON" "v3d-coldstate:" "v3d-pwr: C1-hunt" "v3d-winsys: BIN TIMEOUT")
old_syms=(winsys_init boPool_take mboxProp v3d_c1_lookup_pa v3d_phoenix_rcl_bad_at_entry
	v3d_phoenix_render_recoveries v3d_phoenix_render_timeouts)

do_control=1
while [ "$#" -gt 0 ]; do
	case "$1" in
		--no-control) do_control=0 ;;
		-h|--help) sed -n '2,58p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-stk-v3da: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

log()  { printf '[stk-v3da] %s\n' "$*"; }
warn() { printf '[stk-v3da] WARNING: %s\n' "$*" >&2; }
die()  { printf '[stk-v3da] ERROR: %s\n' "$*" >&2; exit 1; }
sha()  { if [ -e "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else echo "absent"; fi; }
src_digest() { ( cd "$here" && sha256sum -- *.c *.h ); }

# --- never write into a directory a queued Pi cycle stages from ------------------
case "$out" in
	"${here}"/out-stk|"${here}"/out-stk/*|"${here}"/out-stk-*) ;;
	"${here}"/out|"${here}"/out/*|"${here}"/out-*)
		die "refusing output dir $out: out/ and the other out-*/ dirs (p2, p3, h7, einval ...) belong to other builds" ;;
esac

# --- preconditions (fail loud; never fall back) -------------------------------
for f in "$cc" "$ar" "$nm" "$strip" "$readelf" "$shipped_lib" "$gllib" "$linktxt" \
		"$gluedir/sdl_phoenix_glctx.o" "$gluedir/sdl_phoenix_glstubs.o" "$pfx/lib/libSDL2.a" \
		"$shipped_prog" "$shipped_bin" "$sysroot/lib/libphoenix.a" "$launcher_src"; do
	[ -e "$f" ] || die "missing: $f (has the supertuxkart port been built in this buildroot?)"
done
ship_members="$("$ar" t "$shipped_lib")"
for m in "${swap_out[@]}"; do
	grep -qx "$m" <<<"$ship_members" || die "$shipped_lib has no member $m -- the GPU lib layout changed; update this script"
done

guarded=("$shipped_lib" "$gllib" "$shipped_prog" "$shipped_bin" "$shipped_launcher" "$linktxt"
	"$gluedir/sdl_phoenix_glctx.o" "$gluedir/sdl_phoenix_glstubs.o")
declare -A before
for f in "${guarded[@]}"; do before["$f"]="$(sha "$f")"; done
check_guarded() {
	local f bad=0
	for f in "${guarded[@]}"; do
		if [ "${before[$f]}" != "$(sha "$f")" ]; then
			printf '[stk-v3da] ERROR: shared file CHANGED during this build: %s\n' "$f" >&2
			bad=1
		fi
	done
	[ "$bad" = 0 ] || exit 1
	log "guarded shared files unchanged (${#guarded[@]} checked)"
}

mkdir -p "$out/src"

# --- 1. server + adapter objects, from one snapshot of the sources ------------------
src_before="$(src_digest)"
rel="${here#"${repo_root}"/}"
src_git="$(git -C "$repo_root" status --porcelain -- "$rel/*.c" "$rel/*.h" "$rel/build.sh")"
log "building the v3da server/client/adapter into $out/v3da"
"${here}/build.sh" --out "$out/v3da" > "$out/v3da-build.log" 2>&1 || { cat "$out/v3da-build.log" >&2; die "v3da build failed (sources being edited? see above)"; }
[ "$src_before" = "$(src_digest)" ] \
	|| die "tools/gpu-lane/v3d-async sources CHANGED while build.sh ran (another agent editing?) -- re-run when they are stable"
for f in "$out/v3da/obj/v3da_winsys.o" "$out/v3da/obj/libv3da-client.o" "$out/v3da/rpi4-v3d-async"; do
	[ -f "$f" ] || die "build.sh output missing: $f"
done
if [ -n "$src_git" ]; then
	warn "the adapter/server were built from UNCOMMITTED v3d-async sources (recorded in BUILD-INFO.txt):"
	printf '%s\n' "$src_git" | sed 's/^/[stk-v3da]   /' >&2
fi
proto="$(grep -oE 'define V3DA_PROTO_VERSION [0-9]+' "${here}/v3da_proto.h" | grep -oE '[0-9]+$')"

# --- 2. the archive copy with the winsys swapped -----------------------------------
lib="$out/libv3d-phoenix-v3da.a"
cp "$shipped_lib" "$lib"
"$ar" d "$lib" "${swap_out[@]}"
"$ar" r "$lib" "$out/v3da/obj/v3da_winsys.o" "$out/v3da/obj/libv3da-client.o"
n_ship="$(wc -l <<<"$ship_members")"
n_new="$("$ar" t "$lib" | wc -l)"
[ "$n_new" = "$((n_ship - 3 + 2))" ] || die "archive member count $n_ship -> $n_new (expected $((n_ship - 1)))"
new_members="$("$ar" t "$lib")"
for m in "${swap_out[@]}"; do
	if grep -qx "$m" <<<"$new_members"; then die "$m still in the v3da archive"; fi
done
log "archive: $lib ($n_ship -> $n_new members: -${swap_out[*]} +v3da_winsys.o +libv3da-client.o)"

# --- 3. the stage-4 link, as sources/phoenix-rtos-ports/supertuxkart/port.def.sh does it
linkcmd="$(cat "$linktxt")"
case "$linkcmd" in
	*" -o bin/supertuxkart "*) ;;
	*) die "link.txt has no ' -o bin/supertuxkart ' to redirect -- the port recipe changed; update this script" ;;
esac
[ "$(grep -o ' -o bin/supertuxkart ' "$linktxt" | wc -l)" = 1 ] || die "link.txt names the output more than once"

relink() {   # $1 = libv3d archive, $2 = absolute output ELF
	# (via a variable: single quotes written inside a quoted ${x/p/r} replacement
	# are literal in bash 5.2, so '$2' would reach the linker unexpanded)
	local rep=" -o '$2' "
	local cmd="${linkcmd/ -o bin\/supertuxkart /$rep}"
	rm -f "$2"
	( cd "$stkbuild" && export PATH="${tcbin}:${PATH}" && eval "$cmd '${gluedir}/sdl_phoenix_glctx.o' '${gluedir}/sdl_phoenix_glstubs.o' \
		-Wl,--start-group '${pfx}/lib/libSDL2.a' '${gllib}' '$1' \
		'${pfx}/lib/libz.a' '${pfx}/lib/libogg.a' '${pfx}/lib/libvorbis.a' \
		'${pfx}/lib/libvorbisfile.a' '${pfx}/lib/libvorbisenc.a' \
		'${pfx}/lib/libmbedtls.a' '${pfx}/lib/libmbedx509.a' '${pfx}/lib/libmbedcrypto.a' \
		-Wl,--end-group -Wl,-z,stack-size=8388608" ) || die "link failed: $2"
	[ -f "$2" ] || die "link reported success but produced no ELF: $2"
	if "$readelf" -l "$2" 2>/dev/null | grep -q INTERP; then die "$2 has a PT_INTERP segment"; fi
}

control_note="not run (--no-control)"
if [ "$do_control" = 1 ]; then
	log "control relink with the UNMODIFIED archive (proves the recipe reproduces shipped)"
	relink "$shipped_lib" "$out/supertuxkart-control"
	if cmp -s "$out/supertuxkart-control" "$shipped_prog"; then
		control_note="byte-identical to shipped prog/supertuxkart"
		log "  PROOF: control relink == shipped prog/supertuxkart (byte-identical)"
	else
		control_note="DIFFERS from shipped prog/supertuxkart"
		warn "control relink differs from shipped prog/supertuxkart: the port tree or an input"
		warn "  archive changed since the shipped build. An A/B against the shipped stk then"
		warn "  compares more than the GPU layer -- rebuild the port (coordinator) first."
	fi
	rm -f "$out/supertuxkart-control"
fi

log "v3da relink"
relink "$lib" "$out/supertuxkart-$name"
"$strip" -o "$out/supertuxkart-$name.stripped" "$out/supertuxkart-$name"

# --- proofs on the clone -------------------------------------------------------------
syms="$("$nm" "$out/supertuxkart-$name")"
grep -qE ' T phoenix_v3d_ioctl$' <<<"$syms" || die "clone lacks T phoenix_v3d_ioctl"
grep -qE ' T drmSyncobjExportSyncFile$' <<<"$syms" || die "clone lacks the adapter's drmSyncobj surface"
grep -qE ' T v3da_connect$' <<<"$syms" || die "clone lacks T v3da_connect (libv3da-client not linked)"
grep -qE ' T v3d_phoenix_flip$' <<<"$syms" || die "clone lacks T v3d_phoenix_flip (present path)"
for s in "${old_syms[@]}"; do
	if grep -qE " [tTdDbB] ${s}$" <<<"$syms"; then die "clone still carries in-process winsys symbol $s"; fi
done
und_clone="$("$nm" -u "$out/supertuxkart-$name")"
und_ship="$("$nm" -u "$shipped_prog")"
[ "$und_clone" = "$und_ship" ] || die "undefined symbols differ from shipped: clone [$(tr '\n' ' ' <<<"$und_clone")] vs shipped [$(tr '\n' ' ' <<<"$und_ship")]"
n_und="$(grep -c . <<<"$und_clone" || true)"
log "  undefined symbols: clone $n_und, shipped $(grep -c . <<<"$und_ship" || true) (identical lists)"

# grep -a on the ELFs themselves (a `strings | grep -q` pipeline dies of SIGPIPE
# under pipefail; plain grep skips binaries without -a)
for t in "${new_tags[@]}" "v3d-winsys: flipstat"; do
	grep -aqF "$t" "$out/supertuxkart-$name.stripped" || die "clone lacks the adapter string '$t'"
done
for t in "${old_tags[@]}"; do
	if grep -aqF "$t" "$out/supertuxkart-$name.stripped"; then die "clone still carries the old winsys string '$t'"; fi
done
# inverse control: the shipped binary is the old lane
for t in "${new_tags[@]}"; do
	if grep -aqF "$t" "$shipped_bin"; then die "the SHIPPED supertuxkart carries '$t' -- a new-lane build leaked into the real build"; fi
done
grep -aqF "${old_tags[0]}" "$shipped_bin" || die "shipped supertuxkart lacks '${old_tags[0]}' -- the negative check would prove nothing"
log "  PROOF: clone = v3da adapter + libv3da-client, no in-process winsys; shipped = the reverse"

# --- 4. stk-v3da launcher ------------------------------------------------------------
lsrc="$out/src/stk-$name.c"
sed -e "s|\"/usr/bin/supertuxkart\"|\"/usr/bin/supertuxkart-$name\"|" \
    -e "s|\"stk: exec /usr/bin/supertuxkart\"|\"stk-$name: exec /usr/bin/supertuxkart-$name\"|" \
    -e "s|\"stk: DATADIR=|\"stk-$name: DATADIR=|" \
    "$launcher_src" > "$lsrc"
[ "$(grep -c "\"/usr/bin/supertuxkart-$name\"" "$lsrc")" = 1 ] || die "launcher exec path rewrite did not match exactly once"
[ "$(grep -c "\"stk-$name: DATADIR=" "$lsrc")" = 1 ] || die "launcher banner rewrite did not match exactly once"
[ "$(grep -c '/usr/bin/supertuxkart"' "$lsrc")" = 0 ] || die "launcher still names the shipped engine"
diff_lines="$(diff "$launcher_src" "$lsrc" | grep -c '^>' || true)"
[ "$diff_lines" = 3 ] || die "launcher differs from stk-launcher.c in $diff_lines lines (expected exactly 3)"
"$cc" -O2 -static -Wall -Wextra --sysroot="${sysroot}/" -B"${sysroot}/lib/" -iprefix "${sysroot}/" \
	-o "$out/stk-$name" "$lsrc" || die "launcher compile failed"
if "$readelf" -l "$out/stk-$name" 2>/dev/null | grep -q INTERP; then die "stk-$name has a PT_INTERP segment"; fi
grep -aqF "/usr/bin/supertuxkart-$name" "$out/stk-$name" || die "launcher ELF lacks its exec path"

# --- provenance -----------------------------------------------------------------
{
	echo "built:               $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "v3d-async git state: $( [ -n "$src_git" ] && echo "DIRTY" || echo "clean at $(git -C "$repo_root" rev-parse HEAD)")"
	[ -z "$src_git" ] || printf '%s\n' "$src_git" | sed 's/^/                     /'
	echo "V3DA_PROTO_VERSION:  $proto"
	echo "v3da_winsys.c:       $(sha "${here}/v3da_winsys.c")"
	echo "libv3da-client.c:    $(sha "${here}/libv3da-client.c")"
	echo "v3da_proto.h:        $(sha "${here}/v3da_proto.h")"
	echo "server (same build): $(sha "$out/v3da/rpi4-v3d-async")"
	echo "devices HEAD:        $(git -C "${repo_root}/sources/phoenix-rtos-devices" rev-parse HEAD)"
	echo "libv3d (shipped):    $(sha "$shipped_lib")"
	echo "libv3d (v3da):       $(sha "$lib")"
	echo "libGL:               $(sha "$gllib")"
	echo "libSDL2.a:           $(sha "$pfx/lib/libSDL2.a")"
	echo "libphoenix.a:        $(sha "$sysroot/lib/libphoenix.a")"
	echo "link.txt:            $(sha "$linktxt")"
	echo "control relink:      $control_note"
	echo "shipped prog:        $(sha "$shipped_prog")"
	echo "shipped stripped:    $(sha "$shipped_bin") ($(stat -c%s "$shipped_bin") B)"
	echo "v3da stripped:       $(sha "$out/supertuxkart-$name.stripped") ($(stat -c%s "$out/supertuxkart-$name.stripped") B)"
	echo "v3da unstripped:     $(stat -c%s "$out/supertuxkart-$name") B (shipped prog $(stat -c%s "$shipped_prog") B)"
	echo "stk-$name:            $(sha "$out/stk-$name") ($(stat -c%s "$out/stk-$name") B)"
	echo "shipped bin/stk:     $(sha "$shipped_launcher")"
} > "$out/BUILD-INFO.txt"

check_guarded
log "sizes: supertuxkart-$name.stripped $(stat -c%s "$out/supertuxkart-$name.stripped") B (shipped usr/bin/supertuxkart $(stat -c%s "$shipped_bin") B)"
log "       supertuxkart-$name $(stat -c%s "$out/supertuxkart-$name") B (shipped prog $(stat -c%s "$shipped_prog") B); stk-$name $(stat -c%s "$out/stk-$name") B"
log "done:"
log "  $out/supertuxkart-$name.stripped -> stage as /usr/bin/supertuxkart-$name"
log "  $out/stk-$name                   -> stage as /bin/stk-$name"
log "  $out/v3da/rpi4-v3d-async          -> the MATCHING server (protocol v$proto); stage as /bin/rpi4-v3d-async"
log "  $out/supertuxkart-$name          (unstripped, for addr2line)"
log "  $out/BUILD-INFO.txt"
