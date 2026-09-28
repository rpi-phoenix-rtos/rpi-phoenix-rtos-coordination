#!/usr/bin/env bash
#
# sdl2-wl (M8, windowed GPU games on the desktop): SDL 2.30.12 built STATIC with its stock
# WAYLAND video driver (xdg-shell windows, server-side decorations, wl_seat keyboard + pointer)
# next to the KMSDRM driver, on Mesa 26.2's EGL wayland platform with desktop GL + GLES
# (mesa-drm/build.sh --wayland --opengl), libwayland 1.24 + xkbcommon + the Wayland compat
# layer of the labwc-drm build (the stack labwc-2, foot-2 and the XFCE programs link), and
# libdrm-phoenix build-out-low (labwc's). Plus `quakespasm-wl`: a CLONE of the quakespasm port
# rebuilt against that SDL, as sdl2-drm/build.sh builds quakespasm-drm.
#
# Nothing of sdl2-drm is changed: its patches/0001-0009 and overlay/ are read by path and
# this directory adds patches/0101-* (the Wayland cmake wiring, a sigtimedwait-free clipboard
# pipe). Every output lands in <out> (default build-out/, gitignored):
#
#   <out>/sdl-src/ sdl-build/ sdl-prefix/   SDL (libSDL2.a + headers)
#   <out>/wl-include/linux/input.h          weston-drm's <linux/input.h> shim, alone
#   <out>/mesa-link/                        private copies of the Mesa archives that define or
#                                           call os_create_anonymous_file (renamed: libwayland-
#                                           cursor has its own, with another signature)
#   <out>/qs-src/ qs-obj/                   quakespasm at the port's commit + the port's patch
#   <out>/quakespasm-wl[.stripped|.map]     the clone (stage the .stripped)
#   <out>/link-inputs.txt                   the Wayland/Mesa link group, for the relink scripts
#   <out>/BUILD-INFO.txt, logs
#
# Reads (never writes): sources/phoenix-rtos-ports/{sdl2,quakespasm}, sdl2-drm/{patches,overlay},
# mesa-drm/build-out-wayland-gl (built by mesa-drm/build.sh, not from here), labwc-drm/build-out
# (prefix + deps/libffi), weston-drm/{compat,shims}/include, the tree sysroot + ports prefix
# (libffi.a, libz.a), the toolchain, E7's phx-g++. No Pi, no rebuild-rpi4b-fast.sh, no /srv.
#
# Usage: tools/gpu-lane/sdl2-wl/build.sh [--out <dir>] [--clean] [-j N] [--skip-sdl]
#                                        [--mesa-out <dir>] [--wl-prefix <dir>]
#   --skip-sdl   reuse <out>/sdl-prefix as is (relink quakespasm-wl only)
# Stage (coordinator only; new names only):
#   install -m 755 <out>/quakespasm-wl.stripped <live NFS export>/usr/bin/quakespasm-wl
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
G="${root}/tools/gpu-lane"
out="${here}/build-out"
jobs="$(nproc)"
clean=0
skip_sdl=0
M="${G}/mesa-drm/build-out-wayland-gl"
WLB="${G}/labwc-drm/build-out"
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--skip-sdl) skip_sdl=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		--mesa-out) shift; M="${1:?--mesa-out needs a directory}" ;;
		--mesa-out=*) M="${1#--mesa-out=}" ;;
		--wl-prefix) shift; WLB="${1:?--wl-prefix needs a labwc-drm build-out}" ;;
		--wl-prefix=*) WLB="${1#--wl-prefix=}" ;;
		-h|--help) sed -n '2,36p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

