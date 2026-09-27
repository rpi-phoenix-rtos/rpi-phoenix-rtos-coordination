#!/usr/bin/env bash
#
# FRAMEWORK PORT: sources/phoenix-rtos-ports labwc_desktop (+ wayland_phoenix: the weston-drm
# compat/shims/patches this script reuses),
# branch feat/new-lane-wayland-ports; opt-in, not in the default image (docs/gpu-new-lane/
# MIGRATION.md section 4, "Ports (Wayland desktop)"). Every patch/glue file this script uses
# is also a file of the port; scripts/check-wayland-ports-sync.sh keeps the copies identical --
# a change here must be copied there. This script keeps working until the migration switch.
#
# labwc-drm (new GPU lane, M7): a lightweight Wayland desktop cross-built STATIC for
# aarch64-phoenix -- wlroots 0.20 (DRM + libinput + headless backends, GLES2 and
# pixman renderers, libseat session), labwc 0.20 on it, the foot terminal, the fuzzel
# launcher and the swaybg wallpaper.
#
# Reuses the M6 Weston port (tools/gpu-lane/weston-drm/) by path, never by copy:
# its compat layer (epoll/timerfd/signalfd/eventfd over poll, memfd_create over
# shmsrv), its shims (libudev fixed table, libinput-phoenix, libevdev), its
# libwayland and seatd patches. This directory adds only what wlroots, labwc and
# foot need beyond Weston (compat/: shm_open, posix_openpt, <uchar.h>;
# patches/<pkg>/; src/: the builtin keymap and a GLib 2.68 stand-in).
#
#   <out>/dl/              pinned source tarballs (sha256 below)
#   <out>/src/<pkg>/       extracted + patches applied (git apply, one commit each)
#   <out>/prefix/          every library built here + headers + *.pc
#   <out>/deps/            private views of the ports prefix (exactly one library's
#                          headers each; the ports include dir also holds GL/, X11/
#                          and xft headers -- never on a search path)
#   <out>/libdrm-prefix/   a snapshot of libdrm-phoenix (--libdrm-prefix)
#   <out>/{labwc,foot,tinywl,fuzzel,swaybg} (unstripped, addr2line) and *-stripped (stage these)
#
# Writes only into <out> (default build-out/, gitignored). Reads the tree sysroot,
# the ports prefix, the toolchain, the E7 compiler wrappers, a libdrm-phoenix
# prefix and a mesa-drm --wayland build (never rebuilt from here). No Pi, no
# rebuild-rpi4b-fast.sh, no /srv.
#
# Host tools: wayland-scanner 1.24.0, meson >= 1.3, ninja, bison, glib-mkenums,
# hwdata (pnp.ids), xkeyboard-config (baked keymap; a native libxkbcommon is built
# for xkbcli-compile-keymap).
#
# Usage: tools/gpu-lane/labwc-drm/build.sh [--clean] [--out <dir>] [-j N]
#            [--libdrm-prefix <dir>] [--mesa-out <dir>] [--relink]
#   --relink   skip libraries and meson; relink the programs only
#   LINK_EXTRA="<flags>" (environment): extra flags for every program link, e.g.
#              -Wl,--trace-symbol=<sym> to see which archive member resolves a symbol
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
W="${root}/tools/gpu-lane/weston-drm"
out="${here}/build-out"
jobs="$(nproc)"
clean=0
relink=0
libdrm_src_prefix="${root}/tools/gpu-lane/libdrm-phoenix/build-out-low/prefix"
mesa_out="${root}/tools/gpu-lane/mesa-drm/build-out-wayland-low"
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--relink) relink=1 ;;
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
case "${libdrm_src_prefix}" in /*) ;; *) libdrm_src_prefix="${PWD}/${libdrm_src_prefix}" ;; esac
case "${mesa_out}" in /*) ;; *) mesa_out="${PWD}/${mesa_out}" ;; esac

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
LCOMPAT_INC="${here}/compat/include"                               # this port's gaps (shm_open, uchar.h...)
COMPAT_INC="${W}/compat/include"                                   # M6: epoll/timerfd/signalfd/memfd...
MESA_COMPAT_INC="${root}/tools/gpu-lane/mesa-drm/compat/include"   # M3/M4 generic libphoenix gaps
SHIM_INC="${W}/shims/include"
P="${out}/prefix"
D="${out}/deps"
LD_PREFIX="${out}/libdrm-prefix"

# name|file|url|sha256
PKGS=(
	"wayland|wayland-1.24.0.tar.xz|https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.24.0/downloads/wayland-1.24.0.tar.xz|82892487a01ad67b334eca83b54317a7c86a03a89cfadacfef5211f11a5d0536"
	"wayland-protocols|wayland-protocols-1.49.tar.xz|https://gitlab.freedesktop.org/-/project/2891/uploads/7ed597f0cad076a17fe36f8860596f8c/wayland-protocols-1.49.tar.xz|ec4c8f74942d6dff7ace8b4ce4764f0ef9ff618a935d974ea77edee2ad240b14"
	"libxkbcommon|xkbcommon-1.13.2.tar.gz|https://github.com/xkbcommon/libxkbcommon/archive/refs/tags/xkbcommon-1.13.2.tar.gz|acc4d5f7c3cbba5f9f8d08d8bdbeede84ecede46792f47929aa9321873385528"
	"pixman|pixman-0.46.4.tar.xz|https://www.cairographics.org/releases/pixman-0.46.4.tar.xz|a098c33924754ad43f981b740f6d576c70f9ed1006e12221b1845431ebce1239"
	"libdisplay-info|libdisplay-info-0.2.0.tar.xz|https://gitlab.freedesktop.org/emersion/libdisplay-info/-/releases/0.2.0/downloads/libdisplay-info-0.2.0.tar.xz|5a2f002a16f42dd3540c8846f80a90b8f4bdcd067a94b9d2087bc2feae974176"
	"seatd|seatd-0.9.1.tar.gz|https://git.sr.ht/~kennylevinsen/seatd/archive/0.9.1.tar.gz|819979c922a0be258aed133d93920bce6a3d3565a60588d6d372ce9db2712cd3"
	"libinput|libinput-1.26.2.tar.gz|https://gitlab.freedesktop.org/libinput/libinput/-/archive/1.26.2/libinput-1.26.2.tar.gz|5c1c4150f217fea1db2d1fd88e2607b2f1928cfde65c34da65a9f24dcfd69464"
	"libxml2|libxml2-2.15.4.tar.xz|https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.4.tar.xz|98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821"
	"fribidi|fribidi-1.0.16.tar.xz|https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz|1b1cde5b235d40479e91be2f0e88a309e3214c8ab470ec8a2744d82a5a9ea05c"
	"pango|pango-1.44.7.tar.xz|https://download.gnome.org/sources/pango/1.44/pango-1.44.7.tar.xz|66a5b6cc13db73efed67b8e933584509f8ddb7b10a8a40c3850ca4a985ea1b1f"
	"wlroots|wlroots-0.20.2.tar.gz|https://gitlab.freedesktop.org/-/project/12103/uploads/6a56af9eafb5240d2823772aeb1e2e7d/wlroots-0.20.2.tar.gz|80c567d2ed4efb2cfa6b077f22c7a710d9c78ba4e185a75e8694001114af0733"
	"labwc|labwc-0.20.2.tar.gz|https://github.com/labwc/labwc/archive/refs/tags/0.20.2.tar.gz|fae023b6fe022f7057556707a17cdb2d98e0138c5dffaedaa1dade975699f9e8"
	"tllist|tllist-1.1.0.tar.gz|https://codeberg.org/dnkl/tllist/archive/1.1.0.tar.gz|0e7b7094a02550dd80b7243bcffc3671550b0f1d8ba625e4dff52517827d5d23"
	"fcft|fcft-3.3.3.tar.gz|https://codeberg.org/dnkl/fcft/archive/3.3.3.tar.gz|b0c0f4a599f43723736c8565b8b84337c4195077f07f1bb8bb3252bb13a2306a"
	"foot|foot-1.28.0.tar.gz|https://codeberg.org/dnkl/foot/archive/1.28.0.tar.gz|4296be402b5684d049534598e69db92b918f92beac9dab76b585207045f0b037"
	"fuzzel|fuzzel-1.15.0.tar.gz|https://codeberg.org/dnkl/fuzzel/archive/1.15.0.tar.gz|95b6c022fc1f1c7ab586d47c1594417cc311bf41ea8f5f8b5641478da7b5cf3b"
	"swaybg|swaybg-1.2.2.tar.gz|https://github.com/swaywm/swaybg/releases/download/v1.2.2/swaybg-1.2.2.tar.gz|a6652a0060a0bea3c3318d9d03b6dddac34f6aeca01b883eef9e58281f5202a1"
)
WAYLAND_VERSION=1.24.0
# FreeBSD's BSD-2 copy of the evdev event codes (as weston-drm)
EVDEV_CODES_COMMIT=f492ef8318f580081047da41905c3b339e924387
EVDEV_CODES_SHA=fc9c4946818cefcec359ad3a619d448c4cafffa4f8f9571894516fb980b26142

patch_dir() {  # the M6 patch sets are reused as they are
	case "$1" in
		wayland|seatd) echo "${W}/patches/$1" ;;
		*) echo "${here}/patches/$1" ;;
	esac
}

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-gcc-ar" "${TC}-nm" "${TC}-strip" "${PHXCC}" "${PHXCXX}" \
		"${B}/lib/libffi.a" "${B}/lib/libexpat.a" "${B}/lib/libz.a" "${B}/lib/libpng16.a" "${B}/lib/libfreetype.a" \
		"${B}/lib/libfontconfig.a" "${B}/lib/libharfbuzz.a" "${B}/lib/libcairo.a" "${B}/lib/libglib-2.0.a" \
		"${B}/lib/libgobject-2.0.a" "${B}/lib/libiconv.a" "${B}/lib/glib-2.0/include/glibconfig.h" \
		"${libdrm_src_prefix}/lib/libdrm.a" "${mesa_out}/egl-link.txt" "${mesa_out}/prefix/lib/pkgconfig/egl.pc"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
for t in meson ninja wayland-scanner bison glib-mkenums; do
	command -v "${t}" > /dev/null || { echo "build.sh: host tool ${t} not found" >&2; exit 1; }
done
[ "$(wayland-scanner --version 2>&1 | awk '{print $2}')" = "${WAYLAND_VERSION}" ] \
	|| { echo "build.sh: host wayland-scanner is not ${WAYLAND_VERSION}" >&2; exit 1; }
[ -f /usr/share/hwdata/pnp.ids ] || { echo "build.sh: host hwdata (pnp.ids) missing" >&2; exit 1; }
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
	local name="$1" rec file url sum dir stamp pd
	for rec in "${PKGS[@]}"; do
		[ "${rec%%|*}" = "${name}" ] && break
	done
	IFS='|' read -r _ file url sum <<< "${rec}"
	pd="$(patch_dir "${name}")"
	if [ ! -f "${out}/dl/${file}" ]; then
		if [ -f "${W}/build-out/dl/${file}" ]; then
			cp "${W}/build-out/dl/${file}" "${out}/dl/${file}"
		else
			echo "  fetch ${file}"
			curl -sSfL -o "${out}/dl/${file}.part" "${url}"
			mv "${out}/dl/${file}.part" "${out}/dl/${file}"
		fi
	fi
	echo "${sum}  ${out}/dl/${file}" | sha256sum -c --quiet - || { echo "build.sh: ${file}: sha256 mismatch" >&2; exit 1; }
	dir="${out}/src/${name}"
	stamp="$( { echo "${sum}"; cat "${pd}"/*.patch 2>/dev/null || true; } | sha256sum | cut -c1-16)"
	if [ "$(cat "${dir}.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		rm -rf "${dir}" "${dir}.tmp"
		mkdir -p "${dir}.tmp"
		tar -xf "${out}/dl/${file}" -C "${dir}.tmp" --strip-components=1
		mv "${dir}.tmp" "${dir}"
		# Its own git repository: `git apply` inside a directory of ANOTHER repository
		# silently skips every path (weston-drm M6 §5.1).
		git -C "${dir}" init -q
		git -C "${dir}" add -A
		git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "${file}"
		for p in "${pd}"/*.patch; do
			[ -e "${p}" ] || continue
			echo "  apply ${name}/$(basename "${p}")"
			git -C "${dir}" apply --whitespace=nowarn "${p}"
			git -C "${dir}" add -A
			git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "$(basename "${p}")"
		done
		git -C "${dir}" diff "$(git -C "${dir}" rev-list --max-parents=0 HEAD)" > "${out}/${name}-full.patch"
		echo "${stamp}" > "${dir}.stamp"
		rm -f "${out}/${name}.built" "${out}/${name}.configured"   # a changed source rebuilds the package
	fi
}

# --- private views of the ports prefix ------------------------------------------------------
# view <name> <version> <cflags-subdirs> <libs> <requires> <requires.private> <item>...
#   item = inc:<path under $B/include> | lib:<path under $B/lib>   (files or directories)
view() {
	local name="$1" ver="$2" subs="$3" libs="$4" req="$5" reqp="$6" it cf="" s
	shift 6
	rm -rf "${D:?}/${name}"
	mkdir -p "${D}/${name}/include" "${D}/${name}/lib/pkgconfig"
	for it in "$@"; do
		case "${it}" in
			inc:*) mkdir -p "$(dirname "${D}/${name}/include/${it#inc:}")"
				cp -a "${B}/include/${it#inc:}" "${D}/${name}/include/${it#inc:}" ;;
			lib:*) mkdir -p "$(dirname "${D}/${name}/lib/${it#lib:}")"
				cp -a "${B}/lib/${it#lib:}" "${D}/${name}/lib/${it#lib:}" ;;
		esac
	done
	for s in ${subs}; do
		case "${s}" in
			.) cf="${cf} -I\${prefix}/include" ;;
			lib/*) cf="${cf} -I\${prefix}/${s}" ;;
			*) cf="${cf} -I\${prefix}/include/${s}" ;;
		esac
	done
	printf '%s\n' "prefix=${D}/${name}" "Name: ${name}" "Description: ${name} from the Phoenix ports prefix" \
		"Version: ${ver}" "Requires: ${req}" "Requires.private: ${reqp}" "Libs: -L\${prefix}/lib ${libs}" \
		"Cflags:${cf}" > "${D}/${name}/lib/pkgconfig/${name}.pc"
}
pc_alias() {  # alias-name target-view version [extra variables...]
	local a="$1" t="$2" v="$3"
	shift 3
	{ printf '%s\n' "$@"; printf '%s\n' "Name: ${a}" "Description: alias of ${t}" "Version: ${v}" "Requires: ${t}" "Libs:" "Cflags:"; } \
		> "${D}/${t}/lib/pkgconfig/${a}.pc"
}
pcver() { sed -n 's/^Version: *//p' "${B}/lib/pkgconfig/$1.pc"; }

