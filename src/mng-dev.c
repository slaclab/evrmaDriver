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
#include <linux/delay.h>

#include "devref.h"
#include "internal.h"
#include "event-list.h"
#include "mng-dev.h"
#include "virt-dev.h"

enum {
	CLEAN_PRIV,
	CLEAN_HW,
	CLEAN_RM,
	CLEAN_DEV,
	CLEAN_ALL = CLEAN_DEV
};

enum {
	CLEAN_SYS_CLASS,
	CLEAN_SYS_TABLE,
	CLEAN_SYS_CDEV,
	CLEAN_SYS_VDEV,
	
	CLEAN_SYS_ALL = CLEAN_SYS_VDEV
};

#define NO_PID 0

/*
 * which events are counted (0...MAX_COUNTED_EVENTS-1)
 */
#define MAX_COUNTED_EVENTS EVENT_LIST_TYPE_MAX_EVENTS


/*
 * Per MNG_DEV data
 */
struct mngdev_data {

	dev_t devt;
	struct device *dev;
	
	struct modac_mngdev_des *des;
	
	/*
	 * the pid of the process that owns the file descriptor;
	 * NO_PID if the MNG_DEV is not open (can only be open once)
	 */
	pid_t pid;
	
	struct list_head vdev_list;

	/* used to:
	 * - maintain the VIRT_DEV list because the list may be called from the IRQ
	 * - put events to the queue
	 */
	spinlock_t	lock_general;
	unsigned long lock_flags;

	/*
	 * used from IRQ, must be spin lock protected
	 */
	struct event_dispatch_list event_dispatch_list;
	
	/*
	 * the argument for the dbg regs show command.
	 */
	u32 regs_offset, regs_length;
	
	/*
	 * Private data for hw support.
	 */
	void *hw_priv;

	atomic_t event_counters[MAX_COUNTED_EVENTS];
	
	struct modac_hw_support_data hw_support_data;
	struct modac_rm_data rm_data;
	
	/* 
	 * reference to the device
	 */
	struct devref       ref;
};


struct mngdev_table_item {
	
	/*
	 * The cdevs are not allocated in a contiguous minor number space. Thus,
	 * they have to bo created for each MNG_DEV separately.
	 * 
	 * cdevs must be prefabricated in order for the hot-unplug to function
	 * properly (they can't be a part of struct mngdev_data).
	 */
	struct cdev cdev;
	int have_cdev;
	struct mngdev_data *mngdev;
};

struct irq_process_arg {
	int notify_only;
	int event;
	void *data;
	int length;
};





/*
 * common for the MODAC mng system
 */
static struct class *modac_mngdev_class;

static int mngdev_first_minor;

static int mngdev_table_size;
static struct mngdev_table_item *mngdev_table;
static struct mutex    mngdev_table_mutex;


/*****  Forward function declarations *****/


static int unmap_inode(struct inode *inod, void *closure);
static void mngdev_event_dispatch_list_remove_all(
		struct mngdev_data *mngdev,
		struct modac_vdev_des *vdev_des);
static void mngdev_destroy_now(struct mngdev_data *mngdev);

static inline int read_direct_access_active_count(struct modac_vdev_des *vdev_des)
{
	int ret;
	
	spin_lock(&vdev_des->direct_access_spinlock);
	ret = vdev_des->direct_access_active_count;
	spin_unlock(&vdev_des->direct_access_spinlock);
	
	return ret;
}

/*****  Misc functions  *****/

/*
 * This function is called from the 
 * - modac_mngdev_destroy with the arguments 'inode' = NULL and 'dev' = NULL.
 * - file close() method, in that case the 'inode' is the inode of the file
 *   and 'dev' is the device where the close() was called (can be MNG_DEV
 *   or VIRT_DEV).
 * - vdev_des points to the VDEV descriptor in case of VIRT_DEV; it is NULL for MNG_DEV.
 */
static void drvdat_put(struct mngdev_data *mngdev, struct inode *inode, 
		struct device *dev, struct modac_vdev_des *vdev_des)
{
	if ( 0 == devref_put( &mngdev->ref, inode ) ) {
		/* nobody is using the reference; since it was
		 * removed from the table, no future 'open' will see it.
		 */
		
		if(inode == NULL) {
			printk(KERN_INFO "Destroying '%s' regularly\n", mngdev->des->name);
		} else {
			printk(KERN_INFO "HOT-UNPLUG: Destroy required '%s' from the last close(), vdev_des=0x%x\n", mngdev->des->name, (int)(size_t)vdev_des);
		}
		
		if(vdev_des != NULL) {
			
			/* Notify the potential users not to proceed with the read procedure. */
			spin_lock(&vdev_des->direct_access_spinlock);
			vdev_des->direct_access_denied = 1;
			spin_unlock(&vdev_des->direct_access_spinlock);
			
			/* 
			* Wait untill any use procedure actually stopped before proceding to
			* the destruction.
			*/
			while(read_direct_access_active_count(vdev_des) > 0) {
				msleep(1);
			}
		}

		mngdev_destroy_now(mngdev);

	} else {
		
		if(inode == NULL) {
			printk(KERN_INFO "HOT-UNPLUG: devref still active, postponing the destroyal of '%s'.\n", mngdev->des->name);
		} else {
			/*
			 * devref still active, just making a regular close(), no destruction,
			 */
		}
		
	}
}

static void init_dev(struct mngdev_data *mngdev)
{
	int i;
	
	/* Initialize the reference to the device
     */
	devref_init(&mngdev->ref, mngdev);
	
	mngdev->pid = NO_PID;
	
	mngdev->regs_offset = 0;
	mngdev->regs_length = 1024;

	INIT_LIST_HEAD(&mngdev->vdev_list);
	spin_lock_init(&mngdev->lock_general);
	
	for(i = 0; i < MAX_COUNTED_EVENTS; i ++) {
		atomic_set(&mngdev->event_counters[i], 0);
	}
	
	event_dispatch_list_init(&mngdev->event_dispatch_list);
}

static void cleanup(struct mngdev_data *mngdev, int what)
{
	switch(what) {
	case CLEAN_DEV:
		device_destroy(modac_mngdev_class, mngdev->devt);
	case CLEAN_RM:
		modac_rm_end(&mngdev->rm_data);
	case CLEAN_HW:
		mngdev->des->hw_support->end(&mngdev->hw_support_data);
	case CLEAN_PRIV:
		kfree(mngdev);
	}
}

