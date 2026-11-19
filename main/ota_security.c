#include "ota_security.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs_config.h"

#define OTA_UNLOCK_DURATION_US (120LL * 1000000)

static portMUX_TYPE unlock_mutex = portMUX_INITIALIZER_UNLOCKED;
static int64_t unlock_deadline_us;

void ota_security_unlock_from_button(void)
{
    portENTER_CRITICAL(&unlock_mutex);
    unlock_deadline_us = esp_timer_get_time() + OTA_UNLOCK_DURATION_US;
    portEXIT_CRITICAL(&unlock_mutex);
}

void ota_security_lock(void)
{
    portENTER_CRITICAL(&unlock_mutex);
    unlock_deadline_us = 0;
    portEXIT_CRITICAL(&unlock_mutex);
}

uint32_t ota_security_remaining_seconds(void)
{
    portENTER_CRITICAL(&unlock_mutex);
    int64_t remaining_us = unlock_deadline_us - esp_timer_get_time();
    portEXIT_CRITICAL(&unlock_mutex);
    return remaining_us > 0 ? (uint32_t)((remaining_us + 999999) / 1000000) : 0;
}

bool ota_security_is_update_allowed(void)
{
    return !nvs_config_get_bool(NVS_CONFIG_OTA_BUTTON_REQUIRED) || ota_security_remaining_seconds() > 0;
}
