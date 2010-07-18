#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <sys/select.h>

#include "armdsp.h"

void
usage (void)
{
	fprintf (stderr, "usage: armhost [prog]\n");
	exit (1);
}

int
main (int argc, char **argv)
{
	int c;
	char *err;
	fd_set rset;
	char *prog = NULL;

	while ((c = getopt (argc, argv, "v")) != EOF) {
		switch (c) {
		case 'v':
			armdsp_verbose = 1;
			break;
		default:
			usage ();
		}
	}

	if (optind < argc)
		prog = argv[optind++];
	
	if (optind != argc)
		usage ();

	if ((err = armdsp_init ()) != NULL) {
		fprintf (stderr, "armdsp_init error: %s\n", err);
		exit (1);
	}

	if (prog) {
		if ((err = armdsp_run (prog)) != NULL) {
			fprintf (stderr, "armdsp_run error: %s\n", err);
			exit (1);
		}
	}

	while (1) {
		FD_ZERO (&rset);
		FD_SET (armdsp_fd, &rset);
		
		if (select (armdsp_fd + 1, &rset, NULL, NULL, NULL) < 0) {
			perror ("select");
			exit (1);
		}
		
		if (FD_ISSET (armdsp_fd, &rset))
			armdsp_host ();
	}

	return (0);
}
