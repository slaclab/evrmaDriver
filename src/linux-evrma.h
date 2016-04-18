#ifndef LINUX_EVRMA_H_
#define LINUX_EVRMA_H_

#include "linux-modac.h"

//## #define DBG_MEASURE_TIME_FROM_IRQ_TO_USER


/** @file */

/**
 * @defgroup g_linux_evrma_api Linux EVRMA API
 * 
 * @short Common definitions for kernel and user space for the EVR HW.
 * 
 * @note 
 * Every VEVR's IOCTL data structure starts with the
 * struct vdev_ioctl_hw_header.
 *
 * @{
 */

/**
 * Resource types used in the EVR.
 */
enum {
	/**
	 * Pulse generators.
	 */
	EVR_RES_TYPE_PULSEGEN,
	
	/**
	 * Outputs.
	 */
	EVR_RES_TYPE_OUTPUT,
	
	/**
	 * The number of all resource types.
	 */
	EVR_RES_TYPE_COUNT
};

/**
 * Number of supported EVR event codes supported.
 */
#define EVR_EVENT_CODES 256


// ================= MNG DEV ===================================================

/**
 * The structure that holds the data for the MNG_DEV_EVR_IOC_OUTSET IOCTL call.
 */
struct mngdev_evr_output_set {
	/** 
	 * The resource definitions used for the IOCTL call.
	 * The first resource is the output. The second resource is the pulsegen
	 * if used, otherwise the misc_func is to be used.
	 */
	struct mngdev_ioctl_hw_header header;

	/**
	 * The ‘source’ is the number defined in the MRF EVR documentation. ‘source’ 
	 * must not mean the pulse generator entries in the documentation 
	 * otherwise an error is generated (pulse generator must always be set 
	 * through the resource definition in the header).
	 * 
	 * Possible 'source' values:
	 * - 32 Distributed bus bit 0 (DBUS0)
	 * - ...
	 * - 39 Distributed bus bit 7 (DBUS7)
	 * - 40 Prescaler 0
	 * - 41 Prescaler 1
	 * - 42 Prescaler 2
	 * - 59 Event clock output
	 * - 60 Event clock output with 180° phase shift
	 * - 61 Tri-state output
	 * - 62 Force output high (logic 1)
	 * - 63 Force output low (logic 0)
	*/
	int misc_func;
};

/**
 * Initializes the EVR settings.
 */
#define MNG_DEV_EVR_IOC_INIT \
	_IOW(MNG_DEV_IOC_MAGIC, MNG_DEV_HW_IOC_MIN + 0, struct mngdev_ioctl_hw_header)

/**
 * Sets the output source. 
 */
#define MNG_DEV_EVR_IOC_OUTSET	\
	_IOW(MNG_DEV_IOC_MAGIC, MNG_DEV_HW_IOC_MIN + 1, struct mngdev_evr_output_set)


// ================= VIRT DEV ==================================================

/**
 * The definitions used for pulse generator MAP RAM operations.
 */
enum {
	/**
	 * The 'clear' bit for the pulse generator.
	 */
	EVR_PULSE_CFG_BIT_CLEAR,
	/**
	 * The 'set' bit for the pulse generator.
	 */
	EVR_PULSE_CFG_BIT_SET,
	/**
	 * The 'trigger' bit for the pulse generator.
	 */
	EVR_PULSE_CFG_BIT_TRIGGER,
};

/**
 * The data for the VEVR_IOC_PULSE_PARAM_SET and
 * VEVR_IOC_PULSE_PARAM_GET IOCTL calls.
 */
struct vevr_ioctl_pulse_param {
	/** 
	 * The resource defined in the header is the pulse generator.
	 */
	struct vdev_ioctl_hw_header header;
	
	/**
	 * The prescaler value obtained or to be set.
	 */
	uint32_t prescaler;
	/**
	 * The delay value obtained or to be set.
	 */
	uint32_t delay;
	/**
	 * The width value obtained or to be set.
	 */
	uint32_t width;
};

