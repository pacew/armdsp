#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>

#include "armdsp.h"

/* from rtssrc / trgcio.h */
#define _DTOPEN    (0xF0)
#define _DTCLOSE   (0xF1)
#define _DTREAD    (0xF2)
#define _DTWRITE   (0xF3)
#define _DTLSEEK   (0xF4)
#define _DTUNLINK  (0xF5)
#define _DTGETENV  (0xF6)
#define _DTRENAME  (0xF7)
#define _DTGETTIME (0xF8)
#define _DTGETCLK  (0xF9)
#define _DTSYNC    (0xFF)

int vflag;

void
usage (void)
{
	fprintf (stderr, "usage: armhost\n");
	exit (1);
}

void
dump (void *buf, int n, unsigned int offset)
{
	int i;
	int j;
	int c;

	for (i = 0; i < n; i += 16) {
		printf ("%08x: ", offset + i);
		for (j = 0; j < 16; j++) {
			if (j == 8)
				putchar (' ');
			if (i+j < n)
				printf ("%02x ", ((unsigned char *)buf)[i+j]);
			else
				printf ("   ");
		}
		printf ("  ");
		for (j = 0; j < 16; j++) {
			c = ((unsigned char *)buf)[i+j] & 0x7f;
			if (i+j >= n)
				putchar (' ');
			else if (c < ' ' || c == 0x7f)
				putchar ('.');
			else
				putchar (c);
		}
		printf ("\n");

	}
}

int dspfd;

void
put2le (unsigned char **upp, int val)
{
	*(*upp)++ = val;
	*(*upp)++ = val >> 8;
}

unsigned int
get2le (unsigned char **upp)
{
	unsigned int val;
	val = *(*upp)++;
	val |= (*(*upp)++) << 8;
	return (val);
}

/* rtssrc/SHARED/trgdrv.c */
void
handle_write (uint8_t *params, void *datap, int datalen)
{
	uint8_t *up;
	int fd, count;
	short ret;
	uint8_t retbuf[8];

	up = params;
	fd = get2le (&up);
	count = get2le (&up);

	if (vflag)
		printf ("write(%d,...,%d)\n", fd, count);

	if (count != datalen) {
		printf ("proto error\n");
		exit (1);
	}

	if (fd == 1 || fd == 2) {
		ret = write (fd, datap, count);
	} else {
		ret = -1;
	}

	memset (retbuf, 0, sizeof retbuf);
	up = retbuf;
	put2le (&up, ret);
	write (dspfd, retbuf, 8);
}

void
handle_close (uint8_t *params, void *datap, int datalen)
{
	short ret;
	unsigned char *up;
	int fd;
	uint8_t retbuf[8];

	up = params;
	fd = get2le (&up);

	if (vflag)
		printf ("close(%d)\n", fd);

	ret = 0;

	memset (retbuf, 0, sizeof retbuf);
	up = retbuf;
	put2le (&up, ret);
	write (dspfd, retbuf, 8);
}

int
main (int argc, char **argv)
{
	int c;
	uint8_t tbuf[ARMDSP_COMM_TRGBUF_SIZE];
	uint8_t *params;
	ssize_t n;
	void *datap;
	int i;
	int command;
	int datalen;

	while ((c = getopt (argc, argv, "v")) != EOF) {
		switch (c) {
		case 'v':
			vflag = 1;
			break;
		default:
			usage ();
		}
	}

	if (optind != argc)
		usage ();

	if ((dspfd = open ("/dev/armdsp0", O_RDWR)) < 0) {
		perror ("open armdsp");
		exit (1);
	}

	while (1) {
		n = read (dspfd, tbuf, sizeof tbuf);
		if (n < 0) {
			perror ("read");
			break;
		}

		if (n < 9) {
			fprintf (stderr, "proto error\n");
			exit (1);
		}

		command = tbuf[0];
		params = tbuf + 1;
		datap = tbuf + 9;
		datalen = n - 9;

		switch (command) {
		case _DTWRITE:
			handle_write (params, datap, datalen);
			break;
		case _DTCLOSE:
			handle_close (params, datap, datalen);
			break;
		default:
			printf ("unknown command 0x%x\n", command);
			printf ("params ");
			for (i = 0; i < 8; i++)
				printf ("%02x ", params[i]);
			printf ("\n");
			printf ("datalen %d\n", datalen);
			dump (datap, datalen, 0);
			exit (1);
		}
	}

	return (0);
}