write_cross() {
	local cross="${out}/phoenix-aarch64.cross" pkgc="${out}/pkg-config-phoenix" v libdir=""
	for v in "${D}"/*/lib/pkgconfig; do libdir="${libdir}:${v}"; done
	cat > "${pkgc}" <<EOF
#!/bin/sh
# pkg-config restricted to this build's prefix, the private ports views, the libdrm
# snapshot and the mesa-drm --wayland prefix.
export PKG_CONFIG_LIBDIR=${P}/lib/pkgconfig:${P}/share/pkgconfig${libdir}:${LD_PREFIX}/lib/pkgconfig:${mesa_out}/prefix/lib/pkgconfig
unset PKG_CONFIG_PATH
exec /usr/bin/pkg-config --static "\$@"
EOF
	chmod +x "${pkgc}"
	# Include order: this port's compat, the M6 compat (epoll...), mesa-drm's generic gaps.
	local flags="'--sysroot=${S}/', '-B${S}/lib/', '-mcpu=cortex-a72', '-mtune=cortex-a72', '-mstrict-align', '-mno-outline-atomics', '-ffunction-sections', '-fdata-sections', '-I${LCOMPAT_INC}', '-I${COMPAT_INC}', '-I${MESA_COMPAT_INC}'"
	# The compat archive on every link probe: configure checks (memfd_create, epoll,
	# shm_open...) see what the programs will have.
	local lflags="'--sysroot=${S}/', '-B${S}/lib/', '-L${B}/lib', '-Wl,-z,max-page-size=0x1000', '-Wl,-u,__wrap_close', '-Wl,-u,__wrap_write', '-Wl,-u,__wrap_read', '${P}/lib/liblwphx-compat.a', '${P}/lib/libwlphx-compat.a', '-Wl,--wrap=close', '-Wl,--wrap=write', '-Wl,--wrap=read'"
	cat > "${cross}" <<EOF
# Generated by tools/gpu-lane/labwc-drm/build.sh (aarch64-phoenix, Pi 4).
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
}

meson_pkg() {  # name builddir-name meson-args...
	local name="$1" bname="$2"
	shift 2
	local bd="${out}/${bname}"
	if [ -f "${out}/${name}.built" ]; then
		echo "  ${name}: up to date"
		return 0
	fi
	rm -rf "${bd}"
	# EXTRA_C_ARGS: appended to the cross file's c_args for this package only
	local cross="${out}/phoenix-aarch64.cross"
	if [ -n "${EXTRA_C_ARGS:-}" ]; then
		cross="${out}/${bname}.cross"
		sed "s|^c_args = \[\(.*\)\]|c_args = [\1, ${EXTRA_C_ARGS}]|" "${out}/phoenix-aarch64.cross" > "${cross}"
	fi
	meson setup "${bd}" "${out}/src/${name}" --cross-file "${cross}" --prefix "${P}" \
		--libdir lib --buildtype=debugoptimized -Db_staticpic=false --wrap-mode=nodownload "$@" \
		> "${out}/${bname}-setup.log" 2>&1 || { tail -40 "${out}/${bname}-setup.log"; exit 1; }
	# MESON_TARGETS: build only these (packages whose tests cannot be switched off), then
	# install what was built
	# shellcheck disable=SC2086
	ninja -C "${bd}" -j"${jobs}" ${MESON_TARGETS:-} > "${out}/${bname}-ninja.log" 2>&1 || { grep -E -A5 'error|FAILED' "${out}/${bname}-ninja.log" | head -80; exit 1; }
	meson install -C "${bd}" --no-rebuild > "${out}/${bname}-install.log" 2>&1 || { tail -20 "${out}/${bname}-install.log"; exit 1; }
	echo "  ${name}: built ($(grep -c 'warning:' "${out}/${bname}-ninja.log" || true) warning line(s))"
	touch "${out}/${name}.built"
}

# meson setup + ninja of one program's objects (programs are hand-linked below)
meson_objs() {  # name builddir-name program meson-args...
	local name="$1" bname="$2" targets="$3"
	shift 3
	local bd="${out}/${bname}"
	if [ ! -f "${out}/${name}.configured" ]; then
		rm -rf "${bd}"
		local cross="${out}/phoenix-aarch64.cross"
		if [ -n "${EXTRA_C_ARGS:-}" ]; then
			cross="${out}/${bname}.cross"
			sed "s|^c_args = \[\(.*\)\]|c_args = [\1, ${EXTRA_C_ARGS}]|" "${out}/phoenix-aarch64.cross" > "${cross}"
		fi
		meson setup "${bd}" "${out}/src/${name}" --cross-file "${cross}" --prefix /usr \
			--buildtype="${BUILDTYPE:-debugoptimized}" -Db_staticpic=false --wrap-mode=nodownload "$@" \
			> "${out}/${bname}-setup.log" 2>&1 || { tail -40 "${out}/${bname}-setup.log"; exit 1; }
		touch "${out}/${name}.configured"
	fi
	# only the program's objects: meson's own link would need Mesa's static closure
	local -a objs
	mapfile -t objs < <(ninja_objs "${bd}" "${targets}")
	[ "${#objs[@]}" -gt 0 ] || { echo "build.sh: no objects for ${targets} in ${bd}" >&2; exit 1; }
	ninja -C "${bd}" -j"${jobs}" "${objs[@]}" > "${out}/${bname}-ninja.log" 2>&1 \
		|| { grep -E -A6 'error|FAILED' "${out}/${bname}-ninja.log" | head -80; exit 1; }
	echo "  ${name}: objects built ($(grep -c 'warning:' "${out}/${bname}-ninja.log" || true) warning line(s))"
}

# The objects meson would link into an executable (its own link is not used).
ninja_objs() {  # builddir target
	# objects, then the program's own internal archives (implicit "| lib*.a" inputs)
	ninja -C "$1" -t query "$2" | awk '/input: c_LINKER|input: cpp_LINKER/ {on = 1; next} /^  [a-z]/ {on = 0}
		on && $1 ~ /\.o$/ {print $1} on && $1 == "|" && $2 !~ /^\// && $2 ~ /\.a$/ {print $2}'
}

pc_require_compat() {
	local pc
	for pc in "$@"; do
		grep -q 'wlphx-compat' "${pc}" || printf 'Requires.private: wlphx-compat\n' >> "${pc}"
	done
}

if [ "${relink}" = 0 ]; then
	echo "== sources"
	for rec in "${PKGS[@]}"; do fetch_extract "${rec%%|*}"; done

	echo "== dependency views (ports prefix) + libdrm-phoenix snapshot"
	view zlib "$(sed -n 's/^#define ZLIB_VERSION "\(.*\)"/\1/p' "${B}/include/zlib.h")" . -lz "" "" inc:zlib.h inc:zconf.h lib:libz.a
	view libffi "$(pcver libffi)" . -lffi "" "" inc:ffi.h inc:ffitarget.h lib:libffi.a
	view expat "$(pcver expat)" . -lexpat "" "" inc:expat.h inc:expat_config.h inc:expat_external.h lib:libexpat.a
	view libpng16 "$(pcver libpng16)" "libpng16" -lpng16 "" zlib inc:libpng16 lib:libpng16.a
	pc_alias libpng libpng16 "$(pcver libpng16)"
	view freetype2 "$(pcver freetype2)" freetype2 -lfreetype "" "" inc:freetype2 lib:libfreetype.a
	view fontconfig "$(pcver fontconfig)" . -lfontconfig freetype2 expat inc:fontconfig lib:libfontconfig.a
	# harfbuzz is C++: its static archive needs libstdc++ at every link
	view harfbuzz "$(pcver harfbuzz)" "harfbuzz ." "-lharfbuzz -lstdc++ -lm" "" freetype2 inc:harfbuzz lib:libharfbuzz.a
	view libiconv 1.18 . -liconv "" "" inc:iconv.h lib:libiconv.a
	# GLib 2.56.4 (ports, LGPL): the tools pango's meson asks the .pc for are the host's
	view glib-2.0 2.56.4 "glib-2.0 lib/glib-2.0/include" "-lglib-2.0 -lm" "" libiconv inc:glib-2.0 lib:libglib-2.0.a \
		lib:glib-2.0/include/glibconfig.h
	printf '%s\n' "glib_mkenums=/usr/bin/glib-mkenums" "glib_genmarshal=/usr/bin/glib-genmarshal" \
		"glib_compile_resources=/usr/bin/glib-compile-resources" | cat - "${D}/glib-2.0/lib/pkgconfig/glib-2.0.pc" \
		> "${D}/glib-2.0/lib/pkgconfig/glib-2.0.pc.t" && mv "${D}/glib-2.0/lib/pkgconfig/glib-2.0.pc.t" "${D}/glib-2.0/lib/pkgconfig/glib-2.0.pc"
	mkdir -p "${D}/gobject-2.0/lib/pkgconfig"
	cp "${B}/lib/libgobject-2.0.a" "${D}/gobject-2.0/lib/"
	printf '%s\n' "prefix=${D}/gobject-2.0" "Name: gobject-2.0" "Description: GObject 2.56.4 from the ports prefix" \
		"Version: 2.56.4" "Requires: glib-2.0" "Requires.private: libffi" "Libs: -L\${prefix}/lib -lgobject-2.0" "Cflags:" \
		> "${D}/gobject-2.0/lib/pkgconfig/gobject-2.0.pc"
	# cairo 1.16 (ports): image + ft/fc + png surfaces; linked with THIS build's pixman 0.46
	view cairo "$(pcver cairo)" cairo -lcairo "" "pixman-1 fontconfig freetype2 libpng16 zlib" inc:cairo lib:libcairo.a
	pc_alias cairo-ft cairo "$(pcver cairo)"
	pc_alias cairo-fc cairo "$(pcver cairo)"
	pc_alias cairo-png cairo "$(pcver cairo)"

	# Stand-ins for the text stack (shims/): HarfBuzz's GLib script conversions (the
	# ports HarfBuzz has no hb-glib) and GLib 2.68's g_string_replace (labwc). Both go
	# into the private views, declared where the real ones would be.
	mkdir -p "${out}/shim-obj"
	GFL=(-O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${D}/glib-2.0/include/glib-2.0"
		-I"${D}/glib-2.0/lib/glib-2.0/include" -I"${D}/harfbuzz/include/harfbuzz" -I"${here}/shims/include")
	cp "${here}/shims/include/hb-glib.h" "${D}/harfbuzz/include/harfbuzz/"
	"${TC}-gcc" "${GFL[@]}" -c "${here}/shims/src/hb_glib_phoenix.c" -o "${out}/shim-obj/hb_glib_phoenix.o"
	"${TC}-gcc-ar" rcs "${D}/harfbuzz/lib/libhbglib-phoenix.a" "${out}/shim-obj/hb_glib_phoenix.o"
	sed -i 's|^Libs: \(.*\) -lharfbuzz |Libs: \1 -lhbglib-phoenix -lharfbuzz |' "${D}/harfbuzz/lib/pkgconfig/harfbuzz.pc"
	python3 - "${D}/glib-2.0/include/glib-2.0/glib/gstring.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
decl = ('/* labwc-drm: GLib 2.68 API, tools/gpu-lane/labwc-drm/shims/src/glib_compat.c */\n'
        'guint g_string_replace (GString *string, const gchar *find, const gchar *replace, guint limit);\n\n')
