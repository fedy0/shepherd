#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pru_cfg.h>
#include <pru_iep.h>
#include <pru_intc.h>
#include <rsc_types.h>

#include "iep.h"

#include "stdint_fast.h"
#include "gpio.h"
#include "intc.h"
#include "resource_table_def.h"
#include "rpmsg.h"
#include "simple_lock.h"

#include "commons.h"
#include "hw_config.h"
#include "ringbuffer.h"
#include "sampling.h"
#include "shepherd_config.h"
#include "virtual_source.h"

/* Used to signal an invalid buffer index */
#define NO_BUFFER 0xFFFFFFFF

#define SPI_SYS_TEST_EN     0
#if SPI_SYS_TEST_EN > 0 // NOTE: code is currently in extended pssp (not mainline)
#include "spi_transfer_sys.h"
#endif

static void send_message(const uint32_t msg_id, const uint32_t value)
{
	const struct DEPMsg msg_out = { .msg_type= msg_id, .value = value};
	rpmsg_putraw((void *)&msg_out, sizeof(struct DEPMsg));
	// TODO: could also be replaced by new msg-system / rp-replacement
}

static uint32_t handle_block_swap(volatile struct SharedMem *const shared_mem, struct RingBuffer *const free_buffers_ptr,
			  struct SampleBuffer *const buffers_far, const uint32_t current_buffer_idx, const uint32_t analog_sample_idx)
{
	uint32_t next_buffer_idx;
	uint8_t tmp_idx;

	/* If we currently have a valid buffer, return it to host */
	// NOTE1: this must come first or else python-backend gets confused
	// NOTE2: was in mutex-state before, but it does not need to, only blocks gpio-sampling / pru1 (80% of workload is in this fn)
	if (current_buffer_idx != NO_BUFFER) {
		if (analog_sample_idx != ADC_SAMPLES_PER_BUFFER) // TODO: could be removed in future, not possible anymore
			send_message(MSG_DEP_ERR_INCMPLT, analog_sample_idx);

		(buffers_far + current_buffer_idx)->len = analog_sample_idx;
		send_message(MSG_DEP_BUF_FROM_PRU, current_buffer_idx);
	}

	/* Lock access to gpio_edges structure to avoid inconsistency */
	simple_mutex_enter(&shared_mem->gpio_edges_mutex);

	/* Fetch new buffer from ring */
	if (ring_get(free_buffers_ptr, &tmp_idx) > 0) {
		next_buffer_idx = (uint32_t)tmp_idx;
        	struct SampleBuffer *const next_buffer = buffers_far + next_buffer_idx;
		next_buffer->timestamp_ns = shared_mem->next_timestamp_ns;
		shared_mem->gpio_edges = &next_buffer->gpio_edges;
		shared_mem->gpio_edges->idx = 0;

		if (shared_mem->next_timestamp_ns == 0)
		{
			/* debug-output for a wrong timestamp */
			GPIO_TOGGLE(DEBUG_PIN1_MASK);
			GPIO_TOGGLE(DEBUG_PIN0_MASK);
			GPIO_TOGGLE(DEBUG_PIN1_MASK);
			GPIO_TOGGLE(DEBUG_PIN0_MASK);
		}
	} else {
		next_buffer_idx = NO_BUFFER;
		shared_mem->gpio_edges = NULL;
		send_message(MSG_DEP_ERR_NOFREEBUF, 0);
	}
	simple_mutex_exit(&shared_mem->gpio_edges_mutex);

	return next_buffer_idx;
}

