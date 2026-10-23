#ifndef BZM_POWER_H
#define BZM_POWER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "bzm/status.h"

#define BZM_TPS546_FIXED_VOUT_V 2.8f
#define BZM_TPS546_MIN_VOUT_V 2.1f
/* Four series BZM chips are limited to 0.81 V each on average (3.24 V
 * aggregate). Keep 40 mV of command margin below that absolute limit. */
#define BZM_TPS546_MAX_VOUT_V 3.2f
#define BZM_TPS546_VOUT_TOLERANCE_V 0.001f
/* Extended VOUT_MODE uses a 1.953125 mV ULINEAR16 LSB and the TPS helper
 * truncates when encoding 2.800 V, yielding a decoded 2.798828 V. Exact raw
 * word equality is enforced separately; this bound covers representation
 * error only. */
#define BZM_TPS546_VOUT_READBACK_TOLERANCE_V 0.002f
#define BZM_TPS546_VOUT_OPERATING_TOLERANCE_V 0.15f

bool bzm_power_runtime_voltage_is_allowed(float volts);
bool bzm_power_resolve_user_voltage(uint16_t millivolts, float *volts);

typedef struct {
    bzm_status_t (*set_5v_enabled)(void *context, bool enabled);
    bzm_status_t (*set_regulator_enabled)(void *context, bool enabled);
    bzm_status_t (*set_vout)(void *context, float volts);
    bzm_status_t (*validate_power)(void *context, float expected_vout);
    void (*delay_ms)(void *context, uint32_t delay_ms);
} bzm_power_ops_t;

bzm_status_t bzm_power_set_rail_enabled(const bzm_power_ops_t *ops,
                                     void *context, bool enabled);
bzm_status_t bzm_power_set_runtime_voltage(const bzm_power_ops_t *ops,
                                        void *context, float volts);

#ifdef __cplusplus
}
#endif

#endif // BZM_POWER_H
