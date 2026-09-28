# shellcheck shell=bash
#
# Shared body of build-quake2-wl.sh and build-quake3-wl.sh (sourced, not run): relink an SDL2
# GL game port's own objects for a WINDOW on the Wayland desktop (M8) -- SDL 2.30.12 with its
# Wayland video driver + KMSDRM (sdl2-wl/build-out/sdl-prefix), Mesa 26.2 EGL on the wayland
# platform with desktop GL + GLES (mesa-drm/build-out-wayland-gl), the labwc-drm Wayland
# client stack and libdrm-phoenix build-out-low -- exactly the link group sdl2-wl/build.sh
# wrote to build-out/link-inputs.txt for quakespasm-wl.
#
# sdl2-drm/gamedrm/relink-sdl-gl-game.sh (the -drm clones) with that stack swapped in; the
# -drm script and its outputs are not touched. The caller sets:
#   G_APP            clone name, e.g. quake2-wl (tag of every log line, banner, flipstat)
#   G_ENGINE         the port's engine binary name (prog/<G_ENGINE>, /usr/bin/<G_ENGINE>)
#   G_PORTDIR        port-sources/<dir> of the port build tree
#   G_GL             gles (libGLESv2, the stk-drm shape) or gl (libglapi_bridge, quakespasm's)
#   G_API_TEXT       banner text for the API ("GLES" / "desktop GL")
#   G_LAUNCHER_SRC   the shipped launcher's source (tools/<game>-port/<x>-launcher.c)
#   G_LAUNCHER       the shipped launcher's name (/usr/bin/<G_LAUNCHER>)
#   G_ENGINE_SYMS    engine symbols that must be present in the clone (sanity)
#   G_OUT            output directory
#   G_DO_CONTROL     1 = run the control relink
# optionally G_WL_DIR (default sdl2-wl/build-out: its link-inputs.txt) and G_PORT_SHADOW (as the
# -drm script: a gamedrm/shadow-port-build.sh output dir instead of .buildroot's port-sources/).
# Staged names: /usr/bin/<G_ENGINE>-wl and the launcher /usr/bin/<G_LAUNCHER>-wl that execs it.
#
# HOW: the port's own final link from its build log (the ports framework runs p_build under
# `set -x`), (control) re-run verbatim with only -o redirected -- it must reproduce the shipped
# prog/<engine> byte for byte (warning only, if the port tree moved on); (clone) the old SDL-GL
# glue objects dropped and the old group (ports libSDL2.a = the /dev/fb0 video driver,
# libGL-phoenix.a, libv3d-phoenix.a) replaced by link-inputs.txt's group, with its wraps
# (mmap, ioctl, close, write) and --wrap=SDL_GL_SwapWindow for gamewl_hooks.c. Everything else
# -- the engine objects, their order, the port's flags, -lstdc++ -lm and the main-thread stack
# size -- stays as the port built it. The launcher: the shipped one with only its exec target
# rewritten; the window size comes from the arguments it forwards (/bin/game-window.sh).
#
# PROOFS it prints (and fails on): nm -u empty, no PT_INTERP, Wayland + EGL-wayland + Mesa +
# libdrm-phoenix symbols/strings present, old-lane ones absent (and the shipped binary the
# reverse), only __wrap_ioctl / __wrap_mmap call the real ones, the swap wrap in the engine's
# path, no global symbol defined by both the engine's objects and the new stack, the guarded
# shared inputs unchanged.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

