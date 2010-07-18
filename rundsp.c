#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <memory.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include "armdsp.h"

void
usage (void)
{
	fprintf (stderr, "usage: rundsp prog\n");
	exit (1);
}

int
main (int argc, char **argv)
{
	int c;
	char *progname;
	char *err;

	while ((c = getopt (argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	if (optind >= argc)
		usage ();

	progname = argv[optind++];

	if (optind != argc)
		usage ();

	if ((err = armdsp_init ()) != NULL) {
		fprintf (stderr, "armdsp_init error: %s\n", err);
		exit (1);
	}

	if ((err = armdsp_run (progname)) != NULL) {
		fprintf (stderr, "armdsp_run error: %s\n", err);
		exit (1);
	}

	return (0);
}
