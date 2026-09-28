#!/usr/bin/env bash
#
# Build `supertuxkart-wl` + its launcher `stk-wl` (M8): a CLONE of SuperTuxKart 1.4 for a WINDOW
# on the Wayland desktop -- SDL 2.30.12 Wayland (+ KMSDRM), Mesa 26.2 EGL wayland (GLES), the
# labwc-drm Wayland client stack and libdrm-phoenix build-out-low: the link group of
# tools/gpu-lane/sdl2-wl/build.sh (build-out/link-inputs.txt, the "mesa-es" half).
#
# sdl2-drm/build-stk-drm.sh (stk-drm) with that stack swapped in; the -drm script, its outputs,
# /usr/bin/supertuxkart and /bin/stk are not touched. HOW: re-run the supertuxkart port's
# stage-4 link (CMake's link.txt) from the port's build tree, with the ports-prefix libSDL2.a
# (old /dev/fb0 video driver) replaced by sdl2-wl's libSDL2.a, the old GL-context glue objects
# and libGL-phoenix.a / libv3d-phoenix.a not linked, and link-inputs.txt's group + wraps
# (mmap, ioctl, close, write) + -Wl,--wrap=SDL_GL_SwapWindow for gamewl/gamewl_hooks.c
# (GAMEWL_NAME stk-wl). A CONTROL relink (the shipped inputs) must reproduce the shipped
# prog/supertuxkart byte for byte (warning only). The launcher is
# tools/supertuxkart-port/stk-launcher.c with its exec path and two message prefixes
# rewritten; the window comes from the arguments it passes on (/bin/game-window.sh stk:
# --windowed --screensize=WxH; `--windowed` is read after `--fullscreen`, main.cpp).
#
# PROOFS it prints (and fails on): nm -u empty, no PT_INTERP, Wayland/EGL-wayland/Mesa/libdrm
# symbols + strings present, old-lane ones absent (the shipped binary the reverse), only
# __wrap_ioctl / __wrap_mmap call the real ones, Irrlicht's swap goes through the wrapper, no
# global symbol defined by both STK's inputs and the new stack, the guarded inputs unchanged.
#
# WRITES ONLY under $STKWL_OUT (default tools/gpu-lane/sdl2-wl/build-out/stk-wl/).
#
# Usage: tools/gpu-lane/sdl2-wl/build-stk-wl.sh [--no-control]
# Stage (coordinator only; new names only, checked absent first):
#   install -m 755 $OUT/supertuxkart-wl.stripped <export>/usr/bin/supertuxkart-wl
#   install -m 755 $OUT/stk-wl                   <export>/bin/stk-wl
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${here}/../../.." && pwd)"
target="${TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
out="$(realpath -m "${STKWL_OUT:-${here}/build-out/stk-wl}")"
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
		-h|--help) sed -n '2,32p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-stk-wl: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

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
hooks_src="${here}/gamewl/gamewl_hooks.c"
SD="${here}/build-out"                    # the sdl2-wl build (read-only here)
SP="${SD}/sdl-prefix"
LI="${SD}/link-inputs.txt"
name="wl"
elf="${out}/supertuxkart-${name}"