static inline void dev_spin_lock(struct mngdev_data *mngdev)
{
	spin_lock_irqsave(&mngdev->lock_general, mngdev->lock_flags);
}

static inline void dev_spin_unlock(struct mngdev_data *mngdev)
{
	spin_unlock_irqrestore(&mngdev->lock_general, mngdev->lock_flags);
}

static int mngdev_devref_lock(struct mngdev_data *mngdev)
{
	/* Lock the reference. (No need to increment the reference
	 * counter (devref_get()). The reference is 'held' from
	 * open -> close.
	 */
	devref_lock( &mngdev->ref );
	
	/* Check validity */
	if ( devref_ptr( &mngdev->ref ) == NULL ) {
		/* device is gone */
		devref_unlock( &mngdev->ref );
		return -ENODEV;
	}
	
	return 0;
}

static inline void mngdev_devref_unlock(struct mngdev_data *mngdev)
{
	devref_unlock( &mngdev->ref );
}

/*****  Event handling functions  *****/


static int on_subscribe_change(struct mngdev_data *mngdev)
{
	struct event_list_type all_subscriptions;
	int ret;
	
	/* start from empty */
	event_list_clear(&all_subscriptions);
	
	dev_spin_lock(mngdev);
	/*
	 * Quickly (spinlocked section) make a copy of all subscriptions.
	 */
	event_dispatch_list_add_subscribed_events(&mngdev->event_dispatch_list, &all_subscriptions);
	dev_spin_unlock(mngdev);
	
	/* Only call HW if the device is still living */
	if ( devref_ptr(&mngdev->ref) != NULL ) {

		ret = mngdev->des->hw_support->on_subscribe_change(
					&mngdev->hw_support_data, &all_subscriptions);
	} else {
		ret = -ENODEV;
	}
	
	return ret;
}

static void mngdev_event_dispatch_list_remove_all(
		struct mngdev_data *mngdev,
		struct modac_vdev_des *vdev_des)
{
	dev_spin_lock(mngdev);
	event_dispatch_list_remove_all(&mngdev->event_dispatch_list, vdev_des);
	dev_spin_unlock(mngdev);
	
	on_subscribe_change(mngdev);
}

static int mng_event_dispatch_list_vdev_dbg(
		char *buf, size_t count,
		struct mngdev_data *mngdev,
		struct modac_vdev_des *vdev_des)
{
	int n = 0;
	
	dev_spin_lock(mngdev);
	n += event_dispatch_list_dbg(&mngdev->event_dispatch_list, buf, count, vdev_des);
	dev_spin_unlock(mngdev);
	
	return n;
}


/*****  VIRT_DEV list functions  *****/

static struct modac_vdev_des *list_find_vdev(
								struct mngdev_data *mngdev, u8 id)
{
	struct list_head *ptr;
	
	list_for_each(ptr, &mngdev->vdev_list) {
		struct modac_vdev_des *vdev_des = list_entry(ptr, struct modac_vdev_des, mngdev_item);
		if(vdev_des->id == id) {
			return vdev_des;
		}
	}

	return NULL;
}

static struct modac_vdev_des *list_find_vdev_by_name(
								struct mngdev_data *mngdev, const char *name)
{
	struct list_head *ptr;
	
	list_for_each(ptr, &mngdev->vdev_list) {
		struct modac_vdev_des *vdev_des = list_entry(ptr, struct modac_vdev_des, mngdev_item);
		if(STRINGS_EQUAL(vdev_des->name, name)) {
			return vdev_des;
		}
	}

	return NULL;
}

static int list_create_vdev(struct mngdev_data *mngdev, u8 id, 
								struct modac_vdev_des **vdev_des)
{
	*vdev_des = (struct modac_vdev_des *)
			kmalloc(sizeof(struct modac_vdev_des), GFP_KERNEL);
			
	if(*vdev_des == NULL) return -ENOMEM;
	
	(*vdev_des)->id = id;
	(*vdev_des)->mngdev_des = mngdev->des;
	
	list_add(&(*vdev_des)->mngdev_item, &mngdev->vdev_list);
	
	return 0;
}
	
static void list_destroy_vdev(struct mngdev_data *mngdev, 
							  struct modac_vdev_des *vdev_des,
							  int modac_vdev_create_was_done
 							)
{
	if(modac_vdev_create_was_done) {
		mngdev_event_dispatch_list_remove_all(mngdev, vdev_des);
		modac_rm_free_owner(&mngdev->rm_data, vdev_des->id);
		modac_vdev_destroy(vdev_des);
	}
	
	list_del_init(&vdev_des->mngdev_item);
	kfree(vdev_des);
}

static void list_stop_and_destroy_vdevs(struct mngdev_data *mngdev)
{
	struct list_head *head = &mngdev->vdev_list;
	struct list_head *ptr;
	
	ptr = head->next;
	
	while(ptr != head) {
		
		struct modac_vdev_des *vdev_des;
		
		vdev_des = list_entry(ptr, struct modac_vdev_des, mngdev_item);
		
		/* retreive the next pointer before destroying */
		ptr = ptr->next;
		
		list_destroy_vdev(mngdev, vdev_des, 1);
	}
}




/***** VIRT_DEV resources  *****/

static int vdev_on_res_add(struct modac_vdev_des *vdev_des,
							  struct modac_rm_vres_desc *vres_desc)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	int res_count_for_type;
	int res_alloc_count;
	
	res_count_for_type = modac_rm_get_res_count_for_type(&mngdev->rm_data, vres_desc->type);
	if(res_count_for_type < 0) return res_count_for_type;
	
	if(vres_desc->index < 0 || vres_desc->index >= res_count_for_type) return -EINVAL;

	res_alloc_count = vdev_des->res_type_data[vres_desc->type].res_alloc_count;
	
	/* not enough place for new resources? */
	if(res_alloc_count >= MODAC_RES_PER_VIRT_DEV_MAX_COUNT) return -ENOSYS;
	
	vdev_des->res_type_data[vres_desc->type].res_alloc_index[res_alloc_count] = vres_desc->index;
	
	vdev_des->res_type_data[vres_desc->type].res_alloc_count ++;

	return 0;
}

