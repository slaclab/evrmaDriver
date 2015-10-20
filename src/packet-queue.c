#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "internal.h"
#include "packet-queue.h"
#include "linux-modac.h"
 
void modac_cb_init(struct modac_circ_buf *cb)
{
	cb->cb_events.buf = (char *)cb->buf;
	cb->cb_events.head = cb->cb_events.tail = 0;
	cb->overflow_written = 0;
}

int modac_cb_put(struct modac_circ_buf *cb, int event, void *data, int length, 
				   wait_queue_head_t *wait_queue_events)
{
	unsigned long head = cb->cb_events.head;
	unsigned long tail = ACCESS_ONCE(cb->cb_events.tail);
	int space;
	
	if(length > CBUF_EVENT_ENTRY_DATA_LENGTH) {
		printk(KERN_ERR "Too long for the CB: %d\n", length);
		return -ENOMEM;
	}

	space = CIRC_SPACE(head, tail, CBUF_EVENT_COUNT);
	
	if(space >= 1) {

		/* insert items into the buffer */
		
		if(space > 1) {
			
			cb->buf[head].event = event;
			cb->buf[head].length = length;
			memcpy(&cb->buf[head].data, data, length);
			cb->overflow_written = 0;
			
		} else {
			
			/* 
			 * Exactly one slot is left. This indicates a full queue. A special
			 * overflow event is inserted in that case. 'overflow_written'
			 * makes sure this doesn't happen twice in a row.
			 * The incoming event is discarded.
			 */
			
			if(!cb->overflow_written) {
				cb->buf[head].event = MODAC_EVENT_READ_OVERFLOW;
				cb->buf[head].length = 0;
				cb->overflow_written = 1;
			}
		}

		smp_wmb(); /* commit the item before incrementing the head */
		
		cb->cb_events.head = (head + 1) & (CBUF_EVENT_COUNT - 1);
	
		/* wake_up() will make sure that the head is committed before
		* waking anyone up */
		wake_up_interruptible(wait_queue_events);
		return 0;
	} else {
		return -ENOMEM;
	}
}

int modac_cb_get(struct modac_circ_buf *cb, u8 *buf)
{	
	unsigned long head = ACCESS_ONCE(cb->cb_events.head);
	unsigned long tail = cb->cb_events.tail;

	if(CIRC_CNT(head, tail, CBUF_EVENT_COUNT) > 0) {
		
		struct modac_circ_buf_entry *entry;
		
		/* read index before reading contents at that index */
		smp_read_barrier_depends();
		
		entry = &cb->buf[tail];
		memcpy(buf, &entry->event, 2);
		memcpy(buf + 2, entry->data, entry->length);
		
		smp_mb(); /* finish reading descriptor before incrementing tail */

		cb->cb_events.tail = (tail + 1) & (CBUF_EVENT_COUNT - 1);
		
		return 2 + entry->length;
	}
	
	return 0;
}

int modac_cb_available(struct modac_circ_buf *cb)
{
	unsigned long head = ACCESS_ONCE(cb->cb_events.head);
	unsigned long tail = cb->cb_events.tail;

	return CIRC_CNT(head, tail, CBUF_EVENT_COUNT) > 0;
}



