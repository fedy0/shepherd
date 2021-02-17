#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/math64.h>

#include "sync_ctrl.h"
#include "pru_comm.h"

struct sync_data_s *sync_data;

static int64_t ns_sys_to_wrap;
static uint64_t next_timestamp_ns;
static uint64_t prev_timestamp_ns = 0; 	/* for plausibility-check */

void reset_prev_timestamp(void)
{
    prev_timestamp_ns = 0;
}

static enum hrtimer_restart trigger_loop_callback(struct hrtimer *timer_for_restart);
static enum hrtimer_restart sync_loop_callback(struct hrtimer *timer_for_restart);
static uint32_t trigger_loop_period_ns = 100000000; /* just initial value to avoid div0 */

/* Timer to trigger fast synch_loop */
struct hrtimer trigger_loop_timer;
struct hrtimer synch_loop_timer;

/* series of halving sleep cycles, sleep less coming slowly near a total of 100ms of sleep */
const static unsigned int timer_steps_ns[] = {
        20000000u, 20000000u,
        20000000u, 20000000u, 10000000u,
        5000000u,  2000000u,  1000000u,
        500000u,   200000u,   100000u,
        50000u,    20000u};
const static size_t timer_steps_ns_size = sizeof(timer_steps_ns) / sizeof(timer_steps_ns[0]);
//static unsigned int step_pos = 0;

int sync_exit(void)
{
	hrtimer_cancel(&trigger_loop_timer);
	hrtimer_cancel(&synch_loop_timer);
	kfree(sync_data);

	return 0;
}

