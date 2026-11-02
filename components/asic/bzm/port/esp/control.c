#include "bzm/control.h"
#include "bzm/esp.h"
#include "bzm/gpio.h"
#include "bzm/bridge_reset.h"
#include "driver/gpio.h"
#include <string.h>

static bool initialized;
static BzmPins bridge_pins;

esp_err_t BZM_control_init(const char *board_version, const BzmPins *pins)
{
    if (board_version == NULL || strcmp(board_version, "1004") != 0) return ESP_ERR_NOT_SUPPORTED;
    if (initialized) {
        if (pins == NULL) return ESP_OK;
        if (pins->bridge_swclk != bridge_pins.bridge_swclk || pins->bridge_swdio != bridge_pins.bridge_swdio)
            return ESP_ERR_INVALID_STATE;
        return bzm_gpio_init(pins);
    }
    if (pins == NULL) return ESP_ERR_INVALID_ARG;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pins->bridge_swclk) || !GPIO_IS_VALID_OUTPUT_GPIO(pins->bridge_swdio) ||
        pins->bridge_swclk == pins->bridge_swdio) return ESP_ERR_INVALID_ARG;
    if (!bzm_platform_esp_init()) return ESP_FAIL;
    // Disconnect UART0 console pins from the bridge; future use could forward bridge stats or logs.
    esp_err_t err = gpio_reset_pin(GPIO_NUM_43);
    if (err != ESP_OK) return err;
    err = gpio_reset_pin(GPIO_NUM_44);
    if (err != ESP_OK) return err;
    err = bzm_gpio_init(pins);
    if (err == ESP_OK) bridge_pins = *pins;
    initialized = err == ESP_OK;
    return err;
}

#define STATUS_CALL(name) \
    esp_err_t BZM_control_##name(bzm_control_status_t *status) \
    { \
        if (status == NULL) return ESP_ERR_INVALID_ARG; \
        *status = (bzm_control_status_t){.fault = BZM_CONTROL_FAULT_STATUS_INVALID}; \
        if (!initialized) return ESP_ERR_INVALID_STATE; \
        return bzm_gpio_##name(status); \
    }
STATUS_CALL(get_safety_status)
STATUS_CALL(clear_safety_fault)
STATUS_CALL(safe_off)
#undef STATUS_CALL

#define CONTROL_CALL(name, type) \
    esp_err_t BZM_control_##name(type value) \
    { \
        if (!initialized) return ESP_ERR_INVALID_STATE; \
        return bzm_gpio_##name(value); \
    }
CONTROL_CALL(set_5v_enabled, bool)
CONTROL_CALL(set_asic_reset, bool)
CONTROL_CALL(set_fan_percent, float)
CONTROL_CALL(get_fan_rpm, uint16_t *)
#undef CONTROL_CALL

esp_err_t BZM_control_set_core_enabled(bool enabled)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    return bzm_gpio_set_core_enabled(enabled);
}

esp_err_t BZM_control_reset_bridge(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    bzm_control_status_t status;
    esp_err_t err = BZM_control_get_safety_status(&status);
    if (err != ESP_OK || !status.valid || status.fault != BZM_CONTROL_FAULT_NONE ||
        status.trip_input_asserted || !status.asic_reset_asserted) return ESP_ERR_INVALID_STATE;
    if (status.core_enabled && gpio_get_level(bridge_pins.core_power_good) == 0) return ESP_ERR_INVALID_STATE;
    err = bzm_bridge_reset(bridge_pins.bridge_swclk, bridge_pins.bridge_swdio);
    if (err != ESP_OK) return err;
    err = BZM_control_get_safety_status(&status);
    return err == ESP_OK && status.valid && status.fault == BZM_CONTROL_FAULT_NONE &&
        !status.trip_input_asserted && status.asic_reset_asserted &&
        (!status.core_enabled || gpio_get_level(bridge_pins.core_power_good) != 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
