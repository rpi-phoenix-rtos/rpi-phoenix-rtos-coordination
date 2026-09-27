#!/usr/bin/env bash
#
# weston-drm (new GPU lane, M6 preparation): Weston 14 with ONLY the DRM backend,
# the GL renderer (Mesa GBM/EGL/GLES from mesa-drm) and the kiosk shell, cross-built
# STATIC for aarch64-phoenix, plus its Wayland stack and two demo clients.
#
#   <out>/dl/              pinned source tarballs (sha256 below)
#   <out>/src/<pkg>/       extracted + patches/<pkg>/*.patch applied (git apply)
#   <out>/prefix/          everything installed for the target: libwayland-{server,
#                          client,egl,cursor}.a, wayland-protocols, libxkbcommon.a,
#                          libdisplay-info.a, libseat.a, the udev/libinput shims,
#                          libwlphx-compat.a, headers, *.pc
#   <out>/deps/            private views of the ports prefix (libffi, expat, pixman:
#                          exactly their headers -- the ports include dir also holds
#                          GL/ and X11/ headers, never put it on a search path)
#   <out>/libdrm-prefix/   a snapshot of libdrm-phoenix (see --libdrm-prefix)
#   <out>/weston-build/    meson build dir of Weston (static archives + objects)
#   <out>/weston           static, unstripped (addr2line); weston-stripped (stage this)
#   <out>/weston-simple-shm[-stripped], weston-simple-egl[-stripped]
#   <out>/shmsrv[-stripped]   the /shm server (memfd_create backing, shmsrv/)
#   <out>/keymap-us.xkb    the baked default keymap (compiled on the host)
#
# Writes only into <out> (default build-out/, gitignored) -- and, for the wayland
# EGL platform, mesa-drm's own build-out-wayland/ through `mesa-drm/build.sh
# --wayland`. Reads the tree sysroot, the ports prefix (libffi, expat, pixman, zlib),
# the toolchain, the E7 compiler wrappers. No Pi, no rebuild-rpi4b-fast.sh, no /srv.
#
# Host tools used at build time: wayland-scanner (must equal the libwayland version,
# 1.24.0), meson, ninja, bison, and xkbcli/xkeyboard-config for the baked keymap (a
# native libxkbcommon is built for it when the host has no xkbcli).
#
# Usage: tools/gpu-lane/weston-drm/build.sh [--clean] [--out <dir>] [-j N]
#            [--libdrm-prefix <dir>] [--mesa-out <dir>] [--relink] [--no-mesa]
#   --relink     skip the libraries, shims and meson; recompile weston_builtin.c + shmsrv and
#                relink the four programs (a changed shim or compat source needs a full run)
#   --no-mesa    do not (re)build mesa-drm's wayland Mesa; use what --mesa-out has
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
jobs="$(nproc)"
clean=0
relink=0
build_mesa=1
libdrm_src_prefix="${root}/tools/gpu-lane/libdrm-phoenix/build-out-m5b/prefix"
mesa_out="${root}/tools/gpu-lane/mesa-drm/build-out-wayland"
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--relink) relink=1 ;;
		--no-mesa) build_mesa=0 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		--libdrm-prefix) shift; libdrm_src_prefix="${1:?}" ;;
		--libdrm-prefix=*) libdrm_src_prefix="${1#--libdrm-prefix=}" ;;
		--mesa-out) shift; mesa_out="${1:?}" ;;
		--mesa-out=*) mesa_out="${1#--mesa-out=}" ;;
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

B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCC="${root}/tools/gpu-lane/e7-drm-build/bin/phx-gcc"
PHXCXX="${root}/tools/gpu-lane/e7-drm-build/bin/phx-g++"
COMPAT_INC="${here}/compat/include"
MESA_COMPAT_INC="${root}/tools/gpu-lane/mesa-drm/compat/include"   # generic libphoenix-gap shims (M3/M4)
P="${out}/prefix"
D="${out}/deps"
LD_PREFIX="${out}/libdrm-prefix"

