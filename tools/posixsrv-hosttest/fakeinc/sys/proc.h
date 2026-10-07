/* Phoenix <sys/proc.h>: the session calls libtty makes, answered by shim.c. */
#ifndef HOSTTEST_SYS_PROC_H
#define HOSTTEST_SYS_PROC_H

#include <sys/types.h>

#define PROCQ_ALIVE 0x1u

int procExists(pid_t pid, pid_t pgid, pid_t sid, unsigned int flags);
int sessionCtty(pid_t sid, int acquire);

#endif
