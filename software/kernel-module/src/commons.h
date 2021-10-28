#ifndef __COMMONS_H_
#define __COMMONS_H_
#include <linux/types.h>

// NOTE: a (almost)Copy of this definition-file exists for the pru-firmware (copy changes by hand)

/**
 * Size of msg-fifo - unrelated to fifo-buffer of pru / shared mem that stores harvest & emulation data
 * this msg-fifo should be at least slightly larger though
 */
#define MSG_FIFO_SIZE (128U)

/**
 * These are the system events that we use to signal events to the PRUs.
 * See the AM335x TRM Table 4-22 for a list of all events
 */
#define HOST_PRU_EVT_TIMESTAMP          (20)

/* The SharedMem struct resides at the beginning of the PRUs shared memory */
#define PRU_SHARED_MEM_STRUCT_OFFSET    (0x10000)

/* Message content description used to distinguish messages for PRU0 */
enum MsgType {
    /* USERSPACE (enum <0xC0) */
    MSG_NONE = 0x00u,
    MSG_BUF_FROM_HOST = 0x01u,
    MSG_BUF_FROM_PRU = 0x02u,
    // DEBUG
    MSG_DBG_ADC = 0xA0u,
    MSG_DBG_DAC = 0xA1u,
    MSG_DBG_GPI = 0xA2u,
    MSG_DBG_GP_BATOK = 0xA3u,
    MSG_DBG_PRINT = 0xA6u,
    MSG_DBG_VSOURCE_P_INP = 0xA8,
    MSG_DBG_VSOURCE_P_OUT = 0xA9,
    MSG_DBG_VSOURCE_V_CAP = 0xAA,
    MSG_DBG_VSOURCE_V_OUT = 0xAB,
    MSG_DBG_VSOURCE_INIT = 0xAC,
    MSG_DBG_FN_TESTS = 0xAF,

    /* KERNELSPACE (enum >=0xC0) */
    // STATUS
    MSG_STATUS_RESTARTING_ROUTINE = 0xC0,
    // ERROR
    MSG_ERROR = 0xE0u,
    MSG_ERR_MEMCORRUPTION = 0xE1u,
    MSG_ERR_BACKPRESSURE = 0xE2u,
    MSG_ERR_INCMPLT = 0xE3u,
    MSG_ERR_INVLDCMD = 0xE4u,
    MSG_ERR_NOFREEBUF = 0xE5u,
    MSG_ERR_TIMESTAMP = 0xE6u,
    MSG_ERR_SYNC_STATE_NOT_IDLE = 0xE7u,
    MSG_ERR_VALUE = 0xE8u,
    // Routines
    MSG_TEST = 0xEAu,
    MSG_SYNC = 0xEBu
};

enum MsgID { MSG_TO_KERNEL = 0x55, MSG_TO_PRU = 0xAA };

enum ShepherdMode {
	MODE_HARVEST,
	MODE_HARVEST_TEST,
	MODE_EMULATE,
	MODE_EMULATE_TEST,
	MODE_EMULATE_CAL,
	MODE_DEBUG,
	MODE_NONE
};
enum ShepherdState {
	STATE_UNKNOWN,
	STATE_IDLE,
	STATE_ARMED,
	STATE_RUNNING,
	STATE_RESET,
	STATE_FAULT
};

/* Firmware-container for Programmer in same mem-area as FIFO for Samplebuffer */
struct ProgrammerFW {
	uint32_t signature1; // kernel writes and pru tests it for 0xDEADD00D
	uint32_t signature2; // kernel writes and pru tests it for 0x8BADF00D
	/* variable size array "hack" */
	/* -> typecast the mem-pointer to this struct */
	uint32_t length;
	uint32_t data[];
} __attribute__((packed));

/* Programmer-Control as part of SharedMem-Struct */
struct ProgrammerCtrl {
	uint32_t state; 	// flag
	uint32_t protocol; 	// 1: swd, 2: sbw, 3: jtag
	uint32_t datarate;	// baud
	uint32_t pin_tck;	// clock-output
	uint32_t pin_tdio;	// io for swd & sbw, only input for JTAG (TDI)
	uint32_t pin_tdo;	// data-output, only for JTAG
	uint32_t pin_tms;	// mode, only for JTAG (TMS)
} __attribute__((packed));


