//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef MODAC_HW_SUPPORT_H_
#define MODAC_HW_SUPPORT_H_

#include <linux/irqreturn.h>

#include "linux-modac.h"

/** @file */

/**
 * @defgroup g_hw_support_api API to attach the HW support to the MODAC core
 *
 * @{
 */



struct modac_rm_vres_desc;
struct event_list_type;
struct modac_vdev_des;

struct modac_hw_support_data {
	
	/*
	 * The length and data of the table of the hw resource definitions. 
	 * Filled by the HW support.
	 */
	int hw_res_def_count;
	
	struct modac_hw_res_def *hw_res_defs;
	
	/*
	 * The private data of the HW support.
	 */
	void *priv;
	
	/* 
	 * For callbacks from the HW support 
	 */
	struct modac_mngdev_des *mngdev_des;
	
	struct modac_rm_data *rm_data;
	const struct modac_hw_support_def *hw_support;
};

/**
 * @short A structure that defines API to the HW support code.
 */
struct modac_hw_support_def {
	
	/**
	 * The name of the HW support plugin.
	 */
	char hw_name[MODAC_ID_MAX_NAME + 1];
	
	/**
	 * Initializes the HW support. Must be provided.
	 */
	int (*init)(struct modac_hw_support_data *hw_support_data);
	
	/**
	 * Ends the HW support. Must be provided.
	 */
	void (*end)(struct modac_hw_support_data *hw_support_data);
	
	/**
	 * A function that will be called from the MODAC core every time
	 * the subscriptions change. Must be provided.
	 */
	int (*on_subscribe_change)(struct modac_hw_support_data *hw_support_data,
		const struct event_list_type *subscriptions);
	
	/**
	 * Initializes the resource to a known idle state. Needed on VIRT_DEV close.
	 * Must be provided.
	 */
	int (*init_res)(struct modac_hw_support_data *hw_support_data,
				int res_type, int res_index);
	
	/**
	 * Called when the system interrupt for the MODAC device occurs.
	 * Must be provided.
	 */
	irqreturn_t (*isr)(struct modac_hw_support_data *hw_support_data,
			/* 
			 * There could be some info to pass from the creator to the 
			 * HW support. 
			 */
			void *data);
	
	/**
	 * Can be NULL. The program must check the accessibility of the resources
	 * before calling this function.
	 * This function is meant to be called from the MNG_DEV as well as from 
	 * the VIRT_DEV (therefore, with different IOCTL MAGIC ranges).
	 */
	long (*ioctl)(struct modac_hw_support_data *hw_support_data,
						/**
						 * the ref to the VIRT_DEV; NULL if not applicable
						 */
						struct modac_vdev_des *vdev_des,
						/**
						 * the table of 2 resources; can be NULL if not needed
						 */
						struct modac_rm_vres_desc *resources,
						unsigned int cmd, unsigned long arg);
	
	/**
	 * A IOCTL function that is called mutex unprotected. Hence only safe
	 * operations to the hardware can be done which do not interefere with
	 * other operations.
	 * Can be NULL.
	 * This function is meant to be called from the MNG_DEV as well as from 
	 * the VIRT_DEV (therefore, with different IOCTL MAGIC ranges).
	 */
	long (*direct_ioctl)(struct modac_hw_support_data *hw_support_data,
						unsigned int cmd, unsigned long arg);
	
	
	/**
	 * Can be NULL. Returns region(s) to be mmap-ed readonly from the VIRT_DEV.
	 */
	int (*vdev_mmap_ro)(struct modac_hw_support_data *hw_support_data, 
					unsigned long offset, unsigned long vsize,
					unsigned long *physical);
	
	/**
	 * Uses the data that was copied to the /sys/class/<DEV_MNG>/dbg.
	 */
	ssize_t (*store_dbg)(struct modac_hw_support_data *hw_support_data, 
						const char *buf, size_t count);
	/**
	 * Prints the dbg data to the buff. Can be NULL.
	 */
	ssize_t (*show_dbg)(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count);
	
	/**
	 * Prints the data of the resource to the buff. Can be NULL.
	 */
	ssize_t (*dbg_res)(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count, int res_type,
						int res_index);
	
	/**
	 * Prints the data of the hw registers to the buff. Can be NULL.
	 */
	ssize_t (*dbg_regs)(struct modac_hw_support_data *hw_support_data,
						u32 regs_offset, u32 regs_length,
						char *buf, size_t count);
	
	/**
	 * Prints the data of the hw info to the buff. Can be NULL.
	 */
	ssize_t (*dbg_info)(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count);
	
};

/**
 * The resource must be reserved in order to be used. After the reservation this
 * resource is not available to other users anymore.
 */
#define MODAC_RES_FLAG_EXCLUSIVE 0x1

/**
 * @short A structure to define a resource description.
 */
struct modac_hw_res_def {
	/**
	 * The ID of the resource by which the resource is searchable.
	 */
	char name[MODAC_ID_MAX_NAME + 1];
	
	/**
	 * Matches the index in the table 'hw_res_defs'. Filled by the RM. 
	 */
	int type;
	
	/**
	 * The number of the resources of the type.
	 */
	u32 count;
	
	/**
	 * An or-ed combination of MODAC_RES_FLAG_XXX
	 */
	u32 flags;
	
	/**
	 * A function that returns a suitability of the resource according to 
	 * the arg_filter. 'arg_filter' has resource dependent meaning.
	 * The larger the returned number (no special meaning otherwise, must 
	 * just be > 0), the more suitable it is the resource.
	 * Return 0 if not suitable at all.
	 */
	int (*suits)(struct modac_rm_data *rm_data, int index, int *arg_filters);

};

/** @} */


#endif /* MODAC_HW_SUPPORT_H_ */

