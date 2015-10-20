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

#include "internal.h"
#include "rm.h"

#define NO_OWNER (-1)


struct res_state {
	
	/* 
	 * pointer to the original def 
	 */
	const struct modac_hw_res_def *hw_res_def;
	
	/* 
	 * the index in the array of the resource group. 
	 */
	int array_index;
	
	/* The object that owns the resource. NO_OWNER if it is free. 
	 * Applicable only to resources that have to be reserved. 
	 */
	int owner;
};

struct resource_type_data {
	int res_state_start; /* the index to the res_state table */
	int res_state_count; /* this is effectively the copy of hw_support_data->hw_res_defs[i]->count */
};
	
struct res_data {

	struct res_state *res_states;
	int res_state_count;
	
	struct resource_type_data resource_type_data[MODAC_RES_TYPE_MAX_COUNT];
};
	
	

enum {
	CLEAN_DATA,
	CLEAN_STATES,
	
	CLEAN_ALL = CLEAN_STATES
};

static void cleanup(struct modac_rm_data *rm_data, int what)
{
	struct res_data *res_data = (struct res_data *)rm_data->priv;
	
	switch(what) {
	case CLEAN_STATES:
		kfree(res_data->res_states);
	case CLEAN_DATA:
		kfree(res_data);
	}
}

int modac_rm_init(
		struct modac_hw_support_data *hw_support_data, 
		struct modac_rm_data *rm_data)
{
	int ih, ih2, ires;
	int resource_count = 0;
	struct res_data *res_data;
	
	/*
	 * a fixed table too small
	 */
	if(hw_support_data->hw_res_def_count > MODAC_RES_TYPE_MAX_COUNT) return -ENXIO;
	
	res_data = (struct res_data *)kmalloc(sizeof(struct res_data), GFP_ATOMIC);
	if(res_data == NULL) {
		return -ENOMEM;
	}
	
	rm_data->hw_support_data = hw_support_data;
	rm_data->priv = res_data;
	
	/*
	 * count the resources
	 */
	for(ih = 0; ih < hw_support_data->hw_res_def_count; ih ++) {
		struct modac_hw_res_def *hw_res_def = 
					&hw_support_data->hw_res_defs[ih];
		
		hw_res_def->type = ih;
		res_data->resource_type_data[ih].res_state_start = resource_count;
		res_data->resource_type_data[ih].res_state_count = hw_res_def->count;
		
		/*
		 * each array element is a separate resource
		 */
		resource_count += hw_res_def->count;
	}
	
	printk(KERN_INFO "RM: hw_res_def_count=%d, total resource count: %d\n", 
		   hw_support_data->hw_res_def_count, resource_count);
	
	{
		/*
		 * At least one resource slot is allocated even if none are needed.
		 * This is just to make easier to program the case when the system
		 * doesn't need resources at all (no need to check if res_data->res_states
		 * is NULL everywhere we use it as this way it's never NULL).
		 */
		int allocated_count = resource_count;
		if(allocated_count < 1) allocated_count = 1;
		
		res_data->res_states = (struct res_state *)
				kmalloc(sizeof(struct res_state) * allocated_count, GFP_ATOMIC);
	}
	
	if(res_data->res_states == NULL) {
		cleanup(rm_data, CLEAN_DATA);
		return -ENOMEM;
	}

	res_data->res_state_count = resource_count;
	
	/*
	 * fill the resources
	 */
	ires = 0;
	for(ih = 0; ih < hw_support_data->hw_res_def_count; ih ++) {
		const struct modac_hw_res_def *res_def = 
					&hw_support_data->hw_res_defs[ih];
		
		for(ih2 = 0; ih2 < res_def->count; ih2 ++) {
			
			res_data->res_states[ires].hw_res_def = res_def;
			res_data->res_states[ires].array_index = ih2;
			/*
			 * free initially
			 */
			res_data->res_states[ires].owner = NO_OWNER;
			
			ires ++;
		}
	}

	return 0;
}

void modac_rm_end(struct modac_rm_data *rm_data)
{
	cleanup(rm_data, CLEAN_ALL);
}

int modac_rm_alloc(struct modac_rm_data *rm_data, 
				   int owner,
				   const char *resource_name, 
				   int fixed_inx, 
				   int *arg_filters,
				   struct modac_rm_vres_desc *vres_desc)
{
	/*
	 * At the moment it is assumed all the resources are MODAC_RES_FLAG_EXCLUSIVE.
	 */
	
	struct res_data *res_data = (struct res_data *)rm_data->priv;
	struct res_state *res_states = res_data->res_states;
	int i;
	
