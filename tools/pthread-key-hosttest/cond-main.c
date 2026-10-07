/* The condition-variable suite's main(): on Phoenix its groups run from
 * test-libc-pthread's runner (libc/pthread/main.c), which also holds groups
 * this harness does not model. The clock-step group always runs here: the
 * clock it steps is the shim's (shim.c). */
#include <stdlib.h>

#include "unity_fixture.h"

int test_main(int argc, char *argv[]);


static void runner(void)
{
	RUN_TEST_GROUP(pthread_cond_clock);
	RUN_TEST_GROUP(pthread_cond_clockstep);
}


int test_main(int argc, char *argv[])
{
	(void)setenv("PH_TEST_CLOCKSTEP", "1", 1);
	return (UnityMain(argc, (const char **)argv, runner) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
