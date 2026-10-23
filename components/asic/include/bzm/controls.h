#ifndef BZM_CONTROLS_H
#define BZM_CONTROLS_H

#include "bzm/platform.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BZM_CONTROL_STATE_SAFE_OFF = 0,
    BZM_CONTROL_STATE_CONTROLLED,
    BZM_CONTROL_STATE_FAULT_LATCHED,
} bzm_control_state_t;

typedef enum {
    BZM_CONTROL_FAULT_NONE = 0,
    BZM_CONTROL_FAULT_ASIC_TRIP,
    BZM_CONTROL_FAULT_STATUS_INVALID,
} bzm_control_fault_t;

/* Readback of platform-owned Bonanza controls; failures leave valid false. */
typedef struct {
    bool valid;
    bzm_control_state_t state;
    bzm_control_fault_t fault;
    bool core_enabled;
    bool five_volt_enabled;
    bool asic_reset_asserted;
    bool fan_full;
    uint8_t fan_percent;
    bool trip_input_asserted;
} bzm_control_status_t;

typedef struct {
    bzm_status_t (*init)(void *context, const char * board_version);
    bzm_status_t (*get_safety_status)(void *context, bzm_control_status_t * status);
    bzm_status_t (*clear_safety_fault)(void *context, bzm_control_status_t * status);
    bzm_status_t (*safe_off)(void *context, bzm_control_status_t * status);
    bzm_status_t (*set_5v_enabled)(void *context, bool enabled);
    bzm_status_t (*set_asic_reset)(void *context, bool high);
    bzm_status_t (*set_fan_percent)(void *context, float percent);
    bzm_status_t (*get_fan_rpm)(void *context, uint16_t * rpm);
    bzm_status_t (*set_core_enabled)(void *context, bool enabled);
} bzm_control_ops_t;

/* Bind before driver startup; the platform owns pins, rails and fault latching. */
bool bzm_control_bind(const bzm_control_ops_t *ops, void *context);

bool bzm_control_status_allows_fault_clear(const bzm_control_status_t *status);
bzm_status_t bzm_control_init(const char *board_version);
bzm_status_t bzm_control_get_safety_status(bzm_control_status_t *status);
bzm_status_t bzm_control_clear_safety_fault(bzm_control_status_t *status);
bzm_status_t bzm_control_safe_off(bzm_control_status_t *status);
bzm_status_t bzm_control_set_5v_enabled(bool enabled);
bzm_status_t bzm_control_set_asic_reset(bool high);
bzm_status_t bzm_control_pulse_asic_reset(void);
bzm_status_t bzm_control_set_fan_percent(float percent);
bzm_status_t bzm_control_get_fan_rpm(uint16_t *rpm);
bzm_status_t bzm_control_set_core_enabled(bool enabled);

#endif
