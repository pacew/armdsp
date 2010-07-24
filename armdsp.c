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
#include <linux/module.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/poll.h>

#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/atomic.h>

#include <mach/common.h>

#include "armdsp.h"
#include "regs-omap-l138.h"

/* the arm kernel thinks it's a uniprocessor, so normal wmb() is a nop */
#define armdsp_wmb() do { dsb(); } while (0)
#define armdsp_rmb() do { dmb(); } while (0)

#define ARMDSP_NDEV 2

static dev_t armdsp_dev;
static struct cdev armdsp_cdev;

struct armdsp {
	int irq;
	int have_irq;
	wait_queue_head_t wait;
	unsigned long pending;
	uint32_t chipint_mask;
};

static struct armdsp armdsp[ARMDSP_NDEV];

static void *armdsp_comm;
static struct armdsp_trgbuf *trgbuf;

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

static ssize_t
armdsp_read (struct file *filp, char __user *buf, size_t count,
	     loff_t *f_pos)
{
	unsigned int minor = iminor (filp->f_path.dentry->d_inode);
	struct armdsp *dp = &armdsp[minor];
	uint32_t length;
	int ret;
	
	switch (minor) {
	case 0:
		if (trgbuf->owner != ARMDSP_TRGBUF_OWNER_ARM) {
			if (filp->f_flags & O_NONBLOCK)
				return (-EAGAIN);
			ret = wait_event_interruptible
				(dp->wait,
				 trgbuf->owner == ARMDSP_TRGBUF_OWNER_ARM);
			if (ret < 0)
				return (ret);
		}
		armdsp_rmb ();
		length = trgbuf->length;
		if (length > count || length > sizeof trgbuf->buf)
			return (-EINVAL);
		if (copy_to_user (buf, trgbuf->buf, length))
			return (-EFAULT);
		return (length);
	case 1:
		while (test_and_clear_bit (0, &dp->pending) == 0) {
			if (filp->f_flags & O_NONBLOCK)
				return (-EAGAIN);
			ret = wait_event_interruptible
				(dp->wait, test_bit (0, &dp->pending));
			if (ret < 0)
				return (ret);
		}
		return (0);
	}

	return (-EINVAL);
}

static ssize_t
armdsp_write (struct file *filp, const char __user *buf, size_t count,
	      loff_t *f_pos)
{
	unsigned int minor = iminor (filp->f_path.dentry->d_inode);
	
	switch (minor) {
	case 0:
		if (count > sizeof trgbuf->buf)
			return (-EINVAL);
		if (copy_from_user (trgbuf->buf, buf, count))
			return (-EFAULT);
		trgbuf->length = count;
		/* flushes cache and WB */
		clean_dcache_area (trgbuf,
				   &trgbuf->buf[count] - (uint8_t *)trgbuf);
		trgbuf->owner = ARMDSP_TRGBUF_OWNER_DSP;
		clean_dcache_area (&trgbuf->owner, sizeof trgbuf->owner);
		return (count);
	}

	return (-EINVAL);
}

static int
armdsp_ioctl (struct inode *inode, struct file *filp,
	      unsigned int cmd, unsigned long arg)
{
	unsigned int minor = iminor (filp->f_path.dentry->d_inode);
	int err = 0;
	uint32_t val;

	switch(cmd) {
	case ARMDSP_IOCSTOP:
		if (minor != 0) {
			err = -ENOTTY;
			break;
		}
		armdsp_reset ();
		break;

	case ARMDSP_IOCSTART:
		if (minor != 0) {
			err = -ENOTTY;
			break;
		}
		flush_cache_all ();

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
		break;

	case ARMDSP_IOCWMB:
	case ARMDSP_IOCRMB:
		flush_cache_all ();
		break;

	default:
		err = -ENOTTY;
		break;
	}
	return (err);
}

static unsigned int
armdsp_poll (struct file *filp, poll_table *wait)
{
	unsigned int minor = iminor (filp->f_path.dentry->d_inode);
	struct armdsp *dp = &armdsp[minor];

	unsigned int mask;

	poll_wait (filp, &dp->wait, wait);
	
	mask = 0;
	switch (minor) {
	case 0:
		if (trgbuf->owner == ARMDSP_TRGBUF_OWNER_ARM)
			mask |= POLLIN | POLLRDNORM;
		break;
	case 1:
		if (test_bit (0, &dp->pending))
			mask |= POLLIN | POLLRDNORM;
		break;
	}
	mask |= POLLOUT | POLLWRNORM;
	return (mask);
}

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
	unsigned int minor = irq - IRQ_DA8XX_CHIPINT0;
	struct armdsp *dp;

	if (minor > ARMDSP_NDEV)
		return (IRQ_NONE);

	dp = &armdsp[minor];
	armdsp_writephys (dp->chipint_mask, SYSCFG0_ADDR + CHIPSIG_CLR);
	writel (dp->irq, davinci_soc_info.intc_base + SICR);
	set_bit (0, &dp->pending);
	armdsp_wmb ();
	wake_up (&dp->wait);
	return (IRQ_HANDLED);
}

static int armdsp_need_cdev_del;

