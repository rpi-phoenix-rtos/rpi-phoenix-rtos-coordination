/* Runs the time_tz Unity group of phoenix-rtos-tests natively against
 * libphoenix's time.c: see "make unity" in the Makefile. */
#include "unity_fixture.h"
void ph__time_init(void) __attribute__((weak));
static void runner(void) { RUN_TEST_GROUP(time_tz); }
int main(int argc, char *argv[])
{
	if (ph__time_init != NULL) {
		ph__time_init();
	}
	return UnityMain(argc, (const char **)argv, runner) == 0 ? 0 : 1;
}
