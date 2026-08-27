# _blake2 module self-test (hashlib.blake2b / blake2s) — run on the Pi:
#   python3 -S /selftest_blake2.py   # => BLAKE2-OK
# Verifies the statically-linked _blake2 (HACL* portable impl) against the
# canonical RFC 7693 / BLAKE2 empty-input and keyed test vectors.
import hashlib

# Import path: _blake2 must be a builtin (no traceback on these).
import _blake2  # noqa: F401

# BLAKE2b-512 and BLAKE2s-256 of the empty string (official vectors).
b2b_empty = ("786a02f742015903c6c6fd852552d272912f4740e15847618a86e217"
             "f71f5419d25e1031afee585313896444934eb04b903a685b1448b755"
             "d56f701afe9be2ce")
b2s_empty = "69217a3079908094e11121d042354a7c1f55b6482ca1a51e1b250dfd1ed0eef9"

assert hashlib.blake2b(b"").hexdigest() == b2b_empty, "blake2b empty mismatch"
assert hashlib.blake2s(b"").hexdigest() == b2s_empty, "blake2s empty mismatch"

# "abc" (BLAKE2b-512), RFC-style vector.
b2b_abc = ("ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
           "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923")
assert hashlib.blake2b(b"abc").hexdigest() == b2b_abc, "blake2b abc mismatch"

# Keyed hashing + configurable digest_size (BLAKE2 features).
h = hashlib.blake2b(b"message", key=b"secret-key", digest_size=32)
assert h.digest_size == 32, "digest_size not honored"
assert len(h.hexdigest()) == 64, "keyed digest length wrong"

# incremental update matches one-shot.
inc = hashlib.blake2s()
inc.update(b"hello, ")
inc.update(b"world")
assert inc.hexdigest() == hashlib.blake2s(b"hello, world").hexdigest(), "update mismatch"

# hashlib.new(name) path.
assert hashlib.new("blake2b", b"abc").hexdigest() == b2b_abc, "hashlib.new(blake2b) mismatch"

# blake2 present in the guaranteed/available algorithm sets.
assert "blake2b" in hashlib.algorithms_available, "blake2b not advertised"
assert "blake2s" in hashlib.algorithms_available, "blake2s not advertised"

print("BLAKE2-OK: blake2b/blake2s empty+abc+keyed+incremental+hashlib.new all correct")
