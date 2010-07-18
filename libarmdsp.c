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

static unsigned char *sram;
static unsigned char *dram;
static int dspfd;

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
			base = sram;
			size = ARMDSP_SRAM_SIZE;
		} else if (addr >= ARMDSP_DRAM_BASE
			   && addr < ARMDSP_DRAM_BASE + ARMDSP_DRAM_SIZE) {
			addr -= ARMDSP_DRAM_BASE;
			base = dram;
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
	fprintf (stderr, "bad rec %s\n", buf);
	return (-1);
}

/* returns NULL on success, else error string */
static char *
armdsp_init (void)
{
	static int memfd;
	static int beenhere;

	if (beenhere == 0) {
		beenhere = 1;

		if ((memfd = open ("/dev/mem", O_RDWR)) < 0)
			return ("armdsp_init: can't open /dev/mem");

		sram = mmap (NULL, ARMDSP_SRAM_SIZE,
			     PROT_READ | PROT_WRITE,
			     MAP_SHARED, memfd,
			     ARMDSP_SRAM_BASE);
		if (sram == MAP_FAILED)
			return ("armdsp_init: can't mmap sram");


		dram = mmap (NULL, ARMDSP_DRAM_SIZE,
			     PROT_READ | PROT_WRITE,
			     MAP_SHARED, memfd,
			     ARMDSP_DRAM_BASE);
		if (dram == MAP_FAILED)
			return ("armdsp_init: can't mmap dram");

		if ((dspfd = open ("/dev/armdsp0", O_RDONLY | O_NONBLOCK)) < 0)
			return ("armdsp_init: can't open /dev/armdsp0");
	}


	return (NULL);
}

char *
armdsp_run (char const *filename)
{
	char *errstr;

	if ((errstr = armdsp_init ()) != NULL)
		return (errstr);

	if (ioctl (dspfd, ARMDSP_IOCSTOP, 0) < 0)
		return ("armdsp_run: can't stop dsp");

	if (read_prog (filename) < 0)
		return ("armdsp_run: can't read program");

	if(ioctl (dspfd, ARMDSP_IOCSTART, 0) < 0)
		return ("armdsp_run: can't start dsp");

	return (NULL);
}

