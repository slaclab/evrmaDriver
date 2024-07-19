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

#include "internal.h"
#include "evr-sim.h"
#include "linux-evrma.h"

#define EVRSIM_PULSEGEN_COUNT 16
#define EVRSIM_OUTPUT_COUNT 10 // this is debug only

struct pulsegen_params {
	u32 prescaler;
	u32 delay;
	u32 width;
};

struct hw_data {
	
	int pulsegen_prescaler_lengths[EVRSIM_PULSEGEN_COUNT];
	struct pulsegen_params pulsegen_params[EVRSIM_PULSEGEN_COUNT];
	int output_src[EVRSIM_OUTPUT_COUNT];
};

static u32 modac_read_u32(struct modac_mngdev_des *devdes, u32 offset)
{
	// a simple simulation of events arriving
	
	if(offset == EVR_REG_FIFO_SECONDS) {
		static u32 secs = 10;
		secs ++;
		return secs;
	} else if(offset == EVR_REG_FIFO_TIMESTAMP) {
		static u32 ec = 300;
		ec ++;
		return ec;
	} else if(offset == EVR_REG_FIFO_EVENT) {
		return 1;
	} else if(offset == EVR_REG_IRQFLAG) {
		static u32 count = 0;
		count ++;
		return count % 4 != 0 ? EVR_IRQFLAG_EVENT : 0;
	} else {
		return 0;
	}
}

static void modac_write_u32(struct modac_mngdev_des *devdes, u32 offset, u32 value)
{
}

static u16 modac_read_u16(struct modac_mngdev_des *devdes, u32 offset)
{
	return 0; // dummy
}

static void modac_write_u16(struct modac_mngdev_des *devdes, u32 offset, u16 value)
{
}

struct modac_io_rw_plugin evr_sim_rw_plugin = {
	write_u16: modac_write_u16,
	read_u16: modac_read_u16,
	write_u32: modac_write_u32,
	read_u32: modac_read_u32,
};


static int pulsegen_suits(struct modac_rm_data *rm_data, int index, int *arg_filters)
{
	const struct modac_hw_support_data *hw_support_data = rm_data->hw_support_data;
	struct evr_hw_data *evr_hw_data = (struct evr_hw_data *)hw_support_data->priv;
	struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;
	
	// 'arg_filter' means the length of a prescaler in bits. Simply ignore
	// the delay and width here.
	int prescaler_length = arg_filters[0];
	
	if(index < 0 || index >= EVRSIM_PULSEGEN_COUNT) return 0;
	
	// For too short prescaler resources return 0.
	if(hw_data->pulsegen_prescaler_lengths[index] < prescaler_length) return 0;
	
	// The shortest prescaler possible is wanted, so:
	// - Return the largest number 33 for exact match.
	// - For instance, in case of no prescaler pulsegen wanted but we only
	// have the 32-bit one, the returned value will be the minimal 1.
	return 32 + prescaler_length - hw_data->pulsegen_prescaler_lengths[index] + 1;
}

static int output_suits(struct modac_rm_data *rm_data, int index, int *arg_filters)
{
	// the outputs MUST be allocated by a fixed index so this will not be called
	// anyway; if it is called we have to make sure nothing is actually
	// allowed to be allocated.
	// 'arg_filter' means nothing.
	return 0;
}

/*
 * This must match the enums in the EVR_RES_TYPE_...
 */
struct modac_hw_res_def default_res_defs[EVR_RES_TYPE_COUNT] = {
	{
		.name = "pulsegen",
		.count = EVRSIM_PULSEGEN_COUNT,
		.flags = MODAC_RES_FLAG_EXCLUSIVE,
		.suits = pulsegen_suits,
	},
	{
		.name = "output",
		.count = EVRSIM_OUTPUT_COUNT,
		.flags = MODAC_RES_FLAG_EXCLUSIVE,
		.suits = output_suits,
	},
};

