/* E7: simulates the libphoenix fix -- pthread_setcanceltype (POSIX). */
#include_next <pthread.h>
#ifndef PTHREAD_CANCEL_ASYNCHRONOUS
#define PTHREAD_CANCEL_DEFERRED     0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#endif
#ifndef E7_SETCANCELTYPE
#define E7_SETCANCELTYPE
#ifdef __cplusplus
extern "C"
#endif
int pthread_setcanceltype(int type, int *oldtype);
#endif
