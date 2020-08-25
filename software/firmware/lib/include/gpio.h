#ifndef __GPIO_H_
#define __GPIO_H_

#ifdef __GNUC__
#include <pru/io.h>
#else
volatile register uint32_t __R30;
volatile register uint32_t __R31;
#define read_r30()      __R30
#define write_r30(x)    __R30 = x
#define read_r31()      __R31
#define write_r31(x)    __R31 = x
#endif

#if defined(PRU0)

    #define P8_11 15
    #define P8_12 14

    #define P9_25 7
    #define P9_27 5
    #define P9_28 3
    #define P9_29 1
    #define P9_30 2
    #define P9_31 0
    #define P9_41B 6
    #define P9_42B 4

#elif defined(PRU1)

    #define P8_20 13
    #define P8_21 12

    #define P8_27 8
    #define P8_28 10
    #define P8_29 9
    #define P8_30 11

    #define P8_39 6
    #define P8_40 7
    #define P8_41 4
    #define P8_42 5
    #define P8_43 2
    #define P8_44 3
    #define P8_45 0
    #define P8_46 1

#else
    #error
#endif


#define _GPIO_TOGGLE(x) write_r30(read_r30() ^ (1 << x))
#define _GPIO_ON(x)     write_r30(read_r30() | (1 << x))
#define _GPIO_OFF(x)    write_r30(read_r30() & ~(1 << x))

#endif /* __GPIO_H_ */