log()  { printf '[stk-wl] %s\n' "$*"; }
warn() { printf '[stk-wl] WARNING: %s\n' "$*" >&2; }
die()  { printf '[stk-wl] ERROR: %s\n' "$*" >&2; exit 1; }
sha()  { if [ -e "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else echo "absent"; fi; }

# --- never write into the sdl2-drm build's own files --------------------------------------
case "$out" in
	"${SD}"|"${SD}"/sdl-*|"${SD}"/mesa-link|"${SD}"/mesa-link/*|"${SD}"/qs-*|"${SD}"/wl-include*|"${SD}"/pkg-config-sdl)
		die "refusing output dir $out: it belongs to sdl2-wl/build.sh" ;;
esac

# --- preconditions (fail loud; never fall back) -------------------------------------------
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${sysroot}/" -B"${sysroot}/lib/")
[ -f "${LI}" ] || die "missing ${LI} (run sdl2-wl/build.sh first)"
GALLIUM_A="" SDL_A="" MESA=() TAILL=() WRAPS=()
while read -r kind item; do
	case "${kind}" in
		gallium) GALLIUM_A="${item}" ;;
		sdl) SDL_A="${item}" ;;
		mesa-es) MESA+=("${item}") ;;   # STK is a GLES program (-DUSE_GLES2=ON): libGLESv2
		mesa-gl) ;;
		tail) TAILL+=("${item}") ;;
		flag) WRAPS+=("${item}") ;;
		*) die "unknown line in ${LI}: ${kind}" ;;
	esac
done < "${LI}"
[ -n "${GALLIUM_A}" ] && [ -n "${SDL_A}" ] && [ "${#MESA[@]}" -gt 10 ] && [ "${#TAILL[@]}" -gt 5 ] || die "${LI} is incomplete"
for f in "$cc" "$nm" "$strip" "$readelf" "$objdump" "$size" "$linktxt" "$old_sdl" "$shipped_prog" "$shipped_bin" \
		"$sysroot/lib/libphoenix.a" "$launcher_src" "$hooks_src" "$SDL_A" "$GALLIUM_A" "$SP/include/SDL2/SDL_config.h" \
		"${MESA[@]}" "${TAILL[@]}" "${pfx}/lib/libz.a" "${pfx}/lib/libmbedtls.a"; do
	[ -e "$f" ] || die "missing: $f (STK port built? sdl2-wl/build.sh run?)"
done
case " ${MESA[*]} " in *libGLESv2.a*) ;; *) die "the GLES link group has no libGLESv2.a" ;; esac
case " ${MESA[*]} " in *libglapi_bridge.a*) die "the GLES link group holds libglapi_bridge.a" ;; esac
cfg="${SP}/include/SDL2/SDL_config.h"
for d in SDL_VIDEO_DRIVER_WAYLAND SDL_VIDEO_DRIVER_KMSDRM SDL_VIDEO_OPENGL_EGL SDL_VIDEO_OPENGL_ES2 SDL_AUDIO_DRIVER_PHOENIX; do
	grep -qE "^#define ${d} +1" "${cfg}" || die "sdl2-wl's SDL_config.h lacks ${d} 1"
done
for d in SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC SDL_VIDEO_DRIVER_KMSDRM_DYNAMIC SDL_VIDEO_DRIVER_PHOENIX SDL_LOADSO_DLOPEN; do
	if grep -qE "^#define ${d}( |$)" "${cfg}"; then die "sdl2-wl's SDL_config.h defines ${d}"; fi
done

guarded=("$old_sdl" "$old_gl" "$old_v3d" "$shipped_prog" "$shipped_bin" "$shipped_launcher" "$linktxt"
	"$gluedir/sdl_phoenix_glctx.o" "$gluedir/sdl_phoenix_glstubs.o" "$SDL_A" "$GALLIUM_A" "${MESA[@]}" "${TAILL[@]}"
	"${SD}/sdl-src.stamp" "${SD}/quakespasm-wl.stripped")
declare -A before
for f in "${guarded[@]}"; do before["$f"]="$(sha "$f")"; done
check_guarded() {
	local f bad=0
	for f in "${guarded[@]}"; do
		if [ "${before[$f]}" != "$(sha "$f")" ]; then
			printf '[stk-wl] ERROR: shared file CHANGED during this build: %s\n' "$f" >&2
			bad=1
		fi
	done
	[ "$bad" = 0 ] || exit 1
	log "guarded shared files unchanged (${#guarded[@]} checked)"
}

mkdir -p "$out/src" "$out/obj"
rm -f "$out/trial-link.sh" "$out/trial-link.log" "$out/trial.elf"

# --- 2. the clone's hooks object -----------------------------------------------------------
hooks_o="$out/obj/gamewl_hooks.o"
"$cc" -O2 -g -std=gnu17 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SP}/include" \
	-DGAMEWL_NAME='"stk-wl"' -DGAMEWL_API='"GLES"' -c "$hooks_src" -o "$hooks_o" || die "gamewl_hooks.c compile failed"

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
for a in "${MESA[@]}" "${TAILL[@]}"; do AA="${AA} '${a}'"; done
WW=""
for w in "${WRAPS[@]}"; do WW="${WW} ${w}"; done
cmd="${linkcmd/ -o bin\/supertuxkart / -o '${elf}' }"
cmd="${cmd/ ${old_sdl} / '${SDL_A}' }"
cmd="${cmd} '${hooks_o}' -static${WW} -Wl,--wrap=SDL_GL_SwapWindow -Wl,-Map,'${elf}.map' \
	-Wl,--whole-archive '${GALLIUM_A}' -Wl,--no-whole-archive \
	-Wl,--start-group '${SDL_A}'${AA} \
	'${pfx}/lib/libz.a' '${pfx}/lib/libogg.a' '${pfx}/lib/libvorbis.a' \
	'${pfx}/lib/libvorbisfile.a' '${pfx}/lib/libvorbisenc.a' \
	'${pfx}/lib/libmbedtls.a' '${pfx}/lib/libmbedx509.a' '${pfx}/lib/libmbedcrypto.a' \
	-Wl,--end-group -lm -Wl,-z,stack-size=8388608"
for bad in "${old_sdl}" libGL-phoenix libv3d-phoenix sdl_phoenix_glctx sdl_phoenix_glstubs " -o bin/supertuxkart "; do
	case "$cmd" in *"$bad"*) die "the stk-wl link command still names '$bad'" ;; esac
done
printf '%s\n' "$cmd" > "$out/link-cmd.txt"
log "stk-wl relink (SDL Wayland + KMSDRM, Mesa EGL wayland/GLES, Wayland client stack, libdrm-phoenix)"
rm -f "$elf"
( cd "$stkbuild" && export PATH="${tcbin}:${PATH}" && eval "$cmd" ) > "$out/link.log" 2>&1 \
	|| { head -60 "$out/link.log" >&2; die "stk-wl link failed"; }
[ -f "$elf" ] || die "link reported success but produced no ELF"
[ -s "$out/link.log" ] && sed 's/^/[stk-wl]   link: /' "$out/link.log" | head -20
"$strip" -o "$elf.stripped" "$elf"

# --- proofs on the clone --------------------------------------------------------------------
bad=0
if "$readelf" -l "$elf" | grep -q INTERP; then log "  PT_INTERP present"; bad=1; fi
und="$("$nm" -u "$elf" || true)"
log "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && { sed 's/^/[stk-wl]     /' <<< "${und}" | head -20; bad=1; }
syms="$("$nm" "$elf")"
for s in Wayland_CreateDevice Wayland_GLES_SwapWindow Wayland_PumpEvents KMSDRM_CreateDevice SDL_EGL_LoadLibrary \
		SDL_GL_SwapWindow __wrap_SDL_GL_SwapWindow __wrap_mmap __wrap_ioctl __wrap_close __wrap_write drm_phoenix_ioctl \
		wl_display_connect wl_egl_window_create wl_cursor_theme_load xkb_keymap_new_from_string dri2_initialize_wayland \
		memfd_create mesa_os_create_anonymous_file os_create_anonymous_file kmsro_drm_screen_create \
		v3d_drm_screen_create_renderonly eglGetPlatformDisplayEXT eglGetProcAddress _mesa_glapi_get_proc_address; do
	if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then log "  symbol ${s}: yes"; else log "  symbol ${s}: NO"; bad=1; fi
done
forbidden="$(grep -E ' [TtWwDdBbRr] (PHOENIX_bootstrap|PHOENIX_PumpEvents|PHOENIX_GL_[A-Za-z_]*|phxgl_[A-Za-z_]*|phoenix_v3d_ioctl|winsys_init|boPool_take|mboxProp|v3da_connect|v3d_phoenix_flip)$' <<< "${syms}" || true)"
if [ -n "${forbidden}" ]; then log "  forbidden (old-lane) symbols PRESENT:"; sed 's/^/[stk-wl]     /' <<< "${forbidden}"; bad=1
else log "  old-lane symbols (SDL phoenix video, phxgl/PHOENIX_GL glue, in-process + v3da winsys): none"; fi

# objdump: who calls what (one pass over the text)
calls="$("$objdump" -d --no-show-raw-insn "$elf" | awk '
	/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn); next }
	/\tbl?\t/ && / <(ioctl|mmap|__wrap_SDL_GL_SwapWindow|SDL_GL_SwapWindow|__real_SDL_GL_SwapWindow)>$/ {
		t = $NF; gsub(/[<>]/, "", t); print t, fn }' | sort | uniq -c)"
printf '%s\n' "$calls" > "$out/call-sites.txt"
awk '$2 == "ioctl" && $3 != "__wrap_ioctl" { bad = 1 } END { exit bad }' <<< "$calls" \
	|| { log "  real ioctl() called from outside __wrap_ioctl:"; awk '$2 == "ioctl"' <<< "$calls" | sed 's/^/[stk-wl]     /'; bad=1; }
awk '$2 == "mmap" && $3 != "__wrap_mmap" { bad = 1 } END { exit bad }' <<< "$calls" \
	|| { log "  real mmap() called from outside __wrap_mmap:"; awk '$2 == "mmap"' <<< "$calls" | sed 's/^/[stk-wl]     /'; bad=1; }
if grep -qE ' __wrap_SDL_GL_SwapWindow _ZN3irr5video13COGLES2Driver' <<< "$calls" \
		&& ! grep -qE ' SDL_GL_SwapWindow _ZN3irr' <<< "$calls" \
		&& grep -qE ' SDL_GL_SwapWindow __wrap_SDL_GL_SwapWindow$' <<< "$calls"; then
	log "  PROOF: COGLES2Driver -> __wrap_SDL_GL_SwapWindow -> SDL_GL_SwapWindow (frame counter in the path)"
else
	log "  the SDL_GL_SwapWindow wrap is NOT in Irrlicht's swap path:"; sed 's/^/[stk-wl]     /' <<< "$calls"; bad=1
fi
log "  ioctl/mmap: only __wrap_ioctl / __wrap_mmap call the real ones ($out/call-sites.txt)"
# grep -a on the ELF itself (a `strings | grep -q` pipeline dies of SIGPIPE under pipefail)
for s in 'SDL Wayland video driver' 'KMS/DRM Video Driver' 'xdg_wm_base' 'zxdg_decoration_manager_v1' \
		'zwp_relative_pointer_manager_v1' 'zwp_pointer_constraints_v1' 'zwp_linux_dmabuf_v1' '/dev/dri/' \
		'libdrm-phoenix:' '/dev/audio0' 'kmsro' 'stk-wl: windowed GPU game' 'stk-wl flipstat' 'stk-wl swapstat'; do
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
for s in 'stk-wl: windowed GPU game' 'SDL Wayland video driver' 'libdrm-phoenix:'; do
	if grep -aqF -- "$s" "$shipped_bin"; then log "  the SHIPPED supertuxkart carries '$s'"; bad=1; fi
done
grep -aqF 'v3d-winsys: RT scanout' "$shipped_bin" || { log "  shipped supertuxkart lacks 'v3d-winsys: RT scanout' -- the negative check proves nothing"; bad=1; }
log "  inverse control: shipped supertuxkart = old lane (no Wayland / libdrm-phoenix / stk-wl strings)"

# silent duplicates: global symbols defined by STK's own link inputs AND the new stack
stk_in="$( cd "$stkbuild" && tr ' ' '\n' < "$linktxt" | grep -E '\.(obj|a)$' | grep -vxF "${old_sdl}" )"
dups="$( { ( cd "$stkbuild" && while IFS= read -r f; do "$nm" -g --defined-only "$f" 2>/dev/null; done <<< "$stk_in" ) \
		| awk 'NF >= 3 && $2 ~ /[TDBRVW]/ { print $3 }' | LC_ALL=C sort -u > "$out/obj/stk-defs.txt"; \
	for f in "$GALLIUM_A" "${MESA[@]}" "${TAILL[@]}" "$SDL_A" "$hooks_o"; do
			# (the ports libz.a is one of STK's own inputs too: the same archive, not a duplicate)
			[ "$f" = "${pfx}/lib/libz.a" ] || "$nm" -g --defined-only "$f" 2>/dev/null; done \
		| awk 'NF >= 3 && $2 ~ /[TDBRVW]/ { print $3 }' | LC_ALL=C sort -u > "$out/obj/drm-defs.txt"; \
	LC_ALL=C comm -12 "$out/obj/stk-defs.txt" "$out/obj/drm-defs.txt" | grep -vxF 'DW.ref.__gxx_personality_v0' || true; } )"
if [ -n "$dups" ]; then
	log "  symbols defined by BOTH STK's inputs and the new stack ($(grep -c . <<< "$dups")):"
	sed 's/^/[stk-wl]     /' <<< "$dups" | head -30
	bad=1
else
	log "  no global symbol defined by both STK's inputs and the new stack (DW.ref.__gxx_personality_v0 aside)"
fi
rm -f "$out/obj/stk-defs.txt" "$out/obj/drm-defs.txt"

# --- 4. stk-wl launcher ------------------------------------------------------------------------
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
"$size" "$elf" | sed 's/^/[stk-wl]   /'
{
	echo "built:               $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "script/hooks git:    $( [ -n "$hooks_git" ] && echo "DIRTY/untracked" || echo "clean at $(git -C "$repo_root" rev-parse HEAD)")"
	echo "gamewl_hooks.c:      $(sha "$hooks_src") (stk-wl, GLES)"
	echo "libSDL2.a (Wayland): $(sha "$SDL_A") ($(cat "${SD}/sdl-src.stamp" 2>/dev/null || echo '?') = sdl2-wl set)"
	echo "link inputs:         $(sha "$LI"); libgallium $(sha "$GALLIUM_A" | cut -c1-16)"
	echo "link.txt:            $(sha "$linktxt")"
	echo "control relink:      $control_note"
	echo "shipped prog:        $(sha "$shipped_prog") ($(stat -c%s "$shipped_prog") B)"
	echo "shipped stripped:    $(sha "$shipped_bin") ($(stat -c%s "$shipped_bin") B)"
	echo "wl unstripped:       $(sha "$elf") ($(stat -c%s "$elf") B)"
	echo "wl stripped:         $(sha "$elf.stripped") ($(stat -c%s "$elf.stripped") B)"
	echo "stk-$name:             $(sha "$out/stk-$name") ($(stat -c%s "$out/stk-$name") B)"
	echo "shipped bin/stk:     $(sha "$shipped_launcher") ($(stat -c%s "$shipped_launcher") B)"
	echo "libphoenix.a:        $(sha "$sysroot/lib/libphoenix.a")"
} > "$out/BUILD-INFO.txt"
sed 's/^/[stk-wl]   /' "$out/BUILD-INFO.txt"

check_guarded
[ "$bad" = 0 ] || die "verification failed (see above)"
log "done:"
log "  $elf.stripped -> stage as /usr/bin/supertuxkart-$name"
log "  $out/stk-$name                   -> stage as /bin/stk-$name"
log "  $elf          (unstripped, for addr2line; map: $elf.map)"
log "  $out/BUILD-INFO.txt"
