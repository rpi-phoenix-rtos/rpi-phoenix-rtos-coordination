/* Differential test: libphoenix's vswprintf against glibc's, same inputs.
 *
 * glibc is the oracle. Hand-written expectations would encode my own reading of
 * the standard, which is exactly what is being tested -- and swprintf has one
 * trap that makes that dangerous: it returns NEGATIVE on truncation rather than
 * the would-be length like snprintf. A test I wrote from the same assumption as
 * the code would agree with the code and prove nothing.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

/* Pull the implementation in under different names so it can sit beside glibc's. */
#define swprintf  phx_swprintf
#define vswprintf phx_vswprintf
#include "../../sources/libphoenix/stdio/swprintf.c"
#undef swprintf
#undef vswprintf

static int fails, checks;

#define BUFW 256

static void cmp(const char *what, size_t n, const wchar_t *fmt, ...)
{
	wchar_t mine[BUFW], theirs[BUFW];
	int rm, rt;
	va_list a;

	wmemset(mine, L'\xEE', BUFW);
	wmemset(theirs, L'\xEE', BUFW);

	va_start(a, fmt);
	rm = phx_vswprintf(mine, n, fmt, a);
	va_end(a);
	va_start(a, fmt);
	rt = vswprintf(theirs, n, fmt, a);
	va_end(a);

	checks++;
	/* Both sides: same return SIGN and, when it succeeded, same text. */
	int signOk = ((rm < 0) == (rt < 0)) && ((rm < 0) || (rm == rt));
	int textOk = (rm < 0) || (wcscmp(mine, theirs) == 0);

	if (!signOk || !textOk) {
		fails++;
		printf("  [FAIL] %-28s n=%zu  mine=%d \"%ls\"   glibc=%d \"%ls\"\n",
			what, n, rm, mine, rt, theirs);
	}
}

int main(void)
{
	setlocale(LC_ALL, "C.UTF-8");

	cmp("plain", 64, L"hello");
	cmp("percent", 64, L"100%%");
	cmp("int", 64, L"[%d]", 42);
	cmp("int-neg", 64, L"[%d]", -42);
	cmp("int-width", 64, L"[%8d]", 42);
	cmp("int-leftpad", 64, L"[%-8d]", 42);
	cmp("int-zeropad", 64, L"[%08d]", 42);
	cmp("int-plus", 64, L"[%+d]", 42);
	cmp("uint", 64, L"[%u]", 4000000000u);
	cmp("hex", 64, L"[%x][%X]", 0xdeadbeef, 0xdeadbeef);
	cmp("hex-alt", 64, L"[%#x]", 0xabc);
	cmp("octal", 64, L"[%o]", 0755);
	cmp("long", 64, L"[%ld]", 1234567890L);
	cmp("longlong", 64, L"[%lld]", 1234567890123LL);
	cmp("size_t", 64, L"[%zu]", (size_t)123456);
	cmp("short", 64, L"[%hd]", (int)-7);
	cmp("char-mod", 64, L"[%hhd]", (int)-7);
	cmp("double-f", 64, L"[%f]", 3.14159);
	cmp("double-prec", 64, L"[%.2f]", 3.14159);
	cmp("double-e", 64, L"[%e]", 31415.9);
	cmp("double-g", 64, L"[%g]", 0.00031415);
	cmp("char", 64, L"[%c]", 'Z');
	cmp("wchar", 64, L"[%lc]", (wint_t)L'Z');
	cmp("mbs", 64, L"[%s]", "narrow");
	cmp("mbs-prec", 64, L"[%.3s]", "narrow");
	cmp("wcs", 64, L"[%ls]", L"wide");
	cmp("wcs-prec", 64, L"[%.2ls]", L"wide");
	cmp("star-width", 64, L"[%*d]", 6, 42);
	cmp("star-prec", 64, L"[%.*f]", 3, 3.14159);
	cmp("mixed", 64, L"%d/%s/%ls/%c", 7, "ab", L"cd", 'e');
	/* POSIX ' (grouping) flag: no separator in the C locale; the trailing
	 * conversion proves the argument was consumed */
	cmp("group-d", 64, L"[%'d|%d]", 1234567, 99);
	cmp("group-lu", 64, L"[%'lu bytes|%d]", 16535624089ul, 99);
	cmp("group-f", 64, L"[%'10.3f|%d]", 1234.5678, 99);
	cmp("group-left", 64, L"[%-'8d|%d]", 1234, 99);
	cmp("group-zero", 64, L"[%0'8d|%d]", 1234, 99);

	/* The trap: truncation must be NEGATIVE, not the would-be length. */
	cmp("exact-fit", 6, L"12345");
	cmp("one-short", 5, L"12345");
	cmp("truncate-num", 4, L"[%d]", 12345);
	cmp("zero-n", 0, L"x");

	printf("%s: %d checks, %d failure(s)\n", fails ? "SWPRINTF FAIL" : "SWPRINTF OK", checks, fails);
	return fails ? 1 : 0;
}
