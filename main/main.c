#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"

#include "asic_result_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "statistics_task.h"
#include "system.h"
#include "http_server.h"
#include "serial.h"
#include "stratum_task.h"
#include "i2c_bitaxe.h"
#include "adc.h"
#include "nvs_device.h"
#include "self_test.h"
#include "asic.h"
#include "bap/bap.h"
#include "device_config.h"
#include "connect.h"
#include "asic_reset.h"

static GlobalState GLOBAL_STATE;

static const char * TAG = "bitaxe";

// Static task control blocks for PSRAM tasks
static StaticTask_t stratum_tcb, create_jobs_tcb, asic_tcb, asic_result_tcb;

void app_main(void)
{
    ESP_LOGI(TAG, "Welcome to the bitaxe - FOSS || GTFO!");

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "No PSRAM available on ESP32 device!");
        GLOBAL_STATE.psram_is_available = false;
    } else {
        GLOBAL_STATE.psram_is_available = true;
    }

    // Init I2C
    ESP_ERROR_CHECK(i2c_bitaxe_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    //wait for I2C to init
    vTaskDelay(100 / portTICK_PERIOD_MS);

    //Init ADC
    ADC_init();

    //initialize the ESP32 NVS
    if (NVSDevice_init() != ESP_OK){
        ESP_LOGE(TAG, "Failed to init NVS");
        return;
    }

    if (device_config_init(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init device config");
        return;
    }

    if (self_test(&GLOBAL_STATE)) return;

    SYSTEM_init_system(&GLOBAL_STATE);
    statistics_init(&GLOBAL_STATE);

    // init AP and connect to wifi
    wifi_init(&GLOBAL_STATE);

    SYSTEM_init_peripherals(&GLOBAL_STATE);

    xTaskCreate(POWER_MANAGEMENT_task, "power management", 8192, (void *) &GLOBAL_STATE, 10, NULL);

    //start the API for AxeOS
    start_rest_server((void *) &GLOBAL_STATE);

    // Initialize BAP interface
    esp_err_t bap_ret = BAP_init(&GLOBAL_STATE);
    if (bap_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BAP interface: %d", bap_ret);
        // Continue anyway, as BAP is not critical for core functionality
    } else {
        ESP_LOGI(TAG, "BAP interface initialized successfully");
    }

    while (!GLOBAL_STATE.SYSTEM_MODULE.is_connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    queue_init(&GLOBAL_STATE.stratum_queue);
    queue_init(&GLOBAL_STATE.ASIC_jobs_queue);

    if (asic_reset() != ESP_OK) {
        GLOBAL_STATE.SYSTEM_MODULE.asic_status = "ASIC reset failed";
        ESP_LOGE(TAG, "ASIC reset failed!");
        return;
    }

    SERIAL_init();

    if (ASIC_init(&GLOBAL_STATE) == 0) {
        GLOBAL_STATE.SYSTEM_MODULE.asic_status = "Chip count 0";
        ESP_LOGE(TAG, "Chip count 0");
        return;
    }

    SERIAL_set_baud(ASIC_set_max_baud(&GLOBAL_STATE));
    SERIAL_clear_buffer();

    GLOBAL_STATE.ASIC_initalized = true;

    // Allocate stacks in PSRAM for tasks that don't access flash
    if (GLOBAL_STATE.psram_is_available) {
        uint8_t *stratum_stack = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
        uint8_t *create_jobs_stack = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
        uint8_t *asic_stack = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
        uint8_t *asic_result_stack = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);

        if (stratum_stack && create_jobs_stack && asic_stack && asic_result_stack) {
            ESP_LOGI(TAG, "Allocated task stacks in PSRAM");
            xTaskCreateStatic(stratum_task, "stratum admin", 8192, (void *) &GLOBAL_STATE, 5, stratum_stack, &stratum_tcb);
            xTaskCreateStatic(create_jobs_task, "stratum miner", 8192, (void *) &GLOBAL_STATE, 10, create_jobs_stack, &create_jobs_tcb);
            xTaskCreateStatic(ASIC_task, "asic", 8192, (void *) &GLOBAL_STATE, 10, asic_stack, &asic_tcb);
            xTaskCreateStatic(ASIC_result_task, "asic result", 8192, (void *) &GLOBAL_STATE, 15, asic_result_stack, &asic_result_tcb);
        } else {
            ESP_LOGE(TAG, "Failed to allocate some PSRAM stacks, falling back to internal RAM");
            xTaskCreate(stratum_task, "stratum admin", 8192, (void *) &GLOBAL_STATE, 5, NULL);
            xTaskCreate(create_jobs_task, "stratum miner", 8192, (void *) &GLOBAL_STATE, 10, NULL);
            xTaskCreate(ASIC_task, "asic", 8192, (void *) &GLOBAL_STATE, 10, NULL);
            xTaskCreate(ASIC_result_task, "asic result", 8192, (void *) &GLOBAL_STATE, 15, NULL);
        }
    } else {
        ESP_LOGI(TAG, "PSRAM not available, creating tasks in internal RAM");
        xTaskCreate(stratum_task, "stratum admin", 8192, (void *) &GLOBAL_STATE, 5, NULL);
        xTaskCreate(create_jobs_task, "stratum miner", 8192, (void *) &GLOBAL_STATE, 10, NULL);
        xTaskCreate(ASIC_task, "asic", 8192, (void *) &GLOBAL_STATE, 10, NULL);
        xTaskCreate(ASIC_result_task, "asic result", 8192, (void *) &GLOBAL_STATE, 15, NULL);
    }

    xTaskCreate(statistics_task, "statistics", 8192, (void *) &GLOBAL_STATE, 3, NULL);
}