SDL_VERSION=2.30.12
PORTS="${root}/sources/phoenix-rtos-ports"
SDL_TARBALL="${PORTS}/sdl2/SDL2-${SDL_VERSION}.tar.gz"
SDRM="${G}/sdl2-drm"
QS_COMMIT=f5fe17864918239d443fe4c0d6bfb980e44d19e6
QS_TARBALL="${PORTS}/quakespasm/${QS_COMMIT}.tar.gz"
QS_PATCH="${PORTS}/quakespasm/patches/0001-quakespasm-phoenix-v3d-single-elf.patch"
QS_GLUE="${PORTS}/quakespasm/glue"
B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCXX="${G}/e7-drm-build/bin/phx-g++"
MB="${M}/mesa-build"
WLP="${WLB}/prefix"
WL_COMPAT_INC="${G}/weston-drm/compat/include"     # memfd_create, pipe2, F_ADD_SEALS...
INPUT_SHIM="${G}/weston-drm/shims/include/linux/input.h"
qs_name=quakespasm-wl

log() { printf '[sdl2-wl] %s\n' "$*"; }
die() { printf '[sdl2-wl] ERROR: %s\n' "$*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -c1-16; }

for p in "${SDL_TARBALL}" "${QS_TARBALL}" "${QS_PATCH}" "${QS_GLUE}/pl_phoenix_main.c" "${S}/lib/libphoenix.a" \
		"${TC}-gcc" "${TC}-nm" "${TC}-strip" "${TC}-size" "${TC}-readelf" "${TC}-objdump" "${TC}-objcopy" "${TC}-gcc-ar" \
		"${PHXCXX}" "${B}/lib/libz.a" "${B}/lib/libffi.a" "${M}/libdrm-prefix/lib/libdrm.a" "${M}/compat/libmesadrm-compat.a" \
		"${WLP}/lib/libwayland-client.a" "${WLP}/lib/libwayland-egl.a" "${WLP}/lib/libwayland-cursor.a" \
		"${WLP}/lib/libxkbcommon.a" "${WLP}/lib/libwlphx-compat.a" "${WLP}/lib/liblwphx-compat.a" \
		"${WLP}/include/evdev/input-event-codes.h" "${WLB}/deps/libffi/lib/pkgconfig/libffi.pc" \
		"${WL_COMPAT_INC}/sys/mman.h" "${INPUT_SHIM}"; do
	[ -e "${p}" ] || die "missing ${p}"
done
command -v wayland-scanner > /dev/null || die "host wayland-scanner not found"
grep -qx 'opengl=true' "${M}/mesa-opengl.txt" 2>/dev/null || die "${M} is not a desktop-GL Mesa build (mesa-drm/build.sh --wayland --opengl)"
grep -qx 'wayland=true' "${M}/mesa-wayland.txt" 2>/dev/null || die "${M} is not a --wayland Mesa build"
GL_BRIDGE="${MB}/src/mesa/glapi/glapi/libglapi_bridge.a"
[ -f "${GL_BRIDGE}" ] || die "missing ${GL_BRIDGE}"
LD_PREFIX="${M}/libdrm-prefix"   # the libdrm-phoenix snapshot Mesa was built with: one copy
mkdir -p "${out}"

TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

# <linux/input.h> (BTN_* / KEY_* codes for SDL_waylandevents.c): weston-drm's shim, which
# includes <evdev/input-event-codes.h> from the Wayland prefix. Only that header: the rest of
# weston-drm/shims/include (libudev, libevdev, linux/types.h...) stays off SDL's path.
mkdir -p "${out}/wl-include/linux"
cp "${INPUT_SHIM}" "${out}/wl-include/linux/input.h"

# --- 1. SDL source ----------------------------------------------------------------------------
SB="${out}/sdl-build"
SP="${out}/sdl-prefix"
if [ "${skip_sdl}" = 1 ]; then
	[ -f "${SP}/lib/libSDL2.a" ] || die "--skip-sdl: no ${SP}/lib/libSDL2.a"
	stamp="$(cat "${out}/sdl-src.stamp")"
	log "SDL: reusing ${SP} as is (--skip-sdl; set ${stamp})"
else
	src="${out}/sdl-src"
	stamp="$( (sha256sum "${SDL_TARBALL}"; cat "${SDRM}"/patches/*.patch; find "${SDRM}/overlay" -type f | sort | xargs cat
		cat "${here}"/patches/*.patch) | sha256sum | cut -c1-16)"
	if [ "$(cat "${out}/sdl-src.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		log "SDL ${SDL_VERSION} source: tarball + sdl2-drm's $(ls "${SDRM}"/patches/*.patch | wc -l) patches + overlay + $(ls "${here}"/patches/*.patch | wc -l) Wayland patches (set ${stamp})"
		rm -rf "${src}" "${SB}" "${SP}" "${out}/sdl-tmp"
		mkdir -p "${out}/sdl-tmp"
		tar -C "${out}/sdl-tmp" -xzf "${SDL_TARBALL}"
		mv "${out}/sdl-tmp/SDL2-${SDL_VERSION}" "${src}"
		rmdir "${out}/sdl-tmp"
		for p in "${SDRM}"/patches/*.patch; do
			patch -d "${src}" -p1 -s --no-backup-if-mismatch < "${p}" || die "patch failed: $(basename "${p}")"
		done
		cp -a "${SDRM}/overlay/." "${src}/"
		for p in "${here}"/patches/*.patch; do
			patch -d "${src}" -p1 -s --no-backup-if-mismatch < "${p}" || die "patch failed: $(basename "${p}")"
		done
		echo "${stamp}" > "${out}/sdl-src.stamp"
	fi

	# --- 2. SDL configure + build -----------------------------------------------------------------
	# pkg-config sees ONLY: Mesa's prefix (egl, gbm), its libdrm-phoenix snapshot, its private zlib,
	# the labwc-drm Wayland prefix (wayland-client/-egl/-cursor, xkbcommon, wlphx-compat) and that
	# build's private libffi view -- never the ports prefix (header poisoning) or the host's dirs.
	pkgc="${out}/pkg-config-sdl"
	cat > "${pkgc}" <<EOF
#!/bin/sh
# pkg-config restricted to the cross prefixes of the sdl2-wl build.
export PKG_CONFIG_LIBDIR=${M}/prefix/lib/pkgconfig:${LD_PREFIX}/lib/pkgconfig:${M}/zlib-prefix/lib/pkgconfig:${WLP}/lib/pkgconfig:${WLP}/share/pkgconfig:${WLB}/deps/libffi/lib/pkgconfig
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
# The .pc files carry -pthread, which aarch64-phoenix-gcc rejects (pthreads live in libphoenix).
o="\$(/usr/bin/pkg-config --static "\$@")" || exit \$?
[ -z "\$o" ] || printf '%s\n' "\$o" | sed -e 's/\(^\| \)-pthread\( \|\$\)/\1/g'
EOF
	chmod +x "${pkgc}"
	if [ ! -f "${SB}/Makefile" ]; then
		log "SDL cmake configure"
		mkdir -p "${SB}"
		sdl_cflags="${TFLAGS[*]} -O2 -g -std=gnu17 -I${WL_COMPAT_INC} -I${out}/wl-include"
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
			-DSDL_WAYLAND=ON \
			-DSDL_WAYLAND_SHARED=OFF \
			-DSDL_WAYLAND_LIBDECOR=OFF \
			-DSDL_WAYLAND_QT_TOUCH=OFF \
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

cfg="${SP}/include/SDL2/SDL_config.h"
for d in SDL_VIDEO_DRIVER_WAYLAND SDL_VIDEO_DRIVER_KMSDRM SDL_VIDEO_OPENGL_EGL SDL_VIDEO_OPENGL SDL_VIDEO_OPENGL_ES2 \
		SDL_INPUT_PHOENIX SDL_AUDIO_DRIVER_PHOENIX SDL_THREAD_PTHREAD SDL_TIMER_UNIX HAVE_MEMFD_CREATE; do
	grep -qE "^#define ${d} +1" "${cfg}" || die "SDL_config.h lacks ${d} 1"
done
for d in SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_EGL SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_CURSOR \
		SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_XKBCOMMON SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_LIBDECOR HAVE_LIBDECOR_H \
		SDL_VIDEO_DRIVER_KMSDRM_DYNAMIC SDL_VIDEO_DRIVER_PHOENIX SDL_VIDEO_DRIVER_X11 SDL_INPUT_LINUXEV \
		SDL_LOADSO_DLOPEN SDL_VIDEO_VULKAN; do
	if grep -qE "^#define ${d}( |$)" "${cfg}"; then die "SDL_config.h defines ${d}"; fi
done
log "  SDL_config.h: Wayland + KMSDRM (both static), EGL, GL + GLES2, memfd_create, Phoenix HID (KMSDRM) + audio; no dynamic loading, no libdecor"

# --- 3. the link group: Mesa (wayland + desktop GL), Wayland client stack, libdrm ---------------
# Mesa's util/anon_file.c and libwayland-cursor's os-compatibility.c both define a global
# os_create_anonymous_file() with different signatures (hidden from each other as shared
# libraries). Private copies of the Mesa archives that define or call it get Mesa's renamed
# (weston-drm/build.sh does the same for weston-simple-egl); everything else links in place.
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
ML="${out}/mesa-link"
mkdir -p "${ML}"
mesa_private() {  # archive -> path to link
	local a="$1" c t i m
	# (no `nm | grep -q` under pipefail: grep's early exit SIGPIPEs nm and fails the test)
	if ! grep -q ' os_create_anonymous_file$' <<< "$("${TC}-nm" "${a}" 2>/dev/null)"; then
		echo "${a}"
		return
	fi
	c="${ML}/$(echo "${a#"${MB}"/}" | tr '/' '_')"
	if [ ! -f "${c}" ] || [ "${a}" -nt "${c}" ]; then
		if [ "$(head -c 8 "${a}")" = '!<thin>' ]; then
			# meson's internal libraries are thin archives (objcopy cannot copy them): rebuild
			# a regular archive from the renamed members
			t="${c}.d"
			i=0
			rm -rf "${t}" "${c}"
			mkdir -p "${t}"
			while IFS= read -r m; do
				case "${m}" in /*) ;; *) m="$(dirname "${a}")/${m}" ;; esac
				i=$((i + 1))
				"${TC}-objcopy" --redefine-sym os_create_anonymous_file=mesa_os_create_anonymous_file "${m}" \
					"${t}/$(printf '%04d' "${i}")-$(basename "${m}")"
			done < <("${TC}-gcc-ar" t "${a}")
			"${TC}-gcc-ar" rcs "${c}" "${t}"/*.o
			rm -rf "${t}"
		else
			"${TC}-objcopy" --redefine-sym os_create_anonymous_file=mesa_os_create_anonymous_file "${a}" "${c}"
		fi
	fi
	echo "${c}"
}
GALLIUM_A="$(ls "${MB}"/src/gallium/targets/dri/libgallium-*.a)"
GALLIUM_L="$(mesa_private "${GALLIUM_A}")"
MESA_GL=()   # desktop GL: libglapi_bridge.a (libGLESv2.a's gl* would clash with it)
MESA_ES=()   # GLES: libGLESv2.a
for a in "${A[@]}"; do
	[ -f "${MB}/${a}" ] || die "missing Mesa archive ${MB}/${a}"
	MESA_GL+=("$(mesa_private "${MB}/${a}")")
done
MESA_ES=("${MESA_GL[@]}" "$(mesa_private "${MB}/src/mesa/glapi/es2api/libGLESv2.a")")
MESA_GL+=("$(mesa_private "${GL_BRIDGE}")")
WL_LIBS=("${WLP}/lib/libwayland-client.a" "${WLP}/lib/libwayland-egl.a" "${WLP}/lib/libwayland-cursor.a"
	"${WLP}/lib/libxkbcommon.a" "${WLP}/lib/liblwphx-compat.a" "${WLP}/lib/libwlphx-compat.a" "${B}/lib/libffi.a")
TAIL=("${LD_PREFIX}/lib/libdrm.a" "${M}/compat/libmesadrm-compat.a" "${B}/lib/libz.a")
# The flags every Wayland GPU client of the new lane links with (weston-simple-egl's LINK_DRM):
# libdrm-phoenix's --wrap=mmap (BO-token maps) and --wrap=ioctl (sync-file ioctls), and the
# compat layer's --wrap=close/write (epoll/eventfd emulation), pulled with -u.
WRAPS=(-Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,--wrap=close -Wl,--wrap=write -Wl,-u,__wrap_close -Wl,-u,__wrap_write)
# For the relink scripts (build-quake2-wl.sh & co.): one line per item, in link order.
{
	echo "gallium ${GALLIUM_L}"
	echo "sdl ${SDL_A}"
	for a in "${MESA_GL[@]}"; do echo "mesa-gl ${a}"; done
	for a in "${MESA_ES[@]}"; do echo "mesa-es ${a}"; done
	for a in "${WL_LIBS[@]}" "${TAIL[@]}"; do echo "tail ${a}"; done
	for w in "${WRAPS[@]}"; do echo "flag ${w}"; done
} > "${out}/link-inputs.txt"

# --- 4. quakespasm-wl -------------------------------------------------------------------------
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
# TU lists = sdl2-drm/build.sh's (the port's p_build without the old GL-context glue).
globjs=(gl_refrag gl_rlight gl_rmain gl_fog gl_rmisc r_part r_world gl_screen gl_sky
	gl_warp gl_draw image gl_texmgr gl_mesh r_sprite r_alias r_brush gl_model)
core=(strlcat strlcpy net_dgrm net_loop net_main net_udp chase cl_demo cl_input
	cl_main cl_parse cl_tent console keys menu sbar view wad cmd common miniz crc
	cvar cfgfile host host_cmd mathlib pr_cmds pr_edict pr_exec sv_main sv_move
	sv_phys sv_user world zone snd_dma snd_mix snd_mem bgmusic cd_null snd_codec)
sdlbk=(gl_vidsdl in_sdl snd_sdl)
shims=(pl_phoenix_sys pl_phoenix_main pl_phoenix_stubs)
QFLAGS=("${TFLAGS[@]}" -fomit-frame-pointer -std=gnu17 -c -O2 -g -ffreestanding -fno-strict-aliasing -Wno-error
	-DUSE_SDL2 -DNO_SDL_CONFIG -I"${Q}" -I"${M}/mesa-src/include" -I"${SP}/include" -I"${SP}/include/SDL2")
QO="${out}/qs-obj"
rm -rf "${QO}"
mkdir -p "${QO}"
: > "${out}/qs-cc.log"
log "quakespasm-wl: compiling $(( ${#globjs[@]} + ${#core[@]} + ${#sdlbk[@]} + ${#shims[@]} + 1 )) TUs"
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
HOOKS_SRC="${here}/gamewl/gamewl_hooks.c"
HOOKS_O="${QO}/gamewl_hooks.o"
"${TC}-gcc" -O2 -g -std=gnu17 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SP}/include" \
	-DGAMEWL_NAME="\"${qs_name}\"" -DGAMEWL_API='"desktop GL"' -c "${HOOKS_SRC}" -o "${HOOKS_O}" \
	|| die "gamewl_hooks.c compile failed"
objs+=("${HOOKS_O}")
log "  compiled ${#objs[@]} objects ($(grep -c 'warning:' "${out}/qs-cc.log" || true) warning line(s), ${out}/qs-cc.log)"

QS="${out}/quakespasm-wl"
log "quakespasm-wl: link"
"${PHXCXX}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 "${WRAPS[@]}" \
	-Wl,--wrap=SDL_GL_SwapWindow -Wl,-z,stack-size=33554432 -Wl,-Map,"${QS}.map" -o "${QS}" "${objs[@]}" \
	-Wl,--whole-archive "${GALLIUM_L}" -Wl,--no-whole-archive \
	-Wl,--start-group "${SDL_A}" "${MESA_GL[@]}" "${WL_LIBS[@]}" "${TAIL[@]}" -Wl,--end-group -lm \
	> "${out}/qs-link.log" 2>&1 || { head -60 "${out}/qs-link.log"; die "quakespasm-wl link failed"; }
"${TC}-strip" -o "${QS}.stripped" "${QS}"
nlw="$(grep -v -E 'warning: .*(is not fully supported|dlopen|getpwnam|getpwuid|getgrnam|initgroups)' "${out}/qs-link.log" | grep -c 'warning' || true)"
log "  link warnings beyond libphoenix attribute notes: ${nlw} (${out}/qs-link.log)"

# --- 5. verification --------------------------------------------------------------------------
log "verify"
bad=0
if "${TC}-readelf" -l "${QS}" | grep -q INTERP; then log "  PT_INTERP present"; bad=1; fi
und="$("${TC}-nm" -u "${QS}" || true)"
log "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && { sed 's/^/    /' <<< "${und}" | head -20; bad=1; }
syms="$("${TC}-nm" "${QS}")"
for s in Wayland_CreateDevice Wayland_GLES_SwapWindow Wayland_GLES_CreateContext Wayland_PumpEvents \
		KMSDRM_CreateDevice SDL_EGL_LoadLibrary wl_display_connect wl_egl_window_create wl_cursor_theme_load \
		xkb_keymap_new_from_string xkb_state_update_mask dri2_initialize_wayland __wrap_mmap __wrap_ioctl \
		__wrap_close __wrap_write memfd_create pipe2 drm_phoenix_ioctl kmsro_drm_screen_create \
		v3d_drm_screen_create_renderonly glBegin eglGetPlatformDisplayEXT mesa_os_create_anonymous_file \
		os_create_anonymous_file Host_Init __wrap_SDL_GL_SwapWindow; do
	if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then log "  symbol ${s}: yes"; else log "  symbol ${s}: NO"; bad=1; fi
done
for s in PHOENIX_bootstrap PHOENIX_PumpEvents phxgl_init phoenix_v3d_ioctl winsys_init v3da_connect libdecor_new; do
	if grep -qE " [TtWwDdBb] ${s}\$" <<< "${syms}"; then log "  forbidden symbol ${s}: PRESENT"; bad=1; fi
done
strs="$(strings -a "${QS}.stripped")"
# SDL's driver name strings: "wayland" + "SDL Wayland video driver", "KMSDRM" + "KMS/DRM Video Driver"
for s in 'SDL Wayland video driver' 'KMS/DRM Video Driver' 'xdg_wm_base' 'zxdg_decoration_manager_v1' \
		'zwp_relative_pointer_manager_v1' 'zwp_pointer_constraints_v1' 'zwp_linux_dmabuf_v1' 'wl_seat' \
		'WAYLAND_DISPLAY' 'XDG_RUNTIME_DIR' 'libdrm-phoenix:' '/dev/dri/' 'V3D 4.2' 'kmsro' \
		"${qs_name}: windowed GPU game" "${qs_name} flipstat" "${qs_name} swapstat" 'video_driver'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	log "  strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
for s in 'v3d-winsys:' 'v3da-winsys:' 'phxgl' '/dev/fb0' 'RPI4FB_GETMODE' 'phoenix_v3d_ioctl' 'peek_next_scanout' 'v3d-srv' \
		'libdecor-'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	log "  old-lane / unwanted string '${s}': ${n}"
	[ "${n}" = 0 ] || bad=1
done
# The counter must sit in the engine's swap path: GL_EndRendering -> __wrap_SDL_GL_SwapWindow
# -> SDL_GL_SwapWindow, and no direct engine call may bypass the wrapper; only the wrappers
# may call the real ioctl() and mmap().
qs_dis="$("${TC}-objdump" -d --no-show-raw-insn "${QS}")"
calls="$(awk '
	/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn); next }
	/\tbl?\t/ && / <(ioctl|mmap|__wrap_SDL_GL_SwapWindow|SDL_GL_SwapWindow)>$/ {
		t = $NF; gsub(/[<>]/, "", t); print t, fn }' <<< "${qs_dis}" | sort | uniq -c)"
printf '%s\n' "${calls}" > "${out}/call-sites.txt"
if grep -qE ' __wrap_SDL_GL_SwapWindow GL_EndRendering$' <<< "${calls}" \
		&& grep -qE ' SDL_GL_SwapWindow __wrap_SDL_GL_SwapWindow$' <<< "${calls}" \
		&& ! awk '$2 == "SDL_GL_SwapWindow" && $3 != "__wrap_SDL_GL_SwapWindow" { f = 1 } END { exit !f }' <<< "${calls}"; then
	log "  GL_EndRendering -> __wrap_SDL_GL_SwapWindow -> SDL_GL_SwapWindow (the wrapper is the only caller): yes"
else
	log "  the SDL_GL_SwapWindow wrap is NOT in the engine's swap path:"; sed 's/^/    /' <<< "${calls}"; bad=1
fi
awk '$2 == "ioctl" && $3 != "__wrap_ioctl" { b = 1 } $2 == "mmap" && $3 != "__wrap_mmap" { b = 1 } END { exit b }' <<< "${calls}" \
	&& log "  real ioctl()/mmap() called only from __wrap_ioctl/__wrap_mmap: yes" \
	|| { log "  real ioctl()/mmap() called from outside the wrappers:"; sed 's/^/    /' <<< "${calls}"; bad=1; }
"${TC}-size" "${QS}" | sed 's/^/  /'
log "  ${QS}: $(stat -c %s "${QS}") bytes; stripped $(stat -c %s "${QS}.stripped") bytes"
{
	echo "built:              $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "SDL:                ${SDL_VERSION} ($(sha "${SDL_TARBALL}")), set ${stamp} = sdl2-drm patches 0001-0009 + overlay + sdl2-wl $(cd "${here}/patches" && ls | tr '\n' ' ')"
	echo "libSDL2.a:          $(sha "${SDL_A}") $(stat -c %s "${SDL_A}") bytes"
	echo "Mesa:               ${M}: patch set $(cat "${M}/mesa-src.stamp"), opengl=true wayland=true, libgallium $(sha "${GALLIUM_A}")"
	echo "libdrm-phoenix:     $(sed -n 3p "${M}/libdrm-snapshot.txt" | cut -c1-16) ($(sed -n 1p "${M}/libdrm-snapshot.txt"))"
	echo "Wayland stack:      ${WLP}: libwayland-client $(sha "${WLP}/lib/libwayland-client.a"), xkbcommon $(sha "${WLP}/lib/libxkbcommon.a"), wlphx-compat $(sha "${WLP}/lib/libwlphx-compat.a")"
	echo "libphoenix.a:       $(sha "${S}/lib/libphoenix.a")"
	echo "quakespasm:         ${QS_COMMIT} + $(basename "${QS_PATCH}") ($(sha "${QS_PATCH}"))"
	echo "gamewl_hooks.c:     $(sha "${HOOKS_SRC}") (${qs_name}, desktop GL; --wrap=SDL_GL_SwapWindow)"
	echo "quakespasm-wl:      $(sha "${QS}") $(stat -c %s "${QS}") bytes"
	echo "quakespasm-wl.stripped: $(sha256sum "${QS}.stripped" | cut -d' ' -f1) $(stat -c %s "${QS}.stripped") bytes"
} > "${out}/BUILD-INFO.txt"
sed 's/^/  /' "${out}/BUILD-INFO.txt"
[ "${bad}" = 0 ] || die "verification failed (see above)"
log "done: stage ${QS}.stripped as /usr/bin/quakespasm-wl"