static void mngdev_vdev_get_vres_desc(
					struct mngdev_data *mngdev,
					struct modac_vdev_des *vdev_des,
					struct mngdev_ioctl_hw_header_vres *vres_rel_on_vdev,
					struct modac_rm_vres_desc *vres_desc)
{
	int res_type_count = mngdev->hw_support_data.hw_res_def_count;
	int type = vres_rel_on_vdev->type;
	int index = vres_rel_on_vdev->index;
	
	if(type < 0 || type >= res_type_count) goto err;
	
	if(index < 0 || index >= vdev_des->res_type_data[type].res_alloc_count) {
		/* index out of range */
err:
		vres_desc->type = MODAC_RES_TYPE_NONE;
	} else {
		vres_desc->type = type;
		vres_desc->index = vdev_des->res_type_data[type].res_alloc_index[index];
	}
}


/*****  MNG_DEV file operations  *****/

static int mngdev_open(struct inode *inode, struct file *filp)
{
	struct mngdev_data *mngdev;
	int ret = 0;
	int minor = iminor(inode);
	int imngdev;
	
	if(minor % MINOR_MULTIPLICATOR != 0) {
		return -ENODEV;
	}
	
	imngdev = (minor - mngdev_first_minor) / MINOR_MULTIPLICATOR;
	
	if(imngdev < 0 || imngdev >= mngdev_table_size) {
		return -ENODEV;
	}
	
	
	/* Locate the device associated with the minor number.
	 * Look it up in the table; if found then obtain a
	 * reference (i.e., increment the reference count)
	 */
	mutex_lock(&mngdev_table_mutex);
	
	if(mngdev_table[imngdev].mngdev == NULL) {
		mutex_unlock(&mngdev_table_mutex);
		return -ENODEV;
	}
	
	mngdev = mngdev_table[imngdev].mngdev;
	
	ret = modac_mng_check_on_open(mngdev->des, inode);
	if(ret) {
		mutex_unlock(&mngdev_table_mutex);
		return ret;
	}
	
	filp->private_data = (void *)mngdev;

	mutex_unlock(&mngdev_table_mutex);
	
	

	devref_lock( &mngdev->ref );
	
	if(mngdev->pid != NO_PID) {
		ret = -EBUSY;
	} else {
		mngdev->pid = task_pid_nr(current);
	}
	
	devref_unlock( &mngdev->ref );
	
	return ret;
}

static int mngdev_release(struct inode *inode, struct file *filp)
{
	struct mngdev_data *mngdev = (struct mngdev_data *)filp->private_data;
	
	/* Release the reference to the device; 
	 * drvdat_put/devref_put expect that the
	 * reference is locked on entry...
	 */
	devref_lock( &mngdev->ref );

	mngdev->pid = NO_PID;
	
	/* If the reference count drops to zero
	 * then the MNG_DEV is destroyed.
	 */
	drvdat_put(mngdev, inode, mngdev->dev, NULL);
	
	return 0;
}

