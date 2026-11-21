/*
 * wdiff.c -- differential harness for libphoenix's wchar implementation.
 *
 * Compiles libphoenix's multibyte layer (wchar.c, wcwidth.c, uchar.c and
 * setlocale() from locale.c) natively, renames its exported symbols to ph_*,
 * and compares every function against the host glibc on the same inputs, after
 * selecting the locale named on the command line on BOTH sides.
 *
 *   ./wdiff C        the C/POSIX locale: libphoenix is single-byte (bytes
 *                    0x80-0xff are U+0080-U+00FF), glibc is ASCII; non-ASCII
 *                    cases are counted as `known`, everything else gates
 *   ./wdiff C.UTF-8  UTF-8: everything gates, plus the exhaustive runs in
 *                    utf8_runs() (all code points, all 1-3 byte sequences,
 *                    4-byte sequences, byte-at-a-time decode, wcwidth)
 *
 * Every mismatch prints one tagged line. A deliberate canary proves the
 * comparison machinery can actually report a difference.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>
#include <limits.h>
#include <uchar.h>

/* Phoenix implementations (renamed by objcopy) */
size_t ph_wcslen(const wchar_t *);
int ph_wcscmp(const wchar_t *, const wchar_t *);
int ph_wcsncmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *ph_wcschr(const wchar_t *, wchar_t);
wchar_t *ph_wcsrchr(const wchar_t *, wchar_t);
wchar_t *ph_wcsstr(const wchar_t *, const wchar_t *);
size_t ph_wcsspn(const wchar_t *, const wchar_t *);
size_t ph_wcscspn(const wchar_t *, const wchar_t *);
wchar_t *ph_wcspbrk(const wchar_t *, const wchar_t *);
wchar_t *ph_wmemchr(const wchar_t *, wchar_t, size_t);
int ph_wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *ph_wmemset(wchar_t *, wchar_t, size_t);
wchar_t *ph_wcscpy(wchar_t *, const wchar_t *);
wchar_t *ph_wcsncpy(wchar_t *, const wchar_t *, size_t);
wchar_t *ph_wcscat(wchar_t *, const wchar_t *);
wchar_t *ph_wcstok(wchar_t *, const wchar_t *, wchar_t **);
int ph_wcwidth(wchar_t);
int ph_wcswidth(const wchar_t *, size_t);
int ph_wctob(wint_t);
int ph_mbtowc(wchar_t *, const char *, size_t);
int ph_mblen(const char *, size_t);
size_t ph_mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
size_t ph_wcrtomb(char *, wchar_t, mbstate_t *);
size_t ph_mbstowcs(wchar_t *, const char *, size_t);
size_t ph_wcstombs(char *, const wchar_t *, size_t);
size_t ph_mbrlen(const char *, size_t, mbstate_t *);
long ph_wcstol(const wchar_t *, wchar_t **, int);
size_t ph_mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
size_t ph_wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);
int ph_mbsinit(const mbstate_t *);
int ph_wctomb(char *, wchar_t);
wint_t ph_btowc(int);
size_t ph___ctype_get_mb_cur_max(void);
char *ph_setlocale(int, const char *);
size_t ph_mbrtoc16(char16_t *, const char *, size_t, mbstate_t *);
size_t ph_c16rtomb(char *, char16_t, mbstate_t *);
size_t ph_mbrtoc32(char32_t *, const char *, size_t, mbstate_t *);
size_t ph_c32rtomb(char *, char32_t, mbstate_t *);

static void utf8_runs(void);

static unsigned long total, diffs, known;

/* nonzero when the locale under test is the single-byte C/POSIX one */
static int single_byte;

/*
 * libphoenix's conversion layer is C/POSIX byte-identity by design, stated in a
 * comment above every function in wchar.c, and its wcwidth has no East Asian
 * width table. glibc's C locale is ASCII-only and rejects any byte >= 0x80.
 * Those two contracts disagree for NON-ASCII input and agree everywhere else,
 * so a case whose input is non-ASCII is marked with a leading '!' and counted
 * as a documented divergence rather than a defect (see P7 in
 * docs/KNOWN-ISSUES.md). Anything ASCII still gates normally -- classifying by
 * FUNCTION would have masked a real defect in these same functions.
 */
static int mb_nonascii(const char *s)
{
	size_t i;
	for (i = 0; s[i] != '\0'; i++) {
		if ((unsigned char)s[i] >= 0x80u) {
			return 1;
		}
	}
	return 0;
}

static int ws_nonascii(const wchar_t *w)
{
	size_t i;
	for (i = 0; w[i] != L'\0'; i++) {
		if ((unsigned long)w[i] > 0x7fUL) {
			return 1;
		}
	}
	return 0;
}

static int wc_nonascii(wchar_t c)
{
	return ((unsigned long)c > 0x7fUL) ? 1 : 0;
}

/* leading marker consumed by REPORT() above; only the C locale has the
 * documented charmap divergence -- under UTF-8 every case gates */
static const char *mk(int nonascii)
{
	return ((single_byte != 0) && (nonascii != 0)) ? "!" : "";
}

#define REPORT(fn, fmt, ...) \
	do { \
		if ((cs[0] == '!')) { \
			known++; \
		} \
		else { \
			diffs++; \
			printf("WDIFF %-10s " fmt "\n", fn, __VA_ARGS__); \
		} \
	} while (0)

static void cmp_long(const char *fn, const char *cs, long a, long b)
{
	total++;
	if (a != b) {
		REPORT(fn, "case=%s ph=%ld glibc=%ld", cs, a, b);
	}
}

/* compare pointer results as offsets into the base string (NULL -> -1) */
static void cmp_ptr(const char *fn, const char *cs, const wchar_t *base,
		const wchar_t *a, const wchar_t *b)
{
	long oa = (a == NULL) ? -1 : (long)(a - base);
	long ob = (b == NULL) ? -1 : (long)(b - base);
	total++;
	if (oa != ob) {
		REPORT(fn, "case=%s ph=%ld glibc=%ld", cs, oa, ob);
	}
}

