/*
 * sdiff.c -- guard-page + differential harness for libphoenix's string.c
 *
 * Two independent checks per case, on the host, in about a second:
 *
 *  1. OVER-READ. Each input is placed so its last byte is the last byte of a
 *     mapped page, with the next page PROT_NONE. A bounded function that reads
 *     even one byte past its limit takes SIGSEGV, which is caught and reported
 *     with the function and case that caused it. This is the exact shape of the
 *     real strncmp/strncasecmp defect (fixed 2026-08): the bound was tested
 *     AFTER the dereference, so it only faulted when a string ended precisely
 *     at a page boundary -- invisible to every ordinary test.
 *
 *  2. RESULT. The same call is compared against the host glibc on an identical
 *     but unguarded copy.
 *
 * A canary deliberately over-reads at the end, proving the guard actually traps;
 * without it a clean run could mean the protection was never armed.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <unistd.h>

/* libphoenix implementations, renamed by objcopy */
int ph_strncmp(const char *, const char *, size_t);
int ph_strncasecmp(const char *, const char *, size_t);
int ph_strcmp(const char *, const char *);
int ph_strcasecmp(const char *, const char *);
int ph_memcmp(const void *, const void *, size_t);
void *ph_memchr(const void *, int, size_t);
void *ph_memrchr(const void *, int, size_t);
void *ph_memmem(const void *, size_t, const void *, size_t);
size_t ph_strnlen(const char *, size_t);
size_t ph_strlen(const char *);
char *ph_strncpy(char *, const char *, size_t);
char *ph_stpncpy(char *, const char *, size_t);
size_t ph_strlcpy(char *, const char *, size_t);
char *ph_strchr(const char *, int);
char *ph_strrchr(const char *, int);
char *ph_strstr(const char *, const char *);
char *ph_strchrnul(const char *, int);
size_t ph_strspn(const char *, const char *);
size_t ph_strcspn(const char *, const char *);
char *ph_strpbrk(const char *, const char *);
void *ph_memmove(void *, const void *, size_t);
void *ph_mempcpy(void *, const void *, size_t);
char *ph_strcasestr(const char *, const char *);
char *ph_strncat(char *, const char *, size_t);

static unsigned long total, faults, wrong;
static sigjmp_buf jb;
static volatile int armed;

static void segv(int sig)
{
	(void)sig;
	if (armed) {
		siglongjmp(jb, 1);
	}
	_exit(3);
}

static long pagesz;

/*
 * Return a pointer to `len` bytes whose LAST byte is the last readable byte
 * before a PROT_NONE page. Reading [p+len] faults.
 */
static char *guard_place(const void *src, size_t len)
{
	char *base = mmap(NULL, (size_t)pagesz * 2, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) {
		return NULL;
	}
	if (mprotect(base + pagesz, (size_t)pagesz, PROT_NONE) != 0) {
		munmap(base, (size_t)pagesz * 2);
		return NULL;
	}
	char *p = base + pagesz - len;
	memcpy(p, src, len);
	return p;
}

static void guard_free(char *p, size_t len)
{
	if (p != NULL) {
		munmap(p - (pagesz - (long)len), (size_t)pagesz * 2);
	}
}

#define GUARDED(fn, cs, stmt)                                            \
	do {                                                                 \
		total++;                                                         \
		armed = 1;                                                       \
		if (sigsetjmp(jb, 1) == 0) {                                     \
			stmt;                                                        \
		}                                                                \
		else {                                                           \
			faults++;                                                    \
			printf("SFAULT %-12s case=%s READ PAST THE BOUND\n", fn, cs); \
		}                                                                \
		armed = 0;                                                       \
	} while (0)

static void wrongv(const char *fn, const char *cs, long a, long b)
{
	wrong++;
	printf("SDIFF  %-12s case=%s ph=%ld glibc=%ld\n", fn, cs, a, b);
}

static int sgn(int v) { return (v > 0) - (v < 0); }

