#include "esp_event.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"

#include "main.h"
#include "influx_task.h"
#include "asic_result_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "system.h"
#include "http_server.h"
#include "nvs_config.h"
#include "serial.h"
#include "stratum_task.h"
#include "i2c_bitaxe.h"
#include "adc.h"
#include "nvs_device.h"
#include "self_test.h"
#include "asic.h"
#include "driver/gpio.h"

static GlobalState GLOBAL_STATE = {
    .extranonce_str = NULL,
    .extranonce_2_len = 0,
    .abandon_work = 0,
    .version_mask = 0,
    .ASIC_initalized = false,
    .influx_client = NULL
};

static const char * TAG = "bitaxe";

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

    //parse the NVS config into GLOBAL_STATE
    if (NVSDevice_parse_config(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse NVS config");
        return;
    }

    if (ASIC_set_device_model(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Error setting ASIC model");
        return;
    }

    // Optionally hold the boot button
    bool pressed = gpio_get_level(CONFIG_GPIO_BUTTON_BOOT) == 0; // LOW when pressed
    //should we run the self test?
    if (should_test(&GLOBAL_STATE) || pressed) {
        self_test((void *) &GLOBAL_STATE);
        return;
    }

    SYSTEM_init_system(&GLOBAL_STATE);

    char * wifi_ssid;
    char * wifi_pass;
    char * hostname;

    NVSDevice_get_wifi_creds(&GLOBAL_STATE, &wifi_ssid, &wifi_pass, &hostname);

    // init AP and connect to wifi
    wifi_init(&GLOBAL_STATE, wifi_ssid, wifi_pass, hostname);

    SYSTEM_init_peripherals(&GLOBAL_STATE);

    xTaskCreate(POWER_MANAGEMENT_task, "power management", 8192, (void *) &GLOBAL_STATE, 10, NULL);

    //start the API for AxeOS
    start_rest_server((void *) &GLOBAL_STATE);

    while (!GLOBAL_STATE.SYSTEM_MODULE.is_connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "Connected to SSID: %s", wifi_ssid);

    free(wifi_ssid);
    free(wifi_pass);
    free(hostname);

    GLOBAL_STATE.new_stratum_version_rolling_msg = false;

    wifi_softap_off();

    queue_init(&GLOBAL_STATE.stratum_queue);
    queue_init(&GLOBAL_STATE.ASIC_jobs_queue);

    SERIAL_init();

    if (ASIC_init(&GLOBAL_STATE) == 0) {
        GLOBAL_STATE.SYSTEM_MODULE.asic_status = "Chip count 0";
        ESP_LOGE(TAG, "Chip count 0");
        return;
    }

    SERIAL_set_baud(ASIC_set_max_baud(&GLOBAL_STATE));
    SERIAL_clear_buffer();

    GLOBAL_STATE.ASIC_initalized = true;

    xTaskCreate(stratum_task, "stratum admin", 8192, (void *) &GLOBAL_STATE, 5, NULL);
    xTaskCreate(create_jobs_task, "stratum miner", 8192, (void *) &GLOBAL_STATE, 10, NULL);
    xTaskCreate(ASIC_task, "asic", 8192, (void *) &GLOBAL_STATE, 10, NULL);
    xTaskCreate(ASIC_result_task, "asic result", 8192, (void *) &GLOBAL_STATE, 15, NULL);

    // Initialize InfluxDB client if enabled and schedule the stats reporting task
    bool influx_enabled = nvs_config_get_u16(NVS_CONFIG_INFLUX_ENABLED, CONFIG_INFLUXDB_ENABLED);

    if (influx_enabled) {
        bool influx_success = influx_init_and_start(
            &GLOBAL_STATE,
            nvs_config_get_string(NVS_CONFIG_INFLUX_HOST, CONFIG_INFLUXDB_HOST),
            nvs_config_get_u16(NVS_CONFIG_INFLUX_PORT, CONFIG_INFLUXDB_PORT),
            nvs_config_get_string(NVS_CONFIG_INFLUX_TOKEN, CONFIG_INFLUXDB_TOKEN),
            nvs_config_get_string(NVS_CONFIG_INFLUX_BUCKET, CONFIG_INFLUXDB_BUCKET),
            nvs_config_get_string(NVS_CONFIG_INFLUX_ORG, CONFIG_INFLUXDB_ORG),
            nvs_config_get_string(NVS_CONFIG_INFLUX_MEASUREMENT, CONFIG_INFLUXDB_MEASUREMENT)
        );

        if (!influx_success) {
            ESP_LOGE(TAG, "Failed to initialize InfluxDB client");
        } else {
            // Create the InfluxDB stats reporting task only if initialization was successful
            xTaskCreate(influx_task, "influx task", 4096, (void *) &GLOBAL_STATE, 5, NULL);
        }
    }
}
