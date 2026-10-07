/* Host runner: initialise libphoenix's pthread state (as its startup code
 * does on Phoenix), then run the phoenix-rtos-tests binary's own main(). */
void ph__pthread_init(void);
int tsd_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	ph__pthread_init();
	return tsd_main(argc, argv);
}
