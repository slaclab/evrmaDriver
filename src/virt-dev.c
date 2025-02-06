//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/version.h>

#include "internal.h"
#include "event-list.h"
#include "mng-dev.h"
#include "virt-dev.h"
#include "packet-queue.h"

#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(...) 0
#endif

enum {
	CLEAN_PRIV,
	CLEAN_DEV,
	CLEAN_ALL = CLEAN_DEV
};

enum {
	CLEAN_SYS_CLASS,
	CLEAN_SYS_TABLE,
	CLEAN_SYS_CDEV,
	
	CLEAN_SYS_ALL = CLEAN_SYS_CDEV
};


struct vdev_data {
	dev_t devt;
	struct device *dev;
	
	struct modac_vdev_des *des;
	
	struct event_list_type notified_events;
	struct modac_circ_buf cb_events;
	
	/* This lock is not used in the interrupts. */
	spinlock_t	cb_reader_lock;
	wait_queue_head_t wait_queue_events;
};

struct vdev_table_item {
	
	/*
	 * The cdevs are not allocated in a contiguous minor number space. Thus,
	 * they have to bo created for each MNG_DEV separately. Each cdev covers
	 * MAX_VIRT_DEVS_PER_MNG_DEV VIRT_DEVs.
	 * 
	 * cdevs must be prefabricated in order for the hot-unplug to function
	 * properly (they can't be a part of struct mngdev_data).
	 */
	struct cdev cdev;
	int have_cdev;
	struct vdev_data *vdev[MAX_VIRT_DEVS_PER_MNG_DEV];
};




/*
 * A common class for VIRT_DEVs in the MODAC system.
 */
static struct class *modac_vdev_class;

static int vdev_mng_first_minor;

static int vdev_table_size;
static struct vdev_table_item *vdev_table;
static struct mutex    vdev_table_mutex;


static void init_dev(struct vdev_data *vdev)
{
	modac_cb_init(&vdev->cb_events);
	spin_lock_init(&vdev->cb_reader_lock);
	init_waitqueue_head(&vdev->wait_queue_events);
	
	spin_lock_init(&vdev->des->direct_access_spinlock);
	vdev->des->direct_access_denied = 0;
	vdev->des->direct_access_active_count = 0;
	
	event_list_clear(&vdev->notified_events);
}

static inline int dev_name_equal(struct device *dev, void *arg)
{
	const char *dev_name_arg = (const char *)arg;
	const char *dev_name_dev = dev_name(dev);
	
	if(dev_name_dev == NULL) {
		/* sanity check */
		return 0;
	}
	
	return STRINGS_EQUAL(dev_name_arg, dev_name_dev);
}

static inline int dev_exists_for_vdev_class(const char *dev_name_arg)
{
	return class_for_each_device(modac_vdev_class, NULL, 
								 (void *)dev_name_arg, dev_name_equal);
}

static void cleanup(struct vdev_data *vdev, int what)
{
	switch(what) {
	case CLEAN_DEV:
		device_destroy(modac_vdev_class, vdev->devt);
	case CLEAN_PRIV:
		kfree(vdev);
	}
}

static inline int calc_table_indices(int minor, int *imngdev, int *ivdev)
{
	if(minor % MINOR_MULTIPLICATOR == 0) {
		return -ENODEV;
	}
	
	*imngdev = (minor - vdev_mng_first_minor) / MINOR_MULTIPLICATOR;
	*ivdev = (minor - vdev_mng_first_minor) % MINOR_MULTIPLICATOR - 1;

	if(*imngdev < 0 || *imngdev >= vdev_table_size) {
		return -ENODEV;
	}
	
	if(*ivdev < 0 || *ivdev >= MAX_VIRT_DEVS_PER_MNG_DEV) {
		return -ENODEV;
	}
	
	return 0;
}

