#!/usr/bin/env bash
#
# sdl2-drm (new GPU lane, M3 part 4): SDL 2.30.12 -- the version ports/sdl2 ships -- built
# STATIC with its stock KMSDRM video driver on Mesa's GBM + EGL (tools/gpu-lane/mesa-drm, built
# with desktop GL) and libdrm-phoenix, plus `quakespasm-drm`: a CLONE of the quakespasm port
# rebuilt against that SDL + Mesa + libdrm-phoenix. The shipped /usr/bin/quakespasm, the
# ports/sdl2 port, the old lane's Mesa fork and tools/.gpu-libs are never touched (PLAN rule 2).
# quakespasm-drm links gamedrm/gamedrm_hooks.c (shared with quake2-drm/quake3-drm) for its
# banner and the `quakespasm-drm flipstat ... (total N)` frame counter the migration gate reads.
#
#   <out>/mesa-gl/          Mesa 26.2.0 with -Dopengl=true (mesa-drm/build.sh --opengl --out ...),
#                           incl. libglapi_bridge.a (the static gl* entry points)
#   <out>/sdl-src/          SDL2-2.30.12 from ports/sdl2's tarball + patches/*.patch + overlay/
#   <out>/sdl-build/        cmake build dir
#   <out>/sdl-prefix/       `make install`: include/SDL2/*.h, lib/libSDL2.a (+ libSDL2main.a)
#   <out>/qs-src/           quakespasm at the port's pinned commit + the port's patch 0001
#   <out>/qs-obj/           objects
#   <out>/quakespasm-drm    static, unstripped (addr2line); quakespasm-drm.stripped (stage this)
#   <out>/quakespasm-drm.map, <out>/BUILD-INFO.txt, logs
#
# Reads (never writes): sources/phoenix-rtos-ports/{sdl2,quakespasm} (tarballs, the quakespasm
# patch and glue), the tree sysroot, the toolchain, the ports prefix's libz.a, E7's phx-g++, and
# libdrm-phoenix's build-out-m3p3/prefix (via mesa-drm's snapshot). No Pi, no
# rebuild-rpi4b-fast.sh, no /srv. The SDL build never sees the ports prefix's include/ (it holds
# other ports' GL/ and X11/ headers -- header poisoning) nor the host's pkg-config dirs.
#
# Usage: tools/gpu-lane/sdl2-drm/build.sh [--out <dir>] [--clean] [-j N]
#                                         [--libdrm-prefix <dir>] [--skip-mesa] [--skip-sdl]
#                                         [--extra-patches <dir>] [--name <clone name>]
#   --skip-mesa  reuse <out>/mesa-gl as is (default: run mesa-drm/build.sh, a no-op when current)
#   --skip-sdl   reuse <out>/sdl-prefix as is: (re)build quakespasm-drm only. The SDL cmake tree
#                tracks the sysroot headers too, so after a libphoenix install a plain run
#                recompiles libSDL2.a -- which stk-drm, quake2/3-drm and vkquake-drm also link.
#   --extra-patches  apply <dir>/*.patch after patches/ (part of the source stamp). For a variant
#                built into its own --out, e.g. patches-pace (frame-pacing.md): the default set,
#                and so the default build-out's libSDL2.a, stay as they are.
#   --name       the clone's name in its banner/flipstat/swapstat lines (default quakespasm-drm)
# Stage (coordinator only):
#   sudo install -m 755 <out>/quakespasm-drm.stripped <live NFS export>/usr/bin/quakespasm-drm
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
jobs="$(nproc)"
clean=0
skip_mesa=0
skip_sdl=0
extra_patches=""
qs_name=quakespasm-drm
# m3p3 = the part-2 library (G1 import, G2/G3 users, /dev/dri names, G13) + opt-in DRMPHX_TRACE.
libdrm_prefix="${root}/tools/gpu-lane/libdrm-phoenix/build-out-m3p3/prefix"
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--skip-mesa) skip_mesa=1 ;;
		--skip-sdl) skip_sdl=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		--libdrm-prefix) shift; libdrm_prefix="${1:?--libdrm-prefix needs a directory}" ;;
		--libdrm-prefix=*) libdrm_prefix="${1#--libdrm-prefix=}" ;;
		--extra-patches) shift; extra_patches="${1:?--extra-patches needs a directory}" ;;
		--extra-patches=*) extra_patches="${1#--extra-patches=}" ;;
		--name) shift; qs_name="${1:?--name needs a name}" ;;
		--name=*) qs_name="${1#--name=}" ;;
		-h|--help) sed -n '2,40p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
