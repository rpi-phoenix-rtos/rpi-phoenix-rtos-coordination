# _lzma module self-test (the `lzma` stdlib module). Run on the Pi:
#   python3 -S /selftest_lzma.py   # => LZMA-OK
import lzma

data = b"hello phoenix from xz/lzma " * 200

# default (XZ) one-shot roundtrip + it actually compresses.
c = lzma.compress(data)
assert lzma.decompress(c) == data, "xz one-shot roundtrip mismatch"
assert len(c) < len(data), "no compression happened"

# explicit container formats.
xz = lzma.compress(data, format=lzma.FORMAT_XZ)
assert lzma.decompress(xz) == data, "FORMAT_XZ roundtrip mismatch"
alone = lzma.compress(data, format=lzma.FORMAT_ALONE)
assert lzma.decompress(alone, format=lzma.FORMAT_ALONE) == data, "FORMAT_ALONE roundtrip mismatch"

# incremental compressor/decompressor.
comp = lzma.LZMACompressor()
blob = comp.compress(data) + comp.flush()
assert lzma.decompress(blob) == data, "incremental compress mismatch"

dec = lzma.LZMADecompressor()
assert dec.decompress(c) == data, "incremental decompress mismatch"

# LZMAFile over an in-memory buffer.
import io
buf = io.BytesIO()
with lzma.LZMAFile(buf, "wb") as f:
    f.write(data)
buf.seek(0)
with lzma.LZMAFile(buf, "rb") as f:
    assert f.read() == data, "LZMAFile roundtrip mismatch"

print("LZMA-OK compress %d->%d, xz/alone + incremental + LZMAFile roundtrips correct" % (len(data), len(c)))