# name|file|url|sha256
PKGS=(
	"wayland|wayland-1.24.0.tar.xz|https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.24.0/downloads/wayland-1.24.0.tar.xz|82892487a01ad67b334eca83b54317a7c86a03a89cfadacfef5211f11a5d0536"
	"wayland-protocols|wayland-protocols-1.45.tar.xz|https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/1.45/downloads/wayland-protocols-1.45.tar.xz|4d2b2a9e3e099d017dc8107bf1c334d27bb87d9e4aff19a0c8d856d17cd41ef0"
	"libxkbcommon|libxkbcommon-1.7.0.tar.xz|https://xkbcommon.org/download/libxkbcommon-1.7.0.tar.xz|65782f0a10a4b455af9c6baab7040e2f537520caa2ec2092805cdfd36863b247"
	"libdisplay-info|libdisplay-info-0.2.0.tar.xz|https://gitlab.freedesktop.org/emersion/libdisplay-info/-/releases/0.2.0/downloads/libdisplay-info-0.2.0.tar.xz|5a2f002a16f42dd3540c8846f80a90b8f4bdcd067a94b9d2087bc2feae974176"
	"seatd|seatd-0.9.1.tar.gz|https://git.sr.ht/~kennylevinsen/seatd/archive/0.9.1.tar.gz|819979c922a0be258aed133d93920bce6a3d3565a60588d6d372ce9db2712cd3"
	"libinput|libinput-1.26.2.tar.gz|https://gitlab.freedesktop.org/libinput/libinput/-/archive/1.26.2/libinput-1.26.2.tar.gz|5c1c4150f217fea1db2d1fd88e2607b2f1928cfde65c34da65a9f24dcfd69464"
	"weston|weston-14.0.2.tar.xz|https://gitlab.freedesktop.org/wayland/weston/-/releases/14.0.2/downloads/weston-14.0.2.tar.xz|b47216b3530da76d02a3a1acbf1846a9cd41d24caa86448f9c46f78f20b6e0ac"
)
WAYLAND_VERSION=1.24.0
# FreeBSD's BSD-2 copy of the evdev event codes (<linux/input.h> shim, shims/include/linux/input.h)
EVDEV_CODES_COMMIT=f492ef8318f580081047da41905c3b339e924387
EVDEV_CODES_SHA=fc9c4946818cefcec359ad3a619d448c4cafffa4f8f9571894516fb980b26142
SHIM_INC="${here}/shims/include"

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-gcc-ar" "${TC}-nm" "${TC}-strip" "${PHXCC}" "${PHXCXX}" \
		"${B}/lib/libffi.a" "${B}/lib/libexpat.a" "${B}/lib/libpixman-1.a" "${B}/lib/libz.a" \
		"${libdrm_src_prefix}/lib/libdrm.a"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
for t in meson ninja wayland-scanner bison; do
	command -v "${t}" > /dev/null || { echo "build.sh: host tool ${t} not found" >&2; exit 1; }
done
[ "$(wayland-scanner --version 2>&1 | awk '{print $2}')" = "${WAYLAND_VERSION}" ] \
	|| { echo "build.sh: host wayland-scanner is not ${WAYLAND_VERSION}" >&2; exit 1; }
libphx_syms="$("${TC}-nm" -g --defined-only "${S}/lib/libphoenix.a" 2>/dev/null || true)"
has_libc() { grep -qE " [TW] $1\$" <<< "${libphx_syms}"; }
for f in memExport sys_fdpath; do
	has_libc "${f}" || { echo "build.sh: ${S}/lib/libphoenix.a has no ${f} (stale sysroot)" >&2; exit 1; }
done

mkdir -p "${out}/dl" "${out}/src" "${P}/lib/pkgconfig" "${P}/include" "${D}"
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

