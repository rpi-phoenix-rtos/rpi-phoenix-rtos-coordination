/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * labwc-drm host test: compat/src/lwphx_uchar.c (UTF-8 mbrtoc32/c32rtomb, which
 * foot's char32_t layer is built on). Oracle: every code point U+0000..U+10FFFF
 * (surrogates excluded) round-trips through c32rtomb -> mbrtoc32, the byte
 * lengths are the UTF-8 ones, and the host glibc's C.UTF-8 conversions agree
 * byte for byte (run.sh builds ours renamed lwphx_*).
 */

#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <string.h>
#include <uchar.h>
#include <wchar.h>

#include "test.h"

size_t lwphx_mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
size_t lwphx_c32rtomb(char *s, char32_t c32, mbstate_t *ps);

static size_t utf8_len(uint32_t c)
{
	return (c < 0x80u) ? 1u : ((c < 0x800u) ? 2u : ((c < 0x10000u) ? 3u : 4u));
}

int main(void)
{
	char buf[8], ref[8];
	char32_t w;
	mbstate_t st, rst;
	size_t r, rr;
	uint32_t c;
	int bad_rt = 0, bad_len = 0, bad_ref = 0, bad_dec = 0;

	CHECK(setlocale(LC_CTYPE, "C.UTF-8") != NULL, "host C.UTF-8 locale for the reference");
	for (c = 0; c <= 0x10ffffu; c++) {
		if ((c >= 0xd800u) && (c <= 0xdfffu)) {
			continue;
		}
		memset(&st, 0, sizeof(st));
		memset(&rst, 0, sizeof(rst));
		r = lwphx_c32rtomb(buf, (char32_t)c, &st);
		rr = c32rtomb(ref, (char32_t)c, &rst);
		if (r != utf8_len(c)) {
			bad_len++;
		}
		if ((r != rr) || (memcmp(buf, ref, r) != 0)) {
			bad_ref++;
		}
		memset(&st, 0, sizeof(st));
		w = 0xffffffffu;
		rr = lwphx_mbrtoc32(&w, buf, r, &st);
		if ((w != c) || (rr != ((c == 0u) ? 0u : r))) {
			bad_rt++;
		}
		/* glibc decodes our bytes the same way */
		memset(&rst, 0, sizeof(rst));
		if ((c != 0u) && ((mbrtoc32(&w, buf, r, &rst) != r) || (w != c))) {
			bad_dec++;
		}
	}
	CHECK(bad_len == 0, "c32rtomb lengths are the UTF-8 lengths (%d bad)", bad_len);
	CHECK(bad_ref == 0, "c32rtomb bytes equal glibc's C.UTF-8 c32rtomb (%d differ)", bad_ref);
	CHECK(bad_rt == 0, "mbrtoc32(c32rtomb(c)) == c for all 1112064 scalar values (%d bad)", bad_rt);
	CHECK(bad_dec == 0, "glibc's mbrtoc32 decodes our encoding (%d bad)", bad_dec);

	/* foot's locale_is_utf8() */
	memset(&st, 0, sizeof(st));
	r = lwphx_mbrtoc32(&w, "\xc3\xb6", 2, &st);
	CHECK((r == 2u) && (w == 0xf6u), "foot locale_is_utf8(): u8\"ö\" -> 2 bytes, U+00F6 (r=%zu w=%#x)", r, (unsigned)w);

	/* a sequence split over calls: the state carries it */
	memset(&st, 0, sizeof(st));
	r = lwphx_mbrtoc32(&w, "\xf0\x9f", 2, &st);
	CHECK(r == (size_t)-2, "first half of U+1F600 -> (size_t)-2 (r=%zd)", (ssize_t)r);
	CHECK(!mbsinit(&st), "state not initial in the middle of a sequence");
	r = lwphx_mbrtoc32(&w, "\x98", 1, &st);
	CHECK(r == (size_t)-2, "third byte -> still (size_t)-2");
	r = lwphx_mbrtoc32(&w, "\x80", 1, &st);
	CHECK((r == 1u) && (w == 0x1f600u), "last byte completes U+1F600 (r=%zu w=%#x)", r, (unsigned)w);
	CHECK(mbsinit(&st), "state initial again");

	/* invalid input -> EILSEQ, state reset */
	static const struct {
		const char *s;
		size_t n;
		const char *what;
	} bad[] = {
		{ "\x80", 1, "stray continuation byte" },
		{ "\xc0\x80", 2, "overlong NUL" },
		{ "\xe0\x80\x80", 3, "overlong 3-byte" },
		{ "\xed\xa0\x80", 3, "surrogate U+D800" },
		{ "\xf4\x90\x80\x80", 4, "U+110000" },
		{ "\xc3\x41", 2, "lead byte followed by ASCII" },
		{ "\xff", 1, "0xFF" },
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		memset(&st, 0, sizeof(st));
		errno = 0;
		r = lwphx_mbrtoc32(&w, bad[i].s, bad[i].n, &st);
		CHECK((r == (size_t)-1) && (errno == EILSEQ) && mbsinit(&st), "%s -> EILSEQ", bad[i].what);
	}
	errno = 0;
	CHECK((lwphx_c32rtomb(buf, 0xd800u, NULL) == (size_t)-1) && (errno == EILSEQ), "c32rtomb(surrogate) -> EILSEQ");
	CHECK(lwphx_c32rtomb(buf, 0x110000u, NULL) == (size_t)-1, "c32rtomb(U+110000) -> -1");
	memset(&st, 0, sizeof(st));
	CHECK(lwphx_mbrtoc32(&w, "", 1, &st) == 0u, "NUL -> 0");
	CHECK(lwphx_mbrtoc32(&w, "x", 0, &st) == (size_t)-2, "n == 0 -> (size_t)-2");
	return RESULT("uchar");
}
