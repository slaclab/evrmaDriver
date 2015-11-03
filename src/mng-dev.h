#ifndef MODAC_MNG_DEV_H_
#define MODAC_MNG_DEV_H_

#include <linux/interrupt.h>


#include "hw-support.h"
#include "rm.h"

struct modac_io_rw_plugin {
	u16 (*read_u16)(struct modac_mngdev_des *devdes, u32 offset);
	void (*write_u16)(struct modac_mngdev_des *devdes, u32 offset, u16 value);
	u32 (*read_u32)(struct modac_mngdev_des *devdes, u32 offset);
	void (*write_u32)(struct modac_mngdev_des *devdes, u32 offset, u32 value);
};


struct modac_mngdev_des {
	
	int major;
	int minor;
	
	/** 
	The pointer to the IO start for accessing the data (obtained by ioremap_nocache())
	*/
	void *io_start;
	
	/** 
	The pointer to the IO used for mmaping (obtained by, for example, pci_resource_start())
	*/
	u32 io_start_phys;
	
	resource_size_t io_size;
	
	/** The name of the Linux dev node. */
	char name[MODAC_DEVICE_MAX_NAME + 1];
	
	const struct modac_hw_support_def *hw_support;
	
	/** 
	 * An additional info that the calling system might have given with this parameter.
	 * The meaning is HW dependent.
	 * Example: for EVR this is the EVR_TYPE_XXX as found out by the PCI.
	 */
	int hw_support_hint1;
	
	/** The functions to access the IO memory, provided by the BSP. */
	struct modac_io_rw_plugin *io_rw;
	
	/** A bit of info from the underlying HW layer. */
	const char *underlying_info;
	
	/** This is a callback to the calling system which tells if the irq should be
	enabled at all. */
	void (*irq_set)(struct modac_mngdev_des *mngdev_des, int enabled);
	
	/** Private data for dev. */
	void *priv;
	
};

/*****  General MNG_DEV functions, called from both MNG_DEV and VIRT_DEV *****/

int modac_mng_check_on_open(struct modac_mngdev_des *devdes, struct inode *inode);

/*****  MNG_DEV functions called from HW in IRQ context *****/

/* The data length is limited to CBUF_EVENT_ENTRY_DATA_LENGTH (= 12 bytes). */
void modac_mngdev_put_event(struct modac_mngdev_des *devdes, int event, void *data, int length);

void modac_mngdev_notify(struct modac_mngdev_des *devdes, int event);


/*****  VIRT_DEV calls to the MNG_DEV  *****/

int modac_c_vdev_on_open(struct modac_vdev_des *vdev_des, struct inode *inode);

void modac_c_vdev_on_close(struct modac_vdev_des *vdev_des, struct inode *inode, struct device *vdev_dev);

int modac_c_vdev_devref_lock(struct modac_vdev_des *vdev_des);
void modac_c_vdev_devref_unlock(struct modac_vdev_des *vdev_des);

void modac_c_vdev_spin_lock(struct modac_vdev_des *vdev_des);
void modac_c_vdev_spin_unlock(struct modac_vdev_des *vdev_des);

int modac_c_vdev_do_ioctl(
		struct modac_vdev_des *vdev_des,
		struct mngdev_ioctl_hw_header_vres *vres,
		unsigned int cmd, unsigned long arg);

int modac_c_vdev_subscribe(struct modac_vdev_des *vdev_des, 
					int event, 
					// one of VIRT_DEV_IOCTL_SUBSCRIBE_ACTION_...
					u8 action);

/* 
 * Initialze the resources to the known state.
 * 
 * Must be called in the devref protected section with the active HW.
 */
void modac_c_vdev_init_res(struct modac_vdev_des *vdev_des);

int modac_c_vdev_get_res_status(
		struct modac_vdev_des *vdev_des,
		struct vdev_ioctl_res_status *res_status);

int modac_c_vdev_mmap_ro(
		struct modac_vdev_des *vdev_des,
		unsigned long offset,
		unsigned long vsize,
		unsigned long *physical);

ssize_t modac_c_vdev_show_config(
		struct modac_vdev_des *vdev_des,
		struct device_attribute *attr,
		char *buf);

/*****  MNG_DEV init/fini, create/destroy, isr  *****/

int modac_mngdev_create(
					/** device_des must remain allocated after this call */
					struct modac_mngdev_des *devdes
					);

void modac_mngdev_destroy(struct modac_mngdev_des *devdes);

/** the modac loader must know how the interrupts are assigned and must
call the appropriate device to handle it. */
irqreturn_t modac_mngdev_isr(struct modac_mngdev_des *devdes, void *data);


int modac_mngdev_init(dev_t dev_num, int count);
void modac_mngdev_fini(void);



#endif /* MODAC_MNG_DEV_H_ */
