#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mutex.h>

#include <linux/atomic.h>

#include "internal.h"
#include "evr.h"
#include "evr-sim.h"
#include "mng-dev.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Cosylab, d.d.");
MODULE_DESCRIPTION("Driver EVRMA.");
MODULE_VERSION("1.0.0");

#define EVR_DRV_NAME "evrma_drv"

#define NUM_MINORS 256

/*
 * to protect the test device calls
 */
static struct mutex mutex;

static int dev_major;

static struct modac_mngdev_des test_mngdev_des = {
	major: -1,
	minor: (MAX_MNG_DEVS - 1) * MINOR_MULTIPLICATOR,
	io_start: NULL, /* this NULL marks the simulation */
	io_size: 256, /* not used */
	name: "evr-sim-mng",
	hw_support: &hw_support_evr,
	irq_set: evr_sim_irq_set,
	io_rw: &evr_sim_rw_plugin,
};


static int __init evrma_init(void)
{
	int retval;

	dev_t dev_num = MKDEV(0, 0);
	
	printk(KERN_INFO "EVRMA loading in\n");
	
	mutex_init(&mutex);
	
	retval = alloc_chrdev_region(&dev_num, 0, NUM_MINORS, EVR_DRV_NAME);
	if (retval) {
		printk(KERN_ERR "%s <init>: Failed to allocate chrdev region!\n", EVR_DRV_NAME);
		return retval;
	}
	dev_major = MAJOR(dev_num);
	
	test_mngdev_des.major = dev_major;

	retval = modac_mngdev_init(dev_num, NUM_MINORS);
	if(retval) {
		return retval;
	}

	mutex_lock(&mutex);
	
	if(modac_mngdev_create(&test_mngdev_des)) {
		printk(KERN_DEBUG "EVRMA try test mng dev creation failed.\n");
	}
	
	mutex_unlock(&mutex);

	evrma_pci_init(dev_major, 0 * MINOR_MULTIPLICATOR);
	
	return 0;
}

static void __exit evrma_fini(void)
{
	evrma_pci_fini();
	
	mutex_lock(&mutex);
	modac_mngdev_destroy(&test_mngdev_des);
	mutex_unlock(&mutex);
	
	modac_mngdev_fini();
	
	unregister_chrdev_region(MKDEV(dev_major, 0), NUM_MINORS);
	
	printk(KERN_INFO "EVRMA unloaded\n");
}

module_init(evrma_init);
module_exit(evrma_fini);


