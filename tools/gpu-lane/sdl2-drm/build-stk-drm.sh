#!/usr/bin/env bash
#
# Build `supertuxkart-drm` + its launcher `stk-drm`: a CLONE of SuperTuxKart 1.4 on the FULL
# standard DRM stack -- SDL 2.30.12's stock KMSDRM video driver (tools/gpu-lane/sdl2-drm),
# Mesa 26.2 GBM + EGL + GLES (the sdl2-drm Mesa build, patch set 0001-0009) and
# libdrm-phoenix (build-out-m5b: the G15 ioctl interposer) -> rpi4-kms (card0) +
# rpi4-v3d-async (renderD128). The shipped /usr/bin/supertuxkart, /bin/stk, the ports/sdl2
# port, the old lane's Mesa fork and tools/.gpu-libs are never touched (PLAN rule 2; STK is
# also the C1 workload, which is layout-sensitive).
#
# HOW (build-stk-v3da.sh's template: re-run STK's own final link with swapped inputs):
#   1. snapshot libdrm-phoenix m5b (include/ + libdrm.a) into $OUT/libdrm-prefix;
#   2. compile stkdrm/stkdrm_hooks.c (banner, SDL VIDEO/INPUT DEBUG logging, and the
#      `stk-drm flipstat` frame counter behind -Wl,--wrap=SDL_GL_SwapWindow -- in the clone
#      only, so libSDL2.a and quakespasm-drm stay byte-identical);
#   3. re-run the supertuxkart port's stage-4 link (CMake's link.txt, as
#      sources/phoenix-rtos-ports/supertuxkart/port.def.sh does) from the port's existing
#      build tree, with: the ports-prefix libSDL2.a (old /dev/fb0 video driver) replaced by
#      the KMSDRM libSDL2.a; the old GL-context glue objects (sdl_phoenix_glctx.o /
#      sdl_phoenix_glstubs.o) and libGL-phoenix.a / libv3d-phoenix.a NOT linked; instead
#      the quakespasm-drm/kmscube Mesa link shape (libgallium whole-archive + one group of
#      EGL/GBM/dri_gbm/GLESv2/glapi/v3d/broadcom/winsys/util + libdrm.a + the compat shim)
#      and -static -Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,--wrap=SDL_GL_SwapWindow;
#   4. the `stk-drm` launcher: tools/supertuxkart-port/stk-launcher.c with only its exec
#      path and two message prefixes rewritten -- the same default args and the same seeded
#      config.xml (scale_rtts_factor=0.75 sets the render workload; show_fps on).
#
# STK is a GLES program (the port configures -DUSE_GLES2=ON; Irrlicht asks SDL for an ES 3.0
# context and loads every gl* through glad + SDL_GL_GetProcAddress = eglGetProcAddress), so
# no desktop-GL entry points are needed; the Mesa used is the sdl2-drm one because libSDL2.a
# was configured against it (it also has GLES2 on). Its objects were compiled against the
# m3p3 libdrm-phoenix headers, which are byte-identical to m5b's (checked below).
#
# PROOFS it prints (and fails on):
#   * libSDL2.a appears exactly once in link.txt and is substituted; the final command names
#     no old-lane input (libGL-phoenix, libv3d-phoenix, the glue objects, the ports SDL);
#   * a CONTROL relink (build-stk-v3da.sh's recipe verbatim, old-lane inputs) reproduces the
#     shipped prog/supertuxkart byte for byte (warning only, if the port tree moved on);
#   * nm -u of the clone is EMPTY; no PT_INTERP;
#   * new-stack symbols present (KMSDRM, SDL EGL, Phoenix HID, GBM/kmsro/v3d, libdrm-phoenix
#     incl. __wrap_ioctl = m5b), old-lane symbols absent (SDL phoenix video driver, phxgl,
#     in-process/v3da winsys);
#   * the wrap intercepts: Irrlicht's COGLES2Driver calls __wrap_SDL_GL_SwapWindow; only
#     __wrap_ioctl calls ioctl and only __wrap_mmap calls mmap (objdump);
#   * new-stack strings present, old-lane strings absent -- and the shipped binary the
#     reverse (inverse control);
#   * no global symbol is defined both by STK's own link inputs and by the new stack's
#     archives (a silent duplicate would bind one side to the other's code);
#   * the guarded shared inputs are unchanged afterwards.
#
# WRITES ONLY under $STKDRM_OUT (default tools/gpu-lane/sdl2-drm/build-out/stk-drm/). Reads the
# sdl2-drm build-out (libSDL2.a + mesa-gl), libdrm-phoenix build-out-m5b, the STK port build
# tree, the toolchain and tools/.gpu-libs (control relink only). It does NOT run
# sdl2-drm/build.sh or mesa-drm/build.sh, and touches no /srv, TFTP loader, .buildroot output
# or sources/.
#
# Usage: tools/gpu-lane/sdl2-drm/build-stk-drm.sh [--no-control] [--libdrm-prefix <dir>]
#   --no-control      skip the control relink (saves one ~40 s link)
#   --libdrm-prefix   libdrm-phoenix prefix (default tools/gpu-lane/libdrm-phoenix/build-out-m5b/prefix)
# Env:   STKDRM_OUT, TARGET (default aarch64a72-generic-rpi4b), RPI4B_BUILDROOT
# Stage (coordinator only; the live export is the fsid=0 one):
#   install -m 755 $OUT/supertuxkart-drm.stripped <export>/usr/bin/supertuxkart-drm
#   install -m 755 $OUT/stk-drm                   <export>/bin/stk-drm
#   (docs/gpu-new-lane/M3-libdrm-phoenix.md, "STK on the full DRM stack (stk-drm)")
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${here}/../../.." && pwd)"
target="${TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
out="$(realpath -m "${STKDRM_OUT:-${here}/build-out/stk-drm}")"
libdrm_src="${repo_root}/tools/gpu-lane/libdrm-phoenix/build-out-m5b/prefix"
tcbin="${repo_root}/.toolchain/aarch64-phoenix/bin"
cc="${tcbin}/aarch64-phoenix-gcc"
nm="${tcbin}/aarch64-phoenix-nm"
strip="${tcbin}/aarch64-phoenix-strip"
readelf="${tcbin}/aarch64-phoenix-readelf"
objdump="${tcbin}/aarch64-phoenix-objdump"
size="${tcbin}/aarch64-phoenix-size"

