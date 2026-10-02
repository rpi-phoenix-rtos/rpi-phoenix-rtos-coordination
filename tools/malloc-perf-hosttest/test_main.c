/* Host runner for the libphoenix allocator Unity group (see Makefile `test`). */
#include <stdlib.h>
#include "unity_fixture.h"

static void runner(void)
{
	RUN_TEST_GROUP(stdlib_malloc_retain);
}

int main(int argc, char *argv[])
{
	return (UnityMain(argc, (const char **)argv, runner) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
