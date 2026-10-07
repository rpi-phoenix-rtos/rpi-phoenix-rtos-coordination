/* Host runner: initialise libphoenix's pthread state (as its startup code
 * does on Phoenix), then run the phoenix-rtos-tests binary's own main(). */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef uintptr_t ph_pthread_t;

void ph__pthread_init(void);
int ph_pthread_create(ph_pthread_t *thread, const void *attr, void *(*start)(void *), void *arg);
int ph_pthread_join(ph_pthread_t thread, void **ret);
int shim_stacksMapped(void);
int test_main(int argc, char *argv[]);


static void *noop(void *arg)
{
	return arg;
}


int main(int argc, char *argv[])
{
	int ret, stacks = 0;

	ph__pthread_init();
	ret = test_main(argc, argv);

	/* Every thread is gone by now, so every stack pthread.c mapped must be
	 * unmapped again -- once a create/join has reclaimed those that detached
	 * threads parked. Retried for a while: the last detached threads may still
	 * be on their way out. */
	for (int i = 0; i < 100; i++) {
		ph_pthread_t th;
		if ((ph_pthread_create(&th, NULL, noop, NULL) != 0) || (ph_pthread_join(th, NULL) != 0)) {
			fprintf(stderr, "harness: final create/join failed\n");
			return 1;
		}
		stacks = shim_stacksMapped();
		if (stacks == 0) {
			break;
		}
		usleep(10000);
	}
	if (stacks != 0) {
		fprintf(stderr, "harness: %d thread stack(s) still mapped at exit\n", stacks);
		ret = 1;
	}

	return ret;
}
