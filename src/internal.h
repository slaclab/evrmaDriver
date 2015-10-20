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


#endif /* MODAC_INTERNAL_H_ */