static int vdev_open(struct inode *inode, struct file *filp)
{
	struct vdev_data *vdev;
	int ret = 0;
	int imngdev, ivdev;
	
	ret = calc_table_indices(iminor(inode), &imngdev, &ivdev);
	if(ret)
		return ret;
	
	/* Locate the device associated with the minor number.
	 * Look it up in the table; if found then obtain a
	 * reference (i.e., increment the reference count)
	 */
	mutex_lock(&vdev_table_mutex);
	
	if(vdev_table[imngdev].vdev[ivdev] == NULL) {
		mutex_unlock(&vdev_table_mutex);
		return -ENODEV;
	}
	
	vdev = vdev_table[imngdev].vdev[ivdev];
	
	ret = modac_c_vdev_on_open(vdev->des, inode);
	if(ret) {
		mutex_unlock(&vdev_table_mutex);
		printk(KERN_ERR "vdev_open fail: ret=%d, imngdev=%d, ivdev=%d\n", ret, imngdev, ivdev);
		return ret;
	}
	
	filp->private_data = (void *)vdev;
	
	mutex_unlock(&vdev_table_mutex);

	return ret;
}

static int vdev_release(struct inode *inode, struct file *filp)
{
	struct vdev_data *vdev = (struct vdev_data *)filp->private_data;
	
	modac_c_vdev_on_close(vdev->des, inode, vdev->dev);

	return 0;
}

static inline void unlock_direct_call(struct modac_vdev_des *vdev_des)
{
	spin_lock(&vdev_des->direct_access_spinlock);
	vdev_des->direct_access_active_count --;
	spin_unlock(&vdev_des->direct_access_spinlock);
}

static inline int lock_direct_call(struct modac_vdev_des *vdev_des)
{
	/* 
	* Direct HW calls are not protected by a mutex so they have to be
	* protected by this mechanism in case of hot-unplug.
	*/
	
	int retval;

	spin_lock(&vdev_des->direct_access_spinlock);
	
	if (vdev_des->direct_access_denied) {
		retval = 0;
	} else {
		vdev_des->direct_access_active_count ++;
		retval = 1;
	}

	spin_unlock(&vdev_des->direct_access_spinlock);

	return retval;
}


static int read_has_data(struct vdev_data *vdev);

