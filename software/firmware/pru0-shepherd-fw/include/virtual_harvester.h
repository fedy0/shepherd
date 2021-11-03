#ifndef PRU_FIRMWARE_PRU0_SHEPHERD_FW_HARVESTER_H
#define PRU_FIRMWARE_PRU0_SHEPHERD_FW_HARVESTER_H

#include "commons.h"
#include "stdint.h"

void harvester_initialize(const volatile struct HarvesterConfig *const);

void harvester_adc_sample(struct SampleBuffer *const buffer, uint32_t sample_idx);

void harvester_iv_sample(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA);

#endif //PRU_FIRMWARE_PRU0_SHEPHERD_FW_HARVESTER_H