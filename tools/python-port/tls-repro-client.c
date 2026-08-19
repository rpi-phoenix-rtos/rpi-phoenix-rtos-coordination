#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
int main(void)
{
	SSL_library_init();
	SSL_load_error_strings();
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(8443);
	a.sin_addr.s_addr = inet_addr("10.42.0.1");
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { printf("TCP-CONNECT-FAIL\n"); return 1; }
	printf("TCP-CONNECTED\n"); fflush(stdout);
	SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
	SSL *ssl = SSL_new(ctx);
	SSL_set_fd(ssl, fd);
	printf("SSL_connect...\n"); fflush(stdout);
	int r = SSL_connect(ssl);
	if (r != 1) { printf("SSL_CONNECT_FAIL err=%d\n", SSL_get_error(ssl, r)); ERR_print_errors_fp(stdout); return 2; }
	printf("TLS-VERSION %s CIPHER %s\n", SSL_get_version(ssl), SSL_get_cipher(ssl)); fflush(stdout);
	SSL_write(ssl, "GET / HTTP/1.0\r\n\r\n", 18);
	char buf[256];
	int n = SSL_read(ssl, buf, sizeof(buf) - 1);
	if (n > 0) { buf[n] = 0; printf("GOT-BODY %s\n", strstr(buf, "PHOENIX") ? "PHOENIX-TLS-HELLO" : "?"); }
	printf("TLS-C-OK\n");
	return 0;
}