static void fill_hw_res_defs(struct evr_hw_data *evr_hw_data)
{
	struct modac_hw_support_data *hw_support_data = evr_hw_data->hw_support_data;
	// use the default
	memcpy(hw_support_data->hw_res_defs, default_res_defs, sizeof(default_res_defs));
}

static void init_hw_data(struct evr_hw_data *evr_hw_data)
{
	struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;
	
	/* Note that these lengths are only used to test the allocation. They
	 * will not be checked when setting the pulsegen's parameters.
	 */
	const int pulsegen_prescaler_lengths[EVRSIM_PULSEGEN_COUNT] = {
		16, 16, 32, 32,
		8, 8, 8, 8,
		0, 0, 0, 0,
		0, 0, 0, 0,
	};
	
	int i;

	memcpy(hw_data->pulsegen_prescaler_lengths,
			pulsegen_prescaler_lengths, sizeof(pulsegen_prescaler_lengths));

	// to test the validity in the sim
	for(i = 0; i < sizeof(struct vevr_mmap_data); i ++) {
		evr_hw_data->mmap_p[i] = (u8)(i & 0xFF);
	}
}

void evr_sim_irq_set(struct modac_mngdev_des *mngdev_des, int enabled)
{
}

int evr_sim_init(struct evr_hw_data *evr_hw_data)
{
	struct hw_data *hw_data;
	
	hw_data = kzalloc(sizeof(struct hw_data), GFP_ATOMIC);
	if(hw_data == NULL) {
		return -ENOMEM;
	}
	
	evr_hw_data->sim = hw_data;
	
	init_hw_data(evr_hw_data);
	fill_hw_res_defs(evr_hw_data);
	
	return 0;
}

void evr_sim_end(struct evr_hw_data *evr_hw_data)
{
	struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;

	kfree(hw_data);
}

int evr_sim_set_output_to_pulsegen(struct evr_hw_data *evr_hw_data, 
		int index_output, int index_pulsegen)
{
	struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;
	
	
	hw_data->output_src[index_output] = index_pulsegen;
	return 0;
}

int evr_sim_set_output_to_misc_func(struct evr_hw_data *evr_hw_data, 
		int index_output, int misc_func)
{
	struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;

	hw_data->output_src[index_output] = misc_func;
	return 0;
}

int evr_sim_get_pulsegen_param(struct evr_hw_data *evr_hw_data,
					int index,
					u32 *prescaler,
					u32 *delay,
					u32 *width)
{
	if(index >= 0 && index < EVRSIM_PULSEGEN_COUNT) {
		struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;
		struct pulsegen_params *pulsegen_params = 
				&hw_data->pulsegen_params[index];

		*prescaler = pulsegen_params->prescaler;
		*delay = pulsegen_params->delay;
		*width = pulsegen_params->width;
		return 0;
	} else {
		printk(KERN_DEBUG "Error Setting the pulse params\n");
		return -EINVAL;
	}
}

int evr_sim_set_pulsegen_param(struct evr_hw_data *evr_hw_data,
					int index,
					u32 prescaler,
					u32 delay,
					u32 width)
{
	if(index >= 0 && index < EVRSIM_PULSEGEN_COUNT) {
		struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;
		struct pulsegen_params *pulsegen_params = 
				&hw_data->pulsegen_params[index];

		pulsegen_params->prescaler = prescaler;
		pulsegen_params->delay = delay;
		pulsegen_params->width = width;
		return 0;
	} else {
		printk(KERN_DEBUG "Error Setting the pulse params\n");
		return -EINVAL;
	}
}

int evr_sim_init_res(struct evr_hw_data *evr_hw_data,
				int res_type, int res_index)
{
	if(res_type == EVR_RES_TYPE_PULSEGEN) {
		if(res_index >= 0 && res_index < EVRSIM_PULSEGEN_COUNT) {
			struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;
			struct pulsegen_params *pulsegen_params = 
					&hw_data->pulsegen_params[res_index];

			pulsegen_params->prescaler = 0;
			pulsegen_params->delay = 0;
			pulsegen_params->width = 0;
			return 0;
		} else {
			return -EINVAL;
		}
	} else if(res_type == EVR_RES_TYPE_OUTPUT) {
		return 0;
	} else {
		return -ENOSYS;
	}
}

