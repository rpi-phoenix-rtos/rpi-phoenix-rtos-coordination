/* Phoenix <sys/threads.h> for the host: kernel locks and conditions as shim.c models them. */
#ifndef HOSTTEST_SYS_THREADS_H
#define HOSTTEST_SYS_THREADS_H

#include <time.h>

typedef int handle_t;

int mutexCreate(handle_t *h);
int mutexLock(handle_t h);
int mutexUnlock(handle_t h);
int condCreate(handle_t *h);
int condWait(handle_t c, handle_t m, time_t timeout);
int condSignal(handle_t c);
int condBroadcast(handle_t c);
int resourceDestroy(handle_t h);
int gettime(time_t *raw, time_t *offs);
void endthread(void) __attribute__((noreturn));

#endif
