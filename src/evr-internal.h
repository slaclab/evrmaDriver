//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef EVRMA_INTERNAL_H_
#define EVRMA_INTERNAL_H_

#include <linux/irqreturn.h>

#include "internal.h"

#include "linux-evrma.h"
#include "linux-evr-regs.h"
#include "evr.h"
#include "event-list.h"

#define EVR_FIFO_EVENT_LIMIT 256

#define EVR_MAX_PULSEGEN_COUNT 16

#define OUTPUT_REG_MAPPING_FORCE_LOW 63

struct evr_pulsegen_bit_info {
	int prescaler_bits;
	int delay_bits;
	int width_bits;
};

struct evr_map_ram_item_struct {
  u32  int_event;
  u32  pulse_trigger;
  u32  pulse_set;
  u32  pulse_clear;
};

#define EVR_MAPRAM_EVENT_CODES 256

struct evr_type_data {
	char name[MODAC_ID_MAX_NAME + 1];
	int pulsegen_count;
	struct evr_pulsegen_bit_info pulsegen_data[EVR_MAX_PULSEGEN_COUNT];
	
	int input_count;
	int output_count;
	int cml;
	int univ_io;
	int univ_io_slots;
	int univ_gpio;
	int univ_gpio_slots;
	int tb_outputs;
	int tb_inputs;
	int prescalers;
	int prescalers_length;
	
};

enum {
	EVR_OUT_TYPE_FP_TTL,
	EVR_OUT_TYPE_FP_UNIV,
	EVR_OUT_TYPE_TB_OUTPUT,
	
	// These do not have the same register image
	//EVR_OUT_TYPE_FP_CML,
	
	// What to do with GPIO?
	//EVR_OUT_TYPE_UNIV_GPIO,
	
	EVR_OUT_TYPE_COUNT
};

struct evr_hw_data_out_cfg {
	// where in the output resource list this output type starts
	int res_start;
	// number of this type outputs in the output resource list
	int res_count;
	// EVR IO register start used to map the output
	u32 evr_map_reg_start;
};

struct evr_hw_data {
	
	u8 mmap_mem[sizeof(struct vevr_mmap_data) + PAGE_SIZE];
	u8 *mmap_p;
	// after aligning to PAGE_SIZE we may obtain a larger memory than
	// actually needed. We have to keep this because from mmap command we
	// may also obtain a request for bigger memory.
	int mmap_p_final_size;
	
	struct modac_hw_support_data *hw_support_data;
	
	// a (possibly modified) copy of one of the documented_evr_type_data_table
	struct evr_type_data evr_type_data;
	
	int fw_version;
	
	int out_res_count;
	
	struct evr_hw_data_out_cfg out_cfg[EVR_OUT_TYPE_COUNT];
	
	// the current value of the map ram; The first idea was to do all in the
	// MapRam itself, but there were some problems with copy from one to
	// the other MapRam bank.
	struct evr_map_ram_item_struct map_ram[EVR_MAPRAM_EVENT_CODES];
	
	void *sim;
};

ssize_t hw_support_evr_store_dbg(struct modac_hw_support_data *hw_support_data, 
						const char *buf, size_t count);
ssize_t hw_support_evr_show_dbg(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count);
ssize_t hw_support_evr_dbg_res(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count, int res_type,
						int res_index);
ssize_t hw_support_evr_dbg_regs(struct modac_hw_support_data *hw_support_data, 
						u32 regs_offset, u32 regs_length,
						char *buf, size_t count);
ssize_t hw_support_evr_dbg_info(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count);
irqreturn_t hw_support_evr_isr(struct modac_hw_support_data *hw_support_data, 
							   void *data);
int hw_support_evr_on_subscribe_change(struct modac_hw_support_data *hw_support_data,
		const struct event_list_type *subscriptions);

void evr_ram_map_change_flush(
		struct modac_hw_support_data *hw_support_data);

int internal_evr_get_out_map(struct modac_hw_support_data *hw_support_data, 
						int res_output_index);

u16 evr_read16(struct modac_hw_support_data *hw_support_data, int reg);
void evr_write16(struct modac_hw_support_data *hw_support_data, int reg, u16 val);
u32 evr_read32(struct modac_hw_support_data *hw_support_data, int reg);
void evr_write32(struct modac_hw_support_data *hw_support_data, int reg, u32 val);


#endif /* EVRMA_INTERNAL_H_ */