# --- sources -----------------------------------------------------------------------------
fetch_extract() {  # name -> ${out}/src/<name>, patched
	local name="$1" rec file url sum dir stamp
	for rec in "${PKGS[@]}"; do
		[ "${rec%%|*}" = "${name}" ] && break
	done
	IFS='|' read -r _ file url sum <<< "${rec}"
	if [ ! -f "${out}/dl/${file}" ]; then
		echo "  fetch ${file}"
		curl -sSfL -o "${out}/dl/${file}.part" "${url}"
		mv "${out}/dl/${file}.part" "${out}/dl/${file}"
	fi
	echo "${sum}  ${out}/dl/${file}" | sha256sum -c --quiet - || { echo "build.sh: ${file}: sha256 mismatch" >&2; exit 1; }
	dir="${out}/src/${name}"
	stamp="$( { echo "${sum}"; cat "${here}/patches/${name}"/*.patch 2>/dev/null || true; } | sha256sum | cut -c1-16)"
	if [ "$(cat "${dir}.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		rm -rf "${dir}" "${dir}.tmp"
		mkdir -p "${dir}.tmp"
		tar -xf "${out}/dl/${file}" -C "${dir}.tmp" --strip-components=1
		mv "${dir}.tmp" "${dir}"
		# Its own git repository: `git apply` inside a directory of ANOTHER repository (this
		# one) silently skips every path (it applies relative to that repository's root).
		git -C "${dir}" init -q
		git -C "${dir}" add -A
		git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "${file}"
		for p in "${here}/patches/${name}"/*.patch; do
			[ -e "${p}" ] || continue
			echo "  apply ${name}/$(basename "${p}")"
			git -C "${dir}" apply --whitespace=nowarn "${p}"
			git -C "${dir}" add -A
			git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "$(basename "${p}")"
		done
		git -C "${dir}" diff "$(git -C "${dir}" rev-list --max-parents=0 HEAD)" > "${out}/${name}-full.patch"
		echo "${stamp}" > "${dir}.stamp"
		rm -f "${out}/${name}.built"   # a changed source rebuilds the package
	fi
}

# --- private dependency views + libdrm snapshot ---------------------------------------------
dep_view() {  # name lib-basename version cflags-subdir headers...
	local name="$1" lib="$2" ver="$3" sub="$4"
	shift 4
	mkdir -p "${D}/${name}/include/${sub}" "${D}/${name}/lib/pkgconfig"
	local h
	for h in "$@"; do cp "${B}/include/${h}" "${D}/${name}/include/${sub}/"; done
	cp "${B}/lib/lib${lib}.a" "${D}/${name}/lib/"
	printf '%s\n' "prefix=${D}/${name}" "Name: ${name}" "Description: ${name} from the Phoenix ports prefix" \
		"Version: ${ver}" "Libs: -L\${prefix}/lib -l${lib}" "Cflags: -I\${prefix}/include${sub:+/${sub}}" \
		> "${D}/${name}/lib/pkgconfig/${name}.pc"
}

# --- meson cross file + pkg-config ----------------------------------------------------------
write_cross() {
	local cross="${out}/phoenix-aarch64.cross" pkgc="${out}/pkg-config-phoenix"
	cat > "${pkgc}" <<EOF
#!/bin/sh
# pkg-config restricted to this build's prefix, the private ports views and the libdrm snapshot.
export PKG_CONFIG_LIBDIR=${P}/lib/pkgconfig:${P}/share/pkgconfig:${D}/libffi/lib/pkgconfig:${D}/expat/lib/pkgconfig:${D}/pixman-1/lib/pkgconfig:${D}/zlib/lib/pkgconfig:${LD_PREFIX}/lib/pkgconfig:${MESA_PC:-/nonexistent}
unset PKG_CONFIG_PATH
exec /usr/bin/pkg-config --static "\$@"
EOF
	chmod +x "${pkgc}"
	# compat/include first (epoll/timerfd/signalfd/memfd/sealing), then mesa-drm's generic
	# libphoenix-gap shims (sys/file.h LOCK_*, static_assert, SCNxPTR, _SC_PHYS_PAGES...).
	local flags="'--sysroot=${S}/', '-B${S}/lib/', '-mcpu=cortex-a72', '-mtune=cortex-a72', '-mstrict-align', '-mno-outline-atomics', '-ffunction-sections', '-fdata-sections', '-I${COMPAT_INC}', '-I${MESA_COMPAT_INC}'"
	local lflags="'--sysroot=${S}/', '-B${S}/lib/', '-L${B}/lib', '-Wl,-z,max-page-size=0x1000'"
	# Weston's own configure probes (memfd_create, posix_fallocate...) and the links meson
	# does itself must see the compat archive: HAVE_MEMFD_CREATE routes weston's anonymous
	# files (wl_shm pools of its clients, keymaps) to shmsrv.
	local lflags_weston="${lflags}, '-Wl,-u,__wrap_close', '-Wl,-u,__wrap_write', '${P}/lib/libwlphx-compat.a', '-Wl,--wrap=close', '-Wl,--wrap=write'"
	cat > "${cross}" <<EOF
# Generated by tools/gpu-lane/weston-drm/build.sh (aarch64-phoenix, Pi 4).
[binaries]
c = '${PHXCC}'
cpp = '${PHXCXX}'
ar = '${TC}-gcc-ar'
nm = '${TC}-nm'
strip = '${TC}-strip'
objcopy = '${TC}-objcopy'
pkg-config = '${pkgc}'

[host_machine]
system = 'phoenix'
cpu_family = 'aarch64'
cpu = 'cortex-a72'
endian = 'little'

[properties]
needs_exe_wrapper = true

[built-in options]
c_args = [${flags}]
cpp_args = [${flags}]
c_link_args = [${lflags}]
cpp_link_args = [${lflags}]
default_library = 'static'
EOF
	sed -e "s|^c_link_args = .*|c_link_args = [${lflags_weston}]|" -e "s|^cpp_link_args = .*|cpp_link_args = [${lflags_weston}]|" \
		"${cross}" > "${out}/phoenix-aarch64-weston.cross"
}

meson_pkg() {  # [--cross <file>] name builddir-name meson-args...
	local cross="${out}/phoenix-aarch64.cross"
	if [ "$1" = --cross ]; then cross="$2"; shift 2; fi
	local name="$1" bname="$2"
	shift 2
	local bd="${out}/${bname}"
	if [ -f "${out}/${name}.built" ]; then
		echo "  ${name}: up to date"
		return 0
	fi
	rm -rf "${bd}"
	meson setup "${bd}" "${out}/src/${name}" --cross-file "${cross}" --prefix "${P}" \
		--libdir lib --buildtype=debugoptimized -Db_staticpic=false --wrap-mode=nodownload "$@" \
		> "${out}/${bname}-setup.log" 2>&1 || { tail -40 "${out}/${bname}-setup.log"; exit 1; }
	ninja -C "${bd}" -j"${jobs}" > "${out}/${bname}-ninja.log" 2>&1 || { grep -E -A5 'error|FAILED' "${out}/${bname}-ninja.log" | head -60; exit 1; }
	ninja -C "${bd}" install > "${out}/${bname}-install.log" 2>&1 || { tail -20 "${out}/${bname}-install.log"; exit 1; }
	echo "  ${name}: built ($(grep -c 'warning:' "${out}/${bname}-ninja.log" || true) warning line(s))"
	touch "${out}/${name}.built"
}

if [ "${relink}" = 0 ]; then
	echo "== sources"
	for rec in "${PKGS[@]}"; do fetch_extract "${rec%%|*}"; done

	echo "== dependency views (ports prefix) + libdrm-phoenix snapshot"
	dep_view libffi ffi "$(sed -n 's/^Version: //p' "${B}/lib/pkgconfig/libffi.pc")" "" ffi.h ffitarget.h
	dep_view expat expat "$(sed -n 's/^Version: //p' "${B}/lib/pkgconfig/expat.pc")" "" expat.h expat_config.h expat_external.h
	dep_view pixman-1 pixman-1 "$(sed -n 's/^Version: //p' "${B}/lib/pkgconfig/pixman-1.pc")" pixman-1 pixman-1/pixman.h pixman-1/pixman-version.h
	dep_view zlib z "$(sed -n 's/^#define ZLIB_VERSION "\(.*\)"/\1/p' "${B}/include/zlib.h")" "" zlib.h zconf.h
	rm -rf "${LD_PREFIX}"
	mkdir -p "${LD_PREFIX}/lib/pkgconfig"
	cp -a "${libdrm_src_prefix}/include" "${LD_PREFIX}/"
	cp -a "${libdrm_src_prefix}/lib/libdrm.a" "${LD_PREFIX}/lib/"
	# The wraps every program that maps DRM buffers or uses emulated sync files needs (M3 §2.6, M5 §9.3).
	cat > "${LD_PREFIX}/lib/pkgconfig/libdrm.pc" <<EOF
prefix=${LD_PREFIX}
includedir=\${prefix}/include
libdir=\${prefix}/lib

Name: libdrm
Description: libdrm-phoenix snapshot (upstream libdrm + Phoenix backend)
Version: 2.4.134
Libs: -L\${libdir} -ldrm -Wl,--wrap=mmap -Wl,--wrap=ioctl
Cflags: -I\${includedir} -I\${includedir}/libdrm
EOF
	{ echo "source: ${libdrm_src_prefix}"; sha256sum "${LD_PREFIX}/lib/libdrm.a"; } > "${out}/libdrm-snapshot.txt"
	sed 's/^/  /' "${out}/libdrm-snapshot.txt"

	echo "== compat library (epoll/timerfd/signalfd, memfd_create over shmsrv)"
	mkdir -p "${out}/compat-obj"
	compat_defs=()
	has_libc msync || compat_defs+=(-DWLPHX_NEED_MSYNC)
	has_libc pipe2 || compat_defs+=(-DWLPHX_NEED_PIPE2)
	for f in wlphx_epoll wlphx_memfd wlphx_misc; do
		"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${COMPAT_INC}" -I"${MESA_COMPAT_INC}" \
			"${compat_defs[@]}" -c "${here}/compat/src/${f}.c" -o "${out}/compat-obj/${f}.o"
	done
	echo "  stand-ins: ${compat_defs[*]:-none}"
	rm -f "${P}/lib/libwlphx-compat.a"
	"${TC}-gcc-ar" rcs "${P}/lib/libwlphx-compat.a" "${out}/compat-obj/"*.o
	cat > "${P}/lib/pkgconfig/wlphx-compat.pc" <<EOF
prefix=${P}
Name: wlphx-compat
Description: weston-drm libphoenix-gap shims (epoll, timerfd, signalfd, eventfd, ppoll, memfd_create, msync)
Version: 1.0
Libs: -Wl,-u,__wrap_close -Wl,-u,__wrap_write -L\${prefix}/lib -lwlphx-compat -Wl,--wrap=close -Wl,--wrap=write
Cflags:
EOF

	MESA_PC="${mesa_out}/prefix/lib/pkgconfig"
	write_cross
fi

# --- libraries -------------------------------------------------------------------------------
# Every installed wayland-{server,client}.pc pulls the compat archive (epoll & co. for the
# event loop, --wrap=close) into whatever links it.
pc_require_compat() {
	local pc
	for pc in "$@"; do
		grep -q 'wlphx-compat' "${pc}" || printf 'Requires.private: wlphx-compat\n' >> "${pc}"
	done
}

if [ "${relink}" = 0 ]; then
	echo "== libwayland ${WAYLAND_VERSION} (scanner on the host)"
	# The weston cross file: its link probes see the compat archive, so libwayland-cursor's
	# anonymous files (cursor theme pools) use memfd_create = shmsrv, as Weston's do.
	meson_pkg --cross "${out}/phoenix-aarch64-weston.cross" wayland wayland-build -Dlibraries=true -Dscanner=false -Dtests=false -Ddocumentation=false \
		-Ddtd_validation=false
	pc_require_compat "${P}/lib/pkgconfig/wayland-server.pc" "${P}/lib/pkgconfig/wayland-client.pc"

	echo "== wayland-protocols"
	meson_pkg wayland-protocols wayland-protocols-build -Dtests=false

	echo "== libxkbcommon (no X11/wayland tools, no registry)"
	meson_pkg libxkbcommon xkbcommon-build -Denable-x11=false -Denable-wayland=false -Denable-docs=false \
		-Denable-tools=false -Denable-xkbregistry=false -Denable-bash-completion=false \
		-Dxkb-config-root=/usr/share/X11/xkb -Dx-locale-root=/usr/share/X11/locale

	echo "== libdisplay-info"
	meson_pkg libdisplay-info display-info-build

	echo "== libseat (noop backend only: opens devices directly)"
	meson_pkg seatd seatd-build -Dlibseat-logind=disabled -Dlibseat-seatd=disabled -Dlibseat-builtin=disabled \
		-Dserver=disabled -Dexamples=disabled -Dman-pages=disabled -Dwerror=false
fi

# --- shims: libudev (fixed device table), libinput-phoenix (usbkbd/usbmouse), libevdev -----
# Only libinput.h is used from the libinput tarball (MIT); the implementation is ours.
if [ "${relink}" = 0 ]; then
	echo "== shims (libudev, libinput-phoenix, libevdev, <linux/input.h>)"
	mkdir -p "${P}/include/evdev" "${out}/shim-obj"
	evc="${P}/include/evdev/input-event-codes.h"
	if [ ! -f "${evc}" ] || ! echo "${EVDEV_CODES_SHA}  ${evc}" | sha256sum -c --quiet - > /dev/null 2>&1; then
		if [ -d "${root}/external/freebsd-src/.git" ] && \
				git -C "${root}/external/freebsd-src" show "${EVDEV_CODES_COMMIT}:sys/dev/evdev/input-event-codes.h" > "${evc}" 2> /dev/null; then
			:
		else
			curl -sSfL -o "${evc}" "https://raw.githubusercontent.com/freebsd/freebsd-src/${EVDEV_CODES_COMMIT}/sys/dev/evdev/input-event-codes.h"
		fi
		echo "${EVDEV_CODES_SHA}  ${evc}" | sha256sum -c --quiet - || { echo "build.sh: input-event-codes.h sha256 mismatch" >&2; exit 1; }
	fi
	cp "${out}/src/libinput/src/libinput.h" "${P}/include/libinput.h"
	mkdir -p "${P}/include/linux" "${P}/include/libevdev"
	cp "${SHIM_INC}"/linux/*.h "${P}/include/linux/"
	cp "${SHIM_INC}/libevdev/libevdev.h" "${P}/include/libevdev/"
	cp "${SHIM_INC}/libudev.h" "${P}/include/"
	SFLAGS=(-O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${P}/include" -I"${COMPAT_INC}" -I"${MESA_COMPAT_INC}")
	"${TC}-gcc" "${SFLAGS[@]}" -c "${here}/shims/src/udev_phoenix.c" -o "${out}/shim-obj/udev_phoenix.o"
	"${TC}-gcc" "${SFLAGS[@]}" -c "${here}/shims/src/libevdev_phoenix.c" -o "${out}/shim-obj/libevdev_phoenix.o"
	"${TC}-gcc" "${SFLAGS[@]}" -c "${here}/shims/src/libinput_phoenix.c" -o "${out}/shim-obj/libinput_phoenix.o"
	"${TC}-gcc" "${SFLAGS[@]}" -I"${root}/tools/gpu-lane/xorg-drm/src" \
		-c "${here}/shims/src/libinput_phoenix_hid.c" -o "${out}/shim-obj/libinput_phoenix_hid.o"
	rm -f "${P}/lib/libudev.a" "${P}/lib/libinput.a" "${P}/lib/libevdev.a"
	"${TC}-gcc-ar" rcs "${P}/lib/libudev.a" "${out}/shim-obj/udev_phoenix.o"
	"${TC}-gcc-ar" rcs "${P}/lib/libinput.a" "${out}/shim-obj/libinput_phoenix.o" "${out}/shim-obj/libinput_phoenix_hid.o"
	"${TC}-gcc-ar" rcs "${P}/lib/libevdev.a" "${out}/shim-obj/libevdev_phoenix.o"
	shim_pc() {  # name version libs description
		printf '%s\n' "prefix=${P}" "Name: $1" "Description: $4" "Version: $2" "Libs: -L\${prefix}/lib $3" \
			"Cflags: -I\${prefix}/include" > "${P}/lib/pkgconfig/$1.pc"
	}
	shim_pc libudev 256 -ludev "weston-drm shim: libudev over a fixed device table"
	shim_pc libinput 1.26.2 "-linput -ludev" "weston-drm shim: libinput-phoenix (usbkbd, usbmouse)"
	shim_pc libevdev 1.13.0 -levdev "weston-drm shim: libevdev_event_code_from_name"
	echo "  libudev.a libinput.a libevdev.a (+ linux/input.h over FreeBSD's input-event-codes.h)"
fi

# --- baked keymap: evdev/pc105/us compiled on the HOST (Phoenix has no xkeyboard-config) ------
# A native libxkbcommon (same source) provides xkbcli-compile-keymap unless the host has xkbcli.
if [ "${relink}" = 0 ]; then
	echo "== baked keymap (host)"
	km="${out}/keymap-us.xkb"
	if [ ! -s "${km}" ]; then
		if command -v xkbcli > /dev/null; then
			xkbcli compile-keymap --rules evdev --model pc105 --layout us > "${km}"
		else
			hb="${out}/host-xkbcommon-build"
			if [ ! -x "${hb}/xkbcli-compile-keymap" ]; then
				rm -rf "${hb}"
				meson setup "${hb}" "${out}/src/libxkbcommon" --buildtype=release -Denable-x11=false \
					-Denable-wayland=false -Denable-docs=false -Denable-tools=true -Denable-xkbregistry=false \
					-Denable-bash-completion=false -Dxkb-config-root=/usr/share/X11/xkb \
					> "${out}/host-xkbcommon-setup.log" 2>&1 || { tail -20 "${out}/host-xkbcommon-setup.log"; exit 1; }
				ninja -C "${hb}" xkbcli-compile-keymap > "${out}/host-xkbcommon-ninja.log" 2>&1 \
					|| { tail -20 "${out}/host-xkbcommon-ninja.log"; exit 1; }
			fi
			[ -d /usr/share/X11/xkb/rules ] || { echo "build.sh: host has no xkeyboard-config (/usr/share/X11/xkb)" >&2; exit 1; }
			"${hb}/xkbcli-compile-keymap" --rules evdev --model pc105 --layout us > "${km}"
		fi
	fi
	grep -q 'xkb_keymap' "${km}" || { echo "build.sh: ${km} is not a keymap" >&2; exit 1; }
	python3 - "${km}" "${out}/weston_keymap.h" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src).read()
with open(dst, 'w') as f:
    f.write('/* Generated by tools/gpu-lane/weston-drm/build.sh from xkbcli compile-keymap\n')
    f.write(' * --rules evdev --model pc105 --layout us (host xkeyboard-config). */\n')
    f.write('const char weston_builtin_xkb_keymap[] =\n')
    for line in text.splitlines():
        f.write('\t"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"\n')
    f.write('\t;\n')
PY
	echo "  ${km}: $(wc -c < "${km}") bytes -> weston_keymap.h"
fi

# --- Mesa with the EGL wayland platform (mesa-drm --wayland, its own build dir) -----------
if [ "${relink}" = 0 ] && [ "${build_mesa}" = 1 ]; then
	echo "== Mesa (mesa-drm/build.sh --wayland -> ${mesa_out})"
	"${root}/tools/gpu-lane/mesa-drm/build.sh" --wayland --out "${mesa_out}" --libdrm-prefix "${libdrm_src_prefix}" \
		--wayland-pkgconfig "${P}/lib/pkgconfig:${P}/share/pkgconfig:${D}/libffi/lib/pkgconfig" -j "${jobs}" \
		> "${out}/mesa-wayland-build.log" 2>&1 || { tail -30 "${out}/mesa-wayland-build.log"; exit 1; }
	tail -6 "${out}/mesa-wayland-build.log" | sed 's/^/  /'
fi
[ -f "${mesa_out}/egl-link.txt" ] || { echo "build.sh: ${mesa_out}/egl-link.txt missing (build mesa-drm --wayland)" >&2; exit 1; }

# --- Weston: meson builds the archives and objects; the programs are linked below ----------
WB="${out}/weston-build"
WESTON_TARGETS=(libweston/libweston-14.a frontend/libexec_weston.a libweston/backend-drm/drm-backend.a
	libweston/renderer-gl/gl-renderer.a kiosk-shell/kiosk-shell.a)
if [ "${relink}" = 0 ]; then
	echo "== Weston 14 (DRM backend + GL renderer + kiosk shell; static)"
	if [ ! -f "${out}/weston.configured" ] || [ "${out}/src/weston.stamp" -nt "${out}/weston.configured" ]; then
		rm -rf "${WB}"
		meson setup "${WB}" "${out}/src/weston" --cross-file "${out}/phoenix-aarch64-weston.cross" --prefix /usr \
			--buildtype=debugoptimized -Db_staticpic=false --wrap-mode=nodownload \
			-Dbackend-drm=true -Dbackend-drm-screencast-vaapi=false -Dbackend-headless=false \
			-Dbackend-pipewire=false -Dbackend-rdp=false -Dscreenshare=false -Dbackend-vnc=false \
			-Dbackend-wayland=false -Dbackend-x11=false -Dbackend-default=drm -Drenderer-gl=true \
			-Dxwayland=false -Dsystemd=false -Dremoting=false -Dpipewire=false \
			-Dshell-desktop=false -Dshell-ivi=false -Dshell-kiosk=true -Dshell-fullscreen=false \
			-Dcolor-management-lcms=false -Dimage-jpeg=false -Dimage-webp=false -Dtools=[] \
			-Ddemo-clients=false -Dsimple-clients=shm,egl -Dresize-pool=false -Dwcap-decode=false \
			-Dtests=false -Ddoc=false \
			> "${out}/weston-setup.log" 2>&1 || { tail -40 "${out}/weston-setup.log"; exit 1; }
		touch "${out}/weston.configured"
	fi
	grep -E 'Checking for function "(memfd_create|posix_fallocate|mkostemp)"' "${out}/weston-setup.log" | sed 's/^/  /'
	ninja -C "${WB}" -j"${jobs}" "${WESTON_TARGETS[@]}" > "${out}/weston-ninja.log" 2>&1 \
		|| { grep -E -A6 'error|FAILED' "${out}/weston-ninja.log" | head -80; exit 1; }
	echo "  Weston archives built ($(grep -c 'warning:' "${out}/weston-ninja.log" || true) warning line(s))"
fi

# --- link ------------------------------------------------------------------------------------
# Hand links (as Xorg-drm / kmscube): meson's own executables would need Mesa's full static
# closure, which Mesa's .pc files do not describe.
#   gallium whole-archive (the DRI frontend + drivers, found by name), then one group of every
#   other archive; -Wl,--wrap=mmap/ioctl for libdrm-phoenix (BO-token maps, emulated sync
#   files), --wrap=close/write for the compat event loop descriptors (-u pulls them first).
echo "== link"
WL_OBJ="${out}/weston-obj"
mkdir -p "${WL_OBJ}"
OFLAGS=(-O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}")
"${TC}-gcc" "${OFLAGS[@]}" -I"${out}" -c "${here}/src/weston_builtin.c" -o "${WL_OBJ}/weston_builtin.o"
# The objects meson would link into an executable (its own link is not used).
ninja_objs() {
	ninja -C "${WB}" -t query "$1" | awk '/input: c_LINKER/ {on = 1; next} /^  [a-z]/ {on = 0} on && $1 ~ /\.o$/ {print $1}'
}
mapfile -t SHM_OBJS < <(ninja_objs clients/weston-simple-shm)
mapfile -t EGL_OBJS < <(ninja_objs clients/weston-simple-egl)
ninja -C "${WB}" frontend/weston.p/executable.c.o shared/libshared.a "${SHM_OBJS[@]}" "${EGL_OBJS[@]}" \
	> "${out}/weston-ninja-objs.log" 2>&1 || { grep -E -A6 'error|FAILED' "${out}/weston-ninja-objs.log" | head -60; exit 1; }
SHM_OBJS=("${SHM_OBJS[@]/#/${WB}/}")
EGL_OBJS=("${EGL_OBJS[@]/#/${WB}/}")

gallium=""
MESA_A=()
while IFS= read -r l; do
	case "${l}" in
		"--whole-archive "*) gallium="${l#--whole-archive }" ;;
		*) MESA_A+=("${l}") ;;
	esac