static long mngdev_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct mngdev_data *mngdev = (struct mngdev_data *)filp->private_data;
	int ret = 0;
	
	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;

	/* Check that cmd is valid */
	if (_IOC_TYPE(cmd) != MNG_DEV_IOC_MAGIC) {
		ret = -ENOTTY;
		goto bail;
	}
	
	/* Check access */
	if (_IOC_DIR(cmd) & _IOC_READ) {
		ret = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	} else if(_IOC_DIR(cmd) & _IOC_WRITE) {
		ret = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}
	
	if (ret) {
		ret = -EFAULT;
		goto bail;
	}
	
	if (_IOC_NR(cmd) <= MNG_DEV_IOC_MAX) {
		/*
		 * general range
		 */
	} else if (_IOC_NR(cmd) >= MNG_DEV_HW_IOC_MIN &&
			   _IOC_NR(cmd) <= MNG_DEV_HW_IOC_MAX) {
		/*
		 * HW specific range
		 */
		
		struct modac_rm_vres_desc res[2];
		struct mngdev_ioctl_hw_header header_args;
		struct modac_vdev_des *vdev_des;
		int i;
		
		// copy only the header part
		if (copy_from_user(&header_args, (void *)arg, sizeof(struct mngdev_ioctl_hw_header))) {
			ret = -EFAULT;
			goto bail;
		}
		
		vdev_des = list_find_vdev(mngdev, header_args.vdev_id);
		
		if(vdev_des != NULL && vdev_des->usage_counter > 0) {
			
			/*
			 * The MMG_DEV can't access resources of an open VIRT_DEV
			 */
			ret = -EACCES;
			goto bail;
		}

		for(i = 0; i < 2; i ++) {
			
			struct modac_rm_vres_desc *vres_desc = &res[i];
			struct mngdev_ioctl_hw_header_vres *vres = &header_args.vres[i];
			
			// only check the defined ones
			if(vres->type == MODAC_RES_TYPE_NONE) {
				vres_desc->type = MODAC_RES_TYPE_NONE;
				vres_desc->index = -1; // this will not be used anyway
				continue;
			}
			
			if(vdev_des == NULL) {
				vres_desc->type = MODAC_RES_TYPE_NONE;
				vres_desc->index = -1; // this will not be used anyway
			} else {
				mngdev_vdev_get_vres_desc(mngdev, vdev_des, vres, vres_desc);
			}

			ret = modac_rm_get_owner(&mngdev->rm_data, vres_desc);
			
			if(ret != header_args.vdev_id) {
				// the resource is not owned by the wanted instance
				ret = -EACCES;
				goto end2;
			}
		}

		if(mngdev->des->hw_support->ioctl == NULL) {
			ret = -ENOSYS;
		} else {
			ret = mngdev->des->hw_support->ioctl(&mngdev->hw_support_data, NULL, res, cmd, arg);
		}
		
	end2:
	
		goto bail;
	
	} else if (_IOC_NR(cmd) >= MNG_DEV_DBG_IOC_MIN &&
			   _IOC_NR(cmd) <= MNG_DEV_DBG_IOC_MAX) {
		/*
		 * dbg range
		 */
	} else {
		ret = -ENOTTY;
		goto bail;
	}
	
	ret = -ENOSYS; // not implemented by default
	
	switch(cmd) {
		
	case MNG_DEV_IOC_VIRT_DEV_FIND:
	{
		struct mngdev_ioctl_vdev_ids find_args;
		struct modac_vdev_des *vdev_des;
		u8 *arg_id = &((struct mngdev_ioctl_vdev_ids *)arg)->id;
		
		if (copy_from_user(&find_args, (void *)arg, sizeof(struct mngdev_ioctl_vdev_ids))) {
			ret = -EFAULT;
			goto bail;
		}
		
		vdev_des = list_find_vdev_by_name(mngdev, find_args.name);
		
		if(vdev_des == NULL) {
			find_args.id = 0;
		} else {
			find_args.id = vdev_des->id;
		}
		
        if (copy_to_user(arg_id, &find_args.id, sizeof(u8))) {
            ret = -EFAULT;
			goto bail;
        }
        
        ret = 0;
		
		break;
	}
		
	case MNG_DEV_IOC_CREATE:
	{
		struct mngdev_ioctl_vdev_ids create_args;
		struct modac_vdev_des *vdev_des;
		u8 vdev_id;
		
		if (copy_from_user(&create_args, (void *)arg, sizeof(struct mngdev_ioctl_vdev_ids))) {
			ret = -EFAULT;
			goto bail;
		}
		
		if(create_args.id < 0 || create_args.id > MAX_VIRT_DEVS_PER_MNG_DEV) {
			ret = -EINVAL;
			goto bail;
		}
		
		/*
		 * Check if the name is already in use for this MNG_DEV.
		 */
		vdev_des = list_find_vdev_by_name(mngdev, create_args.name);
		if(vdev_des != NULL) {
			ret = -EEXIST;
			printk(KERN_WARNING "Warning: "
				"The name for the virtual device '%s' exists already'\n", 
						create_args.name);
			goto done;
		}

		vdev_des = NULL;
		
		if(create_args.id == 0) {
			/*
			 * auto choose the first free
			 */
			
			/* all VIRT_DEVs can be exhausted */
			ret = -EMFILE;
			
			for(vdev_id = 1; vdev_id <= MAX_VIRT_DEVS_PER_MNG_DEV; vdev_id ++) {
				vdev_des = list_find_vdev(mngdev, vdev_id);
				if(vdev_des == NULL) {
					/* ok found */
					ret = 0;
					break;
				}
			}
		} else {
			vdev_des = list_find_vdev(mngdev, create_args.id);
			vdev_id = create_args.id;
		}
		
		if(ret) {
			/* do nothing, finish */
		} else if(vdev_des != NULL) {
			ret = -EEXIST;
		} else {
			ret = list_create_vdev(mngdev, vdev_id, &vdev_des);
			if(!ret) {
				
				int i;
				
				vdev_des->major = mngdev->des->major;
				vdev_des->minor = mngdev->des->minor + vdev_id;
				strncpy(vdev_des->name, create_args.name, MODAC_DEVICE_MAX_NAME + 1);

				for(i = 0; i < mngdev->hw_support_data.hw_res_def_count; i ++) {
					vdev_des->res_type_data[i].res_alloc_count = 0;
				}
				
				vdev_des->usage_counter = 0;
				vdev_des->leave_res_set_on_last_close = 0;

				ret = modac_vdev_create(vdev_des);
				if(ret) {
					// failed, clean up
					list_destroy_vdev(mngdev, vdev_des, 0);
				}
			}
		}
		
done:
	
		break;
	}
		
	case MNG_DEV_IOC_DESTROY:
	{
		struct mngdev_ioctl_destroy destroy_args;
		struct modac_vdev_des *vdev_des;
		
		if (copy_from_user(&destroy_args, (void *)arg, sizeof(struct mngdev_ioctl_destroy))) {
			ret = -EFAULT;
			goto bail;
		}
		
		vdev_des = list_find_vdev(mngdev, destroy_args.id);
		if(vdev_des == NULL) {
			ret = -ENODEV;
		} else {
			list_destroy_vdev(mngdev, vdev_des, 1);
			ret = 0;
		}
		
		break;
	}
	
	case MNG_DEV_IOC_ALLOC:
	{
		struct modac_vdev_des *vdev_des;
		
		struct mngdev_ioctl_res res_args;
		if (copy_from_user(&res_args, (void *)arg, sizeof(struct mngdev_ioctl_res))) {
			ret = -EFAULT;
			goto bail;
		}
		
		vdev_des = list_find_vdev(mngdev, res_args.id_vdev);
		
		if(vdev_des == NULL) {
			
			/*
			 * such a virt device not existing.
			 */
			ret = -ENXIO;
		
		} else if(vdev_des->usage_counter > 0) {
			
			/*
			 * can't change resources of an open VIRT_DEV
			 */
			ret = -EACCES;
			goto bail;
			
		} else {
			
			struct modac_rm_vres_desc vres_desc;

			ret = modac_rm_alloc(&mngdev->rm_data,
					res_args.id_vdev, // owner
					res_args.resource_id,
					res_args.resource_real_index,
					res_args.arg_filter,
					&vres_desc);
			
			if(ret >= 0) {
				vdev_on_res_add(vdev_des, &vres_desc);
			}
		}
		
		break;
	}
		
	case MNG_DEV_IOC_CONFIG:
	{
		
		struct mngdev_config mngdev_config;
		
		if(mngdev->des->io_start == NULL) {
			mngdev_config.io_memory_length = 0;
		} else {
			mngdev_config.io_memory_length = mngdev->des->io_size;
		}

		if (copy_to_user((void *)arg, &mngdev_config, sizeof(struct mngdev_config))) {
			ret = -EFAULT;
			goto bail;
		}
		
		ret = 0;
		
		break;
	}

	} // switch

bail:

	devref_unlock( &mngdev->ref );
	
	return ret;
}

static void mngdev_vma_open(struct vm_area_struct *vma)
{
}

static void mngdev_vma_close(struct vm_area_struct *vma)
{
}

static struct vm_operations_struct mngdev_vm_ops = {
	.open = mngdev_vma_open,
	.close = mngdev_vma_close,
};

static int mngdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct mngdev_data *mngdev = (struct mngdev_data *)filp->private_data;
	
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long physical;
	
	int ret;
	
	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;
	
	/*
	 * must not surpass the total IO space
	 */
	if(vsize > mngdev->des->io_size) {
		ret = -EINVAL;
		goto bail;
	}

	/*
	 * one IO region that must be marked as 0 is supported.
	 */
	if(offset != 0) {
		ret = -EINVAL;
		goto bail;
	}
	
	physical = (unsigned long)mngdev->des->io_start_phys;
	
	ret = io_remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT,
					vsize, vma->vm_page_prot);

	if (ret) {
		ret = -EAGAIN;
		goto bail;
	}

	vma->vm_ops = &mngdev_vm_ops;
	mngdev_vma_open(vma);

