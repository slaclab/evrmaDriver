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

ssize_t hw_support_evr_store_dbg(struct modac_hw_support_data *hw_support_data, 
						const char *buf, size_t count)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	
	if(hw_data->sim != NULL) {
		return evr_sim_dbg(hw_data, buf, count);
	}
	
	return count;
}

static ssize_t show_map_ram_bits(struct modac_hw_support_data *hw_support_data,
								 char *buf, size_t count, int mapram, int bit)
{
	int i = 0;
	ssize_t n = 0;
	u32 rval;

	n += scnprintf(buf + n, count - n, "MapRam[%d],bit=%d:", mapram, bit);
	
	for(i = EVRMA_FIFO_MIN_EVENT_CODE; i  <= EVRMA_FIFO_MAX_EVENT_CODE; i ++) {
		u32 ram_start = (mapram ? EVR_REG_MAPRAM2 : EVR_REG_MAPRAM1) +
					i * EVR_REG_MAPRAM_SLOT_SIZE;
		int bit_val;
		rval = evr_read32(hw_support_data, ram_start + 4 * (3 - (bit / 32)));

// 		n += scnprintf(buf + n, count - n, "%d", (rval >> (bit % 32)) & 1);

		// instead of printing out 0/1 string the 1 bit numbers are output
		bit_val =  (rval >> (bit % 32)) & 1;
		if(bit_val) {
			n += scnprintf(buf + n, count - n, "%d ", i);
		}
	}
	
	n += scnprintf(buf + n, count - n, "\n");
	return n;
}


ssize_t hw_support_evr_show_dbg(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	ssize_t n = 0;
	u32 rval;

	if(hw_data->sim != NULL) {
		return n;
	}
	
	rval = evr_read32(hw_support_data, EVR_REG_CTRL);
	n += scnprintf(buf + n, count - n, "CTRL[MASTER_ENABLE]=%d\n", 
				  (rval >> C_EVR_CTRL_MASTER_ENABLE) & 1);
	n += scnprintf(buf + n, count - n, "CTRL[MAP_RAM_ENABLE]=%d\n", 
				  (rval >> C_EVR_CTRL_MAP_RAM_ENABLE) & 1);
	n += scnprintf(buf + n, count - n, "CTRL[MAP_RAM_SELECT]=%d\n", 
				  (rval >> C_EVR_CTRL_MAP_RAM_SELECT) & 1);
	
	rval = evr_read32(hw_support_data, EVR_REG_IRQEN);
	n += scnprintf(buf + n, count - n, "IRQEN[MASTER_ENABLE]=%d\n", 
				  (rval >> C_EVR_IRQ_MASTER_ENABLE) & 1);
	n += scnprintf(buf + n, count - n, "IRQEN[DATABUF]=%d\n", 
				  (rval >> C_EVR_IRQFLAG_DATABUF) & 1);
	n += scnprintf(buf + n, count - n, "IRQEN[PULSE]=%d\n", 
				  (rval >> C_EVR_IRQFLAG_PULSE) & 1);
	n += scnprintf(buf + n, count - n, "IRQEN[EVENT]=%d\n", 
				  (rval >> C_EVR_IRQFLAG_EVENT) & 1);
	n += scnprintf(buf + n, count - n, "IRQEN[HEARTBEAT]=%d\n", 
				  (rval >> C_EVR_IRQFLAG_HEARTBEAT) & 1);
	n += scnprintf(buf + n, count - n, "IRQEN[FIFOFULL]=%d\n", 
				  (rval >> C_EVR_IRQFLAG_FIFOFULL) & 1);
	n += scnprintf(buf + n, count - n, "IRQEN[VIOLATION]=%d\n", 
				  (rval >> C_EVR_IRQFLAG_VIOLATION) & 1);
	
	rval = evr_read32(hw_support_data, EVR_REG_STATUS);
	n += scnprintf(buf + n, count - n, "STATUS[LEGVIO]=%d\n", 
				  (rval >> C_EVR_REG_STATUS_LEGVIO) & 1);
	n += scnprintf(buf + n, count - n, "STATUS[FIFOSTP]=%d\n", 
				  (rval >> C_EVR_REG_STATUS_FIFOSTP) & 1);
	n += scnprintf(buf + n, count - n, "STATUS[LINK]=%d\n", 
				  (rval >> C_EVR_REG_STATUS_LINK) & 1);
	n += scnprintf(buf + n, count - n, "STATUS[SFPMOD]=%d\n", 
				  (rval >> C_EVR_REG_STATUS_SFPMOD) & 1);
	
	rval = evr_read32(hw_support_data, EVR_REG_DATA_BUF_CTRL);
	n += scnprintf(buf + n, count - n, "DATA_BUF_CTRL[DATABUF_MODE]=%d\n", 
				  (rval >> C_EVR_DATABUF_MODE) & 1);
	n += scnprintf(buf + n, count - n, "DATA_BUF_CTRL[DATABUF_RECEIVING]=%d\n", 
				  (rval >> C_EVR_DATABUF_RECEIVING) & 1);
	n += scnprintf(buf + n, count - n, "DATA_BUF_CTRL[DATABUF_RXREADY]=%d\n", 
				  (rval >> C_EVR_DATABUF_RXREADY) & 1);
	n += scnprintf(buf + n, count - n, "DATA_BUF_CTRL[DATABUF_CHECKSUM]=%d\n", 
				  (rval >> C_EVR_DATABUF_CHECKSUM) & 1);
	
	n += scnprintf(buf + n, count - n, "DATA_BUF_CTRL[RXSIZE]=0x%x\n", 
				  (rval >> C_EVR_DATABUF_RXSIZE) & C_EVR_DATABUF_RXSIZE_MASK);
	
	rval = evr_read32(hw_support_data, EVR_REG_USEC_DIV);
	n += scnprintf(buf + n, count - n, "USEC_DIV=0x%x\n", rval);
	
	rval = evr_read32(hw_support_data, EVR_REG_FRAC_DIV);
	n += scnprintf(buf + n, count - n, "FRAC_DIV=0x%x\n", rval);

	n += show_map_ram_bits(hw_support_data, buf + n, count - n, 0, EVR_REG_MAPRAM_EVENT_BIT_SAVE_EVENT_IN_FIFO);
	n += show_map_ram_bits(hw_support_data, buf + n, count - n, 1, EVR_REG_MAPRAM_EVENT_BIT_SAVE_EVENT_IN_FIFO);
	
	return n;
}

