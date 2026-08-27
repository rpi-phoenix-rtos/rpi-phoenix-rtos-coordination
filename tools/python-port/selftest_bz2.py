# _bz2 module self-test (the `bz2` stdlib module). Run on the Pi:
#   python3 -S /selftest_bz2.py   # => BZ2-OK
import bz2

data = b"hello phoenix from bzip2 " * 200

# one-shot compress/decompress roundtrip + it actually compresses.
c = bz2.compress(data, 9)
assert bz2.decompress(c) == data, "one-shot roundtrip mismatch"
assert len(c) < len(data), "no compression happened"

# incremental compressor/decompressor.
comp = bz2.BZ2Compressor(9)
blob = comp.compress(data) + comp.flush()
assert bz2.decompress(blob) == data, "incremental compress mismatch"

dec = bz2.BZ2Decompressor()
assert dec.decompress(c) == data, "incremental decompress mismatch"

# BZ2File over an in-memory buffer (exercises the file wrapper).
import io
buf = io.BytesIO()
with bz2.BZ2File(buf, "wb") as f:
    f.write(data)
buf.seek(0)
with bz2.BZ2File(buf, "rb") as f:
    assert f.read() == data, "BZ2File roundtrip mismatch"

print("BZ2-OK compress %d->%d, incremental + BZ2File roundtrips correct" % (len(data), len(c)))