i = s.rindex('G_END_DECLS')
open(p, 'w').write(s[:i] + decl + s[i:])
PY
	"${TC}-gcc" "${GFL[@]}" -c "${here}/shims/src/glib_compat.c" -o "${out}/shim-obj/glib_compat.o"
	"${TC}-gcc-ar" rcs "${D}/glib-2.0/lib/liblwphx-glib.a" "${out}/shim-obj/glib_compat.o"
	sed -i 's|^Libs: \(.*\) -lglib-2.0 |Libs: \1 -llwphx-glib -lglib-2.0 |' "${D}/glib-2.0/lib/pkgconfig/glib-2.0.pc"
	grep -q lhbglib-phoenix "${D}/harfbuzz/lib/pkgconfig/harfbuzz.pc" && grep -q llwphx-glib "${D}/glib-2.0/lib/pkgconfig/glib-2.0.pc" \
		|| { echo "build.sh: text-stack shim .pc edit failed" >&2; exit 1; }

	rm -rf "${LD_PREFIX}"
	mkdir -p "${LD_PREFIX}/lib/pkgconfig"
	cp -a "${libdrm_src_prefix}/include" "${LD_PREFIX}/"
	cp -a "${libdrm_src_prefix}/lib/libdrm.a" "${LD_PREFIX}/lib/"
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
	{ echo "source: ${libdrm_src_prefix}"; sha256sum "${LD_PREFIX}/lib/libdrm.a"; echo "mesa: ${mesa_out}"; } > "${out}/libdrm-snapshot.txt"
	sed 's/^/  /' "${out}/libdrm-snapshot.txt"

	echo "== compat libraries (M6 wlphx + this port's lwphx)"
	mkdir -p "${out}/compat-obj"
	compat_defs=()
	has_libc msync || compat_defs+=(-DWLPHX_NEED_MSYNC)
	has_libc pipe2 || compat_defs+=(-DWLPHX_NEED_PIPE2)
	CFL=(-O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${LCOMPAT_INC}" -I"${COMPAT_INC}" -I"${MESA_COMPAT_INC}")
	for f in wlphx_epoll wlphx_memfd wlphx_misc; do
		"${TC}-gcc" "${CFL[@]}" "${compat_defs[@]}" -c "${W}/compat/src/${f}.c" -o "${out}/compat-obj/${f}.o"
	done
	rm -f "${P}/lib/libwlphx-compat.a" "${P}/lib/liblwphx-compat.a"
	"${TC}-gcc-ar" rcs "${P}/lib/libwlphx-compat.a" "${out}/compat-obj/"wlphx_*.o
	for f in lwphx_shm lwphx_pty lwphx_uchar lwphx_threads lwphx_locale lwphx_sem lwphx_wchar lwphx_epoll_pwait lwphx_misc lwphx_read; do
		"${TC}-gcc" "${CFL[@]}" -c "${here}/compat/src/${f}.c" -o "${out}/compat-obj/${f}.o"
	done
	"${TC}-gcc-ar" rcs "${P}/lib/liblwphx-compat.a" "${out}/compat-obj/"lwphx_*.o
	echo "  stand-ins: ${compat_defs[*]:-none}; lwphx: shm_open shm_unlink posix_openpt mbrtoc32 c32rtomb C11-threads newlocale/uselocale sem_* wcscasecmp/wcsncat epoll_pwait pthread_setname_np reallocarray dirfd"
	cat > "${P}/lib/pkgconfig/wlphx-compat.pc" <<EOF
prefix=${P}
Name: wlphx-compat
Description: weston-drm + labwc-drm libphoenix-gap shims (epoll, timerfd, signalfd, eventfd, memfd_create, shm_open...)
Version: 1.1
Libs: -Wl,-u,__wrap_close -Wl,-u,__wrap_write -Wl,-u,__wrap_read -L\${prefix}/lib -llwphx-compat -lwlphx-compat -Wl,--wrap=close -Wl,--wrap=write -Wl,--wrap=read
Cflags:
EOF
	write_cross
fi

# --- libraries -------------------------------------------------------------------------------
if [ "${relink}" = 0 ]; then
	echo "== libwayland ${WAYLAND_VERSION} (M6 patch)"
	meson_pkg wayland wayland-build -Dlibraries=true -Dscanner=false -Dtests=false -Ddocumentation=false \
		-Ddtd_validation=false
	pc_require_compat "${P}/lib/pkgconfig/wayland-server.pc" "${P}/lib/pkgconfig/wayland-client.pc"

	echo "== wayland-protocols"
	meson_pkg wayland-protocols wayland-protocols-build -Dtests=false

	echo "== libxkbcommon (no X11/wayland tools, no registry)"
	MESON_TARGETS=libxkbcommon.a meson_pkg libxkbcommon xkbcommon-build -Denable-x11=false -Denable-wayland=false -Denable-docs=false \
		-Denable-tools=false -Denable-xkbregistry=false -Denable-bash-completion=false \
		-Dxkb-config-root=/usr/share/X11/xkb -Dx-locale-root=/usr/share/X11/locale

	echo "== pixman (wlroots' pixman renderer needs >= 0.46)"
	meson_pkg pixman pixman-build -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled \
		-Dopenmp=disabled -Dtimers=false

	echo "== libdisplay-info"
	meson_pkg libdisplay-info display-info-build

	echo "== libseat (noop backend only, M6 patches)"
	meson_pkg seatd seatd-build -Dlibseat-logind=disabled -Dlibseat-seatd=disabled -Dlibseat-builtin=disabled \
		-Dserver=disabled -Dexamples=disabled -Dman-pages=disabled -Dwerror=false

	echo "== shims (M6: libudev, libinput-phoenix, libevdev, <linux/input.h>)"
	mkdir -p "${P}/include/evdev" "${out}/shim-obj"
	evc="${P}/include/evdev/input-event-codes.h"
	if [ ! -f "${evc}" ] || ! echo "${EVDEV_CODES_SHA}  ${evc}" | sha256sum -c --quiet - > /dev/null 2>&1; then
		if [ -f "${W}/build-out/prefix/include/evdev/input-event-codes.h" ]; then
			cp "${W}/build-out/prefix/include/evdev/input-event-codes.h" "${evc}"
		elif [ -d "${root}/external/freebsd-src/.git" ]; then
			git -C "${root}/external/freebsd-src" show "${EVDEV_CODES_COMMIT}:sys/dev/evdev/input-event-codes.h" > "${evc}"
		else
			curl -sSfL -o "${evc}" "https://raw.githubusercontent.com/freebsd/freebsd-src/${EVDEV_CODES_COMMIT}/sys/dev/evdev/input-event-codes.h"
		fi
		echo "${EVDEV_CODES_SHA}  ${evc}" | sha256sum -c --quiet - || { echo "build.sh: input-event-codes.h sha256 mismatch" >&2; exit 1; }
	fi
	cp "${out}/src/libinput/src/libinput.h" "${P}/include/libinput.h"
	mkdir -p "${P}/include/linux" "${P}/include/libevdev"
	cp "${SHIM_INC}"/linux/*.h "${P}/include/linux/"
	# wlroots includes <linux/input-event-codes.h> directly
	printf '%s\n' "/* labwc-drm: <linux/input-event-codes.h> = FreeBSD's BSD-2 copy (see linux/input.h) */" \
		"#include <evdev/input-event-codes.h>" > "${P}/include/linux/input-event-codes.h"
	cp "${SHIM_INC}/libevdev/libevdev.h" "${P}/include/libevdev/"
	cp "${SHIM_INC}/libudev.h" "${P}/include/"
	SFLAGS=(-O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${P}/include" -I"${LCOMPAT_INC}" -I"${COMPAT_INC}" -I"${MESA_COMPAT_INC}")
	"${TC}-gcc" "${SFLAGS[@]}" -c "${W}/shims/src/udev_phoenix.c" -o "${out}/shim-obj/udev_phoenix.o"
	"${TC}-gcc" "${SFLAGS[@]}" -c "${W}/shims/src/libevdev_phoenix.c" -o "${out}/shim-obj/libevdev_phoenix.o"
	"${TC}-gcc" "${SFLAGS[@]}" -c "${W}/shims/src/libinput_phoenix.c" -o "${out}/shim-obj/libinput_phoenix.o"
	"${TC}-gcc" "${SFLAGS[@]}" -I"${root}/tools/gpu-lane/xorg-drm/src" \
		-c "${W}/shims/src/libinput_phoenix_hid.c" -o "${out}/shim-obj/libinput_phoenix_hid.o"
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
fi

# --- baked keymap: evdev/pc105/us compiled on the HOST (Phoenix has no xkeyboard-config) ------
if [ "${relink}" = 0 ]; then
	echo "== baked keymap (host libxkbcommon 1.13.2)"
	km="${out}/keymap-us.xkb"
	if [ ! -s "${km}" ]; then
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
	grep -q 'xkb_keymap' "${km}" || { echo "build.sh: ${km} is not a keymap" >&2; exit 1; }
	python3 - "${km}" "${out}/labwc_keymap.h" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src).read()
with open(dst, 'w') as f:
    f.write('/* Generated by tools/gpu-lane/labwc-drm/build.sh from xkbcli-compile-keymap\n')
    f.write(' * --rules evdev --model pc105 --layout us (host xkeyboard-config). */\n')
    f.write('const char labwc_builtin_xkb_keymap[] =\n')
    for line in text.splitlines():
        f.write('\t"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"\n')
    f.write('\t;\n')
PY
	echo "  ${km}: $(wc -c < "${km}") bytes -> labwc_keymap.h"
fi

# --- wlroots ---------------------------------------------------------------------------------
if [ "${relink}" = 0 ]; then
	echo "== wlroots 0.20 (drm + libinput + headless; gles2 + pixman; gbm; libseat)"
	meson_pkg wlroots wlroots-build -Dbackends=drm,libinput -Drenderers=gles2 -Dallocators=gbm \
		-Dsession=enabled -Dxwayland=disabled -Dexamples=false -Dlibliftoff=disabled \
		-Dcolor-management=disabled -Dxcb-errors=disabled -Dwerror=false
	grep -E 'drm-backend|libinput-backend|gles2-renderer|gbm-allocator|session|dmabuf_(linux|fallback)' \
		"${out}/wlroots-build-setup.log" | sed 's/^/  /' || true
fi

# --- text stack for labwc (libxml2, fribidi, pango over the ports cairo/harfbuzz/glib) and
# --- for foot (tllist, fcft over the ports fontconfig/freetype/harfbuzz) -----------------
if [ "${relink}" = 0 ]; then
	echo "== libxml2 (tree API only: no zlib, iconv, ICU, HTTP, modules, python)"
	meson_pkg libxml2 libxml2-build -Dpython=disabled -Dzlib=disabled -Dicu=disabled -Diconv=disabled \
		-Dhttp=disabled -Dmodules=disabled -Dreadline=disabled -Dhistory=disabled -Ddocs=disabled \
		-Dsax1=enabled -Dcatalog=disabled -Ddebugging=disabled

	echo "== fribidi"
	meson_pkg fribidi fribidi-build -Ddocs=false -Dbin=false -Dtests=false

	echo "== pango 1.44 (cairo + fontconfig/freetype + harfbuzz; ports GLib 2.56)"
	meson_pkg pango pango-build -Dintrospection=false -Dgtk_doc=false -Dinstall-tests=false

	echo "== tllist, fcft (foot's font rasterizer: fontconfig/freetype/harfbuzz/pixman)"
	meson_pkg tllist tllist-build
	meson_pkg fcft fcft-build -Ddocs=disabled -Dtest-text-shaping=false -Dexamples=false \
		-Dgrapheme-shaping=enabled -Drun-shaping=disabled -Dsvg-backend=none
fi

# --- labwc and foot: meson builds the objects, the programs are hand-linked below ----------
if [ "${relink}" = 0 ]; then
	echo "== labwc 0.20 (no Xwayland, no SVG buttons, no window icons, no NLS)"
	meson_objs labwc labwc-build labwc -Dxwayland=disabled -Dsvg=disabled -Dicon=disabled -Dnls=disabled \
		-Dman-pages=disabled -Dlabnag=disabled -Dtest=disabled -Dsystemd-session=disabled -Dwerror=false
	echo "== foot 1.28 (xterm-256color: ncurses' built-in fallback on Phoenix, no terminfo files)"
	# foot's char32_t conversions are compat's UTF-8 ones: MB_CUR_MAX-sized buffers need 4.
	# libphoenix's wchar_t is 32 bits and holds code points (its C-locale mapping is
	# byte = U+0000..U+00FF), which is what __STDC_ISO_10646__ asserts (glibc's
	# stdc-predef.h defines it; foot refuses to build without it).
	# buildtype plain (+ -O2 -g): a "debug*" buildtype defines _DEBUG, which turns foot's
	# and fuzzel's UNITTEST blocks into constructors that run at every start (m7b: the
	# XKB errors of a Swedish-keymap test on the UART)
	BUILDTYPE=plain EXTRA_C_ARGS="'-O2', '-g', '-DLWPHX_UTF8_MB_CUR_MAX', '-D__STDC_ISO_10646__=201706L'" meson_objs foot foot-build foot -Ddocs=disabled -Dthemes=false -Dime=true -Dgrapheme-clustering=disabled \
		-Dtests=false -Dterminfo=disabled -Ddefault-terminfo=xterm-256color -Dutmp-backend=none -Dwerror=false
	echo "== fuzzel 1.15 (launcher: fcft + pixman, PNG icons, bundled nanosvg; no cairo)"
	BUILDTYPE=plain EXTRA_C_ARGS="'-O2', '-g', '-DLWPHX_UTF8_MB_CUR_MAX', '-D__STDC_ISO_10646__=201706L'" meson_objs fuzzel fuzzel-build fuzzel \
		-Denable-cairo=disabled -Dpng-backend=libpng -Dsvg-backend=nanosvg -Dwerror=false
	echo "== swaybg 1.2 (wallpaper: cairo PNG loader, no gdk-pixbuf)"
	meson_objs swaybg swaybg-build swaybg -Dgdk-pixbuf=disabled -Dman-pages=disabled -Dwerror=false
fi

# --- link ------------------------------------------------------------------------------------
# Hand links (as weston-drm): meson's own executables would need Mesa's full static
# closure, which Mesa's .pc files do not describe. Gallium whole-archive (found by
# name in egl-link.txt), then one group of every other archive; --wrap=mmap/ioctl
# for libdrm-phoenix, --wrap=close/write for the compat event loop descriptors.
echo "== link"
OBJ="${out}/obj"
mkdir -p "${OBJ}"
gallium=""
MESA_A=()
while IFS= read -r l; do
	case "${l}" in
		"--whole-archive "*) gallium="${l#--whole-archive }" ;;
		*/libdrm-prefix/lib/libdrm.a) MESA_A+=("${LD_PREFIX}/lib/libdrm.a") ;;   # THIS build's snapshot
		*/lib/libz.a) MESA_A+=("${D}/zlib/lib/libz.a") ;;
		*) MESA_A+=("${l}") ;;
	esac
