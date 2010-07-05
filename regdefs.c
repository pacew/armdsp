#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/stat.h>

void foobar (void) {}

void write_output (void);

void
usage (void)
{
	fprintf (stderr, "regdefs\n");
	exit (1);
}

struct area {
	struct area *next;
	unsigned int addr;
	int size;
	char *desc;
};
struct area *areas, **areas_tailp = &areas;
struct area *curarea;

struct reg {
	struct reg *next;
	unsigned int addr;
	char *reg_name;
	char *desc;
	char *docref;
	struct field *fields;
	struct field **fields_tailp;
};
struct reg *regs, **regs_tailp = &regs;

struct reg *curreg;

struct field {
	struct field *next;
	int hi_bit, lo_bit;
	char *field_name;
	char *desc;
};

struct reg *
find_reg (char *reg_name)
{
	struct reg *rp;

	for (rp = regs; rp; rp = rp->next) {
		if (strcmp (rp->reg_name, reg_name) == 0)
			return (rp);
	}
	return (NULL);
}

void handle_area (char *str);
void handle_area_regs (char *str);
void handle_reg_or_areg (char *str);
void handle_fields (char *str);
void handle_field (char *str);

char *filename;
int linenum;

void
syntax_error (char *str)
{
	fprintf (stderr, "%s:%d: syntax error %s\n",
		 filename, linenum, str ? str : "");
	exit (1);
}