bail:

	devref_unlock( &mngdev->ref );
	
	return ret;  
}

static const struct file_operations mngdev_fops = {
	.owner = THIS_MODULE,
	.open = mngdev_open,
	.release = mngdev_release,
	.unlocked_ioctl = mngdev_unlocked_ioctl,
	.mmap = mngdev_mmap,
};



/*****  Local IRQ context support and functions  *****/

/* Called from an IRQ in a spin-locked context. */
static void irq_process(void *subscriber, void *arg_a)
{
	struct modac_vdev_des *vdev_des = (struct modac_vdev_des *)subscriber;
	struct irq_process_arg *arg = (struct irq_process_arg *)arg_a;

	if(arg->notify_only) {
		modac_vdev_notify(vdev_des, arg->event);
	} else {
		modac_vdev_put_cb(vdev_des, arg->event, arg->data, arg->length);
	}
}

/* Called from an IRQ. */
static void modac_mngdev_process_event(struct modac_mngdev_des *devdes, int notify_only,
	int event, void *data, int length)
{
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	struct irq_process_arg arg;
	
	if(event >= 0 && event < MAX_COUNTED_EVENTS) {
		atomic_inc(&mngdev->event_counters[event]);
	}

	arg.notify_only = notify_only;
	arg.event = event;
	arg.data = data;
	arg.length = length;

	/* 
	 * copy the event everywhere
	 */
	dev_spin_lock(mngdev);

	event_dispatch_list_for_all_subscribers(&mngdev->event_dispatch_list, event,
											irq_process, &arg);

	dev_spin_unlock(mngdev);
}

/*****  Local hot-unplug support functions  *****/

/* Remove all mappings that are held by an inode.
 * We keep track of all open inodes that reference
 * a single device (there could be multiple device
 * special files, i.e. multiple inodes per device).
 * When we remove the device we zap all mappings
 * from all inodes.
 */ 
static inline int
unmap_inode(struct inode *inod, void *closure)
{
	unmap_mapping_range( inod->i_mapping, 0, 0, 1 );
	return 0;
}

static inline void app_kill(struct mngdev_data *mngdev)
{
	if(mngdev->pid != NO_PID) {
		kill_pid(find_vpid(mngdev->pid), SIGKILL, 1);
	}
}

static void inline mngdev_destroy_now(struct mngdev_data *mngdev)
{
	list_stop_and_destroy_vdevs(mngdev);
	cleanup(mngdev, CLEAN_ALL);
}

/*****  MNG_DEV sysfs support  *****/

static ssize_t show_alloc(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct mngdev_data *mngdev = dev_get_drvdata(dev);
	ssize_t ret;

	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;
	
	ret = modac_rm_print_info(&mngdev->rm_data, buf, PAGE_SIZE, 0, 200);
	
	devref_unlock( &mngdev->ref );
	
	return ret;
}

static ssize_t store_dbg(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct mngdev_data *mngdev = dev_get_drvdata(dev);
	ssize_t ret;
	
	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;
	
	if(mngdev->des->hw_support->store_dbg == NULL) {
		count = -ENOSYS;
	} else {
		count = mngdev->des->hw_support->store_dbg(&mngdev->hw_support_data, buf, count);
	}
	
	devref_unlock( &mngdev->ref );
		
	return count;
}

static ssize_t show_dbg(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct mngdev_data *mngdev = dev_get_drvdata(dev);
	ssize_t ret;

	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;
	
	if(mngdev->des->hw_support->show_dbg == NULL) {
		ret = -ENOSYS;
	} else {
		ret = mngdev->des->hw_support->show_dbg(&mngdev->hw_support_data,
				buf, PAGE_SIZE);
	}
	
	devref_unlock( &mngdev->ref );
	
	return ret;
}

static ssize_t store_hw_regs(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct mngdev_data *mngdev = dev_get_drvdata(dev);
	ssize_t ret;
	
	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;
	
	sscanf(buf, "%x %d", &mngdev->regs_offset, &mngdev->regs_length);
	
	devref_unlock( &mngdev->ref );

	return count;
}

static ssize_t show_hw_regs(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct mngdev_data *mngdev = dev_get_drvdata(dev);
	ssize_t ret;

	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;
	
	if(mngdev->des->hw_support->dbg_regs != NULL) {
		ret = mngdev->des->hw_support->dbg_regs(&mngdev->hw_support_data,
				mngdev->regs_offset, mngdev->regs_length,
				buf, PAGE_SIZE);
	} else {
		ret = scnprintf(buf, PAGE_SIZE, "X");
	}
	
	devref_unlock( &mngdev->ref );
	
	return ret;
}

static ssize_t show_hw_info(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct mngdev_data *mngdev = dev_get_drvdata(dev);
	struct modac_hw_support_data *hw_support_data = &mngdev->hw_support_data;
	int res_type_count = hw_support_data->hw_res_def_count;
	ssize_t n = 0;
	int count = PAGE_SIZE;
	int i, j;
	ssize_t ret;

	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;
	
	if(mngdev->des->hw_support->dbg_info != NULL) {
		n += mngdev->des->hw_support->dbg_info(hw_support_data, 
				buf + n, count - n);
		n += scnprintf(buf + n, count - n, ", HW: %s\n", mngdev->des->underlying_info);
	} else {
		n += scnprintf(buf + n, count - n, "X\n");
	}
	
	for(i = 0; i < res_type_count; i ++) {
		int res_count = modac_rm_get_res_count_for_type(&mngdev->rm_data, i);
		
		n += scnprintf(buf + n, count - n, "\n%s:", hw_support_data->hw_res_defs[i].name);
		
		for(j = 0; j < res_count; j ++) {
			n += scnprintf(buf + n, count - n, "\n");
			
			if(hw_support_data->hw_support->dbg_res != NULL) {
				n += hw_support_data->hw_support->dbg_res(hw_support_data, 
						buf + n, count - n, i, j);
			} else {
				n += scnprintf(buf + n, count - n, "X");
			}
		}
	}
	
	devref_unlock( &mngdev->ref );
	
	return n;
}

