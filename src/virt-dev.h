#ifndef MODAC_VIRT_DEV_H_
#define MODAC_VIRT_DEV_H_

#include "rm.h"

/**
 * The maximal possible number of allocated resources for one type in the VIRT_DEV. 
 * This is limited so we can use fixed tables without list & kalloc.
 */
#define MODAC_RES_PER_VIRT_DEV_MAX_COUNT 32

struct modac_vdev_des {
	
	u8 id;
	int major;
	int minor;

	/** The name of the Linux dev node. */
	char name[MODAC_DEVICE_MAX_NAME + 1];
	
	// The parent MNG_DEV
	struct modac_mngdev_des *mngdev_des;
	
	// to be able to put this into the MNG_DEV list; only access in MNG_DEV!
	struct list_head mngdev_item;
	
	/* This is only accessible by the MNG_DEV */
	struct res_type_data {
		
		/** The current number of allocated resources */
		int res_alloc_count;
		/** The currently allocated resources */
		int res_alloc_index[MODAC_RES_PER_VIRT_DEV_MAX_COUNT];
		
	} res_type_data[MODAC_RES_TYPE_MAX_COUNT];
	
	/*
	 * This status is only needed to be able to inform the MNG_DEV if the VIRT_DEV
	 * is open by any application. Not related to devref!
	 */
	int usage_counter;
	
	atomic_t activeReaderCount;
	atomic_t readDenied;
	
	/*
	 * If non-zero the HW will not be cleared after the last VIRT_DEV close().
	 */
	int leave_res_set_on_last_close;

	/** Private data for dev. */
	void *priv;

};

int modac_vdev_create(
					/** device_des must remain allocated after this call */
					struct modac_vdev_des *vdev_des
					);

void modac_vdev_destroy(struct modac_vdev_des *vdev_des);

void modac_vdev_notify(struct modac_vdev_des *vdev_des, int event);
void modac_vdev_put_cb(struct modac_vdev_des *vdev_des, int event, void *data, int length);

void modac_vdev_table_reset(int mngdev_minor);

int modac_vdev_init(dev_t dev_num, int count);
void modac_vdev_fini(void);


#endif /* MODAC_VIRT_DEV_H_ */
