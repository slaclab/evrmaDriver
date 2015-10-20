#ifndef EVRMA_H_
#define EVRMA_H_


// Taken from EVR-MRM-007.pdf.
enum {
	
	EVR_TYPE_DOCD_CPCIEVR230,	// cPCIEVR-230
	EVR_TYPE_DOCD_PMCEVR230,		// PMCEVR-230
	EVR_TYPE_DOCD_VMEEVR230,		// VMEEVR-230
	EVR_TYPE_DOCD_VMEEVR230RF,	// VMEEVR-230RF
	EVR_TYPE_DOCD_CPCIEVRTG300,	// cPCIEVRTG-300
	EVR_TYPE_DOCD_CPCIEVR300,	// cPCIEVR-300 ... there is an error in the EVR-MRM-007.pdf: cPCIEVR-330
	EVR_TYPE_DOCD_CRIOEVR300,	// cRIOEVR-300
	EVR_TYPE_DOCD_PCIEEVR300,	// PCIe-EVR-300
	EVR_TYPE_DOCD_PXIEEVR300I,	// PXIe-EVR-300I
	EVR_TYPE_DOCD_PXIEEVR300U,	// PXIe-EVR-300U
	
	EVR_TYPE_DOCD_COUNT,
	
	EVR_TYPE_DOCD_UNDEFINED = -1
};

extern struct modac_hw_support_def hw_support_evr;

// An exported function needed by the PCI code to correctly identify the PCI config
int evr_card_is_slac(u32 fw_version);

#endif /* EVRMA_H_ */