static ssize_t show_events(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct mngdev_data *mngdev = dev_get_drvdata(dev);
	int i;
	ssize_t n = 0;
	ssize_t ret;
	
	ret = mngdev_devref_lock(mngdev);
	if(ret)
		return ret;
	
	for(i = 0; i < MAX_COUNTED_EVENTS; i ++) {
		if(i % 16 == 0) {
			n += scnprintf(buf + n, PAGE_SIZE - n, "%3.3d: ", i);
		}
		n += scnprintf(buf + n, PAGE_SIZE - n, "%d ", atomic_read(&mngdev->event_counters[i]));
		if(i % 16 == 15) {
			n += scnprintf(buf + n, PAGE_SIZE - n, "\n");
		}
	}
	
	devref_unlock( &mngdev->ref );

	return n;
}

/*
 * NOTE: when this table is changed, the attrs_misc must be changed as well
 */
static struct device_attribute dev_attr_misc[] = {
	__ATTR(alloc, S_IRUGO, show_alloc, NULL),
	__ATTR(dbg, S_IRUGO | S_IWUGO, show_dbg, store_dbg),
	__ATTR(regs, S_IRUGO | S_IWUGO, show_hw_regs, store_hw_regs),
	__ATTR(events, S_IRUGO, show_events, NULL),
	__ATTR(hw_info, S_IRUGO, show_hw_info, NULL),

	__ATTR_NULL
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,00)
static struct attribute *attrs_misc[] = {
	&dev_attr_misc[0].attr,
	&dev_attr_misc[1].attr,
	&dev_attr_misc[2].attr,
	&dev_attr_misc[3].attr,
	&dev_attr_misc[4].attr,
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



/*****  General MNG_DEV functions, called from both MNG_DEV and VIRT_DEV *****/

int modac_mng_check_on_open(struct modac_mngdev_des *devdes, struct inode *inode)
{
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	int ret = 0;
	
	/* increment reference count */
	if ( ! (ret = devref_get( &mngdev->ref, inode )) ) {
		devref_unlock( &mngdev->ref );
	}
		
	return ret;
}


/*****  MNG_DEV functions called from HW in IRQ context *****/


void modac_mngdev_put_event(struct modac_mngdev_des *devdes,
	int event, void *data, int length
)
{
	modac_mngdev_process_event(devdes, 0, event, data, length);
}

void modac_mngdev_notify(struct modac_mngdev_des *devdes, int event)
{
	modac_mngdev_process_event(devdes, 1, event, NULL, 0);
}





/*****  VIRT_DEV calls to the MNG_DEV  *****/



void modac_c_vdev_spin_lock(struct modac_vdev_des *vdev_des)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	
	return dev_spin_lock(mngdev);
}

void modac_c_vdev_spin_unlock(struct modac_vdev_des *vdev_des)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	
	dev_spin_unlock(mngdev);
}

int modac_c_vdev_devref_lock(struct modac_vdev_des *vdev_des)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	
	return mngdev_devref_lock(mngdev);
}

void modac_c_vdev_devref_unlock(struct modac_vdev_des *vdev_des)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	
	mngdev_devref_unlock(mngdev);
}

int modac_c_vdev_on_open(struct modac_vdev_des *vdev_des, struct inode *inode)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	int ret = 0;

	ret = modac_mng_check_on_open(mngdev->des, inode);
	if(ret) {
		return ret;
	}
	
	devref_lock( &mngdev->ref );
	
	vdev_des->usage_counter ++;
		
	devref_unlock( &mngdev->ref );
	
	return ret;
}

void modac_c_vdev_init_res(struct modac_vdev_des *vdev_des)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	struct modac_hw_support_data *hw_support_data = 
				&mngdev->hw_support_data;
	int res_type_count = hw_support_data->hw_res_def_count;
	int i, j;
	
	/*
	* Initialize all the resources on the last VIRT_DEV close because:
	* - on open we come to a fresh start.
	* - the device doesn't do stupid things while it's closed.
	*/

	for(i = 0; i < res_type_count; i ++) {
		int res_alloc_count = vdev_des->res_type_data[i].res_alloc_count;
		for(j = 0; j < res_alloc_count; j ++) {
			hw_support_data->hw_support->init_res(hw_support_data,
					i, vdev_des->res_type_data[i].res_alloc_index[j]);
		}
	}
}

#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER

u32 mng_dbg_get_time(struct modac_vdev_des *vdev_des)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	struct modac_hw_support_data *hw_support_data = 
				&mngdev->hw_support_data;
				
	return dbg_get_time(hw_support_data);
}

#endif



void modac_c_vdev_on_close(struct modac_vdev_des *vdev_des, struct inode *inode, struct device *vdev_dev)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	
	/* Release the reference to the device; 
	 * drvdat_put/devref_put expect that the
	 * reference is locked on entry...
	 */
	devref_lock( &mngdev->ref );
	
	/*
	 * This is to check if the VIRT_DEV is not open by any application. Needed
	 * for HW init purposes.
	 * This usage counter has nothing to do with the devref management.
	 */
	vdev_des->usage_counter --;
	
	if(vdev_des->usage_counter < 1) {
		
		if(!vdev_des->leave_res_set_on_last_close) {
			
			/*
			* Only call the final cleanup if the device is still present otherwise
			* an error will happen if the IO space tries to be accessed.
			*/
			if(devref_ptr(&mngdev->ref) != NULL) {
				
				modac_c_vdev_init_res(vdev_des);
			}
		}
		
		mngdev_event_dispatch_list_remove_all(mngdev, vdev_des);
	}

	/* If the reference count drops to zero
	 * then the MNG_DEV is destroyed (together with the VIRT_DEVs).
	 */
	drvdat_put(mngdev, inode, vdev_dev, vdev_des);
	
}