static int sgn(int v) { return (v > 0) - (v < 0); }

/* ---- input corpus ---------------------------------------------------- */

static const wchar_t *const STRS[] = {
	L"", L"a", L"abc", L"aaa", L"abcabc", L"hello world",
	L"é", L"café", L"ééé",
	L"你好", L"你好你",
	L"\U0001F600", L"a\U0001F600b",
	L"\t\n ", L"aAbB", L"zzz\0hidden",
};
#define NSTR ((int)(sizeof(STRS) / sizeof(STRS[0])))

static const wchar_t *const SETS[] = {
	L"", L"a", L"abc", L"xyz", L"é", L"你", L" ", L"abé",
};
#define NSET ((int)(sizeof(SETS) / sizeof(SETS[0])))

static const wchar_t CHARS[] = {
	L'a', L'z', L'\0', L'é', L'你', 0x1F600, L' ', L'Q',
};
#define NCH ((int)(sizeof(CHARS) / sizeof(CHARS[0])))

int main(int argc, char **argv)
{
	char cs[128];
	int i, j, k;
	const char *loc = (argc > 1) ? argv[1] : "C.UTF-8";

	if (setlocale(LC_ALL, loc) == NULL) {
		printf("WCHAR-HOST SKIP locale=%s unavailable\n", loc);
		return 0;
	}
	if (ph_setlocale(LC_ALL, loc) == NULL) {
		printf("WCHAR-HOST FAIL libphoenix setlocale(LC_ALL, \"%s\") refused\n", loc);
		return 2;
	}
	single_byte = (ph___ctype_get_mb_cur_max() == 1u) ? 1 : 0;
	printf("WCHAR-HOST locale=%s glibc=%s libphoenix=%s MB_CUR_MAX ph=%zu glibc=%zu\n", loc,
			setlocale(LC_CTYPE, NULL), ph_setlocale(LC_CTYPE, NULL), ph___ctype_get_mb_cur_max(), MB_CUR_MAX);

	/* ---- canary: the machinery must be able to report a difference ---- */
	{
		unsigned long before = diffs;
		cmp_long("canary", "must-fire", 1, 2);
		if (diffs != before + 1) {
			printf("WCHAR-HOST BROKEN canary-did-not-fire\n");
			return 2;
		}
		diffs = before; /* not a real defect */
		total--;
		printf("WCHAR-HOST canary ok (comparison machinery reports diffs)\n");
	}

	/* ---- pure wide-string functions ---------------------------------- */
	for (i = 0; i < NSTR; i++) {
		const wchar_t *s = STRS[i];

		snprintf(cs, sizeof(cs), "%ss%d", mk(ws_nonascii(s)), i);
		cmp_long("wcslen", cs, (long)ph_wcslen(s), (long)wcslen(s));
		cmp_long("wcswidth", cs, ph_wcswidth(s, wcslen(s)), wcswidth(s, wcslen(s)));

		for (k = 0; k < NCH; k++) {
			snprintf(cs, sizeof(cs), "%ss%d/c%d", mk(ws_nonascii(s) || wc_nonascii(CHARS[k])), i, k);
			cmp_ptr("wcschr", cs, s, ph_wcschr(s, CHARS[k]), wcschr(s, CHARS[k]));
			cmp_ptr("wcsrchr", cs, s, ph_wcsrchr(s, CHARS[k]), wcsrchr(s, CHARS[k]));
			cmp_ptr("wmemchr", cs, s, ph_wmemchr(s, CHARS[k], wcslen(s)),
					wmemchr(s, CHARS[k], wcslen(s)));
		}

		for (j = 0; j < NSTR; j++) {
			const wchar_t *t = STRS[j];
			snprintf(cs, sizeof(cs), "%ss%d/s%d", mk(ws_nonascii(s) || ws_nonascii(t)), i, j);
			cmp_long("wcscmp", cs, sgn(ph_wcscmp(s, t)), sgn(wcscmp(s, t)));
			cmp_ptr("wcsstr", cs, s, ph_wcsstr(s, t), wcsstr(s, t));
			for (k = 0; k <= 4; k++) {
				char cs2[160];
				snprintf(cs2, sizeof(cs2), "%ss%d/s%d/n%d", mk(ws_nonascii(s) || ws_nonascii(t)), i, j, k);
				cmp_long("wcsncmp", cs2, sgn(ph_wcsncmp(s, t, (size_t)k)),
						sgn(wcsncmp(s, t, (size_t)k)));
				if ((size_t)k <= wcslen(s) && (size_t)k <= wcslen(t)) {
					cmp_long("wmemcmp", cs2, sgn(ph_wmemcmp(s, t, (size_t)k)),
							sgn(wmemcmp(s, t, (size_t)k)));
				}
			}
		}

		for (j = 0; j < NSET; j++) {
			const wchar_t *set = SETS[j];
			snprintf(cs, sizeof(cs), "%ss%d/set%d", mk(ws_nonascii(s) || ws_nonascii(set)), i, j);
			cmp_long("wcsspn", cs, (long)ph_wcsspn(s, set), (long)wcsspn(s, set));
			cmp_long("wcscspn", cs, (long)ph_wcscspn(s, set), (long)wcscspn(s, set));
			cmp_ptr("wcspbrk", cs, s, ph_wcspbrk(s, set), wcspbrk(s, set));
		}
	}

	for (k = 0; k < NCH; k++) {
		snprintf(cs, sizeof(cs), "%sc%d(U+%04X)", mk(wc_nonascii(CHARS[k])), k, (unsigned)CHARS[k]);
		cmp_long("wcwidth", cs, ph_wcwidth(CHARS[k]), wcwidth(CHARS[k]));
		cmp_long("wctob", cs, ph_wctob((wint_t)CHARS[k]), wctob((wint_t)CHARS[k]));
	}

	/* ---- copy family: compare resulting buffers ---------------------- */
	for (i = 0; i < NSTR; i++) {
		wchar_t a[64], b[64];
		size_t n;

		for (n = 0; n <= 8; n++) {
			size_t w;
			snprintf(cs, sizeof(cs), "%ss%d/n%zu", mk(ws_nonascii(STRS[i])), i, n);
			for (w = 0; w < 64; w++) {
				a[w] = b[w] = L'#';
			}
			ph_wcsncpy(a, STRS[i], n);
			wcsncpy(b, STRS[i], n);
			total++;
			if (memcmp(a, b, sizeof(a)) != 0) {
				REPORT("wcsncpy", "case=%s buffers differ", cs);
			}
		}

		if (wcslen(STRS[i]) < 30) {
			size_t w;
			for (w = 0; w < 64; w++) {
				a[w] = b[w] = L'#';
			}
			a[0] = b[0] = L'\0';
			ph_wcscpy(a, STRS[i]);
			wcscpy(b, STRS[i]);
			total++;
			if (memcmp(a, b, sizeof(a)) != 0) {
				snprintf(cs, sizeof(cs), "%ss%d", mk(ws_nonascii(STRS[i])), i);
				REPORT("wcscpy", "case=%s buffers differ", cs);
			}

			wcscpy(a, L"pre");
			wcscpy(b, L"pre");
			ph_wcscat(a, STRS[i]);
			wcscat(b, STRS[i]);
			total++;
			if (wcscmp(a, b) != 0) {
				snprintf(cs, sizeof(cs), "%ss%d", mk(ws_nonascii(STRS[i])), i);
				REPORT("wcscat", "case=%s result differs", cs);
			}
		}
	}

	/* ---- wmemset ----------------------------------------------------- */
	for (k = 0; k < NCH; k++) {
		wchar_t a[16], b[16];
		size_t n;
		for (n = 0; n <= 8; n++) {
			size_t w;
			for (w = 0; w < 16; w++) {
				a[w] = b[w] = L'#';
			}
			ph_wmemset(a, CHARS[k], n);
			wmemset(b, CHARS[k], n);
			total++;
			if (memcmp(a, b, sizeof(a)) != 0) {
				snprintf(cs, sizeof(cs), "%sc%d/n%zu", mk(wc_nonascii(CHARS[k])), k, n);
				REPORT("wmemset", "case=%s buffers differ", cs);
			}
		}
	}

	/* ---- multibyte conversions (C.UTF-8) ----------------------------- */
	{
		static const char *const MBS[] = {
			"", "a", "abc", "caf\xc3\xa9", "\xe4\xbd\xa0\xe5\xa5\xbd",
			"\xf0\x9f\x98\x80", "a\xf0\x9f\x98\x80""b",
			"\xff", "\xc3", "\xc3\x28", "\xe4\xbd", "\x80",
		};
		int nmb = (int)(sizeof(MBS) / sizeof(MBS[0]));

		for (i = 0; i < nmb; i++) {
			const char *s = MBS[i];
			size_t len = strlen(s);
			size_t n;

			for (n = 0; n <= len + 1; n++) {
				/* n == 0 lets no byte be examined at all. libphoenix reports -1
				 * ("cannot form a character"); glibc reports 0, which means "s
				 * points to a null character" -- a byte it never looked at. C17
				 * 7.22.7.2 does not settle it, so this is marked documented
				 * rather than changed. */
				const char *edge = (n == 0) ? "!" : "";
				wchar_t wa = 0, wb = 0;
				mbstate_t sa, sb;
				int ra, rb;
				size_t za, zb;

				snprintf(cs, sizeof(cs), "%s%smb%d/n%zu", edge, mk(mb_nonascii(s)), i, n);

				/* glibc's mbtowc() and mblen() keep a hidden state after an
				 * incomplete character even in UTF-8; reset it (C17 7.22.7) */
				(void)mbtowc(NULL, NULL, 0);
				ra = ph_mbtowc(&wa, s, n);
				rb = mbtowc(&wb, s, n);
				cmp_long("mbtowc", cs, ra, rb);
				if (ra > 0 && rb > 0) {
					cmp_long("mbtowc.wc", cs, (long)wa, (long)wb);
				}

				(void)mblen(NULL, 0);
				cmp_long("mblen", cs, ph_mblen(s, n), mblen(s, n));

				memset(&sa, 0, sizeof(sa));
				memset(&sb, 0, sizeof(sb));
				wa = wb = 0;
				za = ph_mbrtowc(&wa, s, n, &sa);
				zb = mbrtowc(&wb, s, n, &sb);
				cmp_long("mbrtowc", cs, (long)za, (long)zb);
				if (za <= n && zb <= n && za > 0 && zb > 0) {
					cmp_long("mbrtowc.wc", cs, (long)wa, (long)wb);
				}

				memset(&sa, 0, sizeof(sa));
				memset(&sb, 0, sizeof(sb));
				cmp_long("mbrlen", cs, (long)ph_mbrlen(s, n, &sa),
						(long)mbrlen(s, n, &sb));
			}

			{
				wchar_t wa[32], wb[32];
				size_t za, zb, w;
				for (w = 0; w < 32; w++) {
					wa[w] = wb[w] = L'#';
				}
				snprintf(cs, sizeof(cs), "%smb%d", mk(mb_nonascii(s)), i);
				za = ph_mbstowcs(wa, s, 32);
				zb = mbstowcs(wb, s, 32);
				cmp_long("mbstowcs", cs, (long)za, (long)zb);
				if (za == zb && za != (size_t)-1) {
					total++;
					if (memcmp(wa, wb, (za + 1) * sizeof(wchar_t)) != 0) {
						REPORT("mbstowcs", "case=%s output differs", cs);
					}
				}
			}
		}

		for (k = 0; k < NCH; k++) {
			char ba[MB_LEN_MAX + 8], bb[MB_LEN_MAX + 8];
			mbstate_t sa, sb;
			size_t za, zb;

			memset(ba, '#', sizeof(ba));
			memset(bb, '#', sizeof(bb));
			memset(&sa, 0, sizeof(sa));
			memset(&sb, 0, sizeof(sb));
			snprintf(cs, sizeof(cs), "%sc%d(U+%04X)", mk(wc_nonascii(CHARS[k])), k, (unsigned)CHARS[k]);
			za = ph_wcrtomb(ba, CHARS[k], &sa);
			zb = wcrtomb(bb, CHARS[k], &sb);
			cmp_long("wcrtomb", cs, (long)za, (long)zb);
			if (za == zb && za != (size_t)-1) {
				total++;
				if (memcmp(ba, bb, za) != 0) {
					REPORT("wcrtomb", "case=%s bytes differ", cs);
				}
			}
		}

		for (i = 0; i < NSTR; i++) {
			char ba[128], bb[128];
			size_t za, zb;
			memset(ba, '#', sizeof(ba));
			memset(bb, '#', sizeof(bb));
			snprintf(cs, sizeof(cs), "%ss%d", mk(ws_nonascii(STRS[i])), i);
			za = ph_wcstombs(ba, STRS[i], sizeof(ba));
			zb = wcstombs(bb, STRS[i], sizeof(bb));
			cmp_long("wcstombs", cs, (long)za, (long)zb);
			if (za == zb && za != (size_t)-1) {
				total++;
				if (memcmp(ba, bb, za) != 0) {
					REPORT("wcstombs", "case=%s bytes differ", cs);
				}
			}
		}
	}

	/* ---- wcstol ------------------------------------------------------ */
	{
		static const wchar_t *const NUMS[] = {
			L"0", L"42", L"-42", L"  12", L"0x1f", L"077", L"abc",
			L"2147483648", L"-2147483649", L"999999999999999999999",
			L"+7", L"12abc", L"", L"  ", L"-",
		};
		int nn = (int)(sizeof(NUMS) / sizeof(NUMS[0]));
		int bases[] = { 0, 10, 16, 8, 2 };
		int nb = (int)(sizeof(bases) / sizeof(bases[0]));

		for (i = 0; i < nn; i++) {
			for (j = 0; j < nb; j++) {
				wchar_t *ea = NULL, *eb = NULL;
				long ra, rb;
				snprintf(cs, sizeof(cs), "num%d/base%d", i, bases[j]);
				errno = 0;
				ra = ph_wcstol(NUMS[i], &ea, bases[j]);
				errno = 0;
				rb = wcstol(NUMS[i], &eb, bases[j]);
				cmp_long("wcstol", cs, ra, rb);
				cmp_ptr("wcstol.end", cs, NUMS[i], ea, eb);
			}
		}
	}

	if (single_byte == 0) {
		utf8_runs();
	}

	printf("WCHAR-HOST total=%lu diffs=%lu known=%lu\n", total, diffs, known);
	return (diffs != 0) ? 1 : 0;
}


