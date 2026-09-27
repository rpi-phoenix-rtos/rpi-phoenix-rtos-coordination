#!/usr/bin/env bash
#
# atril-wayland (new GPU lane, M7): Atril, MATE's GTK 3 document viewer (Xubuntu's default
# PDF viewer), with the Poppler PDF backend, cross-built STATIC for aarch64-phoenix on top of
# the GTK 3 Wayland-only stack of tools/gpu-lane/gtk3-wayland (built with --usr: every package
# configured for /usr and /etc, installed with DESTDIR).
#
#   libxml2 2.15.4      (meson, MIT)        Atril: PDF XMP metadata, the toolbar editor
#   lcms2 2.19.1        (meson, MIT)        Poppler: ICC colour management
#   openjpeg 2.5.4      (cmake, BSD-2)      Poppler: JPEG 2000 (JPX) images
#   Poppler 26.09.0     (cmake, GPL-2/3)    PDF rendering: core + poppler-glib (cairo);
#                                           no Qt/cpp/utils/NSS/GPGME/curl/tiff/boost/harfbuzz
#   Atril 1.28.7        (meson, GPL-2+)     the PDF backend LINKED IN (patch 0004), no X11/SM,
#                                           no mate-desktop, no D-Bus daemon, no keyring, no
#                                           caja extension/thumbnailer/previewer/introspection
#
# The GTK stack is SNAPSHOTTED from the gtk3-wayland --usr out dir into <out>/gtk/ (destdir +
# deps views), as tools/gpu-lane/xfce-wayland does, so a rebuild there cannot change this build.
#
#   <out>/dl/          pinned tarballs (sha256 below)
#   <out>/src/<pkg>/   extracted + patches/<pkg>/*.patch (git am)
#   <out>/gtk/         snapshot of the GTK stack (destdir/usr + deps)
#   <out>/destdir/usr  everything built here (static .a, headers, .pc)
#   <out>/bin/         atril, unstripped (addr2line) and -stripped
#   <out>/data/        the sample PDF, Atril's compiled schema, its icons
#   <out>/stage/       a tree mirroring the NFS root (new names only) + <out>/stage.MANIFEST
#
# Writes only into <out> (default build-out/, gitignored). No Pi, no rebuild-rpi4b-fast.sh, no
# /srv. Poppler and Atril are GPL: their sources live only in <out>/src (fetched, sha256-pinned
# tarballs), never in sources/.
#
# Host tools: meson, ninja, cmake >= 3.28, python3 (+ pycairo for the sample PDF),
# glib-compile-resources, glib-compile-schemas, glib-mkenums, glib-genmarshal, gdbus-codegen.
# No gettext: tools/gpu-lane/xfce-wayland/bin/msgfmt stands in (no translations are shipped).
#
# Usage: tools/gpu-lane/atril-wayland/build.sh [--clean] [--out <dir>] [-j N]
#            [--gtk-out <gtk3-wayland --usr out dir>]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
jobs="$(nproc)"
clean=0
gtk_out="${root}/tools/gpu-lane/gtk3-wayland/build-out-usr"
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		--gtk-out) shift; gtk_out="${1:?}" ;;
		--gtk-out=*) gtk_out="${1#--gtk-out=}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
