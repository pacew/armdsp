/*
 * by Pace Willisson <pace@alum.mit.edu>
 *
 * inspired by dsploader.c by Tobias Knutsson, 2009-6-24
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include <asm/uaccess.h>
#include <asm/cacheflush.h>

#include "trgbuf.h"
#include "armdsp.h"

#define SYSCFG0_ADDR 0x01c14000
#define HOST1CFG_OFFSET 0x44

#define PSC0_ADDR 0x01c10000

/* from arch/arm/mach/psc.h in ti's linux, but missing from hawkboard */
/* PSC register offsets */
#define EPCPR		0x070
#define PTCMD		0x120
#define PTSTAT		0x128
#define PDSTAT		0x200
#define PDCTL1		0x304
#define MDSTAT		0x800
#define MDCTL		0xA00

#define MDSTAT_STATE_MASK 0x1f

#define MDSTAT15	(MDSTAT + 4 * 15)
#define MDCTL15		(MDCTL + 4 * 15)

#define MDCTL_STATE_MASK 	0x07
#define MDCTL_ENABLE		0x03
#define MDCTL_LRESET		(1<<8)

static void *armdsp_comm;
static struct trgbuf *trgbuf;

static uint32_t
armdsp_readphys (uint32_t addr)
{
	uint32_t *p, val = 0;

	if ((p = ioremap (addr, sizeof *p)) != NULL) {
		val = readl (p);
		iounmap (p);
	}
	return (val);
}

static void
armdsp_writephys (uint32_t val, uint32_t addr)
{
	uint32_t *p;
	if ((p = ioremap (addr, sizeof *p)) != NULL) {
		writel (val, p);
		iounmap (p);
	}
}

void
armdsp_reset (void)
{
	uint32_t val;

	/* assert active low reset */
	val = armdsp_readphys (PSC0_ADDR + MDCTL15);
	armdsp_writephys (val & ~MDCTL_LRESET, PSC0_ADDR + MDCTL15);
}

void
armdsp_run (void)
{
	uint32_t val;

	/* write pointer to vector table */
	armdsp_writephys (ARMDSP_COMM_PHYS + ARMDSP_COMM_VECS,
			  SYSCFG0_ADDR + HOST1CFG_OFFSET);

	val = armdsp_readphys (PSC0_ADDR + MDSTAT15);
	if ((val & MDSTAT_STATE_MASK) != MDCTL_ENABLE) {
		/* turn on dsp power */
		val = armdsp_readphys (PSC0_ADDR + MDCTL15);
		val = (val & ~MDCTL_STATE_MASK) | MDCTL_ENABLE;
		armdsp_writephys (val, PSC0_ADDR + MDCTL15);

		val = armdsp_readphys (PSC0_ADDR + PTCMD);
		armdsp_writephys (val | 2, PSC0_ADDR + PTCMD);

		while ((armdsp_readphys (PSC0_ADDR + PTSTAT) & 2) != 0)
			;
	}

	/* un-assert reset */
	val = armdsp_readphys (PSC0_ADDR + MDCTL15);
	armdsp_writephys (val | MDCTL_LRESET, PSC0_ADDR + MDCTL15);
}

ssize_t
armdsp_read (struct file *filp, char __user *buf, size_t count,
	     loff_t *f_pos)
{
	unsigned int i;
	unsigned char locbuf[sizeof (struct trgbuf) + TRGBUF_BUFSIZ];
	unsigned int data_len, total_len;
	uint32_t owner, control;
	
	/*
	 * need to make sure we'll see fresh data from the dsp
	 * 
	 * I don't know the right way to do the cache and barrier stuff,
	 * so try everything.
	 */
	rmb ();
	clean_dcache_area (trgbuf, sizeof trgbuf->control);

	control = readl (&trgbuf->control);

	owner = (control & TRGBUF_OWNER_MASK) >> TRGBUF_OWNER_SHIFT;
	if (owner == 0)
		return (-EAGAIN);

	data_len = (control & TRGBUF_LENGTH_MASK) >> TRGBUF_LENGTH_SHIFT;
	total_len = sizeof (struct trgbuf) + data_len;
	if (total_len > sizeof locbuf || total_len > count)
		return (-EINVAL);

	clean_dcache_area (trgbuf, total_len);

	for (i = 0; i < total_len; i++)
		locbuf[i] = readb ((void *)trgbuf + i);

	if (copy_to_user (buf, locbuf, total_len))
		return (-EFAULT);

	return (total_len);
}

ssize_t
armdsp_write (struct file *filp, const char __user *buf, size_t count,
	      loff_t *f_pos)
{
	unsigned int i;
	union {
		struct trgbuf trgbuf;
		unsigned char buf[sizeof (struct trgbuf) + TRGBUF_BUFSIZ];
	} loc;
	
	if (count > sizeof loc)
		return (-EINVAL);

	if (copy_from_user (loc.buf, buf, count))
		return (-EFAULT);

	for (i = 0; i < count; i++)
		writeb (loc.buf[i], (void *)trgbuf + i);

	/*
	 * need to make sure all our writes get all the way to memory
	 * before we set the owner flag back to the dsp
	 */
	clean_dcache_area (trgbuf, count); /* drain WB */
	wmb ();

	loc.trgbuf.control &= ~TRGBUF_OWNER_MASK;
	writel (loc.trgbuf.control, &trgbuf->control);

	return (count);
}

int
armdsp_ioctl(struct inode *inode, struct file *filp,
	     unsigned int cmd, unsigned long arg)
{

	int err = 0;
	    
	if (_IOC_TYPE(cmd) != DSPLOADER_IOC_MAGIC)
		return -ENOTTY;

	switch(cmd) {
	case DSPLOADER_IOCSTOP:
		armdsp_reset ();
		break;

	case DSPLOADER_IOCSTART:
		flush_cache_all ();
		wmb ();

		armdsp_run();
		break;

	  default:
		return -ENOTTY;
	}
	return err;
}

dev_t armdsp_dev;
struct cdev armdsp_cdev;

struct file_operations armdsp_fops = {
	.owner = THIS_MODULE,
	.read = armdsp_read,
	.write = armdsp_write,
	.ioctl = armdsp_ioctl
};

static void armdsp_cleanup (void);

static int __init 
armdsp_init(void)
{
	int ret = 0;

	ret = alloc_chrdev_region (&armdsp_dev, 0, 1, "armdsp");
	if (ret)
		return (ret);

	armdsp_comm = ioremap (ARMDSP_COMM_PHYS, ARMDSP_COMM_SIZE);
	if (armdsp_comm == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}
	trgbuf = armdsp_comm + ARMDSP_COMM_TRGBUF;
	memset (trgbuf, 0, sizeof *trgbuf);

	cdev_init(&armdsp_cdev, &armdsp_fops);
	armdsp_cdev.owner = THIS_MODULE;
	armdsp_cdev.ops = &armdsp_fops;
	ret = cdev_add (&armdsp_cdev, armdsp_dev, 1);

cleanup:
	if (ret)
		armdsp_cleanup ();

	return (ret);
}

static void __exit
armdsp_exit(void)
{
	armdsp_cleanup ();
}

static void
armdsp_cleanup (void)
{
	if (armdsp_comm) {
		trgbuf = NULL;
		iounmap (armdsp_comm);
		armdsp_comm = NULL;
	}

	if (armdsp_dev) {
		unregister_chrdev_region (armdsp_dev, 1);
		armdsp_dev = 0;
	}
}

module_init(armdsp_init);
module_exit(armdsp_exit);
MODULE_AUTHOR("Pace Willisson <pace@alum.mit.edu>");
MODULE_LICENSE("GPL");