/**
 * The data for the VEVR_IOC_PULSE_PROP_SET and
 * VEVR_IOC_PULSE_PROP_GET IOCTL calls.
 */
struct vevr_ioctl_pulse_properties {
	/** 
	 * The resource defined in the header is the pulse generator.
	 */
	struct vdev_ioctl_hw_header header;

	/**
	 * Enable the pulse generator.
	 */
	uint8_t enable;
	/**
	 * Define the pulse generator's polarity.
	 */
    uint8_t polarity;
	
	/** 
	 * Enabled  MAP RAM event actions. See EVR_PULSE_CFG_BIT_... 
	 */
    uint8_t pulse_cfg_bits;
};


/**
 * The data for the VEVR_IOC_PULSE_MAP_RAM_SET and
 * VEVR_IOC_PULSE_MAP_RAM_GET IOCTL calls.
 */
struct vevr_ioctl_pulse_map_ram {
    /**
	 * The resource defined in the header is the pulse generator.
	 */
	struct vdev_ioctl_hw_header header;    

	/** 
	 * MAP RAM pulse generator actions for all events.
	 * See EVR_PULSE_CFG_BIT_... 
	 */
	uint8_t map[EVR_EVENT_CODES];
};

/**
 * The data for the VEVR_IOC_PULSE_MAP_RAM_SET_FOR_EVENT and
 * VEVR_IOC_PULSE_MAP_RAM_GET_FOR_EVENT IOCTL calls.
 */
struct vevr_ioctl_pulse_map_ram_for_event {
    /**
	 * The resource defined in the header is the pulse generator.
	 */
	 struct vdev_ioctl_hw_header header;    

	/**
	 * The event to be set.
	 */
	uint8_t event_code;
	/** 
	 * MAP RAM pulse generator actions for one event.
	 * See EVR_PULSE_CFG_BIT_... 
	 */
	uint8_t map;
};

/**
 * Miscellaneous status information for the VEVR.
 */
struct vevr_status {
	/**
	 * The FPGA version.
	 */
	uint32_t fpga_version;
	/**
	 * The current IRQ flags.
	 */
	uint32_t irq_flags;
	/**
	 * The Seconds Shift register value.
	 */
	uint32_t seconds_shift;
};

/**
 * The data for the VEVR_IOC_STATUS_GET IOCTL call.
 */
struct vevr_ioctl_status {
    /**
	 * Not used and must be set to MODAC_RES_TYPE_NONE.
	 */
	struct vdev_ioctl_hw_header header;
	
	struct vevr_status status;
};


/**
 * Sets the parameters of the pulse generator. 
 */
#define VEVR_IOC_PULSE_PARAM_SET	\
	_IOW(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 0, struct vevr_ioctl_pulse_param)
	
/**
 * Sets the properties of the pulse generator. 
 */
#define VEVR_IOC_PULSE_PROP_SET \
	_IOW(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 1, struct vevr_ioctl_pulse_properties)
	
/**
 * Gets the parameters of the pulse generator. 
 */
#define VEVR_IOC_PULSE_PARAM_GET	\
	_IOWR(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 2, struct vevr_ioctl_pulse_param)
	
/**
 * Gets the properties of the pulse generator. 
 */
#define VEVR_IOC_PULSE_PROP_GET	\
	_IOWR(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 3, struct vevr_ioctl_pulse_properties)
	
/**
 * Sets the Map RAM for the pulse generator.
 */
#define VEVR_IOC_PULSE_MAP_RAM_SET	\
	_IOW(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 4, struct vevr_ioctl_pulse_map_ram)
	
/**
 * Sets the Map RAM for the pulse generator, for the given event. 
 */
#define VEVR_IOC_PULSE_MAP_RAM_SET_FOR_EVENT	\
	_IOW(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 5, struct vevr_ioctl_pulse_map_ram_for_event)
	
