//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef MODAC_EVENT_LIST_H_
#define MODAC_EVENT_LIST_H_

#include <linux/bitmap.h>

/* ----------------------- event list type ------------------------ */
 
/* Must be divisible by sizeof(unsigned long). */
#define EVENT_LIST_TYPE_MAX_EVENTS 512

struct event_list_type {
	/*
	 * Up to EVENT_LIST_TYPE_MAX_EVENTS event codes can be subscribed to.
	 */
	unsigned long mask[EVENT_LIST_TYPE_MAX_EVENTS / sizeof(unsigned long)];
};


void event_list_clear(
		struct event_list_type *event_list);

int event_list_test(
		const struct event_list_type *event_list, int event);

void event_list_add(
		struct event_list_type *event_list, int event);

void event_list_remove(
		struct event_list_type *event_list, int event);

int event_list_is_empty(
		const struct event_list_type *event_list);

/* return a negative value if none found */
int event_list_extract_one(
		struct event_list_type *event_list);




/* ----------------------- event dispatch list ---------------------- */
 
/* 
 * Should match MAX_VIRT_DEVS_PER_MNG_DEV.
 */
#define EVENT_DISPATCH_LIST_MAX_SUBSCRIBERS 31

struct event_dispatch_list {
	void *subs[EVENT_LIST_TYPE_MAX_EVENTS][EVENT_DISPATCH_LIST_MAX_SUBSCRIBERS];
};


typedef void (* event_dispatch_list_callback)(void *subscriber, void *arg);


void event_dispatch_list_init(
		struct event_dispatch_list *list);

int event_dispatch_list_add(
		struct event_dispatch_list *list, void *subscriber, int event);

void event_dispatch_list_remove(
		struct event_dispatch_list *list, void *subscriber, int event);

void event_dispatch_list_remove_all(
		struct event_dispatch_list *list, void *subscriber);

/* Adds the list from all events used anywhere. */
void event_dispatch_list_add_subscribed_events(
		struct event_dispatch_list *list, struct event_list_type *all_events);

void event_dispatch_list_for_all_subscribers(
		struct event_dispatch_list *list, int event, 
		event_dispatch_list_callback callback, void *arg);

int event_dispatch_list_dbg(
		struct event_dispatch_list *list,
		char *buf, size_t count, void *subscriber);


 #endif /* MODAC_EVENT_LIST_H_ */
