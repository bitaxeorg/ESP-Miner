#include "esp_log.h"
#include "esp_psram.h"
#include "adc.h"
#include "asic.h"
#include "asic_reset.h"
#include "asic_result_task.h"
#include "asic_task.h"
#include "asic_task_module.h"
#include "connect.h"
#include "create_jobs_task.h"
#include "device_config.h"
#include "http_server.h"
#include "i2c_bitaxe.h"
#include "nvs_device.h"
#include "self_test.h"
#include "serial.h"
#include "statistics_task.h"
#include "stratum_task.h"
#include "system.h"
#include "power_management_module.h"
#include "power_management_task.h"
#include "system_module.h"
#include "self_test_module.h"
#include "mining_module.h"
#include "device_config.h"
#include "display.h"
#include "work_queue.h"
#include "wifi_module.h"
#include "pool_module.h"
#include "state_module.h"

SystemModule SYSTEM_MODULE;
PowerManagementModule POWER_MANAGEMENT_MODULE;
DeviceConfig DEVICE_CONFIG;
DisplayConfig DISPLAY_CONFIG;
AsicTaskModule ASIC_TASK_MODULE;
SelfTestModule SELF_TEST_MODULE;
StatisticsModule STATISTICS_MODULE;
mining_queues MINING_MODULE;
WifiSettings WIFI_MODULE;
PoolModule POOL_MODULE;
StateModule STATE_MODULE;

static const char * TAG = "bitaxe";

void app_main(void)
{
    ESP_LOGI(TAG, "Welcome to the bitaxe - FOSS || GTFO!");

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "No PSRAM available on ESP32 device!");
        STATE_MODULE.psram_is_available = false;
    } else {
        STATE_MODULE.psram_is_available = true;
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

    if (device_config_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init device config");
        return;
    }

    if (self_test()) return;

    SYSTEM_init_system();
    statistics_init();

    // init AP and connect to wifi
    wifi_init();

    SYSTEM_init_peripherals();

    xTaskCreate(POWER_MANAGEMENT_task, "power management", 8192, NULL,10, NULL);
    // start the API for AxeOS
    start_rest_server();

    while (!WIFI_MODULE.is_connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    queue_init(&MINING_MODULE.stratum_queue);
    queue_init(&MINING_MODULE.ASIC_jobs_queue);

    if (asic_reset() != ESP_OK) {
        STATE_MODULE.asic_status = "ASIC reset failed";
        ESP_LOGE(TAG, "ASIC reset failed!");
        return;
    }

    SERIAL_init();

    ESP_LOGE(TAG, "ASIC_init id:%i count:%i diff:%i", DEVICE_CONFIG.family.asic.id, DEVICE_CONFIG.family.asic_count,
             DEVICE_CONFIG.family.asic.difficulty);
    if (ASIC_init(POWER_MANAGEMENT_MODULE.frequency_value, DEVICE_CONFIG.family.asic_count,
                  DEVICE_CONFIG.family.asic.difficulty) == 0) {
        STATE_MODULE.asic_status = "Chip count 0";
        ESP_LOGE(TAG, "Chip count 0");
        return;
    }

    SERIAL_set_baud(ASIC_set_max_baud());
    SERIAL_clear_buffer();

    STATE_MODULE.ASIC_initalized = true;
    xTaskCreate(stratum_task, "stratum admin", 8192, NULL, 5, NULL);
    xTaskCreate(create_jobs_task, "stratum miner", 8192, NULL, 10, NULL);
    xTaskCreate(ASIC_task, "asic", 8192, NULL, 10, NULL);
    xTaskCreate(ASIC_result_task, "asic result", 8192, NULL, 15, NULL);
    xTaskCreate(statistics_task, "statistics", 8192, NULL, 3, NULL);
}
