//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/version.h>
#include <linux/mutex.h>

#include <linux/atomic.h>

#include <linux/pci.h>


#include "internal.h"
#include "evr.h"
#include "plx.h"
#include "evrma-core-api.h"
#include "mng-dev.h"

/*
 * Enable this to test the EMCOR card without the MCOR functionality.
 * For test purposes only!
 */
// #define DEBUG_EMCOR_CARD

#ifdef DEBUG_EMCOR_CARD
#include "patched-emcor/emcor-regs.h"
#endif

// needed because the Lattice accesses the EVR regs
#include "evr-internal.h"



#define EVRMA_PCI_NAME    "pci-evrma"
#define DEVICE_NAME           "pci-evrma-dev"

#define PCI_VENDOR_ID_MRF           0x1A3E

// From EVR-MRM-007.pdf, found:

#define PCI_DEVICE_ID_MRF_PMCEVR230 0x11E6 // PMC-EVR-230,
#define PCI_DEVICE_ID_MRF_CPCIEVR300 0x152C // cPCI-EVR-300
#define PCI_DEVICE_ID_MRF_PCIEEVR300 0x172C // PCIe-EVR-300

// From EVR-MRM-007.pdf found in mrfioc2
#define PCI_DEVICE_ID_MRF_EVRTG_300    0x192c   /* PCI Device ID for MRF PCI-EVRTG-300            */

// From EVR-MRM-007.pdf these should be defined but they can't be found anywhere

#define PCI_DEVICE_ID_MRF_CPCIEVRTG300	PCI_DEVICE_ID_MRF_EVRTG_300 // cPCI-EVRTG-300 ???

#define PCI_DEVICE_ID_MRF_CPCIEVR220	undefined // cPCI-EVR-220
#define PCI_DEVICE_ID_MRF_CPCIEVR230	undefined // cPCI-EVR-230
#define PCI_DEVICE_ID_MRF_VMEEVR230		undefined // VME-EVR-230
#define PCI_DEVICE_ID_MRF_VMEEVR230RF	undefined // VME-EVR-230RF
#define PCI_DEVICE_ID_MRF_CRIOEVR300	undefined // cRIO-EVR-300
#define PCI_DEVICE_ID_MRF_PXIEEVR300	undefined // PXIe-EVR-300


// Not in EVR-MRM-007.pdf but found elsewhere:

#define PCI_DEVICE_ID_MRF_PMCEVR200 0x10C8 // this is an old card with only 0x2000 of address space length
#define PCI_DEVICE_ID_MRF_PXIEVR220 0x10DC
#define PCI_DEVICE_ID_MRF_PXIEVR230 0x10E6

// EVG found elsewhere:

#define PCI_DEVICE_ID_MRF_PXIEVG220 0x20DC
#define PCI_DEVICE_ID_MRF_PXIEVG230 0x20E6
#define PCI_DEVICE_ID_MRF_CPCIEVG300 0x252C

// OTHER

#define PCI_VENDOR_ID_PLX             0x10b5   /* PCI Vendor ID for PLX Technology, Inc.          */
#define PCI_DEVICE_ID_PLX_9030        0x9030   /* PCI Device ID for PLX-9030 bridge chip          */
#define PCI_VENDOR_ID_LATTICE         0x1204   /* PCI Vendor ID for Lattice Semiconductor Corp.   */
#define PCI_DEVICE_ID_LATTICE_ECP3    0xec30   /* PCI Device ID for Lattice-ecp30 bridge chip      */



/* SLAC values */
#define PCI_VENDOR_ID_XILINX_PCIE   0x1A4A
#define PCI_DEVICE_ID_XILINX_PCIE   0x2010

#ifdef DEBUG_EMCOR_CARD
	#define EMCOR_PCI_VENDOR	0x1a4a
	#define EMCOR_PCI_DEVICE	0x1000
#endif

#define MAX_PCI_MRF_DEVICES (MAX_MNG_DEVS - 1)
#define HW_INFO_SIZE 1024

#define EVR_REG_PCIE_INT_SLAC	0x0014 // SLAC only
#define EVR_REG_PCIE_INT_SLAC_ENABLED_MASK 0x00000001 // to enable
#define EVR_REG_PCIE_INT_SLAC_STATUS_MASK  0x00000002 // to check status

struct pci_mrf_data;

struct pci_bridge {
	
	int (* enable)(struct pci_mrf_data *ev_device);
	int (* disable)(struct pci_mrf_data *ev_device);
	int (* irq_is_from_evr)(struct pci_mrf_data *ev_device);
};



struct pci_mrf_data {
	int used;
	resource_size_t start;
	resource_size_t end;
	unsigned long  flags;
	resource_size_t length;
	void *io_ptr;
	int irq;            /* Interrupt line */
	int bar;
	char hw_info[HW_INFO_SIZE];
	
