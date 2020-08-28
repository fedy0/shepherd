#ifndef __INTC_H_
#define __INTC_H_

#include "gpio.h"

/*
 * This triggers the corresponding system event in INTC
 * See PRU-ICSS Reference Guide §5.2.2.2
 */
#ifdef __GNUC__
#define INTC_TRIGGER_EVENT(x)   write_r31((1U << 5U) + (x - 16U))
#else
#define INTC_TRIGGER_EVENT(x)   __R31 = (1U << 5U) + (x - 16U)
#endif
#define INTC_CHECK_EVENT(x)     CT_INTC.SECR0 &(1U << x)
#define INTC_CLEAR_EVENT(x)     CT_INTC.SICR_bit.STS_CLR_IDX = x;

#endif /* __INTC_H_ */
