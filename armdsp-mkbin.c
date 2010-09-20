#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <memory.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "armdsp.h"

int armdsp_sram_used;
int armdsp_dram_used;

int
parse_hex6x (char const *filename)
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
			if (addr + count > armdsp_sram_used)
				armdsp_sram_used = addr + count;
		} else if (addr >= ARMDSP_DRAM_BASE
			   && addr < ARMDSP_DRAM_BASE + ARMDSP_DRAM_SIZE) {
			addr -= ARMDSP_DRAM_BASE;
			base = armdsp_dram;
			size = ARMDSP_DRAM_SIZE;
			if (addr + count > armdsp_dram_used)
				armdsp_dram_used = addr + count;
		} else {
			printf ("invalid addr 0x%x\n", addr);
			exit (1);
		}

		if (addr + count > size)
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

void
usage (void)
{
	fprintf (stderr, "usage: armdsp-mkobj infile outfile\n");
	exit (1);
}

void write_file (char *ext, void *base, int size);
char *inname;

int
main (int argc, char **argv)
{
	int c;

	while ((c = getopt (argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	if (optind >= argc)
		usage ();

	inname = argv[optind++];

	if (optind != argc)
		usage ();

	armdsp_sram = malloc (ARMDSP_SRAM_SIZE);
	armdsp_dram = malloc (ARMDSP_DRAM_SIZE);

	if (armdsp_sram == NULL || armdsp_dram == NULL) {
		fprintf (stderr, "out of memory\n");
		exit (1);
	}

	if (parse_hex6x (inname) < 0) {
		fprintf (stderr, "error reading %s\n", inname);
		exit (1);
	}

	write_file (".sram", armdsp_sram, armdsp_sram_used);
	write_file (".dram", armdsp_dram, armdsp_dram_used);

	return (0);
}

void
write_file (char *ext, void *base, int size)
{
	char outname[1000], *p;
	FILE *outf;

	snprintf (outname, sizeof outname - 10, "%s", inname);
	if ((p = strrchr (outname, '.')) != NULL)
		*p = 0;
	strcat (outname, ext);
	remove (outname);
	if ((outf = fopen (outname, "w")) == NULL) {
		fprintf (stderr, "can't create %s\n", outname);
		exit (1);
	}
	fwrite (base, 1, size, outf);
	fclose (outf);
}

