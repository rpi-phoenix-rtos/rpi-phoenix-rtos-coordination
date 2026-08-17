import ssl, hashlib
print("SSL", ssl.OPENSSL_VERSION)
ctx = ssl.create_default_context()
assert isinstance(ctx, ssl.SSLContext), "SSLContext creation failed"
assert ssl.HAS_TLSv1_2, "no TLS1.2"
# OpenSSL-backed hashlib (proves _hashlib links libcrypto)
assert hashlib.sha256(b"phoenix").hexdigest() == "03a8f0dd8edb33781a836ac497800b5f9c5c47c2ddbfd0f89581140589725a85", "sha256 mismatch"
assert "sha256" in hashlib.algorithms_available
print("SHA256", hashlib.sha256(b"phoenix").hexdigest()[:16])
print("SSL-OK")