if [ -n "${extra_patches}" ]; then
	case "${extra_patches}" in /*) ;; *) extra_patches="${PWD}/${extra_patches}" ;; esac
	ls "${extra_patches}"/*.patch > /dev/null 2>&1 || { echo "build.sh: no *.patch in ${extra_patches}" >&2; exit 2; }
fi
if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

SDL_VERSION=2.30.12
PORTS="${root}/sources/phoenix-rtos-ports"
SDL_TARBALL="${PORTS}/sdl2/SDL2-${SDL_VERSION}.tar.gz"
QS_COMMIT=f5fe17864918239d443fe4c0d6bfb980e44d19e6
QS_TARBALL="${PORTS}/quakespasm/${QS_COMMIT}.tar.gz"
QS_PATCH="${PORTS}/quakespasm/patches/0001-quakespasm-phoenix-v3d-single-elf.patch"
QS_GLUE="${PORTS}/quakespasm/glue"
B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCXX="${root}/tools/gpu-lane/e7-drm-build/bin/phx-g++"
M="${out}/mesa-gl"
MB="${M}/mesa-build"

log() { printf '[sdl2-drm] %s\n' "$*"; }
die() { printf '[sdl2-drm] ERROR: %s\n' "$*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -c1-16; }

for p in "${SDL_TARBALL}" "${QS_TARBALL}" "${QS_PATCH}" "${QS_GLUE}/pl_phoenix_main.c" "${S}/lib/libphoenix.a" \
		"${TC}-gcc" "${TC}-nm" "${TC}-strip" "${TC}-size" "${TC}-readelf" "${TC}-objdump" "${PHXCXX}" "${B}/lib/libz.a" \
		"${libdrm_prefix}/lib/libdrm.a"; do
	[ -e "${p}" ] || die "missing ${p}"
done
mkdir -p "${out}"

# --- 1. Mesa with desktop GL ------------------------------------------------------------------
# quakespasm is a desktop-GL program (glBegin, fixed function + GLSL); the committed mesa-drm
# default is GLES-only, so the GL variant is built into our own output dir.
if [ "${skip_mesa}" = 0 ]; then
	log "Mesa (desktop GL) -> ${M}"
	"${root}/tools/gpu-lane/mesa-drm/build.sh" --opengl --out "${M}" --libdrm-prefix "${libdrm_prefix}" -j "${jobs}" \
		> "${out}/mesa-gl-build.log" 2>&1 || { tail -30 "${out}/mesa-gl-build.log"; die "mesa-drm build failed"; }
fi
grep -qx 'opengl=true' "${M}/mesa-opengl.txt" 2>/dev/null || die "${M} is not a desktop-GL Mesa build"
GL_BRIDGE="${MB}/src/mesa/glapi/glapi/libglapi_bridge.a"
[ -f "${GL_BRIDGE}" ] || die "missing ${GL_BRIDGE}"
LD_PREFIX="${M}/libdrm-prefix"   # mesa-drm's snapshot of libdrm-phoenix: Mesa and SDL see one copy

TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

# --- 2. SDL source ----------------------------------------------------------------------------
SB="${out}/sdl-build"
SP="${out}/sdl-prefix"
if [ "${skip_sdl}" = 1 ]; then
	[ -f "${SP}/lib/libSDL2.a" ] || die "--skip-sdl: no ${SP}/lib/libSDL2.a"
	stamp="$(cat "${out}/sdl-src.stamp")"
	log "SDL: reusing ${SP} as is (--skip-sdl; patch/overlay set ${stamp})"
else
	src="${out}/sdl-src"
	# Without --extra-patches the stamp is the one of the default set, byte for byte.
	stamp="$( (sha256sum "${SDL_TARBALL}"; cat "${here}"/patches/*.patch; find "${here}/overlay" -type f | sort | xargs cat
		if [ -n "${extra_patches}" ]; then cat "${extra_patches}"/*.patch; fi) \
		| sha256sum | cut -c1-16)"
	if [ "$(cat "${out}/sdl-src.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		log "SDL ${SDL_VERSION} source: tarball + $(ls "${here}"/patches/*.patch | wc -l) patches + overlay (set ${stamp})"
		rm -rf "${src}" "${out}/sdl-build" "${out}/sdl-prefix" "${out}/sdl-tmp"
		mkdir -p "${out}/sdl-tmp"
		tar -C "${out}/sdl-tmp" -xzf "${SDL_TARBALL}"
		mv "${out}/sdl-tmp/SDL2-${SDL_VERSION}" "${src}"
		rmdir "${out}/sdl-tmp"
		for p in "${here}"/patches/*.patch ${extra_patches:+"${extra_patches}"/*.patch}; do
			patch -d "${src}" -p1 -s --no-backup-if-mismatch < "${p}" || die "patch failed: $(basename "${p}")"
		done
		[ -z "${extra_patches}" ] || log "  + $(ls "${extra_patches}"/*.patch | wc -l) extra patch(es) from ${extra_patches}"
		cp -a "${here}/overlay/." "${src}/"
		echo "${stamp}" > "${out}/sdl-src.stamp"
	fi

	# --- 3. SDL configure + build -----------------------------------------------------------------
	# pkg-config sees ONLY Mesa's prefix (egl, gbm), the libdrm-phoenix snapshot (libdrm) and
	# mesa-drm's private zlib prefix (egl/gbm list zlib in Requires.private).
	pkgc="${out}/pkg-config-sdl"
	cat > "${pkgc}" <<EOF
#!/bin/sh
# pkg-config restricted to the Mesa-GL prefix + the libdrm-phoenix snapshot + zlib (cross only).
export PKG_CONFIG_LIBDIR=${M}/prefix/lib/pkgconfig:${LD_PREFIX}/lib/pkgconfig:${M}/zlib-prefix/lib/pkgconfig
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
# Mesa's .pc files carry -pthread, which aarch64-phoenix-gcc rejects (pthreads live in libphoenix).
o="\$(/usr/bin/pkg-config --static "\$@")" || exit \$?
[ -z "\$o" ] || printf '%s\n' "\$o" | sed -e 's/\(^\| \)-pthread\( \|\$\)/\1/g'
EOF
	chmod +x "${pkgc}"
	if [ ! -f "${SB}/Makefile" ]; then
		log "SDL cmake configure"
		mkdir -p "${SB}"
		sdl_cflags="${TFLAGS[*]} -O2 -g -std=gnu17"
		( cd "${SB}" && PKG_CONFIG="${pkgc}" cmake "${src}" \
			-DCMAKE_INSTALL_PREFIX="${SP}" \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
			-DCMAKE_SYSTEM_NAME=Generic \
			-DCMAKE_SYSTEM_PROCESSOR=aarch64 \
			-DCMAKE_C_COMPILER="${TC}-gcc" \
			-DCMAKE_CXX_COMPILER="${TC}-g++" \
			-DCMAKE_AR="${TC}-gcc-ar" \
			-DCMAKE_RANLIB="${TC}-gcc-ranlib" \
			-DCMAKE_C_FLAGS="${sdl_cflags}" \
			-DCMAKE_C_FLAGS_RELEASE="-DNDEBUG" \
			-DCMAKE_EXE_LINKER_FLAGS="${TFLAGS[*]} -Wl,-z,max-page-size=0x1000" \
			-DPKG_CONFIG_EXECUTABLE="${pkgc}" \
			-DPHOENIX=ON \
			-DSDL_LIBC=ON \
			-DSDL_PTHREADS=ON \
			-DSDL_CLOCK_GETTIME=ON \
			-DSDL_SHARED=OFF \
			-DSDL_STATIC=ON \
			-DSDL_TEST=OFF \
			-DSDL_X11=OFF \
			-DSDL_WAYLAND=OFF \
			-DSDL_KMSDRM=ON \
			-DSDL_KMSDRM_SHARED=OFF \
			-DSDL_OPENGL=ON \
			-DSDL_OPENGLES=ON \
			-DSDL_VULKAN=OFF \
			-DSDL_HIDAPI=OFF \
			-DSDL_LIBUDEV=OFF \
			-DSDL_DBUS=OFF \
			-DSDL_IBUS=OFF \
			-DSDL_PULSEAUDIO=OFF \
			-DSDL_ALSA=OFF \
			-DSDL_PIPEWIRE=OFF \
			-DSDL_JACK=OFF \
			-DSDL_OSS=OFF \
			-DSDL_SNDIO=OFF \
			-DSDL_ESD=OFF \
			-DSDL_NAS=OFF \
			-DSDL_ARTS=OFF \
			-DSDL_DIRECTFB=OFF \
			-DSDL_RPI=OFF \
			-DSDL_VIVANTE=OFF \
			-DSDL_OFFSCREEN=OFF \
			> "${out}/sdl-cmake.log" 2>&1 ) || { tail -40 "${out}/sdl-cmake.log"; die "SDL cmake failed"; }
	fi
	log "SDL build (-j${jobs})"
	make -C "${SB}" -j"${jobs}" install > "${out}/sdl-make.log" 2>&1 || { grep -E -B2 -A6 'error|Error' "${out}/sdl-make.log" | head -60; die "SDL build failed"; }
	log "  $(grep -c 'warning:' "${out}/sdl-make.log" || true) compiler warning line(s) (${out}/sdl-make.log)"
fi
SDL_A="${SP}/lib/libSDL2.a"
[ -f "${SDL_A}" ] || die "no ${SDL_A}"

# The configuration must be what this build is about -- fail loudly otherwise.
cfg="${SP}/include/SDL2/SDL_config.h"
for d in SDL_VIDEO_DRIVER_KMSDRM SDL_VIDEO_OPENGL_EGL SDL_VIDEO_OPENGL SDL_INPUT_PHOENIX SDL_AUDIO_DRIVER_PHOENIX \
		SDL_THREAD_PTHREAD SDL_TIMER_UNIX; do
	grep -qE "^#define ${d} +1" "${cfg}" || die "SDL_config.h lacks ${d} 1"
done
for d in SDL_VIDEO_DRIVER_KMSDRM_DYNAMIC SDL_VIDEO_DRIVER_PHOENIX SDL_VIDEO_DRIVER_X11 SDL_VIDEO_DRIVER_WAYLAND \
		SDL_INPUT_LINUXEV SDL_LOADSO_DLOPEN SDL_VIDEO_VULKAN; do
	if grep -qE "^#define ${d}( |$)" "${cfg}"; then die "SDL_config.h defines ${d}"; fi
done
log "  SDL_config.h: KMSDRM (static), EGL, GL + GLES2, Phoenix HID input + audio; no dynamic loading"

# --- 4. quakespasm-drm ------------------------------------------------------------------------
qsrc="${out}/qs-src"
qstamp="$( (sha256sum "${QS_TARBALL}" "${QS_PATCH}") | sha256sum | cut -c1-16)"
if [ "$(cat "${out}/qs-src.stamp" 2>/dev/null || true)" != "${qstamp}" ]; then
	log "quakespasm source: ${QS_COMMIT:0:12} + $(basename "${QS_PATCH}")"
	rm -rf "${qsrc}" "${out}/qs-tmp"
	mkdir -p "${out}/qs-tmp"
	tar -C "${out}/qs-tmp" -xzf "${QS_TARBALL}"
	mv "${out}/qs-tmp/quakespasm-${QS_COMMIT}" "${qsrc}"
	rmdir "${out}/qs-tmp"
	patch -d "${qsrc}" -p1 -s --no-backup-if-mismatch < "${QS_PATCH}" || die "quakespasm patch failed"
	echo "${qstamp}" > "${out}/qs-src.stamp"
fi
Q="${qsrc}/Quake"
# TU lists = the port's p_build (sources/phoenix-rtos-ports/quakespasm/port.def.sh). The SDL
# platform TUs the Phoenix glue replaces are omitted exactly as there; the old lane's GL-context
# glue (ports/sdl2/glue/sdl_phoenix_glctx.c) is NOT built -- SDL's KMSDRM/EGL owns the context.
globjs=(gl_refrag gl_rlight gl_rmain gl_fog gl_rmisc r_part r_world gl_screen gl_sky
	gl_warp gl_draw image gl_texmgr gl_mesh r_sprite r_alias r_brush gl_model)
core=(strlcat strlcpy net_dgrm net_loop net_main net_udp chase cl_demo cl_input
	cl_main cl_parse cl_tent console keys menu sbar view wad cmd common miniz crc
	cvar cfgfile host host_cmd mathlib pr_cmds pr_edict pr_exec sv_main sv_move
	sv_phys sv_user world zone snd_dma snd_mix snd_mem bgmusic cd_null snd_codec)
sdlbk=(gl_vidsdl in_sdl snd_sdl)
shims=(pl_phoenix_sys pl_phoenix_main pl_phoenix_stubs)
# Port flags minus its ports-prefix include dir; GL headers from the clone's Mesa source (the
# same Mesa the binary links), never external/mesa.
QFLAGS=("${TFLAGS[@]}" -fomit-frame-pointer -std=gnu17 -c -O2 -g -ffreestanding -fno-strict-aliasing -Wno-error
	-DUSE_SDL2 -DNO_SDL_CONFIG -I"${Q}" -I"${M}/mesa-src/include" -I"${SP}/include" -I"${SP}/include/SDL2")
QO="${out}/qs-obj"
rm -rf "${QO}"
mkdir -p "${QO}"
: > "${out}/qs-cc.log"
log "quakespasm-drm: compiling $(( ${#globjs[@]} + ${#core[@]} + ${#sdlbk[@]} + ${#shims[@]} + 1 )) TUs"
{
	for u in "${globjs[@]}" "${core[@]}" "${sdlbk[@]}"; do printf '%s\n' "${Q}/${u}.c"; done
	for u in "${shims[@]}"; do printf '%s\n' "${QS_GLUE}/${u}.c"; done
} > "${out}/qs-tus.txt"
export TCGCC="${TC}-gcc" QO
qs_cc() { local f="$1" o; o="${QO}/$(basename "${f%.c}").o"; "${TCGCC}" "${@:2}" -o "${o}" "${f}" 2>> "${QO}/$(basename "${f%.c}").log" || { echo "compile FAILED: ${f}"; cat "${QO}/$(basename "${f%.c}").log"; exit 1; }; }
export -f qs_cc
xargs -P"${jobs}" -I{} bash -c 'qs_cc "$@"' _ {} "${QFLAGS[@]}" < "${out}/qs-tus.txt" > "${out}/qs-cc-fail.log" 2>&1 \
	|| { head -40 "${out}/qs-cc-fail.log"; die "quakespasm compile failed"; }
cat "${QO}"/*.log > "${out}/qs-cc.log" 2>/dev/null || true
rm -f "${QO}"/*.log
objs=()
while IFS= read -r f; do
	o="${QO}/$(basename "${f%.c}").o"
	[ -f "${o}" ] || die "object missing: ${o}"
	objs+=("${o}")
done < "${out}/qs-tus.txt"
# The clone's process hooks: the shared gamedrm_hooks.c of quake2-drm/quake3-drm (banner, SDL
# VIDEO/INPUT at DEBUG, and the `quakespasm-drm flipstat ... (total N)` + `swapstat` counter on
# GL_EndRendering's SDL_GL_SwapWindow, reached through -Wl,--wrap=SDL_GL_SwapWindow below). Built
# with its own flags (-Werror), not the engine's -Wno-error set.
HOOKS_SRC="${here}/gamedrm/gamedrm_hooks.c"
HOOKS_O="${QO}/gamedrm_hooks.o"
"${TC}-gcc" -O2 -g -std=gnu17 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SP}/include" \
	-DGAMEDRM_NAME="\"${qs_name}\"" -DGAMEDRM_API='"desktop GL"' -c "${HOOKS_SRC}" -o "${HOOKS_O}" \
	|| die "gamedrm_hooks.c compile failed"
objs+=("${HOOKS_O}")
log "  compiled ${#objs[@]} objects ($(grep -c 'warning:' "${out}/qs-cc.log" || true) warning line(s), ${out}/qs-cc.log)"

# Link = mesa-drm's kmscube shape (C++ driver: Mesa's compiler is C++; -static; --gc-sections;
# 4 KiB pages; --wrap=mmap for libdrm-phoenix's BO-token mmap; libgallium whole-archive) plus:
# libglapi_bridge.a (the desktop gl* entry points, instead of libGLESv2.a whose gl* would clash),
# libSDL2.a in the group, the port's 32 MiB main stack (Quake's deep call chains), and
# --wrap=SDL_GL_SwapWindow for the gamedrm frame counter (only this binary's references move).
A=(src/egl/libEGL.a src/gbm/libgbm.a src/gbm/backends/dri/dri_gbm.a
	src/mesa/glapi/shared-glapi/libglapi.a src/gallium/drivers/v3d/libv3d.a
	src/gallium/drivers/v3d/libv3d-v42.a src/gallium/drivers/v3d/libv3d-v71.a
	src/broadcom/libbroadcom-v42.a src/broadcom/libbroadcom-v71.a src/broadcom/qpu/libbroadcom_qpu.a
	src/broadcom/libv3d_neon.a src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a
	src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a src/gallium/winsys/kmsro/drm/libkmsrowinsys.a
	src/gallium/winsys/v3d/drm/libv3dwinsys.a src/gallium/winsys/vc4/drm/libvc4winsys.a
	src/gallium/winsys/sw/kms-dri/libswkmsdri.a src/gallium/winsys/sw/dri/libswdri.a
	src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a
	src/c11/impl/libmesa_util_c11.a)
AA=()
for a in "${A[@]}"; do
	if [ -f "${MB}/${a}" ]; then AA+=("${MB}/${a}"); else log "  (archive not built: ${a})"; fi
done
GALLIUM_A="$(ls "${MB}"/src/gallium/targets/dri/libgallium-*.a)"
QS="${out}/quakespasm-drm"
log "quakespasm-drm: link"
"${PHXCXX}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,--wrap=mmap \
	-Wl,--wrap=SDL_GL_SwapWindow -Wl,-z,stack-size=33554432 -Wl,-Map,"${QS}.map" -o "${QS}" "${objs[@]}" \
	-Wl,--whole-archive "${GALLIUM_A}" -Wl,--no-whole-archive \
	-Wl,--start-group "${SDL_A}" "${GL_BRIDGE}" "${AA[@]}" "${LD_PREFIX}/lib/libdrm.a" \
	"${M}/compat/libmesadrm-compat.a" "${B}/lib/libz.a" -Wl,--end-group -lm > "${out}/qs-link.log" 2>&1 \
	|| { head -60 "${out}/qs-link.log"; die "quakespasm-drm link failed"; }
"${TC}-strip" -o "${QS}.stripped" "${QS}"
[ -s "${out}/qs-link.log" ] && sed 's/^/  link: /' "${out}/qs-link.log" | head -20

# --- 5. verification --------------------------------------------------------------------------
log "verify"
bad=0
if "${TC}-readelf" -l "${QS}" | grep -q INTERP; then log "  PT_INTERP present"; bad=1; fi
und="$("${TC}-nm" -u "${QS}" || true)"
log "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && { sed 's/^/    /' <<< "${und}" | head -20; bad=1; }
syms="$("${TC}-nm" "${QS}")"
for s in KMSDRM_CreateDevice KMSDRM_GLES_SwapWindow SDL_EGL_LoadLibrary SDL_PHOENIX_HID_Poll \
		__wrap_mmap drmPhoenixMmap drm_phoenix_ioctl gbmint_get_backend kmsro_drm_screen_create \
		v3d_drm_screen_create_renderonly glBegin eglGetPlatformDisplayEXT Host_Init __wrap_SDL_GL_SwapWindow; do
	if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then log "  symbol ${s}: yes"; else log "  symbol ${s}: NO"; bad=1; fi
done
for s in PHOENIX_bootstrap PHOENIX_PumpEvents phxgl_init phoenix_v3d_ioctl winsys_init v3da_connect; do
	if grep -qE " [TtWwDdBb] ${s}\$" <<< "${syms}"; then log "  forbidden symbol ${s}: PRESENT"; bad=1; fi
done
if grep -qE ' [Tt] dlopen$' <<< "${syms}"; then log "  note: dlopen is linked (libphoenix); SDL's loadso is the dummy one"; fi
strs="$(strings -a "${QS}.stripped")"
for s in 'KMS/DRM Video Driver' '/dev/dri/' 'libdrm-phoenix:' '/dev/kbd0' '/dev/audio0' 'EGL_KHR_platform_gbm' \
		"${qs_name}:" "${qs_name} flipstat" "${qs_name} swapstat" 'V3D 4.2' 'kmsro'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	log "  strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
# The old lane: in-process winsys, old SDL phoenix video driver, its GL-context glue, /dev/fb0.
for s in 'v3d-winsys:' 'v3da-winsys:' 'phxgl' '/dev/fb0' 'RPI4FB_GETMODE' 'phoenix_v3d_ioctl' 'peek_next_scanout' 'v3d-srv'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	log "  old-lane string '${s}': ${n}"
	[ "${n}" = 0 ] || bad=1
done
# The counter must sit in the engine's swap path: GL_EndRendering -> __wrap_SDL_GL_SwapWindow
# -> SDL_GL_SwapWindow, and no direct engine call may bypass the wrapper. A call is `bl` or,
# as GL_EndRendering's (its last statement), a tail-call `b`.
qs_dis="$("${TC}-objdump" -d --no-show-raw-insn "${QS}")"
ger="$(awk '/^[0-9a-f]+ <GL_EndRendering>:$/{f=1; next} f && /^$/{exit} f' <<< "${qs_dis}")"
if grep -qE '\sbl?\s+[0-9a-f]+ <__wrap_SDL_GL_SwapWindow>$' <<< "${ger}"; then
	log "  GL_EndRendering -> __wrap_SDL_GL_SwapWindow: yes"
else
	log "  GL_EndRendering -> __wrap_SDL_GL_SwapWindow: NO"; bad=1
fi
wrap_body="$(awk '/^[0-9a-f]+ <__wrap_SDL_GL_SwapWindow>:$/{f=1; next} f && /^$/{exit} f' <<< "${qs_dis}")"
if grep -qE '\sbl?\s+[0-9a-f]+ <SDL_GL_SwapWindow>$' <<< "${wrap_body}"; then
	log "  __wrap_SDL_GL_SwapWindow -> SDL_GL_SwapWindow: yes"
else
	log "  __wrap_SDL_GL_SwapWindow -> SDL_GL_SwapWindow: NO"; bad=1
fi
direct="$(grep -cE '\sbl?\s+[0-9a-f]+ <SDL_GL_SwapWindow>$' <<< "${qs_dis}" || true)"
log "  direct calls of the real SDL_GL_SwapWindow: ${direct} (expected 1, from the wrapper)"
[ "${direct}" = 1 ] || bad=1
"${TC}-size" "${QS}" | sed 's/^/  /'
log "  ${QS}: $(stat -c %s "${QS}") bytes; stripped $(stat -c %s "${QS}.stripped") bytes"
{
	echo "built:              $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "SDL:                ${SDL_VERSION} ($(sha "${SDL_TARBALL}")), patch/overlay set ${stamp}"
	echo "libSDL2.a:          $(sha "${SDL_A}") $(stat -c %s "${SDL_A}") bytes"
	echo "Mesa:               $(cat "${M}/mesa-src.stamp") (mesa-drm patch set), opengl=true"
	echo "libdrm-phoenix:     $(sed -n 3p "${M}/libdrm-snapshot.txt" | cut -c1-16) from ${libdrm_prefix}"
	echo "quakespasm:         ${QS_COMMIT} + $(basename "${QS_PATCH}") ($(sha "${QS_PATCH}"))"
	echo "gamedrm_hooks.c:    $(sha "${HOOKS_SRC}") (${qs_name}, desktop GL; --wrap=SDL_GL_SwapWindow)"
	echo "quakespasm-drm:     $(sha "${QS}") $(stat -c %s "${QS}") bytes"
	echo "quakespasm-drm.stripped: $(sha "${QS}.stripped") $(stat -c %s "${QS}.stripped") bytes"
} > "${out}/BUILD-INFO.txt"
sed 's/^/  /' "${out}/BUILD-INFO.txt"
[ "${bad}" = 0 ] || die "verification failed (see above)"
log "done: stage ${QS}.stripped as /usr/bin/quakespasm-drm"