done < "${mesa_out}/egl-link.txt"
[ -f "${gallium}" ] || { echo "build.sh: no gallium archive in ${mesa_out}/egl-link.txt" >&2; exit 1; }

LINK_BASE=("${TC}-g++" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000
	-Wl,--wrap=close -Wl,--wrap=write -Wl,--wrap=read -Wl,-u,__wrap_close -Wl,-u,__wrap_write -Wl,-u,__wrap_read)
LINK_DRM=("${LINK_BASE[@]}" -Wl,--wrap=mmap -Wl,--wrap=ioctl)
COMPAT_LIBS=("${P}/lib/liblwphx-compat.a" "${P}/lib/libwlphx-compat.a")
WLR_LIBS=("${P}/lib/libwlroots-0.20.a" "${P}/lib/libwayland-server.a" "${P}/lib/libwayland-client.a"
	"${P}/lib/libxkbcommon.a" "${P}/lib/libdisplay-info.a" "${P}/lib/libseat.a" "${P}/lib/libinput.a"
	"${P}/lib/libudev.a" "${P}/lib/libevdev.a" "${P}/lib/libpixman-1.a" "${D}/libffi/lib/libffi.a" "${COMPAT_LIBS[@]}")
WLR_CFLAGS=(-I"${P}/include/wlroots-0.20" -I"${P}/include" -I"${P}/include/pixman-1" -I"${LD_PREFIX}/include"
	-I"${LD_PREFIX}/include/libdrm" -I"${mesa_out}/prefix/include" -I"${LCOMPAT_INC}" -I"${COMPAT_INC}" -I"${MESA_COMPAT_INC}"
	-DWLR_USE_UNSTABLE)

