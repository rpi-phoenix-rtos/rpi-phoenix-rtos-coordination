#!/usr/bin/env bash
#
# Build a CLONED SuperTuxKart, `supertuxkart-prof`, whose in-process V3D winsys is
# compiled with -DV3D_PHX_SUBMIT_PROFILE (experiment E2: split the ~88% of the frame
# spent "inside submit"). The shipped /usr/bin/supertuxkart is never rebuilt or
# replaced -- C1 is layout-sensitive, so the shipped binary must stay the one the
# C1 series measures.
#
# HOW (the cheapest path that shares no output with the real build):
#   1. compile v3d_phoenix_winsys.c with the mesa port's OWN flags (imports
#      build-v3d-phoenix.py and calls its transform(), exactly as
#      scripts/syntax-check-v3d.sh does) plus -DV3D_PHX_SUBMIT_PROFILE;
#   2. copy tools/.gpu-libs/libv3d-phoenix.a to the output dir and replace ONE
#      member (v3d_phoenix_winsys.o) in the copy;
#   3. re-run the supertuxkart port's stage-4 link (CMake's link.txt + the SDL2 GL
#      glue objects + the same --start-group) from the port's existing build tree,
#      with the prof archive substituted and `-o` pointed at the output dir;
#   4. build a `stk-prof` launcher: stk-launcher.c with only its exec path and
#      banner rewritten (it must seed the same config.xml -- scale_rtts_factor=0.75
#      changes the render workload -- so running the engine bare is NOT equivalent).
#
# PROOFS it prints (and fails on):
#   * the edited winsys, compiled WITHOUT the macro, is byte-identical to a compile
#     of the pristine HEAD file (debug info stripped first if the flags carry -g);
#   * a CONTROL relink with the unmodified archive reproduces the shipped
#     usr/bin/supertuxkart byte-for-byte, so the prof binary differs from shipped
#     by the one instrumented object and nothing else (warning, not failure, if the
#     port tree has moved on since the shipped build);
#   * none of the guarded shared files changed (sha256 before == after):
#     tools/.gpu-libs/libv3d-phoenix.a (sync-netboot-tree.sh fingerprints it to
#     decide whether to wipe the shader cache), the shipped binaries, the INCR
#     object cache of build-v3d-phoenix.py, the port's link.txt.
#
# WRITES ONLY under $STKPROF_OUT (default artifacts/stkprof/). It does not touch
# /srv, the TFTP loader, .buildroot outputs, or tools/.gpu-libs.
#
# Usage: tools/gpu-lane/stkprof/build-stkprof.sh [--no-control] [--verify-only]
#   --no-control   skip the control relink (saves one ~40 s link)
#   --verify-only  only the byte-identity proof of the default build; no link
# Env:  STKPROF_OUT, TARGET (default aarch64a72-generic-rpi4b), RPI4B_BUILDROOT
#
# E2b extensions (docs/gpu-new-lane/E2b-v3d-render-slowness.md) -- all unset = E2 as before:
#   STKPROF_DEVICES  take v3d_phoenix_winsys.c from this phoenix-rtos-devices tree (a
#                    gpu-lane/* worktree) instead of sources/phoenix-rtos-devices. The
#                    compile FLAGS still come from the main tree's build-v3d-phoenix.py, and
#                    PROOF 1 compares against the file at merge-base(worktree HEAD, master).
#   STKPROF_MESA     a worktree of external/mesa: v3d_job.c, v3d_resource.c and v3dx_draw.c
#                    (the V3D_VERSION=42 variant) are compiled from it with
#                    -DV3D_PHX_JOB_NOTE -DV3D_PHX_RES_CENSUS -DV3D_PHX_EZ_KNOB and replace
#                    their members in the prof archive copy. Each gets the same proof as the
#                    winsys: worktree-without-macros == pristine external/mesa compile.
#   STKPROF_NAME     binary suffix (default "prof"): usr/bin/supertuxkart-$NAME + stk-$NAME,
#                    so an E2b build never overwrites the E2 clone on the export.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
target="${TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
out="${STKPROF_OUT:-${repo_root}/artifacts/stkprof}"
out="$(realpath -m "$out")"   # the compile steps run from other directories: a relative STKPROF_OUT broke them (2026-09-26)
tc="${repo_root}/.toolchain/aarch64-phoenix/bin"
cc="${tc}/aarch64-phoenix-gcc"
ar="${tc}/aarch64-phoenix-gcc-ar"
strip="${tc}/aarch64-phoenix-strip"
objcopy="${tc}/aarch64-phoenix-objcopy"
readelf="${tc}/aarch64-phoenix-readelf"