	/*
	 * 'activity' is used to protect ev_device->mngdev_des in calls from the ISR.
	 * On the multi-CPU platorm one CPU may be in the middle of the ISR 
	 * while another CPU is performing the 'destroy'.
	 * 
	 * 'activity' gets 2 immediatelly after create. This will be subtracted
	 * before the destroy will be attempted - a sign to ISR to not operate.
	 * 
	 * In the ISR a 1 is added
	 * to it which is subtracted when the ISR has finished.
	 * 
	 * The ISR can only proceed if 'activity' >= 2, meaning the device is not
	 * destroyed.
	 * The 'remove' can only proceed when 'activity' falls to 0, meaning no
	 * ISR is using the ev_device->mngdev_des anymore.
	 *
	 */
	atomic_t activity;
	
#ifdef DEBUG_EMCOR_CARD
	resource_size_t emcor_start;
	resource_size_t emcor_length;
	void *emcor_io_ptr;
#endif	
		
	// IRQs ar handled via PLX
	struct evr_plx_data plx;

	/* Must not be left to NULL. */
	struct pci_bridge *pci_bridge;
	
	int irq_enabled;
	
	struct modac_mngdev_des mngdev_des;
	
	/** If defined the device is handled by external PCI driver (like EMCOR) */
	struct evrma_core_api_external_device *external_device;
	
	/** If defined the device PCI is handled here */
	struct pci_dev *pcidev;
};

enum {
	CLEAN_SLOT,
	CLEAN_PCI_ENABLE,
	CLEAN_REQUEST_MEM,
	CLEAN_PLX_STUFF,
	CLEAN_IOREMAP,
	CLEAN_MNG_DEV_CREATE,

	CLEAN_ALL = CLEAN_MNG_DEV_CREATE
};





static const struct pci_device_id evrma_pci_ids[] = {

	{  .vendor = PCI_VENDOR_ID_PLX,
	   .device = PCI_DEVICE_ID_PLX_9030,
	   .subvendor = PCI_VENDOR_ID_MRF,
	   .subdevice = PCI_DEVICE_ID_MRF_PMCEVR230, },

	{  .vendor = PCI_VENDOR_ID_PLX,
	   .device = PCI_DEVICE_ID_PLX_9030,
	   .subvendor = PCI_VENDOR_ID_MRF,
	   .subdevice = PCI_DEVICE_ID_MRF_CPCIEVR300, },

	{  .vendor = PCI_VENDOR_ID_PLX,
	   .device = PCI_DEVICE_ID_PLX_9030,
	   .subvendor = PCI_VENDOR_ID_MRF,
	   .subdevice = PCI_DEVICE_ID_MRF_PCIEEVR300, },

	{ .vendor = PCI_VENDOR_ID_PLX,
	  .device = PCI_DEVICE_ID_PLX_9030,
	  .subvendor = PCI_VENDOR_ID_MRF,
	  .subdevice = PCI_DEVICE_ID_MRF_CPCIEVRTG300, },

        { .vendor = PCI_VENDOR_ID_LATTICE,
          .device = PCI_DEVICE_ID_LATTICE_ECP3,
          .subvendor = PCI_VENDOR_ID_MRF,
          .subdevice = PCI_DEVICE_ID_MRF_PCIEEVR300,  }, 


	// this #if LINUX_VERSION_CODE is only because PCI_DEVICE_SUB is not defined. If you really
	// needed the support you have to find out how to do it.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,00)
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_PMCEVR230), },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_CPCIEVR300), },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_PCIEEVR300), },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_CPCIEVRTG300), },

        { PCI_DEVICE_SUB(PCI_VENDOR_ID_LATTICE, PCI_DEVICE_ID_LATTICE_ECP3, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_PCIEEVR300), },
// 	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_PMCEVR200), },

	{ PCI_DEVICE_SUB(PCI_ANY_ID, PCI_ANY_ID, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_PMCEVR230), },
	{ PCI_DEVICE_SUB(PCI_ANY_ID, PCI_ANY_ID, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_CPCIEVR300), },
	{ PCI_DEVICE_SUB(PCI_ANY_ID, PCI_ANY_ID, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_PCIEEVR300), },
	{ PCI_DEVICE_SUB(PCI_ANY_ID, PCI_ANY_ID, PCI_VENDOR_ID_MRF, PCI_DEVICE_ID_MRF_CPCIEVRTG300), },

#endif
	
	// Signal processing controller: SLAC National Accelerator Lab PPA-REG PCI-Express EVR
    //    Subsystem: SLAC National Accelerator Lab PPA-REG PCI-Express EVR
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_PCIE, PCI_DEVICE_ID_XILINX_PCIE), },
	
