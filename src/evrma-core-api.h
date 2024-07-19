//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef EVRMA_CORE_API_H_
#define EVRMA_CORE_API_H_

/** @file */

/**
 * @defgroup g_evrma_core_api API to create a MODAC device from an external PCI detection code.
 *
 * @{
 */

/**
 * The definition to create a device that was detected on PCI.
 */
struct evrma_core_api_external_device {
	
	
	/** 
	The pointer to the IO start for accessing the data (obtained by ioremap_nocache())
	*/
	void *io_start;
	
	/** 
	The pointer to the IO used for mmaping (obtained by, for example, pci_resource_start())
	*/
	u32 io_start_phys;
	
	/** 
	The length of the IO used for mmaping (obtained by, for example, pci_resource_start())
	*/
	resource_size_t io_size;

	
	/**
	 * Enables / disables the low level PCI interrupt control if needed. Will
	 * be called from the subscription mechanism. Called under the mutex protection
	 * of the PCI-EVR layer.
	 */
	void (* irq_enable)(struct evrma_core_api_external_device *dev_data, int enable);
	
	/** Private section, never touch this. */
	void *priv;
};

/** Probe the device detected externally. */
int evrma_core_api_external_probe(struct evrma_core_api_external_device *dev_data);

/** Removes the externally detected device */
void evrma_core_api_external_remove(struct evrma_core_api_external_device *dev_data);

/** Called on IRQ for the externally detected device */
int evrma_core_api_external_irq(struct evrma_core_api_external_device *dev_data);

/** @} */

#endif /* EVRMA_CORE_API_H_ */