int modac_c_vdev_do_ioctl(
		struct modac_vdev_des *vdev_des,
		struct mngdev_ioctl_hw_header_vres *vres,
		unsigned int cmd, unsigned long arg)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	struct modac_rm_vres_desc res;
	int ret;
	
	// only check the resource if defined
	if(vres->type != MODAC_RES_TYPE_NONE) {
	
		mngdev_vdev_get_vres_desc(mngdev, vdev_des,
					vres, &res);
		ret = modac_rm_get_owner(&mngdev->rm_data, &res);
		if(ret != vdev_des->id) {
			// the resource is not owned by the wanted instance
			ret = -EACCES;
			goto bail;
		}
	}

	if(devdes->hw_support->ioctl == NULL) {
		ret = -ENOSYS;
	} else {
		ret = devdes->hw_support->ioctl(
			&mngdev->hw_support_data, vdev_des, &res, cmd, arg);
	}
	
bail:

	return ret;
}

int modac_c_vdev_do_direct_ioctl(
		struct modac_vdev_des *vdev_des,
		unsigned int cmd, unsigned long arg)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	int ret;
	
	if(devdes->hw_support->direct_ioctl == NULL) {
		ret = -ENOSYS;
	} else {
		ret = devdes->hw_support->direct_ioctl(
			&mngdev->hw_support_data, cmd, arg);
	}
	
	return ret;
}

int modac_c_vdev_subscribe(struct modac_vdev_des *vdev_des,
					int event, 
					u8 action)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;

	int ret = 0;
	
	dev_spin_lock(mngdev);
	
	switch(action) {
	case VIRT_DEV_IOCTL_SUBSCRIBE_ACTION_SUBSCRIBE:
		ret = event_dispatch_list_add(&mngdev->event_dispatch_list, vdev_des, event);
		break;
	case VIRT_DEV_IOCTL_SUBSCRIBE_ACTION_UNSUBSCRIBE:
		event_dispatch_list_remove(&mngdev->event_dispatch_list, vdev_des, event);
		break;
	case VIRT_DEV_IOCTL_SUBSCRIBE_ACTION_CLEAR:
		event_dispatch_list_remove_all(&mngdev->event_dispatch_list, vdev_des);
		break;
	}
	dev_spin_unlock(mngdev);
	
	if(ret)
		return ret;
	
	ret = on_subscribe_change(mngdev);
	
	return ret;
}

int modac_c_vdev_get_res_status(
		struct modac_vdev_des *vdev_des,
		struct vdev_ioctl_res_status *res_status)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	int res_type_count = mngdev->hw_support_data.hw_res_def_count;
	if(res_status->res_type < 0 || res_status->res_type >= res_type_count) return -EINVAL;
	
	res_status->count = vdev_des->res_type_data[res_status->res_type].res_alloc_count;
	
	return 0;
}

int modac_c_vdev_mmap_ro(
		struct modac_vdev_des *vdev_des,
		unsigned long offset,
		unsigned long vsize,
		unsigned long *physical)
{
	int ret;
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	
	if(devdes->hw_support->vdev_mmap_ro == NULL) {
		return -ENOSYS;
	}

	ret = devdes->hw_support->vdev_mmap_ro(
			&mngdev->hw_support_data, offset, vsize, physical);
	
	return ret;
}

ssize_t modac_c_vdev_show_config(
		struct modac_vdev_des *vdev_des,
		struct device_attribute *attr,
		char *buf)
{
	struct modac_mngdev_des *devdes = vdev_des->mngdev_des;
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	struct modac_hw_support_data *hw_support_data = 
				&mngdev->hw_support_data;
	int res_type_count = hw_support_data->hw_res_def_count;
	int i, j;
	ssize_t n = 0;
	int count = PAGE_SIZE;

	for(i = 0; i < res_type_count; i ++) {
		int res_alloc_count = vdev_des->res_type_data[i].res_alloc_count;
		
		n += scnprintf(buf + n, count - n, "%d[", i);
		
		for(j = 0; j < res_alloc_count; j ++) {
			n += scnprintf(buf + n, count - n, "%d{", j);
			
			if(hw_support_data->hw_support->dbg_res != NULL) {
				n += hw_support_data->hw_support->dbg_res(hw_support_data, 
						buf + n, count - n, i, vdev_des->res_type_data[i].res_alloc_index[j]);
			} else {
				n += scnprintf(buf + n, count - n, "X");
			}
			
			n += scnprintf(buf + n, count - n, "}");
		}
		
		n += scnprintf(buf + n, count - n, "]");
	}
	
	n += scnprintf(buf + n, count - n, ", ");
	
	n += mng_event_dispatch_list_vdev_dbg(buf + n, count - n, mngdev, vdev_des);
	
	n += scnprintf(buf + n, count - n, ", res_set_cfg=%d", vdev_des->leave_res_set_on_last_close);
	
	return n;
}



/*****  MNG_DEV init/fini, create/destroy, isr  *****/



/* 
 * The calling system must make sure the 'modac_mngdev_destroy' happens only 
 * after the 'modac_mngdev_create' finishes.
 */
int modac_mngdev_create(struct modac_mngdev_des *devdes)
{
	int ret, imngdev;
	
	struct mngdev_data *mngdev;
	
	imngdev = (devdes->minor - mngdev_first_minor) / MINOR_MULTIPLICATOR;
	
	if (imngdev < 0 || imngdev >= mngdev_table_size)
		return -ENOTSUPP;
	
	devdes->priv = NULL;

	mngdev = kmalloc(sizeof(struct mngdev_data), GFP_ATOMIC);
	if(mngdev == NULL) return -ENOMEM;
	
	init_dev(mngdev);
	
	/* init here, hw_support->init() will already need this: */
	mngdev->hw_support_data.mngdev_des = devdes;
	
	ret = devdes->hw_support->init(&mngdev->hw_support_data);
	if(ret) {
		cleanup(mngdev, CLEAN_PRIV);
		return ret;
	}
	
	ret = modac_rm_init(&mngdev->hw_support_data, &mngdev->rm_data);
	
	if(ret) {
		cleanup(mngdev, CLEAN_HW);
		return ret;
	}
	
	mngdev->hw_support_data.rm_data = &mngdev->rm_data;
	mngdev->hw_support_data.hw_support = devdes->hw_support;
	
	mngdev->devt = MKDEV(devdes->major, devdes->minor);
	mngdev->des = devdes;

	mngdev->dev = device_create(modac_mngdev_class, NULL, mngdev->devt, mngdev, devdes->name);
	if (IS_ERR(mngdev->dev)) {
		printk(KERN_ERR "%s <dev>: Failed to create device!\n", devdes->name);
		ret = PTR_ERR(mngdev->dev);
		cleanup(mngdev, CLEAN_RM);
		return ret;
	}
	
	/* Enter the newly bound device info into the table so that it can
	 * be found by 'open'...
	 */
	mutex_lock(&mngdev_table_mutex);
	
	if (mngdev_table[imngdev].mngdev != NULL) {
		mutex_unlock(&mngdev_table_mutex);
		/* PANIC */
		printk(KERN_ERR "MNG_DEV INTERNAL ERROR -- TABLE SLOT NOT EMPTY\n");
		ret = -EEXIST;
	} else {
		mngdev_table[imngdev].mngdev = mngdev;
	}
	
	mutex_unlock(&mngdev_table_mutex);

	if(ret) {
		cleanup(mngdev, CLEAN_DEV);
		return ret;
	}
	
	devdes->priv = (void *)mngdev;
	
	{
		// starts with no events
		struct event_list_type subscriptions;
		event_list_clear(&subscriptions);
		devdes->hw_support->on_subscribe_change(&mngdev->hw_support_data, &subscriptions);
	}
		
	return 0;
}

