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

#include <linux/pci.h>

#include "internal.h"
#include "evr-internal.h"
#include "evr-sim.h"
#include "linux-evrma.h"

 
#define MODAC_HW_EVR_ID "evr"

// even if the property is 0-bit wide, it is still settable, because it's always 1 in that case
#define MAX_FOR_BIT_INFO(BITS) (u32)((BITS == 0) ? 1 : (((1UL << (BITS)) - 1)))

// ------ General EVR definitions ------------------------------------------
// 
// Taken from EVR-MRM-007.pdf.

// pulse generators as output sources
#define EVR_OUTPUT_SOURCE_PULSEGEN_FIRST 0
#define EVR_OUTPUT_SOURCE_PULSEGEN_COUNT 32

// Form Factor 0 – CompactPCI 3U
// 1 – PMC
// 2 – VME64x
// 3 – CompactRIO
// 4 – CompactPCI 6U
// 6 – PXIe
// 7 – PCIe

#define PULSEGEN_DATA_4x16_32_32	{16, 32, 32},  {16, 32, 32},  {16, 32, 32},  {16, 32, 32}
#define PULSEGEN_DATA_2x0_32_16		{0, 32, 16}, {0, 32, 16}
#define PULSEGEN_DATA_4x0_32_16		PULSEGEN_DATA_2x0_32_16, PULSEGEN_DATA_2x0_32_16
#define PULSEGEN_DATA_6x0_32_16		PULSEGEN_DATA_2x0_32_16, PULSEGEN_DATA_4x0_32_16
#define PULSEGEN_DATA_10x0_32_16	PULSEGEN_DATA_4x0_32_16, PULSEGEN_DATA_6x0_32_16
#define PULSEGEN_DATA_12x0_32_16	PULSEGEN_DATA_6x0_32_16, PULSEGEN_DATA_6x0_32_16
#define PULSEGEN_DATA_2x16_32_32	{16, 32, 32}, {16, 32, 32}
#define PULSEGEN_DATA_2x8_32_16		{8, 32, 16}, {8, 32, 16}
#define PULSEGEN_DATA_4x8_32_16		PULSEGEN_DATA_2x8_32_16, PULSEGEN_DATA_2x8_32_16
#define PULSEGEN_DATA_4x16_32_24	{16, 32, 24}, {16, 32, 24}, {16, 32, 24}, {16, 32, 24}
#define PULSEGEN_DATA_6x0_32_24		{0, 32, 24}, {0, 32, 24}, {0, 32, 24}, {0, 32, 24}, {0, 32, 24}, {0, 32, 24}
#define PULSEGEN_DATA_12x0_32_24	PULSEGEN_DATA_6x0_32_24, PULSEGEN_DATA_6x0_32_24

static const struct evr_type_data documented_evr_type_data_table[EVR_TYPE_DOCD_COUNT] = {
	{
		"cPCI-EVR-230",
		10,
		{
			PULSEGEN_DATA_4x16_32_32,
			PULSEGEN_DATA_6x0_32_16,
		},
		2, 0, 0, 4, 2, 8, 2, 0, 0, 3, 32
	},
	{
		"PMC-EVR-230",
		10,
		{
			PULSEGEN_DATA_2x16_32_32,
			PULSEGEN_DATA_2x8_32_16,
			PULSEGEN_DATA_6x0_32_16,
		},
		1, 3, 0, 0, 0, 0, 0, 10, 0, 3, 16
	},
	{
		"VME-EVR-230",
		16,
		{
			PULSEGEN_DATA_4x16_32_24,
			PULSEGEN_DATA_12x0_32_24,
		},
		2, 8, 0, 4, 2, 8, 2, 16, 16, 3, 32
	},
	{
		"VME-EVR-230RF",
		16,
		{
			PULSEGEN_DATA_4x8_32_16,
			PULSEGEN_DATA_12x0_32_16,
		},
		2, 4, 3, 4, 2, 8, 2, 16, 16, 3, 16
	},
	{
		"cPCI-EVRTG-300",
		10,
		{
			PULSEGEN_DATA_4x16_32_32,
			PULSEGEN_DATA_6x0_32_16,
		},
		0, 0, 8, 4, 2,
		
		// From the software point of view all outputs show up as GTX/CML outputs. Physically there are four
		// UNIV Outputs (two slots), two LVPECL outputs and two SFP outputs
		8, 
		
		2, 0, 0, 3, 32
	},
	{
		"cPCI-EVR-300",
		14,
		{
			PULSEGEN_DATA_4x16_32_32,
			PULSEGEN_DATA_10x0_32_16,
		},
		2, 0, 0, 12, 6, 8, 2, 0, 0, 3, 32
	},
	{
		"cRIO-EVR-300",
		8, 
		{
			PULSEGEN_DATA_4x16_32_32,
			PULSEGEN_DATA_4x0_32_16
		},
		0, 0, 0, 
		
		// From the software point of view the cRIO outputs show up as UNIV outputs. Physically they are available
		// on the DSUB connector.
		4, 
		
		0, 0, 0, 0, 0, 3, 16
	},
	{
		"PCIe-EVR-300",
		16,
		{
			PULSEGEN_DATA_4x16_32_32,
			PULSEGEN_DATA_12x0_32_16
		},
		0, 0, 0, 16, 
		
		// Universal I/O is available on the external I/O box
		8, 
		
		0, 0, 0, 0, 3, 16
	},
	{
		"PXIe-EVR-300I",
		16,
		{
			PULSEGEN_DATA_4x16_32_32,
			PULSEGEN_DATA_12x0_32_16
		},
		2, 0, 0, 16, 
		
		// Universal I/O is available on the external I/O box, which from the software point of view are ports 4 to 19
		// (ports 0 to 3 are physically present on the PCB however, unavailable for mounting a Universal I/O module
		8, 
		
		0, 0, 58, 42, 4, 32
	},
	{
		"PXIe-EVR-300U",
		16,
		{
			PULSEGEN_DATA_4x16_32_32,
			PULSEGEN_DATA_12x0_32_16
		},
		2, 0, 0, 4, 2, 8, 2, 58, 42, 4, 32
	},
	
};

