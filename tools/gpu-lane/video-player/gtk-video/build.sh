#!/usr/bin/env bash
#
# M10 (docs/gpu-new-lane/M10-video-player.md): gtk-video, our small GTK 3 video player, linked
# STATIC for aarch64-phoenix:
#   - GTK 3.24 Wayland-only + GLib + cairo/pango from tools/gpu-lane/gtk3-wayland's --usr out
#     dir (the stack Atril links; read-only: its pkg-config-phoenix, deps/sys, compat headers),
#     linked the way gtk3-wayland/build.sh links gtk3-hello (link_prog);
#   - the player build of the ffmpeg 6.1 port (libavformat/avcodec/swscale/swresample/avutil)
#     from ../build-out/ffmpeg-src -- run ../build-ffplay.sh first;
#   - ../ffplay_phoenix_glue.c: --wrap=pthread_create, 8 MiB default thread stacks (GLib's
#     g_thread_new and libavcodec's workers ask for no size).
# The picture is painted with cairo (no GL): no Mesa in this binary.
#
#   <out>/gtk-video            unstripped (addr2line); <out>/gtk-video-stripped = stage this
#   <out>/gtk-video.map, <out>/BUILD-INFO.txt, logs
#
# Usage: tools/gpu-lane/video-player/gtk-video/build.sh [--out <dir>] [--gtk-out <dir>]
# Stage (coordinator; a NEW path -- check it is absent first, then cmp):
#   install -m 755 <out>/gtk-video-stripped <live NFS export>/usr/bin/gtk-video
#
# Copyright 2026 Phoenix Systems
#
# This file is part of Phoenix-RTOS.
#
# %LICENSE%
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
vp="$(cd "${here}/.." && pwd)"
root="$(cd "${vp}/../../.." && pwd)"
out="${vp}/build-out/gtk-video"
gtk_out="${root}/tools/gpu-lane/gtk3-wayland/build-out-usr"
while [ $# -gt 0 ]; do
	case "$1" in
		--out) shift; out="${1:?}" ;;
		--gtk-out) shift; gtk_out="${1:?}" ;;
		-h|--help) sed -n '2,21p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
log() { printf '[gtk-video] %s\n' "$*"; }
die() { printf '[gtk-video] ERROR: %s\n' "$*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -c1-16; }

B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
GTKW="${root}/tools/gpu-lane/gtk3-wayland"
PHXCC="${GTKW}/bin/phx-gcc"
COMPAT_INC="${root}/tools/gpu-lane/weston-drm/compat/include"
SYSD="${gtk_out}/deps/sys"
PKGC="${gtk_out}/pkg-config-phoenix"
FS="${vp}/build-out/ffmpeg-src"
SRC="${here}/gtk-video.c"
GLUE="${vp}/ffplay_phoenix_glue.c"
FF_A=("${FS}/libavformat/libavformat.a" "${FS}/libavcodec/libavcodec.a" "${FS}/libswscale/libswscale.a"
	"${FS}/libswresample/libswresample.a" "${FS}/libavutil/libavutil.a")
for p in "${PHXCC}" "${PKGC}" "${SYSD}/lib" "${COMPAT_INC}" "${SRC}" "${GLUE}" "${TC}-strip" "${TC}-nm" "${FF_A[@]}"; do
	[ -e "${p}" ] || die "missing ${p}$(case "${p}" in "${FS}"*) echo " (run ${vp}/build-ffplay.sh first)" ;; esac)"
done
mkdir -p "${out}"
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

log "compile"
# shellcheck disable=SC2046  # pkg-config output is a word list by design
"${PHXCC}" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SYSD}/include" -I"${COMPAT_INC}" -I"${FS}" \
	$("${PKGC}" --cflags gtk+-3.0 gtk+-wayland-3.0) -c "${SRC}" -o "${out}/gtk-video.o"
"${TC}-gcc" -O2 -g -std=gnu17 -Wall -Wextra -Werror "${TFLAGS[@]}" -c "${GLUE}" -o "${out}/glue.o"
log "link"
BIN="${out}/gtk-video"
# shellcheck disable=SC2046
"${PHXCC}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,--wrap=pthread_create \
	-Wl,-Map,"${BIN}.map" -o "${BIN}" "${out}/gtk-video.o" "${out}/glue.o" -L"${SYSD}/lib" \
	-Wl,--start-group "${FF_A[@]}" $("${PKGC}" --libs gtk+-3.0 gtk+-wayland-3.0) -Wl,--end-group -lm \
	> "${out}/link.log" 2>&1 || { grep -v 'warning: .* is not fully supported' "${out}/link.log" | head -40; die "link failed"; }
nlw="$(grep -v -E 'warning: .*(is not fully supported|dlopen|getpwnam|getpwuid|getgrnam|initgroups)' "${out}/link.log" | grep -c 'warning' || true)"
log "  link warnings beyond libphoenix's notes: ${nlw} (${out}/link.log)"
"${TC}-strip" -o "${BIN}-stripped" "${BIN}"

log "verify"
bad=0
und="$("${TC}-nm" -u "${BIN}" || true)"
log "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && { sed 's/^/    /' <<< "${und}" | head -20; bad=1; }
syms="$("${TC}-nm" "${BIN}")"
for s in main gtk_init gtk_window_fullscreen gdk_wayland_display_get_type avformat_open_input ff_h264_decoder \
		ff_hevc_decoder ff_aac_decoder sws_scale_frame swr_convert __wrap_pthread_create __wrap_close; do
	if grep -qE " [TtWwDdRr] ${s}\$" <<< "${syms}"; then log "  symbol ${s}: yes"; else log "  symbol ${s}: NO"; bad=1; fi
done
strs="$(strings -a "${BIN}-stripped")"
for s in 'GTK-VIDEO stat t=' 'GTK_VIDEO_AUTOKEYS' '/dev/audio0' 'media-playback-start' '/org/gtk/libgtk/theme/Adwaita'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	log "  strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
# every thread creator through the glue (GLib, libavcodec)
dis="$("${TC}-objdump" -d --no-show-raw-insn "${BIN}")"
direct="$(awk '/^[0-9a-f]+ <[^>]*>:$/ { fn = $2 } /<pthread_create>$/ && /\tb/ { print fn }' <<< "${dis}" | sort -u | tr '\n' ' ')"
log "  direct callers of the real pthread_create: ${direct:-none} (expected: <__wrap_pthread_create>: only)"
[ "${direct}" = "<__wrap_pthread_create>: " ] || bad=1
{
	echo "built:              $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "gtk-video.c:        $(sha "${SRC}")"
	echo "GTK stack:          ${gtk_out} (libgtk-3.a $(sha "${gtk_out}/destdir/usr/lib/libgtk-3.a" 2>/dev/null || echo '?'))"
	echo "ffmpeg:             ${FS} (configure set $(cat "${vp}/build-out/ffmpeg-src.stamp"))"
	echo "gtk-video:          $(sha256sum "${BIN}" | cut -d' ' -f1) $(stat -c %s "${BIN}") bytes"
	echo "gtk-video-stripped: $(sha256sum "${BIN}-stripped" | cut -d' ' -f1) $(stat -c %s "${BIN}-stripped") bytes"
} > "${out}/BUILD-INFO.txt"
sed 's/^/  /' "${out}/BUILD-INFO.txt"
[ "${bad}" = 0 ] || die "verification failed (see above)"
log "done: stage ${BIN}-stripped as /usr/bin/gtk-video"
