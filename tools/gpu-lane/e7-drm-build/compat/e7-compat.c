/* E7 compat stubs (study only). open_memstream is used by libdrm only to print
 * ARM AFBC/AFRC and AMD format-modifier NAMES (xf86drm.c:289, :395); both
 * callers return NULL when it fails. */
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
FILE *open_memstream(char **bufp, size_t *sizep)
{
	(void)bufp; (void)sizep;
	errno = ENOSYS;
	return NULL;
}

int pthread_setcanceltype(int type, int *oldtype)
{
	(void)type;
	if (oldtype != NULL)
		*oldtype = 0;
	return 0;
}