#ifdef DEBUG_EMCOR_CARD
	{ PCI_DEVICE(EMCOR_PCI_VENDOR, EMCOR_PCI_DEVICE), },
#endif

// 	// Signal processing controller: Lattice Semiconductor Corporation Device ec30 (rev 01)
//     //    Subsystem: Device 1a3e:172c
// 	{ PCI_DEVICE(0x1204, 0xec30), },
	
// 	
// 	// Signal processing controller: PLX Technology, Inc. PCI9030 32-bit 33MHz PCI <-> IOBus Bridge (rev 01)
//     //    Subsystem: Device 1a3e:11e6
// 	{ PCI_DEVICE(PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030), },
	

	{ 0, }
};


static struct pci_mrf_data pci_mrf_data[MAX_PCI_MRF_DEVICES];

static int pci_mrf_major, pci_mrf_minor_start;

/*
 * to protect 
 * - the pci_mrf_data table
 * - some critical device operations
 */
static struct mutex mutex;

static inline u32 read_u32(struct pci_mrf_data *ev_device, u32 offset)
{
	void __iomem *io_start = (void __iomem *)ev_device->io_ptr;
	return ioread32(io_start + offset);
}

static inline void write_u32(struct pci_mrf_data *ev_device, u32 offset, u32 value)
{
	void __iomem *io_start = (void __iomem *)ev_device->io_ptr;
	iowrite32(value, io_start + offset);
}

static inline u32 be_read_u32(struct pci_mrf_data *ev_device, u32 offset)
{
	return be32_to_cpu(read_u32(ev_device, offset));
}

static inline void be_write_u32(struct pci_mrf_data *ev_device, u32 offset, u32 value)
{
	write_u32(ev_device, offset, cpu_to_be32(value));
}

static void cleanup(struct pci_mrf_data *ev_device, int what)
{
	switch(what) {
	
	case CLEAN_MNG_DEV_CREATE:
		modac_mngdev_destroy(&ev_device->mngdev_des);
		
	case CLEAN_IOREMAP:
		if(ev_device->pcidev != NULL) {
			iounmap(ev_device->io_ptr);
		}
	case CLEAN_REQUEST_MEM:
		if(ev_device->pcidev != NULL) {
			release_mem_region(ev_device->start, ev_device->length);
		}
	case CLEAN_PLX_STUFF:
		if(ev_device->pcidev != NULL) {
			if(evr_plx_active(&ev_device->plx)) {
				evr_plx_fini(&ev_device->plx);
			}
		}
	case CLEAN_PCI_ENABLE:
		if(ev_device->pcidev != NULL) {
			pci_disable_device(ev_device->pcidev);
		}
		
	case CLEAN_SLOT:
		// must be cleaned so the slot is free for other entries;
		// does not need the mutex for 0... non-0 distinction.
		ev_device->used = 0;
	}
}

static int plx_bridge_enable(struct pci_mrf_data *ev_device)
{
	evr_plx_irq_enable(&ev_device->plx);
	return 0;
}

static int plx_bridge_disable(struct pci_mrf_data *ev_device)
{
	evr_plx_irq_disable(&ev_device->plx);
	return 0;
}

static int plx_bridge_irq_is_from_evr(struct pci_mrf_data *ev_device)
{
	if(evr_plx_irq_is_not_evr(&ev_device->plx)) {
		return 0;
	}
	
	return 1;
}

/*
 * It is supposed that evr_plx_active(&ev_device->plx) is true if
 * this bridge is selected.
 */
static struct pci_bridge pci_bridge_plx = {
	
	enable: plx_bridge_enable,
	disable: plx_bridge_disable,
	irq_is_from_evr: plx_bridge_irq_is_from_evr,
};



static int lattice_enable(struct pci_mrf_data *ev_device)
{
	u32 irqen = be_read_u32(ev_device, EVR_REG_IRQEN);
	irqen |= (1 << C_EVR_IRQ_PCIEE);
	be_write_u32(ev_device, EVR_REG_IRQEN, irqen);
	barrier();
	return be_read_u32(ev_device, EVR_REG_IRQEN);
}

static int lattice_disable(struct pci_mrf_data *ev_device)
{
	u32 irqen = be_read_u32(ev_device, EVR_REG_IRQEN);
	irqen &= (~(1 << C_EVR_IRQ_PCIEE));
	be_write_u32(ev_device, EVR_REG_IRQEN, irqen);
	barrier();
	return be_read_u32(ev_device, EVR_REG_IRQEN);
}

static int lattice_irq_is_from_evr(struct pci_mrf_data *ev_device)
{
	u32 irq_flags = be_read_u32(ev_device, EVR_REG_IRQFLAG);

	if((irq_flags & (1 << C_EVR_IRQ_PCIEE)) == 0) {
		return 0;
	}
	
	return 1;
}

