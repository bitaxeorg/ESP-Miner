#include "mining_control.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "vcore.h"
#include "asic_reset.h"
#include "asic_init.h"
#include "nvs_config.h"
#include "frequency_transition_bmXX.h"

static const char * TAG = "mining_control";

void mining_stop(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Stopping mining");

    // Cut ASIC power and hold in reset
    VCORE_set_voltage(GLOBAL_STATE, 0.0f);
    asic_hold_reset_low();

    // Mark uninitialized immediately so tasks stop issuing UART commands
    GLOBAL_STATE->ASIC_initalized = false;

    // Reset the frequency transition tracker so the ramp starts from
    // the chip's post-reset default (50 MHz) rather than the stale
    // pre-reset frequency, which would cause do_frequency_transition
    // to skip the ramp entirely (seeing current == target).
    reset_frequency_transition_state();

    // Give tasks time to complete any in-progress UART operation
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Flush any stale data from the UART buffers
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Mining stopped");
}

uint8_t mining_start(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Starting mining");

    // Restore voltage from NVS
    uint16_t voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
    VCORE_set_voltage(GLOBAL_STATE, (double) voltage / 1000.0);

    // Wait for voltage to stabilize before touching the ASIC
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Clear any accumulated UART garbage before init
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Stabilization delay of 2000ms prevents race conditions where tasks are
    // just starting to use the ASIC while power management tries to change frequency
    uint8_t chip_count = asic_initialize(GLOBAL_STATE, ASIC_INIT_RECOVERY, 2000);

    if (chip_count > 0) {
        ESP_LOGI(TAG, "Mining started successfully (%d chip(s))", chip_count);
    } else {
        ESP_LOGE(TAG, "Mining start failed - ASIC not detected");
    }

    return chip_count;
}
