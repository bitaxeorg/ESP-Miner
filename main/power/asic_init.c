#include "asic_init.h"
#include "global_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "asic.h"
#include "asic_common.h"
#include "serial.h"
#include "asic_reset.h"

static const char *TAG = "asic_init";

#define INIT_RESET_LOW_MS 100
#define INIT_RESET_RELEASE_MS 100

#define RETRY_RESET_LOW_MS 10
#define RETRY_RESET_RELEASE_MS 100

#define ASIC_UART_SETTLE_MS 20

uint8_t asic_initialize(GlobalState *GLOBAL_STATE, asic_init_mode_t mode, uint32_t stabilization_delay_ms)
{
    const char *mode_str = (mode == ASIC_INIT_COLD_BOOT) ? "cold boot" : "recovery";
    const uint8_t max_attempts = GLOBAL_STATE->DEVICE_CONFIG.family.asic.init_retry_attempts > 0
                                     ? GLOBAL_STATE->DEVICE_CONFIG.family.asic.init_retry_attempts
                                     : 1;
    ESP_LOGI(TAG, "Starting ASIC initialization (%s mode)", mode_str);

    if (asic_reset(INIT_RESET_LOW_MS, INIT_RESET_RELEASE_MS) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC reset failed";
        ESP_LOGE(TAG, "ASIC reset failed!");
        return 0;
    }

    // Check actual UART state for safety
    bool uart_initialized = SERIAL_is_initialized();
    
    // Verify mode matches actual state
    if (mode == ASIC_INIT_COLD_BOOT && uart_initialized) {
        ESP_LOGW(TAG, "Cold boot mode but UART already initialized - will reset baud only");
    } else if (mode == ASIC_INIT_RECOVERY && !uart_initialized) {
        ESP_LOGW(TAG, "Recovery mode but UART not initialized - will do full init");
    }
    
    // Use actual state for decision, not just mode
    if (!uart_initialized) {
        // Fresh boot - full UART initialization
        ESP_LOGI(TAG, "Performing full UART initialization");
        SERIAL_init();
    } else {
        // Live recovery - ASIC was reset, UART needs baud reset to 115200
        // This preserves the running system and avoids reboot
        ESP_LOGI(TAG, "UART already initialized, resetting baud to %d", UART_FREQ);
        SERIAL_set_baud(UART_FREQ);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    uint8_t chip_count = 0;
    for (uint8_t attempt = 1; attempt <= max_attempts; attempt++) {
        if (attempt > 1) {
            ESP_LOGW(TAG, "Resetting and re-probing %s chain (%u/%u)",
                     GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, attempt, max_attempts);
            if (SERIAL_set_baud(UART_FREQ) != ESP_OK ||
                asic_reset(RETRY_RESET_LOW_MS, RETRY_RESET_RELEASE_MS) != ESP_OK) {
                GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC retry reset failed";
                ESP_LOGE(TAG, "%s retry reset failed", GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
                return 0;
            }
        }

        SERIAL_clear_buffer();
        vTaskDelay(pdMS_TO_TICKS(ASIC_UART_SETTLE_MS));

        ESP_LOGI(TAG, "Detecting ASIC chips... attempt %u/%u", attempt, max_attempts);
        clear_asic_chain_error();
        chip_count = ASIC_init(GLOBAL_STATE);

        if (chip_count > 0) {
            break;
        }
    }
    
    if (chip_count == 0) {
        const char *chain_error = get_asic_chain_error();
        ESP_LOGE(TAG, "ASIC initialization failed - chip chain detection failed");
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = chain_error != NULL ? chain_error : "ASIC chain detection failed";
        return 0;
    }

    int max_baud = ASIC_set_max_baud(GLOBAL_STATE);
    if (max_baud == 0 || SERIAL_set_baud(max_baud) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC UART configuration failed";
        ESP_LOGE(TAG, "Failed to configure ASIC UART");
        return 0;
    }
    SERIAL_clear_buffer();

    GLOBAL_STATE->ASIC_initalized = true;
    
    if (stabilization_delay_ms > 0) {
        ESP_LOGI(TAG, "Waiting %u ms for tasks to stabilize...", stabilization_delay_ms);
        vTaskDelay(pdMS_TO_TICKS(stabilization_delay_ms));
    }

    ESP_LOGI(TAG, "ASIC initialized successfully with %d chip(s) (%s mode)", chip_count, mode_str);
    return chip_count;
}
