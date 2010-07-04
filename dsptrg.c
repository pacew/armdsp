#include <stdio.h>
#include <stdint.h>

#include "trgbuf.h"

extern int volatile vector_table[];

/* 
 * low level communication between arm and dsp
 * called on the dsp side by ti's rtssrc/SHARED/trgdrv.c
 * they mate with armdsp/armhost.c
 */
void
writemsg (unsigned char command,
	  const unsigned char *params,
	  const char *data,
	  unsigned int length)
{
	struct trgbuf volatile *trgbuf;
	int i;
	uint32_t control;
	char *outp;

	trgbuf = (struct trgbuf volatile *)
		((unsigned char *)vector_table + ARMDSP_COMM_TRGBUF);

	control = (command << TRGBUF_COMMAND_SHIFT)
		| (length << TRGBUF_LENGTH_SHIFT);
	trgbuf->control = control;
	for (i = 0; i < 8; i++)
		trgbuf->params[i] = params[i];
	outp = (char *)trgbuf + sizeof (struct trgbuf);
	for (i = 0; i < length; i++)
		outp[i] = data[i];

	/* want to drain write buffers here */
	
	control = control | ((1 << TRGBUF_OWNER_SHIFT) & TRGBUF_OWNER_MASK);
	trgbuf->control = control;
}

void
readmsg (unsigned char *params, char *data)
{
	struct trgbuf volatile *trgbuf;

	int i, length;
	char *inp;
	uint32_t control, owner;

	trgbuf = (struct trgbuf volatile *)
		((unsigned char *)vector_table + ARMDSP_COMM_TRGBUF);


	while (1) {
		/* want to flush cache here */

		control = trgbuf->control;
		owner = (control & TRGBUF_OWNER_MASK) >> TRGBUF_OWNER_SHIFT;
		if (owner == 0)
			break;
	}

	length = (control & TRGBUF_LENGTH_MASK) >> TRGBUF_LENGTH_SHIFT;

	for (i = 0; i < 8; i++)
		params[i] = trgbuf->params[i];

	inp = (char *)trgbuf + sizeof (struct trgbuf);
	for (i = 0; i < length; i++)
		data[i] = inp[i];

}
