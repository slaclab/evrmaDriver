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

#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER

u32 dbg_get_time(struct modac_hw_support_data *hw_support_data)
{
	u32 val;
	
	val = evr_read32(hw_support_data, EVR_REG_CTRL);
	val |= (1 << C_EVR_CTRL_LATCH_TIMESTAMP);
	evr_write32(hw_support_data, EVR_REG_CTRL, val);
	
	return evr_read32(hw_support_data, EVR_REG_TIMESTAMP_LATCH);
}

#endif

irqreturn_t hw_support_evr_isr(struct modac_hw_support_data *hw_support_data, void *data)
{
	struct modac_mngdev_des *devdes = hw_support_data->mngdev_des;
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	
#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER
	u32 arrival_time = dbg_get_time(hw_support_data);
#endif
			
	/*
	At this point we examine and process the registers and create events.
	The events are dispatched by calling modac_mngdev_notify/modac_device_put_event.
	*/
	
	u32 irq_flags;
	
	irq_flags = evr_read32(hw_support_data, EVR_REG_IRQFLAG);
	
	{
        /* Clear everything but FIFOFULL. */
		
		u32 irq_flags_no_fifo_full = irq_flags & ~EVR_IRQFLAG_FIFOFULL;
		
        if(irq_flags_no_fifo_full){
			evr_write32(hw_support_data, EVR_REG_IRQFLAG, irq_flags_no_fifo_full);
			evr_write32(hw_support_data, EVR_REG_IRQFLAG, 0); // for SLAC card
			barrier();
		}
	}
	
	if(irq_flags & EVR_IRQFLAG_DATABUF) {
		
		struct vevr_mmap_data *mmap_data = 
				(struct vevr_mmap_data *)hw_data->mmap_p;
		
		int databuf_sts = evr_read32(hw_support_data, EVR_REG_DATA_BUF_CTRL);
		
		struct evr_data_buff_slot_data *slot;
		
		// one slot only is used
		slot = &mmap_data->data_buff;

		if(!(databuf_sts & (1<<C_EVR_DATABUF_CHECKSUM))) {
			/* If no checksum error, grab the buffer too. */
			u32 *dd   = slot->data;
			int i;
			
			// the number of u32, hence >> 2
			slot->size32 = ((databuf_sts >> C_EVR_DATABUF_RXSIZE) & C_EVR_DATABUF_RXSIZE_MASK) >> 2;

			for (i = 0;  i < slot->size32;  i++) {
				dd[i] = evr_read32(hw_support_data, EVR_REG_DATA_BUF + (i << 2));
			}

			slot->status = databuf_sts;
			
		} else {
			slot->size32 = 0;
			slot->status = databuf_sts;
		}
		
		{
			/* reenable */
			u32 dbctl = evr_read32(hw_support_data, EVR_REG_DATA_BUF_CTRL);
			evr_write32(hw_support_data, EVR_REG_DATA_BUF_CTRL, 
									dbctl | (1 << C_EVR_DATABUF_LOAD));
		}
		
		modac_mngdev_notify(devdes, EVRMA_EVENT_DBUF_DATA);

	}
	
	if(irq_flags & EVR_IRQFLAG_EVENT) {
		
		int ilim = EVR_FIFO_EVENT_LIMIT;
		
		while(ilim --) {
			
			u32 stat;

			struct evr_data_fifo_event et_data;

			int event = evr_read32(hw_support_data, EVR_REG_FIFO_EVENT) & 0xFF;
			et_data.seconds = evr_read32(hw_support_data, EVR_REG_FIFO_SECONDS);
			et_data.timestamp = evr_read32(hw_support_data, EVR_REG_FIFO_TIMESTAMP);

#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER
			et_data.dbg_timestamp[0] = arrival_time;
#endif

			modac_mngdev_put_event(devdes, event, &et_data, sizeof(et_data));
			
			stat = evr_read32(hw_support_data, EVR_REG_IRQFLAG);
			
			if(!(stat & EVR_IRQFLAG_EVENT)) break;
		}
		
		if(ilim < 1) {
			printk(KERN_WARNING "EVR FIFO event max. read count reached.");
		}
	}

	if(irq_flags & EVR_IRQFLAG_FIFOFULL) {
		
		// reset the FIFO and start from scratch
		u32 ctrl = evr_read32(hw_support_data, EVR_REG_CTRL);
		ctrl |= (1 << C_EVR_CTRL_RESET_EVENTFIFO);
		evr_write32(hw_support_data, EVR_REG_CTRL, ctrl);

		evr_write32(hw_support_data, EVR_REG_IRQFLAG, EVR_IRQFLAG_FIFOFULL);
		evr_write32(hw_support_data, EVR_REG_IRQFLAG, 0); // for SLAC card
		
		modac_mngdev_notify(devdes, EVRMA_EVENT_ERROR_LOST);
		
	}

	if(irq_flags & EVR_IRQFLAG_PULSE) {
		modac_mngdev_notify(devdes, EVRMA_EVENT_DELAYED_IRQ);
	}
	
	if(irq_flags & EVR_IRQFLAG_HEARTBEAT) {
		modac_mngdev_notify(devdes, EVRMA_EVENT_ERROR_HEART);
	}
	
	if(irq_flags & EVR_IRQFLAG_VIOLATION) {
		modac_mngdev_notify(devdes, EVRMA_EVENT_ERROR_TAXI);
	}

	return IRQ_HANDLED;
}