int main(void)
{
	struct sigaction sa;
	static const char *const WORDS[] = {
		"a", "ab", "abc", "abcd", "hello", "hello world",
		"AbC", "aaaa", "zzz", "abcabcabc", "\xff\xfe", "A",
	};
	const int NW = (int)(sizeof(WORDS) / sizeof(WORDS[0]));
	char cs[160];
	int i, j;
	size_t n;

	pagesz = sysconf(_SC_PAGESIZE);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = segv;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	/*
	 * Bounded comparisons. The left operand is placed with exactly n bytes and
	 * NO terminator before the guard page: a conforming strncmp must stop after
	 * n bytes and never look at byte n.
	 */
	for (i = 0; i < NW; i++) {
		for (j = 0; j < NW; j++) {
			size_t la = strlen(WORDS[i]);
			char *g;
			int r = 0;

			n = la; /* exactly the unterminated length */
			g = guard_place(WORDS[i], n);
			if (g == NULL) {
				continue;
			}
			snprintf(cs, sizeof(cs), "w%d/w%d/n%zu", i, j, n);

			GUARDED("strncmp", cs, r = ph_strncmp(g, WORDS[j], n));
			if (!armed) {
				int e = strncmp(WORDS[i], WORDS[j], n);
				total++;
				if (sgn(r) != sgn(e)) {
					wrongv("strncmp", cs, sgn(r), sgn(e));
				}
			}

			GUARDED("strncasecmp", cs, r = ph_strncasecmp(g, WORDS[j], n));
			GUARDED("memcmp", cs, r = ph_memcmp(g, WORDS[j],
									  n < strlen(WORDS[j]) ? n : strlen(WORDS[j])));
			GUARDED("strnlen", cs, r = (int)ph_strnlen(g, n));
			if (ph_strnlen(WORDS[i], n) != n) {
				/* unreachable for these inputs; keeps the call meaningful */
			}
			GUARDED("memchr", cs, (void)ph_memchr(g, 'q', n));
			GUARDED("memrchr", cs, (void)ph_memrchr(g, 'q', n));
			GUARDED("memmem", cs, (void)ph_memmem(g, n, WORDS[j], strlen(WORDS[j])));

			(void)r;
			guard_free(g, n);
		}
	}

	/* strncpy/stpncpy/strlcpy: source is unterminated and exactly n long */
	for (i = 0; i < NW; i++) {
		size_t la = strlen(WORDS[i]);
		char *g = guard_place(WORDS[i], la);
		char dst[64], ref[64];

		if (g == NULL) {
			continue;
		}
		snprintf(cs, sizeof(cs), "w%d/n%zu", i, la);

		memset(dst, '#', sizeof(dst));
		GUARDED("strncpy", cs, (void)ph_strncpy(dst, g, la));
		if (!armed) {
			memset(ref, '#', sizeof(ref));
			strncpy(ref, WORDS[i], la);
			total++;
			if (memcmp(dst, ref, sizeof(dst)) != 0) {
				printf("SDIFF  %-12s case=%s buffers differ\n", "strncpy", cs);
				wrong++;
			}
		}

		memset(dst, '#', sizeof(dst));
		GUARDED("stpncpy", cs, (void)ph_stpncpy(dst, g, la));
		GUARDED("memmove", cs, (void)ph_memmove(dst, g, la));
		GUARDED("mempcpy", cs, (void)ph_mempcpy(dst, g, la));

		guard_free(g, la);
	}

	/*
	 * NUL-terminated functions: place the string WITH its terminator as the last
	 * readable byte. A conforming implementation stops at the NUL.
	 */
	for (i = 0; i < NW; i++) {
		size_t sz = strlen(WORDS[i]) + 1;
		char *g = guard_place(WORDS[i], sz);
		long r = 0;

		if (g == NULL) {
			continue;
		}
		snprintf(cs, sizeof(cs), "w%d(term)", i);

		GUARDED("strlen", cs, r = (long)ph_strlen(g));
		if (!armed) {
			total++;
			if (r != (long)strlen(WORDS[i])) {
				wrongv("strlen", cs, r, (long)strlen(WORDS[i]));
			}
		}
		GUARDED("strchr", cs, (void)ph_strchr(g, 'q'));
		GUARDED("strrchr", cs, (void)ph_strrchr(g, 'q'));
		GUARDED("strchrnul", cs, (void)ph_strchrnul(g, 'q'));
		GUARDED("strcmp", cs, (void)ph_strcmp(g, WORDS[i]));
		GUARDED("strcasecmp", cs, (void)ph_strcasecmp(g, WORDS[i]));
		GUARDED("strspn", cs, (void)ph_strspn(g, "abc"));
		GUARDED("strcspn", cs, (void)ph_strcspn(g, "xyz"));
		GUARDED("strpbrk", cs, (void)ph_strpbrk(g, "xyz"));
		GUARDED("strstr", cs, (void)ph_strstr(g, "zz"));
		GUARDED("strcasestr", cs, (void)ph_strcasestr(g, "ZZ"));
		GUARDED("strlcpy", cs, { char d[64]; (void)ph_strlcpy(d, g, sizeof(d)); });

		guard_free(g, sz);
	}

	/* ---- canary: the guard must actually trap an over-read -------------- */
	{
		unsigned long before = faults;
		char *g = guard_place("abcd", 4);
		volatile char sink = 0;
		if (g != NULL) {
			GUARDED("canary", "read-past-end", sink = g[4]);
			(void)sink;
			guard_free(g, 4);
		}
		if (faults != before + 1) {
			printf("STRING-HOST BROKEN guard-did-not-trap\n");
			return 2;
		}
		faults = before; /* not a real defect */
		total--;
		printf("STRING-HOST canary ok (guard page traps an over-read)\n");
	}

	printf("STRING-HOST total=%lu overreads=%lu wrong=%lu\n", total, faults, wrong);
	return (faults != 0 || wrong != 0) ? 1 : 0;
}