// ------ General EVR end ------------------------------------------

/* At first the FW version was 0x1fd20005, which change to
 0x1fd00023 after the bug fix */
// #define EVR_TYPE_ADHOC_SLAC1_FW_VERSION 0x1fd00023

#define EVR_TYPE_ADHOC_SLAC_FW_VERSION 0x1f000000
#define EVR_TYPE_ADHOC_SLAC_FW_VERSION_MASK 0xFF000000

static const struct evr_type_data adhoc_evr_type_slac_general = {
	"EVR-SLAC-GENERAL",
	0, // no pulsegens
	{},
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define EVR_TYPE_EMCOR_FW_VERSION 0x1f000003

static const struct evr_type_data adhoc_evr_type_emcor = {
	"EMCOR",
	0, // no pulsegens
	{},
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};


enum {
	CLEAN_RES,
	CLEAN_DATA,
	CLEAN_SIM,
	CLEAN_ALL = CLEAN_SIM
};

int evr_card_is_slac(u32 fw_version)
{
	return (fw_version >> 24) == 0x1f;
}


static void cleanup(struct modac_hw_support_data *hw_support_data, int what)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	
	switch(what) {
	case CLEAN_SIM:
		if(hw_data->sim != NULL) {
			evr_sim_end(hw_data);
		}
	case CLEAN_DATA:
		kfree(hw_support_data->priv);
	case CLEAN_RES:
		kfree(hw_support_data->hw_res_defs);
	}
}

static int pulsegen_property_suitability(int tested, int wanted)
{
	// For too short resource property resources return 0.
	if(tested < wanted) return 0;
	
	// The shortest resource property possible is wanted, so:
	// - Return the largest number 33 for exact match.
	// - For instance, in case of 0-bit property of the pulsegen wanted but we only
	// have the 32-bit one, the returned value will be the minimal 1.
	return 32 + wanted - tested + 1;
}

static int pulsegen_suits(struct modac_rm_data *rm_data, int index, int *arg_filters)
{
	const struct modac_hw_support_data *hw_support_data = rm_data->hw_support_data;
	struct evr_hw_data *evr_hw_data = (struct evr_hw_data *)hw_support_data->priv;
	const struct evr_type_data *evr_type_data = &evr_hw_data->evr_type_data;
	
	#if MNG_DEV_IOCTL_RES_ARG_FILTER_MAX_COUNT < 3
	#error BUG: pulsgen needs 3 arg filters
	#endif
	
	// 'arg_filters' means the length of the prescaler, the delay and the width in bits.
	int prescaler_length = arg_filters[0];
	int delay_length = arg_filters[1];
	int width_length = arg_filters[2];
	int suitability = 0;
	int s1;
	
	if(index < 0 || index >= evr_type_data->pulsegen_count) return 0;
	
	
	
	s1 = pulsegen_property_suitability(
			evr_type_data->pulsegen_data[index].prescaler_bits, prescaler_length);
	if(s1 < 1) return 0; // all properties must suit
	
	suitability += s1;
	
	s1 = pulsegen_property_suitability(
			evr_type_data->pulsegen_data[index].delay_bits, delay_length);
	if(s1 < 1) return 0; // all properties must suit
	
	suitability += s1;
	
	s1 = pulsegen_property_suitability(
			evr_type_data->pulsegen_data[index].width_bits, width_length);
	if(s1 < 1) return 0; // all properties must suit
	
	suitability += s1;
	
	// Just return the simple sum.
	// Example: if all 3 params match exactly, here the value of 66 will be returned.
	return suitability;
}

static int output_suits(struct modac_rm_data *rm_data, int index, int *arg_filters)
{
	/*
	 * the outputs MUST be allocated by a fixed index so this will not be called
	 * anyway; if it is called we have to make sure nothing is actually
	 * allowed to be allocated.
	 * 
	 * 'arg_filters' are not used.
	 */
	
	return 0;
}

// Does everything on 16-bit read (endiannes!)
u16 evr_read16(struct modac_hw_support_data *hw_support_data, int reg)
{
	struct modac_mngdev_des *devdes = hw_support_data->mngdev_des;
	
	return be16_to_cpu(devdes->io_rw->read_u16(devdes, reg));
}

// Does everything on 16-bit write (endiannes!)
void evr_write16(struct modac_hw_support_data *hw_support_data, int reg, u16 val)
{
	struct modac_mngdev_des *devdes = hw_support_data->mngdev_des;
	
	devdes->io_rw->write_u16(devdes, reg, cpu_to_be16(val));
}