link_prog() {  # base|drm output link-arguments...
	local kind="$1" o="$2"
	local -a L
	shift 2
	if [ "${kind}" = drm ]; then L=("${LINK_DRM[@]}"); else L=("${LINK_BASE[@]}"); fi
	"${L[@]}" ${LINK_EXTRA:-} -Wl,-Map,"${out}/${o}.map" -o "${out}/${o}" "$@" > "${out}/${o}-link.log" 2>&1 \
		|| { grep -v 'warning: .* is not fully supported' "${out}/${o}-link.log" | head -60; exit 1; }
	"${TC}-strip" -o "${out}/${o}-stripped" "${out}/${o}"
	echo "  ${o}: $(stat -c %s "${out}/${o}") bytes, stripped $(stat -c %s "${out}/${o}-stripped")"
}

# tinywl (wlroots' own minimal compositor, MIT): the link probe of wlroots alone, and a
# fallback compositor for the first Pi cycle
wayland-scanner server-header "${P}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" "${OBJ}/xdg-shell-protocol.h"
"${TC}-gcc" -O2 -g -std=c11 -D_POSIX_C_SOURCE=200809L "${TFLAGS[@]}" "${WLR_CFLAGS[@]}" -I"${OBJ}" \
	-c "${out}/src/wlroots/tinywl/tinywl.c" -o "${OBJ}/tinywl.o"
