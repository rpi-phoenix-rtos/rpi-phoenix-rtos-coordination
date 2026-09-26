/* E7: simulates the libphoenix fix -- sysconf(_SC_PHYS_PAGES) (XSI/GNU, used by Mesa's
 * os_get_total_physical_memory). Value is arbitrary here; libphoenix returns -1 -> caller fails soft. */
#include_next <unistd.h>
#ifndef _SC_PHYS_PAGES
#define _SC_PHYS_PAGES 85
#endif