int sync_init(uint32_t timer_period_ns)
{
	struct timespec ts_now;
	uint64_t now_ns_system;
	uint32_t ns_over_wrap;
	uint64_t ns_now_until_trigger;

	sync_data = kmalloc(sizeof(struct sync_data_s), GFP_KERNEL);
	if (!sync_data)
		return -1;
	sync_reset();

    /* Timestamp system clock */
    getnstimeofday(&ts_now);
    now_ns_system = (uint64_t)timespec_to_ns(&ts_now);

	/* timer for trigger, TODO: this needs better naming, make clear what it does */
	trigger_loop_period_ns = timer_period_ns; /* 100 ms */
    //printk(KERN_INFO "shprd: KMod - new timer_period_ns = %u\n", trigger_loop_period_ns);

    hrtimer_init(&trigger_loop_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    trigger_loop_timer.function = &trigger_loop_callback;

    /* timer for Synch-Loop */
    hrtimer_init(&synch_loop_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    synch_loop_timer.function = &sync_loop_callback;

	div_u64_rem(now_ns_system, timer_period_ns, &ns_over_wrap);
	if (ns_over_wrap > (timer_period_ns / 2))
		ns_now_until_trigger = 2 * timer_period_ns - ns_over_wrap;
	else
		ns_now_until_trigger = timer_period_ns - ns_over_wrap;

	hrtimer_start(&trigger_loop_timer,
		      ns_to_ktime(now_ns_system + ns_now_until_trigger),
		      HRTIMER_MODE_ABS);

    hrtimer_start(&synch_loop_timer,
            ns_to_ktime(now_ns_system + 1000000),
            HRTIMER_MODE_ABS);

	return 0;
}

int sync_reset(void)
{
	sync_data->err_sum = 0;
	sync_data->err = 0;
	sync_data->clock_corr = 0;
	return 0;
}

enum hrtimer_restart trigger_loop_callback(struct hrtimer *timer_for_restart)
{
	struct timespec ts_now;
	uint64_t now_ns_system;
	uint32_t ns_over_wrap;
	uint64_t ns_now_until_trigger;

	/* Raise Interrupt on PRU, telling it to timestamp IEP */
	pru_comm_trigger(HOST_PRU_EVT_TIMESTAMP);
	/* Timestamp system clock */
	getnstimeofday(&ts_now);

	now_ns_system = (uint64_t)timespec_to_ns(&ts_now);

	/*
     * Get distance of system clock from timer wrap.
     * Is negative, when interrupt happened before wrap, positive when after
     */
	div_u64_rem(now_ns_system, trigger_loop_period_ns,
		    &ns_over_wrap);
	if (ns_over_wrap > (trigger_loop_period_ns / 2)) {
		ns_sys_to_wrap =
			((int64_t)ns_over_wrap - trigger_loop_period_ns);
		next_timestamp_ns = now_ns_system - ns_over_wrap +
				    2 * trigger_loop_period_ns;
		ns_now_until_trigger =
			2 * trigger_loop_period_ns - ns_over_wrap;
	} else {
		ns_sys_to_wrap = ((int64_t)ns_over_wrap);
		next_timestamp_ns = now_ns_system - ns_over_wrap +
                trigger_loop_period_ns;
		ns_now_until_trigger =
                trigger_loop_period_ns - ns_over_wrap;
	}

	hrtimer_forward(timer_for_restart, timespec_to_ktime(ts_now),
			ns_to_ktime(ns_now_until_trigger));

	return HRTIMER_RESTART;
}

/* Handler for ctrl-requests from PRU1 */
enum hrtimer_restart sync_loop_callback(struct hrtimer *timer_for_restart)
{
    struct CtrlReqMsg ctrl_req;
    struct CtrlRepMsg ctrl_rep;
    struct timespec ts_now;
    static unsigned int step_pos = 0;
    /* Timestamp system clock */
    getnstimeofday(&ts_now);

    if (pru_comm_get_ctrl_request(&ctrl_req))
    {
        if (ctrl_req.identifier != MSG_SYNC_CTRL_REQ)
        {
            /* Error occurs if something writes over boundaries */
            printk(KERN_ERR "shprd: Kernel Recv_CtrlRequest -> mem corruption?\n");
        }

        sync_loop(&ctrl_rep, &ctrl_req);

        if (!pru_comm_send_ctrl_reply(&ctrl_rep))
        {
            /* Error occurs if PRU was not able to handle previous message in time */
            printk(KERN_WARNING "shprd: Kernel Send_CtrlResponse -> back-pressure\n");
        }

        /* resetting to longest sleep period */
        step_pos = 0;
    }

    hrtimer_forward(timer_for_restart, timespec_to_ktime(ts_now),
            ns_to_ktime(timer_steps_ns[step_pos])); /* variable sleep cycle */
    /* TODO: ktime_get() seems a proper replacement for "timespec_to_ktime(ts_now)" */

    if (step_pos < timer_steps_ns_size - 1) step_pos++;

    return HRTIMER_RESTART;
}


int sync_loop(struct CtrlRepMsg *const ctrl_rep, const struct CtrlReqMsg *const ctrl_req)
{
	int64_t ns_iep_to_wrap;
	int32_t clock_corr;
	uint64_t ns_per_tick;

	/*
     * Based on the previous IEP timer period and the nominal timer period
     * we can estimate the real nanoseconds passing per tick
     * We operate on fixed point arithmetics by shifting by 30 bit
     */
	ns_per_tick = div_u64(((uint64_t)trigger_loop_period_ns << 30u),
			      ctrl_req->old_period);

	/*
     * Get distance of IEP clock at interrupt from timer wrap
     * negative, if interrupt happened before wrap, positive after
     */
	ns_iep_to_wrap = ((int64_t)ctrl_req->ticks_iep) * ns_per_tick;
    /* YES, following 29 is correct, if ns_iep is over the half it is shorter to go the other direction */
	if (ns_iep_to_wrap > ((uint64_t)trigger_loop_period_ns << 29u))
	{
		ns_iep_to_wrap -= ((uint64_t)trigger_loop_period_ns << 30u);
	}

	/* Difference between system clock and IEP clock phase */
	sync_data->err = div_s64(ns_iep_to_wrap, 1ul<<30u) - ns_sys_to_wrap; // TODO: could save some divs
	sync_data->err_sum += sync_data->err;

	/* This is the actual PI controller equation, TODO: unit of clock_corr is ticks not ns
	 * previous parameters were:    P=1/32, I=1/128, correction settled at ~1340 with values from 1321 to 1359
	 * current parameters:          P=1/100,I=1/300, correction settled at ~1332 with values from 1330 to 1335
	 * */
	clock_corr = (int32_t)(div_s64(sync_data->err, 100) + div_s64(sync_data->err_sum, 300));
    /*
    printk(KERN_ERR "shprd: KMod - error=%lld, ns_iep=%lld, ns_sys=%lld, errsum=%lld, old_period=%u, corr=%d\n",
            sync_data->err,
            div_s64(ns_iep_to_wrap, 1ul<<30u),
            ns_sys_to_wrap,
            sync_data->err_sum,
            ctrl_req->old_period, clock_corr);
    */
	/* for plausibility-check, in case the sync-algo produces jumps */
	if (prev_timestamp_ns > 0)
    {
        int64_t diff_timestamp = div_s64(next_timestamp_ns - prev_timestamp_ns, 1000000u);
        if (diff_timestamp < 0)
            printk(KERN_ERR "shprd: KMod = backwards timestamp-jump detected \n");
        else if (diff_timestamp < 95)
            printk(KERN_ERR "shprd: KMod = too small timestamp-jump detected\n");
        else if (diff_timestamp > 105)
            printk(KERN_ERR "shprd: KMod = forwards timestamp-jump detected\n");
    }
    prev_timestamp_ns = next_timestamp_ns;

	sync_data->clock_corr = clock_corr;

    /* fill msg */
    ctrl_rep->clock_corr = clock_corr;
    ctrl_rep->next_timestamp_ns = next_timestamp_ns;

	return 0;
}