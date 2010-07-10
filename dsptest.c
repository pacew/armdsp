#include <stdio.h>

double x = 3.14;

int
main (int argc, char **argv)
{
	int i;
	unsigned char *p;

	printf ("dsptest %s %g\n", __TIME__, x);

	p = (unsigned char *)&x;
	for (i = 0; i < 8; i++)
		printf ("%02x ", p[i]);
	printf ("\n");
	return (0);
}


void __interrupt int_handler1 (void) { }
void __interrupt int_handler2 (void) { }
void __interrupt int_handler3 (void) { }
void __interrupt int_handler4 (void) { }
void __interrupt int_handler5 (void) { }
void __interrupt int_handler6 (void) { }
void __interrupt int_handler7 (void) { }
void __interrupt int_handler8 (void) { }
void __interrupt int_handler9 (void) { }
void __interrupt int_handler10 (void) { }
void __interrupt int_handler11 (void) { }
void __interrupt int_handler12 (void) { }
void __interrupt int_handler13 (void) { }
void __interrupt int_handler14 (void) { }
void __interrupt int_handler15 (void) { }
