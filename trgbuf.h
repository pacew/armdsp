#ifndef _TRGBUF_H_
#define _TRGBUF_H_

/*
 * owner starts at 0
 * dsp writes fields, sets owner to 1, spin waits
 * arm reads fields, updates, sets owner to 0
 * dsp continues
 */
#define TRGBUF_BUFSIZ 256
struct trgbuf {
	uint32_t control;
	uint8_t params[8];
	/* followed by up to TRGBUF_BUFSIZ bytes of data */
};

#define TRGBUF_OWNER_SHIFT 0
#define TRGBUF_OWNER_MASK 0xff
#define TRGBUF_COMMAND_SHIFT 8
#define TRGBUF_COMMAND_MASK 0xff00
#define TRGBUF_LENGTH_SHIFT 16
#define TRGBUF_LENGTH_MASK 0xffff0000

#endif /* _TRGBUF_H_ */