case "${gtk_out}" in /*) ;; *) gtk_out="${PWD}/${gtk_out}" ;; esac

if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
GTKW="${root}/tools/gpu-lane/gtk3-wayland"
PHXCC="${GTKW}/bin/phx-gcc"      # drops -pthread, also inside @response files
PHXCXX="${GTKW}/bin/phx-g++"
COMPAT_INC="${root}/tools/gpu-lane/weston-drm/compat/include"
MSGFMT_DIR="${root}/tools/gpu-lane/xfce-wayland/bin"
GS="${out}/gtk"                  # the GTK snapshot
X="${out}/destdir"               # DESTDIR of this build
P="${X}/usr"
SYSD="${GS}/deps/sys"            # libintl stub, libiconv, resolv (from the GTK build)
SCHEMAS_DIR=/usr/share/atril/schemas   # Atril's compiled schema on the Pi (patch 0005)

# name|file|url|sha256
PKGS=(
	"libxml2|libxml2-2.15.4.tar.xz|https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.4.tar.xz|98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821"
	"lcms2|lcms2-2.19.1.tar.gz|https://github.com/mm2/Little-CMS/releases/download/lcms2.19.1/lcms2-2.19.1.tar.gz|bfc54f7bab59fbc921012014a8032e4cba4abd46db47d46b76416a8c0b2815c8"
	"openjpeg|openjpeg-2.5.4.tar.gz|https://github.com/uclouvain/openjpeg/archive/refs/tags/v2.5.4.tar.gz|a695fbe19c0165f295a8531b1e4e855cd94d0875d2f88ec4b61080677e27188a"
	"poppler|poppler-26.09.0.tar.xz|https://poppler.freedesktop.org/poppler-26.09.0.tar.xz|8059eadb6805340768f138c465b57f8164c92b4a0773c37ef031ea6c0d987b2e"
	"atril|atril-1.28.7.tar.xz|https://github.com/mate-desktop/atril/releases/download/v1.28.7/atril-1.28.7.tar.xz|91942545e858cb0036b52e2f5d4372cdea813d9fde9702daefdf84854e6bb667"
)

# The Poppler configuration: the SAME list for the Pi and for the host test (hosttest/run.sh)
. "${here}/poppler-options.sh"

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-g++" "${TC}-gcc-ar" "${TC}-nm" "${TC}-strip" "${PHXCC}" "${PHXCXX}" \
		"${gtk_out}/destdir/usr/lib/libgtk-3.a" "${gtk_out}/deps/wayland/lib/libwlphx-compat.a" \
		"${gtk_out}/phoenix-aarch64-wl.cross" "${MSGFMT_DIR}/msgfmt"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p} (gtk3-wayland/build.sh --usr --out ${gtk_out} first?)" >&2; exit 1; }
done
for t in meson ninja cmake python3 glib-compile-resources glib-compile-schemas glib-mkenums glib-genmarshal gdbus-codegen; do
	command -v "${t}" > /dev/null || { echo "build.sh: host tool ${t} not found" >&2; exit 1; }
done
grep -q '^prefix=/usr$' "${gtk_out}/destdir/usr/lib/pkgconfig/gtk+-3.0.pc" \
	|| { echo "build.sh: ${gtk_out} is not a gtk3-wayland --usr build" >&2; exit 1; }
python3 -c 'import cairo' 2> /dev/null || { echo "build.sh: python3 cairo (pycairo) not found (the sample PDF)" >&2; exit 1; }

mkdir -p "${out}/dl" "${out}/src" "${out}/bin" "${out}/data" "${P}/lib/pkgconfig"
export PATH="${MSGFMT_DIR}:${PATH}"
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
		# its own git repository: `git am` inside a directory of ANOTHER repository skips paths
		git -C "${dir}" init -q
		git -C "${dir}" add -A -f
		git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "${file}"
		for p in "${here}/patches/${name}"/*.patch; do
			[ -e "${p}" ] || continue
			echo "  apply ${name}/$(basename "${p}")"
			git -C "${dir}" -c user.name=build -c user.email=build@invalid am -q --whitespace=nowarn "${p}"
		done
		git -C "${dir}" diff "$(git -C "${dir}" rev-list --max-parents=0 HEAD)" > "${out}/${name}-full.patch"
		echo "${stamp}" > "${dir}.stamp"
		rm -f "${out}/${name}.built"   # a changed source rebuilds the package
	fi
}

# --- the GTK snapshot, cross files, pkg-config, cmake toolchain ------------------------------
snapshot_gtk() {
	local stamp t
	stamp="$(sha256sum "${gtk_out}/SHA256SUMS" "${gtk_out}/destdir/usr/lib/libgtk-3.a" \
		"${gtk_out}/destdir/usr/lib/libglib-2.0.a" "${gtk_out}/deps/wayland/lib/libwlphx-compat.a" | sha256sum | cut -c1-16)"
	if [ "$(cat "${GS}.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		rm -rf "${GS}"
		mkdir -p "${GS}"
		cp -a --reflink=auto "${gtk_out}/destdir" "${gtk_out}/deps" "${GS}/"
		cp -a "${gtk_out}/SHA256SUMS" "${GS}/SHA256SUMS.gtk3-wayland"
		echo "${stamp}" > "${GS}.stamp"
		rm -f "${out}"/*.built   # everything is rebuilt on a new GTK stack
		echo "  snapshot of ${gtk_out} (stamp ${stamp})"
	fi
	# pkgconf --define-prefix moves GLib's tool variables into the snapshot: make those the HOST's
	for t in glib-compile-resources glib-compile-schemas glib-mkenums glib-genmarshal gdbus-codegen gobject-query; do
		[ -e "${GS}/destdir/usr/bin/${t}" ] || [ -L "${GS}/destdir/usr/bin/${t}" ] || continue
		command -v "${t}" > /dev/null || continue
		ln -sfn "$(command -v "${t}")" "${GS}/destdir/usr/bin/${t}"
	done
	{ echo "gtk3-wayland --usr: ${gtk_out} (stamp ${stamp})"
	  sha256sum "${GS}"/destdir/usr/lib/lib{gtk-3,gdk-3,glib-2.0,gio-2.0,cairo,pango-1.0}.a "${GS}/deps/wayland/lib/libwlphx-compat.a" \
		| sed "s|${GS}/||"; } > "${out}/snapshots.txt"
}

PKGC="${out}/pkg-config-phoenix"
write_cross() {
	local v libdirs="" c
	for v in zlib libffi expat pixman-1 libpng16 libjpeg freetype2 fontconfig wayland epoxy; do
		libdirs="${libdirs}:${GS}/deps/${v}/lib/pkgconfig"
	done
	cat > "${PKGC}" <<EOF
#!/bin/sh
# pkg-config over this build's DESTDIR, the GTK snapshot and its private ports views. Every
# installed .pc says prefix=/usr; --define-prefix takes each file's prefix from where it lies.
export PKG_CONFIG_LIBDIR=${P}/lib/pkgconfig:${P}/share/pkgconfig:${GS}/destdir/usr/lib/pkgconfig:${GS}/destdir/usr/share/pkgconfig${libdirs}:${GS}/deps/wayland/share/pkgconfig
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
exec /usr/bin/pkg-config --static --define-prefix "\$@"
EOF
	chmod +x "${PKGC}"
	# the GTK build's cross files, pointed at the snapshot and this pkg-config; plus
	# -fmacro-prefix-map (no build-host paths in g_return_if_fail() messages) and --gc-sections
	for c in "" -wl; do
		sed -e "s|${gtk_out}|${GS}|g" -e "s|^pkg-config = .*|pkg-config = '${PKGC}'|" \
			-e "s#^\(c\|cpp\)_args = \[#\1_args = ['-fmacro-prefix-map=../src/=', '-fmacro-prefix-map=${out}/src/=', '-fmacro-prefix-map=${GS}/destdir/usr/include/=', '-fmacro-prefix-map=${P}/include/=', #" \
			-e "s#^\(c\|cpp\)_link_args = \[#\1_link_args = ['-Wl,--gc-sections', #" \
			-e "s|^# Generated by .*|# Generated by tools/gpu-lane/atril-wayland/build.sh from gtk3-wayland's phoenix-aarch64${c}.cross|" \
			"${gtk_out}/phoenix-aarch64${c}.cross" > "${out}/phoenix-aarch64${c}.cross"
	done

	# CMake (openjpeg, Poppler): one root that looks like a /usr tree -- this build's DESTDIR,
	# the GTK snapshot and the ports views, as symlinks -- so CMake's own Find modules
	# (Freetype, Fontconfig, JPEG, PNG, ZLIB) search only target files (FIND_ROOT_PATH ONLY);
	# programs (glib-mkenums) come from the host.
	rm -rf "${out}/cmake-root"
	mkdir -p "${out}/cmake-root/usr"
	cat > "${out}/phoenix-aarch64.cmake" <<EOF
# Generated by tools/gpu-lane/atril-wayland/build.sh (aarch64-phoenix, Pi 4).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER "${PHXCC}")
set(CMAKE_CXX_COMPILER "${PHXCXX}")
set(CMAKE_AR "${TC}-gcc-ar")
set(CMAKE_RANLIB "${TC}-gcc-ranlib")
set(CMAKE_NM "${TC}-nm")
set(CMAKE_STRIP "${TC}-strip")
set(CMAKE_C_FLAGS_INIT "${TFLAGS[*]} -I${SYSD}/include -fmacro-prefix-map=${out}/src/=")
set(CMAKE_CXX_FLAGS_INIT "${TFLAGS[*]} -I${SYSD}/include -fmacro-prefix-map=${out}/src/=")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--sysroot=${S}/ -B${S}/lib/ -L${SYSD}/lib -Wl,-z,max-page-size=0x1000")
set(CMAKE_FIND_ROOT_PATH "${out}/cmake-root")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(PKG_CONFIG_EXECUTABLE "${PKGC}")
EOF
}

refresh_cmake_root() {  # the symlink /usr view, after each install into DESTDIR
	local r="${out}/cmake-root/usr" v
	rm -rf "${r}"
	mkdir -p "${r}/include" "${r}/lib"
	for v in zlib libffi expat pixman-1 libpng16 libjpeg freetype2 fontconfig; do
		cp -asf "${GS}/deps/${v}/include/." "${r}/include/"
		cp -asf "${GS}/deps/${v}/lib/." "${r}/lib/"
	done
	cp -asf "${SYSD}/include/." "${r}/include/"
	cp -asf "${SYSD}/lib/." "${r}/lib/"
	cp -asf "${GS}/destdir/usr/include/." "${r}/include/"
	cp -asf "${GS}/destdir/usr/lib/." "${r}/lib/"
	[ -d "${P}/include" ] && cp -asf "${P}/include/." "${r}/include/"
	cp -asf "${P}/lib/." "${r}/lib/"
	rm -rf "${r}/lib/pkgconfig"   # pkg-config is PKG_CONFIG_EXECUTABLE (the views' .pc)
}

meson_pkg() {  # [--cross <file>] name meson-args...
	local cross="${out}/phoenix-aarch64.cross"
	if [ "$1" = --cross ]; then cross="$2"; shift 2; fi
	local name="$1"
	shift
	local bd="${out}/${name}-build"
	if [ -f "${out}/${name}.built" ]; then
		echo "  ${name}: up to date"
		return 0
	fi
	rm -rf "${bd}"
	meson setup "${bd}" "${out}/src/${name}" --cross-file "${cross}" --prefix /usr --sysconfdir /etc \
		--localstatedir /var --libdir lib --buildtype=debugoptimized -Db_staticpic=false -Db_ndebug=if-release \
		--wrap-mode=nodownload "$@" > "${out}/${name}-setup.log" 2>&1 || { tail -40 "${out}/${name}-setup.log"; exit 1; }
	ninja -C "${bd}" -j"${jobs}" > "${out}/${name}-ninja.log" 2>&1 || { grep -E -A3 'error:|undefined reference|multiple definition|FAILED' "${out}/${name}-ninja.log" | cut -c1-400 | head -60; exit 1; }
	DESTDIR="${X}" ninja -C "${bd}" install > "${out}/${name}-install.log" 2>&1 || { tail -20 "${out}/${name}-install.log"; exit 1; }
	echo "  ${name}: built ($(grep -c 'warning:' "${out}/${name}-ninja.log" || true) warning line(s))"
	touch "${out}/${name}.built"
}

cmake_pkg() {  # name cmake-args...
	local name="$1"
	shift
	local bd="${out}/${name}-build"
	if [ -f "${out}/${name}.built" ]; then
		echo "  ${name}: up to date"
		return 0
	fi
	refresh_cmake_root
	rm -rf "${bd}"
	cmake -S "${out}/src/${name}" -B "${bd}" -G Ninja -DCMAKE_TOOLCHAIN_FILE="${out}/phoenix-aarch64.cmake" \
		-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@" \
		> "${out}/${name}-setup.log" 2>&1 || { grep -v '^--' "${out}/${name}-setup.log" | tail -40; exit 1; }
	ninja -C "${bd}" -j"${jobs}" > "${out}/${name}-ninja.log" 2>&1 || { grep -E -A3 'error:|undefined reference|FAILED' "${out}/${name}-ninja.log" | cut -c1-400 | head -60; exit 1; }
	DESTDIR="${X}" ninja -C "${bd}" install > "${out}/${name}-install.log" 2>&1 || { tail -20 "${out}/${name}-install.log"; exit 1; }
	echo "  ${name}: built ($(grep -c 'warning:' "${out}/${name}-ninja.log" || true) warning line(s))"
	touch "${out}/${name}.built"
}

echo "== sources"
for rec in "${PKGS[@]}"; do fetch_extract "${rec%%|*}"; done

echo "== GTK snapshot (gtk3-wayland --usr)"
snapshot_gtk
write_cross

# --- libraries -------------------------------------------------------------------------------
echo "== libxml2 (tree + XPath; no zlib, iconv, ICU, HTTP, modules, python)"
meson_pkg libxml2 -Dpython=disabled -Dzlib=disabled -Dicu=disabled -Diconv=disabled \
	-Dhttp=disabled -Dmodules=disabled -Dreadline=disabled -Dhistory=disabled -Ddocs=disabled \
	-Dsax1=enabled -Dcatalog=disabled -Ddebugging=disabled

echo "== lcms2 (no tiff/jpeg utilities, no GPL plugins)"
meson_pkg lcms2 -Dtests=disabled -Djpeg=disabled -Dtiff=disabled -Dutils=false -Dfastfloat=false -Dthreaded=false

echo "== openjpeg (libopenjp2 only)"
cmake_pkg openjpeg -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CODEC=OFF -DBUILD_TESTING=OFF \
	-DBUILD_DOC=OFF -DBUILD_JPIP=OFF -DBUILD_THIRDPARTY=OFF -DBUILD_PKGCONFIG_FILES=ON

echo "== Poppler (core + glib/cairo; patch 0001: fontconfig 2.14)"
# (FindPNG looks for <png.h> directly under include/: the ports view has it in include/libpng16/)
cmake_pkg poppler "${POPPLER_OPTS[@]}" -DENABLE_UTILS=OFF -DTESTDATADIR="${out}/src/poppler/test" \
	-DPNG_PNG_INCLUDE_DIR="${out}/cmake-root/usr/include/libpng16"

echo "== Atril (the PDF backend built in: patch 0004; no X11/mate-desktop: 0001-0003)"
meson_pkg --cross "${out}/phoenix-aarch64-wl.cross" atril -Dc_std=gnu11 \
	-Dpdf=enabled -Dps=disabled -Ddvi=disabled -Dt1lib=disabled -Ddjvu=disabled -Dtiff=disabled -Dpixbuf=disabled \
	-Dcomics=disabled -Dxps=disabled -Depub=disabled -Dcaja=disabled -Dx11=disabled -Dmate_desktop=disabled \
	-Dbuiltin_backends=true -Dschemas_dir="${SCHEMAS_DIR}" -Dgtk_unix_print=false -Dkeyring=false \
	-Dpreviewer=false -Dthumbnailer=false -Ddocs=false -Dhelp_files=false -Dintrospection=false -Denable_dbus=false

# --- data: the compiled schema, the sample PDF ------------------------------------------------
echo "== data"
rm -rf "${out}/data/schemas"
mkdir -p "${out}/data/schemas"
cp "${X}${SCHEMAS_DIR}/org.mate.Atril.gschema.xml" "${out}/data/schemas/"
glib-compile-schemas --strict "${out}/data/schemas"
echo "  ${SCHEMAS_DIR}/gschemas.compiled: $(stat -c %s "${out}/data/schemas/gschemas.compiled") bytes"
python3 "${here}/tools/make-sample-pdf.py" "${out}/data/sample.pdf"
echo "  sample.pdf: $(stat -c %s "${out}/data/sample.pdf") bytes"

# --- program: collect, strip, verify -----------------------------------------------------------
echo "== program"
bad=0
f="${P}/bin/atril"
[ -f "${f}" ] || { echo "build.sh: ${f} missing" >&2; exit 1; }
cp -a "${f}" "${out}/bin/atril"
"${TC}-strip" -o "${out}/bin/atril-stripped" "${out}/bin/atril"
und="$("${TC}-nm" -u "${out}/bin/atril" || true)"
n=$(grep -c . <<< "${und}" || true)
interp="$("${TC}-readelf" -l "${out}/bin/atril" | grep -c INTERP || true)"
allsyms="$("${TC}-nm" "${out}/bin/atril")"
x11="$(grep -cE ' (XOpenDisplay|XInternAtom|xcb_connect|gdk_x11_display_get_type|gdk_x11_window_set_user_time|SmcOpenConnection|IceOpenConnection|mate_image_menu_item_new)$' <<< "${allsyms}" || true)"
echo "  atril: nm -u ${n}, PT_INTERP ${interp}, X11/SM/mate-desktop symbols ${x11}; $("${TC}-size" "${out}/bin/atril" | awk 'NR==2 {printf "text %d data %d bss %d", $1, $2, $3}'); stripped $(stat -c %s "${out}/bin/atril-stripped")"
[ "${n}" = 0 ] && [ "${interp}" = 0 ] && [ "${x11}" = 0 ] || { sed 's/^/    /' <<< "${und}" | head -10; bad=1; }
# the PDF backend (built in), Poppler (glib + core + cairo output + JPX + CMS), GTK on Wayland
for s in ev_builtin_backends ev_builtin_pdfdocument_register ev_module_new_builtin \
		poppler_document_new_from_file poppler_page_render _ZN14CairoOutputDev9startPageEiP8GfxStateP4XRef \
		opj_decode cmsCreateTransform xmlXPathNewContext gdk_wayland_display_get_type gtk_image_menu_item_new_with_label \
		ev_view_presentation_new egg_sm_client_get ev_resource_data; do
	grep -qE " [TtWwVvDdBbRr] ${s}\$" <<< "${allsyms}" || { echo "    atril: symbol ${s} missing"; bad=1; }
done
strs="$(strings -a "${out}/bin/atril-stripped")"
for s in 'pdfdocument' 'application/pdf' 'PDF Documents' "${SCHEMAS_DIR}" 'org.mate.Atril' 'wayland-0'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  atril strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
n="$(grep -cE 'tools/gpu-lane|/home/' <<< "${strs}" || true)"
echo "  build-host path strings: ${n}"
{ echo "# atril-wayland build $(date -u +%Y-%m-%dT%H:%MZ)"
  sha256sum "${out}/bin/atril-stripped" "${out}/data/schemas/gschemas.compiled" "${out}/data/sample.pdf" | sed "s|${out}/||"; } \
	> "${out}/SHA256SUMS"
sed 's/^/  /' "${out}/SHA256SUMS"
[ "${bad}" = 0 ] || { echo "build.sh: verification failed" >&2; exit 1; }

# --- the staging tree: <out>/stage mirrors the NFS root (new names only) -----------------------
# Stage with: for each file in stage.MANIFEST, check nothing of that name exists on the export,
# then `sudo -n install -D` it (docs/gpu-new-lane/M7-wayland-desktop.md, "Atril").
echo "== staging tree"
ST="${out}/stage"
rm -rf "${ST}"
st() {  # mode source target-path
	install -D -m "$1" "$2" "${ST}/$3"
}
st 755 "${out}/bin/atril-stripped" bin/atril-wl
st 755 "${here}/pi/xfce-desktop-atril.sh" bin/xfce-desktop-atril.sh
st 644 "${here}/conf/atril.desktop" usr/share/applications/atril.desktop
st 644 "${out}/data/schemas/gschemas.compiled" "${SCHEMAS_DIR#/}/gschemas.compiled"
st 644 "${out}/data/sample.pdf" usr/share/doc/phoenix/sample.pdf
st 644 "${P}/share/atril/hand-open.png" usr/share/atril/hand-open.png
# Atril's own action icons (its private hicolor search path: ATRILDATADIR/icons) + the
# application icon, which the .desktop entry names by absolute path (the staged hicolor
# theme and its cache are the XFCE build's)
( cd "${P}/share/atril/icons" && find . -name '*.png' -printf '%P\n' ) | sort | while read -r f; do
	st 644 "${P}/share/atril/icons/${f}" "usr/share/atril/icons/${f}"
done
for sz in 16x16 22x22 24x24 48x48; do
	st 644 "${P}/share/icons/hicolor/${sz}/apps/atril.png" "usr/share/atril/icons/hicolor/${sz}/apps/atril.png"
done
( cd "${ST}" && find . -type f -printf '%P\n' | sort | xargs sha256sum ) > "${out}/stage.MANIFEST"
echo "  $(wc -l < "${out}/stage.MANIFEST") files ($(du -sh "${ST}" | cut -f1)); not icons:"
grep -v ' usr/share/atril/icons/' "${out}/stage.MANIFEST" | awk '{printf "    %s  %s\n", substr($1,1,16), $2}'
echo "done"
