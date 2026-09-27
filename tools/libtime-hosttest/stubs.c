#include <stdlib.h>
#include "shim/sys/threads.h"
int threadinfo(int tid, unsigned int flags, threadinfo_t *info) { (void)tid; (void)flags; (void)info; abort(); }
