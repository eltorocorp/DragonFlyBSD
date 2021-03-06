/*
 * Infinite sleep test with 900,000 processes (1/10 second loop)
 *
 * Requires system w/ 128GB+ of ram, kern.maxproc=4000000 set in
 * /boot/loader.conf.  60 second stabilization time after last
 * process is forked.
 *
 * Also test tear-down by ^C'ing the test.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define COUNT	900000

int
main(int ac, char **av)
{
	int status;
	int i;
	int j;
	int idno;
	char *path;
	char *id;
	char c;

	if (ac == 1) {
		for (i = 0; i < COUNT; i += 100) {
#if 0
			asprintf(&path, "cp %s /tmp/x%06d", av[0], i);
			system(path);
			free(path);
			asprintf(&path, "/tmp/x%06d", i);
#else
			asprintf(&path, "%s", av[0]);
#endif
			asprintf(&id, "%d", i);
			if (vfork() == 0) {
				execl(path, path, id, NULL);
				_exit(0);
			}
			if (i % 1000 == 0) {
				printf("running %d\r", i);
				fflush(stdout);
			}
			free(path);
			free(id);
		}
	} else {
		idno = strtol(av[1], NULL, 0);
		setpriority(PRIO_PROCESS, 0, 5);
		for (j = 0; j < 100; ++j) {
			if (j == 99 || fork() == 0) {
				int dummy = 0;

				setpriority(PRIO_PROCESS, 0, 15);
				sleep(60);

				while (1) {
					umtx_sleep(&dummy, 0, 100000);
				}
				_exit(0);
			}
		}
		while (wait3(NULL, 0, NULL) >= 0 || errno == EINTR)
			;
		_exit(0);
	}
	printf("running %d\n", i);
	for (;;) {
		sleep(1);
	}
	return 0;
}
