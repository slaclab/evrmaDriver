//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef MODAC_INTERNAL_H_
#define MODAC_INTERNAL_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/circ_buf.h>
#include <linux/wait.h>

#define MODAC_DEVICE_MAX_NAME 31
#define MAX_VIRT_DEVS_PER_MNG_DEV 31

/* 7 PCI and 1 test */
#define MAX_MNG_DEVS 8

#define MINOR_MULTIPLICATOR (MAX_VIRT_DEVS_PER_MNG_DEV + 1)


#define MODAC_MNG_CLASS_NAME "modac-mng"
#define MODAC_VIRT_CLASS_NAME "modac-virt"

#define STRINGS_EQUAL(S1, S2) (strcmp(S1, S2) == 0)
#define PSTRINGS_EQUAL(S1, S2, N) (strncmp(S1, S2, N) == 0)


int evrma_pci_init(int major, int minor_start);
void evrma_pci_fini(void);


// -------- Temporary stuff ------------------

#include "linux-evrma.h" // here it is defined for the testing to be everywhere possible

#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER

struct modac_hw_support_data;
u32 dbg_get_time(struct modac_hw_support_data *hw_support_data);

struct modac_vdev_des;
u32 mng_dbg_get_time(struct modac_vdev_des *vdev_des);

#endif



#endif /* MODAC_INTERNAL_H_ */