static long vdev_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct vdev_data *vdev = (struct vdev_data *)filp->private_data;
	int ret = 0;
	
	/* Check that cmd is valid */
	if (_IOC_TYPE(cmd) != VIRT_DEV_IOC_MAGIC) {
		return -ENOTTY;
	}
	
	/* Check access */
	if (_IOC_DIR(cmd) & _IOC_READ) {
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
		ret = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    #else
		ret = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
    #endif
	} else if(_IOC_DIR(cmd) & _IOC_WRITE) {
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
		ret = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    #else
		ret = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
    #endif
	}
	
	if (ret) {
		return -EFAULT;
	}
	
	
	if (_IOC_NR(cmd) >= VIRT_DEV_HW_DIRECT_IOC_MIN &&
			   _IOC_NR(cmd) <= VIRT_DEV_HW_DIRECT_IOC_MAX) {
		
		if(!lock_direct_call(vdev->des)) {
			return -ENODEV;
		}
		
		ret = modac_c_vdev_do_direct_ioctl(vdev->des, cmd, arg);

		unlock_direct_call(vdev->des);
		
		return ret;
	}

	/* Start of the mutex locked code.
	 */
	ret = modac_c_vdev_devref_lock(vdev->des);
	if(ret) {
		return ret;
	}
	
	if (_IOC_NR(cmd) <= VIRT_DEV_IOC_MAX) {
		
		/* general range; no used IOCTL numbers here */
		
	} else if (_IOC_NR(cmd) >= VIRT_DEV_HW_IOC_MIN &&
			   _IOC_NR(cmd) <= VIRT_DEV_HW_IOC_MAX) {
		// HW specific range

		struct vdev_ioctl_hw_header header_args;
	
		// copy only the header part
		if (copy_from_user(&header_args, (void *)arg, sizeof(struct vdev_ioctl_hw_header))) {
			ret = -EFAULT;
			goto bail;
		}
		
		ret = modac_c_vdev_do_ioctl(vdev->des, &header_args.vres, cmd, arg);
		goto bail;
	
	} else if (_IOC_NR(cmd) >= VIRT_DEV_DBG_IOC_MIN &&
			   _IOC_NR(cmd) <= VIRT_DEV_DBG_IOC_MAX) {
		// dbg range
		ret = -ENOSYS;
		goto bail;
	} else {
		ret = -ENOTTY;
		goto bail;
	}
	
	ret = -ENOSYS; // not implemented by default
	
	switch(cmd) {
		
	case VIRT_DEV_IOC_SUBSCRIBE:
	{
		struct vdev_ioctl_subscribe subscribe_args;
		
		if (copy_from_user(&subscribe_args, (void *)arg, sizeof(struct vdev_ioctl_subscribe))) {
			ret = -EFAULT;
			goto bail;
		}
		
		ret = modac_c_vdev_subscribe(vdev->des, subscribe_args.event, subscribe_args.action);

		break;
	}

	case VIRT_DEV_IOC_STATUS_GET:
	{
		struct vdev_ioctl_status status;

		status.major = vdev->des->major;
		status.minor = vdev->des->minor;
		strncpy(status.name, vdev->des->name, MODAC_DEVICE_MAX_NAME + 1);
		
		ret = 0;
		
		if (copy_to_user((void *)arg, &status, sizeof(struct vdev_ioctl_status))) {
			ret = -EFAULT;
			goto bail;
		}
		break;
	}

	case VIRT_DEV_IOC_RES_STATUS_GET:
	{
		struct vdev_ioctl_res_status res_status_arg;
		
		if (copy_from_user(&res_status_arg, (void *)arg, sizeof(struct vdev_ioctl_res_status))) {
			ret = -EFAULT;
			goto bail;
		}
		
		ret = modac_c_vdev_get_res_status(vdev->des, &res_status_arg);
		if(ret)
			goto bail;

		if (copy_to_user((void *)arg, &res_status_arg, sizeof(struct vdev_ioctl_res_status))) {
			ret = -EFAULT;
			goto bail;
		}

		break;
	}

	} // switch

bail:

	modac_c_vdev_devref_unlock(vdev->des);
	
	return ret;
}

/* Return 0 or 1. */
static inline int read_has_data(struct vdev_data *vdev)
{
	int ret = 0;
	
	spin_lock(&vdev->cb_reader_lock);
	if(modac_cb_available(&vdev->cb_events)) {
		ret = 1;
	}
	spin_unlock(&vdev->cb_reader_lock);
	
	if(ret)
		return ret;
	
	
	modac_c_vdev_spin_lock(vdev->des);
	if(!event_list_is_empty(&vdev->notified_events)) {
		ret = 1;
	}
	modac_c_vdev_spin_unlock(vdev->des);	
	
	return ret;
}

/* 
 * Returns number of bytes read (put into 'buf').
 * 'buf' must be able to accomodate 
 * sizeof(u16) + CBUF_EVENT_ENTRY_DATA_LENGTH bytes.
 */
static int read_get(struct vdev_data *vdev, u8 *buf)
{
	int ret = 0;
	int event;
	
	/*
	 * First see if there's a notifying event available.
	 */
	modac_c_vdev_spin_lock(vdev->des);

	event = event_list_extract_one(&vdev->notified_events);
	if(event >= 0) {
		event_list_remove(&vdev->notified_events, event);
	}

	modac_c_vdev_spin_unlock(vdev->des);
	
	if(event >= 0) {
		u16 event16 = (u16)event;
		memcpy(buf, &event16, sizeof(u16));
		return sizeof(u16);
	}
	
	/* If no notifying event extract the normal event if any. */
	spin_lock(&vdev->cb_reader_lock);
	ret = modac_cb_get(&vdev->cb_events, buf);
	spin_unlock(&vdev->cb_reader_lock);
	
	return ret;
}


