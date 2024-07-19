//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef DEVREF_H
#define DEVREF_H

/* Author: Till Straumann <strauman@slac.stanford.edu>, 7/2014
 *
 * $CommitId:$
 */

#include <linux/mutex.h>
#include <linux/list.h>

/* Implementation of a 'device reference':
 *
 *   This is essentially a pointer, a lock and a reference count.
 *
 *   The useage semantics are as follows:
 *
 *   The reference counter is incremented for as long as a reference
 *   is 'held'. This refers to the reference object *itself*, not
 *   the entity addressed by its pointer member. While the reference
 *   is 'held' it can be accessed (locked, tested for validity and
 *   dereferenced). After it is released (reference counter decremented)
 *   the reference object must no longer be used by the same user.
 *
 *   Holding and releasing a reference is done by calling
 *     devref_get() 
 *     devref_put()
 *
 *   Holding the reference is not to be confused with dereferencing
 *   it. In order to dereference the pointer member:
 *    - the reference object must be locked
 *    - the reference object must be tested for validity
 *
 *   The reference object is not locked all the time it is held
 *   but only while it is being manipulated and/or for the critical
 *   section of code where the pointer is dereferenced.
 *
 *   A reference is locked by calling
 *     devref_lock()
 *     devref_unlock()
 *
 *   The reference is usually 'held' by an open file, i.e.,
 *   open() calls devref_get() and release() calls devref_put().
 * 
 *   The devref pointer also constitutes the 'valid' flag. The pointer
 *   is invalid if it is NULL.
 *
 *   Usage of the referenced object via a reference which is currently
 *   'held' involves the following operations:
 *
 *     devref_lock( ref );
 *
 *     // obtain pointer member
 *     p = devref_ptr( ref );
 *
 *     // validity test
 *     if ( 0 != p ) {
 *       execute_operation_on_deref( *p );
 *     } else {
 *       status = -ENODEV;
 *     }
 *
 *     devref_unlock( ref );
 */  


/* A list node for keeping track of inodes which
 * hold reference objects. Necessary for removing
 * memory maps.
 */
struct inod_ref {
	struct list_head  l_node; /* List node                                          */
	struct inode     *l_inod; /* inode we list here                                 */
	int               l_rcnt; /* counter: how many open files for this inode        */
};

struct devref {
	void             *ref_ptr; /* The pointer we are managing                       */
	int               ref_cnt; /* How many users are holding this reference         */
	struct mutex      ref_mtx; /* The lock                                          */
	struct list_head  ref_lhd; /* keep a list of all inodes that hold the reference */
};

/* Initialize a devref object (embedded in a larger struct)
 *
 * @ref: devref object to be initialized
 * @ptr: pointer to be managed.
 *
 * The reference count is initialized to 1, i.e., after initialization
 * the reference object is 'held' by the creator.
 */

static inline void
devref_init(struct devref *ref, void *ptr)
{
	mutex_init( &ref->ref_mtx );
	ref->ref_cnt = 1;
	ref->ref_ptr = ptr;
	INIT_LIST_HEAD( &ref->ref_lhd );
}

/* Lock the reference 
 */
static inline void
devref_lock(struct devref *ref)
{
	mutex_lock( &ref->ref_mtx );
}

/* Unlock the reference 
 */
static inline void
devref_unlock(struct devref *ref)
{
	mutex_unlock( &ref->ref_mtx );
}

/* Obtain a reference.
 *  - lock 
 *  - increment reference count
 *  - add 'inod' (if not NULL) to the list of inodes with
 *    open files holding 'ref'.
 *
 * RETURNS: 0 on success; reference is locked.
 *          On failure (no memory for new inode list element)
 *          -ENOMEM is returned. The reference counter is unchanged
 *          and the reference is unlocked in case of an error.
 *
 * NOTES: inode may be NULL if the user is not a file with
 *    possible memory maps.
 * 
 *    The same inod may be passed multiple times; the
 *    code maintains a counter.
 */
static inline int
devref_get(struct devref *ref, struct inode *inod)
{
struct inod_ref *l;

	devref_lock( ref );
	
	if(ref->ref_ptr == 0) {
		/* device is gone */
		devref_unlock( ref );
		return -ENODEV;
	}
	
	ref->ref_cnt++;
	if ( inod ) {
		list_for_each_entry( l, &ref->ref_lhd, l_node ) {
			if ( l->l_inod == inod ) {
				/* This inode is already listed; just increment refcnt */
				l->l_rcnt++;
				return 0;
			}
		}
		l = kmalloc( sizeof(*l), GFP_KERNEL);
		if ( ! l ) {
			/* undo and return error */
			ref->ref_cnt--;
			devref_unlock( ref );
			return -ENOMEM;
		}
		INIT_LIST_HEAD( &l->l_node );
		l->l_inod = inod;
		l->l_rcnt = 1;
		list_add( &l->l_node, &ref->ref_lhd );
	}
	return 0;
}

/* Obtain pointer and validity flag
 *
 *  valid := (return_value != NULL)
 * 
 * NOTE: the reference must be locked before calling this
 *       routine.
 */
static inline void *
devref_ptr(struct devref *ref)
{
	return ref->ref_ptr;
}

/* Release a reference.
 *
 *  - decrement reference count
 *  - if inod is non-NULL then locate it in the list and
 *    remove it (i.e., decrement list entry counter and
 *    delete once it drops to zero).
 *  - unlock the reference.
 *
 * RETURNS: new reference count.
 *
 * NOTES: ref *must* be locked before calling this routine.
 *        Caller may dispose of ref (or encapsulating struct)
 *        once this routine returns zero.
 *
 *        Precautions must be taken that the reference count
 *        cannot drop to zero if there is a possibility for
 *        another entity to obtain the reference object again.
 */
static inline int
devref_put(struct devref *ref, struct inode *inod)
{
int              rval;
struct inod_ref *l;
    rval = --ref->ref_cnt;
	if ( inod ) {
		list_for_each_entry( l, &ref->ref_lhd, l_node ) {
			if ( l->l_inod == inod ) {
				if ( --l->l_rcnt == 0 ) {
					list_del( &l->l_node );
					kfree( l );
				}
				mutex_unlock( &ref->ref_mtx );
				return rval;
			}
		}
		// PANIC HERE
		printk(KERN_ERR "Panic in devref_put!\n");
	}
	mutex_unlock( &ref->ref_mtx );
	return rval;
}

/* Invalidate reference
 *
 * NOTE: must be called with mutex locked
 */
static inline void
devref_invalidate(struct devref *ref)
{
	ref->ref_ptr = 0;
}

/* Iterate over all inodes associated with 'ref' and execute
 * 'inod_iterator' callback.
 *
 * NOTES: must be called with mutex locked
 *        the iterator executes with the mutex unlocked.
 */
static inline void
devref_forall_inodes(struct devref *ref, int (*inod_iterator)(struct inode *inod, void *closure), void *closure)
{
struct inod_ref *l, *n;
struct inode    *i;

	list_for_each_entry_safe( l, n, &ref->ref_lhd, l_node ) {
		i = l->l_inod;
		devref_unlock( ref );
		inod_iterator(i, closure);
		devref_lock( ref );
	}
}
#endif
