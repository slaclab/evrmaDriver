//////////////////////////////////////////////////////////////////////////////
// This file is part of 'evrmaDriver'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'evrmaDriver', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#include <linux/version.h>

#include "plx.h"

#define PLX9030_INTCSR_LINTI1_ENA  0x0001 /* LINTi1 enable */
#define PLX9030_INTCSR_LINTI1_POL  0x0002 /* LINTi1 polarity, 1 = active high */
#define PLX9030_INTCSR_LINTI1_STAT 0x0004 /* LINTi1 status, 1 = interrupt active */
#define PLX9030_INTCSR_LINTI2_ENA  0x0008 /* LINTi2 enable */
#define PLX9030_INTCSR_LINTI2_POL  0x0010 /* LINTi2 polarity, 1 = active high */
#define PLX9030_INTCSR_LINTI2_STAT 0x0020 /* LINTi2 status, 1 = interrupt active */
#define PLX9030_INTCSR_PCI_IRQENA  0x0040 /* PCI interrupt enable, 1 = enabled */
#define PLX9030_INTCSR_SWINT       0x0080 /* Software interrupt, 1 = generate PCI IRQ */
#define PLX9030_INTCSR_LINTI1_SENA 0x0100 /* LINTi1 select enable,
					     0 = level, 1 = edge triggerable */
#define PLX9030_INTCSR_LINTI2_SENA 0x0200 /* LINTi1 select enable,
					     0 = level, 1 = edge triggerable */
#define PLX9030_INTCSR_LINTI1_ICLR 0x0400 /* LINTi1 edge triggerable IRQ clear,
					     writing 1 clears irq */
#define PLX9030_INTCSR_LINTI2_ICLR 0x0800 /* LINTi2 edge triggerable IRQ clear,
					     writing 1 clears irq */


struct Pci9030LocalConf
{
  unsigned int LAS0RR;    /* 0x00 Local Address Space 0 Range */
  unsigned int LAS1RR;    /* 0x04 Local Address Space 1 Range */
  unsigned int LAS2RR;    /* 0x08 Local Address Space 2 Range */
  unsigned int LAS3RR;    /* 0x0C Local Address Space 3 Range */
  unsigned int EROMRR;    /* 0x10 Expansion ROM Range */
  unsigned int LAS0BA;    /* 0x14 Local Address Space 0 Local Base Address */
  unsigned int LAS1BA;    /* 0x18 Local Address Space 1 Local Base Address */
  unsigned int LAS2BA;    /* 0x1C Local Address Space 2 Local Base Address */
  unsigned int LAS3BA;    /* 0x20 Local Address Space 3 Local Base Address */
  unsigned int EROMBA;    /* 0x24 Expansion ROM Local Base Address */
  unsigned int LAS0BRD;   /* 0x28 Local Address Space 0 Bus Region Descriptor */
  unsigned int LAS1BRD;   /* 0x2C Local Address Space 1 Bus Region Descriptor */
  unsigned int LAS2BRD;   /* 0x30 Local Address Space 2 Bus Region Descriptor */
  unsigned int LAS3BRD;   /* 0x34 Local Address Space 3 Bus Region Descriptor */
  unsigned int EROMBRD;   /* 0x38 Expansion ROM Bus Region Descriptor */
  unsigned int CS0BASE;   /* 0x3C Chip Select 0 Base Address */
  unsigned int CS1BASE;   /* 0x40 Chip Select 1 Base Address */
  unsigned int CS2BASE;   /* 0x44 Chip Select 2 Base Address */
  unsigned int CS3BASE;   /* 0x48 Chip Select 3 Base Address */
  unsigned short INTCSR;    /* 0x4C Interrupt Control/Status */
  unsigned short PROT_AREA; /* 0x4E Serial EEPROM Write-Protected Address Boundary */
  unsigned int CNTRL;     /* 0x50 PCI Target Response, Serial EEPROM, and
                       Initialization Control */
  unsigned int GPIOC;     /* 0x54 General Purpose I/O Control */
  unsigned int reserved1; /* 0x58 */
  unsigned int reserved2; /* 0x5C */
  unsigned int reserved3; /* 0x60 */
  unsigned int reserved4; /* 0x64 */
  unsigned int reserved5; /* 0x68 */
  unsigned int reserved6; /* 0x6C */
  unsigned int PMDATASEL; /* 0x70 Hidden 1 Power Management Data Select */
  unsigned int PMDATASCALE; /* 0x74 Hidden 2 Power Management Data Scale */
};


void evr_plx_init(struct evr_plx_data *plx, struct pci_dev *pcidev, int plx_bar, const char *device_name)
{
	if(plx_bar >= 0) {
		/* we have a PLX */
		
		unsigned long local_conf_start;
		unsigned long local_conf_end;
		
		local_conf_start = pci_resource_start(pcidev, plx_bar);
		local_conf_end = pci_resource_end(pcidev, plx_bar);
		
		plx->mrLC = local_conf_start;
		plx->lenLC = local_conf_end - local_conf_start + 1;
		
		if (request_mem_region(plx->mrLC, plx->lenLC,
					device_name) != NULL) {
        #if LINUX_VERSION_CODE < KERNEL_VERSION(5,5,0)
			plx->pLC = ioremap_nocache(local_conf_start, plx->lenLC);
        #else
			plx->pLC = ioremap(local_conf_start, plx->lenLC);
        #endif
		} else {
			printk(KERN_ERR "PLX mem region request failed\n");
			plx->pLC = NULL;
		}
		
	} else {
		plx->pLC = NULL;
	}
}

void evr_plx_fini(struct evr_plx_data *plx)
{
	if(plx->pLC != NULL) {
		iounmap(plx->pLC);
		release_mem_region(plx->mrLC, plx->lenLC);
		plx->pLC = NULL;
	}
}

int evr_plx_irq_enable(struct evr_plx_data *plx)
{
	volatile struct Pci9030LocalConf *pLC = plx->pLC;

	if(pLC != NULL) {
		pLC->INTCSR = __constant_cpu_to_le16(PLX9030_INTCSR_LINTI1_ENA |
							PLX9030_INTCSR_LINTI1_POL |
							PLX9030_INTCSR_PCI_IRQENA);
		barrier();
		return le16_to_cpu(pLC->INTCSR);
	} else {
		return -1;
	}
}

int evr_plx_irq_disable(struct evr_plx_data *plx)
{
	volatile struct Pci9030LocalConf *pLC = plx->pLC;

	if(pLC != NULL) {
		pLC->INTCSR = __constant_cpu_to_le16(PLX9030_INTCSR_LINTI1_ENA |
							PLX9030_INTCSR_LINTI1_POL);
		barrier();
		return le16_to_cpu(pLC->INTCSR);
	} else {
		return -1;
	}
}

int evr_plx_irq_is_not_evr(struct evr_plx_data *plx)
{
	volatile struct Pci9030LocalConf *pLC = plx->pLC;
	
	if(pLC != NULL) {		
        if (!(le16_to_cpu(pLC->INTCSR) & PLX9030_INTCSR_LINTI1_STAT))
            return 1;
	}
	
	return 0;
}

int evr_plx_active(struct evr_plx_data *plx)
{
	volatile struct Pci9030LocalConf *pLC = plx->pLC;
	
	return pLC != NULL;
}


