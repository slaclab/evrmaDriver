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
#include "event-list.h"

/* ----------------------- event list type ------------------------ */
 
void event_list_clear(struct event_list_type *event_list)
{
	bitmap_zero(event_list->mask, EVENT_LIST_TYPE_MAX_EVENTS);
}

int event_list_is_empty(const struct event_list_type *event_list)
{
	return bitmap_empty(event_list->mask, EVENT_LIST_TYPE_MAX_EVENTS);
}

int event_list_test(const struct event_list_type *event_list,
					int event)
{
	if(event < 0 || event >= EVENT_LIST_TYPE_MAX_EVENTS) return 0;
	
	return test_bit(event, event_list->mask);
}

void event_list_add(struct event_list_type *event_list,
					int event)
{
	if(event < 0 || event >= EVENT_LIST_TYPE_MAX_EVENTS) return;
	
	return set_bit(event, event_list->mask);

}

void event_list_remove(struct event_list_type *event_list,
					int event)
{
	if(event < 0 || event >= EVENT_LIST_TYPE_MAX_EVENTS) return;
	
	return clear_bit(event, event_list->mask);
}

int event_list_extract_one(struct event_list_type *event_list)
{
	int ret = find_first_bit(event_list->mask, EVENT_LIST_TYPE_MAX_EVENTS);
	if(ret < EVENT_LIST_TYPE_MAX_EVENTS) 
		return ret;
	
	return -1;
}



/* ----------------------- event dispatch list ---------------------- */


void event_dispatch_list_init(struct event_dispatch_list *list)
{
	memset(list, 0, sizeof(struct event_dispatch_list));
}

int event_dispatch_list_add(struct event_dispatch_list *list, void *subscriber, int event)
{
	int i;
	void **event_subs;
	
	if(event < 0 || event >= EVENT_LIST_TYPE_MAX_EVENTS) return -EINVAL;
	
	event_subs = &list->subs[event][0];
	
	for(i = 0; i < EVENT_DISPATCH_LIST_MAX_SUBSCRIBERS; i ++) {
		if(event_subs[i] == NULL) {
			/* add to the list and finish */
			event_subs[i] = subscriber;
			return 0;
		} else {
			/* already added? finish then. */
			if(event_subs[i] == subscriber) 
				return 0;
		}
	}
	
	return -ENOMEM;
}

void event_dispatch_list_remove(
			struct event_dispatch_list *list, void *subscriber, int event)
{
	int i;
	void **event_subs;
	
	if(event < 0 || event >= EVENT_LIST_TYPE_MAX_EVENTS) return;
	
	event_subs = &list->subs[event][0];
	
	for(i = 0; i < EVENT_DISPATCH_LIST_MAX_SUBSCRIBERS; i ++) {
		if(event_subs[i] == NULL) break; /* no more */
		if(event_subs[i] == subscriber) {
			/* move all by 1 so the entry is overwritten */
			memmove(event_subs + i, event_subs + i + 1, 
				(EVENT_DISPATCH_LIST_MAX_SUBSCRIBERS - i - 1) * sizeof(void *));
			/* the last is surely emtpy now: set to NULL */
			event_subs[EVENT_DISPATCH_LIST_MAX_SUBSCRIBERS - 1] = NULL;
			break;
		}
	}
}

void event_dispatch_list_remove_all(
			struct event_dispatch_list *list, void *subscriber)
{
	int ievent;
	
	for(ievent = 0; ievent < EVENT_LIST_TYPE_MAX_EVENTS; ievent ++) {
		event_dispatch_list_remove(list, subscriber, ievent);
	}
}

void event_dispatch_list_add_subscribed_events(struct event_dispatch_list *list,
				struct event_list_type *all_events)
{
	int ievent;
	
	for(ievent = 0; ievent < EVENT_LIST_TYPE_MAX_EVENTS; ievent ++) {
		if(list->subs[ievent][0] != NULL) {
			/* the event is used by at least one */
			event_list_add(all_events, ievent);
		}
	}
}

void event_dispatch_list_for_all_subscribers(struct event_dispatch_list *list, 
			int event, event_dispatch_list_callback callback, void *arg)
{
	void **event_subs;
	int i;
	
	if(event < 0 || event >= EVENT_LIST_TYPE_MAX_EVENTS) return;
	
	event_subs = &list->subs[event][0];
	
	for(i = 0; i < EVENT_DISPATCH_LIST_MAX_SUBSCRIBERS; i ++) {
		if(event_subs[i] == NULL) {
			/* no more */
			break;
		} else {
			callback(event_subs[i], arg);
		}
	}
}

int event_dispatch_list_dbg(struct event_dispatch_list *list, 
							char *buf, size_t count, void *subscriber)
{
	int ievent;
	void **event_subs;
	ssize_t n = 0;
	int i;
	
	n += scnprintf(buf + n, count - n, "subs[");
	
	for(ievent = 0; ievent < EVENT_LIST_TYPE_MAX_EVENTS; ievent ++) {

		event_subs = &list->subs[ievent][0];
		
		for(i = 0; i < EVENT_DISPATCH_LIST_MAX_SUBSCRIBERS; i ++) {
			if(event_subs[i] == NULL) break; /* no more */
			if(event_subs[i] == subscriber) {
				n += scnprintf(buf + n, count - n, "%d ", ievent);
				break;
			}
		}
	}
	n += scnprintf(buf + n, count - n, "]");
	
	return n;
}





