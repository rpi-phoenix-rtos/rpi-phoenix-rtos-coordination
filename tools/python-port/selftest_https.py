import socket, ssl
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
with socket.create_connection(('10.42.0.1', 8443), timeout=20) as s:
    with ctx.wrap_socket(s, server_hostname='phoenix-test') as ss:
        print("TLS", ss.version(), "CIPHER", ss.cipher()[0])
        ss.sendall(b'GET / HTTP/1.0\r\nHost: phoenix\r\n\r\n')
        data = b''
        while True:
            chunk = ss.recv(4096)
            if not chunk: break
            data += chunk
assert b'PHOENIX-TLS-HELLO' in data, "body missing"
print("HTTPS-OK")
