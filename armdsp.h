#ifndef _ARMDSP_H_
#define _ARMDSP_H_

#define ARMDSP_IOC_MAGIC  'a'
#define ARMDSP_IOCSTOP _IO(ARMDSP_IOC_MAGIC, 47)
#define ARMDSP_IOCSTART _IO(ARMDSP_IOC_MAGIC, 48)
#define ARMDSP_IOCSIMINT _IO(ARMDSP_IOC_MAGIC, 49)

#endif /* _ARMDSP_H_ */
