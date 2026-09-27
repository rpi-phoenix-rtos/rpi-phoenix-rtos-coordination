#!/usr/bin/env bash
#
# Host test of tools/gpu-lane/atril-wayland: the same sources, patches and Poppler options
# built NATIVELY (static libraries, on the host's GTK/cairo/glib/freetype/fontconfig), then
#
#   1  poppler's own tools (pdfinfo, pdftocairo from the native build: ENABLE_UTILS=ON is the
#      only difference from the Pi's option list, poppler-options.sh) on the sample PDF:
#      3 pages, the title, one PNG per page;
#   2  render_test.c through poppler-glib, as Atril's PDF backend calls it
#      (poppler_document_new_from_file + poppler_page_render onto cairo): page count, title,
#      page 1's text, and the colour at the points make-sample-pdf.py --points lists (the red
#      rectangle, the green circle, the blue triangle, two corners of the embedded image, a
#      shaded table cell);
#   3  Atril itself, patched as for the Pi (-Dbuiltin_backends=true -Dx11=disabled
#      -Dmate_desktop=disabled -Dschemas_dir): its atril-thumbnailer loads the sample through
#      the BUILT-IN backend table (patch 0004) -- no backends directory exists -- and writes a
#      PNG, whose red-rectangle pixel is checked; negative control: the same thumbnailer on a
#      text file must fail ("unsupported").
#
# Writes only into build-out/hosttest/. Proves the Poppler configuration, the sample document
# and the patches on a second toolchain; not Phoenix.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${here}/build-out/hosttest"
src="${here}/build-out/src"
dl="${here}/build-out/dl"
jobs="$(nproc)"
[ -d "${src}/atril" ] && [ -f "${here}/build-out/data/sample.pdf" ] || { echo "run.sh: run build.sh first (sources, sample.pdf)" >&2; exit 1; }
. "${here}/poppler-options.sh"
mkdir -p "${out}"
H="${out}/prefix"
export PATH="${here}/../xfce-wayland/bin:${PATH}"   # the msgfmt stand-in
# static libraries of this prefix: pkg-config must follow Requires.private
cat > "${out}/pkg-config-static" <<EOF
#!/bin/sh
exec /usr/bin/pkg-config --static "\$@"
EOF
chmod +x "${out}/pkg-config-static"
export PKG_CONFIG="${out}/pkg-config-static"
# Atril's meson asks for gail-3.0 but no Atril source includes it; this host's GTK 3 has no
# libgailutil-3: an empty stand-in .pc (the Pi build links the real one, from GTK 3.24.52)
mkdir -p "${out}/pc"
printf '%s\n' "Name: gail-3.0" "Description: host-test stand-in (no library; Atril uses none of it)" \
	"Version: $(pkg-config --modversion gtk+-3.0)" "Requires: gtk+-3.0" "Libs:" "Cflags:" > "${out}/pc/gail-3.0.pc"
export PKG_CONFIG_PATH="${H}/lib/pkgconfig:${H}/lib/x86_64-linux-gnu/pkgconfig:${out}/pc"

step() {  # name log command...
	local name="$1" log="$2"
	shift 2
	if ! "$@" >> "${log}" 2>&1; then
		echo "run.sh: ${name} failed; the end of ${log}:" >&2
		tail -30 "${log}" >&2
		exit 1
	fi
}