link_prog drm tinywl "${OBJ}/tinywl.o" -Wl,--whole-archive "${gallium}" -Wl,--no-whole-archive \
	-Wl,--start-group "${WLR_LIBS[@]}" "${MESA_A[@]}" -Wl,--end-group -lm

# The text stack (labwc: pango/cairo/libxml2/GLib; foot: fcft)
TEXT_LIBS=("${P}/lib/libpangocairo-1.0.a" "${P}/lib/libpangoft2-1.0.a" "${P}/lib/libpango-1.0.a"
	"${P}/lib/libfribidi.a" "${D}/cairo/lib/libcairo.a" "${D}/harfbuzz/lib/libhbglib-phoenix.a"
	"${D}/harfbuzz/lib/libharfbuzz.a" "${D}/fontconfig/lib/libfontconfig.a" "${D}/freetype2/lib/libfreetype.a"
	"${D}/expat/lib/libexpat.a" "${D}/libpng16/lib/libpng16.a" "${D}/zlib/lib/libz.a"
	"${D}/gobject-2.0/lib/libgobject-2.0.a" "${D}/glib-2.0/lib/liblwphx-glib.a" "${D}/glib-2.0/lib/libglib-2.0.a"
	"${D}/libiconv/lib/libiconv.a" "${P}/lib/libxml2.a")

