#ifndef LINUX_MODAC_H_
#define LINUX_MODAC_H_

/** @file */

/**
 * @defgroup g_linux_modac_gen_api Linux MODAC General API
 *
 * @short Common MODAC definitions for kernel and user space.
 *
 * @{
 */

/**
 * @short The length of MODAC IDs. 
 * 
 * Together with one terminating zero = 32 bytes
 */
#define MODAC_ID_MAX_NAME 31

/**
 * @short The read event in case the read queue in the kernel overflow happened
 */
#define MODAC_EVENT_READ_OVERFLOW 0x8000

/**
 * @short Used to denote an invalid virtual resource type.
 */
#define MODAC_RES_TYPE_NONE (-1)

/**
 * A structure to declare one virtual resource definition for
 * IOCTL calls.
 */
struct mngdev_ioctl_hw_header_vres {
	/**
	 * The resource's type (see EVR_RES_TYPE_XXX for EVR). Can be MODAC_RES_TYPE_NONE.
	 */
	int type;
	/**
	 * The resource's VIRT_DEV relative index. 
	 */
	int index;
};



// ================= MNG DEV ===================================================

/* Pick a free magic number according to Documentation/ioctl/ioctl-number.txt. */
#define MNG_DEV_IOC_MAGIC 	0xF0

/**
 * The mandatory header for all IOCTL HW support structures where the MNG_DEV accesses 
 * the resources of a given VIRT_DEV.
 */
struct mngdev_ioctl_hw_header {
	/**
	 * The id of the VIRT_DEV to be altered. Can by any value if both resources
	 * are defined as MODAC_RES_TYPE_NONE.
	 */
	int vdev_id;
	/**
	 * The two possible resource arguments
	 */
	struct mngdev_ioctl_hw_header_vres vres[2];
};

/**
 * The VIRT_DEV ID data (used as a data for the MNG_DEV_IOC_CREATE and
 * MNG_DEV_IOC_VIRT_DEV_FIND IOCTL calls).
 */
struct mngdev_ioctl_vdev_ids {
	/**
	 * Unique VIRT_DEV id for further reference (1..31), 0 for auto choosing.
	 */
	uint8_t id;
	/**
	 * the device name
	 */
	char name[MODAC_ID_MAX_NAME + 1];
};

/**
 * The data for the MNG_DEV_IOC_DESTROY IOCTL call.
 */
struct mngdev_ioctl_destroy {
	/**
	 * Unique VIRT_DEV id.
	 */
	uint8_t id;
};

#define MNG_DEV_IOCTL_RES_ARG_FILTER_MAX_COUNT 8

/**
 * The data for the MNG_DEV_IOC_ALLOC IOCTL call.
 */
struct mngdev_ioctl_res {
	/**
	 * Unique VIRT_DEV id.
	 */
	uint8_t id_vdev;
	
	/**
	 * The resource id as denfined in HW support.
	 */
	char resource_id[MODAC_ID_MAX_NAME + 1];
	
	/**
	 * To allocate on real index, MODAC_RM_ALLOC_FROM_POOL for pool allocation.
	 */
	int resource_real_index;
	
	/**
	 * Optional filtering arguments, depend on resource type in HW support.
	 */
	int arg_filter[MNG_DEV_IOCTL_RES_ARG_FILTER_MAX_COUNT]; 
};

/**
 * The data for the MNG_DEV_IOC_CONFIG IOCTL call.
 */
struct mngdev_config {
	/**
	 * the length of the mmap-able IO memory; zero means none available.
	 */
	int io_memory_length;
};



/* --------- general ioctls ------------ */

/**
 * Creates a new virtual EVR. Uses 'id' and 'name' from the struct mngdev_ioctl_vdev_ids.
 */
#define MNG_DEV_IOC_CREATE			_IOW(MNG_DEV_IOC_MAGIC, 1, struct mngdev_ioctl_vdev_ids)

/**
 * Destroys a virtual EVR.
 */
#define MNG_DEV_IOC_DESTROY			_IOW(MNG_DEV_IOC_MAGIC, 2, struct mngdev_ioctl_destroy)

/**
 * Allocates a resource for the virtual EVR.
 */
#define MNG_DEV_IOC_ALLOC			_IOW(MNG_DEV_IOC_MAGIC, 3, struct mngdev_ioctl_res)

/**
 * Finds the VIRT_DEV by its name. Uses 'name' from the struct mngdev_ioctl_vdev_ids
 * and returns its 'id' (0 if not found).
 */