// Does everything on 32-bit read (endiannes!)
u32 evr_read32(struct modac_hw_support_data *hw_support_data, int reg)
{
	struct modac_mngdev_des *devdes = hw_support_data->mngdev_des;
	
	return be32_to_cpu(devdes->io_rw->read_u32(devdes, reg));
}

// Does everything on 32-bit write (endiannes!)
void evr_write32(struct modac_hw_support_data *hw_support_data, int reg, u32 val)
{
	struct modac_mngdev_des *devdes = hw_support_data->mngdev_des;
	
	devdes->io_rw->write_u32(devdes, reg, cpu_to_be32(val));
}

static void save_map_ram(struct modac_hw_support_data *hw_support_data, u32 address)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	int i;
	
	for(i = 0; i < EVR_MAPRAM_EVENT_CODES; i ++) {
		evr_write32(hw_support_data, 
					address + i * EVR_REG_MAPRAM_SLOT_SIZE + EVR_REG_MAPRAM_CLEAR_OFFSET,
					hw_data->map_ram[i].pulse_clear);
		evr_write32(hw_support_data, 
					address + i * EVR_REG_MAPRAM_SLOT_SIZE + EVR_REG_MAPRAM_SET_OFFSET,
					hw_data->map_ram[i].pulse_set);
		evr_write32(hw_support_data, 
					address + i * EVR_REG_MAPRAM_SLOT_SIZE + EVR_REG_MAPRAM_TRIGGET_OFFSET,
					hw_data->map_ram[i].pulse_trigger);
		evr_write32(hw_support_data, 
					address + i * EVR_REG_MAPRAM_SLOT_SIZE + EVR_REG_MAPRAM_INT_FUNC_OFFSET,
					hw_data->map_ram[i].int_event);
	}
}

void evr_ram_map_change_flush(
		struct modac_hw_support_data *hw_support_data)
{
	u32 newram_offset;
	u32 ctrl = evr_read32(hw_support_data, EVR_REG_CTRL);
	
	if ((ctrl >> C_EVR_CTRL_MAP_RAM_SELECT) & 1)
		newram_offset = EVR_REG_MAPRAM1;
	else
		newram_offset = EVR_REG_MAPRAM2;
	
	save_map_ram(hw_support_data, newram_offset);

	// switch the ram
	ctrl &= ~((1 << C_EVR_CTRL_MAP_RAM_ENABLE) | (1 << C_EVR_CTRL_MAP_RAM_SELECT));
	ctrl |= (1 << C_EVR_CTRL_MAP_RAM_ENABLE);
	if (newram_offset == EVR_REG_MAPRAM2)
		ctrl |= (1 << C_EVR_CTRL_MAP_RAM_SELECT);
	evr_write32(hw_support_data, EVR_REG_CTRL, ctrl);

}

static void evr_ram_map_init(struct modac_hw_support_data *hw_support_data)
{	
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	
	memset(&hw_data->map_ram, 0, sizeof(hw_data->map_ram));

	// copied from ErInitializeRams in event2
	hw_data->map_ram[0x70].int_event = 1<<C_EVR_MAP_SECONDS_0;
	hw_data->map_ram[0x71].int_event = 1<<C_EVR_MAP_SECONDS_1;
	hw_data->map_ram[0x7a].int_event = 1<<C_EVR_MAP_HEARTBEAT_EVENT;
	hw_data->map_ram[0x7b].int_event = 1<<C_EVR_MAP_RESETPRESC_EVENT;
	hw_data->map_ram[0x7c].int_event = 1<<C_EVR_MAP_TIMESTAMP_CLK;
	hw_data->map_ram[0x7d].int_event = 1<<C_EVR_MAP_TIMESTAMP_RESET;
	
	evr_ram_map_change_flush(hw_support_data);
}

