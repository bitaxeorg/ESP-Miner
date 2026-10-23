#ifndef BZM_GPIO_H
#define BZM_GPIO_H

#include "bzm/control.h"

/* bitaxeBonanza tag 1004x-PCBA-order, commit a0c96ae0. */
#define BZM_GPIO_RESET 1
#define BZM_GPIO_TRIP 2
#define BZM_GPIO_FAN_PWM 8
#define BZM_GPIO_FAN_TACH 9
#define BZM_GPIO_CORE_ENABLE 10
#define BZM_GPIO_PGOOD 11
#define BZM_GPIO_5V_ENABLE 21

esp_err_t bzm_gpio_init(void);
esp_err_t bzm_gpio_get_safety_status(bzm_control_status_t *status);
esp_err_t bzm_gpio_clear_safety_fault(bzm_control_status_t *status);
esp_err_t bzm_gpio_safe_off(bzm_control_status_t *status);
esp_err_t bzm_gpio_set_5v_enabled(bool enabled);
esp_err_t bzm_gpio_set_asic_reset(bool high);
esp_err_t bzm_gpio_set_core_enabled(bool enabled);
esp_err_t bzm_gpio_set_fan_percent(float percent);
esp_err_t bzm_gpio_get_fan_rpm(uint16_t *rpm);

#endif