/* calibration values - usage example: voltage_uV = adc_value * gain_factor + offset
 * numbers for hw-rev2.0
 * ADC: VIn = DOut * 19.5313 uV -> factor for raw-value to calc uV_n8 (*256) = 5'000
 * 	CIn = DOut * 195.313 nA -> factor for raw-value to calc nA_n8 (*256) = 50'000
 * DAC	VOut = DIn * 76.2939 uV -> inverse factor to get raw_n20-value from uV_n20 = 13'743
 */
struct CalibrationConfig {
	/* Gain of load current adc. It converts current to ADC raw value */
	uint32_t adc_current_factor_nA_n8; // n8 means normalized to 2^8 (representing 1.0)
	/* Offset of load current adc */
	int32_t adc_current_offset_nA;
	/* Gain of DAC. It converts voltage to DAC raw value */
	uint32_t dac_voltage_inv_factor_uV_n20; // n20 means normalized to 2^20 (representing 1.0)
	/* Offset of load voltage DAC */
	int32_t dac_voltage_offset_uV;
} __attribute__((packed));

#define LUT_SIZE	(12)

/* This structure defines all settings of virtual converter emulation
 * more complex regulators use vars in their section and above
 * NOTE: sys-FS-FNs currently uses 4 byte steps for transfer, so struct must be (size)mod4=0
 * Container-sizes with SI-Units:
 * 	_nF-u32 = ~ 4.294 F
 * 	_uV-u32 = 4294 V
 * 	_nA-u32 = ~ 4.294 A
 */
struct ConverterConfig {
	/* General Reg Config */
	uint32_t converter_mode; // bitmask to alter functionality
	uint32_t interval_startup_delay_drain_n; // allow target to power up and go to sleep

	uint32_t V_input_max_uV;
	uint32_t I_input_max_nA;  // limits input-power
	uint32_t V_input_drop_uV; // simulate possible diode
	uint32_t Constant_1k_per_Ohm; // resistance only active with disabled boost

	uint32_t Constant_us_per_nF_n28;
	uint32_t V_intermediate_init_uV; // allow a proper / fast startup
	uint32_t I_intermediate_leak_nA;

	uint32_t V_enable_output_threshold_uV;  // -> output gets connected (hysteresis-combo with next value)
	uint32_t V_disable_output_threshold_uV; // -> output gets disconnected
	uint32_t dV_enable_output_uV; // compensate C_out, for disable state when V_intermediate < V_enable/disable_threshold_uV
	uint32_t interval_check_thresholds_n; // some BQs check every 65 ms if output should be disconnected

	uint32_t V_pwr_good_enable_threshold_uV; // target is informed by pwr-good-pin (hysteresis)
	uint32_t V_pwr_good_disable_threshold_uV;
	uint32_t immediate_pwr_good_signal; // bool, 0: stay in interval for checking thresholds, >=1: emulate schmitt-trigger,

	uint32_t V_output_log_gpio_threshold_uV; // min voltage to prevent jitter-noise in gpio-trace-recording

	/* Boost Reg */
	uint32_t V_input_boost_threshold_uV; // min input-voltage for the boost converter to work
	uint32_t V_intermediate_max_uV;  // -> boost shuts off

	/* Buck Reg */
	uint32_t V_output_uV;
	uint32_t V_buck_drop_uV; // simulate dropout-voltage or diode

	/* LUTs */
	uint32_t LUT_input_V_min_log2_uV; // only u8 needed
	uint32_t LUT_input_I_min_log2_nA; // only u8 needed
	uint32_t LUT_output_I_min_log2_nA; // only u8 needed
	uint8_t LUT_inp_efficiency_n8[LUT_SIZE][LUT_SIZE]; // depending on inp_voltage, inp_current, (cap voltage), n8 means normalized to 2^8 => 1.0
	uint32_t LUT_out_inv_efficiency_n4[LUT_SIZE]; // depending on output_current, inv_n4 means normalized to inverted 2^4 => 1/1024,
} __attribute__((packed));


