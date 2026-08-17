import zlib
data = b"hello phoenix " * 100
c = zlib.compress(data, 9)
assert len(c) < len(data), "compress did not shrink"
assert zlib.decompress(c) == data, "roundtrip mismatch"
assert zlib.crc32(b"123456789") == 0xCBF43926, "crc32 wrong"
assert zlib.adler32(b"123456789") == 0x091E01DE, "adler32 wrong"
co = zlib.compressobj(6)
stream = co.compress(data) + co.flush()
assert zlib.decompress(stream) == data, "streaming roundtrip mismatch"
print("ZVER", zlib.ZLIB_VERSION)
print("ZLIB-OK")
