#ifndef BZM_GPIO_H
#define BZM_GPIO_H

#include "bzm/control.h"

esp_err_t bzm_gpio_init(const bzm_control_pins_t *pins);
esp_err_t bzm_gpio_get_safety_status(bzm_control_status_t *status);
esp_err_t bzm_gpio_clear_safety_fault(bzm_control_status_t *status);
esp_err_t bzm_gpio_safe_off(bzm_control_status_t *status);
esp_err_t bzm_gpio_set_5v_enabled(bool enabled);
esp_err_t bzm_gpio_set_asic_reset(bool high);
esp_err_t bzm_gpio_set_core_enabled(bool enabled);
esp_err_t bzm_gpio_set_fan_percent(float percent);
esp_err_t bzm_gpio_get_fan_rpm(uint16_t *rpm);

#endif
