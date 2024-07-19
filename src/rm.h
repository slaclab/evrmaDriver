//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef MODAC_RM_H_
#define MODAC_RM_H_

#include "hw-support.h"

/*
 * The maximal possible number of different resource types. This is limited
 * so we can use fixed tables without kalloc.
 */
#define MODAC_RES_TYPE_MAX_COUNT 16



/* 
 * Resource Manager. Not thread safe. The container of modac_rm_data must 
 * provide mutex locking. 
 */

struct modac_rm_data {
	const struct modac_hw_support_data *hw_support_data;
	void *priv; // opaque data, defined in rm.c
};

/*
 * This is similar to the struct mngdev_ioctl_hw_header_vres but is not the
 * same. Here the index is absolute for the given type of the resource. 
 */
struct modac_rm_vres_desc {
	/** The resource's type (see XXX_RES_TYPE_...). Can be MODAC_RES_TYPE_NONE. */
	int type;
	/** The resource's RM index (absolute). */
	int index;
};



/*
 * uses 'hw_support' to create the resource manager structure; hw_support_data
 * is also filled up additionally.
 */
int modac_rm_init(
		struct modac_hw_support_data *hw_support_data, 
		struct modac_rm_data *rm_data);

void modac_rm_end(struct modac_rm_data *rm_data);

#define MODAC_RM_ALLOC_FROM_POOL (-1)

/* Return absolute rm index in table in VIRT_RES_LIST or <0 on errror. */
int modac_rm_alloc(struct modac_rm_data *rm_data,
				   int owner,
				   const char *resource_name,
				   /* MODAC_RM_ALLOC_FROM_POOL to allocate from pool. */
				   int fixed_inx,
				   /* args to pass to the filter to check resource suitabililty */
				   int *arg_filter,
				   /** The returned vres data. */
				   struct modac_rm_vres_desc *vres_desc);

/*
 * free all resources for the owner
 */
void modac_rm_free_owner(struct modac_rm_data *rm_data, int owner);

/*
 * return owner id or <0 on err
 */
int modac_rm_get_owner(struct modac_rm_data *rm_data, struct modac_rm_vres_desc *vres_desc);

ssize_t modac_rm_print_info(struct modac_rm_data *rm_data, char *buf, ssize_t count, int entry_start, int entry_count);

int modac_rm_get_res_count_for_type(struct modac_rm_data *rm_data, int res_type);




#endif /* MODAC_RM_H_ */
 