# labwc: the compositor, with the builtin keymap
"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${out}" -c "${here}/src/labwc_builtin.c" -o "${OBJ}/labwc_builtin.o"
mapfile -t LABWC_OBJS < <(ninja_objs "${out}/labwc-build" labwc)
[ "${#LABWC_OBJS[@]}" -gt 50 ] || { echo "build.sh: labwc objects not found" >&2; exit 1; }
LABWC_OBJS=("${LABWC_OBJS[@]/#/${out}/labwc-build/}")
link_prog drm labwc "${LABWC_OBJS[@]}" "${OBJ}/labwc_builtin.o" -Wl,--whole-archive "${gallium}" -Wl,--no-whole-archive \
	-Wl,--start-group "${WLR_LIBS[@]}" "${TEXT_LIBS[@]}" "${MESA_A[@]}" -Wl,--end-group "${S}/lib/libm.a"  # libm BEFORE libstdc++ (whose hypotf stub collides); g++ moves a plain -lm after it

# foot: the terminal (wl_shm client: no Mesa, no libdrm)
mapfile -t FOOT_OBJS < <(ninja_objs "${out}/foot-build" foot)
[ "${#FOOT_OBJS[@]}" -gt 30 ] || { echo "build.sh: foot objects not found" >&2; exit 1; }
FOOT_OBJS=("${FOOT_OBJS[@]/#/${out}/foot-build/}")
link_prog base foot -Wl,--start-group "${FOOT_OBJS[@]}" "${P}/lib/libfcft.a" "${P}/lib/libwayland-client.a" "${P}/lib/libwayland-cursor.a" \
	"${P}/lib/libxkbcommon.a" "${P}/lib/libpixman-1.a" "${D}/harfbuzz/lib/libharfbuzz.a" "${D}/fontconfig/lib/libfontconfig.a" \
	"${D}/freetype2/lib/libfreetype.a" "${D}/expat/lib/libexpat.a" "${D}/libpng16/lib/libpng16.a" "${D}/zlib/lib/libz.a" \
	"${D}/libffi/lib/libffi.a" "${COMPAT_LIBS[@]}" -Wl,--end-group "${S}/lib/libm.a"  # libm BEFORE libstdc++ (whose hypotf stub collides); g++ moves a plain -lm after it