main_devices="${repo_root}/sources/phoenix-rtos-devices"
devices="${STKPROF_DEVICES:-${main_devices}}"
winsys_rel="gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c"
winsys="${devices}/${winsys_rel}"
builder="${main_devices}/gpu/rpi4-v3d/mesa/build-v3d-phoenix.py"   # flags: always the main tree's
mesa_main="${repo_root}/external/mesa"
mesa_wt="${STKPROF_MESA:-}"
name="${STKPROF_NAME:-prof}"
case "$name" in *[!a-z0-9]*|"") echo "build-stkprof: STKPROF_NAME must be [a-z0-9]+" >&2; exit 2 ;; esac
shipped_lib="${repo_root}/tools/.gpu-libs/libv3d-phoenix.a"
gllib="${repo_root}/tools/.gpu-libs/libGL-phoenix.a"
pfx="${buildroot}/_build/${target}"                       # the ports' shared install prefix
sysroot="${pfx}/sysroot"
stkbuild="${pfx}/port-sources/supertuxkart-1.4/stk-code-1.4/build"
linktxt="${stkbuild}/CMakeFiles/supertuxkart.dir/link.txt"
gluedir="${stkbuild}/_phoenix_glue"
shipped_bin="${buildroot}/_fs/${target}/root/usr/bin/supertuxkart"   # stripped, what gets staged
shipped_prog="${pfx}/prog/supertuxkart"                              # unstripped
launcher_src="${repo_root}/tools/supertuxkart-port/stk-launcher.c"
incr_obj="/tmp/v3dphx-drvobj/v3d_phoenix_winsys.o"
marker="SUBMIT PROFILE build"

do_control=1
verify_only=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--no-control) do_control=0 ;;
		--verify-only) verify_only=1 ;;
		-h|--help) sed -n '2,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-stkprof: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

