/* Host shim for the Phoenix <sys/threads.h> declarations time.c uses.
 * clock_gettime(CLOCK_THREAD_CPUTIME_ID) needs gettid() and threadinfo(), never
 * reached by these tests. The time zone code guards its rules with a mutex;
 * stubs.c implements one that aborts on recursive locking or an unbalanced
 * unlock, so a self-deadlock on the target shows up here as a crash. */
#include <sys/types.h>
#define PH_THREADINFO_CPUTIME (1U << 4)
typedef int handle_t;
typedef struct { time_t cpuTime; } threadinfo_t;
int gettid(void);
int threadinfo(int tid, unsigned int flags, threadinfo_t *info);
int mutexCreate(handle_t *h);
int mutexLock(handle_t h);
int mutexUnlock(handle_t h);