# fuzzel: the launcher (wl_shm client, layer-shell)
mapfile -t FUZZEL_OBJS < <(ninja_objs "${out}/fuzzel-build" fuzzel)
[ "${#FUZZEL_OBJS[@]}" -gt 10 ] || { echo "build.sh: fuzzel objects not found" >&2; exit 1; }
FUZZEL_OBJS=("${FUZZEL_OBJS[@]/#/${out}/fuzzel-build/}")
link_prog base fuzzel -Wl,--start-group "${FUZZEL_OBJS[@]}" "${P}/lib/libfcft.a" "${P}/lib/libwayland-client.a" \
	"${P}/lib/libwayland-cursor.a" "${P}/lib/libxkbcommon.a" "${P}/lib/libpixman-1.a" "${D}/harfbuzz/lib/libharfbuzz.a" \
	"${D}/fontconfig/lib/libfontconfig.a" "${D}/freetype2/lib/libfreetype.a" "${D}/expat/lib/libexpat.a" \
	"${D}/libpng16/lib/libpng16.a" "${D}/zlib/lib/libz.a" "${D}/libffi/lib/libffi.a" "${COMPAT_LIBS[@]}" \
	-Wl,--end-group "${S}/lib/libm.a"

# swaybg: the wallpaper (cairo image surface from PNG, layer-shell background)
mapfile -t SWAYBG_OBJS < <(ninja_objs "${out}/swaybg-build" swaybg)
[ "${#SWAYBG_OBJS[@]}" -gt 3 ] || { echo "build.sh: swaybg objects not found" >&2; exit 1; }
SWAYBG_OBJS=("${SWAYBG_OBJS[@]/#/${out}/swaybg-build/}")
link_prog base swaybg -Wl,--start-group "${SWAYBG_OBJS[@]}" "${P}/lib/libwayland-client.a" "${D}/cairo/lib/libcairo.a" \
	"${P}/lib/libpixman-1.a" "${D}/fontconfig/lib/libfontconfig.a" "${D}/freetype2/lib/libfreetype.a" \
	"${D}/expat/lib/libexpat.a" "${D}/libpng16/lib/libpng16.a" "${D}/zlib/lib/libz.a" "${D}/libffi/lib/libffi.a" \
	"${COMPAT_LIBS[@]}" -Wl,--end-group "${S}/lib/libm.a"

# --- verification ----------------------------------------------------------------------------
echo "== verify"
bad=0
for o in labwc foot tinywl fuzzel swaybg; do
	und="$("${TC}-nm" -u "${out}/${o}" || true)"
	n=$(grep -c . <<< "${und}" || true)
	interp=$("${TC}-readelf" -l "${out}/${o}" | grep -c INTERP || true)
	echo "  ${o}: nm -u ${n}, PT_INTERP ${interp}; $("${TC}-size" "${out}/${o}" | awk 'NR==2 {printf "text %d data %d bss %d", $1, $2, $3}')"
	[ "${n}" = 0 ] && [ "${interp}" = 0 ] || { sed 's/^/    /' <<< "${und}" | head -10; bad=1; }
	lw="$(grep -v -E 'warning: .*(is not fully supported|dlopen|getpwnam|getpwuid|getgrnam|initgroups)' "${out}/${o}-link.log" 2>/dev/null | grep -c 'warning' || true)"
	echo "    link warnings beyond libphoenix attribute notes: ${lw}"
done
check_syms() {  # binary symbols...
	local b="$1" s syms
	shift
	syms="$("${TC}-nm" "${out}/${b}")"
	for s in "$@"; do
		if grep -qE " [TtDdRrBbWw] ${s}\$" <<< "${syms}"; then echo "  ${b} symbol ${s}: yes"; else echo "  ${b} symbol ${s}: NO"; bad=1; fi
	done
}
check_syms labwc wlr_drm_backend_create wlr_libinput_backend_create wlr_headless_backend_create wlr_gles2_renderer_create_with_drm_fd \
	wlr_pixman_renderer_create wlr_gbm_allocator_create wlr_drm_dumb_allocator_create wlr_session_create libseat_open_seat udev_enumerate_scan_devices \
	di_info_parse_edid labwc_builtin_xkb_keymap xkb_keymap_new_from_string pango_cairo_show_layout xmlReadMemory \
	g_string_replace hb_glib_script_to_script shm_open shm_unlink epoll_wait signalfd timerfd_settime eventfd \
	__wrap_mmap __wrap_ioctl __wrap_close drm_phoenix_ioctl kmsro_drm_screen_create gbmint_get_backend \
	wlr_layer_shell_v1_create wlr_foreign_toplevel_manager_v1_create wlr_xdg_output_manager_v1_create \
	wlr_xdg_activation_v1_create wlr_xdg_decoration_manager_v1_create
check_syms foot fcft_from_name wl_display_connect memfd_create epoll_wait epoll_pwait timerfd_settime posix_openpt \
	__wrap_read wlphx_timer_read \
	mbrtoc32 c32rtomb newlocale thrd_create sem_init __wrap_close
strs="$(strings -a "${out}/labwc-stripped")"
for s in 'using the builtin XKB keymap' 'libdrm-phoenix:' '/dev/dri/card0' 'EGL_KHR_platform_gbm' 'V3D 4.2' \
		'backend/drm' 'render/gles2' 'Unable to open' 'LIBINPUT-PHX' '/shm' 'rc.xml' 'menu.xml' 'autostart'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  labwc strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
check_syms fuzzel fcft_from_name2 wl_display_connect memfd_create epoll_wait timerfd_settime mbrtoc32 sem_init \
	zwlr_layer_shell_v1_interface png_read_info __wrap_close
check_syms swaybg wl_display_connect cairo_image_surface_create_from_png zwlr_layer_shell_v1_interface shm_open __wrap_close
strs="$(strings -a "${out}/foot-stripped")"
for s in 'xterm-256color' 'C.UTF-8' '/dev/ptmx' 'failed to seal SHM backing memory file'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  foot strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
for b in labwc-stripped foot-stripped tinywl-stripped fuzzel-stripped swaybg-stripped; do
	bs="$(strings -a "${out}/${b}")"
	for s in 'v3d-winsys:' phoenix_v3d_ioctl peek_next_scanout v3d-srv /dev/v3d-srv Xphoenix '[fbdev]' glamor_phoenix phxgl; do
		n=$(grep -cF -- "${s}" <<< "${bs}" || true)
		[ "${n}" = 0 ] || { echo "  OLD-LANE string '${s}' in ${b}: ${n}"; bad=1; }
	done
done
echo "  old-lane strings: $([ "${bad}" = 0 ] && echo none || echo 'see above')"
sha256sum "${out}"/labwc-stripped "${out}"/foot-stripped "${out}"/tinywl-stripped "${out}"/fuzzel-stripped \
	"${out}"/swaybg-stripped "${out}"/labwc "${out}"/foot "${out}"/fuzzel "${out}"/swaybg | sed "s|${out}/||; s/^/  /"
[ "${bad}" = 0 ] || { echo "build.sh: verification failed" >&2; exit 1; }
echo "done"