do_control=1
while [ "$#" -gt 0 ]; do
	case "$1" in
		--no-control) do_control=0 ;;
		--libdrm-prefix) shift; libdrm_src="${1:?--libdrm-prefix needs a directory}" ;;
		--libdrm-prefix=*) libdrm_src="${1#--libdrm-prefix=}" ;;
		-h|--help) sed -n '2,62p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-stk-drm: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
libdrm_src="$(realpath -m "${libdrm_src}")"

pfx="${buildroot}/_build/${target}"                        # the ports' shared install prefix
sysroot="${pfx}/sysroot"
stkbuild="${pfx}/port-sources/supertuxkart-1.4/stk-code-1.4/build"
linktxt="${stkbuild}/CMakeFiles/supertuxkart.dir/link.txt"
gluedir="${stkbuild}/_phoenix_glue"
old_sdl="${pfx}/lib/libSDL2.a"
old_v3d="${repo_root}/tools/.gpu-libs/libv3d-phoenix.a"
old_gl="${repo_root}/tools/.gpu-libs/libGL-phoenix.a"
shipped_prog="${pfx}/prog/supertuxkart"                                # unstripped
shipped_bin="${buildroot}/_fs/${target}/root/usr/bin/supertuxkart"     # stripped, what gets staged
shipped_launcher="${buildroot}/_fs/${target}/root/bin/stk"
launcher_src="${repo_root}/tools/supertuxkart-port/stk-launcher.c"
hooks_src="${here}/stkdrm/stkdrm_hooks.c"
SD="${here}/build-out"                    # the sdl2-drm build (read-only here)
SP="${SD}/sdl-prefix"
SDL_A="${SP}/lib/libSDL2.a"
M="${SD}/mesa-gl"
MB="${M}/mesa-build"
COMPAT_A="${M}/compat/libmesadrm-compat.a"
name="drm"
elf="${out}/supertuxkart-${name}"

