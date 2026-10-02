/*
 * dl-hosttest: the libphoenix calls dl.c, Unity and the dlopen tests need, implemented
 * on Linux aarch64 system calls, so that libphoenix's dl.c runs under qemu-aarch64
 * (user mode) on the build host.
 *
 * Everything here is compiled against libphoenix's headers by the Phoenix toolchain:
 * the Phoenix constants (O_RDONLY, MAP_ANONYMOUS, MAP_FIXED) are translated to their
 * Linux values. The pure functions (string.h, setjmp) come from libphoenix.a itself;
 * this file is linked before it, so libphoenix's stdio, malloc and system calls are
 * never pulled in.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>


/* --- Linux aarch64 system calls --- */

#define LNX_openat     56
#define LNX_close      57
#define LNX_lseek      62
#define LNX_read       63
#define LNX_write      64
#define LNX_exit_group 94
#define LNX_munmap     215
#define LNX_mmap       222

#define LNX_AT_FDCWD   -100
#define LNX_O_RDONLY   0
#define LNX_MAP_PRIVATE   0x02
#define LNX_MAP_FIXED     0x10
#define LNX_MAP_ANONYMOUS 0x20


static long lnx_syscall6(long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;

	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
	return x0;
}


#define lnx_syscall(n, a, b, c) lnx_syscall6((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)


int open(const char *path, int oflag, ...)
{
	long r;

	if (oflag != O_RDONLY) {
		return -1; /* only what dl.c needs */
	}
	r = lnx_syscall6(LNX_openat, LNX_AT_FDCWD, (long)path, LNX_O_RDONLY, 0, 0, 0);
	return (r < 0) ? -1 : (int)r;
}


int close(int fd)
{
	return (lnx_syscall(LNX_close, fd, 0, 0) < 0) ? -1 : 0;
}


off_t lseek(int fd, off_t offset, int whence)
{
	long r = lnx_syscall(LNX_lseek, fd, offset, whence);

	return (r < 0) ? -1 : (off_t)r;
}


ssize_t read(int fd, void *buf, size_t nbyte)
{
	long r = lnx_syscall(LNX_read, fd, buf, nbyte);

	return (r < 0) ? -1 : (ssize_t)r;
}


ssize_t write(int fd, const void *buf, size_t nbyte)
{
	long r = lnx_syscall(LNX_write, fd, buf, nbyte);

	return (r < 0) ? -1 : (ssize_t)r;
}


void *mmap(void *vaddr, size_t size, int prot, int flags, int fd, off_t offs)
{
	long lflags = LNX_MAP_PRIVATE;
	long r;

	if ((flags & MAP_ANONYMOUS) != 0) {
		lflags |= LNX_MAP_ANONYMOUS;
	}
	if ((flags & MAP_FIXED) != 0) {
		lflags |= LNX_MAP_FIXED;
	}
	/* PROT_READ/WRITE/EXEC have the Linux values */
	r = lnx_syscall6(LNX_mmap, (long)vaddr, (long)size, prot & 0x7, lflags, fd, (long)offs);
	return ((r < 0) && (r > -4096)) ? MAP_FAILED : (void *)r;
}


int munmap(void *vaddr, size_t size)
{
	return (lnx_syscall(LNX_munmap, vaddr, size, 0) < 0) ? -1 : 0;
}


/* --- output: one buffer, flushed at a newline and at exit --- */

static char out_buf[4096];
static size_t out_len;
static int out_fd = 1;

FILE *stdout = (FILE *)1;
FILE *stderr = (FILE *)2;
FILE *stdin = (FILE *)0;


int fflush(FILE *stream)
{
	(void)stream;
	if (out_len != 0) {
		(void)write(out_fd, out_buf, out_len);
		out_len = 0;
	}
	return 0;
}


static void out_char(int fd, char c)
{
	if (fd != out_fd) {
		(void)fflush(NULL);
		out_fd = fd;
	}
	out_buf[out_len++] = c;
	if ((c == '\n') || (out_len == sizeof(out_buf))) {
		(void)fflush(NULL);
	}
}


int putchar(int c)
{
	out_char(1, (char)c);
	return c & 0xff;
}


/* --- formatting: %s %c %d %i %u %x %p %%, with l/ll/z and a zero-padded width --- */

static size_t fmt_number(char *tmp, unsigned long long v, unsigned int base)
{
	size_t n = 0;

	do {
		tmp[n++] = "0123456789abcdef"[v % base];
		v /= base;
	} while (v != 0);
	return n;
}


int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
	size_t pos = 0, n, width;
	const char *p, *s;
	char tmp[24], pad;
	int lng, neg;
	unsigned long long v;