static void
gpio_test (void)
{
	uint32_t mask5, mask6, mask_other;
	uint32_t val;

	printk ("\n\ngpio test\n");

	printk ("pinmux %x = %x\n", SYSCFG0_PINMUX13,
		armdsp_readphys (SYSCFG0_PINMUX13));

	val = armdsp_readphys (SYSCFG0_PINMUX13);
	val &= ~0xffff;
	val |= 0x8888;
	armdsp_writephys (val, SYSCFG0_PINMUX13);

	printk ("pinmux %x = %x\n", SYSCFG0_PINMUX13,
		armdsp_readphys (SYSCFG0_PINMUX13));


	mask5 = 1 << 12;
	mask6 = 1 << 13;
	mask_other = (1 << 11)|(1 << 10)|(1 << 9)|(1 << 8);
	printk ("dir67 %x\n", armdsp_readphys (GPIO_DIR67));

	val = armdsp_readphys (GPIO_DIR67);
	val &= ~(mask5 | mask6 | mask_other);
	armdsp_writephys (val, GPIO_DIR67);
	
	printk ("dir67 %x\n", armdsp_readphys (GPIO_DIR67));

	printk ("out67 %x\n", armdsp_readphys (GPIO_OUT_DATA67));

	val = armdsp_readphys (GPIO_OUT_DATA67);
	if (1) {
		val |= mask5 | mask6;
	} else {
		val &= ~(mask5 | mask6);
	}
	armdsp_writephys (val, GPIO_OUT_DATA67);
	printk ("out67 %x\n", armdsp_readphys (GPIO_OUT_DATA67));


	val = armdsp_readphys (SYSCFG0_PINMUX11);
	val &= ~0xffffff00;
	val |=  0x88888800;
	armdsp_writephys (val, SYSCFG0_PINMUX11);

	val = armdsp_readphys (SYSCFG0_PINMUX12);
	val &= ~0x0000ffff;
	val |=  0x00008888;
	armdsp_writephys (val, SYSCFG0_PINMUX12);

	val = armdsp_readphys (SYSCFG0_PINMUX13);
	val &= ~0xffffff00;
	val |=  0x88888800;
	armdsp_writephys (val, SYSCFG0_PINMUX13);

}



static int __init 
armdsp_init (void)
{
	unsigned int minor;
	struct armdsp *dp;
	int ret = 0;

	armdsp_reset ();

	for (minor = 0; minor < ARMDSP_NDEV; minor++) {
		dp = &armdsp[minor];
		init_waitqueue_head (&dp->wait);
		dp->irq = IRQ_DA8XX_CHIPINT0 + minor;
		dp->chipint_mask = 1 << minor;

		armdsp_writephys (dp->chipint_mask, SYSCFG0_ADDR + CHIPSIG_CLR);
		writel (dp->irq, davinci_soc_info.intc_base + SICR);
	}

	armdsp_comm = ioremap (ARMDSP_COMM_PHYS, ARMDSP_COMM_SIZE);
	if (armdsp_comm == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}
	trgbuf = armdsp_comm + ARMDSP_COMM_TRGBUF;
	memset (trgbuf, 0, sizeof *trgbuf);

	ret = alloc_chrdev_region (&armdsp_dev, 0, ARMDSP_NDEV, "armdsp");
	if (ret)
		goto cleanup;
	
	cdev_init(&armdsp_cdev, &armdsp_fops);
	armdsp_cdev.owner = THIS_MODULE;
	armdsp_cdev.ops = &armdsp_fops;
	ret = cdev_add (&armdsp_cdev, armdsp_dev, ARMDSP_NDEV);
	if (ret)
		goto cleanup;
	armdsp_need_cdev_del = 1;

	for (minor = 0; minor < ARMDSP_NDEV; minor++) {
		dp = &armdsp[minor];
		ret = request_irq (dp->irq, armdsp_irq,
				   IRQF_DISABLED, "armdsp", &armdsp_cdev);
		if (ret)
			goto cleanup;
		dp->have_irq = 1;
	}


	printk ("pinmux %x = %x\n", SYSCFG0_PINMUX13, readl (SYSCFG0_PINMUX13));

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
	unsigned int minor;
	struct armdsp *dp;

	armdsp_reset ();

	for (minor = 0; minor < ARMDSP_NDEV; minor++) {
		dp = &armdsp[minor];

		if (dp->have_irq) {
			armdsp_writephys (dp->chipint_mask,
					  SYSCFG0_ADDR + CHIPSIG_CLR);
			writel (dp->irq, davinci_soc_info.intc_base + SICR);
			free_irq (dp->irq, &armdsp_cdev);
		}
	}

	if (armdsp_need_cdev_del)
		cdev_del (&armdsp_cdev);

	if (armdsp_dev)
		unregister_chrdev_region (armdsp_dev, ARMDSP_NDEV);

	if (armdsp_comm)
		iounmap (armdsp_comm);
}

module_init(armdsp_init);
module_exit(armdsp_exit);
MODULE_AUTHOR("Pace Willisson <pace@alum.mit.edu>");
MODULE_LICENSE("GPL");
