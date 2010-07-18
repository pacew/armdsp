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


static int
read_prog (char const *filename)
{
	FILE *f;
	char buf[10000];
	unsigned int count, addr;
	char *p;
	int i;
	int val;
	unsigned char volatile *base;
	int size;

	if ((f = fopen (filename, "r")) == NULL)
		return (-1);

	while (fgets (buf, sizeof buf, f) != NULL) {
		p = buf;
		if (*p != 'S')
			continue;
		p++;
		switch (*p) {
		case '0':
			continue;
		case '7':
			goto done;
		case '3':
			break;
		default:
			goto bad;
		}
		p++;
		if (sscanf (p, "%2x%8x", &count, &addr) != 2)
			goto bad;
		p += 5 * 2;
		count -= 5;

		if (addr >= ARMDSP_SRAM_BASE
		    && addr < ARMDSP_SRAM_BASE + ARMDSP_SRAM_SIZE) {
			addr -= ARMDSP_SRAM_BASE;
			base = armdsp_sram;
			size = ARMDSP_SRAM_SIZE;
		} else if (addr >= ARMDSP_DRAM_BASE
			   && addr < ARMDSP_DRAM_BASE + ARMDSP_DRAM_SIZE) {
			addr -= ARMDSP_DRAM_BASE;
			base = armdsp_dram;
			size = ARMDSP_DRAM_SIZE;
		} else {
			printf ("invalid addr 0x%x\n", addr);
			exit (1);
		}

		if (addr < 0 || addr + count > size)
			goto bad;
		for (i = 0; i < count; i++) {
			if (sscanf (p, "%2x", &val) != 1)
				goto bad;
			p += 2;
			base[addr + i] = val;
		}
	}

done:
	fclose (f);
	return (0);

bad:
	fclose (f);
	fprintf (stderr, "%s: invalid srecord\n", filename);
	return (-1);
}

/* returns NULL on success, else error string */
char *
armdsp_init (void)
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

void
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