struct HarvesterConfig{
	uint32_t algorithm;
	uint32_t window_size;
	uint32_t voltage_uV;
	uint32_t voltage_min_uV;
	uint32_t voltage_max_uV;
	uint32_t voltage_step_uV;  // for window-based algo like ivcurve
	uint32_t current_limit_nA;   // lower bound to detect zero current
	uint32_t setpoint_n8;
	uint32_t interval_n;	// between measurements
	uint32_t duration_n;	// of measurement
	uint32_t dac_resolution_bit;  // smallest step-size in bit
	uint32_t wait_cycles_n; // for DAC to settle
} __attribute__((packed));


/* Format of Message-Protocol between PRUs & Kernel Module */
struct ProtoMsg {
	/* Identifier => Canary, This is used to identify memory corruption */
	uint8_t id;
	/* Token-System to signal new message & the ack, (sender sets unread/1, receiver resets/0) */
	uint8_t unread;
	/* content description used to distinguish messages, see enum MsgType */
	uint8_t type;
	/* Alignment with memory, (bytes)mod4 */
	uint8_t reserved[1];
	/* Actual Content of message */
	uint32_t value[2];
} __attribute__((packed));


/* Control reply message sent from this kernel module to PRU1 after running the control loop */
struct SyncMsg {
	/* Identifier => Canary, This is used to identify memory corruption */
	uint8_t id;
	/* Token-System to signal new message & the ack, (sender sets unread, receiver resets) */
	uint8_t unread;
	/* content description used to distinguish messages, see enum MsgType */
	uint8_t type; // only needed for debug
	/* Alignment with memory, (bytes)mod4 */
	uint8_t reserved0[1];
	/* Actual Content of message */
	uint32_t buffer_block_period;   // corrected ticks that equal 100ms
	uint32_t analog_sample_period;  // ~ 10 us
	uint32_t compensation_steps;    // remainder of buffer_block/sample_count = sample_period
	uint64_t next_timestamp_ns;     // start of next buffer block
} __attribute__((packed));


/* This is external to expose some of the attributes through sysfs */
extern void __iomem *pru_shared_mem_io;

struct SharedMem {
	uint32_t shepherd_state;
	/* Stores the mode, e.g. harvesting or emulation */
	uint32_t shepherd_mode;
	/* Allows setting a fixed voltage for the seconds DAC-Output (Channel A),
	 * TODO: this has to be optimized, allow better control (off, link to ch-b, change NOW) */
	uint32_t dac_auxiliary_voltage_raw;
	/* Physical address of shared area in DDR RAM, that is used to exchange data between user space and PRUs */
	uint32_t mem_base_addr;
	/* Length of shared area in DDR RAM */
	uint32_t mem_size;
	/* Maximum number of buffers stored in the shared DDR RAM area */
	uint32_t n_buffers;
	/* Number of IV samples stored per buffer */
	uint32_t samples_per_buffer;
	/* The time for sampling samples_per_buffer. Determines sampling rate */
	uint32_t buffer_period_ns;
	/* active utilization-monitor for PRU0 */
	uint32_t pru0_max_ticks_per_sample;
	/* ADC calibration settings */
	struct CalibrationConfig calibration_settings;
	/* This structure defines all settings of virtual converter emulation*/
	struct ConverterConfig converter_settings;
	struct HarvesterConfig harvester_settings;
	/* settings for programmer-subroutines */
	struct ProgrammerCtrl programmer_ctrl;
	/* Msg-System-replacement for slow rpmsg (check 640ns, receive 2820 on pru0 and 4820ns on pru1) */
	struct ProtoMsg pru0_msg_inbox;
	struct ProtoMsg pru0_msg_outbox;
	struct ProtoMsg pru0_msg_error;
	struct SyncMsg  pru1_sync_inbox;
	struct ProtoMsg pru1_sync_outbox;
	struct ProtoMsg pru1_msg_error;
    	/* NOTE: struct is capped here, following vars are only for PRUs */
} __attribute__((packed));

#endif /* __COMMONS_H_ */