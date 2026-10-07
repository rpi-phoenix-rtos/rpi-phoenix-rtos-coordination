/* Host shim for the Phoenix <sys/proc.h> declarations time.c uses (CPU-time clocks,
 * never reached by these tests; stubs.c aborts if they are). */
#include <sys/types.h>
#include <time.h>
typedef struct { time_t user, sys, childUser, childSys; } cpuTimes_t;
int sys_cpuTime(pid_t pid, int tid, time_t *cpuTime, cpuTimes_t *cpuTimes);
int pidExists(pid_t pid);
