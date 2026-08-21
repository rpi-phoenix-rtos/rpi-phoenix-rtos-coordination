/* SPDX-License-Identifier: Zlib
 *
 * Phoenix epoxy shim — the runtime helpers glamor's core calls.
 *
 * Every GL entrypoint binds directly to Mesa (libGL-phoenix.a); libepoxy is only
 * needed here for GL version / extension queries. On our GL 2.1 compat context the
 * classic space-separated GL_EXTENSIONS string is valid (no GL3-core glGetStringi
 * needed). These are the only epoxy_* symbols referenced by the glamor core sources
 * (libglamor.la); epoxy_has_egl_extension lives only in the unbuilt glamor_egl.c.
 *
 * Copyright 2026 Phoenix Systems.
 */
#include <string.h>
#include <stdbool.h>

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>


int epoxy_gl_version(void)
{
	const char *v = (const char *)glGetString(GL_VERSION);
	int major = 0, minor = 0;

	if (v == NULL) {
		return 0;
	}
	/* Skip any leading non-digit prefix (e.g. "OpenGL ES "). */
	while (*v != '\0' && (*v < '0' || *v > '9')) {
		v++;
	}
	while (*v >= '0' && *v <= '9') {
		major = major * 10 + (*v++ - '0');
	}
	if (*v == '.') {
		v++;
		if (*v >= '0' && *v <= '9') {
			minor = *v - '0';
		}
	}
	return major * 10 + minor; /* "2.1" -> 21 */
}


bool epoxy_is_desktop_gl(void)
{
	const char *v = (const char *)glGetString(GL_VERSION);

	/* GLES version strings start with "OpenGL ES"; desktop GL does not. */
	return !(v != NULL && strncmp(v, "OpenGL ES", 9) == 0);
}


bool epoxy_has_gl_extension(const char *ext)
{
	const char *exts = (const char *)glGetString(GL_EXTENSIONS);
	const char *p;
	size_t len;

	if (exts == NULL || ext == NULL) {
		return false;
	}
	len = strlen(ext);
	p = exts;
	/* Whole-token match against the space-separated extension list. */
	while ((p = strstr(p, ext)) != NULL) {
		if ((p == exts || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0')) {
			return true;
		}
		p += len;
	}
	return false;
}