// TODO: this system can also be replaced by shared-mem-msg-system, including the send_message() above
// fn emits a 0 on error, 1 on success
static bool_ft handle_rpmsg(struct RingBuffer *const free_buffers_ptr, const enum ShepherdMode mode,
		 const enum ShepherdState state, const uint32_t gpio_pin_state)
{
	struct DEPMsg msg_in;

	/*
	 * TI's implementation of RPMSG on the PRU triggers the same interrupt
	 * line when sending a message that is used to signal the reception of
	 * the message. We therefore need to check the length of the potential
	 * message
	 */
	if (rpmsg_get((void *)&msg_in) != sizeof(struct DEPMsg))
		return 1U;

	if ((mode == MODE_DEBUG) && (state == STATE_RUNNING)) {
        uint32_t res;
		switch (msg_in.msg_type) {
		case MSG_DEP_DBG_ADC:
			res = sample_dbg_adc(msg_in.value);
			send_message(MSG_DEP_DBG_ADC, res);
			return 1U;

		case MSG_DEP_DBG_DAC:
			sample_dbg_dac(msg_in.value);
			return 1U;

		case MSG_DEP_DBG_GPI:
			send_message(MSG_DEP_DBG_GPI, gpio_pin_state);
			return 1U;

		default:
			send_message(MSG_DEP_ERR_INVLDCMD, msg_in.msg_type);
			return 0U;
		}
	} else {
		if (msg_in.msg_type == MSG_DEP_BUF_FROM_HOST) {
			ring_put(free_buffers_ptr, (uint8_t)msg_in.value);
			return 1U;
		} else {
			send_message(MSG_DEP_ERR_INVLDCMD, msg_in.msg_type);
			return 0U;
		}
	}
}

void event_loop(volatile struct SharedMem *const shared_mem,
		struct RingBuffer *const free_buffers_ptr,
		struct SampleBuffer *const buffers_far)
{
	uint32_t sample_buf_idx = NO_BUFFER;
	enum ShepherdMode shepherd_mode = (enum ShepherdMode)shared_mem->shepherd_mode;

	while (1)
	{
		// take a snapshot of current triggers -> ensures prioritized handling
		// edge case: sample0 @cnt=0, cmp0&1 trigger, but cmp0 needs to get handled before cmp1
		const uint32_t iep_tmr_cmp_sts = iep_get_tmr_cmp_sts(); // 12 cycles, 60 ns

		// pru0 manages the irq, but pru0 reacts to it directly -> less jitter
		if ((shared_mem->cmp1_handled_by_pru0 == 0) && ((shared_mem->cmp1_handled_by_pru1 == 1) || iep_check_evt_cmp_fast(iep_tmr_cmp_sts, IEP_CMP1_MASK)))
		{
			shared_mem->cmp1_handled_by_pru0 = 1;

			/* The actual sampling takes place here */
			if ((sample_buf_idx != NO_BUFFER) && (shared_mem->analog_sample_counter < ADC_SAMPLES_PER_BUFFER))
			{
				GPIO_ON(DEBUG_PIN0_MASK);
				sample(buffers_far + sample_buf_idx, shared_mem->analog_sample_counter, shepherd_mode);
				GPIO_OFF(DEBUG_PIN0_MASK);
			}
			else
			{
				/* even if offline, the routine should simulate a (short) sampling-action */
				__delay_cycles(4000 / 5);
			}

			shared_mem->analog_sample_counter++;

			if (shared_mem->analog_sample_counter == ADC_SAMPLES_PER_BUFFER)
			{
                		// TODO: this still needs sorting -> block end must be called even before a block ends ... to get a valid buffer
				/* Did the Linux kernel module ask for reset? */
				if (shared_mem->shepherd_state == STATE_RESET) return;

				/* PRU tries to exchange a full buffer for a fresh one if measurement is running */
				if ((shared_mem->shepherd_state == STATE_RUNNING) &&
				    (shared_mem->shepherd_mode != MODE_DEBUG))
				{
					//sample_buf_idx = handle_block_swap(shared_mem, free_buffers_ptr, buffers_far, sample_buf_idx, analog_sample_idx);
					sample_buf_idx = handle_block_swap(shared_mem, free_buffers_ptr, buffers_far, sample_buf_idx, shared_mem->analog_sample_counter);
					shared_mem->analog_sample_counter = 0;
					GPIO_TOGGLE(DEBUG_PIN1_MASK); // NOTE: desired user-feedback
				}
			}
			/* We only handle rpmsg comms if we're not at the last sample */
			else {
				//GPIO_ON(DEBUG_PIN0_MASK);
				handle_rpmsg(free_buffers_ptr,
					     (enum ShepherdMode)shared_mem->shepherd_mode,
					     (enum ShepherdState)shared_mem->shepherd_state,
					     shared_mem->gpio_pin_state);
                		//GPIO_OFF(DEBUG_PIN0_MASK);
			}
		}

		// this stack ensures low overhead to event loop AND full buffer before switching
		if ((shared_mem->cmp0_handled_by_pru0 == 0) && ((shared_mem->cmp0_handled_by_pru1 == 1) || iep_check_evt_cmp_fast(iep_tmr_cmp_sts, IEP_CMP0_MASK)))
		{
			GPIO_TOGGLE(DEBUG_PIN1_MASK);
			shared_mem->cmp0_handled_by_pru0 = 1;
			if (shared_mem->analog_sample_counter > 1)
				shared_mem->analog_sample_counter = 1;
			GPIO_TOGGLE(DEBUG_PIN1_MASK);
		}
	}
}