ssize_t hw_support_evr_dbg_res(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count, int res_type,
						int res_index)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;

	ssize_t n = 0;
	
	if(hw_data->sim != NULL) {
		return evr_sim_dbg_res(hw_data, buf, count, res_type, res_index);
	}
	
	// the real EVR, not the simulation
	
	if(res_type == EVR_RES_TYPE_PULSEGEN) {
		if(res_index < 0 || res_index >= hw_data->evr_type_data.pulsegen_count) {
			n += scnprintf(buf + n, count - n, "invalid");
		} else {
			n += scnprintf(buf + n, count - n, "abs:%d,bits:presc=%d;delay=%d;width=%d", 
					res_index, 
					hw_data->evr_type_data.pulsegen_data[res_index].prescaler_bits,
					hw_data->evr_type_data.pulsegen_data[res_index].delay_bits,
					hw_data->evr_type_data.pulsegen_data[res_index].width_bits);
		}
	} else if(res_type == EVR_RES_TYPE_OUTPUT) {
		if(res_index < 0 || res_index >= hw_data->out_res_count) {
			n += scnprintf(buf + n, count - n, "invalid");
		} else {
			
			const char *name;
			int rel_index;
			int out_map;
			
			if(res_index < hw_data->out_cfg[EVR_OUT_TYPE_FP_TTL].res_start +
					hw_data->out_cfg[EVR_OUT_TYPE_FP_TTL].res_count) {
				name = "FP_TTL";
				rel_index = res_index - hw_data->out_cfg[EVR_OUT_TYPE_FP_TTL].res_start;
			} else if(res_index < hw_data->out_cfg[EVR_OUT_TYPE_FP_UNIV].res_start +
					hw_data->out_cfg[EVR_OUT_TYPE_FP_UNIV].res_count) {
				name = "FP_UNIV";
				rel_index = res_index - hw_data->out_cfg[EVR_OUT_TYPE_FP_UNIV].res_start;
			} else {
				name = "TB";
				rel_index = res_index - hw_data->out_cfg[EVR_OUT_TYPE_TB_OUTPUT].res_start;
			}
			
			n += scnprintf(buf + n, count - n, "OUT[%d]=%s[%d]", 
					res_index, name, rel_index);
			
			out_map = internal_evr_get_out_map(hw_support_data, res_index);
			if(out_map >= 0) {
				n += scnprintf(buf + n, count - n, ",MAP=%d", out_map);
			}

		}
	}
	
	return n;
}

static ssize_t print_regs(struct modac_hw_support_data *hw_support_data,
			char *buf, size_t count, u32 regs_offset, u32 regs_length)
{
	int i = 0;
	ssize_t n = 0;
	u32 regs_end1 = regs_offset + regs_length;
	
	if(
			// rollover check
			regs_end1 < regs_offset ||
			// more than available size
			regs_end1 > hw_support_data->mngdev_des->io_size) {
		
		n += scnprintf(buf + n, count - n, "Bad range, offset=0x%x length=%d", regs_offset, regs_length);
		return n;
	}
	
	for(i = 0; i < regs_length / 4; i ++) {
		u32 reg = evr_read32(hw_support_data, regs_offset + i * 4);
		
		if(i % 8 == 0) {
			n += scnprintf(buf + n, count - n, "%6.6X  ", regs_offset + i * 4);
		}
		
		n += scnprintf(buf + n, count - n, "%8.8X ", reg);
		
		if(i % 8 == 7) {
			n += scnprintf(buf + n, count - n, "\n");
		}
	}
	
	return n;
}



ssize_t hw_support_evr_dbg_regs(struct modac_hw_support_data *hw_support_data, 
						u32 regs_offset, u32 regs_length,
						char *buf, size_t count)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;

	ssize_t n = 0;
	
	if(hw_data->sim != NULL) {
		n += scnprintf(buf + n, count - n, "no_regs");
	} else {
		n += print_regs(hw_support_data, buf + n, count - n, regs_offset, regs_length);
	}
	
	return n;
}

ssize_t hw_support_evr_dbg_info(struct modac_hw_support_data *hw_support_data, 
						char *buf, size_t count)
{
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;

	ssize_t n = 0;
	
	if(hw_data->sim != NULL) {
		n += scnprintf(buf + n, count - n, "simulation");
	} else {
		n += scnprintf(buf + n, count - n, "%s", hw_data->evr_type_data.name);
		n += scnprintf(buf + n, count - n, ", hw_support_hint1=%d", hw_support_data->mngdev_des->hw_support_hint1);
	}
	return n;
}

