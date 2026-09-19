#ifndef BZM_TEST_STATE_H
#define BZM_TEST_STATE_H

#include "device_config.h"

/* A private application view for isolated copies of the Bonanza drivers. */
#define TEST_STUB_GLOBAL_STATE_H
typedef struct {
    float frequency_value, actual_frequency, expected_hashrate;
    float voltage, core_voltage, current, power, vr_temp;
    float chip_temp_avg, chip_temp2_avg;
    uint16_t fan_rpm;
    int fan_perc;
} PowerManagementModule;
typedef struct GlobalState {
    DeviceConfig DEVICE_CONFIG;
    PowerManagementModule POWER_MANAGEMENT_MODULE;
    struct { bool pools_unavailable, mining_paused, hardware_fault; } SYSTEM_MODULE;
    struct { bool is_active; } SELF_TEST_MODULE;
    bool ASIC_initalized;
} GlobalState;

#endif