int
main (int argc, char **argv)
{
	int c;
	FILE *inf;
	char buf[1000], op[1000];
	int len;
	char *p;

	filename = "regs.conf";

	while ((c = getopt (argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	if (optind != argc)
		usage ();

	if ((inf = fopen (filename, "r")) == NULL) {
		fprintf (stderr, "can't open %s\n", filename);
		exit (1);
	}

	linenum = 0;
	while (fgets (buf, sizeof buf, inf) != NULL) {
		linenum++;

		len = strlen (buf);
		while (len > 0 && isspace (buf[len-1]))
			buf[--len] = 0;
		p = buf;
		while (isspace (*p))
			p++;
		if (*p == 0 || *p == '#')
			continue;

		if (sscanf (p, "%s", op) != 1)
			continue;

		if (strcmp (op, "area") == 0) {
			handle_area (p);
		} else if (strcmp (op, "area_regs") == 0) {
			handle_area_regs (p);
		} else if (strcmp (op, "reg") == 0) {
			handle_reg_or_areg (p);
		} else if (strcmp (op, "areg") == 0) {
			handle_reg_or_areg (p);
		} else if (strcmp (op, "fields") == 0) {
			handle_fields (p);
		} else {
			handle_field (p);
		}
	}

	write_output ();

	return (0);
}

void
handle_area (char *str)
{
	unsigned int addr;
	char size_str[1000];
	int off;
	int size;
	int len;
	struct area *ap;

	if (sscanf (str, "%*s 0x%x %s %n", &addr, size_str, &off) != 2) {
		syntax_error (NULL);
		return;
	}

	size = atoi (size_str);
	len = strlen (size_str);
	if (len > 0) {
		switch (size_str[len - 1]) {
		case 'k': case 'K':
			size *= 1024;
			break;
		case 'm': case 'M':
			size *= 1024 * 1024;
			break;
		}
	}

	ap = calloc (1, sizeof *ap);
	ap->addr = addr;
	ap->size = size;
	ap->desc = strdup (str + off);

	*areas_tailp = ap;
	areas_tailp = &ap->next;
}

void
handle_area_regs (char *str)
{
	char *desc;
	struct area *ap;
	int off;

	off = 0;
	if (sscanf (str, "%*s %n", &off) != 0) {
		syntax_error (NULL);
		return;
	}

	desc = str + off;
	for (ap = areas; ap; ap = ap->next) {
		if (strcmp (ap->desc, desc) == 0)
			break;
	}
	if (ap == NULL) {
		syntax_error ("area not found");
		return;
	}
	
	curarea = ap;
}

void
handle_reg_or_areg (char *str)
{
	unsigned int addr;
	char op[1000];
	char reg_name[1000];
	char addr_str[1000];
	char fullname[1000];
	int off;
	struct reg *rp;
	char *endp;

	if (sscanf (str, "%s %s %s %n", op, addr_str, reg_name, &off) != 3) {
		syntax_error (NULL);
		return;
	}

	addr = strtoul (addr_str, &endp, 0);
	if (*endp != 0) {
		syntax_error ("bad address");
		return;
	}

	if (strcmp (op, "areg") == 0) {
		if (curarea == NULL) {
			syntax_error ("stray areg");
			return;
		}
		addr += curarea->addr;
		sprintf (fullname, "%s_%s", curarea->desc, reg_name);
	} else {
		strcpy (fullname, reg_name);
	}

	rp = calloc (1, sizeof *rp);
	rp->addr = addr;
	rp->reg_name = strdup (fullname);
	rp->desc = strdup (str + off);

	rp->fields_tailp = &rp->fields;

	*regs_tailp = rp;
	regs_tailp = &rp->next;
}

void
handle_fields (char *str)
{
	char reg_name[1000];
	char docref[1000];
	struct reg *rp;

	if (sscanf (str, "%*s %s %s", reg_name, docref) != 2) {
		syntax_error (NULL);
		return;
	}

	if ((rp = find_reg (reg_name)) == NULL) {
		syntax_error ("reg_name not found");
		return;
	}
	
	rp->docref = strdup (docref);

	curreg = rp;
}

void
handle_field (char *str)
{
	char field_spec[1000];
	char field_name[1000];
	int off;
	struct field *fp;
	int hi_bit, lo_bit;

	if (curreg == NULL) {
		syntax_error ("stray field def");
		return;
	}

	if (sscanf (str, "%s %s %n", field_spec, field_name, &off) != 2) {
		syntax_error (NULL);
		return;
	}
	if (strcmp (field_name, "L2CC") == 0) foobar ();

	fp = calloc (1, sizeof *fp);

	if (sscanf (field_spec, "%d-%d", &hi_bit, &lo_bit) == 2) {
		fp->hi_bit = hi_bit;
		fp->lo_bit = lo_bit;
	} else if (sscanf (field_spec, "%d", &hi_bit) == 1) {
		fp->hi_bit = hi_bit;
		fp->lo_bit = hi_bit;
	} else {
		syntax_error (NULL);
		return;
	}

	fp->field_name = strdup (field_name);
	fp->desc = strdup (str + off);

	*curreg->fields_tailp = fp;
	curreg->fields_tailp = &fp->next;
}

void
write_output (void)
{
	char tname[1000];
	int fd;
	FILE *outf;
	struct reg *rp;
	struct field *fp;
	int nbits, mask, shift;
	char name[1000];
	struct area *ap;

	strcpy (tname, "TMP.XXXXXX");
	fd = mkstemp (tname);
	fchmod (fd, 0644);
	outf = fdopen (fd, "w");

	for (ap = areas; ap; ap = ap->next) {
		fprintf (outf, "#define %s_AREA 0x%08x\n", ap->desc, ap->addr);
		fprintf (outf, "#define %s_AREA_SIZE 0x%x\n", ap->desc, ap->size);
	}

	for (rp = regs; rp; rp = rp->next) {
		fprintf (outf, "#define %s 0x%08x /* %s */\n",
			 rp->reg_name, rp->addr, rp->desc);
	}


	for (rp = regs; rp; rp = rp->next) {
		for (fp = rp->fields; fp; fp = fp->next) {
			nbits = fp->hi_bit - fp->lo_bit + 1;
			mask = (1 << nbits) - 1;
			shift = fp->lo_bit;

			if (strcmp (rp->reg_name, fp->field_name) == 0
			    && fp->lo_bit == 0
			    && fp->hi_bit == 31) {
				continue;
			}
			sprintf (name, "%s_%s", rp->reg_name, fp->field_name);

			fprintf (outf, "#define %s_MASK 0x%x\n", name, mask);
			fprintf (outf, "#define %s_SHIFT %d\n", name, shift);
		}
	}

	fclose (outf);
	rename (tname, "regs-omap-l138.h");
}


		
