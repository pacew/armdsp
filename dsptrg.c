#include <stdio.h>
#include <stdint.h>

#include "armdsp.h"
#include "regs-omap-l138.h"

#define readreg(addr) (*(uint32_t volatile *)(addr))
#define writereg(addr,val) (*(uint32_t volatile *)(addr) = (val))

extern int volatile vector_table[];

static void
wmb (void)
{
	/*
	 * reading any memory mapped reg drains write buffer
	 * see Cache User's Guide section 2.6
	 */
	readreg (L1PCFG);
}


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
	struct armdsp_trgbuf volatile *trgbuf;
	uint8_t *p;

	if (1 + 8 + length > ARMDSP_COMM_TRGBUF_SIZE)
		exit (1);

	trgbuf = (struct armdsp_trgbuf volatile *)
		((unsigned char *)vector_table + ARMDSP_COMM_TRGBUF);
	p = (uint8_t *)trgbuf->buf;
	*p++ = command;
	memcpy (p, params, 8);
	p += 8;
	memcpy (p, data, length);
	p += length;

	trgbuf->length = p - trgbuf->buf;

	wmb ();

	trgbuf->owner = ARMDSP_TRGBUF_OWNER_ARM;
	writereg (SYSCFG0_CHIPSIG, 1); /* CHIPINT0 to arm */
}

void
readmsg (unsigned char *params, char *data)
{
	struct armdsp_trgbuf volatile *trgbuf;

	uint32_t length;
	uint8_t *inp;

	trgbuf = (struct armdsp_trgbuf volatile *)
		((unsigned char *)vector_table + ARMDSP_COMM_TRGBUF);

	while (1) {
		if (trgbuf->owner == ARMDSP_TRGBUF_OWNER_DSP)
			break;
	}

	length = trgbuf->length;

	if (length < 8)
		exit (1);

	inp = (uint8_t *)trgbuf->buf;
	memcpy (params, inp, 8);

	if (data) {
		inp += 8;
		length -= 8;
		memcpy (data, inp, length);
	}
}
