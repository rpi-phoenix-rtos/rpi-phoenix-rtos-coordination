/* E7: simulates the libphoenix fix -- flock() is declared here but LOCK_* live only in <fcntl.h>. */
#include_next <sys/file.h>
#include <fcntl.h>