static struct pci_bridge pci_bridge_lattice = {
	
	enable: lattice_enable,
	disable: lattice_disable,
	irq_is_from_evr: lattice_irq_is_from_evr,
};

static int slac_enable(struct pci_mrf_data *ev_device)
{
	be_write_u32(ev_device, EVR_REG_PCIE_INT_SLAC, EVR_REG_PCIE_INT_SLAC_ENABLED_MASK);
	barrier();
	return be_read_u32(ev_device, EVR_REG_PCIE_INT_SLAC);
}

static int slac_disable(struct pci_mrf_data *ev_device)
{
	be_write_u32(ev_device, EVR_REG_PCIE_INT_SLAC, 0);
	barrier();
	return be_read_u32(ev_device, EVR_REG_PCIE_INT_SLAC);
}

static int slac_irq_is_from_evr(struct pci_mrf_data *ev_device)
{
	u32 irq_flags = be_read_u32(ev_device, EVR_REG_PCIE_INT_SLAC);

	if((irq_flags & EVR_REG_PCIE_INT_SLAC_STATUS_MASK) == 0) {
		return 0;
	}
	
	return 1;
}

static struct pci_bridge pci_bridge_slac = {
	
	enable: slac_enable,
	disable: slac_disable,
	irq_is_from_evr: slac_irq_is_from_evr,
};


#ifdef DEBUG_EMCOR_CARD
static int emcor_enable(struct pci_mrf_data *ev_device)
{
	iowrite32(EMCOR_M_IRQ_EVR, ev_device->emcor_io_ptr + EMCOR_REG_IRQ_ENBL_SET);
	return 0;
}

static int emcor_disable(struct pci_mrf_data *ev_device)
{
	iowrite32(EMCOR_M_IRQ_EVR, ev_device->emcor_io_ptr + EMCOR_REG_IRQ_ENBL_RST);
	return 0;
}

static int emcor_irq_is_from_evr(struct pci_mrf_data *ev_device)
{
	u32 irq_stat, irq_enbl, irq_src;
	void __iomem *mem_irq = ev_device->emcor_io_ptr;

	irq_enbl = ioread32(mem_irq + EMCOR_REG_IRQ_ENBL_STATUS);
	irq_src = ioread32(mem_irq + EMCOR_REG_IRQ_SRC);
	irq_stat = irq_enbl & irq_src;
	if (!irq_stat) {
		return IRQ_NONE;
	}
	
	if (irq_stat & EMCOR_M_IRQ_EVR) {
		// acknowledge EVR IRQ
		iowrite32(EMCOR_M_IRQ_EVR, mem_irq + EMCOR_REG_IRQ_SRC);
		
		// this is the EVR event
	}
	
	if(irq_stat & EMCOR_M_IRQ_SW) {
		// disable and acknowledge other (MCOR) IRQs
		iowrite32(EMCOR_M_IRQ_SUPPORTED & ~EMCOR_M_IRQ_EVR, mem_irq + EMCOR_REG_IRQ_ENBL_RST);
		iowrite32(EMCOR_M_IRQ_SUPPORTED & ~EMCOR_M_IRQ_EVR, mem_irq + EMCOR_REG_IRQ_SRC);
		
		// This is an MCOR event which we do not handle here
		return 0;
	}
	
	return 1;
};

static struct pci_bridge pci_bridge_emcor = {
	
	enable: emcor_enable,
	disable: emcor_disable,
	irq_is_from_evr: emcor_irq_is_from_evr,
};

#endif

static int external_bridge_enable(struct pci_mrf_data *ev_device)
{
	ev_device->external_device->irq_enable(ev_device->external_device, 1);
	return 0;
}

static int external_bridge_disable(struct pci_mrf_data *ev_device)
{
	ev_device->external_device->irq_enable(ev_device->external_device, 0);
	return 0;
}

static int extrenal_bridge_irq_is_from_evr(struct pci_mrf_data *ev_device)
{
	/* 
	 * The external PCI handler already filters IRQs so here we already
	 * get only the EVR's ones.
	 */
	return 1;
}

/*
 * The pci bridge definition used in case of external device creating.
 * It is supposed that ev_device->external_device != NULL if
 * this bridge is selected.
 */
static struct pci_bridge pci_bridge_external = {
	
	enable: external_bridge_enable,
	disable: external_bridge_disable,
	irq_is_from_evr: extrenal_bridge_irq_is_from_evr,
};


static int no_bridge_enable(struct pci_mrf_data *ev_device)
{
	return 0;
}

static int no_bridge_disable(struct pci_mrf_data *ev_device)
{
	return 0;
}

static int no_bridge_irq_is_from_evr(struct pci_mrf_data *ev_device)
{
	return 1;
}

