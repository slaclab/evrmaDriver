#ifndef LINUX_EVR_REGS_H_
#define LINUX_EVR_REGS_H_

/** @file */

/**
 * @defgroup g_linux_evr_reg_defs Linux EVR Register Definitions
 *
 * @{
 * 
 */



/** 
 * @name Registers
 * @{
 */
#define EVR_REG_STATUS			0x0000
#define EVR_REG_CTRL			0x0004
#define EVR_REG_IRQFLAG			0x0008
#define EVR_REG_IRQEN			0x000C

#define EVR_REG_PULSE_IRQ_MAP	0x0010

#define EVR_REG_DATA_BUF_CTRL	0x0020
#define EVR_REG_FW_VERSION		0x002C
#define EVR_REG_EV_CNT_PRESC	0x0040
#define EVR_REG_USEC_DIV		0x004C

#define EVR_REG_SECONDS_SHIFT			0x005C /* Seconds Counter Shift Register */
#define EVR_REG_SECONDS_COUNTER			0x0060 /* Seconds Counter */
#define EVR_REG_TIMESTAMP_EVENT_COUNTER	0x0064 /* Timestamp Event counter */
#define EVR_REG_SECONDS_LATCH			0x0068 /* Seconds Latch Register */
#define EVR_REG_TIMESTAMP_LATCH			0x006C /* Timestamp Latch Register */

#define EVR_REG_FIFO_SECONDS	0x0070
#define EVR_REG_FIFO_TIMESTAMP	0x0074
#define EVR_REG_FIFO_EVENT		0x0078
#define EVR_REG_FRAC_DIV		0x0080

#define EVR_REG_PULSES			0x0200

#define EVR_REG_DATA_BUF		0x0800

#define EVR_REG_MAPRAM_LENGTH	0x2000
#define EVR_REG_MAPRAM1			0x4000

// What's right? The DOC says this is 0x6000, but in the code 0x5000 is used everywhere
#define EVR_REG_MAPRAM2			0x5000
// temperature register in AxiXadc peripheral
#define AXIXADC_PERIPHERAL_DEV          0x00030000
#define AXIXADC_REG_TEMPERATURE		(AXIXADC_PERIPHERAL_DEV+0x200)
#define AXIXADC_REG_MAXTEMPERATURE	(AXIXADC_PERIPHERAL_DEV+0x280)
/**
 * @}
 */





/** 
 * @name  Status Register bit mappings 
 * @{
 */
#define C_EVR_REG_STATUS_LEGVIO		16
#define C_EVR_REG_STATUS_FIFOSTP	5
#define C_EVR_REG_STATUS_LINK		6
#define C_EVR_REG_STATUS_SFPMOD		7
/**
 * @}
 */



/** 
 * @name  Control Register bit mappings 
 * @{
 */
#define C_EVR_CTRL_MASTER_ENABLE    31
#define C_EVR_CTRL_EVENT_FWD_ENA    30
#define C_EVR_CTRL_TXLOOPBACK       29
#define C_EVR_CTRL_RXLOOPBACK       28
#define C_EVR_CTRL_OUTEN            27
#define C_EVR_CTRL_SRST		        26
#define C_EVR_CTRL_TS_CLOCK_DBUS    14
#define C_EVR_CTRL_RESET_TIMESTAMP  13
#define C_EVR_CTRL_LATCH_TIMESTAMP  10
#define C_EVR_CTRL_MAP_RAM_ENABLE   9
#define C_EVR_CTRL_MAP_RAM_SELECT   8
#define C_EVR_CTRL_FIFO_ENABLE      6
#define C_EVR_CTRL_FIFO_DISABLE     5
#define C_EVR_CTRL_FIFO_STOP_EV_EN  4
#define C_EVR_CTRL_RESET_EVENTFIFO  3
/**
 * @}
 */


/** 
 * @name  Interrupt Flag/Enable Register bit mappings
 * @{
 */
#define C_EVR_IRQ_MASTER_ENABLE   31
#define C_EVR_IRQ_PCIEE           30
#define C_EVR_NUM_IRQ             6
#define C_EVR_IRQFLAG_DATABUF     5
#define C_EVR_IRQFLAG_PULSE       4
#define C_EVR_IRQFLAG_EVENT       3
#define C_EVR_IRQFLAG_HEARTBEAT   2
#define C_EVR_IRQFLAG_FIFOFULL    1
#define C_EVR_IRQFLAG_VIOLATION   0
#define EVR_IRQ_MASTER_ENABLE     (1 << C_EVR_IRQ_MASTER_ENABLE)
#define EVR_IRQFLAG_DATABUF       (1 << C_EVR_IRQFLAG_DATABUF)
#define EVR_IRQFLAG_PULSE         (1 << C_EVR_IRQFLAG_PULSE)
#define EVR_IRQFLAG_EVENT         (1 << C_EVR_IRQFLAG_EVENT)
#define EVR_IRQFLAG_HEARTBEAT     (1 << C_EVR_IRQFLAG_HEARTBEAT)
#define EVR_IRQFLAG_FIFOFULL      (1 << C_EVR_IRQFLAG_FIFOFULL)
#define EVR_IRQFLAG_VIOLATION     (1 << C_EVR_IRQFLAG_VIOLATION)
/**
 * @}
 */