log()  { printf '[stk-drm] %s\n' "$*"; }
warn() { printf '[stk-drm] WARNING: %s\n' "$*" >&2; }
die()  { printf '[stk-drm] ERROR: %s\n' "$*" >&2; exit 1; }
sha()  { if [ -e "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else echo "absent"; fi; }

# --- never write into the sdl2-drm build's own files --------------------------------------
case "$out" in
	"${SD}"|"${SD}"/sdl-*|"${SD}"/mesa-gl|"${SD}"/mesa-gl/*|"${SD}"/qs-*|"${SD}"/pkg-config-sdl)
		die "refusing output dir $out: it belongs to sdl2-drm/build.sh" ;;
esac

# --- preconditions (fail loud; never fall back) -------------------------------------------
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${sysroot}/" -B"${sysroot}/lib/")
A=(src/egl/libEGL.a src/gbm/libgbm.a src/gbm/backends/dri/dri_gbm.a src/mesa/glapi/es2api/libGLESv2.a
	src/mesa/glapi/shared-glapi/libglapi.a src/gallium/drivers/v3d/libv3d.a
	src/gallium/drivers/v3d/libv3d-v42.a src/gallium/drivers/v3d/libv3d-v71.a
	src/broadcom/libbroadcom-v42.a src/broadcom/libbroadcom-v71.a src/broadcom/qpu/libbroadcom_qpu.a
	src/broadcom/libv3d_neon.a src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a
	src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a src/gallium/winsys/kmsro/drm/libkmsrowinsys.a
	src/gallium/winsys/v3d/drm/libv3dwinsys.a src/gallium/winsys/vc4/drm/libvc4winsys.a
	src/gallium/winsys/sw/kms-dri/libswkmsdri.a src/gallium/winsys/sw/dri/libswdri.a
	src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a
	src/c11/impl/libmesa_util_c11.a)
for f in "$cc" "$nm" "$strip" "$readelf" "$objdump" "$size" "$linktxt" "$old_sdl" "$shipped_prog" "$shipped_bin" \
		"$sysroot/lib/libphoenix.a" "$launcher_src" "$hooks_src" "$SDL_A" "$SP/include/SDL2/SDL_config.h" \
		"$COMPAT_A" "${libdrm_src}/lib/libdrm.a" "${libdrm_src}/include/xf86drm.h" "${M}/libdrm-prefix/include/xf86drm.h" \
		"${pfx}/lib/libz.a" "${pfx}/lib/libmbedtls.a"; do
	[ -e "$f" ] || die "missing: $f (STK port built? sdl2-drm/build.sh run? libdrm-phoenix m5b built?)"
done
for a in "${A[@]}"; do
	[ -f "${MB}/${a}" ] || die "missing Mesa archive ${MB}/${a} (sdl2-drm's mesa-gl build incomplete)"
done
GALLIUM_A="$(ls "${MB}"/src/gallium/targets/dri/libgallium-*.a)"
[ -f "${GALLIUM_A}" ] || die "no libgallium-*.a in ${MB}"
cfg="${SP}/include/SDL2/SDL_config.h"
for d in SDL_VIDEO_DRIVER_KMSDRM SDL_VIDEO_OPENGL_EGL SDL_VIDEO_OPENGL_ES2 SDL_INPUT_PHOENIX SDL_AUDIO_DRIVER_PHOENIX; do
	grep -qE "^#define ${d} +1" "${cfg}" || die "sdl2-drm's SDL_config.h lacks ${d} 1"
done
for d in SDL_VIDEO_DRIVER_KMSDRM_DYNAMIC SDL_VIDEO_DRIVER_PHOENIX SDL_LOADSO_DLOPEN; do
	if grep -qE "^#define ${d}( |$)" "${cfg}"; then die "sdl2-drm's SDL_config.h defines ${d}"; fi
done
# Mesa was compiled against its own libdrm-phoenix snapshot's headers; linking another
# libdrm.a is exact only if the headers are identical.
diff -r "${M}/libdrm-prefix/include" "${libdrm_src}/include" > /dev/null \
	|| die "libdrm-phoenix headers of ${libdrm_src} differ from the ones Mesa (${M}/libdrm-prefix) was built with -- rebuild Mesa on it"
grep -qE ' T __wrap_ioctl$' <<< "$("$nm" -g --defined-only "${libdrm_src}/lib/libdrm.a" 2>/dev/null)" \
	|| die "${libdrm_src}/lib/libdrm.a has no __wrap_ioctl (older than m5b)"

guarded=("$old_sdl" "$old_gl" "$old_v3d" "$shipped_prog" "$shipped_bin" "$shipped_launcher" "$linktxt"
	"$gluedir/sdl_phoenix_glctx.o" "$gluedir/sdl_phoenix_glstubs.o" "$SDL_A" "$GALLIUM_A" "${libdrm_src}/lib/libdrm.a")
declare -A before
for f in "${guarded[@]}"; do before["$f"]="$(sha "$f")"; done
check_guarded() {
	local f bad=0
	for f in "${guarded[@]}"; do
		if [ "${before[$f]}" != "$(sha "$f")" ]; then
			printf '[stk-drm] ERROR: shared file CHANGED during this build: %s\n' "$f" >&2
			bad=1
		fi
	done
	[ "$bad" = 0 ] || exit 1
	log "guarded shared files unchanged (${#guarded[@]} checked)"
}

mkdir -p "$out/src" "$out/obj"
rm -f "$out/trial-link.sh" "$out/trial-link.log" "$out/trial.elf"

# --- 1. libdrm-phoenix snapshot -------------------------------------------------------------
LDP="$out/libdrm-prefix"
rm -rf "$LDP"
mkdir -p "$LDP/lib"
cp -a "${libdrm_src}/include" "$LDP/"
cp -a "${libdrm_src}/lib/libdrm.a" "$LDP/lib/"
log "libdrm-phoenix: $(sha "$LDP/lib/libdrm.a" | cut -c1-16) from ${libdrm_src} (headers = Mesa's m3p3 snapshot, identical)"

# --- 2. the clone's hooks object -----------------------------------------------------------
hooks_o="$out/obj/stkdrm_hooks.o"
"$cc" -O2 -g -std=gnu17 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SP}/include" -c "$hooks_src" -o "$hooks_o" \
	|| die "stkdrm_hooks.c compile failed"

# --- 3. the stage-4 link --------------------------------------------------------------------
linkcmd="$(cat "$linktxt")"
[ "$(grep -o ' -o bin/supertuxkart ' "$linktxt" | wc -l)" = 1 ] \
	|| die "link.txt must name ' -o bin/supertuxkart ' exactly once -- the port recipe changed; update this script"
[ "$(grep -oF " ${old_sdl} " "$linktxt" | wc -l)" = 1 ] \
	|| die "link.txt must name ${old_sdl} exactly once -- the port recipe changed; update this script"

relink_control() {   # build-stk-v3da.sh's relink, verbatim: the shipped inputs, $1 = output ELF
	local rep=" -o '$1' "
	local cmd="${linkcmd/ -o bin\/supertuxkart /$rep}"
	rm -f "$1"
	( cd "$stkbuild" && export PATH="${tcbin}:${PATH}" && eval "$cmd '${gluedir}/sdl_phoenix_glctx.o' '${gluedir}/sdl_phoenix_glstubs.o' \
		-Wl,--start-group '${pfx}/lib/libSDL2.a' '${old_gl}' '${old_v3d}' \
		'${pfx}/lib/libz.a' '${pfx}/lib/libogg.a' '${pfx}/lib/libvorbis.a' \
		'${pfx}/lib/libvorbisfile.a' '${pfx}/lib/libvorbisenc.a' \
		'${pfx}/lib/libmbedtls.a' '${pfx}/lib/libmbedx509.a' '${pfx}/lib/libmbedcrypto.a' \
		-Wl,--end-group -Wl,-z,stack-size=8388608" ) || die "control link failed"
}

control_note="not run (--no-control)"
if [ "$do_control" = 1 ]; then
	for f in "$old_gl" "$old_v3d" "$gluedir/sdl_phoenix_glctx.o" "$gluedir/sdl_phoenix_glstubs.o"; do
		[ -e "$f" ] || die "missing control input $f (or run with --no-control)"
	done
	log "control relink with the SHIPPED inputs (proves the recipe reproduces shipped)"
	relink_control "$out/supertuxkart-control"
	if cmp -s "$out/supertuxkart-control" "$shipped_prog"; then
		control_note="byte-identical to shipped prog/supertuxkart"
		log "  PROOF: control relink == shipped prog/supertuxkart (byte-identical)"
	else
		control_note="DIFFERS from shipped prog/supertuxkart"
		warn "control relink differs from shipped prog/supertuxkart: the port tree or an input archive"
		warn "  changed since the shipped build, so the clone's engine objects may not be the shipped ones."
	fi
	rm -f "$out/supertuxkart-control"
fi

AA=""
for a in "${A[@]}"; do AA="${AA} '${MB}/${a}'"; done
cmd="${linkcmd/ -o bin\/supertuxkart / -o '${elf}' }"
cmd="${cmd/ ${old_sdl} / '${SDL_A}' }"
cmd="${cmd} '${hooks_o}' -static -Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,--wrap=SDL_GL_SwapWindow -Wl,-Map,'${elf}.map' \
	-Wl,--whole-archive '${GALLIUM_A}' -Wl,--no-whole-archive \
	-Wl,--start-group '${SDL_A}'${AA} '${LDP}/lib/libdrm.a' '${COMPAT_A}' \
	'${pfx}/lib/libz.a' '${pfx}/lib/libogg.a' '${pfx}/lib/libvorbis.a' \
	'${pfx}/lib/libvorbisfile.a' '${pfx}/lib/libvorbisenc.a' \
	'${pfx}/lib/libmbedtls.a' '${pfx}/lib/libmbedx509.a' '${pfx}/lib/libmbedcrypto.a' \
	-Wl,--end-group -lm -Wl,-z,stack-size=8388608"
for bad in "${old_sdl}" libGL-phoenix libv3d-phoenix sdl_phoenix_glctx sdl_phoenix_glstubs " -o bin/supertuxkart "; do
	case "$cmd" in *"$bad"*) die "the stk-drm link command still names '$bad'" ;; esac
done
printf '%s\n' "$cmd" > "$out/link-cmd.txt"
log "stk-drm relink (KMSDRM SDL + Mesa GBM/EGL/GLES + libdrm-phoenix)"
rm -f "$elf"
( cd "$stkbuild" && export PATH="${tcbin}:${PATH}" && eval "$cmd" ) > "$out/link.log" 2>&1 \
	|| { head -60 "$out/link.log" >&2; die "stk-drm link failed"; }
[ -f "$elf" ] || die "link reported success but produced no ELF"
[ -s "$out/link.log" ] && sed 's/^/[stk-drm]   link: /' "$out/link.log" | head -20
"$strip" -o "$elf.stripped" "$elf"

# --- proofs on the clone --------------------------------------------------------------------
bad=0
if "$readelf" -l "$elf" | grep -q INTERP; then log "  PT_INTERP present"; bad=1; fi
und="$("$nm" -u "$elf" || true)"
log "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && { sed 's/^/[stk-drm]     /' <<< "${und}" | head -20; bad=1; }
syms="$("$nm" "$elf")"
for s in KMSDRM_CreateDevice KMSDRM_GLES_SwapWindow KMSDRM_GetWindowWMInfo SDL_EGL_LoadLibrary SDL_PHOENIX_HID_Poll \
		SDL_GL_SwapWindow __wrap_SDL_GL_SwapWindow __wrap_mmap __wrap_ioctl drmPhoenixMmap drm_phoenix_ioctl \
		gbmint_get_backend kmsro_drm_screen_create v3d_drm_screen_create_renderonly eglGetPlatformDisplayEXT \
		eglGetProcAddress _mesa_glapi_get_proc_address; do
	if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then log "  symbol ${s}: yes"; else log "  symbol ${s}: NO"; bad=1; fi
done
forbidden="$(grep -E ' [TtWwDdBbRr] (PHOENIX_bootstrap|PHOENIX_PumpEvents|PHOENIX_GL_[A-Za-z_]*|phxgl_[A-Za-z_]*|phoenix_v3d_ioctl|winsys_init|boPool_take|mboxProp|v3da_connect|v3d_phoenix_flip)$' <<< "${syms}" || true)"
if [ -n "${forbidden}" ]; then log "  forbidden (old-lane) symbols PRESENT:"; sed 's/^/[stk-drm]     /' <<< "${forbidden}"; bad=1
else log "  old-lane symbols (SDL phoenix video, phxgl/PHOENIX_GL glue, in-process + v3da winsys): none"; fi

# objdump: who calls what (one pass over the text)
calls="$("$objdump" -d --no-show-raw-insn "$elf" | awk '
	/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn); next }
	/\tbl?\t/ && / <(ioctl|mmap|__wrap_SDL_GL_SwapWindow|SDL_GL_SwapWindow|__real_SDL_GL_SwapWindow)>$/ {
		t = $NF; gsub(/[<>]/, "", t); print t, fn }' | sort | uniq -c)"
printf '%s\n' "$calls" > "$out/call-sites.txt"
awk '$2 == "ioctl" && $3 != "__wrap_ioctl" { bad = 1 } END { exit bad }' <<< "$calls" \
	|| { log "  real ioctl() called from outside __wrap_ioctl:"; awk '$2 == "ioctl"' <<< "$calls" | sed 's/^/[stk-drm]     /'; bad=1; }
awk '$2 == "mmap" && $3 != "__wrap_mmap" { bad = 1 } END { exit bad }' <<< "$calls" \
	|| { log "  real mmap() called from outside __wrap_mmap:"; awk '$2 == "mmap"' <<< "$calls" | sed 's/^/[stk-drm]     /'; bad=1; }
if grep -qE ' __wrap_SDL_GL_SwapWindow _ZN3irr5video13COGLES2Driver' <<< "$calls" \
		&& ! grep -qE ' SDL_GL_SwapWindow _ZN3irr' <<< "$calls" \
		&& grep -qE ' SDL_GL_SwapWindow __wrap_SDL_GL_SwapWindow$' <<< "$calls"; then
	log "  PROOF: COGLES2Driver -> __wrap_SDL_GL_SwapWindow -> SDL_GL_SwapWindow (frame counter in the path)"
else
	log "  the SDL_GL_SwapWindow wrap is NOT in Irrlicht's swap path:"; sed 's/^/[stk-drm]     /' <<< "$calls"; bad=1
fi
log "  ioctl/mmap: only __wrap_ioctl / __wrap_mmap call the real ones ($out/call-sites.txt)"
# the frame-pacing order of the default SDL (patches/0009, frame-pacing.md)
if order="$("${here}/gamedrm/check-swap-order.sh" "$elf")"; then log "  ${order}"
else log "  ${order} -- the linked libSDL2.a lacks patches/0009"; bad=1; fi

# grep -a on the ELF itself (a `strings | grep -q` pipeline dies of SIGPIPE under pipefail)
for s in 'KMS/DRM Video Driver' '/dev/dri/' 'libdrm-phoenix:' 'DRMPHX_TRACE' 'DRMPHX sync' '/dev/kbd0' '/dev/audio0' \
		'EGL_KHR_platform_gbm' 'kmsro' 'stk-drm: new GPU lane' 'stk-drm flipstat' 'stk-drm swapstat'; do
	n="$(grep -acF -- "$s" "$elf.stripped" || true)"
	log "  string '$s': $n"
	[ "$n" != 0 ] || bad=1
done
for s in 'v3d-winsys:' 'v3da-winsys:' 'phxgl' 'PHOENIX: GL_CreateContext' '/dev/fb0' 'RPI4FB_GETMODE' 'phoenix_v3d_ioctl' \
		'peek_next_scanout' 'v3d-srv' 'v3d-pool:'; do
	n="$(grep -acF -- "$s" "$elf.stripped" || true)"
	log "  old-lane string '$s': $n"
	[ "$n" = 0 ] || bad=1
done
# inverse control: the shipped binary is the old lane
for s in 'stk-drm: new GPU lane' 'KMS/DRM Video Driver' 'libdrm-phoenix:'; do
	if grep -aqF -- "$s" "$shipped_bin"; then log "  the SHIPPED supertuxkart carries '$s'"; bad=1; fi
done
grep -aqF 'v3d-winsys: RT scanout' "$shipped_bin" || { log "  shipped supertuxkart lacks 'v3d-winsys: RT scanout' -- the negative check proves nothing"; bad=1; }
log "  inverse control: shipped supertuxkart = old lane (no KMSDRM / libdrm-phoenix / stk-drm strings)"

# silent duplicates: global symbols defined by STK's own link inputs AND the new stack
stk_in="$( cd "$stkbuild" && tr ' ' '\n' < "$linktxt" | grep -E '\.(obj|a)$' | grep -vxF "${old_sdl}" )"
dups="$( { ( cd "$stkbuild" && while IFS= read -r f; do "$nm" -g --defined-only "$f" 2>/dev/null; done <<< "$stk_in" ) \
		| awk 'NF >= 3 && $2 ~ /[TDBRVW]/ { print $3 }' | LC_ALL=C sort -u > "$out/obj/stk-defs.txt"; \
	for f in "$GALLIUM_A" "${MB}/${A[0]}" "${MB}/${A[1]}" "${MB}/${A[2]}" "${MB}/${A[4]}" "${MB}/src/util/libmesa_util.a" \
			"$LDP/lib/libdrm.a" "$SDL_A" "$COMPAT_A" "$hooks_o"; do "$nm" -g --defined-only "$f" 2>/dev/null; done \
		| awk 'NF >= 3 && $2 ~ /[TDBRVW]/ { print $3 }' | LC_ALL=C sort -u > "$out/obj/drm-defs.txt"; \
	LC_ALL=C comm -12 "$out/obj/stk-defs.txt" "$out/obj/drm-defs.txt" | grep -vxF 'DW.ref.__gxx_personality_v0' || true; } )"
if [ -n "$dups" ]; then
	log "  symbols defined by BOTH STK's inputs and the new stack ($(grep -c . <<< "$dups")):"
	sed 's/^/[stk-drm]     /' <<< "$dups" | head -30
	bad=1
else
	log "  no global symbol defined by both STK's inputs and the new stack (DW.ref.__gxx_personality_v0 aside)"
fi
rm -f "$out/obj/stk-defs.txt" "$out/obj/drm-defs.txt"

# --- 4. stk-drm launcher -----------------------------------------------------------------------
lsrc="$out/src/stk-$name.c"
sed -e "s|\"/usr/bin/supertuxkart\"|\"/usr/bin/supertuxkart-$name\"|" \
    -e "s|\"stk: exec /usr/bin/supertuxkart\"|\"stk-$name: exec /usr/bin/supertuxkart-$name\"|" \
    -e "s|\"stk: DATADIR=|\"stk-$name: DATADIR=|" \
    "$launcher_src" > "$lsrc"
[ "$(grep -c "\"/usr/bin/supertuxkart-$name\"" "$lsrc")" = 1 ] || die "launcher exec path rewrite did not match exactly once"
[ "$(grep -c "\"stk-$name: DATADIR=" "$lsrc")" = 1 ] || die "launcher banner rewrite did not match exactly once"
[ "$(grep -c '/usr/bin/supertuxkart"' "$lsrc")" = 0 ] || die "launcher still names the shipped engine"
grep -qF 'scale_rtts_factor=\"0.75\"' "$lsrc" || die "launcher lost the seeded scale_rtts_factor=0.75"
diff_lines="$(diff "$launcher_src" "$lsrc" | grep -c '^>' || true)"
[ "$diff_lines" = 3 ] || die "launcher differs from stk-launcher.c in $diff_lines lines (expected exactly 3)"
"$cc" -O2 -static -Wall -Wextra --sysroot="${sysroot}/" -B"${sysroot}/lib/" -iprefix "${sysroot}/" \
	-o "$out/stk-$name" "$lsrc" || die "launcher compile failed"
if "$readelf" -l "$out/stk-$name" 2>/dev/null | grep -q INTERP; then die "stk-$name has a PT_INTERP segment"; fi
grep -aqF "/usr/bin/supertuxkart-$name" "$out/stk-$name" || die "launcher ELF lacks its exec path"
[ -z "$("$nm" -u "$out/stk-$name" || true)" ] || die "launcher has undefined symbols"

# --- provenance ----------------------------------------------------------------------------------
hooks_git="$(git -C "$repo_root" status --porcelain -- "${hooks_src#"${repo_root}"/}" "${BASH_SOURCE[0]#"${repo_root}"/}")"
"$size" "$elf" | sed 's/^/[stk-drm]   /'
{
	echo "built:               $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "script/hooks git:    $( [ -n "$hooks_git" ] && echo "DIRTY/untracked" || echo "clean at $(git -C "$repo_root" rev-parse HEAD)")"
	echo "stkdrm_hooks.c:      $(sha "$hooks_src")"
	echo "libSDL2.a (KMSDRM):  $(sha "$SDL_A") ($(cat "${SD}/sdl-src.stamp" 2>/dev/null || echo '?') = sdl2-drm patch/overlay set)"
	echo "Mesa (mesa-gl):      patch set $(cat "${M}/mesa-src.stamp"), $(cat "${M}/mesa-opengl.txt"); libgallium $(sha "$GALLIUM_A" | cut -c1-16)"
	echo "libdrm-phoenix:      $(sha "$LDP/lib/libdrm.a") from ${libdrm_src}"
	echo "link.txt:            $(sha "$linktxt")"
	echo "control relink:      $control_note"
	echo "shipped prog:        $(sha "$shipped_prog") ($(stat -c%s "$shipped_prog") B)"
	echo "shipped stripped:    $(sha "$shipped_bin") ($(stat -c%s "$shipped_bin") B)"
	echo "drm unstripped:      $(sha "$elf") ($(stat -c%s "$elf") B)"
	echo "drm stripped:        $(sha "$elf.stripped") ($(stat -c%s "$elf.stripped") B)"
	echo "stk-$name:             $(sha "$out/stk-$name") ($(stat -c%s "$out/stk-$name") B)"
	echo "shipped bin/stk:     $(sha "$shipped_launcher") ($(stat -c%s "$shipped_launcher") B)"
	echo "libphoenix.a:        $(sha "$sysroot/lib/libphoenix.a")"
} > "$out/BUILD-INFO.txt"
sed 's/^/[stk-drm]   /' "$out/BUILD-INFO.txt"

check_guarded
[ "$bad" = 0 ] || die "verification failed (see above)"
log "done:"
log "  $elf.stripped -> stage as /usr/bin/supertuxkart-$name"
log "  $out/stk-$name                   -> stage as /bin/stk-$name"
log "  $elf          (unstripped, for addr2line; map: $elf.map)"
log "  $out/BUILD-INFO.txt"
