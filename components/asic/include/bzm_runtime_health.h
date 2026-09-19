#ifndef BZM_RUNTIME_HEALTH_H
#define BZM_RUNTIME_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "bzm_bridge.h"
#include "bzm_telemetry.h"

#define BZM_RUNTIME_HEALTH_DETAIL_LENGTH 160U

/* PMBus values used by the BZM TPS546 runtime interlock. */
#define BZM_RUNTIME_HEALTH_TPS_OPERATION_ON_MASK 0x80U
/* Check the regulator after startup settles; every STATUS_WORD fault stops mining. */
#define BZM_RUNTIME_HEALTH_TPS_STATUS_FAULT_MASK 0xffffU

typedef enum
{
    BZM_RUNTIME_HEALTH_GOOD = 0,
    BZM_RUNTIME_HEALTH_BAD = 1,
} bzm_runtime_health_status_t;

typedef enum
{
    BZM_RUNTIME_HEALTH_FAULT_NONE = 0x0000,
    BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT = 0x0001,

    BZM_RUNTIME_HEALTH_FAULT_BRIDGE_UNAVAILABLE = 0x0100,
    BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATUS_INVALID = 0x0101,
    BZM_RUNTIME_HEALTH_FAULT_BRIDGE_CAPABILITY_MISSING = 0x0102,
    BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATE = 0x0103,
    BZM_RUNTIME_HEALTH_FAULT_BRIDGE_FAULT = 0x0104,
    BZM_RUNTIME_HEALTH_FAULT_BRIDGE_LEASE = 0x0105,
    BZM_RUNTIME_HEALTH_FAULT_BRIDGE_OUTPUT = 0x0106,

    BZM_RUNTIME_HEALTH_FAULT_FAN_TACH_UNAVAILABLE = 0x0201,
    BZM_RUNTIME_HEALTH_FAULT_FAN_TACH_LOW = 0x0202,

    BZM_RUNTIME_HEALTH_FAULT_TPS_UNAVAILABLE = 0x0300,
    BZM_RUNTIME_HEALTH_FAULT_TPS_PGOOD_LOW = 0x0301,
    BZM_RUNTIME_HEALTH_FAULT_TPS_OPERATION_OFF = 0x0302,
    BZM_RUNTIME_HEALTH_FAULT_TPS_STATUS = 0x0303,
    BZM_RUNTIME_HEALTH_FAULT_TPS_VIN_RANGE = 0x0304,
    BZM_RUNTIME_HEALTH_FAULT_TPS_VOUT_RANGE = 0x0305,
    BZM_RUNTIME_HEALTH_FAULT_TPS_TEMPERATURE_RANGE = 0x0306,
    BZM_RUNTIME_HEALTH_FAULT_TPS_IOUT_RANGE = 0x0307,
    BZM_RUNTIME_HEALTH_FAULT_TPS_VOUT_COMMAND = 0x0308,
    BZM_RUNTIME_HEALTH_FAULT_TPS_OVERHEAT = 0x0309,

    BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_UNAVAILABLE = 0x0400,
    BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_MISSING = 0x0401,
    BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_INVALID = 0x0402,
    BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_TRIP = 0x0403,
    BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_STALE = 0x0404,
    BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_BOUNDS = 0x0405,
    BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_CLOCK_UNLOCKED = 0x0406,
    BZM_RUNTIME_HEALTH_FAULT_ASIC_OVERHEAT = 0x0407,

} bzm_runtime_health_fault_t;

typedef struct
{
    bool available;
    bool pgood;
    uint8_t operation;
    uint16_t status_word;
    float vout_command_v;
    bool vout_command_matches_expected;
    float vin_v;
    float vout_v;
    float iout_a;
    float temperature_c;
} bzm_runtime_health_tps_sample_t;

typedef struct
{
    float vin_min_v;
    float vin_max_v;
    float vout_command_v;
    float vout_command_tolerance_v;
    float vout_min_v;
    float vout_max_v;
    float iout_min_a;
    float iout_max_a;
    float temperature_min_c;
    float temperature_max_c;
} bzm_runtime_health_tps_bounds_t;

typedef struct
{
    bool running;

    bool bridge_status_available;
    bzm_bridge_safety_status_t bridge_status;
    bool fan_tach_available;
    uint16_t fan_rpm;
    uint16_t fan_min_rpm;

    bzm_runtime_health_tps_sample_t tps;
    bzm_runtime_health_tps_bounds_t tps_bounds;

    bool telemetry_available;
    bzm_telemetry_store_t telemetry;
    bzm_telemetry_bounds_t telemetry_bounds;
    uint64_t telemetry_now_us;
    uint64_t telemetry_max_age_us;
    /* The board monitor may defer only the finite CH2 absolute-bound
     * decision to its consecutive-sample confirmation state, including a
     * voltage-fault bit carried in that same unchecksummed frame. */
    bool defer_ch2_bounds;
    /* The board monitor may defer only the unchecksummed combined PLL
     * telemetry bit to its consecutive-sample confirmation state. Startup
     * PLL register and lock checks remain strict. */
    bool defer_clock_locks;

} bzm_runtime_health_input_t;

typedef struct
{
    bzm_runtime_health_status_t status;
    bzm_runtime_health_fault_t fault;
    char detail[BZM_RUNTIME_HEALTH_DETAIL_LENGTH];
} bzm_runtime_health_result_t;

/* Evaluate live hardware while running. Stopped ASICs have no live telemetry. */
bzm_runtime_health_result_t bzm_runtime_health_evaluate(const bzm_runtime_health_input_t *input);

#endif /* BZM_RUNTIME_HEALTH_H */
