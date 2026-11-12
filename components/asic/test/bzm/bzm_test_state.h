#ifndef BZM_TEST_STATE_H
#define BZM_TEST_STATE_H

#include "device_config.h"

/* A private application view for isolated copies of the Bonanza drivers. */
#define TEST_STUB_GLOBAL_STATE_H
#include "power_management_task.h"
typedef struct {
    bool pools_unavailable, mining_paused, hardware_fault, overheat_mode;
    char hardware_fault_msg[64];
    const char *asic_status;
} SystemModule;
typedef struct GlobalState {
    DeviceConfig DEVICE_CONFIG;
    PowerManagementModule POWER_MANAGEMENT_MODULE;
    SystemModule SYSTEM_MODULE;
    struct { bool is_active, is_finished; } SELF_TEST_MODULE;
    bool ASIC_initalized;
    uint8_t current_difficulty_power;
} GlobalState;

#endif