/* ======================================================================
 * UTF-8 exhaustive runs (only when the locale under test is UTF-8)
 *
 * libphoenix is STRICT UTF-8 (RFC 3629); glibc is lax in two documented ways,
 * which are counted per class and do not gate:
 *
 *   lax    glibc still decodes and encodes the pre-2003 forms above U+10FFFF
 *          (F5-FD leads, 5- and 6-byte sequences, wcrtomb(0x110000) = 4 bytes);
 *          libphoenix rejects them with EILSEQ.
 *   early  for a prefix that can no longer become a valid character (E0 80,
 *          ED A0, F0 80, F4 90, a lone F5...) glibc answers (size_t)-2 and fails
 *          only on the last byte; libphoenix answers (size_t)-1 at the first
 *          byte that cannot continue it, as "potentially valid" in C17
 *          7.29.6.3.2 requires. Each such case is re-checked: completing the
 *          sequence with continuation bytes must make glibc reject it too (or
 *          land above U+10FFFF), otherwise it is a real difference.
 *
 * Everything else, including every complete sequence decoding to U+10FFFF or
 * below, must match exactly.
 * ==================================================================== */

enum { K_LAX = 0, K_EARLY, K_COUNT };
static const char *const kname[K_COUNT] = { "lax(glibc>U+10FFFF)", "early(glibc -2, rejects later)" };
static unsigned long kcount[K_COUNT];
static unsigned long printed;

