/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * weston-drm host test: Weston's XKB start-up sequence (weston patches 0003 +
 * 0007) against the same libxkbcommon source, with the Pi's situation recreated:
 * no include path exists (HOME, XDG_CONFIG_HOME, XKB_CONFIG_ROOT and
 * XKB_CONFIG_EXTRA_PATH point to missing directories). m6a stopped at
 * "failed to create XKB context" exactly here. Run: run.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon.h>

#include "weston_keymap.h"

static int fails;

#define CHECK(cond, ...) \
	do { \
		printf((cond) ? "ok   " : "FAIL "); \
		fails += !(cond); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (0)

int main(void)
{
	struct xkb_rule_names names = { "evdev", "pc105", "us", NULL, NULL };
	struct xkb_context *ctx;
	struct xkb_keymap *km;
	char *str;

	setenv("HOME", "/nonexistent-home", 1);
	setenv("XDG_CONFIG_HOME", "/nonexistent-xdg", 1);
	setenv("XKB_CONFIG_ROOT", "/nonexistent-xkb-root", 1);
	setenv("XKB_CONFIG_EXTRA_PATH", "/nonexistent-xkb-extra", 1);
	setenv("XKB_LOG_LEVEL", "critical", 1);

	/* the m6a failure, reproduced */
	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	CHECK(ctx == NULL, "xkb_context_new(NO_FLAGS) without any include path -> NULL (the m6a line)");
	if (ctx)
		xkb_context_unref(ctx);

	/* weston patch 0007 */
	ctx = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES);
	CHECK(ctx != NULL, "xkb_context_new(NO_DEFAULT_INCLUDES) -> context");
	if (!ctx)
		return 1;
	CHECK(xkb_context_num_include_paths(ctx) == 0, "no include paths");

	/* weston patch 0003: names fail, the baked keymap compiles */
	km = xkb_keymap_new_from_names(ctx, &names, 0);
	CHECK(km == NULL, "rule names fail without include paths");
	km = xkb_keymap_new_from_string(ctx, weston_builtin_xkb_keymap, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	CHECK(km != NULL, "builtin keymap (%zu bytes) compiles", strlen(weston_builtin_xkb_keymap));
	if (km) {
		xkb_keycode_t kc_a = 30 + 8; /* evdev KEY_A + 8 */
		const xkb_keysym_t *syms;
		struct xkb_state *st = xkb_state_new(km);
		int n = xkb_state_key_get_syms(st, kc_a, &syms);

		CHECK(n == 1 && syms[0] == XKB_KEY_a, "KEY_A -> keysym 'a' (n=%d)", n);
		CHECK(xkb_keymap_num_layouts(km) == 1, "one layout");
		str = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
		CHECK(str != NULL && strlen(str) > 1000, "keymap re-serialises for clients (%zu bytes)", str ? strlen(str) : 0);
		free(str);
		xkb_state_unref(st);
		xkb_keymap_unref(km);
	}
	xkb_context_unref(ctx);
	printf("RESULT fails=%d verdict=%s\n", fails, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
