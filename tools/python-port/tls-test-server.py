import http.server, ssl, sys
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.send_header('Content-Type','text/plain'); self.end_headers()
        self.wfile.write(b'PHOENIX-TLS-HELLO')
    def log_message(self, *a): pass
httpd = http.server.HTTPServer(('0.0.0.0', 8443), H)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain('cert.pem', 'key.pem')
ctx.maximum_version = ssl.TLSVersion.TLSv1_2   # Phoenix openssl port has TLS1.3 disabled
httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
print("HTTPS server on 0.0.0.0:8443 (TLS1.2)", flush=True)
httpd.serve_forever()