	/*
	 * variables needed for pool resources
	 */
	int max_suitability = 0;
	int index_most_suitable = -1;
	
	for(i = 0; i < res_data->res_state_count; i ++) {
		
		struct res_state *res_state = &res_states[i];
		
		if(!STRINGS_EQUAL(res_state->hw_res_def->name, resource_name)) continue;
		
		if(fixed_inx == MODAC_RM_ALLOC_FROM_POOL) {
			
			int suitability;
			
			/*
			 * occupied, try another one
			 */
			if(res_state->owner != NO_OWNER) continue;
			
			/*
			 * a free resource, but how suitable it is?
			 */
			suitability = res_state->hw_res_def->suits(rm_data,
						res_state->array_index, arg_filters);
			
			if(suitability <= max_suitability) {
				continue;
			}
			
			index_most_suitable = i;
			max_suitability = suitability;
			
		} else {
			
			/*
			 * the explicitely defined resource 'fixed_inx' is needed
			 */
			
			/*
			 * is this one the right one?
			 */
			if(res_state->array_index != fixed_inx) continue;
			
			if(res_state->owner == owner) {
				/*
				 * found, but already allocated by the owner
				 */
				return -EADDRINUSE;
			}

			index_most_suitable = i;
			goto end;
		}
	}
	
	if(index_most_suitable >= 0) {
		goto end;
	}
	
	/*
	 * can't allocate any
	 */
	return -EACCES;
	
end:
	/*
	 * seize the resource
	 */
	res_states[index_most_suitable].owner = owner;
	
	vres_desc->type = res_states[index_most_suitable].hw_res_def->type;
	vres_desc->index = res_states[index_most_suitable].array_index;
	
	return index_most_suitable;

}

void modac_rm_free_owner(struct modac_rm_data *rm_data, int owner)
{
	struct res_data *res_data = (struct res_data *)rm_data->priv;
	struct res_state *res_states = res_data->res_states;
	int i;

	for(i = 0; i < res_data->res_state_count; i ++) {
		struct res_state *res_state = &res_states[i];
		
		if(res_state->owner == owner) {
			res_state->owner = NO_OWNER;
		}
	}
}

int modac_rm_get_res_count_for_type(struct modac_rm_data *rm_data, int res_type)
{
	struct res_data *res_data = (struct res_data *)rm_data->priv;
	if(res_type < 0 || res_type >= rm_data->hw_support_data->hw_res_def_count) {
		return -EINVAL;
	}
	
	return res_data->resource_type_data[res_type].res_state_count;
}

int modac_rm_get_owner(struct modac_rm_data *rm_data,
					   struct modac_rm_vres_desc *vres_desc)
{
	struct res_data *res_data = (struct res_data *)rm_data->priv;
	struct resource_type_data *type_data;
	struct res_state *res_states = res_data->res_states;
	struct res_state *res_state;
	
	if(vres_desc->type < 0 || 
			vres_desc->type >= rm_data->hw_support_data->hw_res_def_count) {
		return -EINVAL;
	}
	
	type_data = &res_data->resource_type_data[vres_desc->type];
	
	if(vres_desc->index < 0 || vres_desc->index >= type_data->res_state_count) {
		return -EINVAL;
	}
	
	res_state = &res_states[type_data->res_state_start + vres_desc->index];
	
	return (res_state->owner == NO_OWNER) ? -EACCES : res_state->owner;
}


/* NOTE: the 'count' will be of the order of PAGE_SIZE (4096?). If there
 * is too much text to print the excess will be discarded. This function is
 * meant for debugging purposes only. Use ioctl to obtain a consistent info. 
 */
ssize_t modac_rm_print_info(struct modac_rm_data *rm_data, char *buf, 
							ssize_t count, int entry_start, int entry_count)
{
	struct res_data *res_data = (struct res_data *)rm_data->priv;
	struct res_state *res_states = res_data->res_states;
	ssize_t n = 0;
	int i;
		
	for(i = entry_start; i < res_data->res_state_count && i < entry_start + entry_count; i ++) {
		
		const struct res_state *res_state = &res_states[i];
		const struct modac_hw_res_def *hw_res_def = res_state->hw_res_def;
		
		n += scnprintf(buf + n, count - n, "%d ", i);
		
		n += scnprintf(buf + n, count - n, "%c ", hw_res_def->name[0]);
		
		n += scnprintf(buf + n, count - n, "%d %d\t", 
				res_state->array_index, (int)res_state->owner);
	}
	
	return n;
}