/*
 * The default pci bridge definition used in case the pci bridge is not known.
 */
static struct pci_bridge pci_bridge_none = {
	
	enable: no_bridge_enable,
	disable: no_bridge_disable,
	irq_is_from_evr: no_bridge_irq_is_from_evr,
};

static irqreturn_t evrma_pci_isr(int irq, void *dev_id) 
{
	struct pci_mrf_data *ev_device = (struct pci_mrf_data *) dev_id;
	
	if(!ev_device->pci_bridge->irq_is_from_evr(ev_device)) {
		/* 
		 * We use shared interrupts: return immediately if irq is not 
		 * from the EVR device. 
		 */
		return IRQ_NONE;
	}

	atomic_inc(&ev_device->activity);
	
	if(atomic_read(&ev_device->activity) >= 2) {
		modac_mngdev_isr(&ev_device->mngdev_des, (void *)0);
	}

	atomic_dec(&ev_device->activity);
	
	return IRQ_HANDLED;
}


static void evrma_pci_irq_set(struct modac_mngdev_des *mngdev_des, int enabled)
{
	struct pci_mrf_data *ev_device = container_of(mngdev_des, struct pci_mrf_data, mngdev_des);

	/*
	 * This function is called from the MODAC layer and can be called anytime.
	 * Make sure all the actions are done in one block while the 'activity'
	 * remains >= 2.
	 */
	
	mutex_lock(&mutex);
	
	if(atomic_read(&ev_device->activity) >= 2) {
	
		if(enabled && !ev_device->irq_enabled) {
			int ret = 0;

			if(ev_device->pcidev != NULL) {
				ret = request_irq(
					ev_device->irq,
					&evrma_pci_isr,
					/* Is there need for SA_INTERRUPT? */
					IRQF_SHARED,
					DEVICE_NAME,
					(void *) ev_device);
			} else {
				// the interrupt is obtained externally
			}
			
			if (ret) {
				printk(KERN_ERR DEVICE_NAME ": cannot obtain interrupt %d\n", ev_device->irq);
			} else {
				if(ev_device->pcidev != NULL) {
					printk(KERN_INFO "PCI: " DEVICE_NAME ": obtained interrupt %d\n", ev_device->irq);
				}
				
				ev_device->pci_bridge->enable(ev_device);			
			}

		} else if(!enabled && ev_device->irq_enabled) {
				
			ev_device->pci_bridge->disable(ev_device);
				
			if(ev_device->pcidev != NULL) {
				free_irq(ev_device->irq, (void *)ev_device);
			}
		}
		
		ev_device->irq_enabled = enabled;
	}
	
	mutex_unlock(&mutex);
}

static u32 e_read_u32(struct modac_mngdev_des *devdes, u32 offset)
{
	void __iomem *io_start = (void __iomem *)devdes->io_start;
	return ioread32(io_start + offset);
}

static void e_write_u32(struct modac_mngdev_des *devdes, u32 offset, u32 value)
{
	void __iomem *io_start = (void __iomem *)devdes->io_start;
	iowrite32(value, io_start + offset);
}

static u16 e_read_u16(struct modac_mngdev_des *devdes, u32 offset)
{
	void __iomem *io_start = (void __iomem *)devdes->io_start;
	return ioread16(io_start + offset);
}

static void e_write_u16(struct modac_mngdev_des *devdes, u32 offset, u16 value)
{
	void __iomem *io_start = (void __iomem *)devdes->io_start;
	iowrite16(value, io_start + offset);
}

static struct modac_io_rw_plugin pci_rw_io_plugin = {
	write_u16: e_write_u16,
	read_u16: e_read_u16,
	write_u32: e_write_u32,
	read_u32: e_read_u32,
};

static int sease_a_slot(void)
{
	int i, id = -1;
	
	/* Find empty mrf_dev structure */
	mutex_lock(&mutex);
	for (i = 0; i < MAX_PCI_MRF_DEVICES; i++) {
		if (!pci_mrf_data[i].used) {
			memset(&pci_mrf_data[i], 0, sizeof(pci_mrf_data[i]));
			pci_mrf_data[i].used = 1;
			id = i;
			break;
		}
	}
	mutex_unlock(&mutex);
	
	if (id < 0) {
		printk(KERN_WARNING DEVICE_NAME ": too many devices.\n");
		return -EMFILE;
	}
	
	return id;
}


