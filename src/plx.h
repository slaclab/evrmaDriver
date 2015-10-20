#ifndef MODAC_PLX_H_
#define MODAC_PLX_H_

#include <linux/pci.h>


struct evr_plx_data {
	unsigned long    mrLC;           /* Here we hold the PCI address of BAR0 for PLX */
	void             *pLC;       
	unsigned long    lenLC;
};

/*
 * To invalidate, set plx_bar to -1.
 * On exit the plx->pLC may be NULL, meaning there is no PLX
 */
void evr_plx_init(struct evr_plx_data *plx, struct pci_dev *pcidev, int plx_bar, 
				  const char *device_name);

void evr_plx_fini(struct evr_plx_data *plx);
int evr_plx_irq_enable(struct evr_plx_data *plx);
int evr_plx_irq_disable(struct evr_plx_data *plx);
int evr_plx_irq_is_not_evr(struct evr_plx_data *plx);

int evr_plx_active(struct evr_plx_data *plx);


#endif /* MODAC_PLX_H_ */