#define MAXPRINT 60

static void udiff(const char *fn, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void udiff(const char *fn, const char *fmt, ...)
{
	va_list ap;

	diffs++;
	if (printed++ < MAXPRINT) {
		printf("WDIFF %-10s ", fn);
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		printf("\n");
	}
}

static void hex(char *out, const unsigned char *b, size_t n)
{
	size_t i;
	out[0] = '\0';
	for (i = 0; i < n; i++) {
		sprintf(out + strlen(out), "%s%02x", (i == 0) ? "" : " ", b[i]);
	}
}

/* --- reference classifier for a byte sequence (loose, pre-2003 lengths) --- */
enum { SC_VALID = 0, SC_TRUNC, SC_LONECONT, SC_BADLEAD, SC_BADCONT, SC_OVERLONG, SC_SURR, SC_ABOVE, SC_COUNT };
static const char *const scname[SC_COUNT] = {
	"valid", "truncated", "continuation-without-lead", "bad-lead(FE/FF)",
	"bad-continuation", "overlong", "surrogate", "above-U+10FFFF",
};
static unsigned long sc_tested[SC_COUNT], sc_same[SC_COUNT], sc_known[SC_COUNT];

static int classify(const unsigned char *b, size_t n)
{
	static const unsigned long minv[7] = { 0, 0, 0x80, 0x800, 0x10000, 0x200000, 0x4000000 };
	unsigned char b0 = b[0];
	size_t need, i;
	unsigned long v;

	if (b0 < 0x80) {
		return SC_VALID;
	}
	if (b0 < 0xc0) {
		return SC_LONECONT;
	}
	need = (b0 < 0xe0) ? 2 : (b0 < 0xf0) ? 3 : (b0 < 0xf8) ? 4 : (b0 < 0xfc) ? 5 : (b0 < 0xfe) ? 6 : 0;
	if (need == 0) {
		return SC_BADLEAD;
	}
	v = b0 & (0x7fu >> need);
	for (i = 1; i < need; i++) {
		if (i >= n) {
			return SC_TRUNC;
		}
		if ((b[i] & 0xc0) != 0x80) {
			return SC_BADCONT;
		}
		v = (v << 6) | (b[i] & 0x3fu);
	}
	if (v < minv[need]) {
		return SC_OVERLONG;
	}
	if ((v >= 0xd800) && (v <= 0xdfff)) {
		return SC_SURR;
	}
	if (v > 0x10ffff) {
		return SC_ABOVE;
	}
	return SC_VALID;
}

/* glibc's verdict once the sequence b[0..n) is completed with 0x80 bytes:
 * nonzero if it rejects it or decodes above U+10FFFF (i.e. libphoenix was
 * right to reject the prefix early) */
static int glibc_rejects_completion(const unsigned char *b, size_t n)
{
	unsigned char c[8];
	mbstate_t st;
	wchar_t wc = 0;
	size_t r, k;

	memcpy(c, b, n);
	for (k = n; k < sizeof(c); k++) {
		c[k] = 0x80;
	}
	memset(&st, 0, sizeof(st));
	r = mbrtowc(&wc, (const char *)c, sizeof(c), &st);
	if (r == (size_t)-1) {
		return 1;
	}
	return ((r != (size_t)-2) && ((unsigned long)wc > 0x10ffffUL)) ? 1 : 0;
}

/* Classifies a disagreement ph=ra vs glibc=rb on prefix b[0..n): returns the
 * known class, or -1 for a real difference */
static int known_class(const unsigned char *b, size_t n, size_t ra, size_t rb, wchar_t wb)
{
	if (ra != (size_t)-1) {
		return -1;
	}
	if ((rb != (size_t)-1) && (rb != (size_t)-2) && ((unsigned long)wb > 0x10ffffUL)) {
		return K_LAX;
	}
	if ((rb == (size_t)-2) && (glibc_rejects_completion(b, n) != 0)) {
		return K_EARLY;
	}
	return -1;
}

/* whether glibc takes b[0..n) as a potentially valid prefix that libphoenix
 * rejects early, i.e. the K_EARLY class */
static int early_prefix(const unsigned char *b, size_t n)
{
	mbstate_t sa, sb;
	size_t ra, rb;
	wchar_t wb = 0;

	memset(&sa, 0, sizeof(sa));
	memset(&sb, 0, sizeof(sb));
	ra = ph_mbrtowc(NULL, (const char *)b, n, &sa);
	rb = mbrtowc(&wb, (const char *)b, n, &sb);

	return (known_class(b, n, ra, rb, wb) == K_EARLY) ? 1 : 0;
}

/* whole sequence: mbrtowc, mbrlen, mbtowc, mblen with n = all bytes */
static void cmp_seq(const unsigned char *b, size_t n)
{
	const char *s = (const char *)b;
	mbstate_t sa, sb;
	wchar_t wa = 0, wb = 0;
	size_t ra, rb;
	int ea, eb, ia, ib, cls, kc;
	char hx[32];

	cls = classify(b, n);
	sc_tested[cls]++;
	total++;

	memset(&sa, 0, sizeof(sa));
	memset(&sb, 0, sizeof(sb));
	errno = 0;
	ra = ph_mbrtowc(&wa, s, n, &sa);
	ea = errno;
	errno = 0;
	rb = mbrtowc(&wb, s, n, &sb);
	eb = errno;

	if ((ra == rb) && ((ra > n) || (wa == wb)) && ((ra != (size_t)-1) || (ea == eb)) &&
			((ra != (size_t)-2) || (ph_mbsinit(&sa) == mbsinit(&sb)))) {
		sc_same[cls]++;
	}
	else if ((kc = known_class(b, n, ra, rb, wb)) >= 0) {
		kcount[kc]++;
		sc_known[cls]++;
		known++;
		return; /* the derived functions disagree the same way */
	}
	else {
		hex(hx, b, n);
		udiff("mbrtowc", "seq=[%s] class=%s ph=%ld/U+%04lX errno=%d glibc=%ld/U+%04lX errno=%d", hx, scname[cls],
				(long)ra, (unsigned long)wa, ea, (long)rb, (unsigned long)wb, eb);
		return;
	}

	/* the derived single-call functions must agree whenever mbrtowc does */
	memset(&sa, 0, sizeof(sa));
	memset(&sb, 0, sizeof(sb));
	total += 3;
	if (ph_mbrlen(s, n, &sa) != mbrlen(s, n, &sb)) {
		hex(hx, b, n);
		udiff("mbrlen", "seq=[%s]", hx);
	}
	/* glibc's mbtowc() and mblen() keep a hidden state after an incomplete
	 * character even in UTF-8; reset it (C17 7.22.7) so each call stands alone */
	wa = wb = 0;
	(void)mbtowc(NULL, NULL, 0);
	ia = ph_mbtowc(&wa, s, n);
	ib = mbtowc(&wb, s, n);
	if ((ia != ib) || ((ia > 0) && (wa != wb))) {
		hex(hx, b, n);
		udiff("mbtowc", "seq=[%s] ph=%d glibc=%d", hx, ia, ib);
	}
	(void)mblen(NULL, 0);
	if (ph_mblen(s, n) != mblen(s, n)) {
		hex(hx, b, n);
		udiff("mblen", "seq=[%s]", hx);
	}
}

/* the same bytes fed one at a time through one mbstate_t */
static void cmp_incremental(const unsigned char *b, size_t n)
{
	mbstate_t sa, sb;
	wchar_t wa = 0, wb = 0;
	size_t i, ra, rb;
	char hx[32];
	int kc;

	memset(&sa, 0, sizeof(sa));
	memset(&sb, 0, sizeof(sb));
	total++;
	for (i = 0; i < n; i++) {
		ra = ph_mbrtowc(&wa, (const char *)&b[i], 1, &sa);
		rb = mbrtowc(&wb, (const char *)&b[i], 1, &sb);
		if (ra != rb) {
			kc = known_class(b, i + 1, ra, rb, wb);
			if (kc >= 0) {
				kcount[kc]++;
				known++;
			}
			else {
				hex(hx, b, n);
				udiff("mbrtowc.inc", "seq=[%s] byte=%zu ph=%ld glibc=%ld", hx, i, (long)ra, (long)rb);
			}
			return;
		}
		if (ra == (size_t)-2) {
			if (ph_mbsinit(&sa) != mbsinit(&sb)) {
				hex(hx, b, n);
				udiff("mbsinit", "seq=[%s] byte=%zu", hx, i);
				return;
			}
			continue;
		}
		if ((ra != (size_t)-1) && ((wa != wb) || (ph_mbsinit(&sa) == 0))) {
			hex(hx, b, n);
			udiff("mbrtowc.inc", "seq=[%s] wc ph=U+%04lX glibc=U+%04lX", hx, (unsigned long)wa, (unsigned long)wb);
		}
		return;
	}
}

/* all code points (and a few beyond): encoders, round trip, char16/32, width */
static void codepoint_run(void)
{
	static const long extra[] = { 0x110000, 0x1fffff, 0x200000, 0x7fffffff, -1, -5 };
	unsigned long wwidth[3][4] = { { 0 } }; /* [ph+1][glibc+1], ph -1..1 */
	unsigned long wdiffs[4][4] = { { 0 } };
	unsigned long nx = sizeof(extra) / sizeof(extra[0]);
	unsigned long i, cp;
	int w, x;

	for (i = 0; i < 0x110000UL + nx; i++) {
		char ba[16], bb[16];
		mbstate_t sa, sb;
		size_t za, zb;
		int ka, kb;
		wchar_t wc;

		cp = (i < 0x110000UL) ? i : (unsigned long)extra[i - 0x110000UL];
		wc = (wchar_t)cp;

		/* wcrtomb */
		memset(ba, '#', sizeof(ba));
		memset(bb, '#', sizeof(bb));
		memset(&sa, 0, sizeof(sa));
		memset(&sb, 0, sizeof(sb));
		errno = 0;
		za = ph_wcrtomb(ba, wc, &sa);
		ka = errno;
		errno = 0;
		zb = wcrtomb(bb, wc, &sb);
		kb = errno;
		total++;
		if ((za == (size_t)-1) && (zb != (size_t)-1) && ((unsigned long)wc > 0x10ffffUL)) {
			kcount[K_LAX]++;
			known++;
		}
		else if ((za != zb) || ((za == (size_t)-1) && (ka != kb)) || ((za != (size_t)-1) && (memcmp(ba, bb, za) != 0))) {
			udiff("wcrtomb", "U+%04lX ph=%ld glibc=%ld", cp, (long)za, (long)zb);
		}

		/* wctomb and c32rtomb agree with wcrtomb */
		total += 2;
		ka = ph_wctomb(bb, wc);
		if ((za == (size_t)-1) ? (ka != -1) : ((size_t)ka != za || memcmp(ba, bb, za) != 0)) {
			udiff("wctomb", "U+%04lX ph=%d wcrtomb=%ld", cp, ka, (long)za);
		}
		memset(&sa, 0, sizeof(sa));
		zb = ph_c32rtomb(bb, (char32_t)wc, &sa);
		if ((zb != za) || ((za != (size_t)-1) && (memcmp(ba, bb, za) != 0))) {
			udiff("c32rtomb", "U+%04lX ph=%ld wcrtomb=%ld", cp, (long)zb, (long)za);
		}

		if ((za != (size_t)-1) && (i < 0x110000UL)) {
			wchar_t wa = 0;
			char32_t c32 = 0;
			char16_t u[2] = { 0, 0 };
			size_t r1, r2;

			/* round trip through libphoenix's own decoder, all at once and
			 * byte at a time (against glibc) */
			memset(&sa, 0, sizeof(sa));
			total++;
			if ((ph_mbrtowc(&wa, ba, za, &sa) != ((cp == 0) ? 0 : za)) || ((unsigned long)wa != cp)) {
				udiff("roundtrip", "U+%04lX", cp);
			}
			cmp_seq((const unsigned char *)ba, za);
			cmp_incremental((const unsigned char *)ba, za);

			memset(&sa, 0, sizeof(sa));
			total++;
			if ((ph_mbrtoc32(&c32, ba, za, &sa) != ((cp == 0) ? 0 : za)) || (c32 != cp)) {
				udiff("mbrtoc32", "U+%04lX", cp);
			}

			/* char16_t both ways, against glibc */
			{
				char16_t ga = 0, gb = 0;
				size_t q1, q2;

				memset(&sa, 0, sizeof(sa));
				memset(&sb, 0, sizeof(sb));
				r1 = ph_mbrtoc16(&u[0], ba, za, &sa);
				q1 = mbrtoc16(&ga, ba, za, &sb);
				r2 = (r1 == (size_t)-1) ? r1 : ph_mbrtoc16(&u[1], "", 1, &sa);
				q2 = (q1 == (size_t)-1) ? q1 : mbrtoc16(&gb, "", 1, &sb);
				total++;
				if ((r1 != q1) || (u[0] != ga) || (r2 != q2) || ((r2 == (size_t)-3) && (u[1] != gb))) {
					udiff("mbrtoc16", "U+%04lX ph=%ld,%ld [%04x %04x] glibc=%ld,%ld [%04x %04x]", cp,
							(long)r1, (long)r2, u[0], u[1], (long)q1, (long)q2, ga, gb);
				}

				memset(&sa, 0, sizeof(sa));
				memset(&sb, 0, sizeof(sb));
				memset(ba, '#', sizeof(ba));
				memset(bb, '#', sizeof(bb));
				if (cp > 0xffffUL) {
					q1 = ph_c16rtomb(ba, (char16_t)(0xd800 | ((cp - 0x10000) >> 10)), &sa);
					r1 = c16rtomb(bb, (char16_t)(0xd800 | ((cp - 0x10000) >> 10)), &sb);
					q2 = ph_c16rtomb(ba, (char16_t)(0xdc00 | (cp & 0x3ff)), &sa);
					r2 = c16rtomb(bb, (char16_t)(0xdc00 | (cp & 0x3ff)), &sb);
				}
				else {
					q1 = r1 = 0;
					q2 = ph_c16rtomb(ba, (char16_t)cp, &sa);
					r2 = c16rtomb(bb, (char16_t)cp, &sb);
				}
				total++;
				if ((q1 != r1) || (q2 != r2) || ((q2 != (size_t)-1) && (memcmp(ba, bb, q2) != 0))) {
					udiff("c16rtomb", "U+%04lX ph=%ld,%ld glibc=%ld,%ld", cp, (long)q1, (long)q2, (long)r1, (long)r2);
				}
			}
		}

		/* wcwidth */
		total++;
		w = ph_wcwidth(wc);
		x = wcwidth(wc);
		if ((w >= -1) && (w <= 1) && (x >= -1) && (x <= 2)) {
			wwidth[w + 1][x + 1]++;
		}
		if (w != x) {
			if ((w >= -1) && (w <= 2) && (x >= -1) && (x <= 2)) {
				wdiffs[w + 1][x + 1]++;
			}
			udiff("wcwidth", "U+%04lX ph=%d glibc=%d", cp, w, x);
		}
	}

	printf("WCHAR-HOST wcwidth over U+0000..U+10FFFF (+%lu outside): differences by (ph, glibc) width:", nx);
	x = 0;
	for (w = 0; w < 4; w++) {
		int g;
		for (g = 0; g < 4; g++) {
			if (wdiffs[w][g] != 0) {
				printf(" (%d,%d)=%lu", w - 1, g - 1, wdiffs[w][g]);
				x = 1;
			}
		}
	}
	printf("%s\n", (x == 0) ? " none" : "");
}

/* every 1-byte, 2-byte and 3-byte sequence (leads C0-FF for 3), and 4-byte
 * sequences over all first two bytes with the last two from a boundary set */
static void sequence_run(void)
{
	static const unsigned char edge[] = { 0x00, 0x41, 0x7f, 0x80, 0x8f, 0x90, 0x9f, 0xa0, 0xbf, 0xc0, 0xff };
	const size_t ne = sizeof(edge);
	unsigned char b[4];
	unsigned int b0, b1, b2;
	size_t i, j, c;

	for (b0 = 0; b0 < 256; b0++) {
		b[0] = (unsigned char)b0;
		cmp_seq(b, 1);
		cmp_incremental(b, 1);
		for (b1 = 0; b1 < 256; b1++) {
			b[1] = (unsigned char)b1;
			cmp_seq(b, 2);
			cmp_incremental(b, 2);
			if (b0 < 0xc0) {
				continue; /* decided by the first byte */
			}
			for (b2 = 0; b2 < 256; b2++) {
				b[2] = (unsigned char)b2;
				cmp_seq(b, 3);
				cmp_incremental(b, 3);
			}
			for (i = 0; i < ne; i++) {
				for (j = 0; j < ne; j++) {
					b[2] = edge[i];
					b[3] = edge[j];
					cmp_seq(b, 4);
					cmp_incremental(b, 4);
				}
			}
		}
	}

	printf("WCHAR-HOST sequence classes (whole-sequence mbrtowc): tested / identical / by-design\n");
	for (c = 0; c < SC_COUNT; c++) {
		printf("WCHAR-HOST   %-26s %9lu %9lu %9lu\n", scname[c], sc_tested[c], sc_same[c], sc_known[c]);
	}
}

/* string conversions, small inputs, every length limit */
static void string_run(void)
{
	static const char *const MBS[] = {
		"", "abc", "caf\xc3\xa9", "\xe4\xbd\xa0\xe5\xa5\xbd", "a\xf0\x9f\x98\x80z",
		"ab\xe4\xbd\xa0\xffxy", "ab\xe4", "\xed\xa0\x80x", "x\xc0\x80", "\xf4\x90\x80\x80", "\x80",
		"\xc3\xa9\xe2\x82\xac\xf0\x90\x8d\x88",
	};
	static const wchar_t W1[] = { L'a', 0xe9, 0x4f60, 0x1f600, 0 };
	static const wchar_t W2[] = { L'a', 0xd800, L'b', 0 };
	static const wchar_t W3[] = { 0x10ffff, L'z', 0 };
	static const wchar_t W4[] = { L'q', 0x110000, 0 };
	static const wchar_t *const WS[] = { L"", L"plain", W1, W2, W3, W4 };
	size_t i, len;

	for (i = 0; i < sizeof(MBS) / sizeof(MBS[0]); i++) {
		for (len = 0; len <= 9; len++) {
			wchar_t wa[16], wb[16];
			const char *pa = MBS[i], *pb = MBS[i];
			mbstate_t sa, sb;
			size_t za, zb;
			int ea, eb;

			wmemset(wa, L'#', 16);
			wmemset(wb, L'#', 16);
			memset(&sa, 0, sizeof(sa));
			memset(&sb, 0, sizeof(sb));
			errno = 0;
			za = ph_mbsrtowcs((len == 9) ? NULL : wa, &pa, len, &sa);
			ea = errno;
			errno = 0;
			zb = mbsrtowcs((len == 9) ? NULL : wb, &pb, len, &sb);
			eb = errno;
			total++;
			if ((za != zb) || (ea != eb) || (pa != pb) ||
					((len != 9) && (za != (size_t)-1) && (wmemcmp(wa, wb, 16) != 0))) {
				if ((za == (size_t)-1) && (zb != (size_t)-1) && (strstr(MBS[i], "\xf4\x90") != NULL)) {
					kcount[K_LAX]++;
					known++;
				}
				else if ((za == (size_t)-1) && (zb == za) && (ea == eb) && (pa != NULL) && (pb != NULL) &&
						(pa < pb) && (early_prefix((const unsigned char *)pa, (size_t)(pb - pa)) != 0)) {
					/* C17 7.29.6.4.1: *src is "just past the last character
					 * converted"; glibc points into the sequence it took bytes of
					 * before its late rejection */
					kcount[K_EARLY]++;
					known++;
				}
				else {
					udiff("mbsrtowcs", "s%zu len=%zu ph=%ld/off%ld glibc=%ld/off%ld", i, len, (long)za,
							(pa == NULL) ? -1L : (long)(pa - MBS[i]), (long)zb, (pb == NULL) ? -1L : (long)(pb - MBS[i]));
				}
			}
		}
		{
			wchar_t wa[16], wb[16];
			size_t za, zb;

			wmemset(wa, L'#', 16);
			wmemset(wb, L'#', 16);
			za = ph_mbstowcs(wa, MBS[i], 16);
			zb = mbstowcs(wb, MBS[i], 16);
			total++;
			if (((za != zb) || ((za != (size_t)-1) && (wmemcmp(wa, wb, 16) != 0))) &&
					!((za == (size_t)-1) && (strstr(MBS[i], "\xf4\x90") != NULL))) {
				udiff("mbstowcs", "s%zu ph=%ld glibc=%ld", i, (long)za, (long)zb);
			}
		}
	}

	/* mbsrtowcs resuming a sequence that mbrtowc left pending */
	{
		const char *pa = "\xbd\xa0z", *pb = pa, *base = pa;
		wchar_t wa[4], wb[4];
		mbstate_t sa, sb;
		size_t za, zb;

		memset(&sa, 0, sizeof(sa));
		memset(&sb, 0, sizeof(sb));
		(void)ph_mbrtowc(NULL, "\xe4", 1, &sa);
		(void)mbrtowc(NULL, "\xe4", 1, &sb);
		za = ph_mbsrtowcs(wa, &pa, 4, &sa);
		zb = mbsrtowcs(wb, &pb, 4, &sb);
		total++;
		if ((za != zb) || (pa != pb) || (wmemcmp(wa, wb, (za == (size_t)-1) ? 0 : za + 1) != 0)) {
			udiff("mbsrtowcs", "resume ph=%ld glibc=%ld", (long)za, (long)zb);
		}
		(void)base;
	}

	/* mbrtowc(NULL, NULL, 0) on a pending sequence */
	{
		mbstate_t sa, sb;
		size_t za, zb;
		int ea, eb;

		memset(&sa, 0, sizeof(sa));
		memset(&sb, 0, sizeof(sb));
		(void)ph_mbrtowc(NULL, "\xf0\x9f", 2, &sa);
		(void)mbrtowc(NULL, "\xf0\x9f", 2, &sb);
		errno = 0;
		za = ph_mbrtowc(NULL, NULL, 0, &sa);
		ea = errno;
		errno = 0;
		zb = mbrtowc(NULL, NULL, 0, &sb);
		eb = errno;
		total++;
		if ((za != zb) || (ea != eb)) {
			udiff("mbrtowc", "NULL on pending ph=%ld/%d glibc=%ld/%d", (long)za, ea, (long)zb, eb);
		}
	}

	for (i = 0; i < sizeof(WS) / sizeof(WS[0]); i++) {
		for (len = 0; len <= 13; len++) {
			char ba[16], bb[16];
			const wchar_t *pa = WS[i], *pb = WS[i];
			mbstate_t sa, sb;
			size_t za, zb;
			int ea, eb;

			memset(ba, '#', 16);
			memset(bb, '#', 16);
			memset(&sa, 0, sizeof(sa));
			memset(&sb, 0, sizeof(sb));
			errno = 0;
			za = ph_wcsrtombs((len == 13) ? NULL : ba, &pa, len, &sa);
			ea = errno;
			errno = 0;
			zb = wcsrtombs((len == 13) ? NULL : bb, &pb, len, &sb);
			eb = errno;
			total++;
			if ((za != zb) || (ea != eb) || (pa != pb) || (memcmp(ba, bb, 16) != 0)) {
				if ((WS[i] == W4) && (za == (size_t)-1)) {
					kcount[K_LAX]++;
					known++;
				}
				else {
					udiff("wcsrtombs", "w%zu len=%zu ph=%ld/off%ld glibc=%ld/off%ld", i, len, (long)za,
							(pa == NULL) ? -1L : (long)(pa - WS[i]), (long)zb, (pb == NULL) ? -1L : (long)(pb - WS[i]));
				}
			}
		}
		{
			char ba[16], bb[16];
			size_t za, zb;

			memset(ba, '#', 16);
			memset(bb, '#', 16);
			za = ph_wcstombs(ba, WS[i], 16);
			zb = wcstombs(bb, WS[i], 16);
			total++;
			if (((za != zb) || (memcmp(ba, bb, 16) != 0)) && !((WS[i] == W4) && (za == (size_t)-1))) {
				udiff("wcstombs", "w%zu ph=%ld glibc=%ld", i, (long)za, (long)zb);
			}
		}
	}

	/* single bytes */
	{
		int c;

		for (c = -129; c <= 256; c++) {
			total++;
			if (ph_btowc(c) != btowc(c)) {
				udiff("btowc", "c=%d ph=%ld glibc=%ld", c, (long)ph_btowc(c), (long)btowc(c));
			}
		}
		for (c = 0; c <= 0x200; c++) {
			total++;
			if (ph_wctob((wint_t)c) != wctob((wint_t)c)) {
				udiff("wctob", "c=%d", c);
			}
		}
		total++;
		if (ph_wctob(WEOF) != wctob(WEOF)) {
			udiff("wctob", "WEOF");
		}
	}
}

static void utf8_runs(void)
{
	unsigned long d0 = diffs, k;

	printf("WCHAR-HOST UTF-8 runs (MB_CUR_MAX: ph=%zu glibc=%zu -- glibc keeps the historic 6, not compared)\n",
			ph___ctype_get_mb_cur_max(), MB_CUR_MAX);
	codepoint_run();
	sequence_run();
	string_run();
	for (k = 0; k < K_COUNT; k++) {
		printf("WCHAR-HOST by-design %-32s %lu\n", kname[k], kcount[k]);
	}
	printf("WCHAR-HOST UTF-8 runs: %lu differences\n", diffs - d0);
}