static int hw_support_evr_init(struct modac_hw_support_data *hw_support_data)
{
	struct evr_hw_data *hw_data;
	int ret;
	int current_clean; // keeps track of what to clean up on error.
	
	/*
	 * hw_res_defs must be allocated. The static structure can't be used because
	 * this same hw support may be used again for some other EVR flavour in which
	 * case the hw_res_defs won't be the same. Each MNG_DEV must get its own copy
	 * of this.
	 */
	
	hw_support_data->hw_res_def_count = EVR_RES_TYPE_COUNT;
	hw_support_data->hw_res_defs = kmalloc(
			sizeof(struct modac_hw_res_def) * EVR_RES_TYPE_COUNT, GFP_ATOMIC);
	if(hw_support_data->hw_res_defs == NULL) {
		return -ENOMEM;
	}
	
	current_clean = CLEAN_RES;
	
	hw_data = kzalloc(sizeof(struct evr_hw_data), GFP_ATOMIC);
	if(hw_data == NULL) {
		cleanup(hw_support_data, current_clean);
		return -ENOMEM;
	}
	
	current_clean = CLEAN_DATA;
	
	hw_data->mmap_p = (void *)((long)(hw_data->mmap_mem + PAGE_SIZE - 1) & PAGE_MASK);
	hw_data->mmap_p_final_size = 
			(u8 *)&hw_data->mmap_mem + sizeof(hw_data->mmap_mem) - (u8 *)hw_data->mmap_p;

	hw_data->hw_support_data = hw_support_data;
	hw_support_data->priv = hw_data;
	
	// io_start == NULL means the simulation
	if(hw_support_data->mngdev_des->io_start == NULL) {
		
		ret = evr_sim_init(hw_data);
		if(ret < 0) {
			cleanup(hw_support_data, current_clean);
		}

	} else {
		
		// non-simulation stuff
		
		int evr_type_docd;
		
		hw_data->sim = NULL;
	
		
		/*
		 * The first SLAC card (not present anymore) had: FWVersion=0x1f000000
		 * Second SLAC card: fw_version=0x1fd20005
		 */
		
		evr_type_docd = hw_support_data->mngdev_des->hw_support_hint1;
		
		hw_data->fw_version = evr_read32(hw_support_data, EVR_REG_FW_VERSION);
	
		if(evr_type_docd >= 0 && evr_type_docd < EVR_TYPE_DOCD_COUNT) {
			
			// evr_type_docd can be one of the documented EVR types 
			
			memcpy(&hw_data->evr_type_data, 
					&documented_evr_type_data_table[evr_type_docd], 
					sizeof(struct evr_type_data));

		} else if((hw_data->fw_version & EVR_TYPE_ADHOC_SLAC_FW_VERSION_MASK) 
			== EVR_TYPE_ADHOC_SLAC_FW_VERSION) {
			
			memcpy(&hw_data->evr_type_data, 
					&adhoc_evr_type_slac_general, 
					sizeof(struct evr_type_data));
			
		} else if(hw_data->fw_version == EVR_TYPE_EMCOR_FW_VERSION) {
			
			memcpy(&hw_data->evr_type_data, 
					&adhoc_evr_type_emcor, 
					sizeof(struct evr_type_data));
			
		} else {
			
			printk(KERN_ERR "Not supported: evr_type_docd=%d, fw_version=0x%x\n", evr_type_docd, hw_data->fw_version);
			cleanup(hw_support_data, current_clean);
			
			// The HW we got is not supported
			return -ENOSYS;
		}

		printk(KERN_DEBUG "OK, going on. FW_VERSION: 0x%x, Type: '%s'\n", hw_data->fw_version, hw_data->evr_type_data.name);

		{
			int otype;
			int current_res_inx = 0;
			
			for(otype = 0; otype < EVR_OUT_TYPE_COUNT; otype ++) {
				int cnt = 0;
				u32 evr_map_reg_start = 0;

				switch(otype) {
				case EVR_OUT_TYPE_FP_TTL:
					cnt = hw_data->evr_type_data.output_count;
					evr_map_reg_start = EVR_REG_FIRST_OUTPUT_FP_TTL;
					break;
				case EVR_OUT_TYPE_FP_UNIV:
					cnt = hw_data->evr_type_data.univ_io;
					evr_map_reg_start = EVR_REG_FIRST_OUTPUT_FP_UNIV;
					break;
				case EVR_OUT_TYPE_TB_OUTPUT:
					cnt = hw_data->evr_type_data.tb_outputs;
					evr_map_reg_start = EVR_REG_FIRST_OUTPUT_TB;
					break;
				}
				
				hw_data->out_cfg[otype].res_start = current_res_inx;
				hw_data->out_cfg[otype].res_count = cnt;
				hw_data->out_cfg[otype].evr_map_reg_start = evr_map_reg_start;
				
				current_res_inx += cnt;
			}

			hw_data->out_res_count = current_res_inx;
		}
		
		strcpy(hw_support_data->hw_res_defs[0].name, "pulsegen");
		strcpy(hw_support_data->hw_res_defs[1].name, "output");
		hw_support_data->hw_res_defs[0].flags = MODAC_RES_FLAG_EXCLUSIVE;
		hw_support_data->hw_res_defs[1].flags = MODAC_RES_FLAG_EXCLUSIVE;
		hw_support_data->hw_res_defs[0].suits = pulsegen_suits;
		hw_support_data->hw_res_defs[1].suits = output_suits;
		hw_support_data->hw_res_defs[0].count = hw_data->evr_type_data.pulsegen_count;
		hw_support_data->hw_res_defs[1].count = hw_data->out_res_count;
		ret = 0;
		
		evr_ram_map_init(hw_support_data);
	}
	
	return ret;
}
	
static void hw_support_evr_end(struct modac_hw_support_data *hw_support_data)
{
	cleanup(hw_support_data, CLEAN_ALL);
}

static struct evr_hw_data_out_cfg *find_out_cfg(struct evr_hw_data *hw_data,
					int res_output_index, int *rel_index)
{
	int iotype;
	
	for(iotype = 0; iotype < EVR_OUT_TYPE_COUNT; iotype ++) {
		
		struct evr_hw_data_out_cfg *out_cfg = &hw_data->out_cfg[iotype];
		
		if(res_output_index >= out_cfg->res_start &&
					res_output_index < out_cfg->res_start + out_cfg->res_count) {
			*rel_index = res_output_index - out_cfg->res_start;
			return out_cfg;
		}
	}
	
	return NULL;
}

