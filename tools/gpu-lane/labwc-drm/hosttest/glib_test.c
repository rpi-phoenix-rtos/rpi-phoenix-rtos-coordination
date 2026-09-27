/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * labwc-drm host test: shims/src/glib_compat.c (g_string_replace, GLib 2.68) is
 * built as lwphx_g_string_replace and compared with the host GLib's own
 * g_string_replace (>= 2.68) on the same inputs: result string and count.
 */

#include <string.h>
#include <glib.h>

#include "test.h"

guint lwphx_g_string_replace(GString *string, const gchar *find, const gchar *replace, guint limit);

int main(void)
{
	static const struct {
		const char *s, *find, *repl;
		guint limit;
	} t[] = {
		{ "abcabc", "b", "X", 0 },
		{ "abcabc", "b", "XYZ", 0 },
		{ "abcabc", "bc", "", 0 },
		{ "abcabc", "b", "X", 1 },
		{ "aaaa", "aa", "a", 0 },
		{ "aaaa", "a", "aa", 0 },
		{ "hello", "x", "y", 0 },
		{ "", "a", "b", 0 },
		{ "ab", "", "-", 0 },
		{ "ab", "", "-", 2 },
		{ "", "", "-", 0 },
		{ "%s and %s", "%s", "Foot", 0 },
		{ "Lab Lab Lab", "Lab", "labwc", 2 },
		{ "\xc3\xb6\xc3\xb6", "\xc3\xb6", "o", 0 },
		{ "xyz", "xyz", "", 0 },
	};
	for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		GString *a = g_string_new(t[i].s), *b = g_string_new(t[i].s);
		guint na = lwphx_g_string_replace(a, t[i].find, t[i].repl, t[i].limit);
		guint nb = g_string_replace(b, t[i].find, t[i].repl, t[i].limit);
		CHECK((na == nb) && (strcmp(a->str, b->str) == 0) && (a->len == b->len),
			"\"%s\" /%s/%s/ limit=%u -> \"%s\" (%u), GLib \"%s\" (%u)", t[i].s, t[i].find, t[i].repl, t[i].limit,
			a->str, na, b->str, nb);
		g_string_free(a, TRUE);
		g_string_free(b, TRUE);
	}
	return RESULT("glib");
}