done < "${mesa_out}/egl-link.txt"
[ -f "${gallium}" ] || { echo "build.sh: no gallium archive in ${mesa_out}/egl-link.txt" >&2; exit 1; }
# Mesa's util/anon_file.c and Weston's shared/os-compatibility.c both export
# os_create_anonymous_file() -- with different signatures (hidden from each other in
# shared builds). Private copies of the Mesa archives that define or call it get Mesa's
# renamed; everything else is linked in place.
ML="${out}/mesa-link"
mkdir -p "${ML}"
mesa_private() {  # archive -> path to link
	local a="$1" c
	# (no `nm | grep -q` under pipefail: grep's early exit SIGPIPEs nm and fails the test)
	if ! grep -q ' os_create_anonymous_file$' <<< "$("${TC}-nm" "${a}" 2>/dev/null)"; then
		echo "${a}"
		return
	fi
	c="${ML}/$(echo "${a#"${mesa_out}"/}" | tr '/' '_')"
	if [ ! -f "${c}" ] || [ "${a}" -nt "${c}" ]; then
		if [ "$(head -c 8 "${a}")" = '!<thin>' ]; then
			# meson's internal libraries are thin archives (objcopy cannot copy them):
			# rebuild a regular archive from the renamed members
			local t="${c}.d" i=0 m
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
gallium="$(mesa_private "${gallium}")"
for i in "${!MESA_A[@]}"; do
	MESA_A[i]="$(mesa_private "${MESA_A[i]}")"
done
LINK_BASE=("${TC}-g++" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000
	-Wl,--wrap=close -Wl,--wrap=write -Wl,-u,__wrap_close -Wl,-u,__wrap_write)
LINK_DRM=("${LINK_BASE[@]}" -Wl,--wrap=mmap -Wl,--wrap=ioctl)
WL_LIBS=("${P}/lib/libwayland-server.a" "${P}/lib/libwayland-client.a" "${P}/lib/libwayland-egl.a"
	"${P}/lib/libxkbcommon.a" "${P}/lib/libdisplay-info.a" "${P}/lib/libseat.a" "${P}/lib/libinput.a"
	"${P}/lib/libudev.a" "${P}/lib/libevdev.a" "${D}/pixman-1/lib/libpixman-1.a" "${D}/libffi/lib/libffi.a"
	"${P}/lib/libwlphx-compat.a")

link_prog() {  # base|drm output link-arguments...
	local kind="$1" o="$2"
	local -a L
	shift 2
	if [ "${kind}" = drm ]; then L=("${LINK_DRM[@]}"); else L=("${LINK_BASE[@]}"); fi
	"${L[@]}" -Wl,-Map,"${out}/${o}.map" -o "${out}/${o}" "$@" > "${out}/${o}-link.log" 2>&1 \
		|| { grep -v 'warning: .* is not fully supported' "${out}/${o}-link.log" | head -60; exit 1; }
	"${TC}-strip" -o "${out}/${o}-stripped" "${out}/${o}"
	echo "  ${o}: $(stat -c %s "${out}/${o}") bytes, stripped $(stat -c %s "${out}/${o}-stripped")"
}

link_prog drm weston "${WB}/frontend/weston.p/executable.c.o" "${WL_OBJ}/weston_builtin.o" \
	-Wl,--whole-archive "${gallium}" -Wl,--no-whole-archive \
	-Wl,--start-group "${WB}/frontend/libexec_weston.a" "${WB}/kiosk-shell/kiosk-shell.a" \
	"${WB}/libweston/backend-drm/drm-backend.a" "${WB}/libweston/renderer-gl/gl-renderer.a" \
	"${WB}/libweston/libweston-14.a" "${MESA_A[@]}" "${WL_LIBS[@]}" -Wl,--end-group -lm

# weston-simple-shm: wl_shm + xdg-shell only (pixels by the CPU into a memfd pool = shmsrv)
link_prog base weston-simple-shm "${SHM_OBJS[@]}" \
	-Wl,--start-group "${WB}/shared/libshared.a" "${WL_LIBS[@]}" -Wl,--end-group -lm

# weston-simple-egl: GLES on the wayland-egl platform (Mesa --wayland)
link_prog drm weston-simple-egl "${EGL_OBJS[@]}" \
	-Wl,--whole-archive "${gallium}" -Wl,--no-whole-archive \
	-Wl,--start-group "${WB}/shared/libshared.a" "${MESA_A[@]}" "${P}/lib/libwayland-cursor.a" "${WL_LIBS[@]}" \
	-Wl,--end-group -lm

# shmsrv: the /shm server (memfd_create backing)
"${TC}-gcc" "${OFLAGS[@]}" -c "${here}/shmsrv/shmsrv.c" -o "${WL_OBJ}/shmsrv.o"
"${TC}-gcc" "${TFLAGS[@]}" -static -Wl,--gc-sections -o "${out}/shmsrv" "${WL_OBJ}/shmsrv.o"
"${TC}-strip" -o "${out}/shmsrv-stripped" "${out}/shmsrv"
echo "  shmsrv: $(stat -c %s "${out}/shmsrv") bytes, stripped $(stat -c %s "${out}/shmsrv-stripped")"

# --- verification ----------------------------------------------------------------------------
echo "== verify"
bad=0
for o in weston weston-simple-shm weston-simple-egl shmsrv; do
	und="$("${TC}-nm" -u "${out}/${o}" || true)"
	n=$(grep -c . <<< "${und}" || true)
	echo "  ${o}: undefined symbols (nm -u): ${n}; $("${TC}-size" "${out}/${o}" | awk 'NR==2 {printf "text %d data %d bss %d", $1, $2, $3}')"
	[ "${n}" = 0 ] || { sed 's/^/    /' <<< "${und}" | head -10; bad=1; }
	lw="$(grep -v -E 'warning: .*(is not fully supported|dlopen|getpwnam|getpwuid|getgrnam|initgroups)' "${out}/${o}-link.log" 2>/dev/null | grep -c 'warning' || true)"
	[ -f "${out}/${o}-link.log" ] && echo "    link warnings beyond libphoenix attribute notes: ${lw}"
done
check_syms() {  # binary symbols...
	local b="$1" s syms
	shift
	syms="$("${TC}-nm" "${out}/${b}")"
	for s in "$@"; do
		if grep -qE " [TtDdRrBbWw] ${s}\$" <<< "${syms}"; then echo "  ${b} symbol ${s}: yes"; else echo "  ${b} symbol ${s}: NO"; bad=1; fi
	done
}
check_syms weston weston_builtin_modules weston_builtin_xkb_keymap weston_backend_init gl_renderer_interface \
	wet_shell_init __wrap_mmap __wrap_ioctl __wrap_close __wrap_write drm_phoenix_ioctl drmPhoenixMmap \
	epoll_wait timerfd_settime signalfd eventfd memfd_create libseat_open_seat libinput_udev_assign_seat \
	udev_enumerate_scan_devices di_info_parse_edid xkb_keymap_new_from_string kmsro_drm_screen_create \
	v3d_drm_screen_create_renderonly gbmint_get_backend mesa_os_create_anonymous_file os_create_anonymous_file
check_syms weston-simple-shm memfd_create os_create_anonymous_file wl_display_connect __wrap_close
check_syms weston-simple-egl dri2_initialize_wayland wl_egl_window_create __wrap_mmap drm_phoenix_ioctl
n=$(grep -c V3D_PHOENIX_SHARED_SCANOUT <<< "$(strings -a "${out}/weston-simple-egl-stripped")" || true)
echo "  weston-simple-egl strings 'V3D_PHOENIX_SHARED_SCANOUT' (mesa-drm patch 0012): ${n}"
[ "${n}" != 0 ] || bad=1
strs="$(strings -a "${out}/weston-stripped")"
for s in 'drm-backend.so' 'gl-renderer.so' 'kiosk-shell.so' 'linked into the program' 'using the builtin XKB keymap' \
		'DRM backend' 'libdrm-phoenix:' 'DRMPHX_TRACE' '/dev/dri/card0' '/dev/dri/renderD128' '/kmsbuf' 'LIBINPUT-PHX' \
		'noop' 'EGL_KHR_platform_gbm' 'V3D 4.2' 'xkb_keymap' '/shm'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  weston strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
for b in weston-stripped weston-simple-shm-stripped weston-simple-egl-stripped shmsrv-stripped; do
	bs="$(strings -a "${out}/${b}")"
	for s in 'v3d-winsys:' phoenix_v3d_ioctl peek_next_scanout v3d-srv /dev/v3d-srv Xphoenix '[fbdev]' glamor_phoenix phxgl; do
		n=$(grep -cF -- "${s}" <<< "${bs}" || true)
		[ "${n}" = 0 ] || { echo "  OLD-LANE string '${s}' in ${b}: ${n}"; bad=1; }
	done
done
echo "  old-lane strings: $([ "${bad}" = 0 ] && echo none || echo 'see above')"
sha256sum "${out}"/weston-stripped "${out}"/weston-simple-shm-stripped "${out}"/weston-simple-egl-stripped \
	"${out}"/shmsrv-stripped | sed "s|${out}/||; s/^/  /"
[ "${bad}" = 0 ] || { echo "build.sh: verification failed" >&2; exit 1; }
echo "done"
