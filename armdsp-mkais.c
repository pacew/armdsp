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

FILE *outf;

static void
put32le (uint32_t val)
{
	putc (val, outf);
	putc (val >> 8, outf);
	putc (val >> 16, outf);
	putc (val >> 24, outf);
}

void write_section (uint8_t *buf, int size, uint32_t dest);

int
main (int argc, char **argv)
{
	int c;
	char outname[1000], *p;

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

	
	snprintf (outname, sizeof outname - 10, "%s", inname);
	if ((p = strrchr (outname, '.')) != NULL)
		*p = 0;
	strcat (outname, ".ais");
	remove (outname);
	if ((outf = fopen (outname, "w")) == NULL) {
		fprintf (stderr, "can't create %s\n", outname);
		exit (1);
	}

	put32le (0x41504954); /* ais start */

	armdsp_sram_used = (armdsp_sram_used + 3) & ~3;
	armdsp_dram_used = (armdsp_dram_used + 3) & ~3;

	write_section (armdsp_sram, armdsp_sram_used, ARMDSP_SRAM_BASE);
	write_section (armdsp_dram, armdsp_dram_used, ARMDSP_DRAM_BASE);

	put32le (0x58535906); /* jump and close */
	put32le (ARMDSP_SRAM_BASE); /* start addr */

	fclose (outf);

	return (0);
}

static uint32_t
get32le (void *arg)
{
	unsigned char *p = arg;
	return (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

void
write_section (uint8_t *buf, int size, uint32_t dest)
{
	uint32_t next_off, next_zero_off, zero_bytes, off, val, nonzero_bytes;

	next_off = 0;
	while (next_off < size) {
		next_zero_off = 0;
		zero_bytes = 0;

		off = next_off;
		while (off < size) {
			val = get32le (buf + off);
			if (val == 0) {
				if (zero_bytes == 0)
					next_zero_off = off;
				zero_bytes += 4;
			} else {
				if (zero_bytes >= 32)
					break;
				zero_bytes = 0;
			}
			off += 4;
		}

		if (zero_bytes == 0)
			next_zero_off = size;
		nonzero_bytes = next_zero_off - next_off;

		if (nonzero_bytes) {
			printf ("load 0x%x 0x%x\n",
				dest + next_off, nonzero_bytes);
			put32le (0x58536901);
			put32le (dest + next_off);
			put32le (nonzero_bytes);
			fwrite (buf + next_off, 1, nonzero_bytes, outf);
		}

		if (zero_bytes) {
			printf ("zero 0x%x 0x%x\n",
				dest + next_zero_off, zero_bytes);
			put32le (0x585e590a);
			put32le (dest + next_zero_off);
			put32le (zero_bytes);
			put32le (0); /* type of memory access 8/16/32 */
			put32le (0);
		}

		next_off += nonzero_bytes + zero_bytes;
	}
		
}