static int finish_probe(struct pci_mrf_data *ev_device, int id, int current_clean)
{
	int ret;
	
	ev_device->mngdev_des.io_rw = &pci_rw_io_plugin;

	// store to pointer to the info here
	ev_device->mngdev_des.underlying_info = ev_device->hw_info;
	
	ev_device->mngdev_des.major = pci_mrf_major;
	ev_device->mngdev_des.minor = pci_mrf_minor_start + id * MINOR_MULTIPLICATOR;
	ev_device->mngdev_des.io_start = ev_device->io_ptr;
	ev_device->mngdev_des.io_start_phys = ev_device->start;
	ev_device->mngdev_des.io_size = ev_device->length;
	ev_device->mngdev_des.hw_support = &hw_support_evr;
	ev_device->mngdev_des.irq_set = evrma_pci_irq_set;
	scnprintf(ev_device->mngdev_des.name, MODAC_DEVICE_MAX_NAME + 1, "evr%dmng", id);
	
	printk(KERN_DEBUG "evrma_pci_probe to modac_mngdev_create: io_ptr=0x%x, evr_type=%d\n", 
		 (int)(size_t)ev_device->io_ptr, ev_device->mngdev_des.hw_support_hint1);
	
	ret = modac_mngdev_create(&ev_device->mngdev_des);
	if(ret) {
		cleanup(ev_device, current_clean);
		return ret;
	}
	
	atomic_set(&ev_device->activity, 2);
	
	return 0;
}

static int evrma_pci_probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id) 
{
	struct pci_mrf_data *ev_device;
	int id;
	int ret;
	int current_clean; // keeps track of what to clean up on error.
	int bar;

	char *hw_info_ptr;
	int hw_info_n = 0;

	printk(KERN_DEBUG "evrma_pci_probe: vendor=0x%x device=0x%x subvendor=0x%x subdevice=0x%x class=0x%x class_mask=0x%x\n", 
		 dev_id->vendor, dev_id->device, dev_id->subvendor, dev_id->subdevice, dev_id->class, dev_id->class_mask);
	
	id = sease_a_slot();
	if(id < 0) return id;
	
	current_clean = CLEAN_SLOT;

	ev_device = &pci_mrf_data[id];
	
	// A PCI device, not external
	ev_device->pcidev = pcidev;
	ev_device->external_device = NULL;
	
	// initial state
	ev_device->irq_enabled = false;

	/* Here we enable device before we can do any accesses to device. */
	ret = pci_enable_device(pcidev);
	if(ret) {
		printk(KERN_WARNING DEVICE_NAME ": error enabling device.\n");
		cleanup(ev_device, current_clean);
		return ret;
	}
	
	current_clean = CLEAN_PCI_ENABLE;
	
	/*
	 * The default in case no PCI bridge is detected.
	 */
	ev_device->pci_bridge = &pci_bridge_none;
	
	// try to find the EVR type by PCI ID
	{
		u32 pci_mrf_device_id = PCI_ANY_ID;
		int hw_support_hint1 = EVR_TYPE_DOCD_UNDEFINED;
		
		if(dev_id->vendor == PCI_VENDOR_ID_MRF) {
			pci_mrf_device_id = dev_id->device;
		} else if(dev_id->subvendor == PCI_VENDOR_ID_MRF) {
			pci_mrf_device_id = dev_id->subdevice;
#ifdef DEBUG_EMCOR_CARD
		} else if(dev_id->vendor == EMCOR_PCI_VENDOR && dev_id->device == EMCOR_PCI_DEVICE) {

			ev_device->pci_bridge = &pci_bridge_emcor;
			
			goto label1;
#endif
		}
		switch(pci_mrf_device_id) {
		case PCI_DEVICE_ID_MRF_PMCEVR230:
			hw_support_hint1 = EVR_TYPE_DOCD_PMCEVR230;
			break;
		case PCI_DEVICE_ID_MRF_CPCIEVR300:
			hw_support_hint1 = EVR_TYPE_DOCD_CPCIEVR300;
			break;
		case PCI_DEVICE_ID_MRF_PCIEEVR300:
			hw_support_hint1 = EVR_TYPE_DOCD_PCIEEVR300;
			break;
		case PCI_DEVICE_ID_MRF_CPCIEVRTG300:
			hw_support_hint1 = EVR_TYPE_DOCD_CPCIEVRTG300;
			break;
		}
		
#ifdef DEBUG_EMCOR_CARD
	label1:
#endif
		ev_device->mngdev_des.hw_support_hint1 = hw_support_hint1;
	}
	
	
	ev_device->length = 0;

	for(bar = 0; bar <= 5; bar ++) {
		
		resource_size_t start;
		resource_size_t end;
		resource_size_t length;
		
		start = pci_resource_start(pcidev, bar);
		if(start != 0) {
			end = pci_resource_end(pcidev, bar);
			length = end - start + 1;
			printk(KERN_INFO "BAR%d @0x%x, len=0x%x\n", bar, (u32)start, (u32)length);
		
#ifdef DEBUG_EMCOR_CARD
			if(ev_device->pci_bridge == &pci_bridge_emcor) {
				
				// in EMCOR the EVR is on bar 2
				
				if(bar == 0) {
					
					ev_device->emcor_start = start;
					ev_device->emcor_length = length;
					
				}  else if(bar == 2) {
					
					ev_device->bar = bar;
					ev_device->start = start;
					ev_device->end = end;
					ev_device->length = length;
				}
				
			} else 
#endif				
			{
				
				/* 
				* Find the first BAR with the mem region at least 32k (this heuristics 
				* will properly detect all EVR types (hopefully).
				* Get the Event Receiver registers memory mapped base address, end address and flags.
				*/

				// set the first ok
				if(length >= 0x8000) {
					
					if(ev_device->length == 0) {
						// not set yet
						ev_device->bar = bar;
						ev_device->start = start;
						ev_device->end = end;
						ev_device->length = length;
					}
				}
			}
		}
	}
	
	if(ev_device->length == 0) {
		printk(KERN_ERR "No BAR has a memory region long enough. Aborting.\n");
		cleanup(ev_device, current_clean);
		return -ENXIO;
	}

#ifdef DEBUG_EMCOR_CARD
	if(ev_device->pci_bridge == &pci_bridge_emcor) {
		if (request_mem_region(ev_device->emcor_start, ev_device->emcor_length,
					DEVICE_NAME) == NULL) {
			printk(KERN_ERR "EMCOR request_mem_region failed\n");
			cleanup(ev_device, current_clean);
			return -ENXIO;
		}
		
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5,5,0)
		ev_device->emcor_io_ptr = ioremap_nocache(ev_device->emcor_start, ev_device->emcor_length);
    #else
		ev_device->emcor_io_ptr = ioremap(ev_device->emcor_start, ev_device->emcor_length);
    #endif


	} else 