/** 
 * @name  Databuffer Control Register bit mappings
 * @{
 */
#define C_EVR_DATABUF_LOAD        15
#define C_EVR_DATABUF_RECEIVING   15
#define C_EVR_DATABUF_STOP        14
#define C_EVR_DATABUF_RXREADY     14
#define C_EVR_DATABUF_CHECKSUM    13
#define C_EVR_DATABUF_MODE        12
#define C_EVR_DATABUF_RXSIZE      0 // the bit where the value starts
#define C_EVR_DATABUF_RXSIZE_MASK 0x00000FFF
/**
 * @}
 */



/** 
 * @name  Pulse Control Register bit mappings and slot definitions
 * @{
 */
#define C_EVR_PULSE_OUT             7
#define C_EVR_PULSE_SW_SET          6
#define C_EVR_PULSE_SW_RESET        5
#define C_EVR_PULSE_POLARITY        4
#define C_EVR_PULSE_MAP_RESET_ENA   3
#define C_EVR_PULSE_MAP_SET_ENA     2
#define C_EVR_PULSE_MAP_TRIG_ENA    1
#define C_EVR_PULSE_ENA             0

#define EVR_REG_PULSE_SLOT_SIZE	16
#define EVR_REG_PULSE_COUNT		16
#define EVR_REG_PULSE_CTRL_OFFSET	0x0
#define EVR_REG_PULSE_PRESC_OFFSET	0x4
#define EVR_REG_PULSE_DELAY_OFFSET	0x8
#define EVR_REG_PULSE_WIDTH_OFFSET	0xC

/**
 * @}
 */

/** 
 * @name  Output slot definitions
 * @{
 */

#define EVR_REG_FIRST_OUTPUT_FP_TTL 0x400
#define EVR_REG_FIRST_OUTPUT_FP_UNIV 0x440
#define EVR_REG_FIRST_OUTPUT_TB 0x480

// two bytes per output
#define EVR_REG_OUTPUT_SLOT_SIZE 2

/**
 * @}
 */



/** 
 * @name  Map RAM Internal event mappings
 * @{
 */
#define C_EVR_MAP_SAVE_EVENT        31
#define C_EVR_MAP_LATCH_TIMESTAMP   30
#define C_EVR_MAP_LED_EVENT         29
#define C_EVR_MAP_FORWARD_EVENT     28
#define C_EVR_MAP_STOP_FIFO         27
#define C_EVR_MAP_HEARTBEAT_EVENT   5
#define C_EVR_MAP_RESETPRESC_EVENT  4
#define C_EVR_MAP_TIMESTAMP_RESET   3
#define C_EVR_MAP_TIMESTAMP_CLK     2
#define C_EVR_MAP_SECONDS_1         1
#define C_EVR_MAP_SECONDS_0         0

#define EVR_REG_MAPRAM_CLEAR_OFFSET		12
#define EVR_REG_MAPRAM_SET_OFFSET		8
#define EVR_REG_MAPRAM_TRIGGET_OFFSET	4
#define EVR_REG_MAPRAM_INT_FUNC_OFFSET	0
#define EVR_REG_MAPRAM_SLOT_SIZE		16

#define EVR_REG_MAPRAM_EVENT_BIT_SAVE_EVENT_IN_FIFO		(96 + C_EVR_MAP_SAVE_EVENT)


/**
 * @}
 */



/** 
 * @name  Patterns for Some Commonly Used Event Clock Rates
 * 
 * The Micrel SY87739L Fractional-N Synthesizer Configuration 
 * 
 * @{
 */
#define EVR_CLOCK_124950_MHZ        0x00FE816D      /* 124.950 MHz.                                   */
#define EVR_CLOCK_124907_MHZ        0x0C928166      /* 124.907 MHz.                                   */
#define EVR_CLOCK_119000_MHZ        0x018741AD      /* 119.000 MHz.                                   */
#define EVR_CLOCK_106250_MHZ        0x049E81AD      /* 106.250 MHz.                                   */
#define EVR_CLOCK_100625_MHZ        0x02EE41AD      /* 100.625 MHz.                                   */
#define EVR_CLOCK_099956_MHZ        0x025B41ED      /*  99.956 MHz.                                   */
#define EVR_CLOCK_080500_MHZ        0x0286822D      /*  80.500 Mhz.                                   */
#define EVR_CLOCK_050000_MHZ        0x009743AD      /*  50.000 MHz.                                   */
#define EVR_CLOCK_049978_MHZ        0x025B43AD      /*  47.978 MHz.                                   */
/**
 * @}
 */


/** @} */


#endif /* LINUX_EVR_REGS_H_ */


