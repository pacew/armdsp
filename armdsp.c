/*
    armdsp - a simple framework for developing dsp programs for the TI OMAP
    Copyright (C) 2010  Pace Willisson <pace@alum.mit.edu>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * this module was partly inspired by dsploader.c by Tobias Knutsson,
 * 2009-6-24
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/poll.h>

#include <asm/uaccess.h>
#include <asm/cacheflush.h>

#include <mach/common.h>

#include "trgbuf.h"
#include "armdsp.h"

#define SYSCFG0_ADDR 0x01c14000
#define HOST1CFG_OFFSET 0x44
#define CHIPSIG 0x174
#define CHIPSIG_CLR 0x178

#define SICR 0x24

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
static int have_irq_chipint0;


#define armdsp_readphys(addr) (readl(IO_ADDRESS(addr)))
#define armdsp_writephys(val,addr) (writel(val,IO_ADDRESS(addr)))

static void
armdsp_reset (void)
{
	uint32_t val;

	/* assert active low reset */
	val = armdsp_readphys (PSC0_ADDR + MDCTL15);
	armdsp_writephys (val & ~MDCTL_LRESET, PSC0_ADDR + MDCTL15);
}

static void
armdsp_run (void)
{
	uint32_t val;

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

	/* write pointer to vector table */
	armdsp_writephys (ARMDSP_COMM_PHYS + ARMDSP_COMM_VECS,
			  SYSCFG0_ADDR + HOST1CFG_OFFSET);

	/* un-assert reset */
	val = armdsp_readphys (PSC0_ADDR + MDCTL15);
	armdsp_writephys (val | MDCTL_LRESET, PSC0_ADDR + MDCTL15);
}

static DECLARE_WAIT_QUEUE_HEAD (armdsp_wait);

#define armdsp_trgbuf_avail() (readl(&trgbuf->control) & TRGBUF_OWNER_MASK)

static ssize_t
armdsp_read (struct file *filp, char __user *buf, size_t count,
	     loff_t *f_pos)
{
	/* 268 bytes */
	unsigned char locbuf[sizeof (struct trgbuf) + TRGBUF_BUFSIZ];
	unsigned int data_len, total_len;
	int ret;
	
	if ((filp->f_flags & O_NONBLOCK) != 0 && armdsp_trgbuf_avail () == 0)
		return (-EAGAIN);

	ret = wait_event_interruptible (armdsp_wait, armdsp_trgbuf_avail());
	if (ret < 0)
		return (ret);

	data_len = (readl (&trgbuf->control) & TRGBUF_LENGTH_MASK)
		>> TRGBUF_LENGTH_SHIFT;

	total_len = sizeof (struct trgbuf) + data_len;
	if (total_len > sizeof locbuf || total_len > count)
		return (-EINVAL);

	memcpy_fromio (locbuf, trgbuf, total_len);

	if (copy_to_user (buf, locbuf, total_len))
		return (-EFAULT);

	return (total_len);
}

static ssize_t
armdsp_write (struct file *filp, const char __user *buf, size_t count,
	      loff_t *f_pos)
{
	union {
		struct trgbuf trgbuf;
		unsigned char buf[sizeof (struct trgbuf) + TRGBUF_BUFSIZ];
	} loc; /* 268 bytes */
	
	if (count > sizeof loc)
		return (-EINVAL);

	if (copy_from_user (loc.buf, buf, count))
		return (-EFAULT);

	/* make sure arm keeps ownership until all the data is in place */
	loc.trgbuf.control |= TRGBUF_OWNER_MASK;
	memcpy_toio (trgbuf, loc.buf, count);

	/* now give ownership back to dsp */
	writel (loc.trgbuf.control & ~TRGBUF_OWNER_MASK, &trgbuf->control);

	return (count);
}

static int
armdsp_ioctl(struct inode *inode, struct file *filp,
	     unsigned int cmd, unsigned long arg)
{

	int err = 0;
	    
	switch(cmd) {
	case ARMDSP_IOCSTOP:
		armdsp_reset ();
		break;

	case ARMDSP_IOCSTART:
		flush_cache_all ();
		wmb ();

		armdsp_run();
		break;

	  default:
		return -ENOTTY;
	}
	return err;
}

static unsigned int
armdsp_poll (struct file *file, poll_table *wait)
{
	unsigned int mask;

	poll_wait (file, &armdsp_wait, wait);
	
	mask = 0;
	if (armdsp_trgbuf_avail ())
		mask |= POLLIN | POLLRDNORM;
	mask |= POLLOUT | POLLWRNORM;
	return (mask);
}

static dev_t armdsp_dev;
static struct cdev armdsp_cdev;

static struct file_operations armdsp_fops = {
	.owner = THIS_MODULE,
	.read = armdsp_read,
	.write = armdsp_write,
	.ioctl = armdsp_ioctl,
	.poll = armdsp_poll,
};

static void armdsp_cleanup (void);

static irqreturn_t
armdsp_irq (int irq, void *dev_id)
{
	armdsp_writephys (1, SYSCFG0_ADDR + CHIPSIG_CLR); /* CHIPINT0 */
	writel (IRQ_DA8XX_CHIPINT0, davinci_soc_info.intc_base + SICR);

	wake_up (&armdsp_wait);

	return (IRQ_HANDLED);
}

static int __init 
armdsp_init (void)
{
	int ret = 0;

	ret = alloc_chrdev_region (&armdsp_dev, 0, 1, "armdsp");
	if (ret)
		goto cleanup;
	
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
	if (ret)
		goto cleanup;

	/* clear any pending interrupt */
	armdsp_writephys (1, SYSCFG0_ADDR + CHIPSIG_CLR); /* CHIPINT0 */
	writel (IRQ_DA8XX_CHIPINT0, davinci_soc_info.intc_base + SICR);

	ret = request_irq (IRQ_DA8XX_CHIPINT0, armdsp_irq,
			   IRQF_DISABLED, "armdsp", &armdsp_cdev);
	if (ret)
		goto cleanup;
	have_irq_chipint0 = 1;

	return (0);

cleanup:
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
	/* clear any pending interrupt */
	armdsp_writephys (1, SYSCFG0_ADDR + CHIPSIG_CLR); /* CHIPINT0 */
	writel (IRQ_DA8XX_CHIPINT0, davinci_soc_info.intc_base + SICR);

	if (have_irq_chipint0) {
		have_irq_chipint0 = 0;
		free_irq (IRQ_DA8XX_CHIPINT0, &armdsp_cdev);
	}

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