int hw_support_evr_on_subscribe_change(struct modac_hw_support_data *hw_support_data,
		const struct event_list_type *subscriptions)
{
	struct modac_mngdev_des *devdes = hw_support_data->mngdev_des;
	struct evr_hw_data *hw_data = (struct evr_hw_data *)hw_support_data->priv;
	int evst;
	u32 irq_enable_prev;
	u32 irq_enable;
	int interrupts_needed;

	if(hw_data->sim != NULL) {
		return 0;
	}
	
	interrupts_needed = !event_list_is_empty(subscriptions);

	// first inform the lower system level about enabled interrupts;
	if(devdes->irq_set != NULL) {
		devdes->irq_set(devdes, interrupts_needed);
	}
	
	irq_enable_prev = evr_read32(hw_support_data, EVR_REG_IRQEN);
	
	// we have to set this not to change bits belonging to the lower system 
	irq_enable = irq_enable_prev & (1 << C_EVR_IRQ_PCIEE);
	

	if(interrupts_needed) {
		irq_enable |= EVR_IRQ_MASTER_ENABLE;
	}

	// --------- make the EVR HW produce only the needed events ----------

	{
		int ievent;
		u32 mask_save_event_in_fifo = (1 << (EVR_REG_MAPRAM_EVENT_BIT_SAVE_EVENT_IN_FIFO % 32));
		int fifo_events_enabled = 0;
		
		for(ievent = EVRMA_FIFO_MIN_EVENT_CODE; ievent <= EVRMA_FIFO_MAX_EVENT_CODE; ievent ++) {
			
			u32 *func32;
					
			func32 = &hw_data->map_ram[ievent].int_event;
			
			// only save subscribed Event FIFO events
			evst = event_list_test(subscriptions, ievent);

			if(evst) {
				fifo_events_enabled = 1;
				*func32 |= mask_save_event_in_fifo;
			} else {
				*func32 &= (~mask_save_event_in_fifo);
			}
		}
		
		evr_ram_map_change_flush(hw_support_data);
		
		// is true if at least one Event FIFO event is enabled
		if(fifo_events_enabled) {
			irq_enable |= EVR_IRQFLAG_EVENT;
		}
	}

	// is DataBuff needed?
	evst = event_list_test(subscriptions, EVRMA_EVENT_DBUF_DATA);
	
	if(evst) {
		
		if((irq_enable_prev & EVR_IRQFLAG_DATABUF) == 0) {
		
			/* On DataBuf start this must be written. When the DataBuf
			 * is already running this mustn't be touched because it
			 * influences the DataBuf IRQ triggering and would spoil the
			 * interrupts for other VEVRs if present.
			 */
			evr_write32(hw_support_data, EVR_REG_DATA_BUF_CTRL,
					(1 << C_EVR_DATABUF_MODE) | (1 << C_EVR_DATABUF_LOAD));
		}
		
		irq_enable |= EVR_IRQFLAG_DATABUF;
	} else {
		evr_write32(hw_support_data, EVR_REG_DATA_BUF_CTRL,
				(1 << C_EVR_DATABUF_STOP));
	}

	// other statuses
	
	evst = event_list_test(subscriptions, EVRMA_EVENT_ERROR_TAXI);
	if(evst) {
		irq_enable |= EVR_IRQFLAG_VIOLATION;
	}

	evst = event_list_test(subscriptions, EVRMA_EVENT_DELAYED_IRQ);
	if(evst) {
		irq_enable |= EVR_IRQFLAG_PULSE;
	}

	evst = event_list_test(subscriptions, EVRMA_EVENT_ERROR_HEART);
	if(evst) {
		irq_enable |= EVR_IRQFLAG_HEARTBEAT;
	}

	evst = event_list_test(subscriptions, EVRMA_EVENT_ERROR_LOST);
	if(evst) {
		irq_enable |= EVR_IRQFLAG_FIFOFULL;
	}

	if(irq_enable_prev != irq_enable) {
		// set the combined irq enabling mask
		evr_write32(hw_support_data, EVR_REG_IRQEN, irq_enable);
	}
	
	return 0;
}

