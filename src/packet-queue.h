//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef PACKET_QUEUE_H_
#define PACKET_QUEUE_H_

#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER
// TODO: temporarily, to set DBG_MEASURE_TIME_FROM_IRQ_TO_USER
#include "linux-evrma.h"
#warning **********************************************************************
#warning **********************************************************************
#warning **********************************************************************
#warning Please, remove #include "linux-evrma.h" now that it is not needed anymore.
#warning **********************************************************************
#warning **********************************************************************
#warning **********************************************************************
#endif

/*
 * NOTE: According to Documentation/circular-buffers.txt all of these functions
 * (except modac_cb_init) must be protected with a spin lock.
 */

#define CBUF_EVENT_COUNT 1024 /* must be a power of 2 */

#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER
	
#define CBUF_EVENT_ENTRY_DATA_LENGTH 28

#else

/* 
 * Up to 3 words of data allowed. Note that the struct modac_circ_buf_entry
 * will have 4 words in total.
 */
#define CBUF_EVENT_ENTRY_DATA_LENGTH 12
	
#endif

struct modac_circ_buf_entry {
	/*
	 * The event is stored as an int value throughout the system and
	 * -1 means an invalid event. 
	 * The pure value of event is always 16-bit, that's all to be stored here.
	 */
	u16 event;
	u16 length;
	u8  data[CBUF_EVENT_ENTRY_DATA_LENGTH];
};

struct modac_circ_buf {
	struct circ_buf               cb_events;
	int                           overflow_written;
	struct modac_circ_buf_entry   buf[CBUF_EVENT_COUNT];
};

void modac_cb_init(struct modac_circ_buf *cb);

/* Return negative value on error */
int modac_cb_put(struct modac_circ_buf *cb, int event, void *data, int length, 
				   wait_queue_head_t *wait_queue_events);

/* 
 * Return number of message bytes. 'buf' must be able to accomodate
 * CBUF_EVENT_ENTRY_DATA_LENGTH + 2 bytes. 
 */
int modac_cb_get(struct modac_circ_buf *cb, u8 *buf);
/* Return non-zero if data available. */
int modac_cb_available(struct modac_circ_buf *cb);

#endif /* PACKET_QUEUE_H_ */