static ssize_t vdev_read(struct file *filp, char __user *buff, size_t buf_len, loff_t *offp)
{
	struct vdev_data *vdev = (struct vdev_data *)filp->private_data;
	int count_read = 0;
	int buf_still_free = buf_len;
	int ret = 0;

	/*
	 * The devref lock can not be used here. It uses a mutex which could make
	 * this high priority thread block by lower priority threads. A different
	 * lock is implemented instead to prevent running the code after the
	 * VDEV destruction.
	 */
	if(!lock_direct_call(vdev->des)) {
		return -ENODEV;
	}
	
	/* There must be a space for at least for one full event so it can be
	 * returned if it exists.
	 */
	if(buf_still_free < sizeof(u16) + CBUF_EVENT_ENTRY_DATA_LENGTH) {
		ret = -EINVAL;
		goto bail;
	}
	
	while(buf_still_free >= sizeof(u16) + CBUF_EVENT_ENTRY_DATA_LENGTH) {
		
		u8 evbuf[sizeof(u16) + CBUF_EVENT_ENTRY_DATA_LENGTH];
		
		int n = read_get(vdev, evbuf);
		
		if(n == 0) {
			
			/* If at least some data was already read return with it. */
			if(count_read > 0) {
				ret = count_read;
				goto bail;
			}
			
			if (filp->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				goto bail;
			}
			
			/*
			 * The process will sleep so the devref mechanism must be unlocked.
			 */
			unlock_direct_call(vdev->des);
			
			/*
			 * The system is unlocked now and a close can happen while the read
			 * is waiting to be woken up. If the close happens
			 * and if it is about to destroy the MNG_DEV and all the VIRT_DEVs
			 * there will be no problem. The vdev->wait_queue_events is not
			 * referred at that point anymore. Namely, the close first
			 * cancels the waiting thus releasing the vdev->wait_queue_events
			 * which is then free to disappear.
			 */
			
			if(wait_event_interruptible(vdev->wait_queue_events, 
								read_has_data(vdev)
										)) {
				return -ERESTARTSYS;
			}

			/* lock again */
			if(!lock_direct_call(vdev->des)) {
				return -ENODEV;
			}
		} else {
			
#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER
	{
		u32 t = mng_dbg_get_time(vdev->des);
		// dbg_timestamp[2]  is at 2 + 2*4 + 2*4
		memcpy(evbuf + 18, &t, 4);
	}
#endif

			if(copy_to_user(buff + count_read, evbuf, n)) {
				printk(KERN_ERR 
					"'copy_to_user' failed while reading the EVRMA data. "
					"Some event data was lost.");
				ret = -EFAULT;
				goto bail;
			}
			
			buf_still_free -= n;
			count_read += n;
		}
	}
	
	ret = count_read;
	
bail:

	unlock_direct_call(vdev->des);

	return ret;
}

static unsigned int vdev_poll(struct file *filp, poll_table *wait) 
{
	struct vdev_data *vdev = (struct vdev_data *)filp->private_data;

	int ret = 0;
	
	/* The poll() is, like read(), called from the high-priority thread and 
	 * must not use the mutexes to lock.
	 */
	if(!lock_direct_call(vdev->des)) {
		return -ENODEV;
	}

	poll_wait(filp, &vdev->wait_queue_events, wait);
	if(read_has_data(vdev)) {
		ret = POLLIN | POLLRDNORM;
	}
	
	unlock_direct_call(vdev->des);
	
	return ret;
}

static void vdev_vma_open(struct vm_area_struct *vma)
{
}

static void vdev_vma_close(struct vm_area_struct *vma)
{
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,139)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0)
static int
#else
static vm_fault_t
#endif
vdev_vma_fault(struct vm_fault *vmf)
#else
static int vdev_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
#endif
{
	printk(KERN_WARNING "'vdev_vma_fault' happened\n");
    return VM_FAULT_SIGBUS;
}