static int evr_set_out_map(struct modac_hw_support_data *hw_support_data, 
						int res_output_index,
						int map)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	int evr_output_count = hw_support_data->hw_res_defs[EVR_RES_TYPE_OUTPUT].count;
	struct evr_hw_data_out_cfg *out_cfg;
	int rel_index;
	
	if(res_output_index < 0 || res_output_index >= evr_output_count) {
		// Sanity check. These values would mean a bug in the program.
		return -EINVAL;
	}

	out_cfg = find_out_cfg(hw_data, res_output_index, &rel_index);
	if(out_cfg == NULL) {
		// Sanity check. These values would mean a bug in the program.
		return -EINVAL;
	}
	
	evr_write16(hw_support_data, 
				out_cfg->evr_map_reg_start + rel_index * EVR_REG_OUTPUT_SLOT_SIZE,
				map);

	return 0;
}

static void set_pulse_params(struct modac_hw_support_data *hw_support_data,
							 int pulse_start_reg,
							 u32 prescaler, u32 delay, u32 width)
{
	evr_write32(hw_support_data, pulse_start_reg + EVR_REG_PULSE_PRESC_OFFSET, 
								prescaler);
	evr_write32(hw_support_data, pulse_start_reg + EVR_REG_PULSE_DELAY_OFFSET, 
								delay);
	evr_write32(hw_support_data, pulse_start_reg + EVR_REG_PULSE_WIDTH_OFFSET, 
								width);
}