if [ ! -x "${H}/bin/pdftocairo" ]; then
	echo "== native build (openjpeg, lcms2, libxml2, Poppler + utils; static)"
	rm -rf "${out}"/*-build "${out}"/*.log
	step openjpeg "${out}/openjpeg.log" cmake -S "${src}/openjpeg" -B "${out}/openjpeg-build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX="${H}" -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CODEC=OFF \
		-DBUILD_TESTING=OFF -DBUILD_DOC=OFF -DBUILD_JPIP=OFF -DBUILD_THIRDPARTY=OFF -DBUILD_PKGCONFIG_FILES=ON
	step openjpeg "${out}/openjpeg.log" ninja -C "${out}/openjpeg-build" -j"${jobs}" install
	step lcms2 "${out}/lcms2.log" meson setup "${out}/lcms2-build" "${src}/lcms2" --prefix "${H}" --libdir lib --default-library static \
		-Dtests=disabled -Djpeg=disabled -Dtiff=disabled -Dutils=false -Dfastfloat=false -Dthreaded=false
	step lcms2 "${out}/lcms2.log" ninja -C "${out}/lcms2-build" install
	step libxml2 "${out}/libxml2.log" meson setup "${out}/libxml2-build" "${src}/libxml2" --prefix "${H}" --libdir lib \
		--default-library static -Dpython=disabled -Dzlib=disabled -Dicu=disabled -Diconv=disabled -Dhttp=disabled \
		-Dmodules=disabled -Dreadline=disabled -Dhistory=disabled -Ddocs=disabled -Dsax1=enabled -Dcatalog=disabled \
		-Ddebugging=disabled
	step libxml2 "${out}/libxml2.log" ninja -C "${out}/libxml2-build" install
	step poppler "${out}/poppler.log" cmake -S "${src}/poppler" -B "${out}/poppler-build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX="${H}" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="${H}" "${POPPLER_OPTS[@]}" -DENABLE_UTILS=ON \
		-DTESTDATADIR="${src}/poppler/test"
	step poppler "${out}/poppler.log" ninja -C "${out}/poppler-build" -j"${jobs}" install
fi
if [ ! -x "${H}/bin/atril-thumbnailer" ]; then
	echo "== native Atril (the Pi's patches and options; + atril-thumbnailer)"
	rm -rf "${out}/atril-build" "${out}/atril.log"
	step atril "${out}/atril.log" meson setup "${out}/atril-build" "${src}/atril" --prefix "${H}" --libdir lib \
		--default-library static -Dc_std=gnu11 \
		-Dpdf=enabled -Dps=disabled -Ddvi=disabled -Dt1lib=disabled -Ddjvu=disabled -Dtiff=disabled -Dpixbuf=disabled \
		-Dcomics=disabled -Dxps=disabled -Depub=disabled -Dcaja=disabled -Dx11=disabled -Dmate_desktop=disabled \
		-Dbuiltin_backends=true -Dschemas_dir="${H}/share/atril/schemas" -Dgtk_unix_print=false -Dkeyring=false \
		-Dpreviewer=false -Dthumbnailer=true -Ddocs=false -Dhelp_files=false -Dintrospection=false -Denable_dbus=false
	step atril "${out}/atril.log" ninja -C "${out}/atril-build" -j"${jobs}" install
fi
grep -E '^  (use|cairo output|glib wrapper)' "${out}/poppler.log" | sed 's/^/  poppler-host:/' || true

echo "== render_test (poppler-glib, as Atril's backend)"
gcc -O2 -g -Wall -Wextra -Werror -o "${out}/render_test" "${here}/hosttest/render_test.c" \
	$(pkg-config --static --cflags --libs poppler-glib cairo) -lstdc++

fail=0
check() {  # description command...
	local d="$1"
	shift
	if "$@"; then echo "  PASS ${d}"; else echo "  FAIL ${d}"; fail=1; fi
}
pdf="${here}/build-out/data/sample.pdf"
title='Atril on Phoenix-RTOS - sample document'

echo "== 1: poppler's tools"
info="$("${H}/bin/pdfinfo" "${pdf}")"
check "pdfinfo: Pages: 3" grep -qE '^Pages: +3$' <<< "${info}"
check "pdfinfo: the title" grep -qF "Title:           ${title}" <<< "${info}"
rm -f "${out}"/page-*.png
"${H}/bin/pdftocairo" -png -r 72 "${pdf}" "${out}/page"
check "pdftocairo: 3 PNGs" test "$(ls "${out}"/page-*.png | wc -l)" = 3

echo "== 2: poppler-glib"
mapfile -t points < <(python3 "${here}/tools/make-sample-pdf.py" --points)
if RENDER_TEST_PNG="${out}/render" "${out}/render_test" "${pdf}" 3 "${title}" "Atril on Phoenix-RTOS" "${points[@]}"; then
	echo "  PASS render_test"
else
	echo "  FAIL render_test"
	fail=1
fi

echo "== 3: Atril's thumbnailer through the built-in backend table (patch 0004)"
check "no backends directory (${H}/lib/atril)" test ! -e "${H}/lib/atril"
check "atril-thumbnailer: ev_builtin_pdfdocument_register linked in" \
	grep -q ' T ev_builtin_pdfdocument_register$' <<< "$(nm "${H}/bin/atril-thumbnailer")"
rm -f "${out}/thumb.png"
"${H}/bin/atril-thumbnailer" -s 400 "file://${pdf}" "${out}/thumb.png" > "${out}/thumb.log" 2>&1 || true
check "atril-thumbnailer: wrote thumb.png" test -s "${out}/thumb.png"
if [ -s "${out}/thumb.png" ]; then
	# page 1 scaled to 400 px WIDTH (atril-thumbnailer: size / page width); the red rectangle (110, 400)
	px="$(python3 - "${out}/thumb.png" <<'PY'
import sys, cairo
s = cairo.ImageSurface.create_from_png(sys.argv[1])
w, h = s.get_width(), s.get_height()
k = w / 595.0
d, st = s.get_data(), s.get_stride()
x, y = int(110 * k), int(400 * k)
o = y * st + x * 4
print('%dx%d %02x%02x%02x' % (w, h, d[o + 2], d[o + 1], d[o + 0]))
PY
)"
	echo "  thumb.png: ${px}"
	check "thumbnail: page 1's red rectangle (d02020 +-24)" python3 -c "
import sys; c = '${px##* }'
r, g, b = int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16)
sys.exit(0 if abs(r - 0xd0) <= 24 and abs(g - 0x20) <= 24 and abs(b - 0x20) <= 24 else 1)"
fi
echo 'not a document' > "${out}/not-a-pdf.txt"
rm -f "${out}/thumb-neg.png"
if "${H}/bin/atril-thumbnailer" -s 128 "file://${out}/not-a-pdf.txt" "${out}/thumb-neg.png" > "${out}/thumb-neg.log" 2>&1; then
	neg=0
else
	neg=1
fi
check "negative control: a text file is refused (no PNG)" test "${neg}" = 1 -a ! -e "${out}/thumb-neg.png"
[ "${fail}" = 0 ] && echo "ALL PASS" || { echo "FAILURES"; exit 1; }