void modac_mngdev_destroy(struct modac_mngdev_des *devdes)
{
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	int imngdev = (devdes->minor - mngdev_first_minor) / MINOR_MULTIPLICATOR;
	
	/* remove the 'mngdev' from the lookup table
	 * so that future 'open' won't find it.
	 * After removing it the reference count
	 * can only drop but never increase.
	 */
	mutex_lock(&mngdev_table_mutex);
	
	if(mngdev_table[imngdev].mngdev != mngdev) {
		printk(KERN_WARNING "Strange value in the mng dev slot[%d]: 0x%x != 0x%x\n", 
			   imngdev, (int)(size_t)mngdev_table[imngdev].mngdev, (int)(size_t)mngdev);
	}
	
	mngdev_table[imngdev].mngdev = NULL;
	
	/* Also erase all the VIRT_DEVs' slots so that future 'open's won't find them.*/
	modac_vdev_table_reset(devdes->minor);

	mutex_unlock(&mngdev_table_mutex);
	
	/* must lock the reference
	 */
	devref_lock(&mngdev->ref);

	/* 
	 * First kill the application that may have opened the MNG_DEV.
	 */
	app_kill(mngdev);
	
	/* mark as invalid
	 */
	devref_invalidate(&mngdev->ref);

	/* remove any mappings
	 */
	devref_forall_inodes(&mngdev->ref, unmap_inode, mngdev);

	printk(KERN_INFO "Unbounding the device '%s'\n", devdes->name);
	
	/* drop reference count; if nobody holds the char-device
	 * open then the 'mngdev' is free'd right here.
	 */
	drvdat_put(mngdev, NULL, NULL, NULL);

	/* At this point, future file-operations on any (already
	 * open) file which holds a reference to the unbound device
	 * should fail and all mappings of registers have disappeared.
	 */

}

/* Called in the IRQ context. */
irqreturn_t modac_mngdev_isr(struct modac_mngdev_des *devdes, void *data)
{
	struct mngdev_data *mngdev = (struct mngdev_data *)devdes->priv;
	
	return mngdev->des->hw_support->isr(&mngdev->hw_support_data, data);
}


static void cleanup_sys(int what)
{
	int i;
	
	switch(what) {
		
	case CLEAN_SYS_VDEV:
		
		modac_vdev_fini();
	
	case CLEAN_SYS_CDEV:
	
		for(i = 0; i < mngdev_table_size; i ++) {
			if(mngdev_table[i].have_cdev) {
				cdev_del(&mngdev_table[i].cdev);
			}
		}
	
	case CLEAN_SYS_TABLE:
	
		kfree(mngdev_table);
		
	case CLEAN_SYS_CLASS:

		class_destroy(modac_mngdev_class);
	}
}

int modac_mngdev_init(dev_t dev_num, int count)
{
	int ret, i;
	int dev_major = MAJOR(dev_num);
	
	mngdev_first_minor = MINOR(dev_num);
	
	/*
	 * The region must start aligned 
	 */
	if(mngdev_first_minor % MINOR_MULTIPLICATOR != 0) {
		return -EINVAL;
	}
	
	/*
	 * And must contain a whole number of MNG_DEV slots.
	 */
	if(count % MINOR_MULTIPLICATOR != 0) {
		return -EINVAL;
	}
	
	mutex_init(&mngdev_table_mutex);
	
	modac_mngdev_class = class_create(THIS_MODULE, MODAC_MNG_CLASS_NAME);
	if (IS_ERR(modac_mngdev_class)) {
		printk(KERN_ERR "%s <init>: Failed to create device class!\n", MODAC_MNG_CLASS_NAME);
		return PTR_ERR(modac_mngdev_class);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,12,00)
	modac_mngdev_class->dev_attrs = dev_attr_misc;
#else
	modac_mngdev_class->dev_groups = dev_groups_misc;
#endif
	
	mngdev_table_size = count / MINOR_MULTIPLICATOR;
	mngdev_table = kzalloc(
			sizeof(struct mngdev_table_item) * mngdev_table_size, GFP_ATOMIC);
	
	if(mngdev_table == NULL) {
		cleanup_sys(CLEAN_SYS_CLASS);
		return -ENOMEM;
	}
	
	ret = 0;
	
	for(i = 0; i < mngdev_table_size; i ++) {
		
		cdev_init(&mngdev_table[i].cdev, &mngdev_fops);
		mngdev_table[i].cdev.owner = THIS_MODULE;
		
		ret = cdev_add(&mngdev_table[i].cdev, MKDEV(dev_major, i * MINOR_MULTIPLICATOR), 1);
		if (ret) {
			printk(KERN_ERR "<MNG_DEV>: Failed to add cdev structure!\n");
			cleanup_sys(CLEAN_SYS_TABLE);
			break;
		}
		
		mngdev_table[i].have_cdev = 1;
	}
	
	/* 
	 * It can happen that not all the slots were initialized.
	 */
	if(ret) {
		cleanup_sys(CLEAN_SYS_CDEV);
		return ret;
	}
	
	ret = modac_vdev_init(dev_num, count);
	if(ret < 0) {
		cleanup_sys(CLEAN_SYS_CDEV);
		return ret;
	}
	
	return 0;
}

void modac_mngdev_fini(void)
{
	cleanup_sys(CLEAN_SYS_ALL);
}

