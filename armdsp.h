#ifndef _ARMDSP_H_
#define _ARMDSP_H_

#define ARMDSP_SRAM_BASE 0x80000000
#define ARMDSP_SRAM_SIZE 0x00020000

#define ARMDSP_DRAM_BASE 0xc4000000
#define ARMDSP_DRAM_SIZE 0x04000000

/*
 * communication area is first page of shared ram
 */
#define ARMDSP_COMM_PHYS ARMDSP_SRAM_BASE

#define ARMDSP_COMM_VECS 0
/* vector table size is 0x200 */
#define ARMDSP_COMM_TRGBUF 0x200
#define ARMDSP_COMM_TRGBUF_SIZE (4+8+256)

#define ARMDSP_COMM_USER 0x800
#define ARMDSP_COMM_USER_SIZE 0x800

#define ARMDSP_COMM_SIZE 0x1000

struct armdsp_trgbuf {
	uint32_t owner;
	uint32_t length;
	uint8_t buf[ARMDSP_COMM_TRGBUF_SIZE];
};

#define ARMDSP_TRGBUF_OWNER_DSP 0
#define ARMDSP_TRGBUF_OWNER_ARM 1


#define ARMDSP_IOC_MAGIC  'a'
#define ARMDSP_IOCSTOP _IO(ARMDSP_IOC_MAGIC, 47)
#define ARMDSP_IOCSTART _IO(ARMDSP_IOC_MAGIC, 48)
#define ARMDSP_IOCWMB _IO(ARMDSP_IOC_MAGIC, 49)
#define ARMDSP_IOCRMB _IO(ARMDSP_IOC_MAGIC, 50)

#ifndef __KERNEL__
char *armdsp_init (int cold_boot);
char *armdsp_run (char const *filename);
void armdsp_host (void);

int armdsp_verbose;
int armdsp_fd;

void *armdsp_sram;
void *armdsp_dram;

#endif /* __KERNEL__ */

#endif /* _ARMDSP_H_ */
