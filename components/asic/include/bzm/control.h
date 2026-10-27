#ifndef BZM_CONTROL_H
#define BZM_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#include "bzm/controls.h"

typedef struct bzm_control_pins {
    int reset, trip, fan_pwm, fan_tach, core_enable, pgood, five_volt_enable;
} bzm_control_pins_t;

// NULL reuses controls already initialized from device configuration.
esp_err_t BZM_control_init(const char *board_version, const bzm_control_pins_t *pins);
esp_err_t BZM_control_get_safety_status(bzm_control_status_t *status);
esp_err_t BZM_control_clear_safety_fault(bzm_control_status_t *status);
esp_err_t BZM_control_safe_off(bzm_control_status_t *status);
esp_err_t BZM_control_set_5v_enabled(bool enabled);
esp_err_t BZM_control_set_asic_reset(bool high);
esp_err_t BZM_control_set_fan_percent(float percent);
esp_err_t BZM_control_get_fan_rpm(uint16_t *rpm);
esp_err_t BZM_control_set_core_enabled(bool enabled);

#endif