static struct vm_operations_struct vdev_vm_ops = {
	.open = vdev_vma_open,
	.close = vdev_vma_close,
	.fault = vdev_vma_fault,
};

static int vdev_mmap_ro(struct file *filp, struct vm_area_struct *vma)
{
	struct vdev_data *vdev = (struct vdev_data *)filp->private_data;
	
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long physical;
	int ret;
	
	ret = modac_c_vdev_devref_lock(vdev->des);
	if(ret) {
		return ret;
	}
	
	/* It does not seem that the PAGE_READONLY has any effect when calling 
	 * remap_pfn_range. So, prevent from continuing here.
	 */
	if(pgprot_val(vma->vm_page_prot) != pgprot_val(PAGE_READONLY)) {
		printk(KERN_WARNING "Tried to mmap not readonly\n");
		ret = -EINVAL;
		goto bail;
	}
	
	ret = modac_c_vdev_mmap_ro(vdev->des, offset, vsize, &physical);
	
	if(ret)
		goto bail;
	
	ret = remap_pfn_range(vma, vma->vm_start, __pa(physical) >> PAGE_SHIFT,
					vsize, PAGE_READONLY);

	if (ret) {
		ret = -EAGAIN;
		goto bail;
	}

	vma->vm_ops = &vdev_vm_ops;
	vdev_vma_open(vma);

bail:

	modac_c_vdev_devref_unlock(vdev->des);
	return ret;  
}

static const struct file_operations vdev_fops = {
	.owner = THIS_MODULE,
	.open = vdev_open,
	.release = vdev_release,
	.read = vdev_read,
	.poll = vdev_poll,
	.unlocked_ioctl = vdev_unlocked_ioctl,
	.mmap = vdev_mmap_ro,
};


/* Called from an IRQ in a spin-lock protected context. */
void modac_vdev_notify(struct modac_vdev_des *vdev_des, int event)
{
	struct vdev_data *vdev = (struct vdev_data *)vdev_des->priv;
	
	event_list_add(&vdev->notified_events, event);
	
	wake_up_interruptible(&vdev->wait_queue_events);
}

/* Called from an IRQ in a spin-locked context. */
void modac_vdev_put_cb(struct modac_vdev_des *vdev_des, int event, void *data, int length)
{
	struct vdev_data *vdev = (struct vdev_data *)vdev_des->priv;
	
#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER
	{
		struct evr_data_fifo_event *et_data = data;
		et_data->dbg_timestamp[1] = mng_dbg_get_time(vdev_des);
	}
#endif
			
	if(modac_cb_put(&vdev->cb_events, 
			event, data, length, &vdev->wait_queue_events) < 0) {
		
		/* 
		 * No event (not even the overflow event) was saved. Not waking up.
		 */

	} else {
		/* nothing to do here, wake up was already called */
	} 
}

static ssize_t show_config(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct vdev_data *vdev = dev_get_drvdata(dev);
	
	ssize_t ret = modac_c_vdev_devref_lock(vdev->des);
	if(ret) {
		return ret;
	}
	
	ret = modac_c_vdev_show_config(vdev->des, attr, buf);
	
	modac_c_vdev_devref_unlock(vdev->des);
	return ret;
}

static ssize_t store_config(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct vdev_data *vdev = dev_get_drvdata(dev);
	ssize_t ret;
	
	ret = modac_c_vdev_devref_lock(vdev->des);
	if(ret)
		return ret;
	
	if(PSTRINGS_EQUAL(buf, "hw_reset", 8)) {
		
		/* The command "hw_reset" resets the resources */
		modac_c_vdev_init_res(vdev->des);
		
	} else {
		
		unsigned long val;

		if (kstrtoul(buf, 0, &val) < 0) {
			ret = -EINVAL;
		} else {
			
			/* Non-zero will leave the hw intact after close. */ 
			vdev->des->leave_res_set_on_last_close = val;
		}
	}
	
	modac_c_vdev_devref_unlock(vdev->des);
		
	return count;
}