/**
 * Gets the Map RAM for the pulse generator.
 */
#define VEVR_IOC_PULSE_MAP_RAM_GET	\
	_IOWR(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 6, struct vevr_ioctl_pulse_map_ram)
	
/**
 * Gets the Map RAM for the pulse generator, for the given event. 
 */
#define VEVR_IOC_PULSE_MAP_RAM_GET_FOR_EVENT	\
	_IOWR(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 7, struct vevr_ioctl_pulse_map_ram_for_event)

/**
 * Reads the miscellaneous status information for the VEVR
 */
#define VEVR_IOC_STATUS_GET	\
	_IOWR(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_IOC_MIN + 8, struct vevr_ioctl_status)
	
/**
 * Reads the value of the latched timestamp. This call is direct (no mutex
 * locking, no sleeping, and is therefore suitable to be called from high 
 * priority threads).
 */
#define VEVR_IOC_LATCHED_TIMESTAMP_GET	\
	_IOWR(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_DIRECT_IOC_MIN + 0, uint32_t)

/**
 * Reads out temperature register, in AxiXadc
 * AxiXadc, address: 0x00030000
 * temperature register, offset: 0x200
 */
#define VEVR_IOC_AXIXADC_TEMPERATURE_GET \
	_IOWR(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_DIRECT_IOC_MIN + 1, uint32_t)

#define VEVR_IOC_AXIXADC_MAXTEMPERATURE_GET \
        _IOWR(VIRT_DEV_IOC_MAGIC, VIRT_DEV_HW_DIRECT_IOC_MIN + 2, uint32_t)


/**
 * The DataBuf data for one message.
 */
struct evr_data_buff_slot_data {
	/**
	 * The status of the DataBuf message as read from the EVR HW.
	 */
	uint32_t status;
	
	/** Number of data words. */
	uint32_t size32;
	
	/** The data. */
	uint32_t data[512];
};

/**
 * The memory mapped region definition for the VEVR.
 */
struct vevr_mmap_data {
	
	/**
	 * Only one slot of DataBuf data is stored here. It is assumed the data
	 * will be read by the application before the next data arrives.
	 */
	struct evr_data_buff_slot_data data_buff;
};
	
	
	
// ================= Events ==================================================

/**
 * The EVRMA_EVENT_... values from 0 to 0xFF correspond to the Event FIFO codes.
 * 
 * Every read from the kernel has attached data struct evr_data_fifo_event.
 */
#define EVRMA_FIFO_MIN_EVENT_CODE 0

/**
 * The last Event FIFO code.
 */
#define EVRMA_FIFO_MAX_EVENT_CODE (EVR_EVENT_CODES - 1)

/*
 * The other codes follow after 0x100,
 */

/**
 * Receiver violation event.
 */
#define EVRMA_EVENT_ERROR_TAXI		0x100

/**
 * Event FIFO full event.
 */
#define EVRMA_EVENT_ERROR_LOST		0x101

/**
 * Heartbeat interrupt event.
 */
#define EVRMA_EVENT_ERROR_HEART		0x102

/**
 * Hardware interrupt event (mapped signal).
 */
#define EVRMA_EVENT_DELAYED_IRQ		0x104

/**
 * Data buffer event.
 */
#define EVRMA_EVENT_DBUF_DATA		0x105

/**
 * The data attached to the Event FIFO event codes.
 */
struct evr_data_fifo_event {
	/**
	 * The read value of the FIFO Seconds Register.
	 */
	uint32_t seconds;
	/**
	 * The read value of the FIFO Timestamp Register.
	 */
	uint32_t timestamp;
	
#ifdef DBG_MEASURE_TIME_FROM_IRQ_TO_USER
	uint32_t dbg_timestamp[5];
#endif
};

/** @} */

#endif /* LINUX_EVRMA_H_ */


