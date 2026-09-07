#!/bin/sh
# stkpcmproof.sh — host-side proof that STK's PC-alignment faults are written by
# MusicOggStream::streamIntoBuffer()'s 44100-byte STACK array (music_ogg.cpp:329),
# not by mesh index data.
#
# Decodes the staged menutheme.ogg to raw s16le PCM and locates, byte-exactly:
#   * the 128-byte stack window the kernel dumped from sp
#   * both observed corrupted saved-LR constants
# The two LR constants are exactly m_buffer_size (11025*4 = 44100) apart, i.e. the
# same intra-chunk offset in consecutive streamIntoBuffer() reads.
#
# Usage: tests/pi-repro/stkpcmproof.sh [path/to/menutheme.ogg]
set -eu

OGG="${1:-/srv/phoenix-rpi4-nfs-gcc16/usr/share/supertuxkart/stk-assets/music/menutheme.ogg}"
RAW="${TMPDIR:-/tmp}/menutheme-proof.raw"

echo "ogg: $OGG"
ffmpeg -v error -i "$OGG" -f s16le -acodec pcm_s16le "$RAW" -y
echo "pcm: $RAW ($(stat -c %s "$RAW") bytes)"

# 128-byte window printed by hal/aarch64/exceptions.c from sp (stkdump-T1/T2)
python3 - "$RAW" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
words = """0243014800eb0118 056d0563049f035d 0a4c089e075d0617 0ff60e780d0c0be4
12ce12961210111c 13261314138b1362 12eb127b12aa131c 187416e415b4145e
1b671b0d1a451970 1a981bdf1c4c1bd6 1c611b4e1b251a84 1f5e1fc41f301de7
187219f21c701e8b 1823164916a217a4 1aec1c3e1a971969 171f178218c81959""".split()
win = b''.join(struct.pack('<Q', int(w, 16)) for w in words)
lrA = bytes.fromhex('b2f105f31df4b5f4')   # lr = 0xf4b5f41df305f1b2
lrB = bytes.fromhex('c304c3026e047d04')   # lr = 0x047d046e02c304c3
w, a, b = d.find(win), d.find(lrA), d.find(lrB)
print("stack window (128 B) found at PCM offset :", w)
print("corrupt lr 0xf4b5f41df305f1b2 at offset  :", a, "(= window - %d)" % (w - a))
print("corrupt lr 0x047d046e02c304c3 at offset  :", b)
print("offset delta between the two lr values   :", b - a, "  m_buffer_size =", 11025 * 4)
ok = (w >= 0 and a >= 0 and b >= 0 and (b - a) == 11025 * 4)
print("VERDICT:", "PROVEN - the corrupting bytes are menutheme.ogg PCM" if ok else "NOT REPRODUCED")
sys.exit(0 if ok else 1)
PY