g_main() {
	local here repo_root target buildroot pfx sysroot tcbin cc nm strip readelf objdump size
	here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"          # tools/gpu-lane/sdl2-wl
	repo_root="$(cd "${here}/../../.." && pwd)"
	target="${TARGET:-aarch64a72-generic-rpi4b}"
	buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
	pfx="${buildroot}/_build/${target}"
	sysroot="${pfx}/sysroot"
	tcbin="${repo_root}/.toolchain/aarch64-phoenix/bin"
	cc="${tcbin}/aarch64-phoenix-gcc"
	nm="${tcbin}/aarch64-phoenix-nm"
	strip="${tcbin}/aarch64-phoenix-strip"
	readelf="${tcbin}/aarch64-phoenix-readelf"
	objdump="${tcbin}/aarch64-phoenix-objdump"
	size="${tcbin}/aarch64-phoenix-size"

	local out="${G_OUT}"
	local buildlog="${pfx}/port-sources/${G_PORTDIR}/build.log"
	local port_prog="${pfx}/prog"
	if [ -n "${G_PORT_SHADOW:-}" ]; then
		buildlog="${G_PORT_SHADOW}/build.log"
		port_prog="${G_PORT_SHADOW}/prog"
	fi
	local old_sdl="${pfx}/lib/libSDL2.a"
	local old_gl="${repo_root}/tools/.gpu-libs/libGL-phoenix.a"
	local old_v3d="${repo_root}/tools/.gpu-libs/libv3d-phoenix.a"
	local shipped_prog="${pfx}/prog/${G_ENGINE}"
	local shipped_bin="${buildroot}/_fs/${target}/root/usr/bin/${G_ENGINE}"
	local shipped_launcher="${buildroot}/_fs/${target}/root/usr/bin/${G_LAUNCHER}"
	local hooks_src="${here}/gamewl/gamewl_hooks.c"
	local SD="${G_WL_DIR:-${here}/build-out}"
	local SP="${SD}/sdl-prefix"
	local LI="${SD}/link-inputs.txt"
	local engine_drm="${G_ENGINE}-wl"
	local elf="${out}/${engine_drm}"
	local launcher_drm="${G_LAUNCHER}-wl"

	log()  { printf '[%s] %s\n' "${G_APP}" "$*"; }
	warn() { printf '[%s] WARNING: %s\n' "${G_APP}" "$*" >&2; }
	die()  { printf '[%s] ERROR: %s\n' "${G_APP}" "$*" >&2; exit 1; }
	sha()  { if [ -e "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else echo "absent"; fi; }

	case "$out" in
		"${SD}"|"${SD}"/sdl-*|"${SD}"/mesa-link|"${SD}"/mesa-link/*|"${SD}"/qs-*|"${SD}"/wl-include*|"${SD}"/pkg-config-sdl)
			die "refusing output dir $out: it belongs to sdl2-wl/build.sh" ;;
	esac

	# --- preconditions (fail loud; never fall back) -------------------------------------
	local TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
		--sysroot="${sysroot}/" -B"${sysroot}/lib/")
	[ -f "${LI}" ] || die "missing ${LI} (run sdl2-wl/build.sh first)"
	case "${G_GL}" in gl|gles) ;; *) die "G_GL must be gles or gl" ;; esac
	local kind item GALLIUM_A="" SDL_A="" MESA=() TAILL=() WRAPS=()
	while read -r kind item; do
		case "${kind}" in
			gallium) GALLIUM_A="${item}" ;;
			sdl) SDL_A="${item}" ;;
			mesa-gl) if [ "${G_GL}" = gl ]; then MESA+=("${item}"); fi ;;
			mesa-es) if [ "${G_GL}" = gles ]; then MESA+=("${item}"); fi ;;
			tail) TAILL+=("${item}") ;;
			flag) WRAPS+=("${item}") ;;
			*) die "unknown line in ${LI}: ${kind}" ;;
		esac
	done < "${LI}"
	[ -n "${GALLIUM_A}" ] && [ -n "${SDL_A}" ] && [ "${#MESA[@]}" -gt 10 ] && [ "${#TAILL[@]}" -gt 5 ] \
		|| die "${LI} is incomplete"
	local f a
	for f in "$cc" "$nm" "$strip" "$readelf" "$objdump" "$size" "$buildlog" "$old_sdl" "$shipped_prog" "$shipped_bin" \
			"$shipped_launcher" "$sysroot/lib/libphoenix.a" "$G_LAUNCHER_SRC" "$hooks_src" "$SDL_A" "$GALLIUM_A" \
			"$SP/include/SDL2/SDL_config.h" "${MESA[@]}" "${TAILL[@]}"; do
		[ -e "$f" ] || die "missing: $f (port built? sdl2-wl/build.sh run?)"
	done
	local GL_BRIDGE="" GLESV2=""
	for a in "${MESA[@]}"; do
		case "$a" in *libglapi_bridge.a) GL_BRIDGE="$a" ;; *libGLESv2.a) GLESV2="$a" ;; esac
	done
	if [ "${G_GL}" = gl ]; then
		[ -n "${GL_BRIDGE}" ] && [ -z "${GLESV2}" ] || die "a gl link group must hold libglapi_bridge.a and not libGLESv2.a"
	else
		[ -n "${GLESV2}" ] && [ -z "${GL_BRIDGE}" ] || die "a gles link group must hold libGLESv2.a and not libglapi_bridge.a"
	fi
	local cfg="${SP}/include/SDL2/SDL_config.h" d
	for d in SDL_VIDEO_DRIVER_WAYLAND SDL_VIDEO_DRIVER_KMSDRM SDL_VIDEO_OPENGL_EGL SDL_VIDEO_OPENGL SDL_VIDEO_OPENGL_ES2 \
			SDL_AUDIO_DRIVER_PHOENIX; do
		grep -qE "^#define ${d} +1" "${cfg}" || die "sdl2-wl's SDL_config.h lacks ${d} 1"
	done
	for d in SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC SDL_VIDEO_DRIVER_KMSDRM_DYNAMIC SDL_VIDEO_DRIVER_PHOENIX SDL_LOADSO_DLOPEN; do
		if grep -qE "^#define ${d}( |$)" "${cfg}"; then die "sdl2-wl's SDL_config.h defines ${d}"; fi
	done

	# --- the port's final link, from its build log ---------------------------------------
	local linkline
	linkline="$(grep -E -- "^\+ aarch64-phoenix-gcc .* -o ${port_prog}//?${G_ENGINE}\$" "$buildlog" || true)"
	[ "$(grep -c . <<< "$linkline")" = 1 ] \
		|| die "${buildlog} must hold exactly one '+ aarch64-phoenix-gcc ... -o ${port_prog}/${G_ENGINE}' line -- the port recipe changed; update this script"
	linkline="${linkline#+ }"
	local objs=() ctl_args=() tok
	# The command has no quoting: every token is a flag or an absolute path (checked).
	read -r -a ctl_args <<< "$linkline"
	for tok in "${ctl_args[@]}"; do
		case "$tok" in *\'*|*\"*|*\\*) die "unexpected quoting in the port link line: $tok" ;; esac
	done
	local glue_ctx="" glue_stubs="" n_sdl=0 n_gl=0 n_v3d=0 n_out=0 prev=""
	for tok in "${ctl_args[@]}"; do
		case "$tok" in
			*/sdl_phoenix_glctx.o) glue_ctx="$tok" ;;
			*/sdl_phoenix_glstubs.o) glue_stubs="$tok" ;;
			*.o) objs+=("$tok") ;;
			"$old_sdl") n_sdl=$((n_sdl + 1)) ;;
			"$old_gl") n_gl=$((n_gl + 1)) ;;
			"$old_v3d") n_v3d=$((n_v3d + 1)) ;;
		esac
		[ "$prev" = "-o" ] && n_out=$((n_out + 1))
		prev="$tok"
	done
	[ -n "$glue_ctx" ] && [ -n "$glue_stubs" ] || die "the port link names no sdl_phoenix_glctx.o/glstubs.o -- recipe changed"
	[ "$n_sdl" = 1 ] && [ "$n_gl" = 1 ] && [ "$n_v3d" = 1 ] && [ "$n_out" = 1 ] \
		|| die "the port link must name the ports libSDL2.a, libGL-phoenix.a, libv3d-phoenix.a and -o exactly once each (got ${n_sdl}/${n_gl}/${n_v3d}/${n_out})"
	for f in "${objs[@]}" "$glue_ctx" "$glue_stubs"; do [ -f "$f" ] || die "missing port object $f"; done
	log "port link: ${#objs[@]} engine objects + 2 old SDL-GL glue objects (${buildlog})"

	local guarded=("$old_sdl" "$old_gl" "$old_v3d" "$shipped_prog" "$shipped_bin" "$shipped_launcher" "$buildlog"
		"$glue_ctx" "$glue_stubs" "$SDL_A" "$GALLIUM_A" "${MESA[@]}" "${TAILL[@]}" "${SD}/sdl-src.stamp"
		"${SD}/quakespasm-wl.stripped")
	declare -A before
	for f in "${guarded[@]}"; do before["$f"]="$(sha "$f")"; done
	check_guarded() {
		local g bad=0
		for g in "${guarded[@]}"; do
			if [ "${before[$g]}" != "$(sha "$g")" ]; then
				printf '[%s] ERROR: shared file CHANGED during this build: %s\n' "${G_APP}" "$g" >&2
				bad=1
			fi
		done
		[ "$bad" = 0 ] || exit 1
		log "guarded shared files unchanged (${#guarded[@]} checked)"
	}

	mkdir -p "$out/src" "$out/obj"

	# --- 2. hooks object -----------------------------------------------------------------------
	local hooks_o="$out/obj/gamewl_hooks.o"
	"$cc" -O2 -g -std=gnu17 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SP}/include" \
		-DGAMEWL_NAME="\"${G_APP}\"" -DGAMEWL_API="\"${G_API_TEXT}\"" -c "$hooks_src" -o "$hooks_o" \
		|| die "gamewl_hooks.c compile failed"

	# --- 3a. control relink ------------------------------------------------------------------
	local control_note="not run (--no-control)"
	if [ "${G_DO_CONTROL}" = 1 ]; then
		local cargs=() i=0
		for tok in "${ctl_args[@]}"; do
			if [ "$i" -gt 0 ] && [ "${ctl_args[$((i - 1))]}" = "-o" ]; then cargs+=("$out/${G_ENGINE}-control")
			else cargs+=("$tok"); fi
			i=$((i + 1))
		done
		log "control relink with the SHIPPED inputs (proves the recipe reproduces shipped)"
		rm -f "$out/${G_ENGINE}-control"
		( export PATH="${tcbin}:${PATH}" && "${cargs[@]}" ) > "$out/control-link.log" 2>&1 \
			|| { head -40 "$out/control-link.log" >&2; die "control link failed"; }
		if cmp -s "$out/${G_ENGINE}-control" "$shipped_prog"; then
			control_note="byte-identical to shipped prog/${G_ENGINE}"
			log "  PROOF: control relink == shipped prog/${G_ENGINE} (byte-identical)"
		else
			control_note="DIFFERS from shipped prog/${G_ENGINE}"
			warn "control relink differs from shipped prog/${G_ENGINE}: the port tree, libphoenix or an input"
			warn "  archive changed since the shipped build, so the clone's engine objects may not be the shipped ones."
		fi
		rm -f "$out/${G_ENGINE}-control"
	fi

	# --- 3b. the clone link --------------------------------------------------------------------
	local dargs=() skip=0 i=0 ingroup=0
	local newgroup=(-Wl,--whole-archive "${GALLIUM_A}" -Wl,--no-whole-archive -Wl,--start-group "${SDL_A}"
		"${MESA[@]}" "${TAILL[@]}" -Wl,--end-group)
	for tok in "${ctl_args[@]}"; do
		if [ "$skip" = 1 ]; then skip=0; i=$((i + 1)); continue; fi
		case "$tok" in
			"$glue_ctx"|"$glue_stubs") ;;
			-o) dargs+=(-o "$elf"); skip=1 ;;
			-Wl,--start-group) ingroup=1 ;;
			-Wl,--end-group)
				[ "$ingroup" = 1 ] || die "unbalanced group in the port link"
				ingroup=2
				dargs+=("$hooks_o" "${newgroup[@]}") ;;
			*)
				if [ "$ingroup" = 1 ]; then
					case "$tok" in "$old_sdl"|"$old_gl"|"$old_v3d") ;; *) die "unexpected member of the port's link group: $tok" ;; esac
				else
					dargs+=("$tok")
				fi ;;
		esac
		i=$((i + 1))
	done
	[ "$ingroup" = 2 ] || die "the port link has no --start-group/--end-group"
	dargs+=(-static "${WRAPS[@]}" -Wl,--wrap=SDL_GL_SwapWindow -Wl,-Map,"${elf}.map")
	local cmdtext="${dargs[*]}"
	for tok in "${old_sdl}" libGL-phoenix libv3d-phoenix sdl_phoenix_glctx sdl_phoenix_glstubs "${shipped_prog}"; do
		case "$cmdtext" in *" $tok "*|*" $tok") die "the ${G_APP} link command still names '$tok'" ;; esac
	done
	printf '%s\n' "$cmdtext" > "$out/link-cmd.txt"
	log "${engine_drm} relink (SDL Wayland + KMSDRM, Mesa EGL wayland/${G_API_TEXT}, Wayland client stack, libdrm-phoenix)"
	rm -f "$elf"
	( export PATH="${tcbin}:${PATH}" && "${dargs[@]}" ) > "$out/link.log" 2>&1 \
		|| { head -60 "$out/link.log" >&2; die "${engine_drm} link failed"; }
	[ -f "$elf" ] || die "link reported success but produced no ELF"
	[ -s "$out/link.log" ] && sed "s/^/[${G_APP}]   link: /" "$out/link.log" | head -20
	"$strip" -o "$elf.stripped" "$elf"

	# --- proofs on the clone ----------------------------------------------------------------
	local bad=0 und syms s n
	if "$readelf" -l "$elf" | grep -q INTERP; then log "  PT_INTERP present"; bad=1; fi
	und="$("$nm" -u "$elf" || true)"
	log "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
	[ -n "${und}" ] && { sed "s/^/[${G_APP}]     /" <<< "${und}" | head -20; bad=1; }
	syms="$("$nm" "$elf")"
	for s in Wayland_CreateDevice Wayland_GLES_SwapWindow Wayland_PumpEvents KMSDRM_CreateDevice SDL_EGL_LoadLibrary \
			SDL_GL_SwapWindow __wrap_SDL_GL_SwapWindow __wrap_mmap __wrap_ioctl __wrap_close __wrap_write drm_phoenix_ioctl \
			wl_display_connect wl_egl_window_create wl_cursor_theme_load xkb_keymap_new_from_string dri2_initialize_wayland \
			memfd_create mesa_os_create_anonymous_file os_create_anonymous_file kmsro_drm_screen_create \
			v3d_drm_screen_create_renderonly eglGetPlatformDisplayEXT eglGetProcAddress _mesa_glapi_get_proc_address \
			${G_ENGINE_SYMS}; do
		if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then log "  symbol ${s}: yes"; else log "  symbol ${s}: NO"; bad=1; fi
	done
	local forbidden
	forbidden="$(grep -E ' [TtWwDdBbRr] (PHOENIX_bootstrap|PHOENIX_PumpEvents|PHOENIX_GL_[A-Za-z_]*|phxgl_[A-Za-z_]*|phoenix_v3d_ioctl|winsys_init|boPool_take|mboxProp|v3da_connect|v3d_phoenix_flip)$' <<< "${syms}" || true)"
	if [ -n "${forbidden}" ]; then log "  forbidden (old-lane) symbols PRESENT:"; sed "s/^/[${G_APP}]     /" <<< "${forbidden}"; bad=1
	else log "  old-lane symbols (SDL phoenix video, phxgl/PHOENIX_GL glue, in-process + v3da winsys): none"; fi

	local calls
	calls="$("$objdump" -d --no-show-raw-insn "$elf" | awk '
		/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn); next }
		/\tbl?\t/ && / <(ioctl|mmap|__wrap_SDL_GL_SwapWindow|SDL_GL_SwapWindow)>$/ {
			t = $NF; gsub(/[<>]/, "", t); print t, fn }' | sort | uniq -c)"
	printf '%s\n' "$calls" > "$out/call-sites.txt"
	awk '$2 == "ioctl" && $3 != "__wrap_ioctl" { bad = 1 } END { exit bad }' <<< "$calls" \
		|| { log "  real ioctl() called from outside __wrap_ioctl:"; awk '$2 == "ioctl"' <<< "$calls" | sed "s/^/[${G_APP}]     /"; bad=1; }
	awk '$2 == "mmap" && $3 != "__wrap_mmap" { bad = 1 } END { exit bad }' <<< "$calls" \
		|| { log "  real mmap() called from outside __wrap_mmap:"; awk '$2 == "mmap"' <<< "$calls" | sed "s/^/[${G_APP}]     /"; bad=1; }
	local engine_callers
	engine_callers="$(awk '$2 == "__wrap_SDL_GL_SwapWindow" { print $3 }' <<< "$calls" | tr '\n' ' ')"
	if [ -n "$engine_callers" ] \
			&& ! awk '$2 == "SDL_GL_SwapWindow" && $3 != "__wrap_SDL_GL_SwapWindow" { f = 1 } END { exit !f }' <<< "$calls" \
			&& grep -qE ' SDL_GL_SwapWindow __wrap_SDL_GL_SwapWindow$' <<< "$calls"; then
		log "  PROOF: ${engine_callers}-> __wrap_SDL_GL_SwapWindow -> SDL_GL_SwapWindow (frame counter in the path)"
	else
		log "  the SDL_GL_SwapWindow wrap is NOT in the engine's swap path:"; sed "s/^/[${G_APP}]     /" <<< "$calls"; bad=1
	fi
	log "  ioctl/mmap: only __wrap_ioctl / __wrap_mmap call the real ones ($out/call-sites.txt)"
	for s in 'SDL Wayland video driver' 'KMS/DRM Video Driver' 'xdg_wm_base' 'zxdg_decoration_manager_v1' \
			'zwp_relative_pointer_manager_v1' 'zwp_pointer_constraints_v1' 'zwp_linux_dmabuf_v1' '/dev/dri/' \
			'libdrm-phoenix:' '/dev/audio0' 'kmsro' "${G_APP}: windowed GPU game" "${G_APP} flipstat" "${G_APP} swapstat"; do
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
	for s in "${G_APP}: windowed GPU game" 'SDL Wayland video driver' 'libdrm-phoenix:'; do
		if grep -aqF -- "$s" "$shipped_bin"; then log "  the SHIPPED ${G_ENGINE} carries '$s'"; bad=1; fi
	done
	grep -aqF 'v3d-winsys: RT scanout' "$shipped_bin" \
		|| { log "  shipped ${G_ENGINE} lacks 'v3d-winsys: RT scanout' -- the negative checks prove nothing"; bad=1; }
	log "  inverse control: shipped ${G_ENGINE} = old lane (no Wayland / libdrm-phoenix / ${G_APP} strings)"

	# silent duplicates: global symbols defined by the engine's objects AND the new stack
	local dups
	dups="$( { for f in "${objs[@]}"; do "$nm" -g --defined-only "$f" 2>/dev/null; done \
			| awk 'NF >= 3 && $2 ~ /[TDBRVW]/ { print $3 }' | LC_ALL=C sort -u > "$out/obj/eng-defs.txt"; \
		for f in "$GALLIUM_A" "${MESA[@]}" "${TAILL[@]}" "$SDL_A" "$hooks_o"; do
			"$nm" -g --defined-only "$f" 2>/dev/null; done \
			| awk 'NF >= 3 && $2 ~ /[TDBRVW]/ { print $3 }' | LC_ALL=C sort -u > "$out/obj/drm-defs.txt"; \
		LC_ALL=C comm -12 "$out/obj/eng-defs.txt" "$out/obj/drm-defs.txt" || true; } )"
	if [ -n "$dups" ]; then
		log "  symbols defined by BOTH the engine's objects and the new stack ($(grep -c . <<< "$dups")):"
		sed "s/^/[${G_APP}]     /" <<< "$dups" | head -30
		bad=1
	else
		log "  no global symbol defined by both the engine's objects and the new stack"
	fi
	rm -f "$out/obj/eng-defs.txt" "$out/obj/drm-defs.txt"

	# --- 4. launcher -------------------------------------------------------------------------------
	local lsrc="$out/src/${launcher_drm}.c" diff_lines
	sed -e "s|\"/usr/bin/${G_ENGINE}\"|\"/usr/bin/${engine_drm}\"|" "$G_LAUNCHER_SRC" > "$lsrc"
	[ "$(grep -c "\"/usr/bin/${engine_drm}\"" "$lsrc")" = 1 ] || die "launcher exec target rewrite did not match exactly once"
	[ "$(grep -c "\"/usr/bin/${G_ENGINE}\"" "$lsrc")" = 0 ] || die "launcher still names the shipped engine"
	diff_lines="$(diff "$G_LAUNCHER_SRC" "$lsrc" | grep -c '^>' || true)"
	[ "$diff_lines" = 1 ] || die "launcher differs from $(basename "$G_LAUNCHER_SRC") in $diff_lines lines (expected exactly 1)"
	"$cc" -O2 -static -Wall -Wextra --sysroot="${sysroot}/" -B"${sysroot}/lib/" -iprefix "${sysroot}/" \
		-o "$out/${launcher_drm}" "$lsrc" || die "launcher compile failed"
	if "$readelf" -l "$out/${launcher_drm}" 2>/dev/null | grep -q INTERP; then die "${launcher_drm} has a PT_INTERP segment"; fi
	grep -aqF "/usr/bin/${engine_drm}" "$out/${launcher_drm}" || die "launcher ELF lacks its exec target"
	[ -z "$("$nm" -u "$out/${launcher_drm}" || true)" ] || die "launcher has undefined symbols"

	# --- provenance --------------------------------------------------------------------------------
	local src_git
	src_git="$(git -C "$repo_root" status --porcelain -- "tools/gpu-lane/sdl2-wl/gamewl" "tools/gpu-lane/sdl2-wl/build-${G_APP}.sh")"
	"$size" "$elf" | sed "s/^/[${G_APP}]   /"
	{
		echo "built:               $(date -u +%Y-%m-%dT%H:%M:%SZ)"
		echo "script/hooks git:    $( [ -n "$src_git" ] && echo "DIRTY/untracked" || echo "clean at $(git -C "$repo_root" rev-parse HEAD)")"
		echo "gamewl_hooks.c:      $(sha "$hooks_src")"
		echo "libSDL2.a (Wayland): $(sha "$SDL_A") ($(cat "${SD}/sdl-src.stamp" 2>/dev/null || echo '?') = sdl2-wl set)"
		echo "link inputs:         $(sha "$LI") (${LI}); libgallium $(sha "$GALLIUM_A" | cut -c1-16); API ${G_GL}"
		echo "port build log:      $(sha "$buildlog") (${buildlog})"
		echo "control relink:      $control_note"
		echo "shipped prog:        $(sha "$shipped_prog") ($(stat -c%s "$shipped_prog") B)"
		echo "shipped stripped:    $(sha "$shipped_bin") ($(stat -c%s "$shipped_bin") B)"
		echo "wl unstripped:       $(sha "$elf") ($(stat -c%s "$elf") B)"
		echo "wl stripped:         $(sha "$elf.stripped") ($(stat -c%s "$elf.stripped") B)"
		echo "${launcher_drm}:$(printf '%*s' $((19 - ${#launcher_drm})) '') $(sha "$out/${launcher_drm}") ($(stat -c%s "$out/${launcher_drm}") B)"
		echo "shipped ${G_LAUNCHER}:$(printf '%*s' $((11 - ${#G_LAUNCHER})) '') $(sha "$shipped_launcher") ($(stat -c%s "$shipped_launcher") B)"
		echo "libphoenix.a:        $(sha "$sysroot/lib/libphoenix.a")"
	} > "$out/BUILD-INFO.txt"
	sed "s/^/[${G_APP}]   /" "$out/BUILD-INFO.txt"

	check_guarded
	[ "$bad" = 0 ] || die "verification failed (see above)"
	log "done:"
	log "  $elf.stripped -> stage as /usr/bin/${engine_drm}"
	log "  $out/${launcher_drm} -> stage as /usr/bin/${launcher_drm}"
	log "  $elf (unstripped, for addr2line; map: $elf.map)"
	log "  $out/BUILD-INFO.txt"
}