#endif


	// If EVR is on BAR2 we have a PLX on the BAR0 (except in EMCOR case)
	
	evr_plx_init(&ev_device->plx, pcidev, (ev_device->bar == 2) ? 0 : -1, DEVICE_NAME);
	
	if(evr_plx_active(&ev_device->plx)) {
		ev_device->pci_bridge = &pci_bridge_plx;
	}
	
	current_clean = CLEAN_PLX_STUFF;
	
	printk(KERN_DEBUG "evrma_pci_probe: start=0x%x, length=0x%x\n", (int)ev_device->start, (int)ev_device->length);
	
	if (request_mem_region(ev_device->start, ev_device->length, DEVICE_NAME) == NULL) {
		printk(KERN_ERR "evrma_pci_probe request_mem_region failed\n");
		cleanup(ev_device, current_clean);
		return -ENXIO;
	}
	
	current_clean = CLEAN_REQUEST_MEM;
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,5,0)
	ev_device->io_ptr = ioremap_nocache(ev_device->start, ev_device->length);
#else
	ev_device->io_ptr = ioremap(ev_device->start, ev_device->length);
#endif
	
	if(ev_device->io_ptr == NULL) {
		printk(KERN_ERR "evrma_pci_probe ioremap_nocache failed\n");
		cleanup(ev_device, current_clean);
		return -ENXIO;
	}
	
	current_clean = CLEAN_IOREMAP;

	ev_device->flags = pci_resource_flags(pcidev, ev_device->bar);

	
	hw_info_ptr = ev_device->hw_info;
	hw_info_ptr[0] = 0;
	
	if(evr_plx_active(&ev_device->plx)) {
		
		hw_info_n += scnprintf(hw_info_ptr + hw_info_n, HW_INFO_SIZE - hw_info_n,
				"PLX");
	
#ifdef DEBUG_EMCOR_CARD
	} else if(ev_device->pci_bridge == &pci_bridge_emcor) {
		
		hw_info_n += scnprintf(hw_info_ptr + hw_info_n, HW_INFO_SIZE - hw_info_n,
				"EMCOR");
#endif
	} else {

		u32 probed_fw_version = be_read_u32(ev_device, EVR_REG_FW_VERSION);
		
		if(evr_card_is_slac(probed_fw_version)) {
			
			hw_info_n += scnprintf(hw_info_ptr + hw_info_n, HW_INFO_SIZE - hw_info_n,
					"SLAC");

			ev_device->pci_bridge = &pci_bridge_slac;
		} else {
			
			hw_info_n += scnprintf(hw_info_ptr + hw_info_n, HW_INFO_SIZE - hw_info_n,
					"Lattice");

			ev_device->pci_bridge = &pci_bridge_lattice;
		}
	}
	
	
	ev_device->irq = pcidev->irq;
	hw_info_n += scnprintf(hw_info_ptr + hw_info_n, HW_INFO_SIZE - hw_info_n,
			", IRQ: %d, ", ev_device->irq);
	
	return finish_probe(ev_device, id, current_clean);

}

