/* Host shim: time.c's clock_gettime(CLOCK_THREAD_CPUTIME_ID) needs gettid() and
 * threadinfo(); stubbed in tdiff-side, never reached by the calendar tests. */
#include <sys/types.h>
#define PH_THREADINFO_CPUTIME (1U << 4)
typedef struct { time_t cpuTime; } threadinfo_t;
int gettid(void);
int threadinfo(int tid, unsigned int flags, threadinfo_t *info);