static struct device_attribute dev_attr_misc[] = {
	__ATTR(config, 0660, show_config, store_config),

	__ATTR_NULL
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,00)
static struct attribute *attrs_misc[] = {
	&dev_attr_misc[0].attr,
	NULL
};

static struct attribute_group dev_attr_grp_misc = {
	.attrs = attrs_misc,
};
static const struct attribute_group *dev_groups_misc[] = {
	&dev_attr_grp_misc,
	NULL
};
#endif






int modac_vdev_create(struct modac_vdev_des *vdev_des)
{
	int ret;
	int imngdev, ivdev;
	
	struct vdev_data *vdev;
	
	ret = calc_table_indices(vdev_des->minor, &imngdev, &ivdev);
	if(ret)
		return ret;
	
	vdev_des->priv = NULL;

	vdev = kmalloc(sizeof(struct vdev_data), GFP_ATOMIC);
	if(vdev == NULL) return -ENOMEM;
	
	vdev->des = vdev_des;
	
	init_dev(vdev);
	
	vdev->devt = MKDEV(vdev_des->major, vdev_des->minor);

	/*
	 * Check if the name is already in use for the MODAC VIRT_DEV class.
	 * 
	 * NOTE: This check is necessary because the 'device_create' would act
	 * strangely if called with the already used name.
	 */
	if(dev_exists_for_vdev_class(vdev_des->name)) {
		ret = -EEXIST;
		printk(KERN_WARNING "Warning: "
			"The name for the virtual device '%s' already used for some other MNG_DEV'\n", 
					vdev_des->name);
		cleanup(vdev, CLEAN_PRIV);
		return ret;
	}
	
	vdev->dev = device_create(modac_vdev_class, NULL, vdev->devt, vdev, vdev_des->name);
	if (IS_ERR(vdev->dev)) {
		printk(KERN_ERR "%s <dev>: Failed to create device!\n", vdev_des->name);
		ret = PTR_ERR(vdev->dev);
		cleanup(vdev, CLEAN_PRIV);
		return ret;
	}

	/* Enter the newly bound device info into the table so that it can
	 * be found by 'open'...
	 */
	mutex_lock(&vdev_table_mutex);
	
	if (vdev_table[imngdev].vdev[ivdev] != NULL) {
		mutex_unlock(&vdev_table_mutex);
		/* PANIC */
		printk(KERN_ERR "VIRT_DEV INTERNAL ERROR -- TABLE SLOT NOT EMPTY\n");
		ret = -EEXIST;
	} else {
		vdev_table[imngdev].vdev[ivdev] = vdev;
	}
	
	mutex_unlock(&vdev_table_mutex);

	if(ret) {
		cleanup(vdev, CLEAN_DEV);
		return ret;
	}

	vdev_des->priv = (void *)vdev;

	return 0;
}

void modac_vdev_destroy(struct modac_vdev_des *vdev_des)
{
	struct vdev_data *vdev = (struct vdev_data *)vdev_des->priv;
	int imngdev, ivdev;
	
	if(calc_table_indices(vdev_des->minor, &imngdev, &ivdev)) {
		/* 
		 * Saniti check. In reality this can't happen
		 * (the same call in 'modac_vdev_create' succeeded otherwise the code
		 * couldn't have arrived here). 
		 */
		return;
	}

	/* remove the 'vdev' from the lookup table
	 * so that future 'open' won't find it.
	 * After removing it the reference count
	 * can only drop but never increase.
	 */
	mutex_lock(&vdev_table_mutex);

	if(vdev_table[imngdev].vdev[ivdev] != vdev) {
		if(vdev_table[imngdev].vdev[ivdev] == NULL) {
			printk(KERN_WARNING "Value in the vdev (minor=%d) slot already NULL "
				"which is normal if was set by 'modac_vdev_table_reset'.\n", 
							vdev_des->minor);
		} else {
			printk(KERN_ERR "Strange value in the vdev slot\n");
		}
	}
	
	vdev_table[imngdev].vdev[ivdev] = NULL;

	mutex_unlock(&vdev_table_mutex);
	
	cleanup(vdev, CLEAN_ALL);
}

