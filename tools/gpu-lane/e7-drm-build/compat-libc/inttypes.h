/* E7: simulates the libphoenix fix -- SCNxPTR/SCNuPTR (C99 7.8.1) are missing. */
#include_next <inttypes.h>
#ifndef SCNxPTR
#define SCNxPTR "lx"
#define SCNuPTR "lu"
#endif
