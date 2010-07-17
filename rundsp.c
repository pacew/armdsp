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

#define SRAM_BASE 0x80000000
#define SRAM_SIZE 0x00020000

#define DRAM_BASE 0xc4000000
#define DRAM_SIZE 0x04000000

unsigned char *sram;
unsigned char *dram;

int
read_prog (char *filename)
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

		if (addr >= SRAM_BASE && addr < SRAM_BASE + SRAM_SIZE) {
			addr -= SRAM_BASE;
			base = sram;
			size = SRAM_SIZE;
		} else if (addr >= DRAM_BASE && addr < DRAM_BASE + DRAM_SIZE) {
			addr -= DRAM_BASE;
			base = dram;
			size = DRAM_SIZE;
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

int
main (int argc, char **argv)
{
	int dspfd;
	int memfd;
	int c;
	int ret;
	char *progname;

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

	if ((memfd = open ("/dev/mem", O_RDWR)) < 0) {
		perror ("open /dev/mem");
		exit (1);
	}

	sram = mmap (NULL, SRAM_SIZE,
		     PROT_READ | PROT_WRITE,
		     MAP_SHARED, memfd,
		     SRAM_BASE);
	if (sram == MAP_FAILED) {
		perror ("mmap sram");
		exit (1);
	}

	dram = mmap (NULL, DRAM_SIZE,
		     PROT_READ | PROT_WRITE,
		     MAP_SHARED, memfd,
		     DRAM_BASE);
	if (dram == MAP_FAILED) {
		perror ("mmap dram");
		exit (1);
	}

	if ((dspfd = open ("/dev/armdsp", O_RDONLY)) < 0) {
		perror ("open /dev/armdsp");
		exit (1);
	}

	ioctl (dspfd, ARMDSP_IOCSIMINT, 0);
	exit (0);


	if (ioctl (dspfd, ARMDSP_IOCSTOP, 0) < 0) {
		perror ("ioctl stop");
		exit (1);
	}

	if (read_prog (progname) < 0) {
		fprintf (stderr, "error: can't load program %s\n", progname);
		exit (1);
	}

	if((ret = ioctl (dspfd, ARMDSP_IOCSTART, 0)) < 0) {
		perror ("start");
		exit (1);
	}

	return (0);
}