static void evrma_pci_remove(struct pci_dev *pcidev) 
{
	struct pci_mrf_data *ev_device = NULL;
	int i;

	printk(KERN_DEBUG "evrma_pci_remove\n");
	
	// will search by pcidev
	
	mutex_lock(&mutex);
	for (i = 0; i < MAX_PCI_MRF_DEVICES; i++) {
		
		// must check for 'used' because the 'pcidev' may be left from the past in unused dev
		if (pci_mrf_data[i].used && pci_mrf_data[i].pcidev == pcidev) {
			ev_device = &pci_mrf_data[i];
			break;
		}
	}
	
	if (ev_device != NULL) {
		atomic_sub(2, &ev_device->activity);
	}
	
	mutex_unlock(&mutex);
	
	if (ev_device == NULL) {
		printk(KERN_ALERT "Trying to remove uninstalled device on PCI\n");
	} else {

		if(ev_device->irq_enabled) {
			free_irq(ev_device->irq, (void *)ev_device);
		}
		
		/* 
		 * The ev_device->mngdev_des will be destroyed soon.
		 * Wait for the end of ISR if it is using it.
		 */
		while(atomic_read(&ev_device->activity) > 0) {
			msleep(1);
		}

		cleanup(ev_device, CLEAN_ALL);
	}
}

int evrma_core_api_external_probe(struct evrma_core_api_external_device *dev_data)
{
	struct pci_mrf_data *ev_device;
	int id;
	int current_clean;
	char *hw_info_ptr;
	int hw_info_n = 0;
	
	id = sease_a_slot();
	if(id < 0) return id;
	
	current_clean = CLEAN_SLOT;
	
	ev_device = &pci_mrf_data[id];
	dev_data->priv = (void *)ev_device;
	
	// not a PCI device handled here
	ev_device->external_device = dev_data;
	ev_device->pci_bridge = &pci_bridge_external;
	ev_device->pcidev = NULL;
	
	// unknown
	ev_device->mngdev_des.hw_support_hint1 = -1;
	
	hw_info_ptr = ev_device->hw_info;
	hw_info_ptr[0] = 0;
	hw_info_n += scnprintf(hw_info_ptr + hw_info_n, HW_INFO_SIZE - hw_info_n,
				"Extern");
	
	ev_device->io_ptr = dev_data->io_start;
	ev_device->start = dev_data->io_start_phys;
	ev_device->length = dev_data->io_size;
	
	return finish_probe(ev_device, id, current_clean);
}

void evrma_core_api_external_remove(struct evrma_core_api_external_device *dev_data)
{
	struct pci_mrf_data *ev_device = (struct pci_mrf_data *)dev_data->priv;
	int i;
	
	printk(KERN_DEBUG "evrma_core_api_external_remove\n");
	
	// will search by external_device
	
	mutex_lock(&mutex);
	for (i = 0; i < MAX_PCI_MRF_DEVICES; i++) {
		
		// must check for 'used' because the 'dev_data' may be left from the past in unused dev
		if (pci_mrf_data[i].used && pci_mrf_data[i].external_device == dev_data) {
			ev_device = &pci_mrf_data[i];
			break;
		}
	}
	mutex_unlock(&mutex);

	if (ev_device == NULL) {
		printk(KERN_ALERT "Trying to remove uninstalled device on PCI\n");
	} else {
		
		if(ev_device->irq_enabled) {
			free_irq(ev_device->irq, (void *)ev_device);
		}
		
		cleanup(ev_device, CLEAN_ALL);
	}
}

int evrma_core_api_external_irq(struct evrma_core_api_external_device *dev_data)
{
	struct pci_mrf_data *ev_device = (struct pci_mrf_data *)dev_data->priv;
	
	modac_mngdev_isr(&ev_device->mngdev_des, (void *)0);

	return IRQ_HANDLED;
}



static struct pci_driver evrma_pci_driver = {
	.name = EVRMA_PCI_NAME,
	.id_table = evrma_pci_ids,
	.probe = evrma_pci_probe,
	.remove = evrma_pci_remove,
};

int evrma_pci_init(int major, int minor_start)
{
	int ret;
	
	mutex_init(&mutex);
	
	pci_mrf_major = major;
	pci_mrf_minor_start = minor_start;
	
	ret = pci_register_driver(&evrma_pci_driver);
	if (ret) {
		printk(KERN_ERR "%s <init>: Failed to register PCI driver!\n", EVRMA_PCI_NAME);
		return ret;
	}
	
	return 0;
}

void evrma_pci_fini(void)
{
	pci_unregister_driver(&evrma_pci_driver);
}

EXPORT_SYMBOL(evrma_core_api_external_probe);
EXPORT_SYMBOL(evrma_core_api_external_remove);
EXPORT_SYMBOL(evrma_core_api_external_irq);


