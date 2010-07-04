#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>

#include "trgbuf.h"

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
	fprintf (stderr, "usage: hostd\n");
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
int
handle_write (struct trgbuf *tp, void *datap, int length)
{
	unsigned char *up;
	int fd, count;
	short ret;

	up = tp->params;
	fd = get2le (&up);
	count = get2le (&up);

	if (vflag)
		printf ("write(%d,...,%d)\n", fd, count);

	if (count > length) {
		fprintf (stderr, "write: invalid count %d %d\n",
			 count, length);
		exit (1);
	}

	if (fd == 1 || fd == 2) {
		ret = write (fd, datap, count);
	} else {
		ret = -1;
	}

	up = tp->params;
	put2le (&up, ret);

	return (0);
}

int
handle_close (struct trgbuf *tp, void *datap, int length)
{
	short ret;
	unsigned char *up;
	int fd;

	up = tp->params;
	fd = get2le (&up);

	if (vflag)
		printf ("close(%d)\n", fd);

	ret = 0;

	up = tp->params;
	put2le (&up, ret);

	return (0);
}

int
main (int argc, char **argv)
{
	int c;
	union {
		struct trgbuf trgbuf;
		unsigned char buf[sizeof (struct trgbuf) + TRGBUF_BUFSIZ];
	} u;

	struct trgbuf *tp;
	ssize_t n;
	void *datap;
	int i;
	int command;
	int length;

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

	if ((dspfd = open ("/dev/armdsp", O_RDWR)) < 0) {
		perror ("open armdsp");
		exit (1);
	}

	printf ("ready\n");

	while (1) {
		n = read (dspfd, u.buf, sizeof u.buf);
		if (n < 0) {
			if (errno == EAGAIN) {
				usleep (10 * 1000);
				continue;
			}
			perror ("read");
			break;
		}
		tp = &u.trgbuf;

		command = (tp->control & TRGBUF_COMMAND_MASK)
			>> TRGBUF_COMMAND_SHIFT;
		length = (tp->control & TRGBUF_LENGTH_MASK)
			>> TRGBUF_LENGTH_SHIFT;

		if (length > TRGBUF_BUFSIZ) {
			fprintf (stderr, "invalid length %d\n", length);
			exit (1);
		}

		datap = &u.buf[sizeof (struct trgbuf)];

		switch (command) {
		case _DTWRITE:
			length = handle_write (tp, datap, length);
			break;
		case _DTCLOSE:
			length = handle_close (tp, datap, length);
			break;
		default:
			printf ("unknown command 0x%x\n", command);
			printf ("params ");
			for (i = 0; i < 8; i++)
				printf ("%02x ", tp->params[i]);
			printf ("\n");
			printf ("length %d\n", length);
			dump (datap, length, 0);
			exit (1);
		}

		/* write response - just update length */
		tp->control = (tp->control & ~TRGBUF_LENGTH_MASK)
			| (length << TRGBUF_LENGTH_SHIFT);
		write (dspfd, tp, sizeof (struct trgbuf) + length);
	}

	return (0);
}