void modac_vdev_table_reset(int mngdev_minor)
{
	int imngdev = (mngdev_minor - vdev_mng_first_minor) / MINOR_MULTIPLICATOR;
	int i;
	
	BUG_ON(imngdev < 0 || imngdev >= vdev_table_size);
	
	printk(KERN_INFO "modac_vdev_table_reset called for mngdev_minor=%d", 
						mngdev_minor);
	
	mutex_lock(&vdev_table_mutex);
	
	for(i = 0; i < MAX_VIRT_DEVS_PER_MNG_DEV; i ++) {
		vdev_table[imngdev].vdev[i] = NULL;
	}
	
	mutex_unlock(&vdev_table_mutex);
}



static void cleanup_sys(int what)
{
	int i;
	
	switch(what) {
		
	case CLEAN_SYS_CDEV:
	
		for(i = 0; i < vdev_table_size; i ++) {
			if(vdev_table[i].have_cdev) {
				cdev_del(&vdev_table[i].cdev);
			}
		}
	
	case CLEAN_SYS_TABLE:
	
		kfree(vdev_table);
		
	case CLEAN_SYS_CLASS:

		class_destroy(modac_vdev_class);
	}
}

/* Init/fini code, no locks */
int modac_vdev_init(dev_t dev_num, int count)
{
	int ret, i;
	int dev_major = MAJOR(dev_num);
	
	vdev_mng_first_minor = MINOR(dev_num);
	
	/* 
	 * Not checking the region here because
	 * it was checked in the MNG_DEV
	 */
	
	
	
	mutex_init(&vdev_table_mutex);
	
	modac_vdev_class = 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0) || (defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,4))
        class_create(MODAC_VIRT_CLASS_NAME);
#else
        class_create(THIS_MODULE, MODAC_VIRT_CLASS_NAME);
#endif

	if (IS_ERR(modac_vdev_class)) {
		printk(KERN_ERR "%s <init>: Failed to create device class!\n", MODAC_VIRT_CLASS_NAME);
		return PTR_ERR(modac_vdev_class);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,12,00)
	modac_vdev_class->dev_attrs = dev_attr_misc;
#else
	modac_vdev_class->dev_groups = dev_groups_misc;
#endif
	
	vdev_table_size = count / MINOR_MULTIPLICATOR;
	vdev_table = kzalloc(
			sizeof(struct vdev_table_item) * vdev_table_size, GFP_ATOMIC);
	
	if(vdev_table == NULL) {
		cleanup_sys(CLEAN_SYS_CLASS);
		return -ENOMEM;
	}
	
	ret = 0;
	
	for(i = 0; i < vdev_table_size; i ++) {
		
		cdev_init(&vdev_table[i].cdev, &vdev_fops);
		vdev_table[i].cdev.owner = THIS_MODULE;
		
		ret = cdev_add(&vdev_table[i].cdev, 
					   MKDEV(dev_major, i * MINOR_MULTIPLICATOR + 1),
					   MAX_VIRT_DEVS_PER_MNG_DEV);
		if (ret) {
			printk(KERN_ERR "<VIRT_DEV>: Failed to add cdev structure!\n");
			cleanup_sys(CLEAN_SYS_TABLE);
			break;
		}
		
		vdev_table[i].have_cdev = 1;
	}
	
	/* 
	 * It can happen that not all the slots were initialized.
	 */
	if(ret) {
		cleanup_sys(CLEAN_SYS_CDEV);
		return ret;
	}

	return 0;
}

/* Init/fini code, no locks */
void modac_vdev_fini(void)
{
	cleanup_sys(CLEAN_SYS_ALL);
}



