#ifndef _ARMDSP_H_
#define _ARMDSP_H_

/*
 * communication area is first page of shared ram
 */
#define ARMDSP_COMM_PHYS 0x80000000

#define ARMDSP_COMM_VECS 0 /* 512 (0x200) bytes */
#define ARMDSP_COMM_TRGBUF 0x800 /* 20 + 256 bytes */

#define ARMDSP_COMM_SIZE 0x1000

#define ARMDSP_IOC_MAGIC  'a'
#define ARMDSP_IOCSTOP _IO(ARMDSP_IOC_MAGIC, 47)
#define ARMDSP_IOCSTART _IO(ARMDSP_IOC_MAGIC, 48)

#endif /* _ARMDSP_H_ */