#define MNG_DEV_IOC_VIRT_DEV_FIND \
		_IOWR(MNG_DEV_IOC_MAGIC, 4, struct mngdev_ioctl_vdev_ids)

/**
 * Obtains the configuration of the EVR card.
 */
#define MNG_DEV_IOC_CONFIG \
		_IOWR(MNG_DEV_IOC_MAGIC, 5, struct mngdev_config)


#define MNG_DEV_IOC_MAX  		5



/* --------- HW (EVR) specific ioctl ------------ */

/*
 * Must have its own range; the actuall IOCTLS will be defined in linux-evrma.h
 */
#define MNG_DEV_HW_IOC_MIN  		128
#define MNG_DEV_HW_IOC_MAX  		239


/* ---------- dbg ioctls ------------ */

#define MNG_DEV_DBG_IOC_MIN  		240
#define MNG_DEV_DBG_IOC_MAX  		240





/* ================= VIRT DEV =============================================== */

/**
 * The mandatory header for all the HW support structures of the VIRT_DEV IOCTLs.
 */
struct vdev_ioctl_hw_header {
	/**
	 * The one possible resource argument 
	 */
	struct mngdev_ioctl_hw_header_vres vres;
};

/**
 * Subscribe actions.
 */
enum {
	/**
	 * Subscription for one event.
	 */
	VIRT_DEV_IOCTL_SUBSCRIBE_ACTION_SUBSCRIBE,
	/**
	 * Unsubscription for one event.
	 */
	VIRT_DEV_IOCTL_SUBSCRIBE_ACTION_UNSUBSCRIBE,
	/**
	 * Removes all the subscriptions.
	 */
	VIRT_DEV_IOCTL_SUBSCRIBE_ACTION_CLEAR
};

/**
 * The data for the VIRT_DEV_IOC_SUBSCRIBE IOCTL call.
 */
struct vdev_ioctl_subscribe {
	/**
	 * The event altered.
	 */
	int event;
	
	/**
	 * one of VIRT_DEV_IOCTL_SUBSCRIBE_ACTION_... (subscribe, unsubscribe, 
	 * remove all subscriptions)
	 */
	uint8_t action;
};

/**
 * The data for the VIRT_DEV_IOC_STATUS_GET IOCTL call.
 */
struct vdev_ioctl_status {
	/**
	 * The device major number.
	 */
	int major;
	/**
	 * The device minor number.
	 */
	int minor;
	
	/**
	 * the device name
	 */
	char name[MODAC_ID_MAX_NAME + 1];
};

/**
 * The data for the VIRT_DEV_IOC_RES_STATUS_GET IOCTL call.
 */
struct vdev_ioctl_res_status {
	/* 
	 * The requested resource type.
	 */
	int res_type;
	/* 
	 * The number of resources for the resource type.
	 */
	int count;
};

/* Pick a free magic number according to Documentation/ioctl/ioctl-number.txt. */
#define VIRT_DEV_IOC_MAGIC 	0xF1

/*  ------- general ioctls ------------ */

/**
 * Subscribes/unsubscribes to a virtual event / clears subscriptions. 
 */
#define VIRT_DEV_IOC_SUBSCRIBE		_IOW(VIRT_DEV_IOC_MAGIC, 1, struct vdev_ioctl_subscribe)

/**
 * Return miscellaneous status data for the VIRT_DEV.
 */
#define VIRT_DEV_IOC_STATUS_GET		_IOWR(VIRT_DEV_IOC_MAGIC, 2, struct vdev_ioctl_status)

/**
 * Return miscellaneous status data for a resource type.
 */
#define VIRT_DEV_IOC_RES_STATUS_GET	_IOWR(VIRT_DEV_IOC_MAGIC, 3, struct vdev_ioctl_res_status)


#define VIRT_DEV_IOC_MAX  		3



/* ------------- HW (EVR) specific ioctls ------------ */

/*
 * Must have its own range; the actuall IOCTLS will be defined in linux-evrma.h
 */
#define VIRT_DEV_HW_IOC_MIN  		128
#define VIRT_DEV_HW_IOC_MAX  		239


/* ---------- dbg ioctls ------------ */

#define VIRT_DEV_DBG_IOC_MIN  		240
#define VIRT_DEV_DBG_IOC_MAX  		0

/** @} */

#endif /* LINUX_MODAC_H_ */