static long hw_support_evr_ioctl(struct modac_hw_support_data *hw_support_data, 
				struct modac_vdev_des *vdev_des,
				struct modac_rm_vres_desc *resources, 
				unsigned int cmd, unsigned long arg)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	int ret;
	
	int evr_pulsegen_count = hw_support_data->hw_res_defs[EVR_RES_TYPE_PULSEGEN].count;
		
	ret = -ENOSYS;
	
	switch(cmd) {
		
	// -------- MNG_DEV IOCTLs -----------------------------------
	
	case MNG_DEV_EVR_IOC_INIT:
	{
		int otype;
		int ipulse;
		
		/*
		 * Set all outputs to low state.
		 */
		for(otype = 0; otype < EVR_OUT_TYPE_COUNT; otype ++) {
			
			int i;
			
			for(i = 0; i < hw_data->out_cfg[otype].res_count; i ++) {
				evr_write16(hw_support_data, 
					hw_data->out_cfg[otype].evr_map_reg_start + 
								i * EVR_REG_OUTPUT_SLOT_SIZE,
					OUTPUT_REG_MAPPING_FORCE_LOW);
			}
		}
		
		/*
		 * Disable and initialize all pulse generators.
		 */
		for(ipulse = 0; ipulse < evr_pulsegen_count; ipulse ++) {
			
			int pulse_start_reg = EVR_REG_PULSES + EVR_REG_PULSE_SLOT_SIZE * ipulse;
			
			set_pulse_params(hw_support_data, pulse_start_reg, 0, 0, 0);
			evr_write32(hw_support_data, 
						pulse_start_reg + EVR_REG_PULSE_CTRL_OFFSET, 0);
		}
		
		/*
		 * Initialize the MAP RAM.
		 */
		evr_ram_map_init(hw_support_data);
		
		ret = 0;
		
		break;
	}
	
	case MNG_DEV_EVR_IOC_OUTSET:
	{
		struct mngdev_evr_output_set set_args;
		
		struct modac_rm_vres_desc *res_output = &resources[0];
		struct modac_rm_vres_desc *res_pulsegen = &resources[1];
		
		if(res_output->type != EVR_RES_TYPE_OUTPUT) {
			// must be a defined output
			return -EINVAL;
		}
		
		if (copy_from_user(&set_args, (void *)arg, sizeof(struct mngdev_evr_output_set))) {
			return -EFAULT;
		}
		
		if(res_pulsegen->type == EVR_RES_TYPE_PULSEGEN) {
			
			if(res_pulsegen->index < EVR_OUTPUT_SOURCE_PULSEGEN_FIRST ||
					res_pulsegen->index >= EVR_OUTPUT_SOURCE_PULSEGEN_FIRST +
									EVR_OUTPUT_SOURCE_PULSEGEN_COUNT) {
				// Sanity check. These values would mean a bug in the program.
				return -EINVAL;
			}
			
			if(hw_data->sim != NULL) {
				ret = evr_sim_set_output_to_pulsegen(hw_data, 
						res_output->index,
						res_pulsegen->index);
			} else {
				
				if(res_pulsegen->index < 0 || res_pulsegen->index >= evr_pulsegen_count) {
					// Sanity check. These values would mean a bug in the program.
					return -EINVAL;
				}
			
				ret = evr_set_out_map(hw_support_data, 
						res_output->index,
						// pulsegen functions start from the func=0
						0 + res_pulsegen->index);
			}
			
		} else if(res_pulsegen->type == MODAC_RES_TYPE_NONE) {

			if(set_args.misc_func >= EVR_OUTPUT_SOURCE_PULSEGEN_FIRST &&
					set_args.misc_func < EVR_OUTPUT_SOURCE_PULSEGEN_FIRST +
									EVR_OUTPUT_SOURCE_PULSEGEN_COUNT) {
				// These source values can only be set by pulsegen defined.
				return -EINVAL;
			}
			
			if(hw_data->sim != NULL) {
				ret = evr_sim_set_output_to_misc_func(hw_data, 
						res_output->index,
						set_args.misc_func);
			} else {

				ret = evr_set_out_map(hw_support_data, 
						res_output->index,
						set_args.misc_func);
			}
		} else {
			// must be a pulsegen or undefined
			return -EINVAL;
		}
		
		break;
	}
	
	// -------- VIRT_DEV IOCTLs -----------------------------------
	
	case VEVR_IOC_STATUS_GET:
	{
		struct vevr_ioctl_status set_arg;
		
		set_arg.status.fpga_version = evr_read32(hw_support_data, EVR_REG_FW_VERSION);
		set_arg.status.irq_flags = evr_read32(hw_support_data, EVR_REG_IRQFLAG);
		set_arg.status.seconds_shift = evr_read32(hw_support_data, EVR_REG_SECONDS_SHIFT);
		set_arg.status.timestamp_latch = evr_read32(hw_support_data, EVR_REG_TIMESTAMP_LATCH);
		
		if (copy_to_user((void *)arg, &set_arg, 
					sizeof(struct vevr_ioctl_status))) {
			return -EFAULT;
		}
		ret = 0;
		break;
	}
		
	case VEVR_IOC_PULSE_PARAM_SET:
	case VEVR_IOC_PULSE_PARAM_GET:
	{
		struct vevr_ioctl_pulse_param pulse_param_args;
		struct modac_rm_vres_desc *res_pulsegen = &resources[0];
		
		int reading = (cmd == VEVR_IOC_PULSE_PARAM_GET);
		
		if(res_pulsegen->type != EVR_RES_TYPE_PULSEGEN) {
			// must be a defined pulsegen
			return -EINVAL;
		}
		
		if(!reading) {
			if (copy_from_user(&pulse_param_args, (void *)arg, 
							sizeof(struct vevr_ioctl_pulse_param))) {
				return -EFAULT;
			}
		}
		
		if(hw_data->sim != NULL) {
			if(reading) {
				ret = evr_sim_get_pulsegen_param(hw_data, 
						res_pulsegen->index,
						&pulse_param_args.prescaler,
						&pulse_param_args.delay,
						&pulse_param_args.width);
			} else {
				ret = evr_sim_set_pulsegen_param(hw_data, 
						res_pulsegen->index,
						pulse_param_args.prescaler,
						pulse_param_args.delay,
						pulse_param_args.width);
			}
		} else {
			
			const struct evr_pulsegen_bit_info *pulsegen_bit_info;
			int pulse_start_reg;

			if(res_pulsegen->index < 0 || res_pulsegen->index >= evr_pulsegen_count) {
				// Sanity check. These values would mean a bug in the program.
				return -EINVAL;
			}
			
			pulsegen_bit_info = &hw_data->evr_type_data.
											pulsegen_data[res_pulsegen->index];
											
			pulse_start_reg = EVR_REG_PULSES + EVR_REG_PULSE_SLOT_SIZE * res_pulsegen->index;
			
			if(reading) {
				pulse_param_args.prescaler = evr_read32(hw_support_data, 
							pulse_start_reg + EVR_REG_PULSE_PRESC_OFFSET);
				pulse_param_args.delay = evr_read32(hw_support_data, 
							pulse_start_reg + EVR_REG_PULSE_DELAY_OFFSET);
				pulse_param_args.width = evr_read32(hw_support_data, 
								pulse_start_reg + EVR_REG_PULSE_WIDTH_OFFSET);
			} else {
				if(pulse_param_args.prescaler > MAX_FOR_BIT_INFO(pulsegen_bit_info->prescaler_bits)) {
					printk(KERN_WARNING "Pulsegen prescaler too short\n");
					return -EINVAL;
				}
												
				if(pulse_param_args.delay > MAX_FOR_BIT_INFO(pulsegen_bit_info->delay_bits)) {
					printk(KERN_WARNING "Pulsegen delay too short\n");
					return -EINVAL;
				}
												
				if(pulse_param_args.width > MAX_FOR_BIT_INFO(pulsegen_bit_info->width_bits)) {
					printk(KERN_WARNING "Pulsegen width too short");
					return -EINVAL;
				}
				
				set_pulse_params(hw_support_data,
						pulse_start_reg,
						pulse_param_args.prescaler, 
						pulse_param_args.delay, 
						pulse_param_args.width);
			}
		}

		if(reading) {
			if (copy_to_user((void *)arg, &pulse_param_args, 
						sizeof(struct vevr_ioctl_pulse_param))) {
				return -EFAULT;
			}
		}
		
		ret = 0;
		
		break;
	}

	case VEVR_IOC_PULSE_PROP_SET:
	case VEVR_IOC_PULSE_PROP_GET:
	{
		struct vevr_ioctl_pulse_properties pulse_prop_args;
		struct modac_rm_vres_desc *res_pulsegen = &resources[0];
		u32 pctrl;
		int pulse_start_reg;
		
		int reading = (cmd == VEVR_IOC_PULSE_PROP_GET);
		
		if(res_pulsegen->type != EVR_RES_TYPE_PULSEGEN) {
			// must be a defined pulsegen
			return -EINVAL;
		}
		
		if(hw_data->sim != NULL) {
			// not supported on the test simulation
			return -ENOSYS;
		}
		
		if(res_pulsegen->index < 0 || res_pulsegen->index >= evr_pulsegen_count) {
			// Sanity check. These values would mean a bug in the program.
			return -EINVAL;
		}
		
		if(!reading) {
			if (copy_from_user(&pulse_prop_args, (void *)arg, 
						sizeof(struct vevr_ioctl_pulse_properties))) {
				return -EFAULT;
			}
		}
		
		pulse_start_reg = EVR_REG_PULSES + EVR_REG_PULSE_SLOT_SIZE * res_pulsegen->index;
		
		pctrl = evr_read32(hw_support_data, pulse_start_reg + EVR_REG_PULSE_CTRL_OFFSET);
		
		if(reading) {
			
			pulse_prop_args.enable = (pctrl & ~(1 << C_EVR_PULSE_ENA)) ? 1 : 0;
			pulse_prop_args.polarity = (pctrl & ~(1 << C_EVR_PULSE_POLARITY)) ? 1 : 0;
			pulse_prop_args.pulse_cfg_bits = 0;
			if(pctrl & (1 << C_EVR_PULSE_MAP_RESET_ENA)) {
				pulse_prop_args.pulse_cfg_bits |= (1 << EVR_PULSE_CFG_BIT_CLEAR);
			}
			if(pctrl & (1 << C_EVR_PULSE_MAP_SET_ENA)) {
				pulse_prop_args.pulse_cfg_bits |= (1 << EVR_PULSE_CFG_BIT_SET);
			}
			if(pctrl & (1 << C_EVR_PULSE_MAP_TRIG_ENA)) {
				pulse_prop_args.pulse_cfg_bits |= (1 << EVR_PULSE_CFG_BIT_TRIGGER);
			}
			
			if (copy_to_user((void *)arg, &pulse_prop_args, 
						sizeof(struct vevr_ioctl_pulse_properties))) {
				return -EFAULT;
			}
			
		} else {

			if(pulse_prop_args.enable) {
				pctrl |= (1 << C_EVR_PULSE_ENA);
			} else {
				pctrl &= ~(1 << C_EVR_PULSE_ENA);
			}
			
			if(pulse_prop_args.polarity) {
				pctrl |= (1 << C_EVR_PULSE_POLARITY);
			} else {
				pctrl &= ~(1 << C_EVR_PULSE_POLARITY);
			}
			
			if(pulse_prop_args.pulse_cfg_bits & (1 << EVR_PULSE_CFG_BIT_CLEAR)) {
				pctrl |= (1 << C_EVR_PULSE_MAP_RESET_ENA);
			} else {
				pctrl &= ~(1 << C_EVR_PULSE_MAP_RESET_ENA);
			}
			
			if(pulse_prop_args.pulse_cfg_bits & (1 << EVR_PULSE_CFG_BIT_SET)) {
				pctrl |= (1 << C_EVR_PULSE_MAP_SET_ENA);
			} else {
				pctrl &= ~(1 << C_EVR_PULSE_MAP_SET_ENA);
			}
			
			if(pulse_prop_args.pulse_cfg_bits & (1 << EVR_PULSE_CFG_BIT_TRIGGER)) {
				pctrl |= (1 << C_EVR_PULSE_MAP_TRIG_ENA);
			} else {
				pctrl &= ~(1 << C_EVR_PULSE_MAP_TRIG_ENA);
			}
			
			evr_write32(hw_support_data, 
							pulse_start_reg + EVR_REG_PULSE_CTRL_OFFSET, pctrl);
		}
		
		ret = 0;

		break;
	}
		
	case VEVR_IOC_PULSE_MAP_RAM_SET:
	case VEVR_IOC_PULSE_MAP_RAM_SET_FOR_EVENT:
	case VEVR_IOC_PULSE_MAP_RAM_GET:
	case VEVR_IOC_PULSE_MAP_RAM_GET_FOR_EVENT:
	{
		struct modac_rm_vres_desc *res_pulsegen = &resources[0];
		
		struct vevr_ioctl_pulse_map_ram pulse_map_ram256;
		struct vevr_ioctl_pulse_map_ram_for_event pulse_map_ram1;
		
		int reading = (cmd == VEVR_IOC_PULSE_MAP_RAM_GET ||
						cmd == VEVR_IOC_PULSE_MAP_RAM_GET_FOR_EVENT);
		
		int event_first;
		int event_count = (cmd == VEVR_IOC_PULSE_MAP_RAM_SET ||
							cmd == VEVR_IOC_PULSE_MAP_RAM_GET) ? 
										EVR_EVENT_CODES : 1;
		int i;
		u8 *map;
		
		if(res_pulsegen->type != EVR_RES_TYPE_PULSEGEN) {
			// must be a defined pulsegen
			return -EINVAL;
		}
		
		if(hw_data->sim != NULL) {
			// not supported on the test simulation
			return -ENOSYS;
		}
		
		// a real EVR (not the sim)
		
		
		if(event_count == 1) {
			
			if(!reading) {
				if (copy_from_user(&pulse_map_ram1, (void *)arg, 
							sizeof(struct vevr_ioctl_pulse_map_ram_for_event))) {
					return -EFAULT;
				}
			}
			
			event_first = pulse_map_ram1.event_code;
			map = &pulse_map_ram1.map;
			
			if(event_first < EVRMA_FIFO_MIN_EVENT_CODE || 
						event_first > EVRMA_FIFO_MAX_EVENT_CODE) {
				return -EINVAL;
			}
		
		} else {
			
			if(!reading) {
				if (copy_from_user(&pulse_map_ram256, (void *)arg, 
							sizeof(struct vevr_ioctl_pulse_map_ram))) {
					return -EFAULT;
				}
			}
			
			event_first = EVRMA_FIFO_MIN_EVENT_CODE;
			map = pulse_map_ram256.map;
		}
		
		if(res_pulsegen->index < 0 || res_pulsegen->index >= evr_pulsegen_count) {
			// Sanity check. These values would mean a bug in the program.
			return -EINVAL;
		}
		
		for(i = 0; i < event_count; i ++) {
			
			u32 pulse_mask = (1 << res_pulsegen->index);
			u32 *clr32, *set32, *trg32;
			
			clr32 = &hw_data->map_ram[event_first + i].pulse_clear;
			set32 = &hw_data->map_ram[event_first + i].pulse_set;
			trg32 = &hw_data->map_ram[event_first + i].pulse_trigger;

			if(reading) {
				map[i] = 0;
				
				if(*clr32 & pulse_mask) {
					map[i] |= (1 << EVR_PULSE_CFG_BIT_CLEAR);
				}
				
				if(*set32 & pulse_mask) {
					map[i] |= (1 << EVR_PULSE_CFG_BIT_SET);
				}
				
				if(*trg32 & pulse_mask) {
					map[i] |= (1 << EVR_PULSE_CFG_BIT_TRIGGER);
				}
				
			} else {
				if(map[i] & (1 << EVR_PULSE_CFG_BIT_CLEAR)) {
					*clr32 |= pulse_mask;
				} else {
					*clr32 &= (~pulse_mask);
				}
				
				if(map[i] & (1 << EVR_PULSE_CFG_BIT_SET)) {
					*set32 |= pulse_mask;
				} else {
					*set32 &= (~pulse_mask);
				}
				
				if(map[i] & (1 << EVR_PULSE_CFG_BIT_TRIGGER)) {
					*trg32 |= pulse_mask;
				} else {
					*trg32 &= (~pulse_mask);
				}
			}
			
			ret = 0;
		}

		if(reading) {
			
			if(event_count == 1) {
			
				if (copy_to_user((void *)arg, &pulse_map_ram1, 
							sizeof(struct vevr_ioctl_pulse_map_ram_for_event))) {
					return -EFAULT;
				}
			
			} else {

				if (copy_to_user((void *)arg, &pulse_map_ram256, 
							sizeof(struct vevr_ioctl_pulse_map_ram))) {
					return -EFAULT;
				}
			}
		} else {
			// the changes must be written to HW
			evr_ram_map_change_flush(hw_support_data);
		}
		
		break;
	}
	
	
	} // switch
	
	return ret;
}

