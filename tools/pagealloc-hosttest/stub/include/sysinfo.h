#include "kstub.h"
typedef struct _pageinfo_t { unsigned int count; addr_t addr; char marker; } pageinfo_t;
typedef struct _meminfo_t {
	struct { unsigned int alloc, free, boot, sz; int mapsz; pageinfo_t *map; } page;
} meminfo_t;