log()  { printf '[stkprof] %s\n' "$*"; }
warn() { printf '[stkprof] WARNING: %s\n' "$*" >&2; }
die()  { printf '[stkprof] ERROR: %s\n' "$*" >&2; exit 1; }
sha()  { if [ -e "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else echo "absent"; fi; }

# --- preconditions (fail loud; never fall back) -------------------------------
for f in "$cc" "$ar" "$strip" "$objcopy" "$readelf" "$winsys" "$builder" "$shipped_lib" \
		"$sysroot/lib/libphoenix.a" "$launcher_src"; do
	[ -e "$f" ] || die "missing: $f"
done
[ -d /tmp/mesa-v3d-build ] || die "host Mesa build tree /tmp/mesa-v3d-build missing (build-v3d-phoenix.py needs it)"
if [ "$verify_only" = 0 ]; then
	for f in "$linktxt" "$gluedir/sdl_phoenix_glctx.o" "$gluedir/sdl_phoenix_glstubs.o" "$gllib" \
			"$pfx/lib/libSDL2.a" "$shipped_bin" "$shipped_prog"; do
		[ -e "$f" ] || die "missing: $f (has the supertuxkart port been built in this buildroot?)"
	done
fi

mkdir -p "$out/obj/prof" "$out/obj/plain" "$out/obj/pristine" "$out/obj/shipped" "$out/src"

# --- guard the shared files ---------------------------------------------------
guarded=("$shipped_lib" "$shipped_bin" "$shipped_prog" "$incr_obj" "$linktxt")
declare -A before
for f in "${guarded[@]}"; do before["$f"]="$(sha "$f")"; done

check_guarded() {
	local f bad=0
	for f in "${guarded[@]}"; do
		if [ "${before[$f]}" != "$(sha "$f")" ]; then
			printf '[stkprof] ERROR: shared file CHANGED during this build: %s\n' "$f" >&2
			bad=1
		fi
	done
	[ "$bad" = 0 ] || exit 1
	log "guarded shared files unchanged (${#guarded[@]} checked)"
}

# --- compile the winsys with the port's own flags -----------------------------
# $1 = source, $2 = output object, rest = extra flags. cwd=HOSTBUILD as the port does.
compile_winsys() {
	PHOENIX_RPI_ROOT="$repo_root" PY_BUILDER="$builder" PY_SRC="$1" PY_OUT="$2" python3 - "${@:3}" <<'PYEOF'
import sys
sys.dont_write_bytecode = True    # no __pycache__ in the devices repo
import importlib.util, os, subprocess
extra = sys.argv[1:]
spec = importlib.util.spec_from_file_location("bv", os.environ["PY_BUILDER"])
mod = importlib.util.module_from_spec(spec)
saved, sys.argv = sys.argv, ["bv"]
try:
    spec.loader.exec_module(mod)
finally:
    sys.argv = saved
cmd = mod.transform(mod.template_entry(), os.environ["PY_SRC"], os.environ["PY_OUT"]) + extra
r = subprocess.run(cmd, cwd=mod.HOSTBUILD, capture_output=True, text=True)
msg = (r.stderr or "") + (r.stdout or "")
if r.returncode != 0 or msg.strip():
    print("$ " + " ".join(cmd))
    print(msg.rstrip())
raise SystemExit(r.returncode)
PYEOF
}

# Strip what legitimately differs between two compiles of the same code from two
# paths (debug info, if any) and compare the rest.
same_code() {
	local a="$1" b="$2" ta tb
	if cmp -s "$a" "$b"; then echo "byte-identical"; return 0; fi
	ta="$(mktemp)"; tb="$(mktemp)"
	"$objcopy" --strip-debug "$a" "$ta"; "$objcopy" --strip-debug "$b" "$tb"
	if cmp -s "$ta" "$tb"; then rm -f "$ta" "$tb"; echo "identical after --strip-debug"; return 0; fi
	rm -f "$ta" "$tb"; echo "DIFFERENT"; return 1
}

# Compile one Mesa driver file with the flags of ITS OWN compile_commands entry (the entry
# is looked up by the pristine external/mesa path; $4, when set, is a token the entry must
# carry, e.g. -DV3D_VERSION=42 for the v42 variant of a v3dx_*.c file).
# $1 = mesa-relative path, $2 = source to compile, $3 = output object, $4 = required token.
compile_mesa() {
	PHOENIX_RPI_ROOT="$repo_root" PY_BUILDER="$builder" PY_REL="$1" PY_SRC="$2" PY_OUT="$3" \
	PY_NEED="${4:-}" python3 - "${@:5}" <<'PYEOF'
import sys
sys.dont_write_bytecode = True
import importlib.util, os, shlex, subprocess
extra = sys.argv[1:]
spec = importlib.util.spec_from_file_location("bv", os.environ["PY_BUILDER"])
mod = importlib.util.module_from_spec(spec)
saved, sys.argv = sys.argv, ["bv"]
try:
    spec.loader.exec_module(mod)
finally:
    sys.argv = saved
want = os.path.normpath(os.path.join(mod.MESA, os.environ["PY_REL"]))
need = os.environ["PY_NEED"]
cands = [e for e in mod.db if mod.abssrc(e["file"]) == want and
         (not need or need in shlex.split(e.get("command") or " ".join(e["arguments"])))]
if len(cands) != 1:
    print(f"compile_mesa: {len(cands)} compile_commands entries for {want} {need!r} (want 1)")
    raise SystemExit(2)
cmd = mod.transform(cands[0], os.environ["PY_SRC"], os.environ["PY_OUT"]) + extra
r = subprocess.run(cmd, cwd=mod.HOSTBUILD, capture_output=True, text=True)
msg = (r.stderr or "") + (r.stdout or "")
# Warnings located IN the compiled file matter in our (macro) compile; the shim headers'
# known DRM_SYNCOBJ_* redefinition warnings are pre-existing noise in every build.
import re
own = [l for l in msg.splitlines()
       if re.search(re.escape(os.environ["PY_SRC"]) + r":\d+:\d+: .*(warning|error)",
                    re.sub(r"\x1b\[[0-9;]*[mK]", "", l))]
if r.returncode != 0 or (extra and own):
    print("$ " + " ".join(cmd))
    print(msg.rstrip())
raise SystemExit(r.returncode)
PYEOF
}

if [ "$devices" = "$main_devices" ]; then
	pristine_ref="HEAD"
else
	pristine_ref="$(git -C "$devices" merge-base HEAD master)" || die "no merge-base of $devices HEAD and master"
fi
log "devices tree $devices (HEAD $(git -C "$devices" rev-parse --short HEAD), pristine ref $(git -C "$devices" rev-parse --short "$pristine_ref")); winsys diff: $(git -C "$devices" diff --stat "$pristine_ref" -- "$winsys_rel" | tail -1)"
git -C "$devices" show "${pristine_ref}:${winsys_rel}" > "$out/src/v3d_phoenix_winsys.c"   # same basename

log "compile: pristine HEAD winsys, no macro"
compile_winsys "$out/src/v3d_phoenix_winsys.c" "$out/obj/pristine/v3d_phoenix_winsys.o" || die "pristine compile failed"
log "compile: working-tree winsys, no macro (= what every default build ships)"
compile_winsys "$winsys" "$out/obj/plain/v3d_phoenix_winsys.o" || die "default compile failed"
log "compile: working-tree winsys, -DV3D_PHX_SUBMIT_PROFILE"
compile_winsys "$winsys" "$out/obj/prof/v3d_phoenix_winsys.o" -DV3D_PHX_SUBMIT_PROFILE || die "profile compile failed"
( cd "$out/obj/shipped" && "$ar" x "$shipped_lib" v3d_phoenix_winsys.o ) || die "no v3d_phoenix_winsys.o member in $shipped_lib"

log "PROOF 1 -- default build unchanged: working tree (no macro) vs pristine HEAD:"
v="$(same_code "$out/obj/plain/v3d_phoenix_winsys.o" "$out/obj/pristine/v3d_phoenix_winsys.o")" \
	|| die "working-tree winsys WITHOUT the macro differs from HEAD ($v) -- the default build would change"
log "  $v"
log "PROOF 1b -- that object vs the member inside the shipped libv3d-phoenix.a:"
if v="$(same_code "$out/obj/plain/v3d_phoenix_winsys.o" "$out/obj/shipped/v3d_phoenix_winsys.o")"; then
	log "  $v"
else
	warn "shipped archive member differs from a compile of the current source ($v):"
	warn "  the archive predates the current devices HEAD (or another agent's edit)."
	warn "  The prof binary is then NOT 'shipped + instrumentation'; rebuild the GPU libs first."
fi
v="$(same_code "$out/obj/prof/v3d_phoenix_winsys.o" "$out/obj/plain/v3d_phoenix_winsys.o" || true)"
[ "$v" = "DIFFERENT" ] || die "the -DV3D_PHX_SUBMIT_PROFILE object equals the default one -- the macro did nothing"
[ "$(strings -a "$out/obj/prof/v3d_phoenix_winsys.o" | grep -c "$marker")" -ge 1 ] || die "profile object lacks '$marker'"
[ "$(strings -a "$out/obj/plain/v3d_phoenix_winsys.o" | grep -c "$marker")" = 0 ] || die "default object carries '$marker'"
log "  profile object differs and carries '$marker'; default object does not"

# --- E2b: instrumented Mesa objects from a worktree (STKPROF_MESA) ---------------
mesa_members=""
if [ -n "$mesa_wt" ]; then
	[ -d "$mesa_wt/src/gallium/drivers/v3d" ] || die "STKPROF_MESA=$mesa_wt is not a Mesa tree"
	[ -z "$(git -C "$mesa_main" status --porcelain -- src/gallium/drivers/v3d)" ] \
		|| die "external/mesa has uncommitted v3d changes -- the pristine compile would not be HEAD"
	mesa_macros="-DV3D_PHX_JOB_NOTE -DV3D_PHX_RES_CENSUS -DV3D_PHX_EZ_KNOB"
	mkdir -p "$out/obj/mesa-pristine" "$out/obj/mesa-plain" "$out/obj/mesa-instr" "$out/obj/mesa-shipped"
	# rel-path | archive member | required compile_commands token | marker string in the instr object
	for spec in \
		"src/gallium/drivers/v3d/v3d_job.c|v3d_job.c.o||v3d-job-note" \
		"src/gallium/drivers/v3d/v3d_resource.c|v3d_resource.c.o||v3d-res-census" \
		"src/gallium/drivers/v3d/v3dx_draw.c|v3dx_draw.c_v42.o|-DV3D_VERSION=42|V3D_PHX_EZ"; do
		IFS='|' read -r rel member need mark <<< "$spec"
		log "Mesa: $rel -> $member"
		compile_mesa "$rel" "$mesa_main/$rel" "$out/obj/mesa-pristine/$member" "$need" || die "pristine compile of $rel failed"
		compile_mesa "$rel" "$mesa_wt/$rel" "$out/obj/mesa-plain/$member" "$need" || die "worktree compile of $rel failed"
		compile_mesa "$rel" "$mesa_wt/$rel" "$out/obj/mesa-instr/$member" "$need" $mesa_macros || die "instrumented compile of $rel failed"
		v="$(same_code "$out/obj/mesa-plain/$member" "$out/obj/mesa-pristine/$member")" \
			|| die "PROOF M1 $member: worktree WITHOUT macros differs from pristine external/mesa ($v)"
		log "  PROOF M1 (worktree, no macros == pristine): $v"
		( cd "$out/obj/mesa-shipped" && "$ar" x "$shipped_lib" "$member" ) || die "no $member in $shipped_lib"
		if v="$(same_code "$out/obj/mesa-pristine/$member" "$out/obj/mesa-shipped/$member")"; then
			log "  PROOF M1b (pristine == shipped archive member): $v"
		else
			warn "$member: shipped archive member differs from a compile of external/mesa HEAD ($v):"
			warn "  the archive predates the Mesa HEAD; the instrumented binary is then not 'shipped + hooks'."
		fi
		[ "$(same_code "$out/obj/mesa-instr/$member" "$out/obj/mesa-plain/$member" || true)" = "DIFFERENT" ] \
			|| die "$member: the macros changed nothing"
		[ "$(strings -a "$out/obj/mesa-instr/$member" | grep -c "$mark")" -ge 1 ] || die "$member lacks marker '$mark'"
		[ "$(strings -a "$out/obj/mesa-plain/$member" | grep -c "$mark")" = 0 ] || die "$member default object carries '$mark'"
		mesa_members="$mesa_members $member"
	done
fi

if [ "$verify_only" = 1 ]; then
	check_guarded
	log "verify-only: done"
	exit 0
fi

# --- the prof archive: a COPY with one member replaced --------------------------
prof_lib="$out/libv3d-phoenix-prof.a"
cp "$shipped_lib" "$prof_lib"
"$ar" r "$prof_lib" "$out/obj/prof/v3d_phoenix_winsys.o"
n_ship="$("$ar" t "$shipped_lib" | wc -l)"; n_prof="$("$ar" t "$prof_lib" | wc -l)"
[ "$n_ship" = "$n_prof" ] || die "member count changed ($n_ship -> $n_prof): the replace appended instead of replacing"
[ "$(strings -a "$prof_lib" | grep -c "$marker")" -ge 1 ] || die "prof archive lacks '$marker'"
log "prof archive: $prof_lib ($n_prof members, winsys replaced)"
if [ -n "$mesa_wt" ]; then
	for m in $mesa_members; do
		"$ar" r "$prof_lib" "$out/obj/mesa-instr/$m" || die "replacing $m failed"
	done
	[ "$("$ar" t "$prof_lib" | wc -l)" = "$n_ship" ] || die "member count changed after the Mesa replacements"
	log "  + Mesa members replaced: $mesa_members"
fi

# --- the stage-4 link, as sources/phoenix-rtos-ports/supertuxkart/port.def.sh does it
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
	( cd "$stkbuild" && eval "$cmd '${gluedir}/sdl_phoenix_glctx.o' '${gluedir}/sdl_phoenix_glstubs.o' \
		-Wl,--start-group '${pfx}/lib/libSDL2.a' '${gllib}' '$1' \
		'${pfx}/lib/libz.a' '${pfx}/lib/libogg.a' '${pfx}/lib/libvorbis.a' \
		'${pfx}/lib/libvorbisfile.a' '${pfx}/lib/libvorbisenc.a' \
		'${pfx}/lib/libmbedtls.a' '${pfx}/lib/libmbedx509.a' '${pfx}/lib/libmbedcrypto.a' \
		-Wl,--end-group -Wl,-z,stack-size=8388608" ) || die "link failed: $2"
	[ -f "$2" ] || die "link reported success but produced no ELF: $2"
	if "$readelf" -l "$2" 2>/dev/null | grep -q INTERP; then die "$2 has a PT_INTERP segment"; fi
}

if [ "$do_control" = 1 ]; then
	log "control relink with the UNMODIFIED archive (proves the recipe reproduces shipped)"
	relink "$shipped_lib" "$out/supertuxkart-control"
	if cmp -s "$out/supertuxkart-control" "$shipped_prog"; then
		log "  PROOF 2: control relink == shipped prog/supertuxkart (byte-identical)"
	else
		warn "control relink differs from shipped prog/supertuxkart: the port tree or an input"
		warn "  archive changed since the shipped build. The prof binary then differs from shipped"
		warn "  by more than the winsys -- rebuild the port (coordinator) before an A/B vs shipped."
	fi
	rm -f "$out/supertuxkart-control"
fi

log "prof relink"
relink "$prof_lib" "$out/supertuxkart-$name"
"$strip" -o "$out/supertuxkart-$name.stripped" "$out/supertuxkart-$name"
[ "$(strings -a "$out/supertuxkart-$name.stripped" | grep -c "$marker")" -ge 1 ] || die "prof ELF lacks '$marker'"
[ "$(strings -a "$shipped_bin" | grep -c "$marker")" = 0 ] || die "the SHIPPED supertuxkart carries '$marker' -- a profile build leaked into the real build"
log "  prof ELF carries '$marker'; shipped usr/bin/supertuxkart does not"

# --- stk-prof launcher -----------------------------------------------------------
lsrc="$out/src/stk-$name.c"
sed -e "s|\"/usr/bin/supertuxkart\"|\"/usr/bin/supertuxkart-$name\"|" \
    -e "s|\"stk: exec /usr/bin/supertuxkart\"|\"stk-$name: exec /usr/bin/supertuxkart-$name\"|" \
    -e "s|\"stk: DATADIR=|\"stk-$name: DATADIR=|" \
    "$launcher_src" > "$lsrc"
[ "$(grep -c "\"/usr/bin/supertuxkart-$name\"" "$lsrc")" = 1 ] || die "launcher exec path rewrite did not match exactly once"
[ "$(grep -c "\"stk-$name: DATADIR=" "$lsrc")" = 1 ] || die "launcher banner rewrite did not match exactly once"
[ "$(grep -c '/usr/bin/supertuxkart"' "$lsrc")" = 0 ] || die "launcher still names the shipped engine"
"$cc" -O2 -static -Wall -Wextra --sysroot="${sysroot}/" -B"${sysroot}/lib/" -iprefix "${sysroot}/" \
	-o "$out/stk-$name" "$lsrc" || die "launcher compile failed"
if "$readelf" -l "$out/stk-$name" 2>/dev/null | grep -q INTERP; then die "stk-$name has a PT_INTERP segment"; fi

# --- provenance -----------------------------------------------------------------
{
	echo "built:            $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "devices tree:     $devices"
	echo "devices HEAD:     $(git -C "$devices" rev-parse HEAD) (pristine ref $(git -C "$devices" rev-parse "$pristine_ref"))"
	echo "winsys (tree):    $(sha "$winsys")"
	if [ -n "$mesa_wt" ]; then
		echo "mesa worktree:    $mesa_wt (HEAD $(git -C "$mesa_wt" rev-parse HEAD))"
		for m in $mesa_members; do echo "mesa instr $m: $(sha "$out/obj/mesa-instr/$m")"; done
	fi
	echo "libv3d (shipped): $(sha "$shipped_lib")"
	echo "libGL:            $(sha "$gllib")"
	echo "libphoenix.a:     $(sha "$sysroot/lib/libphoenix.a")"
	echo "link.txt:         $(sha "$linktxt")"
	echo "shipped stripped: $(sha "$shipped_bin")"
	echo "prof stripped:    $(sha "$out/supertuxkart-$name.stripped")"
	echo "stk-$name:         $(sha "$out/stk-$name")"
} > "$out/BUILD-INFO.txt"

check_guarded
log "done:"
log "  $out/supertuxkart-$name.stripped  -> stage as /usr/bin/supertuxkart-$name"
log "  $out/supertuxkart-$name           (unstripped, for addr2line)"
log "  $out/stk-$name                    -> stage as /bin/stk-$name"
log "  $out/BUILD-INFO.txt              (input SHAs; libphoenix drift confounds an A/B vs shipped)"
