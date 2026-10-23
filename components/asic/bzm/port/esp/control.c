#include "bzm/control.h"
#include "bzm/esp.h"
#include "bzm/gpio.h"
#include <string.h>
#include "sdkconfig.h"

static bool initialized;

esp_err_t BZM_control_init(const char *board_version)
{
    if (board_version == NULL || strcmp(board_version, "1004") != 0) return ESP_ERR_NOT_SUPPORTED;
    if (!bzm_platform_esp_init()) return ESP_FAIL;
    if (initialized) return ESP_OK;
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    /* GPIO43/44 are wired to the RP2040. */
    return ESP_ERR_INVALID_STATE;
#endif
    esp_err_t err = bzm_gpio_init();
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