void main(void)
{
	GPIO_OFF(DEBUG_PIN0_MASK | DEBUG_PIN1_MASK);
	static struct RingBuffer free_buffers;

	/*
	 * The shared mem is dynamically allocated and we have to inform user space
	 * about the address and size via sysfs, which exposes parts of the
	 * shared_mem structure.
	 * Do this initialization early! The kernel module relies on it.
	 */
	volatile struct SharedMem *const shared_memory = (volatile struct SharedMem *)PRU_SHARED_MEM_STRUCT_OFFSET;
	// Initialize all struct-Members Part A (Part B in Reset Loop)
	shared_memory->mem_base_addr = resourceTable.shared_mem.pa;
	shared_memory->mem_size = resourceTable.shared_mem.len;

	shared_memory->n_buffers = RING_SIZE;
	shared_memory->samples_per_buffer = ADC_SAMPLES_PER_BUFFER;
	shared_memory->buffer_period_ns = BUFFER_PERIOD_NS;

	shared_memory->dac_auxiliary_voltage_raw = 0;
	shared_memory->shepherd_state = STATE_IDLE;
	shared_memory->shepherd_mode = MODE_HARVEST;  // TODO: is this the the error for "wrong state"?

	shared_memory->next_timestamp_ns = 0;
	shared_memory->analog_sample_counter = 0;

	/* this init is nonsense, but testable for byteorder and proper values */
	shared_memory->calibration_settings = (struct Calibration_Config){
		.adc_current_factor_nA_n8=255u, .adc_current_offset_nA=-1,
		.dac_voltage_inv_factor_uV_n20=254u, .dac_voltage_offset_uV=-2};

	vsource_struct_init(&shared_memory->virtsource_settings);

	shared_memory->ctrl_req = (struct CtrlReqMsg){.identifier=0u, .msg_unread=0u, .ticks_iep=0u, .old_period=0u};
	shared_memory->ctrl_rep = (struct CtrlRepMsg){
		.identifier=0u,
		.msg_unread=0u,
		.buffer_block_period=TIMER_BASE_PERIOD,
		.analog_sample_period=TIMER_BASE_PERIOD/ADC_SAMPLES_PER_BUFFER,
		.compensation_steps=0,
		.compensation_distance=0xFFFFFFFFu,
		.next_timestamp_ns=0u};
	/*
	 * The dynamically allocated shared DDR RAM holds all the buffers that
	 * are used to transfer the actual data between us and the Linux host.
	 * This memory is requested from remoteproc via a carveout resource request
	 * in our resourcetable
	 */
	struct SampleBuffer *const buffers_far = (struct SampleBuffer *)resourceTable.shared_mem.pa;

	/* Allow OCP master port access by the PRU so the PRU can read external memories */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;
	rpmsg_init("rpmsg-pru");

#if SPI_SYS_TEST_EN // TODO: Test-Area
	sys_spi_init();
#endif

reset:
	ring_init(&free_buffers);

	GPIO_ON(DEBUG_PIN0_MASK | DEBUG_PIN1_MASK);
	sample_init(shared_memory);
	GPIO_OFF(DEBUG_PIN0_MASK | DEBUG_PIN1_MASK);

	shared_memory->gpio_edges = NULL;

	// Initialize struct-Members Part B
	// Reset Token-System to init-values
	shared_memory->cmp0_handled_by_pru0 = 0;
	shared_memory->cmp0_handled_by_pru1 = 0;
	shared_memory->cmp1_handled_by_pru0 = 0;
	shared_memory->cmp1_handled_by_pru1 = 0;

	shared_memory->shepherd_state = STATE_IDLE;
	/* Make sure the mutex is clear */
	simple_mutex_exit(&shared_memory->gpio_edges_mutex);

	event_loop(shared_memory, &free_buffers, buffers_far);
	goto reset;
}