#define EMIT(ch) \
	do { \
		if ((pos + 1) < size) { \
			str[pos] = (ch); \
		} \
		pos++; \
	} while (0)

	for (p = format; *p != '\0'; p++) {
		if (*p != '%') {
			EMIT(*p);
			continue;
		}
		p++;
		pad = ' ';
		if (*p == '0') {
			pad = '0';
			p++;
		}
		width = 0;
		while ((*p >= '0') && (*p <= '9')) {
			width = (width * 10) + (size_t)(*p++ - '0');
		}
		lng = 0;
		while ((*p == 'l') || (*p == 'z')) {
			lng = 1;
			p++;
		}
		neg = 0;
		switch (*p) {
			case 's':
				s = va_arg(ap, const char *);
				if (s == NULL) {
					s = "(null)";
				}
				for (n = strlen(s); n < width; n++) {
					EMIT(' ');
				}
				while (*s != '\0') {
					EMIT(*s++);
				}
				continue;
			case 'c':
				EMIT((char)va_arg(ap, int));
				continue;
			case '%':
				EMIT('%');
				continue;
			case 'p':
				v = (unsigned long long)(uintptr_t)va_arg(ap, void *);
				EMIT('0');
				EMIT('x');
				n = fmt_number(tmp, v, 16);
				break;
			case 'd':
			case 'i':
				if (lng != 0) {
					long l = va_arg(ap, long);
					neg = (l < 0) ? 1 : 0;
					v = (neg != 0) ? (0ULL - (unsigned long long)l) : (unsigned long long)l;
				}
				else {
					int i = va_arg(ap, int);
					neg = (i < 0) ? 1 : 0;
					v = (neg != 0) ? (0ULL - (unsigned long long)(long long)i) : (unsigned long long)i;
				}
				n = fmt_number(tmp, v, 10);
				break;
			case 'u':
			case 'x':
				v = (lng != 0) ? (unsigned long long)va_arg(ap, unsigned long) : (unsigned long long)va_arg(ap, unsigned int);
				n = fmt_number(tmp, v, (*p == 'x') ? 16U : 10U);
				break;
			default:
				EMIT('?');
				continue;
		}
		if (neg != 0) {
			EMIT('-');
		}
		while (width > n + (size_t)neg) {
			EMIT(pad);
			width--;
		}
		while (n > 0) {
			EMIT(tmp[--n]);
		}
	}
	if (size != 0) {
		str[(pos < size) ? pos : (size - 1)] = '\0';
	}
#undef EMIT
	return (int)pos;
}


int snprintf(char *str, size_t size, const char *format, ...)
{
	va_list ap;
	int r;

	va_start(ap, format);
	r = vsnprintf(str, size, format, ap);
	va_end(ap);
	return r;
}


int fprintf(FILE *stream, const char *format, ...)
{
	char buf[1024];
	va_list ap;
	int r, i;

	va_start(ap, format);
	r = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	for (i = 0; (i < r) && (buf[i] != '\0'); i++) {
		out_char((stream == stderr) ? 2 : 1, buf[i]);
	}
	return r;
}


int printf(const char *format, ...)
{
	char buf[1024];
	va_list ap;
	int r, i;

	va_start(ap, format);
	r = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	for (i = 0; (i < r) && (buf[i] != '\0'); i++) {
		out_char(1, buf[i]);
	}
	return r;
}


/* gcc turns fprintf(f, "%s", s) into fputs() and fprintf(f, "%c", c) into fputc() */
int fputs(const char *s, FILE *stream)
{
	while (*s != '\0') {
		out_char((stream == stderr) ? 2 : 1, *s++);
	}
	return 0;
}


int fputc(int c, FILE *stream)
{
	out_char((stream == stderr) ? 2 : 1, (char)c);
	return c & 0xff;
}


int puts(const char *s)
{
	while (*s != '\0') {
		out_char(1, *s++);
	}
	out_char(1, '\n');
	return 0;
}


/* --- memory: a bump allocator; free() only forgets --- */

static unsigned char heap[32 << 20] __attribute__((aligned(16)));
static size_t heap_used;


void *malloc(size_t size)
{
	size_t *blk;

	size = (size + sizeof(size_t) + 15U) & ~(size_t)15U;
	if ((heap_used + size) > sizeof(heap)) {
		return NULL;
	}
	blk = (size_t *)&heap[heap_used];
	heap_used += size;
	blk[0] = size - sizeof(size_t);
	return &blk[1];
}


void *calloc(size_t nmemb, size_t size)
{
	void *p = malloc(nmemb * size);

	if (p != NULL) {
		memset(p, 0, nmemb * size);
	}
	return p;
}


void *realloc(void *ptr, size_t size)
{
	void *p = malloc(size);
	size_t old;

	if ((p != NULL) && (ptr != NULL)) {
		old = ((size_t *)ptr)[-1];
		memcpy(p, ptr, (old < size) ? old : size);
	}
	return p;
}


void free(void *ptr)
{
	(void)ptr;
}


char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);

	if (d != NULL) {
		memcpy(d, s, n);
	}
	return d;
}


/* --- process --- */

char **environ;
const char *argv_progname; /* crt0-common.c's, read by dl.c */

static unsigned char tls_area[4096] __attribute__((aligned(64)));


char *getenv(const char *name)
{
	size_t n = strlen(name);
	char **e;

	for (e = environ; (e != NULL) && (*e != NULL); e++) {
		if ((strncmp(*e, name, n) == 0) && ((*e)[n] == '=')) {
			return *e + n + 1;
		}
	}
	return NULL;
}


void _exit(int status)
{
	for (;;) {
		(void)lnx_syscall(LNX_exit_group, status, 0, 0);
	}
}


void exit(int status)
{
	(void)fflush(NULL);
	_exit(status);
}


void abort(void)
{
	(void)fflush(NULL);
	_exit(134);
}


extern int main(int argc, char *argv[]);


/* called by _start with the initial stack pointer: argc, argv[], NULL, envp[], NULL */
__attribute__((used)) void shim_startc(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)&sp[1];

	/* libphoenix code may touch TLS (errno): give TPIDR_EL0 a zeroed block */
	__asm__ volatile("msr tpidr_el0, %0" : : "r"(tls_area));
	environ = &argv[argc + 1];
	argv_progname = argv[0];
	exit(main(argc, argv));
}


__asm__(
	".text\n"
	".global _start\n"
	".type _start, %function\n"
	"_start:\n"
	"	mov x29, #0\n"
	"	mov x30, #0\n"
	"	mov x0, sp\n"
	"	bl shim_startc\n"
	"	brk #0\n");
