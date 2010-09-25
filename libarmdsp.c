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

static uint32_t
get32le (FILE *f)
{
	int a, b, c, d;

	a = getc (f);
	b = getc (f);
	c = getc (f);
	d = getc (f);

	return (a | (b << 8) | (c << 16) | (d << 24));
}


static int
read_prog (char const *filename)
{
	FILE *inf;
	uint32_t cmd, addr, nbytes, limit, pattern, i;
	uint8_t volatile *base;

	if ((inf = fopen (filename, "r")) == NULL)
		return (-1);

	if (get32le (inf) != 0x41504954)
		goto bad;

	while (! feof (inf)) {
		cmd = get32le (inf);

		switch (cmd) {
		default:
			goto bad;

		case 0x58535901: /* section load */
			addr = get32le (inf);
			nbytes = get32le (inf);

			if (addr >= ARMDSP_SRAM_BASE
			    && addr < ARMDSP_SRAM_BASE + ARMDSP_SRAM_SIZE) {
				addr -= ARMDSP_SRAM_BASE;
				base = armdsp_sram;
				limit = ARMDSP_SRAM_SIZE;
			} else if (addr >= ARMDSP_DRAM_BASE
				   && addr
				   < ARMDSP_DRAM_BASE + ARMDSP_DRAM_SIZE) {
				addr -= ARMDSP_DRAM_BASE;
				base = armdsp_dram;
				limit = ARMDSP_DRAM_SIZE;
			} else {
				goto bad;
			}

			if (addr + nbytes > limit)
				goto bad;

			for (i = 0; i < nbytes; i++)
				base[addr + i] = getc (inf);
			if (nbytes & 3) {
				for (i = (nbytes & 3); i < 4; i++)
					getc (inf);
			}
			break;
		case 0x5853590A:
			addr = get32le (inf);
			nbytes = get32le (inf);
			if (get32le (inf) != 0) /* type must be bytes */
				goto bad;
			pattern = get32le (inf);

			if (addr >= ARMDSP_SRAM_BASE
			    && addr < ARMDSP_SRAM_BASE + ARMDSP_SRAM_SIZE) {
				addr -= ARMDSP_SRAM_BASE;
				base = armdsp_sram;
				limit = ARMDSP_SRAM_SIZE;
			} else if (addr >= ARMDSP_DRAM_BASE
				   && addr
				   < ARMDSP_DRAM_BASE + ARMDSP_DRAM_SIZE) {
				addr -= ARMDSP_DRAM_BASE;
				base = armdsp_dram;
				limit = ARMDSP_DRAM_SIZE;
			} else {
				goto bad;
			}

			if (addr + nbytes > limit)
				goto bad;

			memset ((void *)(base + addr), pattern, nbytes);
			break;

		case 0x58535906:
			addr = get32le (inf);
			goto done;
		}
	}

done:
	fclose (inf);
	return (0);

bad:
	fclose (inf);
	fprintf (stderr, "%s: invalid ais file\n", filename);
	return (-1);
}

/* returns NULL on success, else error string */
char *
armdsp_init (int cold_boot)
{
	static int memfd;
	static int beenhere;

	if (beenhere == 0) {
		beenhere = 1;

		if ((memfd = open ("/dev/mem", O_RDWR)) < 0)
			return ("armdsp_init: can't open /dev/mem");

		armdsp_sram = mmap (NULL, ARMDSP_SRAM_SIZE,
				    PROT_READ | PROT_WRITE,
				    MAP_SHARED, memfd,
				    ARMDSP_SRAM_BASE);
		if (armdsp_sram == MAP_FAILED)
			return ("armdsp_init: can't mmap sram");


		armdsp_dram = mmap (NULL, ARMDSP_DRAM_SIZE,
				    PROT_READ | PROT_WRITE,
				    MAP_SHARED, memfd,
				    ARMDSP_DRAM_BASE);
		if (armdsp_dram == MAP_FAILED)
			return ("armdsp_init: can't mmap dram");

		if ((armdsp_fd = open ("/dev/armdsp0",O_RDWR|O_NONBLOCK))<0)
			return ("armdsp_init: can't open /dev/armdsp0");
	}

	if (cold_boot) {
		ioctl (armdsp_fd, ARMDSP_IOCSTOP, 0);
		memset (armdsp_sram, 0, ARMDSP_SRAM_SIZE);
		ioctl (armdsp_fd, ARMDSP_IOCWMB, 0);
	}

	return (NULL);
}

char *
armdsp_run (char const *filename)
{
	if (ioctl (armdsp_fd, ARMDSP_IOCSTOP, 0) < 0)
		return ("armdsp_run: can't stop dsp");

	if (read_prog (filename) < 0)
		return ("armdsp_run: can't read program");

	if(ioctl (armdsp_fd, ARMDSP_IOCSTART, 0) < 0)
		return ("armdsp_run: can't start dsp");

	return (NULL);
}

/* from ti's rtssrc/trgcio.h */
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

static void
put2le (unsigned char **upp, int val)
{
	*(*upp)++ = val;
	*(*upp)++ = val >> 8;
}

static unsigned int
get2le (unsigned char **upp)
{
	unsigned int val;
	val = *(*upp)++;
	val |= (*(*upp)++) << 8;
	return (val);
}

/* communicates with ti's rtssrc/SHARED/trgdrv.c */
static void
handle_write (uint8_t *params, void *datap, int datalen)
{
	uint8_t *up;
	int fd, count;
	short ret;
	uint8_t retbuf[8];

	up = params;
	fd = get2le (&up);
	count = get2le (&up);

	if (armdsp_verbose)
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
	write (armdsp_fd, retbuf, 8);
}

static void
handle_close (uint8_t *params, void *datap, int datalen)
{
	short ret;
	unsigned char *up;
	int fd;
	uint8_t retbuf[8];

	up = params;
	fd = get2le (&up);

	if (armdsp_verbose)
		printf ("close(%d)\n", fd);

	ret = 0;

	memset (retbuf, 0, sizeof retbuf);
	up = retbuf;
	put2le (&up, ret);
	write (armdsp_fd, retbuf, 8);
}

void
armdsp_host (void)
{
	uint8_t tbuf[ARMDSP_COMM_TRGBUF_SIZE];
	uint8_t *params;
	ssize_t n;
	void *datap;
	int i;
	int command;
	int datalen;

	while ((n = read (armdsp_fd, tbuf, sizeof tbuf)) > 0) {
		if (n < 9) {
			fprintf (stderr, "armdsp proto error\n");
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
			printf ("armdsp_host unknown command 0x%x\n", command);
			printf ("params ");
			for (i = 0; i < 8; i++)
				printf ("%02x ", params[i]);
			printf ("\n");
			printf ("datalen %d\n", datalen);
			exit (1);
			break;
		}
	}
}