// return <0 on err; >=0 is the result
static int extract_hex4(const char *buf, size_t count, int *i)
{
	if(*i < count) {
		u8 v = (u8)buf[*i];
		(*i) ++;
		if(v >= '0' && v <= '9') return v - '0';
		if(v >= 'A' && v <= 'F') return v - 'A' + 10;
		if(v >= 'a' && v <= 'f') return v - 'a' + 10;
		return -1; // unknown char
	} else {
		return -1;
	}
}

static int extract_hex8(const char *buf, size_t count, int *i)
{
	int l, h;
	
	h = extract_hex4(buf, count, i);
	if(h < 0) return -1;
	l = extract_hex4(buf, count, i);
	if(l < 0) return -1;
	
	return (h << 4) | l;
}

static int extract_hex12(const char *buf, size_t count, int *i)
{
	int l, h, hh;
	
	hh = extract_hex4(buf, count, i);
	if(hh < 0) return -1;
	h = extract_hex4(buf, count, i);
	if(h < 0) return -1;
	l = extract_hex4(buf, count, i);
	if(l < 0) return -1;
	
	return (hh << 8) | (h << 4) | l;
}

// the string is expected to trigger the isr:
// iKK00112233445566778899AABBCCDDEEFF
// KK is the code, 00..FF are the data bytes, there can be any number, from 0 to 16
ssize_t evr_sim_dbg(struct evr_hw_data *evr_hw_data, 
						const char *buf, size_t count)
{
	int i = 0;
	u8 cmd;
	
	if(i < count) {
		cmd = (u8)buf[i];
	} else 
		return count;
	
	i += 1;
	
	if(cmd == 'i') {
		// simulate irq with normal event
		
		u8 b[16];
		int bn;
		
		// first the code
		int code = extract_hex8(buf, count, &i);
		if(code < 0) return count;
		
		for(bn = 0; bn < 16; bn ++) {
			int v = extract_hex8(buf, count, &i);
			if(v < 0) break;
			b[bn] = (u8)v;
		}
		
		printk(KERN_DEBUG "Putting event 0x%x with %d bytes of data\n", code, bn);
		modac_mngdev_put_event(evr_hw_data->hw_support_data->mngdev_des, code, b, bn);
		
	} else if(cmd == 'n') {
		// simulate irq with notifying event
		
		int code = extract_hex12(buf, count, &i);
		if(code < 0) return count;
		
		
		printk(KERN_DEBUG "Putting notifying event 0x%x\n", code);
		modac_mngdev_notify(evr_hw_data->hw_support_data->mngdev_des, code);
		
	}

	return count;
}

ssize_t evr_sim_dbg_res(struct evr_hw_data *evr_hw_data, 
						char *buf, size_t count, int res_type,
						int res_index)
{
	struct hw_data *hw_data = (struct hw_data *)evr_hw_data->sim;
	ssize_t n = 0;
	
	if(res_type == EVR_RES_TYPE_PULSEGEN && res_index >= 0 && res_index < EVRSIM_PULSEGEN_COUNT) {
		struct pulsegen_params *pulsegen_params = 
				&hw_data->pulsegen_params[res_index];

		n += scnprintf(buf + n, count - n, "%d,", pulsegen_params->prescaler);
		n += scnprintf(buf + n, count - n, "%d,", pulsegen_params->delay);
		n += scnprintf(buf + n, count - n, "%d", pulsegen_params->width);
	} else if(res_type == EVR_RES_TYPE_OUTPUT && res_index >= 0 && res_index < EVRSIM_OUTPUT_COUNT) {
		n += scnprintf(buf + n, count - n, "%d", hw_data->output_src[res_index]);
	} else {
		n += scnprintf(buf + n, count - n, "XXX");
	}
	
	return n;
}