static int hw_support_evr_vdev_mmap_ro(struct modac_hw_support_data *hw_support_data, 
				unsigned long offset, unsigned long vsize,
				unsigned long *physical)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;

	// must not go over what's available
	if(vsize > hw_data->mmap_p_final_size) return -EINVAL;
	
	*physical = (unsigned long)hw_data->mmap_p;
	return 0;
}

static int hw_support_evr_init_res(struct modac_hw_support_data *hw_support_data,
				int res_type, int res_index)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	
	if(hw_data->sim != NULL) {
		return evr_sim_init_res(hw_data, res_type, res_index);
	}

	if(res_type == EVR_RES_TYPE_PULSEGEN) {
		
		int pulse_start_reg = EVR_REG_PULSES + EVR_REG_PULSE_SLOT_SIZE * res_index;
		int i;
		u32 pulse_inv_mask = (~(1 << res_index));
		
		set_pulse_params(hw_support_data, pulse_start_reg, 0, 0, 0);
		
		evr_write32(hw_support_data, 
					pulse_start_reg + EVR_REG_PULSE_CTRL_OFFSET, 0);
		
		for(i = EVRMA_FIFO_MIN_EVENT_CODE; i <= EVRMA_FIFO_MAX_EVENT_CODE; i ++) {
			hw_data->map_ram[i].pulse_clear &= pulse_inv_mask;
			hw_data->map_ram[i].pulse_set &= pulse_inv_mask;
			hw_data->map_ram[i].pulse_trigger &= pulse_inv_mask;
		}
		
		// the changes must be written to HW
		evr_ram_map_change_flush(hw_support_data);
		
	} else if(res_type == EVR_RES_TYPE_OUTPUT) {
		
		// nothing to do, the output config is not defined by the VEVR

	} else {
		return -ENOSYS;
	}
	
	return 0;
}

struct modac_hw_support_def hw_support_evr = {
	hw_name: MODAC_HW_EVR_ID,
	init: hw_support_evr_init,
	end: hw_support_evr_end,
	isr: hw_support_evr_isr,
	ioctl: hw_support_evr_ioctl,
	on_subscribe_change: hw_support_evr_on_subscribe_change,
	init_res: hw_support_evr_init_res,
	vdev_mmap_ro: hw_support_evr_vdev_mmap_ro,
	store_dbg: hw_support_evr_store_dbg,
	show_dbg: hw_support_evr_show_dbg,
	dbg_res: hw_support_evr_dbg_res,
	dbg_regs: hw_support_evr_dbg_regs,
	dbg_info: hw_support_evr_dbg_info,
};
